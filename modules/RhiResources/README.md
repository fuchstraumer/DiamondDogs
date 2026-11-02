# RhiResources

`ResourceContext` creates and manages buffers, images and samplers, and moves data into them. Any thread can call
it. The context does the work on its own threads and returns a future at once.

## Threads and messages

- A call on `ResourceContext` becomes a message. The message goes onto an MWSR queue, and the call returns its future.
- The **worker thread** owns the EnTT registry and a VMA allocator. It validates each message, creates the objects,
  and writes host-visible data directly.
- The **transfer thread** owns its own allocator, staging buffers, command pools and timeline semaphore. It records
  and submits the uploads, fills and copies that device-local resources need, on the transfer queue. It sends a
  progress message back to the worker each time its completed ticket advances. That push never blocks.
- Every resource uses concurrent sharing across the queue families, so no ownership transfer exists.

## Futures

Each call returns one of three move-only types, declared in `ResourceFuture.hpp`:

| Type | Returned by | `Get()` |
|---|---|---|
| `rhi::ResourceFuture<GraphicsResource>` | `CreateBuffer`, `CreateImage`, `CreateSampler`, `CopyBuffer`, `CopyImage` | The resource |
| `rhi::ResourceFuture<void*>` | `MapBuffer` | The mapped pointer |
| `rhi::OperationFuture` | Every other call | None |

`State()`, `Error()`, `Completion()` and `IsDone()` never block. `ResourceContext::Wait(future, timeout_ns)` is the
blocking call. It returns the state at the end of the wait, so a state that is not `Complete` or `Failed` means a
timeout. Use `rhi::k_WaitForever` for no limit. A `Created` future sleeps on the transfer timeline, not in a spin.

| State | Means |
|---|---|
| `Invalid` | Default-constructed or moved from. |
| `Pending` | The worker has not processed the call yet. |
| `Created` | The object exists, so you can record commands that use it. Its transfer is still in flight. |
| `Complete` | The call is done, and its transfer finished on the GPU. |
| `Failed` | Permanent. `Error()` gives the `rhi::ResourceError`. |

### The future table

A future holds no heap object. It is a pointer to its context's `FutureTable` and a slot index. The table is one
array of `ResourceContextCreateInfo::MaxFutures` slots (default `k_DefaultMaxFutures`, 4096), allocated at
`Initialize`. Each slot is 64 bytes, one cache line: an atomic word with the state, the error and two flag bits,
the ticket, the `GraphicsResource`, and the mapped pointer.

- **Allocation.** The free slots form a Treiber stack: a lock-free linked list whose head is one 64-bit atomic. The
  head packs a 32-bit tag above the 32-bit slot index, so that a pop that read a stale head fails its
  compare-exchange (the ABA problem). Each free slot stores the index of the next free slot.
- **A full table.** Slot 0 is never free. It is `Failed` with `FutureTableFull` from `Initialize` on. When the stack
  is empty, the call gets slot 0, sends no message, and returns at once. The `FutureTableFull` counter goes up. The
  call does not block, because a caller that waits for a slot can wait on itself.
- **Ownership handoff, with no lock.** Two parties touch a live slot: the owner (the future) and the producer (the
  worker or the transfer thread). The last one to finish frees the slot.
  - The owner's destructor sets the `Released` bit with `fetch_or`. If the state it replaced was final, the owner
    frees the slot.
  - The producer writes each state with a compare-exchange loop. If the state it wrote is final and `Released` was
    already set, the producer frees the slot.
  - The two operations are on the same atomic word, so exactly one of them sees the other's write. A future that you
    drop while its upload is on the GPU is safe: the transfer thread frees the slot when the upload completes.
- **No generation.** A future is move-only, and one future owns one slot. No second future can name a slot after it
  is freed, so a stale-slot check has nothing to catch.

**Lifetime rule:** destroy every future before the `ResourceContext` that made it. The table lives in the context.

`FuturesLive` counts the slots in use, including slots whose future was dropped but whose call is still in flight.

### Errors

`rhi::ResourceError` says why a future failed. `rhi::ToString` gives the name. The resolve step returns
`StaleResource`, `WrongResourceKind` or `ResourceRetiring`. The create checks return `InvalidCreateInfo` or
`UnsupportedUsage`. The transfer checks return `MissingTransferUsage`, `InvalidFillRange`, `CopySizeMismatch`,
`CopyShapeMismatch` or `UnsupportedCopy`. The transfer thread returns `StagingFailed`, `RecordingFailed` or
`SubmitFailed`. The timeline calls return `InvalidTimeline`, `TimelineAlreadyRegistered`, `NoFreeTimelineSlot` or
`TimelineNotRegistered`.

## Validation happens on the worker, once

Each message that takes a `GraphicsResource` from a caller resolves it first (`resolveBuffer`, `resolveImage`,
`resolveResource`). The resolve step checks two things:

1. The entity is alive in the registry.
2. The API handle in the `GraphicsResource` is the handle that the registry stores for that entity.

The second check catches a stale `GraphicsResource`. EnTT's 32-bit entity has a 12-bit version, which wraps after
4096 reuses of one index. The handle comparison is a second, independent answer.

The usage checks (transfer source, transfer destination, alignment) also run on the worker, before a message reaches
the transfer system.

A buffer with `DeviceAddressBuffer` usage fails with `UnsupportedUsage` unless the context was created with
`DeviceAddressesEnabled`. Without that flag the allocator has no device address support, and VMA asserts on such a
buffer. The raytracing usages are refused in all cases for now.

## Names

The create infos carry `Name` as a `std::string_view`. `CreateSampler` takes its name from `SamplerCreateInfo::Name`. `ResourceContext` copies the name into the message on the
calling thread, into a fixed 64-byte `DebugName` (63 chars and a length byte), so the caller's string only has to live until the call returns. Names
longer than 62 characters are truncated. Views, samplers and allocations are named after their resource, with a
suffix such as `_image_view`. Names reach Vulkan only when validation is on.

## Host-visible writes are not ordered against the GPU

`SetBufferData` on a host-visible buffer copies the data at once and reports `Complete`. Nothing orders that write
against GPU work that reads the buffer. If a frame in flight reads the buffer, the GPU sees a race. Keep one copy of
per-frame data for each frame in flight, and write only the copy that no submitted frame uses.

## Failures

- `Initialize` returns a `ResourceContextError`. On failure it starts no thread, and the context stays uninitialized.
- A transfer that cannot get its staging buffer fails with `StagingFailed`. One that cannot get its command pool or
  command buffer fails with `RecordingFailed`. Neither asserts.
- A failed submit fails every future in that batch with `SubmitFailed`. The transfer thread then waits for the transfer
  queue to go idle and signals the timeline from the host, so that later tickets and waiters do not stall.
- `TransferSetupFailed`: the transfer system could not create its timeline semaphore or a command pool.

## Transfers and tickets

The transfer system has one timeline semaphore. Every transfer gets a **ticket**, a value on that timeline.

1. The worker validates the message, then issues the next ticket (`++lastIssuedTicket`). It puts the ticket in the
   future's slot, sets the future to `Created`, and records the ticket on every entity that the transfer reads or
   writes (`LastTransferTicket`). Then it pushes the message to the transfer thread. This is the only path into the
   transfer system.
2. The transfer thread records transfers into a batch. It has 4 batches, each with its own command pool and one
   command buffer, and each holds at most 64 transfers. An ordering barrier goes before each transfer, so two
   transfers to one resource run in the order they were issued.
3. A submit signals the timeline to the highest ticket in the batch. Tickets increase in issue order, so one signal
   completes every transfer at or below it. A ticket that failed before recording is still signaled, by the next
   batch or by an empty submit, so that the timeline never has a gap.
4. Each loop of the transfer thread reads the timeline counter once (`vkGetSemaphoreCounterValue`). It retires every
   batch at or below that value and sets those futures to `Complete`. When no batch is ready, it waits on the oldest
   batch for at most 0.5 ms. When nothing is in flight, it blocks on its queue.

A consumer on another queue uses `Completion()`, which returns an `rhi::GpuWait`:

| Field | Value |
|---|---|
| `Semaphore` | The transfer timeline. |
| `Value` | The ticket. Wait for this value in your own submit. 0 means that the call made no transfer. |
| `Submitted` | True once the batch with this ticket was submitted. Before that, a GPU wait on the ticket can block forever. |

`Wait` on the CPU waits for the state to leave `Pending`, then for the ticket on the timeline, then for the final
state. Only the transfer thread writes the final state of a transfer. A future makes no Vulkan object of its own.

## Destroy and retirement

`DestroyResource` does not free at once when the GPU can still use the resource. The destroy records:

- the last ticket that touches the entity, while that ticket is above the completed ticket;
- the pending value of each registered consumer timeline, while that value is above the timeline's completed value.

When nothing is recorded, the objects are freed at once. Otherwise the entity gets a `Retiring` tag and goes into
the retirement list. The resolve functions refuse a retiring entity, so every later call that names it fails. The
worker checks the list after each message, and frees an entry when all its values have passed. The destroy's future
completes only when the objects are freed.

At shutdown, `Destroy` waits for the transfer queue, then frees every entry in the list. The owner must wait for its
own timelines before it destroys the context.

## Consumer timelines and Tick

A consumer timeline is a timeline semaphore that a user of the resources signals, such as the renderer's frame
timeline. At most `k_MaxConsumerTimelines` (4) can be registered.

- `RegisterConsumerTimeline` adds one. The future fails with `TimelineAlreadyRegistered` or `NoFreeTimelineSlot`.
- `Tick` is the owner's per-frame call. It gives the pending value of each timeline: the value that the work recorded
  from now on signals at the latest. Call it before you record a frame, so that a destroy issued during recording
  waits for that frame. The worker reads each registered counter once per `Tick`, then frees what is ready.
- `UnregisterConsumerTimeline` removes one. Call it only after the GPU passed every value you gave for it. Every
  retirement stops waiting on that timeline. You can destroy the semaphore when the future completes.

The context does not read a consumer timeline on its own. Without `Tick`, retirements that wait on one never free.

## Counters

`Counters()` returns `ResourceContextCounters`. Each value is a relaxed atomic, so together they are not one snapshot.
They count messages, recorded and failed transfers, submitted batches, timeline counter reads (transfer and
consumer), staged bytes, retired and pending retirements, pushes that blocked on a full queue, live futures, and
calls that found the future table full. The tests use them to prove that completion costs one counter read per loop,
not one per future, and that every future slot comes back.

The context queue holds 64 messages. A push to a full queue blocks until the worker takes one. The 64 comes from the
queue's 128-bit exit CAS, which has a 64-bit mask of completed writes. `ContextPushesBlocked` shows how often a call
blocked.
