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
