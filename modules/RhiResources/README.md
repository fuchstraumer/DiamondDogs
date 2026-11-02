# RhiResources

`ResourceContext` creates and manages buffers, images and samplers, and moves data into them. Any thread can call
it. The context does the work on its own threads and returns a reply at once.

## Threads and messages

- A call on `ResourceContext` becomes a message. The message goes onto an MWSR queue, and the call returns its reply.
- The **worker thread** owns the EnTT registry and a VMA allocator. It validates each message, creates the objects,
  and writes host-visible data directly.
- The **transfer thread** owns its own allocator, staging buffers and command pools. It records and submits the
  uploads, fills and copies that device-local resources need, on the transfer queue.
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

## Names

The create infos carry `Name` as a `std::string_view`. `ResourceContext` copies the name into the message on the
calling thread, into a fixed 64-byte buffer, so the caller's string only has to live until the call returns. Names
longer than 63 characters are truncated. Views, samplers and allocations are named after their resource, with a
suffix such as `_image_view`. Names reach Vulkan only when validation is on.

## Host-visible writes are not ordered against the GPU

`SetBufferData` on a host-visible buffer copies the data at once and reports `Completed`. Nothing orders that write
against GPU work that reads the buffer. If a frame in flight reads the buffer, the GPU sees a race. Keep one copy of
per-frame data for each frame in flight, and write only the copy that no submitted frame uses.

## Failures

- `Initialize` returns a `ResourceContextError`. On failure it starts no thread, and the context stays uninitialized.
- A transfer that cannot get its command pool, its command buffer or its staging buffer fails its reply. It does not
  assert.
- A failed submit fails the reply of every command in that batch.
