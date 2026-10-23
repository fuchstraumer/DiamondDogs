# ColorPipe

ColorPipe turns scene-linear color into the encoded image of a display, for SDR and HDR. This file explains the
module for people and for agents. Code comments in the module state only local constraints. The reasons are here.

## What the module is

The module has two CMake targets.

- `color_math` is the double-precision CPU reference. It has no dependencies. It contains:
  - gamut matrices that are derived from chromaticities (`ColorSpaces.hpp`);
  - luminance, chromatic adaptation and a Kelvin-to-color function (`ColorSpaces.hpp`);
  - the sRGB, gamma 2.2, PQ and scRGB transfer functions (`TransferFunctions.hpp`).
- `color_pipe` is the runtime. It links `color_math`, `rhi_resources`, `rhi_system` and `platform_system`. It contains:
  - the CPU stages `ChooseSurface` and `ResolveDisplayTarget` (`DisplayTarget.hpp`);
  - the CPU reference of the composite pass (`ColorComposite.hpp`);
  - `color::ColorPipe`, which owns the targets and records the composite pass (`ColorPipe.hpp`).

The CPU stages are in `color_pipe` and not in `color_math`, because they use `DisplayColorCapabilities` and the
surface pairs of PlatformSystem. `color_math` must stay free of dependencies, so that its tests stay fast.

`tools/color_matrix_gen.cpp` is a build tool. It writes `ddColorGenerated.slang`, which contains the gamut matrices
for the shaders. It uses `color_math`, so the shader matrices and the CPU matrices come from the same chromaticities.
The gamut conversions use Bradford adaptation. A Slang matrix multiplies a row vector (`mul(vector, matrix)`). Each shader constant is therefore the transpose of the
column-vector matrix in `color_math`. The tool rewrites the file only when its contents change, so that an unchanged
run does not re-run the shader cook.

## The domains

A pixel passes through these domains in this order. The CPU reference (`CompositePixel`) and the shader
(`ColorComposite.slang`) do the same steps in the same order.

1. **AP1 scene-linear.** Shaders upstream write this and nothing else. The white point is the ACES white.
2. **Exposure.** The pass multiplies by `2^EV`.
3. **Tonemapper.** It returns linear Rec.2020 that is relative to paper white. 1.0 is paper white. The largest value
   is the headroom (`PeakNits / PaperWhiteNits`).
4. **Display primaries.** The pass converts Rec.2020 to the primaries of the encoding, which are Rec.709 for sRGB and
   scRGB, and Rec.2020 for PQ. Then it clips each channel to `[0, headroom]`.
5. **Nits.** The pass multiplies by the paper white.
6. **UI composite.** The pass composites the UI over the scene, in nits (see "The frame").
7. **Encode.** The pass writes one of:
   - the sRGB OETF of `nits / paper white` (an SDR UNORM target);
   - `nits / 80` (scRGB, an FP16 target);
   - the PQ signal of `nits` (a 10-bit UNORM target).

## The frame

Each frame uses three images and one buffer.

- **Scene target** (`ColorPipe::SceneTarget`): RGBA16F, AP1 scene-linear. The consumer renders into it, or resolves
  its MSAA image into it.
- **UI target** (`ColorPipe::UiTarget`): RGBA8 UNORM. Dear ImGui draws into it. The consumer clears it to transparent.
  The colors are sRGB-encoded, and they are premultiplied by alpha (see "Decisions").
- **Composite**: `RecordComposite` draws one fullscreen triangle. The fragment shader reads both targets with `Load`
  and writes the output image. It is a fragment pass and not a compute pass, because swapchain images do not reliably
  support storage usage, and an `_SRGB` format never does.
- **EV buffer**: a small storage buffer that the fragment shader reads (element 0 is the EV in stops). `RecordComposite`
  writes it in the queue. The adaptation pass will write it later, and the composite will not change.

### Layouts

- Both targets rest in the `Attachment` layout between composites. A new target is already in that layout. The
  consumer declares that layout when it renders to the target, and leaves the target in it.
- `RecordComposite` moves both targets to `ReadOnly`, draws, and moves them back to `Attachment`.
- `output_view` is in `ColorAttachment` layout on entry. It stays in that layout on exit. The consumer makes the
  transition to the present layout.
- `output_view` must have the size of the pipe and the format of `Surface`, in the encoding of `Target()`.

## The CPU stages

Both stages are pure functions with unit tests.

### `ChooseSurface(preferences, available)`

- Input: the preferences, and the pairs (format, color space) that the surface offers.
- Output: a `SurfaceChoice`.
- Output `Sdr` picks BGRA8 and then RGBA8, UNORM, with the sRGB color space. It never picks an `_SRGB` format.
- Output `Hdr` picks the preferred encoding first. The two encodings are 10-bit PQ (HDR10) and FP16 scRGB. PQ goes
  first unless scRGB is preferred. Any other preference, and a missing pair, fall back to the other HDR pair, and
  then to the SDR pair.
- Output `Invalid`, and a surface without a usable pair, return an `Encoding` of `Invalid`.
  `ColorPipe::Create` fails with `SurfaceUnusable` for that.
- The unit test uses the real pair list of the LG ULTRAGEAR+ on the RTX 4080 (measured with DisplayProbe on
  2026-10-22). That list has no FP16 and PQ pair.

### `ResolveDisplayTarget(capabilities, surface, overrides)`

- Paper white is the override, or the SDR white level of the display. It is at least `k_MinimumPaperWhiteNits`. A
  display that reports none gets 80 nits.
- The peak of an SDR encoding equals the paper white. The peak of an HDR encoding is the override, or the maximum
  luminance of the display. It is at least the paper white.
- A display that reports no maximum gets a peak that equals its paper white. Its headroom is 1.
- Black is the override, or the minimum luminance of the display, within `[0, paper white]`.
- Overrides exist because EDID and DXGI peak and black values are often marketing numbers, and an OLED panel has a
  black level of nearly zero.

## The Lodestone axes

`ColorComposite.slang` declares two axes. The policy file lists the variants that a cook makes.

- `TONEMAPPER` is an interface axis (`ITonemapper`). Each value is a struct with the attribute
  `[ls_axis_interface_impl("ITonemapper")]`. The values today are `Passthrough` and `FalseColorEv`.
- `ENCODE` is an enum axis (`ColorEncoding`): `SrgbNonlinear`, `ScRgbLinear` and `Hdr10Pq`.
- That is 2 x 3 variants today. A change of tonemapper or encoding selects another variant at runtime. Nothing
  recompiles.
- `ColorCompositePolicy.toml` marks both axes inert for the vertex stage, which draws the same triangle for every
  variant. It sets `MaxVariants = 24`. That leaves room for more tonemappers, and it fails the cook when an axis
  multiplies the count by accident.

`FalseColorEv` shows the exposure of the scene in 11 zones of 1 EV, from -5 EV to +5 EV around middle gray (18%).
Its zone colors are in two places: `k_FalseColorZonesSrgb` in `ColorComposite.cpp` and in
`ColorCompositeTonemappers.slang`. The two tables must stay equal.

### How to add a tonemapper

1. In `ColorCompositeTonemappers.slang`, add a struct that implements `ITonemapper`, with the attribute
   `[ls_axis_interface_impl("ITonemapper")]`. `Tonemap` receives exposed AP1. It returns linear Rec.2020 that is
   relative to paper white, and no channel may exceed `display.Headroom()`.
2. Add a value to `TonemapperKind` in `DisplayTarget.hpp`, if it is not there yet. `KhronosNeutral` and `AgX` exist
   already.
3. In `ColorPipe.cpp`, add the kind to `k_WrittenTonemappers`, and make `TonemapperTypeName` return the struct name.
4. In `ColorComposite.cpp`, add the double-precision reference to `Tonemap`.
5. In `ColorPipeTest`, add the kind to the tonemapper list. In `color_pipe_tests`, add the unit tests of the
   reference.
6. Raise `MaxVariants` in the policy file if the new count is above 24.

## How a consumer uses the module

The module has no shader bundle of its own. A consumer cooks the composite into its own bundle, so that one
`rhi::ShaderLibrary` serves the consumer's shaders and the composite.

CMake variables (set by `modules/ColorPipe/CMakeLists.txt`):

| Variable | Use |
|---|---|
| `COLOR_PIPE_COMPOSITE_SHADER_MODULE` | Add this file to `MODULES` of `lodestone_add_shader_cook`. |
| `COLOR_PIPE_COMPOSITE_INCLUDE_DIRS` | Add this to `INCLUDE_DIRS` of the cook. It lists the folder of `ddColor.slang` and the folder of the generated matrices. |
| `COLOR_PIPE_COMPOSITE_POLICY_FILE` | Use this as `POLICY_FILE`, or copy its sections into the policy file of the consumer. |
| `COLOR_PIPE_SHADER_MODULE` | The `ddColor.slang` source, for a consumer that wants `ddColor` in its bundle as a module. |
| `COLOR_PIPE_SHADER_DIR`, `COLOR_PIPE_GENERATED_SHADER_DIR` | The folders of `ddColor.slang` and of `ddColorGenerated.slang`. |

Rules:

- The cook target must depend on `color_pipe_shader_data`. That target runs `color_matrix_gen`. `ColorComposite`
  imports `ddColor`, which includes `ddColorGenerated.slang`, so the cook fails without the generated file.
- A policy file that merges the module's sections needs the `[ColorComposite.InertAxesForEntryPoints]` and
  `[ColorComposite.targets.spirv]` tables.
- Link `color_pipe`. Its public include directories also export those of PlatformSystem and foundation, because
  those modules do not export their own, and the public headers of `color_pipe` include them.
- Pass the library to `ColorPipe::Create` in `ColorPipeCreateInfo::Library`. `Create` selects the variants of the
  encoding of the surface, creates the programs, the targets, the EV buffer and the descriptor sets, and waits for
  one short submit on the general queue.
- The consumer calls `OnSwapchainRecreated` after each swapchain recreation. A headless pipe (no swapchain) sets
  `Width` and `Height`, and calls `Resize` directly.
- The consumer calls `SetExposureEv`, `SetTonemapper` and `SetOverrides` at any time. The next `RecordComposite` uses
  the new values.

## How it is tested

Run each test from its own output folder. A pass needs exit code 0 and no `Validation Error` or `VUID` text.

| Target | What it proves |
|---|---|
| `color_pipe_tests` | The CPU reference: matrices, luminance weights, transfer functions, `ChooseSurface`, `ResolveDisplayTarget`, and each step of the composite. |
| `ColorMathTest` | Every `ddColor` function on the GPU against `color_math`, on random and edge inputs. |
| `ColorPipeTest` | The whole composite on the GPU against `CompositePixel`, for each tonemapper and each encoding. It has no window. |

### Tolerances and their reasons

- **`color_pipe_tests`**
  - Matrix identities: `1e-12`. The arithmetic is double precision.
  - Published luminance weights: the rounding of the last published digit (`1e-4` for Rec.709, `1e-5` for Rec.2020,
    `1e-6` for AP1).
- **`ColorMathTest`** (float32 shader against the double reference)
  - Matrices: `2e-6`, relative to the largest component of the result. A matrix mixes components, and cancellation
    makes the relative error of a small component meaningless.
  - Transfer functions: `1e-5`. Kelvin: `1e-4`.
  - PQ: `1e-4`. PQ is ill-conditioned in float32. `PqEncode` raises a ratio near 1 to the power 78.8, so each ulp of
    the ratio (6e-8) becomes 4.7e-6 of the signal. `PqDecode` subtracts nearly equal numbers and raises the
    quotient to the power 6.3.
- **`ColorPipeTest`** (the error is measured in the unit of the output format)
  - UNORM formats (BGRA8 for sRGB, A2B10G10R10 for PQ): 1 code. The GPU rounds to the nearest code, so the error is
    0.5 code at most. The float32 error of the encode adds about 0.0003 code for sRGB and 0.005 code for PQ. A tolerance
    of 1 code leaves room for that, and it still fails a wrong breakpoint, a wrong matrix and a wrong order of steps.
  - RGBA16F (scRGB): 1.01 ulp of a half float, at the largest channel of the reference pixel. This GPU truncates
    when it writes RGBA16F, so a result can be 1 ulp low (measured: 12.49219 where the reference is exactly 12.5). The
    0.01 is slack for the float32 error before the conversion. The unit is the ulp at the largest channel, because a
    small result of large terms has the float32 error of the terms.
  - `FalseColorEv` pixels closer than 0.01 stops to a zone boundary are not compared, because a difference of 1e-7
    between the shader and the reference could select another zone. The test exposures keep the gray ramp away from
    the boundaries.
  - Each image has two halves with the same scene. The top half has a transparent UI, and the bottom half has the UI
    pattern. Every pair of tonemapper and encoding runs with 3 exposures, then after `SetOverrides`, then after
    `Resize`.
  - `Passthrough` has extra invariants that need no reference: a gray stays gray, the gray ramp never goes down, no
    channel is above the peak, and nothing is NaN or infinite.

The matrices have no hand-derived table. The derivation is checked by properties: white maps to white, a round
trip is the identity, and the derived luminance weights equal the published weights.

## Decisions and their reasons

- **Display parameters are push constants.** They are three floats and a pad. A change of overrides then needs no
  buffer write and no synchronization. `ColorPipe.cpp` checks the reflected push constant size against its own
  `DisplayParameters` when it creates a program (`ShaderLayoutMismatch`), because the shader and the C++ declare the
  block twice.
- **The EV is written in the queue.** `RecordComposite` records a `FillBuffer` of the EV buffer, between two
  barriers. A frame in flight then reads its own EV. A CPU write at record time would change the value under a frame
  that the GPU has not read yet. The first barrier orders the read of the previous composite before the write. The
  second orders the write before the read of this composite.
- **The UI is premultiplied, and the shader un-premultiplies it.** Dear ImGui blends with `SRC_ALPHA` into a target
  that was cleared to transparent, so the stored color is the straight color times alpha. The shader divides by alpha
  before it decodes the sRGB, because the decode is not linear. A texel with no alpha covers nothing, and its color
  bytes are ignored. The test fills bytes that Dear ImGui could not write (a color above its alpha), to prove that
  the shader saturates the result.
- **The UI target is UNORM, not `_SRGB`.** Dear ImGui's colors and its alpha blending assume sRGB-encoded values in a
  UNORM target. An `_SRGB` target would blend in linear light and change how the UI looks. The composite decodes the
  UI itself.
- **The output formats are UNORM, never `_SRGB`.** The composite encodes in the shader. An `_SRGB` swapchain would
  encode a second time. This is why `ChooseSurface` never picks an `_SRGB` format.
- **The clip to headroom.** After the primaries conversion, the pass clips each channel to `[0, headroom]`. A
  conversion from Rec.2020 to Rec.709 makes negative values and values above 1. This is a clip and not a gamut map,
  and it is the simplest stage that keeps the output in range. A later step can replace it with a gamut map.
- **Where the tonemapper clips.** `Passthrough` clamps to the headroom. The pass clips again after the conversion
  of primaries, because the conversion can leave the range.
- **The HDR metadata uses the container primaries** (`DisplayTarget::Primaries`: Rec.709 for scRGB, Rec.2020 for PQ).
  It does not report the primaries of the panel. This is open.
- **`FullscreenDrawState` is a copy.** With shader objects, every part of the graphics state is dynamic, and a draw
  sets all of it. `FullscreenDrawState` sets the state of a fullscreen triangle with no depth and no blending. It
  duplicates `ShaderObjectState` in OceanFft, until the RHI has a wrapper for the state.
