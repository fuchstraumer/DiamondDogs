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
                         │   Bind    │  LM301-LM305    each Bind("name", key) to one slot of that variant
                         └─────┬─────┘
                          BoundGraph ──Accesses──┐
                         ┌─────▼─────┐           │
                         │   Size    │  LM4xx    │     not written yet
                         └─────┬─────┘           │
                         ┌─────▼─────┐           │
                         │   Order   │◀──────────┘     LM5xx    edges, run order, hazards, dead writes
                         └─────┬─────┘
                          OrderedGraph
                         ┌─────▼─────┐
                         │  Layout   │  LM6xx          not written yet
                         └─────┬─────┘
                               ▼
                 Realize, Tick (LM7xx)                  not written yet
```

Each stage is a free function with a typed input and output, so a unit test can feed one stage directly. Each stage
reports every error it finds to a `DiagnosticSink`, not only the first one, and returns no output when it reported
an error. After Bind, no stage reads a binding name, and Tick touches no Lodestone type.

## The stages

| Stage | Entry point | Output | Code |
|---|---|---|---|
| Freeze | `Freeze(record, sink)` | `FrozenGraph`: every key resolved to a table index, passes in declaration order. | `src/freeze/` |
| Resolve | `Resolve(frozen, inputs, sink)` | `ResolvedGraph`: per pass, the environment, the variant key and the entry points. | `src/resolve/` |
| Bind | `Bind(frozen, resolved, sink)` | `BoundGraph`: the slot table and the access table. | `src/bind/` |
| Order | `Order(frozen, accesses, sink)` | `OrderedGraph`: the run order and the reason for each edge. `Explain` prints it. | `src/order/` |

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
3. `Params.cpp`: the `Params` statement against the entry point constants. LM305 is a size check, `sizeof(T)`
   against `Constants().ByteSize()`. The generated `Params<CookProfile>` already asserts each offset, so the size
   catches a type of the wrong profile. Constants that hold caller data and no `Params` statement are LM302.

`BoundGraph` holds:

- `Slots`: one `BindingSlot` per resource, all passes in one array. A pass's slots are
  `Slots[FirstSlot, FirstSlot + SlotCount)`. A slot has the placement form: `Bound` is a descriptor at (group,
  binding); `Handle` and `Pointer` are 8-byte values that Loom writes at `ValueOffset` in a uniform buffer or the
  push constants.
- `Accesses`: Order's input, one access per `FrozenPass::Binds` entry, after narrowing. A bind that the selected
  variant does not use gets `Invalid`, and Order skips it.

### Order

Order builds the edges (anchors, sequences, single-writer data flow), sorts with Kahn's algorithm and a
declaration-order tie-break, and checks the data: LM501 (cycle), LM502 (a hazard with no ordering path), LM503 (a
read before any write), LM504 (dead write) and LM505 (orphan pass). `Explain` and `ExplainPass` print the order and
the reason for each edge.

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

## Tests

`tests/unit_tests/loom` (`loom_tests`, no GPU). The Resolve and Bind tests read a bundle that the build cooks from
`shaders/` for `spirv` and `spirv-modern-mutable`, with a generated host header (`ShaderLibrary.host.hpp`).

| File | Covers |
|---|---|
| `test_builder.cpp` | What the builders record. |
| `test_frozen_graph.cpp`, `test_freeze_resolve.cpp`, `test_freeze_draw.cpp` | Freeze. |
| `test_resolve.cpp` (`LoomResolveTest.slang`, `LoomResolveOther.slang`) | Resolve, and the host header layout. |
| `test_bind.cpp` (`LoomBindTest.slang`) | Bind, Freeze's bind rules, and Freeze to Order with Bind's access table. |
| `test_order.cpp`, `test_explain.cpp` | Order and `Explain`, from hand-made access tables. |
| `test_diagnostics.cpp` | The catalog and the sinks. |

Every test must be able to fail: plant the fault, see the test fail, and restore the source.
