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

## Maintenance1 extras (P2)

All of these need `UsesMaintenance1()`; without it they do nothing, and a mode change always recreates.

- **Present-mode switching.** At create, the swapchain lists the modes the surface says it can switch to from the
  created mode (`VkSurfacePresentModeCompatibilityKHR`, in `VkSwapchainPresentModesCreateInfoKHR`), and each present
  names its mode (`VkSwapchainPresentModeInfoKHR`). `SetPresentMode` returns `NextPresent` for a mode in that list (no
  recreate), `NeedsRecreate` for another mode the surface offers (`SwapchainFrameTarget` then recreates at its next
  acquire), and `Unsupported` otherwise. On the RTX 4080: FIFO switches to MAILBOX or IMMEDIATE and back; FIFO_RELAXED
  and FIFO_LATEST_READY switch to nothing. FIFO_LATEST_READY counts as offered only when the device enabled
  `k_FifoLatestReadyFeature` (the window stage asks for it as Wanted): the surface lists it either way, and creating
  with it unenabled is a validation error.
- **Present scaling** (`SetPresentScaling`, on by default). The swapchain is created with aspect-ratio stretch (or
  stretch), centered, where the surface offers it. During a drag, the presentation engine scales the old images
  instead of reporting out of date at once. A resize still recreates once it is reported suboptimal: the `--resize`
  run recreates three times either way. With scaling, a minimized window is not reported as out of date, so
  `SwapchainFrameTarget` asks `WindowHasArea()` before it acquires: without that check, 31 to 40 frames were drawn into a
  minimized window per run.
- **Deferred image memory** (`SetDeferredImageMemory`, off by default). The images get memory at their first acquire,
  and their views are made then too. **It crashes the NVIDIA driver** (`nvoglv64.dll` 32.0.16.1714, an access
  violation at offset 0xdec1b8, every run, both presets): the first recreate into a deferred swapchain works, the next
  recreate from it crashes inside the driver, with no validation message. A driver bug in the `oldSwapchain` handoff of
  deferred images is my guess, not proven. `LoomTriangleTest --deferred-memory` reproduces it.
- **Statistics.** `Statistics()` counts recreates, their total and longest time, and the in-place mode switches.

Recreate times measured by LoomTriangleTest (both presets, Debug): with maintenance1, 7 to 8 ms mean per recreate
during `--resize` (no device idle); core presentation, 8 to 9 ms (device idle first). In `--present-modes` with core
presentation, the longest recreate was 266 ms (a FIFO_LATEST_READY or IMMEDIATE swapchain coming up; not broken down
yet).

## Present ids and present waits (P3)

Every present gets an id when the swapchain has a present wait: 1, 2, 3, and so on, across recreates. An id is used up
even when its present fails, so ids only go up. `LastPresentId()` is the id of the last present that was accepted.
`WaitForPresent(id, timeout)` blocks until that present, or a later one, reached the display.

`PresentWaitTier()` says which version is in use. It is chosen at each create, best first:

| Tier | Needs | Per present | Wait |
|---|---|---|---|
| `PresentWait2` | device features `presentId2` and `presentWait2`, **and** the surface reports both (`VkSurfaceCapabilitiesPresentId2KHR`, `...PresentWait2KHR`); the swapchain is created with the two `PRESENT_ID_2` / `PRESENT_WAIT_2` flags | `VkPresentId2KHR` | `vkWaitForPresent2KHR` |
| `PresentWait` | device features `presentId` and `presentWait` | `VkPresentIdKHR` | `vkWaitForPresentKHR` |
| `None` | | no id | `Unsupported` |

The window stage asks for all four features as Wanted. Present waits do not need maintenance1: the core path uses them
too.

A wait returns at once when there is nothing it could wait for: `NotPresented` for id 0 or an id above
`LastPresentId`, and `Retired` for an id that went to a swapchain a Recreate replaced. Without the `Retired` guard, a
wait on an old id before the first present on the new swapchain would block until the next present or the timeout.
Once the new swapchain has presented, the driver answers such a wait itself ("or a later one"), which is why the plant
that removes the guard is not caught (below).

Loom has no part in this. The app owns the `Swapchain` and the graph presents inside Tick, so after Tick,
`LastPresentId()` is that frame's id. The pacer (P4) waits on "last id minus k" before the next frame's input.

## Frame pacing (P4)

`foundation::FramePacer` (`foundation/include/timing/FramePacer.hpp`) decides when the next frame starts. It has no
graphics API: the app gives it the swapchain's present wait as a `PresentWaitDelegate`, and the last present id each
frame. Call `WaitForFrameStart(last_present_id)` once per frame, before input and before the frame's work. It waits in
two steps:

1. **The latency limit** (`MaxFramesToDisplay`, k, any mode). With N the last present, it waits until present
   N + 1 - k reached the display: k = 1 waits for the last present itself (lowest latency), k = 2 lets one frame be in
   flight. Off when k is 0 or no present wait is bound.
2. **The schedule**, by mode:
   - `Uncapped`: no wait.
   - `FixedCadence`: frames start on a fixed grid at `CadenceHz`. A frame up to one interval late is caught up (the
     next one starts at once, the one after is back on the grid); a later one starts a new grid.
   - `VrrCeiling`: a frame starts no sooner than 1 / (`VrrMaxHz` - `VrrGuardHz`) after the one before it, and a late
     frame never makes the next one start early. Inside the VRR window the frame rate is free; above it, the driver
     would queue (FIFO) or tear (IMMEDIATE). `RecommendedVrrGuardHz` gives 2 Hz or 2 %, whichever is more (4.8 Hz at
     240 Hz: a 235.2 Hz ceiling).

The display wait comes first, so the schedule counts from when the display caught up. Every setting is explicit; the
pacer reads time and sleeps through a `PacerClock` (the real one sleeps with `HighResolutionWait`, then spins), so the
unit tests run on a fake clock. `FrameLimiter` is unchanged and stays for `VulkanScene` until both go.

**The VRR range.** `DisplayRefreshRateCapabilities` has `MinRefreshRate` and `MaxRefreshRate` from the EDID's range
limits descriptor (tag 0xFD, with the EDID 1.4 +255 Hz offsets), read on Win32 with the EDID the HDR data already
uses (`src/EdidRange.cpp`, platform free). Zero means unknown (Linux, or no descriptor). The range does **not** prove
VRR: a fixed-refresh display lists the timings it accepts (often 56 to 76 Hz), and whether VRR is on is a driver
setting. So there is no "supports VRR" flag. With VRR, the top of the window is the active mode's refresh rate, so
`VrrMaxHz` comes from `RefreshRate`, and the EDID minimum tells where low-framerate compensation starts. On the
author's LG ULTRAGEAR+: active 239.989 Hz, EDID 48 to 240 Hz.

## Tests

`LoomTriangleTest` is the proof (see the Loom README): `--resize`, `--minimize`, `--no-maintenance1`,
`--present-modes`, `--no-scaling`, `--deferred-memory`, `--present-wait`, `--present-wait-v1`, `--no-present-wait`,
`--no-fifo-latest-ready`, and, with maintenance1, ten acquire-and-release cycles through `Swapchain` before the graph
runs. The fallback switches decline the Wanted features at device creation (`DeviceConfig::Declined`), so the fallback
runs on a device that really lacks them, not behind a test switch in `Swapchain`.

Plants (2026-11-23, ninja-msvc, each restored byte for byte):

| Plant | Result |
|---|---|
| `Release` returns the semaphore to the free list without the empty submit | caught: `VUID-vkAcquireNextImageKHR-semaphore-01286` |
| An acquire frees its own semaphore at once, instead of at the next acquire of the image | caught: the run crashes, with `VUID-VkSubmitInfo-pWaitSemaphores-parameter` |
| The old swapchain is destroyed at once, not retired | **not caught, and cannot be on NVIDIA** (2026-11-24). A present fence signals as soon as the present took its semaphore, and under rule R that semaphore is already signaled. A present held back on purpose does not help: `vkCreateSwapchainKHR` with `oldSwapchain` waits inside the driver for the old swapchain's queued presents (a recreate took exactly as long as a 200 ms hold), so the recreate never finds a present pending. (Holding a present back with a host-signaled timeline is also not legal: `VUID-vkQueuePresentKHR-pWaitSemaphores-03268`.) The deferred path is the spec-correct net for drivers that do not wait; `Statistics().RetiredWithPendingPresents` shows whether it ran. |

P2 plants (2026-11-23, ninja-msvc, each restored byte for byte):

| Plant | Result |
|---|---|
| `SetPresentMode` treats every mode as switchable | caught: `VUID-VkSwapchainPresentModeInfoKHR-pPresentModes-07761` |
| `SwapchainFrameTarget` skips the `WindowHasArea` check | caught by `--minimize`: 33 submits after the first 10 Ticks |
| FIFO_LATEST_READY offered without its feature | **not caught here**: this device enables the feature (Wanted), so the gate changes nothing. Only a device without the feature can prove it. |

P3 plants (2026-11-24, ninja-msvc, each restored byte for byte):

| Plant | Result |
|---|---|
| The id is counted but never attached to the present | caught: `VUID-vkWaitForPresent2KHR-presentId-10817`, and every wait times out |
| The 2 versions without their create flags | caught: `VUID-VkPresentId2KHR-None-10820`, `VUID-vkWaitForPresent2KHR-None-10816` |
| The tier ignores the device (`--present-wait-v1`, ids2 used anyway) | caught: the tier check, `VUID-VkPresentInfoKHR-pNext-10821`, then an access violation |
| `FirstPresentId` is not moved at a recreate (`--resize`) | **not caught**: the test waits on N - 1 only after N went to the new swapchain, and the driver correctly answers "a later one was presented". The guard matters for a wait before the first present on the new swapchain. |

P4 plants (2026-11-24, ninja-msvc, unit tests, each restored byte for byte): all caught.

| Plant | Caught by |
|---|---|
| A VRR ceiling catches up like a fixed cadence | `VrrCeilingDoesNotBurstAfterASlowFrame` (it first used a 30 ms stall, which restarts the schedule either way, and missed the plant; it now uses 1.5 intervals) |
| The latency limit waits on N - k instead of N + 1 - k | `LatencyLimitWaitsForTheRightPresent`, `LatencyLimitOfOneWaitsForTheLastPresent` |
| The schedule runs before the display wait | `ReportsTheWaitsItMade` |
| The EDID +255 Hz offsets are ignored | `AddsTheMaxOffset`, `AddsBothOffsets` |
| The EDID checksum is not checked | `RefusesABadBlock` |
