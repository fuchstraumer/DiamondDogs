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
| `ContentTypes.hpp` | `Quality`, `ArtifactKind`, `SlotUse`, `KindId`, `RecipeId`, `Slot`, `Output`. |
| `ContentDiagnostic.hpp` | `Severity`, `DiagnosticCode`, `Diagnostic`. |
| `ExecutorTypes.hpp` | What a kind's `Run` sees: `Inputs`, `Context`, `KindResult`, `Artifact`, `ArtifactRef`, `ContentError`. |
| `ArtifactTable.hpp` | `ArtifactTable`: every artifact, by slot and generation, with a holder count. |
| `Executor.hpp` | `Executor`, `Root`, `RootView`, `Transience`. A skeleton: see "Executor". |
| `src/NodeRow.hpp` | `NodeRow` and `NodeState`: one row per recipe. |
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
    static constexpr std::array Outputs
    {
        cc::Output{ "Value", cc::ArtifactKind::CpuBytes },
    };

    static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
};

registry.Register<ScaleKind>();
registry.Freeze();
```

A kind can make more than one artifact, for example a height image and a normal image from one dispatch. Each one is
an `Output` with a name and the kind of artifact that it produces:

```cpp
static constexpr std::array Outputs
{
    cc::Output{ "Height", cc::ArtifactKind::GpuImage },
    cc::Output{ "Normal", cc::ArtifactKind::GpuImage },
    cc::Output{ "Stats", cc::ArtifactKind::CpuBytes },
};
```

The `Kind` concept checks all of this at compile time. A slot needs a name, a use and an accepted kind. Slot names must
differ inside one kind. A kind needs at least one output. An output needs a non-empty name and a `Produces` that is not
`Invalid`. Output names must differ inside one kind. A kind that breaks a rule does not compile, so `Register`
has no runtime error for these.

`Register<K>` is the only template that touches a kind. It builds a `KindOps` table. The table stores the name, the
version, a span over the kind's static slot array, a span over its static output array, the identity of the params
type, and a `Run` function pointer. The pointer takes the params as `const void*`. T3 never calls `Run`.

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
3. the inputs in slot order: the slot name, the key of the input recipe, and the name of the output that the slot
   reads.

An optional slot with no edge is absent from the list. A connected optional slot changes the key.

Every edge names the output that it reads, also for a kind with one output. Two consumers that are equal except for the
output that they read get two keys and two ids. The key uses the name of the output and not its index. Reordering the
outputs of a kind therefore keeps the keys of its readers, and renaming an output changes them.

`RecipeInputRef::Output` is the index of that output in the `Outputs` of the producer's kind. Ingestion resolves the
name to the index, and the executor uses the index. `RecipeTable::Intern` takes the index, looks up the name in the
registry through the kind of the source recipe, and asserts that the index is in range.

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
slot, a slot that already has an edge, unknown output, slot kind mismatch), required slots, and cycles.

| Code | `Nodes` holds |
|---|---|
| `UnknownKind`, `ParamsTypeMismatch`, `NaNParameter` | the node |
| `DanglingEdge` | the end of the edge that exists (none if both ends are missing) |
| `UnknownSlot`, `SlotAlreadyConnected`, `SlotKindMismatch` | the consumer |
| `UnknownOutput` | the producer |
| `MissingRequiredSlot` | the node |
| `CycleDetected` | the nodes of one cycle, in edge order, starting at the lowest index |

`FieldPath` of a NaN is relative to the kind's params, for example `Nested.Weights[2]`.

A dangling edge fills no slot, so the required slot behind it is also reported missing. An edge with an unknown output
fills no slot either, so the required slot behind it is also reported missing.

`UnknownOutput` names the output and the kind of the producer, and lists the outputs that the kind has.
`SlotKindMismatch` compares the `Accepts` of the slot with the `Produces` of the output that the edge names, not with
a property of the whole kind. `ValidatedGraph` stores the output index of each filled slot beside the producer.

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

## Executor (T4, skeleton, 2026-11-07)

The types, the artifact table, `Root`, the run coroutine, and the four parts that decide correctness (`want`,
`unwant`, `cancel`, `complete`) are written. "How want walks" below traces `want`.

Three helpers are shared by the four parts:

- `drainUnwants` takes a list of rows, takes 1 from each row's `Wanted`, and cancels each `Waiting` row that reaches 0.
  A cancel adds the row's inputs to the same list. So a cancel cascade is a loop over a list, not a recursion.
- `releaseInputs` is called once for each row that leaves `Waiting`, `Ready` or `Running`. The row leaves the
  `Dependents` of each input that has not finished, and each input goes on the unwant list.
- `failDependents` starts from a row that is `Failed`. Every row that waits on it fails with `InputFailed` and the same
  `Origin`, and releases its inputs.

**Counts are per edge.** A row that reads two outputs of one input is in that input's `Dependents` twice, and counts 2
in its `Wanted` and in its own `Unresolved`. Every part counts the same way, so the numbers always match.

**Unwants wait until a walk ends.** `complete` and `want` collect their unwants in a list and drain it at the end.
During `want`, an early unwant could cancel a row that is still on the walk's stack, before that row has wanted its
own inputs. Its cancel would then release inputs that it never wanted.

**A status that is not `Succeeded` is a failure**, `Invalid` included. A `Succeeded` run with an output that is unset,
or that has another kind than the output declares, fails with `MissingOutput`. The diagnostics of each failure go into
the failure memo under a new `FailureId`, and every row that fails because of it shares that id.

**One row per recipe.** `rows[recipeId]` grows beside the recipe table. The row is the in-flight entry, the memory
cache and the failure memo, so no second table can disagree with it.

**Two counters.**

- `Unresolved`: the inputs that have not succeeded yet. This is the in-degree counter of Kahn's algorithm. At 0 the
  row is spawned. A `Spawn` during a `Tick` starts in that `Tick`, so a chain of nodes runs in one `Tick`.
- `Wanted`: the reasons to keep the row. Each root that targets it counts 1, and each dependent that is Waiting,
  Ready or Running counts 1.

| Event | Effect |
|---|---|
| `Wanted` 0 to 1 on an `Idle` row | It becomes `Waiting`, and wants each input in turn. |
| `Unresolved` reaches 0 on a wanted row | `Ready`: its run is spawned. |
| `Wanted` reaches 0 on a `Waiting` row | Cancelled: back to `Idle`, and it releases each input in turn. No frame exists yet. |
| `Wanted` reaches 0 on a `Ready` row | Its run cancels it when it starts. This avoids a second spawn if it is wanted again first. |
| The run starts | `Running`. It is never cancelled from here, and its result is cached, because it is correct. |
| The run finishes | `Succeeded` or `Failed`. Each dependent is told; a failure fails the dependents with `InputFailed`. Then the row releases its inputs. |

Two old rules come free from `Wanted`. A running node pins its inputs, because it still counts in their `Wanted`. A
shared node is not cancelled by one requester, because the other requester still counts.

### How want walks

`want(recipe, transience)` adds one reason to keep a row. When that is the row's first reason, the row starts, and it
wants its own inputs, which may start them in turn. So one call can start a whole subgraph. It has three steps.

1. **`addWant`** on the target: `Wanted` plus 1, and the stronger transience kept. If `Wanted` went 0 to 1 on an `Idle`
   row, the row becomes `Waiting` at once and goes on the `starting` list.
2. **The loop.** Pop a row from `starting` and call **`startRow`** on it, until the list is empty:
   - If any input is `Failed`, the row fails now with `InputFailed` and that input's `Origin`. It wants none of its
     inputs. `failDependents` fails every row that already waits on it.
   - Else, for each input edge: if the input has not succeeded, the row joins the input's `Dependents` and its own
     `Unresolved` goes up by 1. Then `addWant` on the input, which may put the input on `starting`.
   - If `Unresolved` is 0 (every input had succeeded already), the row is spawned: `Ready`.
3. **After the loop:** drain the unwants that the failures collected, then tell the roots about each row that failed.

The `starting` list is the explicit stack of an iterative depth-first walk. A recursive walk would use one machine
stack frame per row, and a chain of 10,000 rows would overflow it in Debug.

**The diamond.** `A` feeds `B` and `C`; `B` and `C` feed `D`. Nothing is cached. A root calls `want(D)`.

| Step | What runs | `starting` after | A | B | C | D |
|---|---|---|---|---|---|---|
| 1 | `addWant(D)`: 0 to 1, `Idle`, so `Waiting` | D | | | | W1 Waiting |
| 2 | pop D. B not done: D joins B's dependents, D.U 1, `addWant(B)` starts B. Same for C. D.U is 2, not 0 | B, C | | W1 Waiting | W1 Waiting | U2 |
| 3 | pop C. A not done: C joins A's dependents, C.U 1, `addWant(A)` starts A | B, A | W1 Waiting | | U1 | |
| 4 | pop A. No inputs, so A.U is 0: spawned | B | Ready | | | |
| 5 | pop B. A is `Ready`, not done: B joins A's dependents, B.U 1, `addWant(A)`: W 2, A is not `Idle`, so nothing more | (empty) | W2 | U1 | | |

`A` is on `starting` once, because only the 0-to-1 want of an `Idle` row pushes it. That is single-flight without an
in-flight table. Then `A` runs, and `complete` takes the rest: `B` and `C` reach `U0` and spawn in the same `Tick`,
then `D`.

**Why a row is `Waiting` before it is popped.** Suppose `P` reads `S` and `R`, in that slot order. `startRow(P)` pushes
`S`, then `R`, so `R` is popped first. `R` fails at once (its input failed long ago). `failDependents` fails `P`, and `releaseInputs(P)`
must take `P` out of `S`'s dependents. It looks only in inputs that are `Waiting`, `Ready` or `Running`. `S` has not
been popped yet, so if it were still `Idle`, `P` would stay in its dependents: a stale entry that `complete` would later
trip on.

**Why the unwants wait until the loop ends.** In the same case, `P`'s failure also unwants `S`. `S`'s count goes back to
0. If that unwant ran now, `S` would be cancelled while it is still on `starting`, before it had wanted its own inputs,
and its cancel would release inputs it never wanted: their counts would go below 0. After the loop, `S` has wanted its
inputs, so the cancel undoes exactly what the start did. `S` started for nothing, which costs a little work and no
correctness.

**Counts are per edge.** A row that reads outputs `A` and `B` of one `Pair` row is in the pair's dependents twice, and
counts 2 in the pair's `Wanted` and in its own `Unresolved`. `complete` takes 1 per dependents entry, and
`releaseInputs` releases once per edge, so every count returns to where it was.

**Transience.** Each row whose `Wanted` this call raises keeps the stronger of its old transience and the request's.
Rows below a row that had already started are not reached, so they keep theirs. Nothing reads transience yet: it is
for the transient tier's eviction, which is not designed in code.

**What a kind writes.** `Run(const Params&, Inputs, Context&)` returns `foundation::Task<KindResult>`. It sets each
output with `context.SetOutput(index, artifact)` and reports diagnostics with `context.Report`. `KindResult` holds
only a status, so it is trivially copyable. A failure's diagnostics stay in the executor under a `FailureId`.

**Teardown.** Destroy every `Root` first. Then call `Shutdown()`, tick the scheduler until `Idle()`, and destroy the
executor. The destructor asserts `Idle()`.