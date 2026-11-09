"""MCP server over stdio for the DiamondDogs agent tools.

An agent speaks MCP to this process. This process speaks JSON-RPC to the engine's ToolServer over 127.0.0.1, one JSON
message per line. The official MCP Python SDK (the `mcp` package, version 2) handles the MCP side. README.md in this
folder explains the tools, the timeouts and the limits.

Only MCP messages go to stdout. Every log line goes to stderr.
"""

import argparse
import asyncio
import collections
import concurrent.futures
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import threading
import time
from typing import Annotated, Any

try:
    import anyio
    from mcp.server.mcpserver import Image, MCPServer
    from mcp.server.mcpserver.exceptions import ToolError
    from pydantic import Field
except ImportError as missing:
    sys.exit(f"diamond_dogs_mcp.py needs the MCP SDK, version 2 ({missing}). Install it for this Python: "
             f"\"{sys.executable}\" -m pip install --user \"mcp>=2,<3\"")

SERVER_NAME = "diamond-dogs"
SERVER_VERSION = "0.4.0"
PORT_LINE = re.compile(r"tool server listening on 127\.0\.0\.1:(\d+)")
DEFAULT_TIMEOUT_SECONDS = 10.0
LAUNCH_TIMEOUT_SECONDS = 180.0
# a frame.wait for N frames gets this much time per frame, on top of the default. 10 frames per second is slow for
# the demo, so a timeout means that the frames stopped
SECONDS_PER_FRAME_ALLOWANCE = 0.1
LOG_LINES_KEPT = 400
# MCP clients accept large images poorly. A larger capture still exists on disk, and its path is in the reply
MAX_INLINE_IMAGE_BYTES = 8 * 1024 * 1024
# the writer thread encodes a full-size PNG, which takes a second or more in a Debug build
CAPTURE_TIMEOUT_SECONDS = 60.0
# frames in flight between recording a frame and its timings: more than enough for the demo's two or three
TIMING_SETTLE_FRAMES = 10


def log(message):
    print(f"[{SERVER_NAME}] {message}", file=sys.stderr, flush=True)


class EngineConnection:
    """One JSON-RPC connection to the engine. Requests are sequential: the MCP loop handles one tool call at a time."""

    def __init__(self, port):
        self.port = port
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=DEFAULT_TIMEOUT_SECONDS)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buffer = b""
        self.next_id = 1

    def close(self):
        try:
            self.socket.close()
        except OSError:
            pass

    def request(self, method, params, timeout_seconds):
        """Returns the result, raises ToolError for an error reply, and raises TimeoutError when no reply comes."""
        request_id = self.next_id
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}
        self.socket.sendall((json.dumps(message) + "\n").encode("utf-8"))
        deadline = time.monotonic() + timeout_seconds

        while True:
            reply = self._read_message(deadline)
            # a reply to an earlier request that timed out arrives late. Its id is older, so it is dropped here
            if reply.get("id") != request_id:
                continue
            if "error" in reply:
                raise ToolError(format_engine_error(reply["error"]))
            return reply.get("result")

    def _read_message(self, deadline):
        while b"\n" not in self.buffer:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError()
            self.socket.settimeout(left)
            try:
                chunk = self.socket.recv(65536)
            except socket.timeout:
                raise TimeoutError() from None
            if not chunk:
                raise ConnectionError("the engine closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)


class Engine:
    """The engine process, when this sidecar launched it, and the connection to the engine."""

    def __init__(self, default_executable):
        self.default_executable = default_executable
        self.process = None
        self.connection = None
        self.log_lines = collections.deque(maxlen=LOG_LINES_KEPT)
        self.port_found = threading.Event()
        self.port = None

    def launch(self, executable, arguments, timeout_seconds):
        if self.process is not None and self.process.poll() is None:
            raise ToolError("an engine is already running. Call engine_stop first.")
        path = pathlib.Path(executable or self.default_executable or "")
        if not path.is_file():
            raise ToolError(f"no executable at '{path}'. Pass executable, or start the sidecar with --executable.")
        self.disconnect()
        self.log_lines.clear()
        self.port_found.clear()
        self.port = None
        # the demo looks for its assets and writes its captures relative to its own folder
        self.process = subprocess.Popen([str(path), "--tool-port=0", *arguments], cwd=path.parent,
                                        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, errors="replace", bufsize=1)
        # the engine blocks when its stdout pipe is full, so a thread always reads it
        threading.Thread(target=self._read_output, args=(self.process,), daemon=True).start()
        if not self.port_found.wait(timeout_seconds):
            exit_code = self.process.poll()
            state = f"it exited with code {exit_code}" if exit_code is not None else "it is still starting"
            raise ToolError(f"the engine printed no tool port within {timeout_seconds} s, and {state}. "
                            f"Last output:\n{self.tail(20)}")
        self.connect(self.port)
        return {"pid": self.process.pid, "port": self.port}

    def _read_output(self, process):
        for line in process.stdout:
            self.log_lines.append(line.rstrip("\n"))
            match = PORT_LINE.search(line)
            if match and not self.port_found.is_set():
                self.port = int(match.group(1))
                self.port_found.set()

    def connect(self, port):
        self.disconnect()
        try:
            self.connection = EngineConnection(port)
        except OSError as error:
            raise ToolError(f"cannot connect to 127.0.0.1:{port}: {error}. The server accepts one client at a time.")
        # a second client gets a refusal line and a closed socket, so the first request proves the connection
        try:
            self.connection.request("server.ping", {}, DEFAULT_TIMEOUT_SECONDS)
        except (ToolError, ConnectionError, OSError, TimeoutError) as error:
            self.disconnect()
            raise ToolError(f"the engine at 127.0.0.1:{port} did not answer a ping: {error or 'timeout'}. "
                            "Another client may be connected, and the server accepts one at a time.")
        return {"port": port}

    def disconnect(self):
        if self.connection is not None:
            self.connection.close()
            self.connection = None

    def stop(self):
        stopped = {"quit_sent": False, "exit_code": None}
        if self.connection is not None:
            try:
                self.connection.request("tool.call", {"name": "app.quit"}, DEFAULT_TIMEOUT_SECONDS)
                stopped["quit_sent"] = True
            except (ToolError, ConnectionError, OSError, TimeoutError):
                pass
        self.disconnect()
        if self.process is not None:
            try:
                stopped["exit_code"] = self.process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                self.process.kill()
                stopped["exit_code"] = self.process.wait()
                stopped["killed"] = True
            self.process = None
        return stopped

    def close(self):
        """Ends the session: stops an engine that this sidecar launched, and leaves any other engine running."""
        if self.process is not None:
            self.stop()
        else:
            self.disconnect()

    def tail(self, count):
        return "\n".join(list(self.log_lines)[-count:])

    def request(self, method, params, timeout_seconds):
        if self.connection is None:
            raise ToolError("no engine is connected. Call engine_launch or engine_connect first.")
        try:
            return self.connection.request(method, params, timeout_seconds)
        except TimeoutError:
            raise ToolError(self._explain_timeout(method, timeout_seconds)) from None
        except (ConnectionError, OSError) as error:
            self.disconnect()
            exit_code = self.process.poll() if self.process is not None else None
            detail = f" The engine exited with code {exit_code}." if exit_code is not None else ""
            raise ToolError(f"the connection to the engine failed: {error}.{detail}")

    def _explain_timeout(self, method, timeout_seconds):
        try:
            self.connection.request("server.ping", {}, 2.0)
        except (ToolError, ConnectionError, OSError, TimeoutError):
            return (f"{method} got no reply within {timeout_seconds:.1f} s, and the engine does not answer a ping "
                    "either. It may have stopped or hung.")
        return (f"{method} got no reply within {timeout_seconds:.1f} s, but the engine answers a ping, so its frames "
                "have stopped. The window may be minimized: tool calls run only while frames render. The request "
                "stays queued, and it runs when frames start again.")


def format_engine_error(error):
    message = error.get("message", "unknown error")
    data = error.get("data")
    if data is None:
        return f"{message} (code {error.get('code')})"
    return f"{message}\n{json.dumps(data, indent=2)}"


def inline_image(path):
    """Returns the PNG as an MCP image, or a note when the PNG is too large to send inline."""
    data = pathlib.Path(path).read_bytes()
    if len(data) > MAX_INLINE_IMAGE_BYTES:
        return (f"The PNG is {len(data)} bytes, above the {MAX_INLINE_IMAGE_BYTES} byte limit for an inline image. Use "
                "a downsample or a region, or open the file.")
    return Image(data=data, format="png")


def compare_pngs(path_a, path_b):
    """Compares two PNG captures pixel by pixel. Writes a heat map next to the first PNG when they differ, and returns
    the report. Needs numpy and Pillow, which only this tool uses."""
    try:
        import numpy
        from PIL import Image as PillowImage
    except ImportError as missing:
        raise ToolError(f"capture_diff needs numpy and Pillow ({missing}). Install them for {sys.executable}.") from None

    images = []

    for path in (path_a, path_b):
        file = pathlib.Path(path)

        if file.suffix.lower() != ".png" or not file.is_file():
            raise ToolError(f"'{path}' is not a PNG file. Pass the Path from a capture reply.")

        images.append(numpy.asarray(PillowImage.open(file).convert("RGB")).astype(numpy.int16))

    first, second = images

    if first.shape != second.shape:
        raise ToolError(f"the captures have different sizes: {first.shape[1]}x{first.shape[0]} and "
                        f"{second.shape[1]}x{second.shape[0]}")

    difference = numpy.abs(first - second)
    largest = difference.max(axis=2)
    changed = largest > 0
    report = {
        "width": int(first.shape[1]),
        "height": int(first.shape[0]),
        "identical": not bool(changed.any()),
        "max_difference_rgb": [int(value) for value in difference.max(axis=(0, 1))],
        "mean_difference": float(difference.mean()),
        "changed_pixels": int(changed.sum()),
        "changed_fraction": float(changed.mean()),
    }

    if not changed.any():
        return report, None

    rows, columns = numpy.nonzero(changed)
    report["changed_box"] = {
        "x": int(columns.min()), "y": int(rows.min()),
        "width": int(columns.max() - columns.min() + 1), "height": int(rows.max() - rows.min() + 1),
    }

    # black where the captures agree. The largest difference is white, so a difference of one code value shows too
    scale = 255.0 / float(largest.max())
    heat_map = pathlib.Path(path_a).with_name(f"diff_{pathlib.Path(path_a).stem}_{pathlib.Path(path_b).stem}.png")
    PillowImage.fromarray((largest * scale).astype(numpy.uint8), "L").save(heat_map)
    report["heat_map"] = str(heat_map)
    report["heat_map_code_values_per_step"] = 1.0 / scale
    return report, heat_map


def read_target(reply, pixel):
    """Reads the .bin of a capture.target reply. Writes the reply as a JSON header next to it, and a picture PNG for an
    image, and returns statistics per channel, the values of one texel when asked, and the picture's path. Needs numpy
    and Pillow."""
    try:
        import numpy
        from PIL import Image as PillowImage
    except ImportError as missing:
        raise ToolError(f"capture_target needs numpy and Pillow ({missing}). Install them for {sys.executable}.") from None

    path = pathlib.Path(reply["Path"])
    width, height, channels = reply["Width"], reply["Height"], reply["Channels"]
    values = numpy.fromfile(path, dtype=reply["Dtype"])

    if values.size != width * height * channels:
        raise ToolError(f"{path.name} holds {values.size} values, and the reply describes {width}x{height}x{channels}")

    values = values.reshape(height, width, channels).astype(numpy.float64)
    names = reply["ChannelNames"].split(",")
    header = path.with_suffix(".json")
    header.write_text(json.dumps(reply, indent=2))

    statistics = []

    for index, channel_name in enumerate(names):
        channel = values[..., index]
        finite = channel[numpy.isfinite(channel)]
        statistics.append({
            "channel": channel_name,
            "min": float(finite.min()) if finite.size else None,
            "max": float(finite.max()) if finite.size else None,
            "mean": float(finite.mean()) if finite.size else None,
            "nan": int(numpy.isnan(channel).sum()),
            "inf": int(numpy.isinf(channel).sum()),
        })

    report = {key: reply[key] for key in ("Target", "Path", "Dtype", "Width", "Height", "Channels", "Frame", "Doc")}
    report["header"] = str(header)
    report["numpy"] = (f"numpy.fromfile(path, dtype='{reply['Dtype']}').reshape({height}, {width}, {channels})")
    report["statistics"] = statistics

    if pixel is not None:
        x, y = pixel

        if not (0 <= x < width and 0 <= y < height):
            raise ToolError(f"the texel ({x}, {y}) is not inside the {width}x{height} target")

        report["texel"] = {"x": x, "y": y, "values": {name: float(values[y, x, i]) for i, name in enumerate(names)}}

    if width * height == 1:
        return report, None

    # the engine declares the mapping for each target: clamp(value x scale + bias, 0, 1), with no transfer curve
    scale = numpy.array(reply["ShowScale"][:channels])
    bias = numpy.array(reply["ShowBias"][:channels])
    shown = numpy.clip(numpy.nan_to_num(values) * scale + bias, 0.0, 1.0)
    pixels = numpy.round(shown * 255.0).astype(numpy.uint8)
    picture = path.with_suffix(".png")

    if channels == 1:
        PillowImage.fromarray(pixels[..., 0], "L").save(picture)
    else:
        rgb = numpy.zeros((height, width, 3), dtype=numpy.uint8)
        rgb[..., :min(channels, 3)] = pixels[..., :3]
        PillowImage.fromarray(rgb, "RGB").save(picture)

    report["picture"] = str(picture)
    report["picture_mapping"] = {"scale": reply["ShowScale"], "bias": reply["ShowBias"]}
    return report, picture


TIMING_FIELDS = ("Frames", "FirstFrame", "LastFrame", "AsyncSubmissions", "FrameIntervalUs", "Optimized", "Validation",
                 "Paused", "Repeatable", "ScreenWidth", "ScreenHeight", "DroppedScopes", "DroppedBlocks", "LostBlocks")


def timing_row(label, queue, time):
    return (label, queue, str(time["Frames"]), f"{time['MedianUs']:.2f}", f"{time['MeanUs']:.2f}",
            f"{time['MinUs']:.2f}", f"{time['MaxUs']:.2f}")


def summarize_timings(reply, include_samples):
    """Turns a frame.timings reply into a report: the context of the measurement, warnings when the times are not fit to
    compare, and a table with one line per scope, indented by its depth. The samples stay out unless asked for."""
    warnings = []

    if not reply["Optimized"]:
        warnings.append("Debug build: compare times only between optimized builds (RelWithDebInfo).")

    if reply["Validation"]:
        warnings.append("The validation layer is on, and it can change the times. Launch with --validation=off to "
                        "compare times.")

    if reply["DroppedScopes"] or reply["DroppedBlocks"] or reply["LostBlocks"]:
        warnings.append(f"Some timings are missing: {reply['DroppedScopes']} dropped scopes, {reply['DroppedBlocks']} "
                        f"dropped blocks, {reply['LostBlocks']} lost blocks.")

    rows = [("scope", "queue", "frames", "median us", "mean us", "min us", "max us")]

    for scope in reply["Scopes"]:
        rows.append(timing_row("  " * scope["Depth"] + scope["Name"], scope["Queue"], scope["Time"]))

    rows.append(timing_row("busy (sum of the top scopes)", "graphics", reply["Busy"]))
    widths = [max(len(row[column]) for row in rows) for column in range(len(rows[0]))]
    # the names align left and the numbers align right
    table = ["  ".join(cell.ljust(width) if column < 2 else cell.rjust(width)
                       for column, (cell, width) in enumerate(zip(row, widths))) for row in rows]

    report = {key: reply[key] for key in TIMING_FIELDS}
    report["warnings"] = warnings
    report["table"] = table

    if include_samples:
        report["reply"] = reply

    return report


class EngineThread:
    """One thread owns the engine and its connection. The SDK can run tool calls at the same time, and the connection
    takes one request at a time, so a tool hands its work to this thread and awaits the result. There is no lock: the
    executor's queue is the handoff, and only its one thread touches the engine."""

    def __init__(self, engine):
        self.engine = engine
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=1, thread_name_prefix="engine")

    async def run(self, work, *arguments):
        return await asyncio.wrap_future(self.executor.submit(work, *arguments))

    def run_blocking(self, work, *arguments):
        return self.executor.submit(work, *arguments).result()

    def shutdown(self):
        self.run_blocking(self.engine.close)
        self.executor.shutdown()


INSTRUCTIONS = ("Call engine_launch, then describe. Change values with set or call, wait_frames for the change to show, "
                "then capture_screen to see it. For an A/B comparison, call hold_time (or step_time) before each "
                "capture, so that both captures show the same simulation time, then capture_diff. For exact values "
                "(normals, depth, HDR color, the exposure), use capture_target. For GPU cost, use gpu_timings.")

HideUi = Annotated[bool, Field(description="Hides the panel on the captured frame only.")]
CaptureName = Annotated[str | None, Field(description="The file name without .png: letters, digits, '_' and '-'. "
                                                      "Empty: a new name.")]
IncludeImage = Annotated[bool, Field(description="Returns the PNG as an image.")]
PixelCoordinate = Annotated[int, Field(ge=0, description="Pixels from the left or top edge of the final image.")]
PixelCount = Annotated[int, Field(ge=1, description="A size in pixels.")]
SettleFrames = Annotated[int, Field(ge=0, le=1000, description="Frames to render at the time before the reply.")]


def frames_timeout(frames):
    return DEFAULT_TIMEOUT_SECONDS + frames * SECONDS_PER_FRAME_ALLOWANCE


def build_server(owner):
    """Returns the MCP server with its 17 tools. Every tool runs its engine work on the owner's thread."""
    server = MCPServer(SERVER_NAME, version=SERVER_VERSION, instructions=INSTRUCTIONS)
    engine = owner.engine

    def tool(description):
        # the engine replies are JSON text for the agent to read, so no tool declares structured output
        return server.tool(description=description, structured_output=False)

    def request(method, params, timeout_seconds):
        return owner.run(engine.request, method, params, timeout_seconds)

    def capture(command, capture_arguments, name, include_image):
        if name:
            capture_arguments["Name"] = name

        result = engine.request("tool.call", {"name": command, "arguments": capture_arguments}, CAPTURE_TIMEOUT_SECONDS)
        content = [result]

        if include_image:
            content.append(inline_image(result["Path"]))

        return content

    def hold(time, settle_frames):
        engine.request("tool.call", {"name": "frame.pause", "arguments": {"Paused": True}}, DEFAULT_TIMEOUT_SECONDS)
        engine.request("tool.call", {"name": "frame.repeatable", "arguments": {"Enabled": True}},
                       DEFAULT_TIMEOUT_SECONDS)
        params = {"name": "frame.time", "arguments": {"Time": time, "SettleFrames": settle_frames}}
        return engine.request("tool.call", params, frames_timeout(settle_frames))

    def step(frames, seconds, settle_frames):
        engine.request("tool.call", {"name": "frame.repeatable", "arguments": {"Enabled": True}},
                       DEFAULT_TIMEOUT_SECONDS)
        params = {"name": "frame.step", "arguments": {"Frames": frames, "Seconds": seconds, "SettleFrames": settle_frames}}
        return engine.request("tool.call", params, frames_timeout(frames + settle_frames))

    def diff(path_a, path_b, include_image):
        report, heat_map = compare_pngs(path_a, path_b)
        content = [report]

        if include_image and heat_map is not None:
            content.append(inline_image(heat_map))

        return content

    def status(log_lines):
        report = {"connected": engine.connection is not None}

        if engine.process is not None:
            report["pid"] = engine.process.pid
            report["exit_code"] = engine.process.poll()

        if engine.connection is not None:
            report["port"] = engine.connection.port
            report["frame"] = engine.request("tool.call", {"name": "frame.info"}, DEFAULT_TIMEOUT_SECONDS)

        report["log"] = engine.tail(log_lines)
        return report

    @tool("Starts the engine with --tool-port=0, and connects to it. Useful arguments: --window=WxH, --borderless, "
          "--size=128|256|512, --dt=SECONDS for a fixed simulation step, --script=FILE.")
    async def engine_launch(
            executable: Annotated[str | None, Field(description="The engine executable. Empty: the sidecar's "
                                                                "--executable.")] = None,
            arguments: Annotated[list[str] | None, Field(description="More command-line arguments.")] = None,
            timeout_seconds: Annotated[float, Field(gt=0, description="Seconds to wait for startup.")]
            = LAUNCH_TIMEOUT_SECONDS):
        return await owner.run(engine.launch, executable, arguments or [], timeout_seconds)

    @tool("Connects to an engine that already runs with --tool-port=PORT.")
    async def engine_connect(port: Annotated[int, Field(ge=1, le=65535)]):
        return await owner.run(engine.connect, port)

    @tool("Returns whether an engine is connected, its frame and screen size, and its last output lines.")
    async def engine_status(log_lines: Annotated[int, Field(ge=0, description="Output lines to return.")] = 20):
        return await owner.run(status, log_lines)

    @tool("Asks the engine to quit, so that it writes its captures, and waits for the process to exit.")
    async def engine_stop():
        return await owner.run(engine.stop)

    @tool("Returns the JSON Schema of every object and command, or of one path. Start here: names, units, limits and "
          "docs all come from this.")
    async def describe(path: Annotated[str, Field(description="An object, field or command. Empty: everything.")] = ""):
        params = {"path": path} if path else {}
        return await request("tool.describe", params, DEFAULT_TIMEOUT_SECONDS)

    @tool("Returns the value at a path, for example ocean.sea or ocean.sea.WindForce.")
    async def get(path: str):
        return await request("tool.get", {"path": path}, DEFAULT_TIMEOUT_SECONDS)

    @tool("Writes one value at a path, and returns the value that the field now contains. A value outside the field's "
          "limits is refused, with the limits in the error.")
    async def set(path: str, value: int | float | bool | str):
        return await request("tool.set", {"path": path, "value": value}, DEFAULT_TIMEOUT_SECONDS)

    @tool("Runs a command with its arguments, for example camera.lookat with {\"Eye\": [0, 30, 80], \"Target\": [0, 0, "
          "0]}. describe lists the commands.")
    async def call(
            name: str,
            arguments: Annotated[dict[str, Any] | None, Field(description="The command's arguments.")] = None,
            timeout_seconds: Annotated[float, Field(gt=0, description="Seconds to wait for the reply.")]
            = DEFAULT_TIMEOUT_SECONDS):
        params = {"name": name}

        if arguments:
            params["arguments"] = arguments

        return await request("tool.call", params, timeout_seconds)

    @tool("Returns after this many frames. Use it after a change, so that the simulation shows the change.")
    async def wait_frames(frames: Annotated[int, Field(ge=0)]):
        params = {"name": "frame.wait", "arguments": {"Frames": frames}}
        return await request("tool.call", params, frames_timeout(frames))

    @tool("Pauses the sea at this exact simulation time, in repeatable mode, so that a capture is the same byte for "
          "byte each time. Repeatable mode turns TAA off, so edges alias. Use it before each capture of an A/B "
          "comparison. frame.repeatable with Enabled false restores TAA.")
    async def hold_time(
            time: Annotated[float, Field(ge=0, le=100000, description="Seconds of simulation time.")],
            settle_frames: SettleFrames = 2):
        return await owner.run(hold, time, settle_frames)

    @tool("Advances the paused sea by exactly `seconds` on each of `frames` frames, in repeatable mode, then holds. "
          "N steps from a time that hold_time set reach the same time as hold_time with time + N x seconds. Use it to "
          "watch a wave fold step by step.")
    async def step_time(
            frames: Annotated[int, Field(ge=1, le=100000)],
            seconds: Annotated[float, Field(ge=0.0001, le=10, description="Simulation seconds per step.")],
            settle_frames: SettleFrames = 2):
        return await owner.run(step, frames, seconds, settle_frames)

    @tool("Compares two PNG captures pixel by pixel: whether they are identical, the largest and mean difference in "
          "8-bit code values, how many pixels changed, and the box around them. Returns a heat map when they differ: "
          "black where they agree, and white at the largest difference.")
    async def capture_diff(
            path_a: Annotated[str, Field(description="The Path from a capture reply.")],
            path_b: Annotated[str, Field(description="The Path from a second capture reply, the same size.")],
            include_image: Annotated[bool, Field(description="Returns the heat map as an image.")] = True):
        return await anyio.to_thread.run_sync(diff, path_a, path_b, include_image)

    @tool("Captures the whole final image, downsampled by averaging in linear light, and returns it.")
    async def capture_screen(
            downsample: Annotated[int, Field(ge=1, le=16, description="The factor for each axis.")] = 4,
            hide_ui: HideUi = True,
            name: CaptureName = None,
            include_image: IncludeImage = True):
        capture_arguments = {"Downsample": downsample, "HideUi": hide_ui}
        return await owner.run(capture, "capture.screen", capture_arguments, name, include_image)

    @tool("Captures one rectangle of the final image at full resolution, and returns it. engine_status returns the "
          "screen size.")
    async def capture_region(
            x: PixelCoordinate,
            y: PixelCoordinate,
            width: PixelCount,
            height: PixelCount,
            hide_ui: HideUi = True,
            name: CaptureName = None,
            include_image: IncludeImage = True):
        capture_arguments = {"X": x, "Y": y, "Width": width, "Height": height, "HideUi": hide_ui}
        return await owner.run(capture, "capture.region", capture_arguments, name, include_image)

    @tool("Copies one intermediate image or buffer of the frame unchanged, at the end of a frame: the HDR scene, the "
          "G-buffer, depth, Hi-Z, or the auto exposure. Returns its type, shape, and the min, max, mean and NaN count "
          "of each channel, and writes a .bin with a JSON header. With pixel_x and pixel_y, returns that texel's "
          "values. The picture shows each channel as clamp(value x scale + bias), with the scale and bias that the "
          "engine declares for the target. A wrong target name fails, and the error lists the targets.")
    async def capture_target(
            target: Annotated[str, Field(description="The target name, for example scene, normal_sigma, ocean_depth, "
                                                     "exposure_ev.")],
            name: CaptureName = None,
            pixel_x: Annotated[int | None, Field(ge=0, description="A texel to read, with pixel_y.")] = None,
            pixel_y: Annotated[int | None, Field(ge=0, description="A texel to read, with pixel_x.")] = None,
            include_image: Annotated[bool, Field(description="Returns the picture as an image.")] = True):
        if (pixel_x is None) != (pixel_y is None):
            raise ToolError("give both pixel_x and pixel_y, or neither")

        arguments = {"Target": target}

        if name:
            arguments["Name"] = name

        reply = await request("tool.call", {"name": "capture.target", "arguments": arguments}, CAPTURE_TIMEOUT_SECONDS)
        pixel = (pixel_x, pixel_y) if pixel_x is not None else None
        report, picture = await anyio.to_thread.run_sync(read_target, reply, pixel)
        content = [report]

        if include_image and picture is not None:
            content.append(inline_image(picture))

        return content

    @tool("Measures the GPU time of every timer scope over the next `frames` frames: the median, mean, min and max in "
          "microseconds, in a table indented by nesting. Only frames from this call on count, so a change made before "
          "it is in every measured frame. For an A/B comparison, change one thing between two calls. Compare times "
          "only from a RelWithDebInfo build launched with --validation=off: the warnings say when that is not so. "
          "Release builds have no timers, and the call fails there.")
    async def gpu_timings(
            frames: Annotated[int, Field(ge=1, le=600, description="Frames to measure.")] = 120,
            include_samples: Annotated[bool, Field(description="Adds the engine's whole reply, with one sample per "
                                                               "frame for each scope.")] = False):
        params = {"name": "frame.timings", "arguments": {"Frames": frames}}
        # the reply waits for the GPU to finish the last frame, a few frames after it is recorded
        reply = await request("tool.call", params, frames_timeout(frames + TIMING_SETTLE_FRAMES))
        return summarize_timings(reply, include_samples)

    @tool("Returns one pixel of the final image: linear BT.709 (1.0 is paper white), 8-bit sRGB, and the stored bytes.")
    async def capture_pixel(x: PixelCoordinate, y: PixelCoordinate, hide_ui: HideUi = True):
        params = {"name": "capture.pixel", "arguments": {"X": x, "Y": y, "HideUi": hide_ui}}
        return await request("tool.call", params, DEFAULT_TIMEOUT_SECONDS)

    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--executable", default=os.environ.get("DIAMOND_DOGS_ENGINE"),
                        help="The engine that engine_launch starts. Default: the DIAMOND_DOGS_ENGINE variable.")
    parser.add_argument("--port", type=int, help="Connects at startup to an engine on this port.")
    options = parser.parse_args()
    owner = EngineThread(Engine(options.executable))

    if options.port is not None:
        try:
            owner.run_blocking(owner.engine.connect, options.port)
        except ToolError as error:
            log(str(error))

    try:
        build_server(owner).run("stdio")
    finally:
        owner.shutdown()


if __name__ == "__main__":
    main()
