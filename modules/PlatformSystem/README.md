# PlatformSystem

GLFW windows, displays, surfaces and the swapchain. This README covers presentation: how `Swapchain` acquires,
presents and releases images, and why each semaphore and fence can be used again when it is. The plan behind it is
`docs/presentation-plan.md` (phase P1, 2026-11-23).

## Who owns what

`Swapchain` owns every object of presentation: the acquire semaphores, one render-done semaphore per image, the present
fences, and the old swapchains that a present may still use. A caller (Loom's `SwapchainFrameTarget`, or any other
frame loop) gets handles and follows three rules:

1. Its submit waits on `SwapchainAcquire::WaitSemaphore` before the first use of the image.
2. Its submit signals `SwapchainAcquire::RenderDoneSemaphore`.
3. It calls `Present(index)` or `Release(index)` for every image it acquired, on the thread that submits to the
   general queue.

`VulkanScene` (the old test fixture) still acquires and presents on its own. It uses only `Recreate` and the image
handles, so none of this applies to it.

## The two paths

`UsesMaintenance1()` says which path runs. It is true when the device enabled swapchain maintenance1 (the window stage
asks for it as Wanted, with surface maintenance1) and `LimitToCorePresentation()` was not called.

| | With maintenance1 | Core presentation |
|---|---|---|
| Present | a fence per present (`VkSwapchainPresentFenceInfoKHR`), from a ring of 8 | no fence |
| `Release` | `vkReleaseSwapchainImagesKHR` | `Unsupported`: only a present gives an image back |
| `WaitForPresents` | waits on the pending fences | returns false: the caller waits for the device |
| `Recreate` | needs only rule R below; the old swapchain is destroyed once its presents finished | the device must also be idle |
| Destruction | waits for the presents itself | the device must be idle |

**Rule R (both paths):** before `Recreate`, no image may be acquired and not yet presented or released, and every
submit that waited on an acquire semaphore must have finished. Loom meets it by recreating only after its last submit
passed.

## Why each object can be reused

**Acquire semaphores.** A free list; a new one is made when the list is empty, so the count settles at about one more
than the image count. An acquire that gives no image leaves its semaphore unsignaled, so it goes straight back. An
acquire that gives image `i` stores its semaphore against `i`. The semaphore stored for `i` before that goes back to
the free list: image `i` came back, so its last present finished; that present waited on the render-done semaphore,
which the submit signaled at its end; and that submit waited on the old acquire semaphore. So the wait on it finished.

**Released images.** A released image's acquire semaphore was signaled and never waited on, and a binary semaphore
cannot be signaled again in that state. `Release` submits an empty batch that waits on it, with a fence; once the fence
signals, the semaphore goes back to the free list.

**Render-done semaphores.** One per image, because a present holds its semaphore until it finishes, and image `i`
cannot be acquired again before then. After a recreate, the old swapchain's semaphores go with it (they may still be
waited on by its last presents).

**Present fences.** Each present takes the next fence of the ring and a serial number. A fence is reset only after it
signaled. Everything up to "the oldest pending serial, minus one" has finished; an old swapchain whose last serial is
at or below that is destroyed, with its render-done semaphores. The check runs at each acquire and present.

```mermaid
sequenceDiagram
    participant C as Caller (Loom)
    participant S as Swapchain
    participant Q as Queue
    participant P as Present engine
    C->>S: TryAcquire(0)
    S->>P: vkAcquireNextImageKHR(acquire semaphore a)
    Note over S: a stored for image i; the previous one for i is free again
    S-->>C: image i, wait a, signal RenderDone[i]
    C->>Q: submit: wait a ... signal RenderDone[i]
    C->>S: Present(i)
    S->>P: vkQueuePresentKHR(wait RenderDone[i], fence of serial n)
    P-->>S: fence signals: serial n finished, old swapchains up to n can go
```

## Tests

`LoomTriangleTest` is the proof (see the Loom README): `--resize`, `--minimize`, `--no-maintenance1`, and, with
maintenance1, ten acquire-and-release cycles through `Swapchain` before the graph runs.

Plants (2026-11-23, ninja-msvc, each restored byte for byte):

| Plant | Result |
|---|---|
| `Release` returns the semaphore to the free list without the empty submit | caught: `VUID-vkAcquireNextImageKHR-semaphore-01286` |
| An acquire frees its own semaphore at once, instead of at the next acquire of the image | caught: the run crashes, with `VUID-VkSubmitInfo-pWaitSemaphores-parameter` |
| The old swapchain is destroyed at once, not retired | **not caught.** In LoomTriangleTest every present has finished before the recreate (Loom waits for its last submit, and the probe showed completed serial = present serial at all three recreates), so the deferred destruction never runs. It is unproven on the GPU. |
