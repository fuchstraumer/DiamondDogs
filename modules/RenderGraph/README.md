# Loom

Loom is the render graph of DiamondDogs (namespace `loom`, target `loom`). A module declares its resources and
passes through a builder. Compile then turns those declarations into a checked, ordered graph, with no GPU. Realize
and Tick (not written yet) run that graph on a device.

The design record is the API spec, and the agent handbook holds the working state. Both are in the author's
untracked `docs/` folder. This README is the map of the code.

## The pipeline

```
 GraphBuilder ──Record()──▶ GraphRecord            what the caller wrote, with a source_location per statement
                               │
                         ┌─────▼─────┐
                         │  Freeze   │  LM1xx, LM306   names to indices, values in range, no shader
                         └─────┬─────┘
                          FrozenGraph
                               │            ◀── rhi::ShaderBundle (device-free), device capabilities and limits
                         ┌─────▼─────┐
                         │  Resolve  │  LM2xx          one variant and its entry points per pass
                         └─────┬─────┘
                          ResolvedGraph
                         ┌─────▼─────┐
                         │   Bind    │  LM301-LM305,   each Bind("name", key) to one slot of that variant
                         │           │  LM307
                         └─────┬─────┘
                          BoundGraph ──Accesses──┐
                         ┌─────▼─────┐           │
                         │   Size    │           │     LM401-LM403     sizes, extents, formats, usage
                         └─────┬─────┘           │
                          SizedGraph             │
                         ┌─────▼─────┐           │
                         │   Order   │◀──────────┘     LM5xx    edges, run order, hazards, dead writes
                         └─────┬─────┘
                          OrderedGraph
                         ┌─────▼─────┐   ◀── LayoutLimits (device limits)
                         │  Layout   │  LM601-LM604    group counts, attachments, feedback loops, limits,
                         └─────┬─────┘                  one description per distinct pipeline
                          LaidOutGraph
                               ▼
                 Realize, Tick (LM7xx)                  not written yet
```

Each stage is a free function with a typed input and output, so a unit test can feed one stage directly. Each stage
reports every error it finds to a `DiagnosticSink`, not only the first one, and returns no output when it reported
an error. After Bind, no stage reads a binding name, and Tick touches no Lodestone type.

**File layout (2026-11-20).** Each source file has a header of the same name beside it, and that header declares
what the source defines: `order/Sort.cpp` and `order/Sort.hpp`, `layout/Feedback.cpp` and `layout/Feedback.hpp`. A
stage's output type has its own header, apart from the stage function: `SizedGraph.hpp` holds the table, and
`Size.hpp` declares `Size()`, which `Size.cpp` defines (likewise `LaidOutGraph.hpp` and `Layout.hpp`). A
`*Context.hpp` holds only a stage's shared state, the context struct that its steps pass along, and has no source.
Public headers follow the same rule: `Compile.hpp` and `Compile.cpp`, `CompiledGraph.hpp` and `CompiledGraph.cpp`,
`DecideRuns.hpp` and `DecideRuns.cpp`. The headers forward-declare what they take by reference. Freeze follows the
rule too: `FreezeContext.hpp` holds its state, `Reports.hpp` its LM101 and LM109 helpers, and `Publish`, `ResolveKey`,
`ResolveNames`, `ResolveDraw` and `Validate` are header and source pairs. Only templates and `constexpr` functions keep
their bodies in a header. Source files include what they use directly (clang-tidy `misc-include-cleaner`, 0 findings
on 2026-11-20); headers include only what their declarations need.

## The stages

| Stage | Entry point | Output | Code |
|---|---|---|---|
| Freeze | `Freeze(record, sink)` | `FrozenGraph`: every key resolved to a table index, passes in declaration order. | `src/freeze/` |
| Resolve | `Resolve(frozen, inputs, sink)` | `ResolvedGraph`: per pass, the environment, the variant key and the entry points. | `src/resolve/` |
| Bind | `Bind(frozen, resolved, sink)` | `BoundGraph`: the slot table and the access table. | `src/bind/` |
| Size | `Size(frozen, bound, sink)` | `SizedGraph`: per resource, the buffer byte size, the image extent and dimension, and the GPU usage. | `src/size/` |
| Order | `Order(frozen, accesses, sink)` | `OrderedGraph`: the run order and the reason for each edge. `Explain` prints it. | `src/order/` |
| Layout | `Layout(frozen, resolved, bound, sized, limits, sink)` | `LaidOutGraph`: per pass, the group count and a pipeline index; the table of distinct pipeline descriptions. | `src/layout/` |

Layout reads no `OrderedGraph`: it checks each pass alone, so Order and Layout can run in either order.

### Freeze

Freeze needs no shader and no device. It publishes every declaration into one symbol table, then resolves each name
that a statement uses. It also runs the checks that need only the declarations:

- **LM101 to LM113:** a required field is missing, a key has two publishers or none, a value is out of range, a pass
  has no shader or two, or a statement that may appear once appears twice.
- **LM113 and LM306 on binds** (`CheckPassBinds` in `Validate.hpp`): a binding name bound twice in a pass, `Params`
  twice, and `BindPrevious` on a resource with no history ring. LM306 keeps its Bind number, but it needs no shader.

### Resolve

Resolve opens one module per pass from the device-free `rhi::ShaderBundle` and selects one variant:

1. `Shaders.cpp`: the module and the entry points exist (LM201), and their stages fit the pass kind (LM202).
2. `Pins.cpp`: the pass pins and the graph pins merge, pass value first, one value per axis.
3. `Variants.cpp`: one query from the merged pins, the device filter, then rounds of defaults until one variant
   remains (LM203 to LM205, LM207). LM206 runs last, on a clean graph only.

Pin modes decide gated axes. A pass pin is `Required`, so it can switch a gate on. A graph pin and a declared
default are `WhenActive`: they test a value only where the axis is active, so they never switch a gate on.

`Supports` must answer from the capabilities that the device has *enabled*, not from what the physical device
supports.

### Bind

Bind reads the layout of the variant that Resolve selected:

1. `Layout.cpp`: `MergedLayoutView::Merge` gives the pipeline layout, each resource once with the stages that read
   it. A dispatch merges its one compute stage, so a dispatch and a draw take the same path. Two stages that disagree
   at one placement are LM304.
2. `Slots.cpp`: each bind name maps to one binding.
   - A name in no variant of the entry points is LM301, with the nearest name. That search walks every variant, and
     it runs only on a miss.
   - A name that only other variants have is skipped with no diagnostic. One build lambda then serves every variant.
   - A key of the wrong kind is LM304. A narrowing that adds access is LM303: a narrowing may only remove access.
   - A binding of the selected variant that no bind names is LM302.
   - A view and its root bound to different keys in one pass is LM307 (see "Views and aliases" below). The check
     sees one pass only: a pass that binds the view without its root has nothing to compare with.
3. `Params.cpp`: the `Params` statement against the entry point constants. LM305 is a size check, `sizeof(T)`
   against `Constants().ByteSize()`. The generated `Params<CookProfile>` already asserts each offset, so the size
   catches a type of the wrong profile. Constants that hold caller data and no `Params` statement are LM302.

`BoundGraph` holds:

- `Slots`: one `BindingSlot` per resource, all passes in one array. A pass's slots are
  `Slots[FirstSlot, FirstSlot + SlotCount)`. A slot has the placement form: `Bound` is a descriptor at (group,
  binding); `Handle` and `Pointer` are 8-byte values that Loom writes at `ValueOffset` in a uniform buffer or the
  push constants.
- `Sources`: compile time only, beside `Slots`: the Lodestone reflection of each slot, and its pass and bind. Size
  reads it.
- `Accesses`: Order's input, one access per `FrozenPass::Binds` entry, after narrowing. A bind that the selected
  variant does not use gets `Invalid`, and Order skips it.

### Size

Size gives each declared buffer its byte size and its GPU usage (`src/size/Size.cpp`), and each image its extent,
dimension and GPU usage (`src/size/ImageSize.cpp`). Samplers need nothing from Size.

#### Buffers

Each binding or statement that names a buffer is a requirement:

- **Exact:** the shader states an element count (`[ls_element_count]`), so it uses exactly count times stride bytes.
- **At least:** a uniform block (its block size), or indirect arguments (the offset plus 12 bytes for a dispatch, 16
  for a draw).

Sizes compare in **bytes**, never in elements, so two typed views of one buffer agree. A `Bytes(n)` buffer must fit
every requirement. A `Derived` buffer takes its size from the first exact requirement, or from the largest one when
none is exact; a requirement that does not fit is LM401, and no requirement at all is LM402. `DescribeCreation`
decides whether the shader gives a size. An imported buffer gets no size here: its future brings it, and Realize
checks it (LM701).

Usage is the union of `RequiredBufferUsage()` over the shader bindings, plus the pass statements: indirect arguments,
vertex and index buffers, and a Record pass's uses. CPU access (maps, upload copies) belongs to Realize.

#### Images

- **Extent.** A binding states an extent with `[ls_extent_1d/2d/3d]`; the axes that it leaves out are 1. The rules
  mirror buffers. A `Fixed` image, or an import with an `Exact` expectation, must equal every stated extent (LM401).
  A `Derived` image takes the first stated extent, and every other one must equal it (LM401); none is LM402. An
  `OfTarget` image and an `AnyExtent` import get no extent here: Realize knows it and checks it (LM701). LM401 reports
  the first axis that differs, so each side is one number.
- **Format (LM403).** A storage binding reads and writes raw texels, so its format must equal the image's. A sampled
  binding must read the scalar class of the format: a float binding reads float and depth formats, a depth binding
  depth formats, a uint binding uint formats. Filterability and component count are not checked. A color target needs
  a color format and a depth target a depth format, in a draw or a Record pass.
- **Dimension.** Realize needs 1D, 2D or 3D, and the declaration does not state it. Size takes it from the binding
  shapes (an array or a cube is 2D with layers), and two bindings that disagree are LM403. An image that no shader
  binds is 2D.
- **Usage.** `Sampled`, `Storage` or `RenderAttachment` from each binding kind, plus targets and Record uses. A copy
  for an upload belongs to Realize.

In every LM403, `Expected` is the image's side (its format or first dimension), and `Actual` is the use's side.

### Order

Order builds the edges (anchors, sequences, single-writer data flow), sorts with Kahn's algorithm and a
declaration-order tie-break, and checks the data: LM501 (cycle), LM502 (a hazard with no ordering path), LM503 (a
read before any write), LM504 (dead write) and LM505 (orphan pass). `Explain` and `ExplainPass` print the order and
the reason for each edge.

### Layout

Layout is the last Compile stage. It gives Realize the facts that it needs to create pipelines and record dispatches,
so that Realize makes no decision of its own. Each check lives in the file named for its question (`src/layout/`).

- **Group counts** (`Dispatch.cpp`). A dispatch states threads, and the device runs groups. Each axis is the thread
  count divided by the reflected workgroup size, rounded up: 65 threads in groups of 8 is 9 groups, and the shader
  tests its bounds. `DispatchThreadsOver(image)` divides the image's extent from Size. An image sized from a frame
  target, or an import of any extent, has no extent yet, so the source is `AtRealize` and Layout keeps the workgroup
  size for Realize. An indirect dispatch reads its count from its buffer. A count above `maxComputeWorkGroupCount` is
  LM601.
- **Attachments, LM602** (`Attachments.cpp`). Each output location of the fragment shader needs a `Color` statement
  at that slot, and each `Color` slot needs an output: an attachment with no output receives undefined values. The
  output's scalar class must fit the format (float to a float format, uint to a uint format), and the shader must
  write every component that the format holds. That rule is the opposite of the one for sampled bindings: Vulkan
  fills the components that a sampled read lacks, but not the components that a fragment shader leaves out. A shader
  that writes depth needs a `Depth` statement. A frame target's format is known at Realize, which checks it there.
  `Expected` is the attachment's side, `Actual` the shader's side.
- **Vertex inputs, LM603** (`Attachments.cpp`). Loom has no vertex format statement, so each reflected vertex input
  is LM603, with the semantic in `Binding`. Draws read their vertices from a buffer with `SV_VertexID` (vertex
  pulling), as every ocean draw does. A vertex format statement waits for a real consumer; WebGPU may need one.
- **Feedback loops, LM604** (`Feedback.cpp`). A pass that reads an image that it also writes as a target reads
  undefined texels, because Loom records barriers between passes, never inside one. A depth target with depth writes
  off is read-only, so the pass may sample it (soft particles). An input attachment reads the target through the
  attachment, which is its purpose. A Record pass states both sides as uses.
- **Device limits, LM601** (`Limits.cpp`). Resolve already checks the workgroup size, invocations, workgroup memory
  and push constant bytes (LM207). Layout checks the rest that Compile can see: descriptor sets (highest group + 1),
  descriptors per stage (a combined image sampler counts as a sampler and a sampled image; `maxPerStageResources`
  counts the color attachments and not the samplers), color attachments (highest slot + 1), buffer ranges (Bind has
  no range, so a descriptor covers the whole buffer), and image extents per dimension. Only `Bound` slots are
  descriptors, and an unbounded array is not counted: bindless has its own limits. `LayoutLimits` holds the limits
  in `VkPhysicalDeviceLimits` terms, so Compile stays device-free; fill every field, because 0 rejects. `Expected` is
  the limit and `Actual` the value, both under the limit's name.
- **Pipelines.** Each dispatch and draw gets a `PipelineDescription`: module, variant suffix, entry points, color
  formats and blend per slot, depth format, compare and write, and raster state. Identity is by name (G9), never by
  `VariantKey`. Equal descriptions (`==`) share one row. There is no hash: the cache across recompiles belongs to
  Realize, and the pipeline shape itself waits for a design that suits Vulkan and WebGPU both.

## Formats

Image declarations use Lodestone's `TextureFormat` (`include/LoomFormats.hpp`), because reflection reports a storage
image's format in it. `ToTextureFormat` converts foundation's `ImageFormat`: for content the compiler imports, and
for a format the app picks at run time from what the device supports. It gives no value for a format that Lodestone
lacks (16-bit unorm, signed integer, three-component), and each of those is a request to Lodestone. The enum is
declared opaque in the header, so it stays light; code that names an enumerator includes Lodestone's
`ResourceFlags.hpp`. RhiResources' flags are in `RhiResourceFlags.hpp` (renamed 2026-11-19 to end a name clash).

## Views and aliases

Two words, two opposite ideas. Keep them apart in code, comments and diagnostics.

- **A view** is one resource seen through more than one binding, all valid at the same time. PlanetGen binds one
  buffer as atomic `uint`s in some entry points and as plain `int4`s where it only reads. The shader marks the second
  binding as a view of the first (`ViewOf:<root>` today, Lodestone request C27 later). Order treats every binding of
  one key as one resource, so views need nothing from Order. Size compares them in bytes; Bind checks that a view and
  its root name one key (LM307).
- **An alias** is more than one resource sharing one memory, never valid at the same time. Loom will alias transient
  resources whose lifetimes do not overlap (not written yet). Order's lifetimes must prove that they do not overlap.

## Diagnostics

`include/LoomError.hpp` lists the codes. The hundreds digit is the stage (`StageOf`), and `LoomError.cpp` holds the
catalog text and default severity. A `Diagnostic` holds views into key literals and the shader bundle, so keep the
bundle open while a sink keeps diagnostics. `CollectingSink`, `TextSink` and `JsonSink` are the sinks.

## Stubs for Lodestone

When Loom needs something from Lodestone, it stubs it here and adds a row to Lodestone's requests document.

| Stub | Where | Replaced by |
|---|---|---|
| R3: a boolean capability axis that still varies after the device filter takes `true` | `PrefersCapableValue`, `src/resolve/Helpers.cpp` | `PreferCapable()`, once request C25 makes it ignore axes that a capability gates off |
| A shim in an entry point parameter reflects as `data` in scope `entryPointParams_<name>` | `BindingNameOf`, `src/bind/Helpers.cpp` | Request C26 |
| A view names its root in the tag `ViewOf:<root>` | `ViewRootOf`, `src/bind/Helpers.cpp` | Request C27 |

## Tests

`tests/unit_tests/loom` (`loom_tests`, no GPU). The Resolve and Bind tests read a bundle that the build cooks from
`shaders/` for `spirv` and `spirv-modern-mutable`, with a generated host header (`ShaderLibrary.host.hpp`).

| File | Covers |
|---|---|
| `test_builder.cpp` | What the builders record. |
| `test_frozen_graph.cpp`, `test_freeze_resolve.cpp`, `test_freeze_draw.cpp` | Freeze. |
| `test_resolve.cpp` (`LoomResolveTest.slang`, `LoomResolveOther.slang`) | Resolve, and the host header layout. |
| `test_bind.cpp` (`LoomBindTest.slang`) | Bind, Freeze's bind rules, and Freeze to Order with Bind's access table. |
| `test_size.cpp` (`LoomSizeTest.slang`) | Size for buffers and images, and LM307. |
| `test_layout.cpp` (`LoomLayoutTest.slang`) | Layout: group counts, LM601-LM604, and pipeline sharing. |
| `test_order.cpp`, `test_explain.cpp` | Order and `Explain`, from hand-made access tables. |
| `test_diagnostics.cpp` | The catalog and the sinks. |

Every test must be able to fail: plant the fault, see the test fail, and restore the source.
