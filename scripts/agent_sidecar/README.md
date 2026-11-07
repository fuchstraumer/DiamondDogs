# Agent sidecar: MCP for DiamondDogs

`diamond_dogs_mcp.py` is an MCP server over stdio. An agent speaks MCP to it, and it speaks JSON-RPC to the engine's
`agent::ToolServer` on 127.0.0.1. The official MCP Python SDK (the `mcp` package, version 2) handles the MCP side:
the handshake, the protocol versions, the schemas and the stdio transport. This file holds only the tools.

```text
agent <-> MCP over stdio <-> diamond_dogs_mcp.py <-> JSON-RPC over 127.0.0.1 <-> OceanFft --tool-port
```

## Setup

Install the SDK for the Python that runs the sidecar. On this machine that is `C:\Python315`, and the package is in the
user site-packages:

```bash
C:/Python315/python.exe -m pip install --user "mcp>=2,<3"
```

pip warns that the user `Scripts` folder is not on PATH. The sidecar does not need it: that folder holds only the SDK's
own command-line programs. The sidecar exits with a clear message when the SDK is missing.

Register the sidecar with an MCP client. Name the Python by its full path, so that the client starts the Python that has
the SDK. For Claude Code:

```bash
claude mcp add diamond-dogs -- C:/Python315/python.exe C:/SoftwareDev/DiamondDogs/scripts/agent_sidecar/diamond_dogs_mcp.py --executable C:/SoftwareDev/DiamondDogs/build/ninja-msvc/applications/OceanFft/Release/OceanFft.exe
```

- `--executable` is the engine that `engine_launch` starts. The `DIAMOND_DOGS_ENGINE` variable also sets it.
- `--port N` connects at startup to an engine that already runs with `--tool-port=N`.

## Tools

| Tool | What it does |
|---|---|
| `engine_launch` | Starts the engine with `--tool-port=0` and more arguments, reads the port from its output, and connects. |
| `engine_connect` | Connects to an engine that already runs. |
| `engine_status` | Returns the connection, the frame, the screen size, and the last lines of the engine's output. |
| `engine_stop` | Calls `app.quit`, so that the engine writes its captures, and waits for the exit. |
| `describe` | The JSON Schema of everything, or of one path. The names, units, limits and docs come from here. |
| `get`, `set` | Read or write a value by path, for example `ocean.sea.WindForce`. |
| `call` | Runs an engine command with arguments, for example `camera.lookat` or `debug.view`. |
| `wait_frames` | Returns after N frames, so that a change shows. |
| `capture_screen` | The final image, downsampled in linear light, returned as an image. |
| `capture_region` | One rectangle at full resolution, returned as an image. |
| `capture_pixel` | One pixel: linear BT.709, 8-bit sRGB, and the stored bytes. |
| `hold_time` | Pauses at an exact simulation time in repeatable mode, so that a capture is the same byte for byte each time. |
| `step_time` | Advances the paused sea by an exact step per frame, for N frames, in repeatable mode. |
| `capture_diff` | Compares two captures: identical or not, the largest and mean difference, the changed pixels and their box, and a heat map. |

The engine's own commands, which `call` runs: `camera.get`, `camera.set`, `camera.lookat`, `frame.info`, `frame.wait`,
`frame.pause`, `frame.time`, `frame.step`, `frame.repeatable`, `debug.view`, `app.quit`, and the three captures. `describe` lists them with their schemas.

Useful engine arguments for `engine_launch`: `--window=WxH`, `--borderless` (the display's full size), `--size=128|256|512`,
`--dt=SECONDS` (a fixed simulation step, so that runs repeat), and `--script=FILE`.

## Behavior to know

- **Timeouts.** Each call waits 10 s by default; `wait_frames` adds 0.1 s per frame, and a capture waits 60 s. On a
  timeout the sidecar pings the engine. A ping that works means that the frames stopped: tool calls run only while frames
  render, and a minimized window renders none. The request stays queued, and it runs when frames start again.
- **One client.** The engine accepts one connection. `engine_connect` fails with a clear message when another client
  holds it.
- **Captures are files.** The engine writes PNGs to its `captures` folder, and the reply contains the absolute path. An
  inline image above 8 MiB is left out, with a note. Use a downsample or a region.
- **Paused is not static, unless it is repeatable.** `frame.pause` stops the simulation, but TAA still jitters by up to a
  pixel each frame, so two captures of a paused sea differ by up to 76 code values at edges. `hold_time` and
  `step_time` turn on repeatable mode (`frame.repeatable`): TAA stops (no jitter and no resolve, so edges alias), and
  auto exposure reaches its target in one frame (today the image does not use the auto exposure). A paused time then gives the same bytes whatever came before it:
  `check_engine.py` reaches time 10 by a jump and by 16 steps from 9.5, after a jump from 37.3, and compares.
- **Times are exact.** The compute shaders get the time as a 32-bit float, and every reply returns that float. The
  engine keeps the double that a tool sent, and N steps add to it in double, so N steps of `s` from `T` store the same
  float as `hold_time` with `T + N * s`. A time that the sea reached by running is only a float.
- **Foam is history.** `hold_time` rebuilds the persistent foam for the new time (the replay that "Rebuild foam" runs).
  `step_time` advances the foam step by step instead, so with foam on, a stepped frame and a held frame can differ.
  Foam is off by default.
- **A/B recipe.** `hold_time` T, `capture_screen` (keep its `Path`), change one thing, `hold_time` T again,
  `capture_screen`, then `capture_diff` with both paths. `identical: true` means that the change has no visible effect
  at T. `capture_diff` compares 8-bit PNGs, so a change below one code value does not show.
- **Debug views go through the tonemapper.** `debug.view` changes what the final image shows, so the colors are not raw
  values. Use them to see where something happens.
- **Shutdown.** When the MCP client closes stdin, the sidecar stops an engine that it launched, and leaves an engine that
  it only connected to.
- **stdout carries only MCP.** Log lines go to stderr. While the SDK serves, it points the process's file descriptor 1 at
  stderr, so a stray `print` cannot corrupt the stream. On Windows the SDK ends each message with `\r\n`. A JSON parser
  reads the `\r` as whitespace, so clients accept it.
- **One engine thread.** The SDK can run tool calls at the same time. The engine connection takes one request at a time,
  so one thread owns it, and each tool hands its work to that thread (`EngineThread`). There is no lock.

## Tests

| File | Needs | Proves |
|---|---|---|
| `test_sidecar.py` | The SDK, numpy, Pillow | Every tool's mapping and schema, `capture_diff` on known images, refusals, timeouts, stale replies, one request at a time to the engine, and the real stdio process, against a fake engine. The tests use the SDK's in-process client. `python -m unittest test_sidecar` |
| `check_engine.py ... checks` | The SDK, numpy, Pillow, a built OceanFft | The real engine through MCP. Two paused captures are identical, a region equals the crop of the full capture, the downsample equals a linear box average, pixels equal the PNG, a debug view off restores the image, a held time is identical by a jump and by steps, steps of 1/60 s land on the same float as a jump, a different time differs, and bad input is refused. |
| `check_engine.py ... demo DIR` | The SDK, numpy, Pillow, a built OceanFft | Captures three sea states and three debug views, for a person to look at. |

`mcp_client.py` is the small MCP client that `check_engine.py` uses. It writes the JSON-RPC lines itself and does not
use the SDK, so it is a second, independent reading of the protocol. It starts the sidecar with its own Python, so run
`check_engine.py` with the Python that has the SDK.
