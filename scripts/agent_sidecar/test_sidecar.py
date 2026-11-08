"""Tests the sidecar against a fake engine. No GPU and no engine build: python -m unittest test_sidecar

The tests drive the sidecar through the MCP SDK's own client, in process, so they test the tools and not the protocol.
StdioTest runs the real process once, to prove the stdio wiring."""
import asyncio
import base64
import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

import numpy
from mcp.client import Client
from PIL import Image as PillowImage

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import diamond_dogs_mcp as sidecar  # noqa: E402

# a 1x1 PNG, so that the image path can be checked without Pillow
TINY_PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC")


class FakeEngine:
    """A ToolServer stand-in. handlers maps a tool.call name or a method to a function of the params that returns
    ("result", value), ("error", error_object), or ("silent", None) for no reply."""

    def __init__(self, handlers):
        self.handlers = handlers
        self.listener = socket.create_server(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.received = []
        self.stale_reply_first = False
        # the real server reads one request at a time per connection. This counts requests that overlap
        self.in_flight = 0
        self.most_in_flight = 0
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        try:
            connection, _ = self.listener.accept()
        except OSError:
            # the test closed the listener without a connection
            return

        buffer = b""

        with connection:
            while True:
                chunk = connection.recv(65536)

                if not chunk:
                    return

                buffer += chunk

                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    self.answer(connection, json.loads(line))

    def answer(self, connection, request):
        self.received.append(request)
        method = request["method"]
        key = request["params"].get("name", method) if method == "tool.call" else method

        if method == "server.ping":
            kind, value = "result", {"Pong": True}
        else:
            kind, value = self.handlers.get(key, lambda params: ("error", {"code": -32601, "message": "no"}))(
                request["params"])

        if kind == "silent":
            return

        if self.stale_reply_first:
            self.stale_reply_first = False
            stale = {"jsonrpc": "2.0", "id": request["id"] - 1000, "result": "stale"}
            connection.sendall((json.dumps(stale) + "\n").encode())

        reply = {"jsonrpc": "2.0", "id": request["id"], kind: value}
        connection.sendall((json.dumps(reply) + "\n").encode())

    def close(self):
        self.listener.close()


def slow_frame_info(fake):
    def answer(params):
        # holds the request long enough that a second request from another thread would arrive during it
        fake.in_flight += 1
        fake.most_in_flight = max(fake.most_in_flight, fake.in_flight)
        time.sleep(0.05)
        fake.in_flight -= 1
        return "result", {"Frame": 7, "ScreenWidth": 640, "ScreenHeight": 360}

    return answer


def text(result, index):
    return result.content[index].text


def json_text(result, index):
    return json.loads(text(result, index))


def in_a_client(test_class):
    """Runs each async test inside its own SDK client. The client's task group must open and close in one task, and
    IsolatedAsyncioTestCase runs its setup and teardown in other tasks."""

    def wrap(test):
        def run(self):
            async def body():
                async with Client(sidecar.build_server(self.owner)) as client:
                    self.client = client
                    await test(self)

            asyncio.run(body())

        return run

    for name in list(vars(test_class)):
        if name.startswith("test_"):
            setattr(test_class, name, wrap(getattr(test_class, name)))

    return test_class


@in_a_client
class SidecarTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.png = pathlib.Path(self.folder.name) / "shot.png"
        self.png.write_bytes(TINY_PNG)
        png_path = str(self.png)
        # a 3x2 target with two channels, and one NaN, as capture.target writes it: rows top first, channels interleaved
        self.target_values = numpy.array([[[0.5, -1.0], [1.0, 0.0], [-0.5, 2.0]],
                                          [[0.25, numpy.nan], [0.0, 1.0], [2.0, -2.0]]], numpy.float16)
        self.target_bin = pathlib.Path(self.folder.name) / "target.bin"
        self.target_values.tofile(self.target_bin)
        self.buffer_bin = pathlib.Path(self.folder.name) / "buffer.bin"
        numpy.array([1.5], numpy.float32).tofile(self.buffer_bin)
        self.fake = FakeEngine({
            "tool.get": lambda params: ("result", 5.0),
            "tool.set": lambda params: ("result", params["value"]),
            "tool.describe": lambda params: ("result", {"objects": [], "path": params.get("path")}),
            "frame.wait": lambda params: ("result", {"Frame": 7 + params["arguments"]["Frames"]}),
            "capture.screen": lambda params: ("result", {"Path": png_path, "Width": 1, "Height": 1}),
            "capture.pixel": lambda params: ("result", {"Srgb8": [1, 2, 3]}),
            "app.quit": lambda params: ("result", {"Frame": 7}),
            "frame.pause": lambda params: ("result", {"Paused": params["arguments"]["Paused"]}),
            "frame.repeatable": lambda params: ("result", {"Repeatable": params["arguments"]["Enabled"]}),
            "frame.time": lambda params: ("result", {"SimulationTime": params["arguments"]["Time"]}),
            "frame.step": lambda params: ("result", {"Frames": params["arguments"]["Frames"]}),
            "capture.target": self.target_reply,
            "never.answers": lambda params: ("silent", None),
            "refuses": lambda params: ("error", {"code": -32000, "message": "the value is outside the limits",
                                                 "data": {"Code": "ValueRefused", "Suggestions": ["WindForce"]}}),
        })
        self.fake.handlers["frame.info"] = slow_frame_info(self.fake)
        self.owner = sidecar.EngineThread(sidecar.Engine(None))

    def target_reply(self, params):
        """An image target, a one-element buffer, and a target whose file is too short for its shape."""
        name = params["arguments"]["Target"]
        image = {"Path": str(self.target_bin), "Target": name, "Dtype": "float16", "Width": 3, "Height": 2,
                 "Channels": 2, "ChannelNames": "a,b", "Bytes": 24, "ShowScale": [0.5, 1.0, 0.0, 0.0],
                 "ShowBias": [0.5, 0.0, 0.0, 0.0], "Doc": "a test image", "Frame": 9}

        if name == "buffer":
            return "result", {**image, "Path": str(self.buffer_bin), "Dtype": "float32", "Width": 1, "Height": 1,
                              "Channels": 1, "ChannelNames": "ev"}

        if name == "short":
            return "result", {**image, "Height": 3}

        return "result", image

    def tearDown(self):
        self.owner.run_blocking(self.owner.engine.disconnect)
        self.owner.executor.shutdown()
        self.fake.close()
        self.folder.cleanup()

    async def tool(self, name, arguments):
        return await self.client.call_tool(name, arguments)

    async def connect(self):
        result = await self.tool("engine_connect", {"port": self.fake.port})
        self.assertFalse(result.is_error, result)

    async def test_tools_list_has_sixteen_tools_with_object_schemas(self):
        tools = (await self.client.list_tools()).tools
        self.assertEqual(sorted(tool.name for tool in tools), sorted([
            "engine_launch", "engine_connect", "engine_status", "engine_stop", "describe", "get", "set", "call",
            "wait_frames", "capture_screen", "capture_region", "capture_pixel", "hold_time", "step_time",
            "capture_diff", "capture_target"]))

        for tool in tools:
            self.assertEqual(tool.input_schema["type"], "object", tool.name)
            self.assertTrue(tool.description, tool.name)

        region = next(tool for tool in tools if tool.name == "capture_region")
        self.assertEqual(sorted(region.input_schema["required"]), ["height", "width", "x", "y"])

    async def test_a_tool_before_a_connection_explains_what_to_do(self):
        result = await self.tool("get", {"path": "ocean.sea"})
        self.assertTrue(result.is_error)
        self.assertIn("engine_launch", text(result, 0))

    async def test_get_and_set_pass_through(self):
        await self.connect()
        self.assertEqual(json_text(await self.tool("get", {"path": "a.b"}), 0), 5.0)
        self.assertEqual(json_text(await self.tool("set", {"path": "a.b", "value": 9}), 0), 9)
        self.assertEqual(self.fake.received[-1]["params"], {"path": "a.b", "value": 9})

    async def test_set_keeps_the_type_of_its_value(self):
        await self.connect()

        for value in (9, 9.5, True, "Moderate"):
            await self.tool("set", {"path": "a.b", "value": value})
            sent = self.fake.received[-1]["params"]["value"]
            self.assertEqual((type(sent), sent), (type(value), value))

    async def test_an_engine_refusal_is_a_tool_error_with_its_data(self):
        await self.connect()
        result = await self.tool("call", {"name": "refuses", "arguments": {"X": 1}})
        self.assertTrue(result.is_error)
        self.assertIn("ValueRefused", text(result, 0))
        self.assertIn("WindForce", text(result, 0))

    async def test_call_sends_its_arguments_as_an_object(self):
        await self.connect()
        await self.tool("call", {"name": "frame.info", "arguments": {"A": [1, 2]}})
        self.assertEqual(self.fake.received[-1]["params"], {"name": "frame.info", "arguments": {"A": [1, 2]}})

    async def test_wait_frames_uses_frame_wait(self):
        await self.connect()
        self.assertEqual(json_text(await self.tool("wait_frames", {"frames": 3}), 0)["Frame"], 10)

    async def test_a_stale_reply_is_dropped(self):
        await self.connect()
        self.fake.stale_reply_first = True
        self.assertEqual(json_text(await self.tool("get", {"path": "a"}), 0), 5.0)

    async def test_a_timeout_with_a_live_engine_points_at_stopped_frames(self):
        await self.connect()
        start = time.monotonic()
        result = await self.tool("call", {"name": "never.answers", "timeout_seconds": 0.3})
        self.assertLess(time.monotonic() - start, 5.0)
        self.assertTrue(result.is_error)
        self.assertIn("minimized", text(result, 0))

    async def test_capture_screen_returns_the_png_as_an_image(self):
        await self.connect()
        result = await self.tool("capture_screen", {"downsample": 2})
        self.assertFalse(result.is_error, result)
        self.assertEqual(result.content[1].type, "image")
        self.assertEqual(result.content[1].mime_type, "image/png")
        self.assertEqual(base64.b64decode(result.content[1].data), TINY_PNG)
        self.assertEqual(self.fake.received[-1]["params"]["arguments"], {"Downsample": 2, "HideUi": True})

    async def test_capture_screen_can_leave_the_image_out(self):
        await self.connect()
        result = await self.tool("capture_screen", {"include_image": False, "name": "x"})
        self.assertEqual(len(result.content), 1)
        self.assertEqual(self.fake.received[-1]["params"]["arguments"]["Name"], "x")

    async def test_a_large_png_becomes_a_note(self):
        await self.connect()
        self.png.write_bytes(b"\0" * (sidecar.MAX_INLINE_IMAGE_BYTES + 1))
        result = await self.tool("capture_screen", {})
        self.assertFalse(result.is_error, result)
        self.assertEqual(result.content[1].type, "text")
        self.assertIn("limit", text(result, 1))

    async def test_capture_pixel_maps_its_arguments(self):
        await self.connect()
        await self.tool("capture_pixel", {"x": 3, "y": 4})
        self.assertEqual(self.fake.received[-1]["params"]["arguments"], {"X": 3, "Y": 4, "HideUi": True})

    async def test_a_missing_argument_is_a_tool_error(self):
        await self.connect()
        result = await self.tool("capture_region", {"x": 1})
        self.assertTrue(result.is_error)
        self.assertEqual(self.fake.received[-1]["method"], "server.ping")

    async def test_a_value_outside_the_schema_is_a_tool_error(self):
        await self.connect()
        result = await self.tool("capture_screen", {"downsample": 17})
        self.assertTrue(result.is_error)
        self.assertEqual(self.fake.received[-1]["method"], "server.ping")

    async def test_an_unknown_tool_is_a_tool_error(self):
        result = await self.tool("format_disk", {})
        self.assertTrue(result.is_error)

    async def test_status_reports_the_frame(self):
        await self.connect()
        status = json_text(await self.tool("engine_status", {}), 0)
        self.assertTrue(status["connected"])
        self.assertEqual(status["frame"]["Frame"], 7)

    async def test_tool_calls_at_the_same_time_reach_the_engine_one_at_a_time(self):
        await self.connect()
        calls = [self.tool("call", {"name": "frame.info"}) for _ in range(6)]
        results = await asyncio.gather(*calls)

        for result in results:
            self.assertFalse(result.is_error, result)

        self.assertEqual(self.fake.most_in_flight, 1)

    async def test_hold_time_pauses_turns_on_repeatable_frames_and_sets_the_time(self):
        await self.connect()
        result = await self.tool("hold_time", {"time": 12.5})
        self.assertFalse(result.is_error, result)
        sent = [(request["params"].get("name"), request["params"].get("arguments")) for request in self.fake.received[-3:]]
        self.assertEqual(sent, [("frame.pause", {"Paused": True}), ("frame.repeatable", {"Enabled": True}),
                                ("frame.time", {"Time": 12.5, "SettleFrames": 2})])

    async def test_step_time_sends_its_step(self):
        await self.connect()
        await self.tool("step_time", {"frames": 16, "seconds": 0.03125, "settle_frames": 0})
        self.assertEqual(self.fake.received[-1]["params"]["arguments"], {"Frames": 16, "Seconds": 0.03125,
                                                                         "SettleFrames": 0})

    async def test_capture_diff_of_one_file_is_identical(self):
        result = await self.tool("capture_diff", {"path_a": str(self.png), "path_b": str(self.png)})
        self.assertFalse(result.is_error, result)
        report = json_text(result, 0)
        self.assertTrue(report["identical"])
        self.assertEqual(report["changed_pixels"], 0)
        self.assertEqual(len(result.content), 1)

    async def test_capture_diff_measures_a_change_and_returns_a_heat_map(self):
        first = numpy.zeros((4, 6, 3), numpy.uint8)
        second = first.copy()
        second[1, 2] = (3, 0, 0)
        second[2, 4] = (0, 6, 1)
        first_path = pathlib.Path(self.folder.name) / "first.png"
        second_path = pathlib.Path(self.folder.name) / "second.png"
        PillowImage.fromarray(first).save(first_path)
        PillowImage.fromarray(second).save(second_path)
        result = await self.tool("capture_diff", {"path_a": str(first_path), "path_b": str(second_path)})
        report = json_text(result, 0)
        self.assertFalse(report["identical"])
        self.assertEqual(report["max_difference_rgb"], [3, 6, 1])
        self.assertEqual(report["changed_pixels"], 2)
        self.assertEqual(report["changed_box"], {"x": 2, "y": 1, "width": 3, "height": 2})
        self.assertEqual(result.content[1].type, "image")
        heat = numpy.asarray(PillowImage.open(report["heat_map"]))
        self.assertEqual((int(heat[2, 4]), int(heat[1, 2]), int(heat[0, 0])), (255, 127, 0))

    async def test_capture_diff_refuses_different_sizes_and_other_files(self):
        small = pathlib.Path(self.folder.name) / "small.png"
        PillowImage.fromarray(numpy.zeros((2, 2, 3), numpy.uint8)).save(small)
        sizes = await self.tool("capture_diff", {"path_a": str(self.png), "path_b": str(small)})
        self.assertTrue(sizes.is_error)
        self.assertIn("different sizes", text(sizes, 0))
        other = await self.tool("capture_diff", {"path_a": str(self.png), "path_b": __file__})
        self.assertTrue(other.is_error)
        self.assertIn("not a PNG", text(other, 0))

    async def test_capture_target_reports_each_channel_a_texel_and_a_picture(self):
        await self.connect()
        result = await self.tool("capture_target", {"target": "image", "name": "probe", "pixel_x": 2, "pixel_y": 1})
        self.assertFalse(result.is_error, result)
        self.assertEqual(self.fake.received[-1]["params"]["arguments"], {"Target": "image", "Name": "probe"})
        report = json_text(result, 0)
        first, second = report["statistics"]
        self.assertEqual((first["channel"], first["min"], first["max"], first["nan"]), ("a", -0.5, 2.0, 0))
        self.assertAlmostEqual(first["mean"], 3.25 / 6.0)
        # the NaN is counted, and left out of the other statistics
        self.assertEqual((second["min"], second["max"], second["nan"]), (-2.0, 2.0, 1))
        self.assertEqual(report["texel"]["values"], {"a": 2.0, "b": -2.0})
        self.assertEqual(json.loads(pathlib.Path(report["header"]).read_text())["Channels"], 2)
        # red is a x 0.5 + 0.5 and green is b, both clamped to [0, 1], and blue is empty
        picture = numpy.asarray(PillowImage.open(report["picture"]))
        self.assertEqual(picture.shape, (2, 3, 3))
        self.assertEqual(picture[0, 0].tolist(), [191, 0, 0])
        self.assertEqual(picture[0, 2].tolist(), [64, 255, 0])
        self.assertEqual(picture[1, 0].tolist(), [159, 0, 0])
        self.assertEqual(result.content[1].type, "image")

    async def test_capture_target_of_a_buffer_has_no_picture(self):
        await self.connect()
        result = await self.tool("capture_target", {"target": "buffer"})
        self.assertFalse(result.is_error, result)
        report = json_text(result, 0)
        self.assertEqual(report["statistics"][0]["max"], 1.5)
        self.assertNotIn("picture", report)
        self.assertEqual(len(result.content), 1)

    async def test_capture_target_refuses_a_short_file_and_half_a_texel(self):
        await self.connect()
        short = await self.tool("capture_target", {"target": "short"})
        self.assertTrue(short.is_error)
        self.assertIn("holds 12 values", text(short, 0))
        half = await self.tool("capture_target", {"target": "image", "pixel_x": 1})
        self.assertTrue(half.is_error)
        self.assertIn("both pixel_x and pixel_y", text(half, 0))
        outside = await self.tool("capture_target", {"target": "image", "pixel_x": 3, "pixel_y": 0})
        self.assertTrue(outside.is_error)
        self.assertIn("not inside", text(outside, 0))

    async def test_close_leaves_an_engine_it_did_not_launch_running(self):
        await self.connect()
        self.owner.run_blocking(self.owner.engine.close)
        names = [request["params"].get("name") for request in self.fake.received]
        self.assertNotIn("app.quit", names)

    async def test_engine_stop_asks_the_engine_to_quit(self):
        await self.connect()
        await self.tool("engine_stop", {})
        names = [request["params"].get("name") for request in self.fake.received]
        self.assertIn("app.quit", names)

    async def test_launch_refuses_a_missing_executable(self):
        result = await self.tool("engine_launch", {"executable": str(pathlib.Path(self.folder.name) / "nope.exe")})
        self.assertTrue(result.is_error)
        self.assertIn("no executable", text(result, 0))


class StdioTest(unittest.TestCase):
    def test_the_process_speaks_mcp_over_stdio(self):
        script = pathlib.Path(__file__).parent / "diamond_dogs_mcp.py"
        process = subprocess.Popen([sys.executable, str(script)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        messages = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                        "clientInfo": {"name": "test_sidecar", "version": "1"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        ]
        output, errors = process.communicate(b"".join((json.dumps(m) + "\n").encode() for m in messages), timeout=30)
        lines = output.decode().splitlines()
        self.assertEqual(len(lines), 2, errors.decode(errors="replace"))
        self.assertEqual(json.loads(lines[0])["result"]["protocolVersion"], "2025-06-18")
        self.assertEqual(len(json.loads(lines[1])["result"]["tools"]), 16)


if __name__ == "__main__":
    unittest.main()
