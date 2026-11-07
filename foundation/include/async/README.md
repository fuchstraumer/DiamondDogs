# foundation/async

## FrameHeap: where coroutine frames live

Each `Scheduler` owns a `FrameHeap`. A `FrameHeap` is one mimalloc heap (`mi_heap_new`) plus the counters of its frames.

How a frame is allocated:

- `BasePromise::operator new` calls `FrameHeap::AllocateFrame`. This function reads a thread-local pointer, the
  "current frame heap".
- If the pointer is set, the frame comes from that heap, through the owner thread's `mi_theap_t` (a cached handle, so
  there is no thread-local lookup in mimalloc). If the pointer is null, the frame comes from `mi_malloc`.
- Every frame has a 16-byte header before it. The header stores the `FrameHeap*` that counted the frame (null for the
  fallback). `operator delete` reads the header to update the right counters, then calls `mi_free_size` on the block.
  The header keeps the frame 16-byte aligned, which is all that `alignof(std::max_align_t)` needs here.
- The header costs 16 bytes for each frame. `mi_heap_of` can map a pointer to a `mi_heap_t*`, but not to our
  `FrameHeap*`, and a table for that mapping needs a lock. The header needs no lock.
- Global `::operator new` and `::operator delete` are mimalloc too (`foundation/src/miMallocOverride.cpp`, linked into
  every executable that uses foundation). `malloc` and `free` are still the C runtime's.

When the pointer is set:

- The `Scheduler` constructor binds its heap to the constructing thread. That thread is the owner thread. The
  constructor binds, and not the first call, because the first root's frame is created before `Spawn`.
- Binding remembers the previously current heap. The destructor restores it.
- Two schedulers on one thread nest. The one constructed last owns the routing, and schedulers must be destroyed in
  reverse order. Destroying them out of order asserts.
- `Tick` rebinds. It makes its own heap the current heap for its duration and restores the previous heap at the end
  (two thread-local writes). Ticking an older scheduler while a newer one lives on the same thread allocates the
  children of the older scheduler in the older heap. The root frame is different: `Spawn` runs outside `Tick`, so the
  heap that is current at that time serves the root.

Counters:

- `FrameHeap::Snapshot()` and `Scheduler::FrameStatistics()` return the live frames, live bytes, peak live bytes and
  total frames of one heap.
- `foundation::FrameStatistics()` returns the same counters for every frame in the process, routed or not.
- The counters are relaxed atomics, because another thread can free a frame.
- The counters exist only when `FOUNDATION_ASYNC_FRAME_STATISTICS` is 1. CMake sets it to 0 in Release and MinSizeRel,
  and to 1 in the other configurations (`NDEBUG` cannot decide this, because RelWithDebInfo defines it and keeps the
  counters). When it is 0, the allocation path has no atomics, and both snapshot functions return zeros.

Limits:

- A frame must not outlive its `FrameHeap`. The header of the frame points to the heap. The destructor uses
  `mi_heap_delete`, so the memory stays valid, but a later free would update dead counters. In Debug, the destructor
  prints a line to `stderr` when frames are still alive.
- A frame is never over-aligned. There is no `align_val_t` overload. A static assert checks this.
- Cost: each frame updates two sets of atomic counters (process and heap). In a Release build, three runs of one
  million small tasks each, this made the scheduler heap about 8 ns slower for each frame (27 ns against 19 ns). With
  the heap counters removed, both measured the same. Release builds now compile the counters out.

## Timeline failure

A `Timeline` has a terminal failed state, for a signaler that can never signal again (a lost GPU device).

- `Fail()` is called by the signaler only, like `Signal`. `Failed()` reads the flag with acquire semantics. The flag
  and the value share one atomic word (the top bit is the flag), so that one `notify_all` wakes a blocked `Wait`.
  A timeline value therefore has 63 bits.
- A parked frame resumes on the first `Tick` that sees its target reached or its timeline failed. `co_await
  timeline.Until(n)` returns `TimelineWait::Reached` or `TimelineWait::Failed`, and the result is `[[nodiscard]]`.
  Check it: after `Failed`, the value that the frame waited for was never reached.
- `Wait` returns the same enum, and returns `Failed` instead of blocking forever.
- `Fail` never changes what `Reached(n)` says. A target that was reached before the failure still gives `Reached`.
- `rhi::GpuTimeline::Poll` calls `Fail` once when `vkGetSemaphoreCounterValue` fails, and prints one line. Later polls
  do nothing.

## Post and Yield: the ready list

`Scheduler::Post(handle)` puts a suspended frame on the ready list. `co_await foundation::Yield()` (in `Yield.hpp`)
gives the frame back until the next `Tick`.

Order of one `Tick`:

1. Poll the sources.
2. Resume the parked frames whose target is reached.
3. Start the spawned roots.
4. Run the ready list. Frames that yielded in the previous `Tick` go first, in the order they yielded. Then the posted
   frames go, in the order they were posted. A resumed frame can post again, so the list is drained until it is empty.
5. Destroy the finished roots.

Rules:

- `Post` is for the owner thread only. The posted frame resumes later in the same `Tick`, after the running frame
  suspends or ends. `Post` never resumes a frame inline.
- A handle posted outside a `Tick` resumes in the next `Tick`.
- A frame that posts itself each time it resumes never ends the `Tick`. A long computation must call `Yield` to give
  the frame back.
- `Yield` binds its scheduler through `BindOwner`, like `Timeline::Until`. The call site names no scheduler. Awaiting
  it in a coroutine that no scheduler runs asserts.
- A frame that yields in a `Tick` always resumes in a later `Tick`, even when it yields before the ready list runs.
- `Post(handle, waiter)` does the same as `Post(handle)`. `DumpState` uses `waiter` to print the full backtrace of the
  posted frame. Without it, `DumpState` prints one line for the frame.
- `DumpState` lists ready and yielded frames with `ready: N ticks` and `yielded: N ticks`, followed by the frame lines,
  like parked frames.

## Shared<T>: one result, many awaiters (designed 2026-11-06, not built yet)

`Task<T>` has one awaiter. `Shared<T>` wraps a `Task<T>`, so that many coroutines can await one run of it. It is for the
owner thread only: plain fields, no atomics, no locks.

Decisions:

- **Lazy.** The first awaiter starts it. A `Shared` that nobody awaits never runs.
- **It runs to the end.** Cancellation is cooperative, so nothing stops a started `Shared` from outside.
- **Awaiters borrow it.** One owner holds the `Shared` (the executor's in-flight table). There is no reference count.
- **`co_await shared` returns `const T&`.** Every awaiter reads the one stored result. `T` is not void.
- **No allocation for each waiter.** The awaiter object lives in the awaiting frame, and it is the node of the waiter list.
- **An awaiter of a finished `Shared` does not suspend.** `await_ready` is true, and the result returns at once.

### Ordering rules and traps

Each rule says what breaks if the code ignores it.

1. **The result reference is valid only until the awaiter's next suspension.** The owner can destroy the `Shared` at any
   safe point after that. `const T& r = co_await shared; co_await Yield(); Use(r);` reads freed memory, and no compiler
   reports it. To keep a result across a suspension, copy it.
2. **Code that runs inside the `Shared` must never destroy the `Shared`.** The running frame belongs to the `Shared`, so
   this frees the frame while it runs. A task must not erase its own table entry when it finishes.
3. **The owner destroys a `Shared` only at a safe point: `Started() == false`, or `Done()` and `PendingWaiters() == 0`.**
   A waiter counts as pending until its `await_resume` runs. A woken waiter can sit on the ready list after `Done()` is
   true, so `Done()` alone is not enough. The destructor asserts this condition.
4. **Wake order: detach the list, post every waiter but the first, then transfer to the first.** The first waiter runs
   at once, and it can finish and destroy its own frame, which holds a list node. After that transfer, the waking code
   must not touch the list. The other waiters run later in the same `Tick`, in the order they arrived.
5. **A suspended waiter must not be destroyed.** Its frame holds a list node, and the wake would write through a dangling
   pointer. Cooperative cancellation keeps a waiter suspended until the result arrives, so normal work never does this.
   Only teardown can (rule 6).
6. **Teardown: drain first.** Neither destruction order is safe while work is in flight. If the owner of a `Shared` goes
   first, a frame that the scheduler has parked is freed, and `Tick` resumes freed memory. If the scheduler goes first,
   the frames outlive their `FrameHeap`, and the awaiting roots are destroyed while they are on a waiter list. So the
   owner stops new work and ticks the scheduler until every `Shared` is at a safe point. It is then destroyed, before
   the scheduler. A lost device cannot hang this drain, because the timeline fails (see "Timeline failure").
7. **All awaiters share one scheduler.** Awaiting from another scheduler asserts. A wake would post a frame to the wrong
   owner.
8. **The backtrace goes through the first awaiter only.** The frames of the wrapped task name the first awaiter as
   `Awaiting`. `DumpState` shows the other waiters on their own, and it does not show them from the shared work. A frame
   cleared at completion leaves no dangling `Awaiting` pointer.
