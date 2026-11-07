# AgentTools: the self-describing tool surface

Namespace `agent`. This module lets a tool discover, read, write and call what an application exposes. It depends on
`foundation` only. It has no socket, no JSON parser and no Python. Those are the next layers.

```text
Agent <-> MCP over stdio <-> Python sidecar <-> JSON-RPC over loopback <-> this registry
```

The intent is to leverage the same reflection systems, schemas, and data that we already have to declare for things
like the `ContentCompiler` or just a regular ol' Editor runtime to agents as well - given them the ability to observe,
modify, and act on the engine and it's runtime data at the same depth and granularity as any other tool can.
## Files

| File | Contains |
|---|---|
| `ToolRegistry.hpp` | `ToolRegistry`, the `ToolCommand` concept, `NamedValue`, `NoParams`, `NoReply`. |
| `ToolReply.hpp` | `ReplySink`, `PendingReply`, `ReplyToken<Reply>`. |
| `ToolResult.hpp` | `ToolResultCode`, and `ToolResult`, the status record of a request. |
| `ToolResult.reflect.hpp` | The descriptors of `ToolResultCode` and `ToolResult`. |
| `ToolLine.hpp` | `RunToolLine`: one request from a line of text, for scripts and consoles. |
| `src/ToolSchema.*` | Writes JSON Schema from a `TypeInfo`. |

## Registration

Registration is opt-in. A descriptor alone doesn't expose anything - it's just the way we declare a reusable schema for
different modules and tools to use. The application connects objects and commands at startup, and then freezes the registry:

```cpp
const agent::ToolResultCode seaAdded = registry.AddObject("ocean.sea", seaState, seaChanged);
const agent::ToolResultCode moveAdded = registry.AddCommand(moveCommand);
registry.Freeze();
```

There is no self-registration, because static initializers depend on order, and a linker can drop an unreferenced
object file from a static library. The tool would then disappear with no error.

An object path cannot be a parent of another object path. `world.sea` and `world.sea.inner` are refused together, so
that each path has one owner. `world.sea` and `world.seashell` are fine.

## Objects

| Call | Result |
|---|---|
| `DescribeAll()` | JSON with every object and command, and their schemas. |
| `Describe(path)` | The schema of one command, object or field. |
| `GetValue(path, sink)` | Writes the value to a `CanonicalSink`. Use `JsonSink` for JSON. |
| `SetValue(path, value)` | Writes one scalar with `reflect::SetValue`, and then calls the object's callback. |

- `ocean.sea.WindForce` means the object `ocean.sea` and the field path `WindForce`. `foundation/include/reflect/README.md`
  has the rules of field paths.
- A refused write changes nothing and calls no callback.
- The callback receives the field path relative to the object. Use it for side effects. For example, the sea state must
  rerun its pipe after each write.
- `Hidden` removes a field from schemas and suggestions. A path can still reach it.

**This write is temporary.** `SetValue` writes a runtime struct directly, and the rule for live edits is that they become
proposals to the store. The direct write stays only until the store exists.

## Commands

A command is a struct with a name, a doc, and described `Params` and `Reply` types:

```cpp
struct CameraLookAtCommand
{
    static constexpr std::string_view Name{ "camera.lookat" };
    static constexpr std::string_view Doc{ "Places the camera at Eye, looking at Target." };
    using Params = CameraLookAtParams;
    using Reply = CameraReply;

    void Run(const Params& params, agent::ReplyToken<Reply> token);
};
```

`Call(name, arguments, reply)` makes a default `Params`, writes each argument through `reflect::SetValue`, and then runs
the command. So a command's limits come from the descriptor of its `Params`, and `Run` receives only checked values.

An argument is a path and a scalar. An array arrives as one value per element: `Eye[0]`, `Eye[1]`, `Eye[2]`. The JSON
layer will flatten its objects and arrays into this form. There is no separate decode walk.

| Refusal | When |
|---|---|
| `MissingArgument` | A `Required` field has no argument. A required fixed array of scalars needs one argument per element. |
| `DuplicateArgument` | Two arguments have the same path. |
| `ValueRefused` | `reflect::SetValue` refuses the argument. |
| `UnknownCommand` | No command has the name. |

`Required` applies to the fields at the top of `Params`. A field that is not `Required` keeps its default.

## Replies

A command answers through its `ReplyToken`, now or on a later frame, so fast and slow commands use one path. The token
encodes the reply as JSON when the command answers, so the reply value can go out of scope.

- A token answers once. A second answer is a defect, and it asserts.
- A token that is destroyed without an answer fails with `NoReply`, so that a lost command never hangs its caller.
- A `ReplySink` must outlive every token that points to it.

## Failures

`ToolResult` contains the result code, a sentence, the path, the position in the path where the walk stopped, and the
nearest valid names. The registry suggests. It never applies a suggestion, because that would hide the mistake from the
caller.

A function that yields no value, such as `SetValue`, returns the record itself. `Succeeded()` is the check, and a
record that nobody set is not a success. A function that yields a value returns `std::expected<T, ToolResult>`.

A suggestion is within an edit distance of 2, or a quarter of the length of the name if that is larger. The comparison
ignores case.

## JSON

`foundation/include/content/JsonSink.hpp` writes values:

- A struct becomes an object, a sequence an array, and an enum its case name. An empty optional becomes null.
- A struct that is a variant alternative gains a `$type` member. An enum alternative becomes `{"$type":...,"value":...}`.
- JSON has no NaN or infinity, so these become the strings `"NaN"`, `"Infinity"` and `"-Infinity"`.
- A float is written as the shortest text that reads back to the same float, so `0.1f` is `0.1`.

Schemas are JSON Schema, with a few additions: `x-unit`, `x-type` (the descriptor name), `x-case-docs` and `x-recursive`
(a type that contains itself, where the schema stops). An open limit has no `minimum` or `maximum`.

## Threads

There are no locks. `Freeze` ends registration, and the tables never change after it. Every request runs on the thread
that owns the objects. Only the frame thread calls the registry in `OceanFft`.

## In OceanFft

`applications/OceanFft/OceanSceneTools.cpp` defines what tools can see of the scene. Scripts reach it with the `tool`
command, and each reply prints `tool <id> ok <json>` or `tool <id> error <json>`:

```text
tool set ocean.sea.Depth 12
tool call camera.lookat Eye[0]=0 Eye[1]=30 Eye[2]=80 Target[0]=0 Target[1]=0 Target[2]=0
tool describe camera.set
```

The `sea` command also writes through the registry. The other text commands stay on their old path until they move.
