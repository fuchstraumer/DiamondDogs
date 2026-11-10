# The noise field library

This folder holds the Slang shaders of the noise graph: a pull-based field library that the planet generator builds on.
Lodestone cooks it (`noise_shaders` in `modules/NoiseGraph`, SPIR-V), and `assets/scripts/cook_noise_wgsl.ps1` cooks it
for WGSL with Tint, to keep it portable. The content compiler (`cc`) will run its kernels; no C++ calls them yet.

It is a new implementation of the author's CUDA_Noise (her GPU port of libnoise, about ten years old). The old code is a
reference only. The section "What changed from CUDA_Noise" says what was wrong in it and what this library kept.

## The idea in five lines

- A **field** is a function that the caller evaluates at a position it chooses (`IField3.eval(p)`). That is a pull
  signature, so a node can move the position of its source: a **domain edge** (warp, turbulence). A pointwise node
  (add, curve, select) is a **value edge**.
- A **node** is a generic struct over its input fields, so a whole graph is one Slang type expression, such as
  `Select<Add<Mix, Detail>, Floor, Swells>`. Its parameters reflect as dotted paths: in `NoiseSampleGraphCS`, the
  Jordan fractal under the blend control is `graph.first.left.control.source.source.frequency` (81 members, 1200 bytes).
- **Mode A** (editor): one kernel for each node type, with every input materialized. The kernel set is bounded, so
  Lodestone cooks it ahead of time. `NoiseKernels.slang` is the start of it.
- **Mode B** (cook, final preview): the scheduler fuses a graph into one type expression and specializes it.
  `NoiseSampleGraph.slang` is one written by hand. Lodestone cannot yet take a type expression as an axis value (request
  C16); the spike notes explain.
- One fractal loop serves every octaved noise. What changes between FBM, Billow, RidgedMulti, Swiss and Jordan is an
  **octave rule** type.

## Files

| Module | File | Contains |
|---|---|---|
| `NoiseHash` | `NoiseHash.slang` | `Pcg3d`, `Pcg4d`, `HashCell`, the range reduction, hash to float. |
| `NoiseLattice` | `NoiseLattice.slang` | `LatticePoint`, `MakeLatticePoint`, the 12 edge gradients, the quintic fade. |
| `NoiseField` | `NoiseField.slang` | `IField3`, `IVecField3`, `IBasis3`, `IOctaveRule`, `OctaveState`, `k_MaxOctaves`, the autodiff gradient. |
| `NoiseBases` | `NoiseBases.slang`, `BasisPerlin.slang`, `BasisSimplex.slang`, `BasisValue.slang` | `Perlin3`, `Simplex3`, `Value3`. |
| `NoiseFractal` | `NoiseFractal.slang` | `Fractal<B, R>`, the octave rotation. |
| `NoiseRules` | `NoiseRules.slang` | `Fbm`, `Billow`, `RidgedMulti`, `Swiss`, `Jordan`. |
| `NoiseNodes` | `NoiseNodes.slang`, `NodesValue.slang`, `NodesDomain.slang` | Value nodes, `VecFromFields`, `Warp`, `Affine`. |
| `NoiseGrids` | `NoiseGrids.slang` | `PlaneGrid` (the plane z = 0). |
| `NoiseKernels` | `NoiseKernels.slang` | Mode A kernels `NoiseBasisCS` (axis `BASIS`) and `NoiseFractalCS` (axes `BASIS` x `RULE`): 15 variants. |
| `NoiseSampleGraph` | `NoiseSampleGraph.slang` | `NoiseSampleGraphCS`: one fused graph that uses every node, basis and rule. |
| | `spikes/` | The M0 probes and their notes. The build does not cook them. |

The kernels write a `float4` per element (value, then gradient) to a storage buffer over a `PlaneGrid`. That output form is
temporary: the rasterizer of the noise graph (cube face, lat-lon and HEALPix grids, textures, `Sampled` leaves) replaces it.

## Conventions

- **3D only.** 2D noise is 3D noise at z = 0 (`PlaneGrid`). A 2D basis for cube-face detail stays possible if a profile
  asks for it.
- **Ranges.** Every basis returns about [-1, 1]. Every rule's `finish` returns about [-1, 1]. Blend and Select read the
  control as it is, so map a noise control to [0, 1] with `ScaleBias(0.5, 0.5)`.
- **Gradients.** `IBasis3.evalGrad` returns the analytic gradient with respect to the position. `IField3.evalGrad` has a
  default body that differentiates `eval` with forward-mode autodiff, three passes. A field with a cheaper analytic gradient
  overrides it (with `override`).
- **Octave count.** A runtime uniform, clamped to [1, 16]. The loop has `[MaxIters(16)]`, which autodiff needs.
- **Seeds.** Octave i uses `seed + i`. The seed is the fourth lane of `Pcg4d`, so every seed bit reaches every output bit.
- **Integer flags in uniforms** are `int` (0 or 1), because WGSL cannot put a `bool` in a uniform buffer.
- **`[Differentiable, NoDiffThis]`** on every `eval`: positions are differentiated, node parameters are not.

## Precision: `LatticePoint`

Float32 has a 24-bit mantissa. At planet scale (radius about 2^22.6 m) a lattice coordinate of 2^22 keeps about one bit of
fraction, and the noise falls apart. Every basis therefore takes a `LatticePoint { int3 cell; float3 frac; }` and reads only
`frac` as a float. Today `MakeLatticePoint(float3)` builds it, so the precision is still that of the float3 position. Later
the host can compute each tile's cell and fraction in double, per octave, and no basis changes (decision D2).

Simplex noise skews the lattice, and a naive skew adds `sum(cell) / 3` as a float, which loses the fraction again.
`Simplex3` keeps the skew exact: with `k = sum(cell) = 3q + r` (r in {0, 1, 2}),

```
skewed = (cell + q) + frac + (r + sum(frac)) / 3
x0     = frac - step + (r + sum(step)) / 6        where step = floor(frac + (r + sum(frac)) / 3)
```

`q` cancels in the unskew, so every float in the computation is small, whatever the cell index. The integer sums
overflow near 2^31 / 3, far beyond any planet lattice.

## The bases

| Basis | Algorithm | Gradient | Scale |
|---|---|---|---|
| `Perlin3` | Gradient noise, quintic fade, 12 edge gradients (Perlin 2002). | Analytic: the trilinear interpolation expanded into eight terms. | 0.984766 |
| `Simplex3` | Simplex noise, kernel `(0.5 - r^2)^4 * dot(g, x)`, 12 edge gradients. | Analytic, as in Gustavson's `sdnoise`. | 76.881376 |
| `Value3` | Hashed corner values in [-1, 1], quintic fade. | Analytic. | 1.0 (exact) |

The scale is 1 / (largest |n| found). `assets/scripts/measure_noise_ranges.py` measured it: 400,000 random points for
each of three seeds, then a local search from the 64 largest. The search finds a lower bound of the true maximum, so a
value can pass 1 by a small amount. Perlin3 reached 1.01547, Simplex3 0.013007 (with the scale at 1), Value3 0.999997.

The gradient pick takes the high 16 bits of the hash and maps them to 12 values with a multiply and a shift (Lemire). The
bias is at most 12 / 65536, and no lookup table of hashes exists.

## The fractal and its rules

`Fractal<B, R>` runs one loop. For octave i:

```
scaled   = p * frequency_i + state.sampleOffset        (the rule writes sampleOffset)
sample   = mul(scaled, rotation_i)                      (rotation_i = R^(i+1), or identity)
noise    = basis.evalGrad(MakeLatticePoint(sample), seed + i)
gradient = mul(rotation_i, noise.yzw)                   (back to the scaled space)
rule.step(state, {i, frequency_i, lacunarity}, float4(noise.x, gradient))
frequency_(i+1) = frequency_i * lacunarity
```

The loop owns the frequency, the seed and the rotation. The rule owns the sum, the amplitude and the warp, through one
shared `OctaveState`. (The audit sketched an `associatedtype State` for each rule. One shared struct is simpler to read and
to differentiate; a new rule adds a field.)

`R` is a rotation by the golden angle (2.39996 rad) about the unit axis (0.36, 0.48, 0.8). Without it, the lattices of
successive octaves coincide at the origin, and at a lacunarity of exactly 2 they share lattice points. `rotateOctaves = 0`
turns it off, to compare with the old look.

Octave 0 is rotated too. An unrotated octave on the plane z = 0 samples a lattice plane. There, Perlin's edge gradients
`(±1, 0, ±1)` have no y component, so when both corners of a cell edge get one, n is exactly 0 along that whole edge.
Ridged rules (Swiss, RidgedMulti) turn n = 0 into a crest, which drew straight seams one cell long in the first Swiss
renders (2026-11-08). FBM hides the fault. With `rotateOctaves = 0` the seams come back, and so does any bare basis
evaluated on z = 0 (`NoiseBasisCS`).

## Sample heightmaps

`NoiseHeightmaps` (in `modules/NoiseGraph`) is a headless app that dispatches `NoiseFractalCS` for FBM, RidgedMulti,
Swiss (both warp modes) and Jordan over a 5 x 5 area at 2048 x 2048. It writes a 1024 x 1024 height PNG and a
shaded-relief PNG for each. The relief is lit from the kernel's own gradient, so it also shows the gradient visually.
Run it with an output folder: `NoiseHeightmaps <folder>`.

It also renders a 256 x 256 patch at a spacing of 0.0002 (about 39 elements per wavelength of the finest octave), and
compares the autodiff gradient with central differences of the value. Results on 2026-11-08 (RTX 4080, both presets
the same):

| Sample | Relative RMS error | Median relative error |
|---|---|---|
| FBM, Perlin3 | 0.0010 | 0.0009 |
| RidgedMulti, Simplex3 | 0.0348 | 0.0004 |
| Swiss, Perlin3 | 0.0879 | 0.0013 |
| Swiss compounding, Perlin3 | 0.0911 | 0.0014 |
| Jordan, Perlin3 | 0.0025 | 0.0011 |

The medians show that forward-mode autodiff through the whole loop is right, including Swiss and Jordan, which
differentiate the analytic basis gradient again. The larger RMS of the ridged rules comes from the `abs(n)` creases: a
central difference across a crease is not the one-sided gradient.

| Rule | Contribution of octave i | Next amplitude | Source |
|---|---|---|---|
| `Fbm` | `amp * n` | `amp * gain` | Musgrave, *Texturing and Modeling* |
| `Billow` | `amp * (2 abs(n) - 1)` | `amp * gain` | Perlin's absolute-value turbulence (1985) |
| `RidgedMulti` | `(offset - abs(n))^2 * weight * lacunarity^(-H i)`, then `weight = saturate(signal * gain)` | spectral weight | Musgrave, *Texturing and Modeling* |
| `Swiss` | `amp * (1 - abs(n))`; `dsum += amp * gradient * -n` | `amp * gain * saturate(sum)` | de Carpentier, 2012 |
| `Jordan` | `damped_amp * n^2`; `dsum_warp += warp * n * gradient`, `dsum_damp += damp * n * gradient` | `amp * (1 - dampScale / (1 + dot(dsum_damp, dsum_damp)))` | de Carpentier, 2012 |

Notes on the rules:

- **Normalization.** Each `finish` divides the sum by the largest sum the octaves could make, then maps it to [-1, 1]. Swiss
  and Jordan use the undamped amplitudes for that bound. The loops themselves use the raw sum: Swiss's `saturate(sum)` sees
  exactly what de Carpentier's code sees. Only the output scale differs from the published functions, which return the raw
  sum.
- **Swiss warp.** `compoundingWarp = 0` is de Carpentier's form: octave i samples at `(p + warp * dsum) * frequency_i`.
  `compoundingWarp = 1` is the CUDA_Noise form, where the offset is multiplied by the lacunarity again each octave. At
  lacunarity 2 and 8 octaves, the offset of the first octave arrives 128 times larger, which is why the old code needed a
  warp of 0.01 to 0.02, against de Carpentier's 0.15. The author keeps both (question Q6).
- **Swiss `-n`.** The chain rule gives `gradient * -sign(n)` for `1 - abs(n)`. De Carpentier uses `-n` on purpose, to avoid
  artifacts from the jump of `sign`. The rule keeps `-n`.
- **Jordan** evaluates its first octave with its own gain, warp and damping (`gain1`, `warp0`, `damp0`), as de Carpentier's
  code does before its loop. Here the loop calls the rule for octave 0 too, and the rule branches on the octave. Its warp is in
  sample space (`p * frequency + dsum_warp`), not scaled by the frequency, as in the source. De Carpentier offsets the
  seed by `i / 256` (a float seed); this library uses the integer `seed + i`.
- The de Carpentier defaults are in the comments of each rule.

## The nodes

| Node | Output | Bound |
|---|---|---|
| `Const` | `value` | `abs(value)` |
| `ScaleBias<S>` | `s * scale + bias` | from the source |
| `Abs<S>`, `Invert<S>` | `abs(s)`, `-s` | from the source |
| `Clamp<S>` | `clamp(s, lower, upper)` | from the limits |
| `Exponent<S>` | s mapped to [0, 1], raised to `exponent`, mapped back | from the source |
| `Add`, `Multiply`, `Min`, `Max` `<L, R>` | pointwise | from the inputs |
| `Power<B, E>` | `sign(b) * abs(b)^e` | unknown |
| `Blend<L, R, C>` | `lerp(l, r, saturate(c))` | from the inputs |
| `Select<F, S, C>` | `f` where `lower <= c <= upper`, else `s`; smoothstep edges of half-width `falloff` | from the inputs |
| `Curve<S>` | Catmull-Rom spline through up to 16 points `(input, output)` | unknown (the spline can overshoot) |
| `Terrace<S>` | between sorted levels, `lerp(lower, upper, t^2)`; `invert` flips it | from the levels |
| `VecFromFields<X, Y, Z>` | a vector field from three scalar fields | length of the bounds |
| `Warp<S, W>` | `s(p + amount * w(p))` | from the source; displacement `amount * w.bound()` |
| `Affine<S>` | `s(mul(float4(p, 1), transform))`; replaces translate, scale and rotate | from the source |

`bound()` is in the interface because the scheduler will size the aprons of domain warps from it.

The host writes Curve and Terrace control points sorted, with at least 4 Curve points and at least 2 Terrace levels. The
shader clamps the counts to [2, 16] and trusts the order: the editor validates once, at ingestion.

## What changed from CUDA_Noise

These shaders were the author's first real compute code, and the instincts in them were good. What this library kept:

- **The pull instinct.** `Module3D` evaluated a field at positions that the caller supplied (`Sphere::Build`). That is the
  whole interface of this library.
- **The simplex derivative structure.** The old `simplex3d` gradient (`-8 t^3 (g.x) x + t^4 g`, per corner) was right. It
  is kept, and so is its corner-ordering branch structure.
- **The Swiss loop.** The amplitude rule `amp *= persist * saturate(sum)` and the `-n` gradient weight were correct. The
  compounding warp is kept as an option, because its look is distinctive.
- **The node vocabulary**, and the pointwise math of Select, Curve and Terrace.

What was wrong, and what replaced it (the audit numbers each defect):

| Defect | Old code | Now |
|---|---|---|
| N2 | FNV-1a on whole words, folded to 8 bits: the low k bits depend only on the low k input bits. All 2D noise tiled every 1024 cells, and seeds equal mod 1024 gave the same noise. | `Pcg4d`, with the seed as the fourth lane, and the gradient pick from the high bits. |
| N3 | Gradient tables with only axis-aligned vectors (4 in 2D, 6 in 3D, and 256 is not a multiple of 6). | Perlin's 12 edge gradients, picked with a bias of at most 12/65536. |
| N4 | The Perlin derivative evaluated the fade derivative at the faded value, and one polynomial matched no term. | The derivative of the expanded trilinear form, written again from the math. |
| N5 | Simplex 2D skew `F2 = 0.366035403` (a typo for 0.3660254038). | No 2D basis (z = 0). The 3D skew and unskew are exact rationals with an integer split. |
| N6 | Simplex kernel radius `r^2 = 0.6`, which cut each kernel at simplex boundaries. | `r^2 = 0.5`, and a measured scale. |
| N7, N8 | `volatile` hot locals, `static float` constants. | Plain locals, `static const`. |
| N10 | Scale factors (`1.5`, `40`, `28`) not checked. | Measured scales, and the method in this file. |
| N11 | Float coordinates with no cell and fraction split. | `LatticePoint`, with an exact simplex skew. |
| Billow | Summed `abs(n)`. | `2 abs(n) - 1`, normalized. |
| RidgedMulti | Summed `1 - abs(n)` with no feedback: ridged turbulence. | Musgrave's ridged multifractal: squared signal, feedback weight, spectral weights. |
| Swiss 1 | The warp compounded (`point *= lacun; point += warp * dsum`). | De Carpentier's form by default; the old form behind `compoundingWarp`. |
| Swiss 2 | `d_swiss_3d` wrote the gradient into `d`, not `dsum`, so the 3D warp was zero. | The rule accumulates into `warpSum`. |
| Swiss | Warp strength hard-coded (0.02 simplex, 0.01 Perlin), seeds unused. | `warpStrength` is a parameter; each octave has its own seed. |
| All | No normalization; octaves shared one lattice origin. | Every rule returns about [-1, 1]; the octave rotation. |

No code from libnoise is here (decision D3: libnoise is LGPL). The node names follow libnoise, because its users know them.

## Not done yet

- **Numeric checks.** Only the autodiff against central differences check exists (`NoiseHeightmaps`). Still to come: the
  basis statistics and the hash against a NumPy reference, analytic against autodiff for the bases, each rule against a
  NumPy reference, mode A against mode B.
- **Jordan's normalization** is loose: the bound assumes n^2 = 1 in every octave, so the output sits in about
  [-1, -0.43]. The other rules use most of [-1, 1].
- **RidgedMulti on Simplex3** shows a few short straight creases. Not traced yet: they may be kernel edges that the
  ridge sharpens.
- **Bases and nodes of milestone M3 and later:** `Cellular3` (plates need cell ids and edge distance), `SphereWarp`, `Turbulence`,
  `Displace`, the stream-function flow, and the curvature rules.
- **Grids and the rasterizer:** cube face, lat-lon, HEALPix, texture output, `Sampled` leaves.
- **Performance:** each basis `eval` calls `evalGrad` (todo-perf: a value-only path), and a fractal's gradient comes from
  three forward-mode passes.

## Credits

- **Ken Perlin**: gradient noise ("An Image Synthesizer", SIGGRAPH 1985), the quintic fade and the 12 edge gradients
  ("Improving Noise", SIGGRAPH 2002), simplex noise ("Noise hardware", SIGGRAPH 2001 course notes), and absolute-value
  turbulence.
- **Stefan Gustavson**: "Simplex noise demystified" (2005), and `sdnoise` (2003), whose analytic-derivative structure the
  simplex gradient follows.
- **Mark Jarzynski and Marc Olano**: `pcg3d` and `pcg4d`, "Hash Functions for GPU Rendering", JCGT vol. 9 no. 3 (2020),
  building on Melissa O'Neill's PCG.
- **F. Kenton Musgrave**: fBm and the ridged multifractal, in Ebert, Musgrave, Peachey, Perlin and Worley, *Texturing and
  Modeling: A Procedural Approach*, chapter "Procedural Fractal Terrains".
- **Giliam de Carpentier**: Swiss and Jordan turbulence, "Scape: 3. Procedural extensions" (2012,
  https://www.decarpentier.nl/scape-procedural-extensions). The page states no license, so this code is written from the
  math, not copied.
- **Inigo Quilez**: the expanded form of the gradient-noise derivative (iquilezles.org, article on noise derivatives).
- **Daniel Lemire**: the multiply-shift range reduction ("Fast Random Integer Generation in an Interval", 2019).
- **Edwin Catmull and Raphael Rom** (1974): the Catmull-Rom spline of `Curve`.
- **libnoise** (Jason Bevins): the node vocabulary only.
- **The author's CUDA_Noise**: the pull instinct, the simplex derivative structure, the Swiss loop and the node set that this
  library grew from.
