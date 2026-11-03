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
| `rhi::ResourceFuture<GraphicsResource>` | `CreateBuffer`, `CreateImage`, `CreateSampler`, `CopyBuffer`, `CopyImage`, and the placement calls `CreateMemoryHeap`, `CreatePlacedBuffer`, `CreatePlacedImage` | The resource |
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
`CopyShapeMismatch`, `UnsupportedCopy`, `InvalidUsageTransition`, `TooManyWaits`, `InvalidWait` or `WaitOnHostWrite`.
The placed creates return `InvalidPlacement`, and a destroy of a heap that still has
placed resources returns `HeapInUse`. The transfer thread returns `StagingFailed`, `RecordingFailed` or
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

## Image usage handoff and inbound waits

The context does not track the layout of an image. It cannot know what other queues do to the image after a
transfer. The caller knows the last GPU use of the image, so the caller says it in every transfer call.

A transfer on an existing image takes an `rhi::ImageUsageTransition`, which has two fields:

| Field | Means |
|---|---|
| `Before` | The usage of the image on the GPU just before the transfer runs. |
| `After` | The usage that the image must have after the transfer. |

The usages are `ImageUsageBits`, the same words as `ResourceBarriers` uses. No Vulkan type is in the public calls.
The worker derives both layouts from the usages with `ImageLayoutFromUsageBits`. The transfer thread uses the layouts
as they come.

- **A new image.** The only source of `UNDEFINED` is a new image. `CreateImage(info)` makes an image with no data.
  `CreateImage(info, initial_data, usage_after_upload)` uploads from `UNDEFINED` and leaves the image in the layout of
  `usage_after_upload`. The context adds `TransferDestination` to the create usage.
- **An existing image.** `SetImageData`, `CopyImageToBuffer`, `CopyBufferToImage` and `CopyImageContents` take the
  transition for each image side. `CopyImage` takes the transition of the source and the resting usage of the new
  image. The new image starts from `UNDEFINED`.
- **Buffers.** `CopyBufferContents` is for two buffers. It fails with `WrongResourceKind` when a side is an image.
  `CopyBuffer` fails with `WrongResourceKind` for an image, and `CopyImage` fails with it for a buffer.
- **Discard.** `ImageUsageBits::Discard` as `Before` says that the old contents of the image do not matter. The
  transition starts from `UNDEFINED`. A barrier from `ResourceBarriers` gets no source stage and no source access. The
  caller must still order the transfer after earlier work on other queues, with inbound waits or with its own
  semaphores. A `Discard` upload that covers only part of the image leaves the rest of the image undefined. `Discard`
  is valid only as `Before` of an image that the transfer writes, and only alone. A transfer that reads the image needs
  defined contents. As `After`, as `Before` of a copy source, combined with another bit, in a create usage, or as
  `usage_after_upload`, it fails: with
  `InvalidUsageTransition` for the transitions, and with `InvalidCreateInfo` for a create call. On WebGPU (a future
  backend) a copy needs no layout, so `Discard` becomes a no-op there.

**Choose `Before` with care.** If `Before` does not match the layout that the image is in on the GPU, the barrier of the
transfer starts from the wrong old layout. Validation reports it at the submit.

The checks run on the worker, before the transfer exists. `Before` and `After` must each be non-zero. Each must be a
subset of the usage that the image was created with. Otherwise the future fails with `InvalidUsageTransition`. The
check stops a usage that gives a layout that validation rejects later, far from the cause.

### Inbound waits

A transfer can wait for GPU work on another queue. `SetBufferData`, `SetImageData`, `FillBuffer`, `CopyBuffer`,
`CopyImage`, `CopyBufferContents`, `CopyImageToBuffer`, `CopyBufferToImage` and `CopyImageContents` take
`std::span<const rhi::TransferWait> waits` as their last parameter. An empty span means no wait. The create calls,
`MapBuffer`, `UnmapBuffer` and `DestroyResource` take none.

Each `rhi::TransferWait` has a `Semaphore` (a timeline) and a `Value`. The transfer does not start on the GPU until the
timeline reaches every value. A call takes at most `rhi::k_MaxTransferWaits` (4) waits. The message stores them in a
fixed array, so no heap memory is used.

**The caller owns this rule: an inbound wait must name a value whose signal is already submitted, or will be submitted
by work that does not wait for this transfer.** The context does not check it. If the rule is broken, the GPU can
deadlock: the transfer waits for a frame, and the frame waits for the transfer. The render graph will check the rule. A
signal from the host is also a submitted signal.

The calls fail in these ways:

| Error | Cause | Where it fails |
|---|---|---|
| `TooManyWaits` | More than 4 waits. | On the calling thread, before any message exists. The future is `Failed` when the call returns. |
| `InvalidWait` | A wait with a null semaphore or a value of 0. | On the worker. |
| `WaitOnHostWrite` | A wait on `SetBufferData` of a host-visible buffer. | On the worker. The worker writes that memory at once, and a wait would block the worker thread. |

`CopyBuffer` with `copy_contents` false makes no transfer, so it ignores its waits.

### The own-batch rule, and what it does not fix

A queue runs the commands of one submit in order, and a semaphore signal waits for all commands that were submitted
before it. So a blocked transfer would hold every transfer in its batch. To protect the transfers issued before it, a
transfer with waits goes in a batch of its own:

1. The transfer thread submits the open batch, if it holds a transfer. It does not submit an empty batch.
2. It records the waiting transfer alone in a fresh batch.
3. It submits that batch at once. Each wait is in `pWaitSemaphoreInfos`, with the all-transfer stage. When two waits
   name the same semaphore, only the larger value is used.
4. The batch signals its ticket as usual. Completion and retirement do not change.

The rule does not help the transfers issued **after** the waiting one. They go in later batches, and their signals come
after the blocked batch on the same queue. They cannot complete before the wait is released. The tickets stay in order,
which the completion code needs. A second transfer queue would remove this limit. It is not built.

### In-frame copies

In-frame copies are not for this API. A readback of the target of the current frame is work for the render graph. It
records the copy on its own command buffer. The copy calls of the context are for work outside a frame.

## Placed resources

Placed resources let two buffers or images share the same memory (aliasing). The render graph uses them for transient
targets whose lifetimes do not overlap. The calls are free functions in `ResourcePlacement.hpp`, namespace
`rhi::placement`. They are for the Vulkan backend only: WebGPU has no heaps and no memory placement, and its backend
aliases memory by reusing whole textures. `ResourceContext.hpp` has no placement call. It only declares the functions
as friends, so that they can reach the impl.

### Queries

`GetImageMemoryRequirements` and `GetBufferMemoryRequirements` return a `MemoryRequirements` (`Size`, `Alignment`,
`MemoryTypeBits`). They run on the calling thread with `vkGetDeviceImageMemoryRequirements` and
`vkGetDeviceBufferMemoryRequirements`. They send no message and take no lock, so a graph can pack offsets while it
compiles. The create path and the query both call `ToVkImageCreateInfo` or `ToVkBufferCreateInfo`, so the answer is for
the object that the create makes. Pass the usage that you create with. A create info that `CreateImage` refuses (an
extent of 0, an invalid format, a buffer size of 0) gives all zeros.

### Heaps

`CreateMemoryHeap` allocates one block of device-local memory. The worker calls `vmaAllocateMemory` with
`VMA_MEMORY_USAGE_UNKNOWN`, the required flag `DEVICE_LOCAL`, and the dedicated-memory flag. The AUTO usages cannot be
used, because VMA asserts when it does not know what the memory is for. A dedicated allocation starts at offset 0 of its
own `VkDeviceMemory`, so the offsets that you give are offsets into real device memory. The heap is a `GraphicsResource`
with `ResourceType::Heap`. A size or a `MemoryTypeBits` of 0 fails with `InvalidCreateInfo`. To make one heap for
several resources, AND their `MemoryTypeBits` together.

### Placed creates

`CreatePlacedImage` and `CreatePlacedBuffer` take a create info and a `Placement` (`Heap`, `Offset`). There are no
initial data overloads. The domain must be `GPUOnly`, or the future fails with `InvalidCreateInfo`.

On the worker:

1. The heap is resolved. A stale, retiring or non-heap resource fails as it does for any other call: `StaleResource`,
   `ResourceRetiring` or `WrongResourceKind`.
2. The context creates the raw `VkImage` or `VkBuffer`, and reads its real memory requirements.
3. It checks three things: the offset is a multiple of the alignment, the offset and the size are inside the heap, and the
   memory type of the heap is in `MemoryTypeBits`. A failure destroys the raw object and fails with `InvalidPlacement`.
4. It binds the memory with `vmaBindImageMemory2` or `vmaBindBufferMemory2` at the offset. It does not use
   `vmaCreateAliasingImage2`, because that call asserts on a bad placement and these checks must answer first.
5. Views, the sampler, names and the registry components follow the normal create path. A placed entity has a `PlacedIn`
   component and no `VmaAllocation`.

A placed resource has no data and no defined layout. **The first write of a placed image must use `Before =
ImageUsageBits::Discard`.** After another resource used the same memory, the old layout is meaningless, and a `Discard`
starts the transition from `UNDEFINED`. The inbound ordering is the same transfer queue, so the ordering barrier of the
transfer covers the hazard between two aliasing images that use the transfer system. Work on other queues needs the
caller's semaphores or inbound waits.

`CopyBuffer` and `CopyImage` of a placed resource make a normal resource with its own allocation, not another placed
one. `MapBuffer` and `UnmapBuffer` on a placed buffer fail with `NotHostVisible`.

### Placement count and HeapInUse

The heap counts its placed resources. The count goes up when a placed resource is created. It goes down when the placed
resource is **freed**, after its retirement, and not when its destroy is requested, because the GPU can use the memory
until then. `DestroyResource` on a heap with a count above 0 fails with `HeapInUse`, and the heap stays alive and
usable. A heap with a count of 0 goes through the normal destroy and retirement, and its free calls `vmaFreeMemory`.
A placed resource frees only its Vulkan object and its views, because `vmaDestroyImage` and `vmaDestroyBuffer` destroy
only the Vulkan object when the allocation is null. At shutdown the context frees every placed resource before any heap.

### Overlap is not checked

The context does not check which placements overlap. Overlap is the purpose of aliasing. The caller owns the lifetimes:
the GPU must be done with one aliasing resource before the next one is used, and the next one must start with a
`Discard` write.

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
