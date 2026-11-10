# Noise library spikes (2026-11-08)

These probes answer the **[spike]** items of milestone M0 in the noise audit, on Slang `v2026.19-87` through the
Lodestone cooker of the DiamondDogs build (`spirv`) and Lodestone's own build (`wgsl`, with Tint). The build does not
cook them. To run one, cook it by hand:

```bat
lodestone_cooker_console -o out --target=spirv --dump-stage=resolved assets/shaders/compute/Noise/spikes/SpikeAutodiff.slang
```

| Spike | File | Result |
|---|---|---|
| S1 Autodiff call shapes, default `evalGrad` | `SpikeAutodiff.slang` | Passes, with one shape rule. |
| S2 Recursion through dynamic dispatch | `SpikeDynamicDispatch.slang` | Fails, as expected. Mode A stands. |
| S3 `export struct X : I = Generic<A, B>;` | `SpikeGenericAxis.slang` | Slang passes. Lodestone cannot do it (C16), and drops a tagged generic in silence (C15). |
| S4 Reflection paths of a nested generic `ParameterBlock` | `SpikeAutodiff.slang`, and the real kernels | Passes. |
| S5 Runtime octave count under `[MaxIters(16)]` | `SpikeMaxIters.slang` | Passes. The bound does not reach the SPIR-V loop control. |

## S1: autodiff call shapes, and a default `evalGrad`

Question: can `evalGrad` be an interface method with a default body that differentiates `eval`?

Yes. Three rules came out of it:

1. **A default body in an interface works.** An implementation that replaces it must say `override`, or Slang fails with
   E30853 ("missing 'override' keyword").
2. **`fwd_diff(field.eval)` on a value of a generic type crashes Slang**: internal error, assert
   `slang-ir-constexpr.cpp(536): paramCount == callArgCount`. Both forward and reverse mode work through a free function
   that takes the field as `no_diff`:

   ```slang
   [Differentiable]
   public float EvalFieldAt<F : IField3>(no_diff F field, float3 p) { return field.eval(p); }

   fwd_diff(EvalFieldAt<F>)(field, diffPair(p, float3(1.0, 0.0, 0.0)));
   bwd_diff(EvalFieldAt<F>)(field, pair, 1.0);
   ```

   `NoiseField.slang` uses this form (`EvalFieldAt`, `ForwardGradientOf`).
3. **A differentiable method that reads its own members needs `[NoDiffThis]`**, or Slang fails with E31159 ("no derivative
   for member on non-differentiable struct"). Node parameters are not differentiated, so every `eval` in the library is
   `[Differentiable, NoDiffThis]`.

Two more facts from the real kernels:

- `no_diff` decorates a call only (E38031). For a value, use `detach(x)`.
- Slang's `smoothstep` has no derivative (E41022). `NodesValue.slang` has `DifferentiableSmoothStep`.

The whole chain differentiates: `NoiseFractalCS` calls `evalGrad` (three forward passes) on `Fractal<BASIS, RULE>` for
every basis and rule, Swiss and Jordan included, and `NoiseSampleGraphCS` on a graph that uses every node. Both cook for
`spirv` and `wgsl`. **Not checked yet:** that the numbers are right. The audit's gradient triple check (analytic,
autodiff, central differences) needs a dispatch, which the noise kernels do not have yet.

## S2: recursion through dynamic dispatch

Question: can a node hold its source as an interface value, so that one shader runs any graph (the planet notes' "dynamic
dispatch" mode)?

No, on both targets, in two ways:

- A struct that holds an `IDispatchField` and conforms to `IDispatchField` fails with E41003: "type contains circular
  reference through conforming interface ... prevents computing a finite AnyValue size".
- A node that holds a child index and calls `gGraph[child].eval(...)` through the interface fails with E55201: "recursion not
  allowed".

Also: through Lodestone, the probe fails earlier with E50100 ("no type conformances found"), because the cooker adds no type
conformances to the link. `slangc -conformance` gets past that to the two errors above. No request was filed, because the plan
does not use dynamic dispatch.

Result: mode A (materialize every node) for the editor, and mode B (fused type expressions) for the cook. The audit
section 7.4 already planned this.

## S3: a generic type expression as an interface-axis value

Question: does `export struct GRAPH : I = Generic<A, B>;` link, and can a Lodestone interface axis take it?

- **Slang: yes.** With `Defs`, `Main` (declares `extern struct GRAPH : ISpikeGraph = SpikeConstantGraph;` and
  `ParameterBlock<GRAPH>`) and `Link` (`export struct GRAPH : ISpikeGraph = SpikeComposedGraph<SpikeLeafA, SpikeLeafB>;`),
  `slangc Main.slang -r Link.slang-module` emits SPIR-V that passes spirv-val, and WGSL whose block has the composed
  type's `scale` member. Passing both `.slang` files to one slangc call fails with E30200 (one translation unit), so link a
  precompiled module.
- **Lodestone: no.** An interface axis takes the non-generic types that carry `ls_axis_interface_impl`, and it links the
  bare name. A generic type that carries the tag is dropped from the axis with no error: this file tags three types, and
  `--dump-stage=space` shows two values. Request rows: C15 (the silent drop) and C16 (the open-world axis, the plan's L19).
- **Stub on the DiamondDogs side:** none is needed for M1 and M2. A fused graph is a `typealias` in a module of its own
  (`NoiseSampleGraph.slang`), or a non-generic struct that wraps the composition and carries the tag
  (`SpikeNamedComposition` in this file).

## S4: reflection paths of a nested generic `ParameterBlock`

Question: does reflection give a usable path for each parameter of a composed type?

Yes. `ParameterBlock<SpikeParameters>` with `SpikeShifted<SpikeQuadratic> composed` gives `composed.source.scale` at
offset 0, `composed.offset` at 16 and `analytic.scale` at 32. In the real kernels, `gFractalNode` of each variant has the
rule's own members: `fractal.rule.gain1` to `fractal.rule.dampScale` for Jordan (96 bytes), `fractal.rule.warpStrength`
for Swiss (80 bytes). So a member named after its input slot maps a path back to a node with no C++ mirror of the struct.

One fault in the report, not in the manifest: `ShaderLibrary.resources.txt` says "size: shader" for `gFractalNode`,
whose size changes with `RULE`. Request row C17.

## S5: a runtime octave count under `[MaxIters(16)]`

Question: does a loop with a uniform count and `[MaxIters(16)]` cook and validate?

Yes, on both targets. Facts:

- The SPIR-V `OpLoopMerge` has loop control `None` at `--O0` and at `--O2`: the bound does not reach the driver.
- Slang uses `[MaxIters]` for autodiff: a loop inside a differentiable function needs `[MaxIters(n)]` or `[ForceUnroll]`
  (slang-diagnostics.lua). So the fractal loop needs it, and the count must never pass it. `Fractal.eval` clamps the
  count to `[1, k_MaxOctaves]` for that reason.
- The count stays a runtime uniform (the audit's Q5 default). A generic count would multiply the variants by every count
  a user picks.
