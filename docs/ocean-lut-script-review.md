# Review: `assets/scripts/ocean_fft_support.py` (2026-10-21)

The script generates the Donelan-Banner normalization LUT, the f16 sin/cos LUT, and the Mitsuyasu and Hasselmann
Q(s) polynomial fits, from `OceanFftConfig.yaml`. It came from VeloxRhi, where it runs as a build step. Nothing in
DiamondDogs runs it yet: `SpectrumInitCS` binds a 1x1 stand-in LUT, so the Donelan-Banner mode is not usable.

Ordered by impact. Item 1 was measured with an independent check of the same integral (2,000,001-point trapezoid
over [-pi, pi]); the script itself was not run here.

1. **The Donelan-Banner quadrature fails at large swell.** 48 Gauss-Legendre nodes over [-pi, pi]
   (`window = min(pi, 8 / beta)` is always pi, since beta <= 2.39). `cos(theta / 2)^(2s)` narrows like 1/sqrt(s):

   | s     | relative error of the integral |
   |-------|--------------------------------|
   | 100   | 1e-4 to 1e-3                   |
   | 1000  | 73%                            |
   | 14490 | 100% (integral 2e-17, Q = 5e16) |

   The config's swell max of 30 gives s_swell up to 16.1 * 900 = 14490, so most of the V axis is wrong.
   Fix: shrink the window with swell too, for example `min(pi, 8 / beta, c / sqrt(s))`, or substitute variables.
2. **The V axis is linear over [0, 14490].** 256 rows put about 57 units of s in each texel, and realistic swell
   (s up to about 50) falls into the first one. Use a log or sqrt mapping, and the same one in the sampling function.
3. **The LUT is transposed.** `lut_data[beta, swell]` flattens row-major, so texture x is swell and y is beta, but
   `SampleHasselmanLUT` samples u = beta, v = swell (line 426 warns about it). Write `lut_data.T`.
4. **The generated sampling function doesn't cook here.** It takes the texture and sampler as parameters, and
   Lodestone records sampler-texture pairs by tracing them to bound globals (`ReflectionSamplerPairUnresolved`).
   It also uses `Sample`, which has an implicit LOD and is not valid in compute. Read the globals and use
   `SampleLevel(..., 0)`, as `Spectra.slang` now does. The name says Hasselman, but it samples the Donelan-Banner LUT.
5. **The polynomial fits sample linearly in s.** The fit is in log space, but the 1024 samples and the split
   candidates are `linspace` in s, so small s has almost no samples and the reported error says nothing about it.
   `OceanFFT_Generated.slang` shows it: the Hasselman split landed at 14433 of 14490. Use `np.geomspace`.
6. **The sin/cos LUT only fits IFFT_SIZE 512.** It holds 512 angles 2 pi i / 512, and the shader reads
   `(p + 0.5) / IFFT_SIZE`. Other sizes read the wrong angle, and filtering blends two entries. One LUT at the
   largest size, read with an integer `Load(p << (log2(LUT size) - IFFT_LOG2_SIZE))`, works for every size.
7. **Check the sin/cos sign when it is wired in.** The LUT stores e^(-i theta), and the IFFT needs e^(+i 2 pi jk / N).
   Whether the call sites keep `.y *= -1` depends on the intent. OceanFftTest compares with a CPU FFT, so a wrong
   sign fails it at once.
8. **The generated C++ does not link.** The header declares `extern const uint32_t X[N]`, and the .cpp defines
   `constexpr uint32_t X[N]`: a namespace-scope constexpr has internal linkage. Use `extern constexpr`, or `const`.
9. Smaller: `matplotlib` is imported and unused (an extra build dependency). The cascade-parameter functions are
   never called. `fft_resolution` lists 2048, which the shaders cannot run. `use_f16` is ignored (Donelan-Banner
   is always f32, sin/cos always f16). The docstring says 5 values per line, the code writes 9.
10. **Two copies of the fits disagree.** `Spectra.slang` (included by the module) has older fits (swell clamp 16.1,
    beta down to 8e-7). `OceanFFT_Generated.slang` and `OceanFftGenerated.slang` are identical, newer, and
    included by nothing. When the script is a build step, `Spectra` should include its output instead.

## Related findings in the shaders (not changed)

- **The spectrum is about 2x too high.** OceanFftTest measures Hs = 4.19 m at 10 m/s wind and 100 km fetch.
  JONSWAP gives about 1.6 m, and a fully developed sea about 2.1 m (0.21 U^2 / g). `GetGaussianRandomFloat2`
  returns two independent N(0, 1), so E|xi|^2 = 2, and h0 has no 1/sqrt(2); the tick's h0(k) and conj(h0(-k))
  terms then add the variance twice. Multiplying h0 by 1/sqrt(2) gives the expected 2.1 m. The "multiplied by 2.0f"
  comment in `SpectrumInitCS` may be related.
- **`CascadeContribution` multiplies overlapping cascades.** It returns the number of cascades whose band covers k,
  and the amplitude is multiplied by it. With the demo's non-overlapping bands it is always 1, so nothing shows yet.

## Suggested plan

Fix items 1, 3, 4, 6 and 8, run the script as a DiamondDogs build step into the build directory, and upload the
Donelan-Banner LUT as `R32_SFLOAT` and the sin/cos LUT as `R16G16_SFLOAT`. Decide first whether the VeloxRhi
copy gets the same fixes, since the two would otherwise diverge.
