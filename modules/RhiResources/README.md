# RhiResources

`ResourceContext` creates and manages buffers, images and samplers, and moves data into them. Any thread can call
it. The context does the work on its own threads and returns a reply at once.

## Threads and messages

- A call on `ResourceContext` becomes a message. The message goes onto an MWSR queue, and the call returns its reply.
- The **worker thread** owns the EnTT registry and a VMA allocator. It validates each message, creates the objects,
  and writes host-visible data directly.
- The **transfer thread** owns its own allocator, staging buffers, command pools and timeline semaphore. It records
  and submits the uploads, fills and copies that device-local resources need, on the transfer queue. It sends a
  progress message back to the worker each time its completed ticket advances. That push never blocks.
- Every resource uses concurrent sharing across the queue families, so no ownership transfer exists.

## Validation happens on the worker, once

Each message that takes a `GraphicsResource` from a caller resolves it first (`resolveBuffer`, `resolveImage`,
`resolveResource`). The resolve step checks two things:

1. The entity is alive in the registry.
2. The API handle in the `GraphicsResource` is the handle that the registry stores for that entity.

The second check catches a stale `GraphicsResource`. EnTT's 32-bit entity has a 12-bit version, which wraps after
4096 reuses of one index. The handle comparison is a second, independent answer.

The usage checks (transfer source, transfer destination, alignment) also run on the worker, before a message reaches
the transfer system.

A buffer with `DeviceAddressBuffer` usage fails its reply unless the context was created with
`DeviceAddressesEnabled`. Without that flag the allocator has no device address support, and VMA asserts on such a
buffer. The raytracing usages are refused in all cases for now.

## Names

The create infos carry `Name` as a `std::string_view`. `ResourceContext` copies the name into the message on the
calling thread, into a fixed 64-byte `DebugName` (63 chars and a length byte), so the caller's string only has to live until the call returns. Names
longer than 62 characters are truncated. Views, samplers and allocations are named after their resource, with a
suffix such as `_image_view`. Names reach Vulkan only when validation is on.

## Host-visible writes are not ordered against the GPU

`SetBufferData` on a host-visible buffer copies the data at once and reports `Completed`. Nothing orders that write
against GPU work that reads the buffer. If a frame in flight reads the buffer, the GPU sees a race. Keep one copy of
per-frame data for each frame in flight, and write only the copy that no submitted frame uses.

## Failures

- `Initialize` returns a `ResourceContextError`. On failure it starts no thread, and the context stays uninitialized.
- A transfer that cannot get its command pool, its command buffer or its staging buffer fails its reply. It does not
  assert.
- A failed submit fails the reply of every command in that batch. The transfer thread then waits for the transfer
  queue to go idle and signals the timeline from the host, so that later tickets and waiters do not stall.
- `TransferSetupFailed`: the transfer system could not create its timeline semaphore or a command pool.

## Transfers and tickets

The transfer system has one timeline semaphore. Every transfer gets a **ticket**, a value on that timeline.

1. The worker validates the message, then issues the next ticket (`++lastIssuedTicket`). It puts the ticket in the
   reply, sets the reply to `Transferring`, and records the ticket on every entity that the transfer reads or
   writes (`LastTransferTicket`). Then it pushes the message to the transfer thread. This is the only path into the
   transfer system.
2. The transfer thread records transfers into a batch. It has 4 batches, each with its own command pool and one
   command buffer, and each holds at most 64 transfers. An ordering barrier goes before each transfer, so two
   transfers to one resource run in the order they were issued.
3. A submit signals the timeline to the highest ticket in the batch. Tickets increase in issue order, so one signal
   completes every transfer at or below it. A ticket that failed before recording is still signaled, by the next
   batch or by an empty submit, so that the timeline never has a gap.
4. Each loop of the transfer thread reads the timeline counter once (`vkGetSemaphoreCounterValue`). It retires every
   batch at or below that value and sets those replies to `Completed`. When no batch is ready, it waits on the oldest
   batch for at most 0.5 ms. When nothing is in flight, it blocks on its queue.

What a consumer on another queue uses, for example Loom's `GpuWait`:

| Reply call | Value |
|---|---|
| `SemaphoreHandle()` | The transfer timeline. It is set when the reply is made. |
| `SemaphoreValue()` | The ticket. Wait for this value in your own submit. |
| `IsSubmitted()` | True once the batch with this ticket was submitted. Before that, a GPU wait on the ticket can block forever. |

`WaitForCompletion` on the CPU waits for the status to leave `Pending`, then for the ticket on the timeline, then for
the final status. Only the transfer thread writes the final status. A reply makes no Vulkan object of its own.

## Destroy and retirement

`DestroyResource` does not free at once when the GPU can still use the resource. The destroy records:

- the last ticket that touches the entity, while that ticket is above the completed ticket;
- the pending value of each registered consumer timeline, while that value is above the timeline's completed value.

When nothing is recorded, the objects are freed at once. Otherwise the entity gets a `Retiring` tag and goes into
the retirement list. The resolve functions refuse a retiring entity, so every later call that names it fails. The
worker checks the list after each message, and frees an entry when all its values have passed. The destroy reply
completes only when the objects are freed.

At shutdown, `Destroy` waits for the transfer queue, then frees every entry in the list. The owner must wait for its
own timelines before it destroys the context.

## Consumer timelines and Tick

A consumer timeline is a timeline semaphore that a user of the resources signals, such as the renderer's frame
timeline. At most `k_MaxConsumerTimelines` (4) can be registered.

- `RegisterConsumerTimeline` adds one. The reply fails for a duplicate or when no slot is free.
- `Tick` is the owner's per-frame call. It gives the pending value of each timeline: the value that the work recorded
  from now on signals at the latest. Call it before you record a frame, so that a destroy issued during recording
  waits for that frame. The worker reads each registered counter once per `Tick`, then frees what is ready.
- `UnregisterConsumerTimeline` removes one. Call it only after the GPU passed every value you gave for it. Every
  retirement stops waiting on that timeline. You can destroy the semaphore when the reply completes.

The context does not read a consumer timeline on its own. Without `Tick`, retirements that wait on one never free.

## Counters

`Counters()` returns `ResourceContextCounters`. Each value is a relaxed atomic, so together they are not one snapshot.
They count messages, recorded and failed transfers, submitted batches, timeline counter reads (transfer and
consumer), staged bytes, retired and pending retirements, and pushes that blocked on a full queue. The tests use them
to prove that completion costs one counter read per loop, not one per reply.

The context queue holds 64 messages. A push to a full queue blocks until the worker takes one. The 64 comes from the
queue's 128-bit exit CAS, which has a 64-bit mask of completed writes. `ContextPushesBlocked` shows how often a call
blocked.
