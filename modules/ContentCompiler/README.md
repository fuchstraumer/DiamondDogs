# ContentCompiler: kinds, recipes and ingestion

Namespace `cc`. This module is the T3 slice of the content compiler. It turns an authored graph into recipes. A recipe
is a node with a key. The key is a hash of everything that decides the node's result. The executor (T4) is not here.

The module depends on `foundation` only. It has no Vulkan, no `rhi` and no Loom. The key and descriptor code is in
`foundation/include/content` (see its README).

## The idea

- An edit changes a key. A changed key is a new recipe. No code invalidates anything.
- Recipes are stored once. Two nodes with the same kind, quality, params and input keys are one recipe.
- The table only grows in v0. Ids start at 1. Id 0 is invalid.
- Everything runs on one thread. The module has no lock and no atomic.

## Files

| File | Holds |
|---|---|
| `ContentTypes.hpp` | `Quality`, `ArtifactKind`, `SlotUse`, `KindId`, `RecipeId`, `Slot`. |
| `ContentDiagnostic.hpp` | `Severity`, `DiagnosticCode`, `Diagnostic`. |
| `ExecutorTypes.hpp` | Placeholders (`Context`, `Inputs`, `Result`) so that `Register<K>` compiles. T4 replaces them. |
| `ParamsBox.hpp` | `ParamsBox`: one kind's params, type-erased. |
| `KindRegistry.hpp` | The `Kind` concept, `KindOps`, `KindRegistry`. |
| `RecipeParams.hpp` | `RecipeParams<P>`: the struct that a recipe key encodes as its parameters. |
| `RecipeTable.hpp` | `Recipe`, `RecipeTable`: the interner. |
| `Ingest.hpp` | `AuthoredGraph`, `Ingest`. |
| `src/IngestValidation.*` | Phase 1 of `Ingest`: every refusal, and the topological sort. |

## What a kind author writes

```cpp
struct ScaleKind
{
    static constexpr std::string_view Name{ "test.Scale" };
    static constexpr uint32_t Version{ 3 };
    using Params = ScaleParams;                      // needs a Describe<ScaleParams>
    static constexpr std::array Slots
    {
        cc::Slot{ "Source", cc::SlotUse::Required, cc::ArtifactKind::CpuBytes },
    };
    static constexpr cc::ArtifactKind Output{ cc::ArtifactKind::CpuBytes };

    static foundation::Task<cc::Result> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
};

registry.Register<ScaleKind>();
registry.Freeze();
```

The `Kind` concept checks all of this at compile time. A slot needs a name, a use and an accepted kind. Slot names must
differ inside one kind. `Output` must not be `Invalid`. A kind that breaks a rule does not compile, so `Register`
has no runtime error for these.

`Register<K>` is the only template that touches a kind. It builds a `KindOps` table. The table stores the name, the
version, a span over the kind's static slot array, the output kind, the identity of the params type, and a `Run`
function pointer. The pointer takes the params as `const void*`. T3 never calls `Run`.

A second `Register` of one name returns `DuplicateKindName`. `Register` after `Freeze` asserts.

## ParamsBox

`ParamsBox::Make(value)` copies a value to the heap and stores a pointer to a static table of operations for its type:
copy, destroy, encode to a `CanonicalSink`, and `FindNaN`. The box has no virtual functions.

- Heap storage is the v0 choice. A params struct is small, and a box is copied only when a recipe is created.
- The identity of a type is the address of a per-type token (`ParamsTypeIdentity<P>()`). It needs no RTTI. The token is
  not `const`, because the linker may fold equal `const` data into one address.
- Ingestion compares the identity in the box with the identity in the kind. A mismatch is `ParamsTypeMismatch`.

## What a recipe key hashes

The stream is the one that `foundation::content::MakeRecipeKey` writes for `RecipeParams<P>`:

1. the domain, the encoding version, the kind name and the kind version;
2. the struct `cc.RecipeParams` with the fields `Quality` and `Params`;
3. the inputs in slot order: the slot name and the key of the input recipe.

An optional slot with no edge is absent from the list. A connected optional slot changes the key.

`Quality` is in every key (Preview and Final are different recipes). `RecipeTable` writes the stream through the
type-erased path of `ParamsBox`, into a `BytesSink`. The tests make the same key through the typed path of
`MakeRecipeKey`. The two paths share no code, and the keys must be equal.

## Interning, and why bytes decide

`RecipeTable` stores the canonical bytes of each recipe. The key is XXH3-128 of those bytes. A lookup that finds a key
compares the bytes:

- equal bytes: the same `RecipeId`;
- other bytes: a `KeyCollision` diagnostic. The table never merges them.

A real 128-bit collision cannot be found, so the tests use `InternComputed`, which takes a key and bytes from the
caller. One test stores a recipe, then offers the same key with changed bytes. Another stores a recipe under the key
that a node of a graph will get, and ingests the graph.

`Get` returns a reference that stays valid for the life of the table (the table is a deque).

## Ingestion

`Ingest(graph, quality, registry, recipes)` has two phases.

**Phase 1 validates everything and collects every diagnostic.** It does not stop at the first. It checks, in this
order of output: node kinds and params (unknown kind, params type, NaN with its field path), edges (dangling, unknown
slot, a slot that already has an edge, slot kind mismatch), required slots, and cycles.

| Code | `Nodes` holds |
|---|---|
| `UnknownKind`, `ParamsTypeMismatch`, `NaNParameter` | the node |
| `DanglingEdge` | the end of the edge that exists (none if both ends are missing) |
| `UnknownSlot`, `SlotAlreadyConnected`, `SlotKindMismatch` | the consumer |
| `MissingRequiredSlot` | the node |
| `CycleDetected` | the nodes of one cycle, in edge order, starting at the lowest index |

`FieldPath` of a NaN is relative to the kind's params, for example `Nested.Weights[2]`.

A dangling edge fills no slot, so the required slot behind it is also reported missing.

`SlotAlreadyConnected` is not in the list of codes in the plan. Two edges into one slot needed a code.

**Phase 2 prepares, checks, then interns.** It runs only if phase 1 found nothing.

1. Prepare: visit the nodes in topological order (Kahn's algorithm; of the nodes that are ready, the lowest authored
   index goes first). Write the canonical bytes and the key of each node. A key depends on the keys of the inputs and
   not on their ids, so no node needs the table for this step.
2. Check: look every key up in the table, and compare the nodes of the graph with each other. A stored key with other
   bytes, or two nodes with one key and different bytes, is a `KeyCollision`. Equal key and equal bytes are one recipe.
3. Intern: only if the check found nothing, add the recipes in topological order.

The result does not depend on the order of the authored nodes. A refused graph adds nothing to the table, and that
includes `KeyCollision`.

A cycle diagnostic lists one cycle, not every node that Kahn's algorithm left. Nodes that sit downstream of a cycle
are not on it. The walk goes backward from the lowest unsorted node along the lowest unsorted predecessor until a node
repeats.

## Tests

`content_compiler_tests` covers the proofs of the T3 brief. The test kinds are in `test_kinds.hpp`. Their `Run`
functions exist only so that the kinds register.
