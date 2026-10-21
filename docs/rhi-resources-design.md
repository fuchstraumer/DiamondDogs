# RhiResources: transfer, streaming, device address, and bindless

Draft, 2026-10-20. Written by Claude from what the port showed (`audit.md` sections 4.4 and 10), to start the design
session with the author. Nothing here is implemented. Each section ends with the decisions that are hers.

The design that stays: calls become messages, a worker owns the registry, replies carry results, and the transfer
system runs on its own thread. Every proposal below fits inside that shape.

---

## 0. One prerequisite for everything: retirement

`DestroyResource` destroys the objects at once, on the worker thread. Nothing checks that the GPU is done with them.
Today the caller must make sure of that. Three of the four topics below make this worse:

- Streaming evicts resources while frames that used them are still in flight.
- A device address stays readable by shaders after the buffer is gone, and nothing in the API fails.
- A bindless slot that is freed and reused while an earlier frame still reads it gives the wrong texture, with no
  error.

**Proposal: a retirement queue keyed on timeline values.** The context knows one or more "consumer" timelines (the
frame timeline of the renderer, the transfer timeline). A destroy message records the current values of those
timelines, and the worker destroys the objects only after every timeline passed its value. The check is one
`vkGetSemaphoreCounterValue` per timeline per worker tick, not one per resource.

This also gives a natural place for slot and address reuse: a slot returns to the free list when its resource
retires, not when the destroy message arrives.

Decisions for the author:

- Who owns the frame timeline: the renderer passes it to `ResourceContext`, or the context creates it and the
  renderer signals it.
- Whether immediate destroy stays available (tests, shutdown) as a separate message.

---

## 1. Transfer system rework

### What the port found

| Cost or gap | Where it comes from |
|---|---|
| One `VkSemaphore` per reply, created on the caller's thread. | `ResourceTransferReply(const rhi::Device*)`. |
| One command pool, one command buffer, and one staging buffer (one VMA allocation) per transfer. | `TransferCommand`. The author's `todo.md` already names the pool cost. |
| One fence per batch, and one `vkWaitSemaphores` per command on the worker. | `waitForCommandsToComplete`. |
| A transfer cannot wait on the user's GPU work. | Messages carry no wait. A copy of a dispatch result has to be recorded by the caller. |
| The transfer system tracks image layouts that only it changes. | `TrackedImageLayout`. A user barrier makes it stale. |
| All resources use concurrent sharing. | No ownership transfers, but some drivers turn off image compression for concurrent images. Not measured. |
| A device with one queue family races the application's submits. | `todo-ship` in `TransferSystem.cpp`. |

### Proposal

**One timeline per transfer queue, with tickets.** The transfer system owns one timeline semaphore. Each batch it
submits signals the next value. A reply stores the pair (semaphore, ticket) instead of its own semaphore. The
existing `SemaphoreHandle()` and `SemaphoreValue()` keep their meaning for a consumer: wait on this semaphore for
this value. `k_TransferCompleteSemaphoreValue` goes away.

- Completion is one counter read: every reply with a ticket at or below the counter value is complete. The worker no
  longer waits per command.
- Replies cost no Vulkan object. A `ResourceTransferReply` becomes two words and a status.

**A staging ring.** One persistently mapped, host-visible buffer (size to be measured; 64 MiB is a start) used as a
ring. Each batch takes a range, and the range comes back when the batch's ticket retires. An upload larger than the
ring is split into chunks across batches, or gets a one-time dedicated buffer (her choice; chunking keeps memory
flat, a dedicated buffer keeps the code simple).

**Pooled command recording.** A small set of command pools (one per batch in flight) used round robin. A pool is
reset when its batch retires. One command buffer per batch, not per transfer.

**Inbound waits.** A transfer message may carry a list of (semaphore, value) waits. The transfer submits with those
waits. This makes `CopyBufferContents` after a dispatch correct, and it is how streaming will order an upload after
a GPU producer.

**Layout ownership: stateless context.** Recommended over better tracking. The transfer operations that touch images
take the image's current and next usage as arguments (the same `ImageUsageBits` vocabulary as `ResourceBarriers`),
and `TrackedImageLayout` goes away. Whoever schedules frames (the render graph later, the test now) owns the usage
state. Tracking inside the context would require every barrier to go through the context, which fights the message
design.

**Sharing mode: measure first.** Keep concurrent sharing for buffers (no measured cost). For images, write one
benchmark (sample a large compressed-candidate render target with concurrent and with exclusive sharing) before
adding queue-family ownership transfers, which double the barriers of every cross-queue image.

### How it is proved

By comparison, as the repository prefers:

- A stress test uploads random sizes from several threads, through a ring smaller than the total, and reads every
  byte back. The readback is the second opinion on ring reuse.
- Sync validation is the second opinion on barriers. It already found one real hazard (ImGuiTest).
- Counters that somebody reads: bytes staged, ring-full stalls, batches per second. Print them in the tests, so that a
  regression shows as a number.

Decisions for the author: ring size and the large-upload rule; whether replies keep the `Transferring` status
or report only Pending/Completed/Failed; the image sharing benchmark before or after the rework.

---

## 2. Streaming

### What the noise host needs

The planet generator produces most of its data on the GPU: noise kernels write height and material tiles. So
streaming here is mostly **residency** (which tiles exist at which detail), and less **upload bandwidth**. The
upload path still matters for authored content (textures, meshes) and for the CPU-side copies of tiles (collision,
placement), which come back through `GPUToCPU` readback.

### Proposal: a layer above ResourceContext, not inside it

`ResourceContext` stays the allocator and transfer service. A `StreamingSystem` (new module, or inside the planet
module) owns policy:

- **Requests** with a priority, a generation number, and a cancel. A request whose generation is stale is dropped
  before it costs a transfer.
- **A budget** from `VK_EXT_memory_budget` (VMA exposes it through `vmaGetHeapBudgets`). The streaming system evicts
  by least-recent use when a heap passes its budget. Eviction is a destroy, so it needs retirement (section 0).
- **GPU producers as first-class sources.** A tile is either uploaded (transfer queue, staging ring) or generated (a
  compute dispatch). Both end in the same state: resident, with a ticket on some timeline. Inbound waits (section 1)
  let a later upload or copy order itself after a generator.
- **File IO on its own thread.** `ResourceLoader` (in this module, untouched by the port) is a file loader with a
  worker pool. Review it before reuse: it uses `std::list`, mutexes, and a singleton, which the house style avoids.

Later, and only if measurement asks: sparse (partially resident) images for very large virtual textures.

Decisions for the author: where the streaming system lives; the priority model (distance-based for tiles, or a
general score); whether generated tiles are ever evicted to disk, or simply regenerated (the noise graph is
deterministic, so regeneration may beat storage).

---

## 3. Buffer device address

### What it needs

- The `bufferDeviceAddress` feature (core in 1.2). RhiSystem enables every supported feature of every requested
  extension today (`audit.md` 4.2), which is the wrong default for this: request it explicitly.
- `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT` on both allocators.
- `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`, from `BufferUsageBits::DeviceAddressBuffer`. The context refuses
  that bit today, by decision, until this lands.
- Lodestone: the Pointer placement on SPIR-V (request C4) and push constants (request C1), since a pointer usually
  arrives in a push constant.

### Proposal

- `GraphicsResource` and `GraphicsResourceReply` gain a `DeviceAddress` (`uint64_t`, 0 when the buffer has none).
  The worker queries it once at creation.
- Addresses are valid until the buffer retires (section 0), never until the destroy message.
- The author's own note in `ResourceFlags.hpp` already points here: device-address buffers replace buffer
  descriptors. So the plan is **no storage-buffer descriptors at all for new code**: buffers go by address, and
  descriptors remain for images and samplers (section 4).

### How it is proved

GPU-assisted validation checks every device-address access against live buffers. Run it in the resource tests once
the feature lands. A test that reads a buffer only by address and compares with the CPU is the integration proof,
like ComputeManifestTest is for bound buffers.

Decisions for the author: whether every buffer gets an address (simple, small cost) or only those with the usage bit
(current refusal implies the bit); how the address reaches a shader before Lodestone C1 and C4 exist (a stub: a
storage buffer of addresses).

---

## 4. Bindless

### Options

| Option | For | Against |
|---|---|---|
| Descriptor indexing (1.2 core): one large array per descriptor type, `UPDATE_AFTER_BIND`, `PARTIALLY_BOUND` | Supported everywhere the engine runs. Mature validation. Slots are plain indices. | Pool and layout limits differ per vendor. Updates go through `vkUpdateDescriptorSets`. |
| `VK_EXT_descriptor_buffer` | Descriptors are bytes in a buffer the engine manages; the transfer system could write them. | Less mature tooling. Layout sizes per descriptor type to query. Not needed for buffers once they go by address. |

**Recommendation: descriptor indexing for images and samplers, addresses for buffers.** That covers "setting and
selectively filling" with the smallest new surface.

### Proposal

- The context owns one global set per descriptor type (sampled image, storage image, sampler). Sizes come from device
  limits, capped by a configuration value.
- A resource asks for a slot with a usage bit (for example `ImageUsageBits::Bindless`; name to decide). The worker
  takes a slot from a free list at creation, writes it with `vkUpdateDescriptorSets` (allowed while the set is in use,
  under `UPDATE_AFTER_BIND`, as long as no in-flight work reads that slot), and returns the index in
  `GraphicsResource`.
- **Selective filling:** a slot is written only after the resource is complete (after its upload's ticket), so a
  shader never samples an image that is still `UNDEFINED`. Until then the slot holds a fallback (a 1x1 texture), so a
  shader that reads too early gets a known value, not undefined behavior.
- A slot is freed when its resource retires (section 0), never at the destroy message.
- Lodestone: the Handle placement (request C4). Slang's `DescriptorHandle<T>` must lower to an index into these
  arrays on SPIR-V; how Slang binds that heap is to be read from Slang's source, not assumed.

### How it is proved

A test that fills slots out of order, frees and reuses some, samples each slot in a compute shader, and compares with
the CPU. The fallback texture gives a visible, checkable value for a slot read too early.

Decisions for the author: the slot request (a usage bit, or a separate call); fallback content; set sizes; whether
storage images also go bindless.

---

## Proposed order

1. Retirement queue (section 0). Everything else needs it.
2. Transfer timeline and tickets, then the staging ring and pooled recording, then inbound waits (section 1).
3. Buffer device address (section 3), with explicit feature enabling in RhiSystem.
4. Bindless images and samplers (section 4).
5. Streaming (section 2), on top of the rest.

The author reserves this design work. These are proposals to argue with, not a plan to execute.
