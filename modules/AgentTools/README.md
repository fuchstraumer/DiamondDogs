# AgentTools: the self-describing tool surface

Namespace `agent`. This module lets a tool discover, read, write and call what an application exposes. It depends on
`foundation`, and on nlohmann in one private source file. The Python sidecar is the next layer.

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
| `ToolServer.hpp` | `ToolServer`: JSON-RPC over loopback. See "Transport". |
| `src/ToolSchema.*` | Writes JSON Schema from a `TypeInfo`. |
| `src/ToolRequests.*` | Describe, get and set. `ToolLine` and `ToolServer` share these. |
| `src/ToolRpc.*` | Line framing, request parsing, flattening, and reply text. No socket, so tests call it directly. The only nlohmann user. |

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

## Transport

`ToolServer` speaks JSON-RPC 2.0 over TCP on 127.0.0.1, with one JSON message per line. Try it by hand with any TCP
client.

```text
sidecar <-> TCP <-> socket thread <-> two mwsrQueues <-> frame thread <-> ToolRegistry
```

- The socket thread reads bytes, splits lines, parses JSON, and writes replies.
- The frame thread calls `Pump(registry, max_requests)` once per frame. Only `Pump` calls the registry. The budget
  keeps a flood of requests from stretching a frame.
- The threads share two queues and one atomic stop flag. This code adds no lock, and it uses only the non-blocking
  queue calls.
- The socket thread sleeps in `Poll`. At the end of a `Pump` with work, the frame thread makes one non-blocking `send` on a
  private `SocketWaker`, so a reply goes out at once. The frame thread touches no other socket.

**Latency:** the round trip of a tool call is about one frame, because only the frame thread can run a handler. `server.ping`
shows what the socket itself costs, which is about 20 us.

| Method | Params | Result |
|---|---|---|
| `tool.describe` | `path` (optional) | One schema, or every schema without a path. |
| `tool.get` | `path` | The value. |
| `tool.set` | `path`, `value` (one scalar) | The value that the field now contains. |
| `tool.call` | `name`, `arguments` (optional object) | The reply of the command. |
| `server.ping` | none | `{"Pong":true}`. The socket thread answers this, with no pump. |

```text
{"jsonrpc":"2.0","id":1,"method":"tool.call","params":{"name":"camera.lookat","arguments":{"Eye":[0,30,80],"Target":[0,0,0]}}}
```

**A stalled frame:** a ping that succeeds while `frame.info` does not return means that the engine lives and its frame
is stuck. When the client has a full queue of unanswered requests, the socket is not read, so a ping also waits.

**Errors:**

| Code | Meaning |
|---|---|
| -32700 | The line is not JSON. |
| -32600 | The request is malformed, or the line is above 1 MiB. Batches, notifications and ids that are not integers are refused. |
| -32601 | The method does not exist. |
| -32602 | The params are wrong, including an unknown param name. |
| -32000 | The registry refused the request. `data` contains the `ToolResult`, with its suggestions. |
| -32001 | Another client is connected. |

A malformed line never ends the connection.

**Flattening:** `{"Eye":[1,2],"Lens":{"Fov":60}}` becomes the arguments `Eye[0]`, `Eye[1]` and `Lens.Fov`. These are the
limits:

- `null`, an empty array and an empty object are refused, because they would write nothing.
- Nesting deeper than 32 levels is refused, because the walk recurses.
- nlohmann keeps only the last of two equal keys, so `DuplicateArgument` cannot see this case.

**What goes wrong, and what the server does:**

- **Reused ids.** A client can disconnect during `frame.wait`, and the next client can use the same id. So the server
  assigns its own request ids, and a reply for an older connection is dropped. A request from a closed client that is
  already in the queue still runs.
- **Moved strings.** `NamedValue` contains views, and a move of a short `std::string` moves its bytes. So a queued
  request owns its strings, and `Pump` makes the views just before the call.
- **A full queue.** `mwsrQueue` holds 63 items through `try_push`, and that call destroys its argument when the queue is
  full. So the queues store raw pointers, and the sender keeps the `unique_ptr` until a push succeeds. A reply that does
  not fit waits in a frame-thread list for the next `Pump`. A request that does not fit stops the socket reads, and TCP
  then slows the client.
- **A client that does not read.** Above 16 MiB of unsent bytes the server stops reading requests. This limit is soft: the
  requests that are already in the queues still finish. With 512 KiB replies and a client that reads nothing, 65 of 300
  requests ran, or 124 to 128 when one more queue was in flight. Each `send` is capped at 64 KiB, because Winsock accepts a non-blocking send of any size while its send buffer
  has room. Without the cap, the whole backlog moves into kernel memory, and the limit never sees it.
- **End of file.** The server treats end of file as a closed client, so a client must keep its socket open until the
  last reply.

**Lifetime:** a `ToolServer` must outlive every `PendingReply`, because a reply points to it. `Stop` sends the replies
that exist and closes the port. A reply after `Stop` is dropped.

**Safety:** the server binds 127.0.0.1 with `SO_EXCLUSIVEADDRUSE`, and it accepts one client at a time. There is no
token, so any local process can write values while the server runs. An application starts the server only on request.

## In OceanFft

`applications/OceanFft/OceanSceneTools.cpp` defines what tools can see of the scene. Scripts reach it with the `tool`
command, and each reply prints `tool <id> ok <json>` or `tool <id> error <json>`:

```text
tool set ocean.sea.Depth 12
tool call camera.lookat Eye[0]=0 Eye[1]=30 Eye[2]=80 Target[0]=0 Target[1]=0 Target[2]=0
tool describe camera.set
```

The `sea` command also writes through the registry. The other text commands stay on their old path until they move.

The scene exposes these commands: `camera.get`, `camera.set`, `camera.lookat`, `frame.info` (with the screen size),
`frame.wait`, `frame.pause`, `debug.view` (the shader debug views by name), `app.quit`, and three captures:

| Command | Result |
|---|---|
| `capture.screen` | The final image as a PNG, downsampled by averaging in linear light (`Downsample`, 1 to 16). |
| `capture.region` | One rectangle of the final image at full resolution. |
| `capture.pixel` | One pixel: linear BT.709 (1.0 is paper white), the 8-bit sRGB value, and the stored bytes in hexadecimal. |

A capture replies after the file exists, a few frames later. `HideUi` hides the panel on the captured frame only. A file
name may use only letters, digits, `_` and `-`, so that a tool cannot write outside the capture folder. One frame records
one capture, and a script, burst or scheduled capture takes the frame first.

`--tool-port=N` starts the server. `--tool-port=0` lets the system choose, and the demo prints
`tool server listening on 127.0.0.1:<port>` before the slow startup work. `main` owns the server, so it outlives the
scene and every waiting reply. Up to 32 requests run per frame, in `tickToolSurface`, at the same point in the frame as
script commands. A bad port stops the demo with exit code 1.
