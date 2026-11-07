"""End-to-end checks of the sidecar and the engine's capture tools, through MCP as an agent sees them.

    python check_engine.py <OceanFft.exe> checks [--window=WxH | --borderless]
    python check_engine.py <OceanFft.exe> demo <output folder>

checks compares independent results: two captures of a paused sea with TAA off, a region against a crop of the full
capture, the downsample against a linear box average, and pixels against the PNG. It needs numpy and Pillow.
demo captures the default sea, a shallow gale and a glassy swell from one fixed camera, and stacks them.
"""
import io
import json
import pathlib
import shutil
import sys
import tempfile

import numpy
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from mcp_client import McpClient  # noqa: E402

failures = []


def check(condition, label):
    print(("ok   " if condition else "FAIL ") + label, flush=True)
    if not condition:
        failures.append(label)


def decode_png(data):
    return numpy.asarray(Image.open(io.BytesIO(data)).convert("RGB")).astype(numpy.int32)


def to_linear(srgb8):
    value = srgb8 / 255.0
    return numpy.where(value <= 0.04045, value / 12.92, ((value + 0.055) / 1.055) ** 2.4)


def to_srgb8(linear):
    value = numpy.clip(linear, 0.0, 1.0)
    encoded = numpy.where(value <= 0.0031308, 12.92 * value, 1.055 * value ** (1.0 / 2.4) - 0.055)
    return numpy.floor(encoded * 255.0 + 0.5).astype(numpy.int32)


def capture(client, tool, arguments):
    is_error, text, image = client.tool(tool, arguments)
    if is_error:
        raise RuntimeError(f"{tool} failed: {text}")
    reply = json.loads(text)
    on_disk = pathlib.Path(reply["Path"]).read_bytes()
    check(image == on_disk, f"{tool} returns the same bytes as {pathlib.Path(reply['Path']).name}")
    return reply, decode_png(image)


def validation_lines(client):
    status = client.json_tool("engine_status", {"log_lines": 400})
    return [line for line in status["log"].splitlines() if "Validation Error" in line or "VUID" in line]


def run_checks(client, window_arguments):
    with tempfile.TemporaryDirectory() as folder:
        script = pathlib.Path(folder) / "taa_off.txt"
        # TAA jitters the projection by up to a pixel each frame, so a paused sea differs between frames with it on
        script.write_text("taa off\n")
        client.json_tool("engine_launch", {"arguments": [*window_arguments, "--size=128", f"--script={script}"]})
        client.json_tool("wait_frames", {"frames": 60})
        paused = client.json_tool("call", {"name": "frame.pause", "arguments": {"Paused": True}})
        width, height = paused["ScreenWidth"], paused["ScreenHeight"]
        print(f"screen {width}x{height}", flush=True)
        client.json_tool("wait_frames", {"frames": 5})

        _, full = capture(client, "capture_screen", {"downsample": 1, "name": "check_full_a"})
        _, again = capture(client, "capture_screen", {"downsample": 1, "name": "check_full_b"})
        check(full.shape == (height, width, 3), f"the full capture is {full.shape}")
        check(numpy.array_equal(full, again), "a paused sea with TAA off gives two identical captures")

        region_x, region_y, region_w, region_h = width // 3, height // 4, 640, 360
        _, crop = capture(client, "capture_region", {"x": region_x, "y": region_y, "width": region_w,
                                                     "height": region_h, "name": "check_region"})
        check(numpy.array_equal(crop, full[region_y:region_y + region_h, region_x:region_x + region_w]),
              "the region equals the same crop of the full capture")

        _, reduced = capture(client, "capture_screen", {"downsample": 4, "name": "check_small"})
        check(reduced.shape == ((height + 3) // 4, (width + 3) // 4, 3), f"the downsampled capture is {reduced.shape}")
        whole_h, whole_w = (height // 4) * 4, (width // 4) * 4
        blocks = to_linear(full[:whole_h, :whole_w]).reshape(whole_h // 4, 4, whole_w // 4, 4, 3).mean(axis=(1, 3))
        difference = numpy.abs(to_srgb8(blocks) - reduced[: whole_h // 4, : whole_w // 4])
        print(f"downsample against a box average of the 8-bit capture: max {difference.max()}, "
              f"mean {difference.mean():.3f}", flush=True)
        check(difference.max() <= 2 and difference.mean() < 0.2, "the downsample is a linear box average")

        for x, y in ((width // 2, height // 2), (0, 0), (width - 1, height - 1)):
            pixel = client.json_tool("capture_pixel", {"x": x, "y": y})
            check(pixel["Srgb8"] == [int(value) for value in full[y, x]],
                  f"pixel ({x},{y}) {pixel['Srgb8']} matches the PNG; linear {pixel['LinearRgb']} {pixel['Encoding']}")

        _, with_ui = capture(client, "capture_screen", {"downsample": 1, "hide_ui": False, "name": "check_with_ui"})
        check(not numpy.array_equal(with_ui, full), "hide_ui false shows the panel")

        views = client.json_tool("call", {"name": "debug.view"})
        print(f"debug views: {views['Choices']}", flush=True)
        surface_names = views["Choices"].split("Surface: ")[1].split(". Atmosphere")[0].split(", ")
        shown = client.json_tool("call", {"name": "debug.view", "arguments": {"Surface": surface_names[1]}})
        check(shown["Surface"] == surface_names[1], f"debug.view selects {surface_names[1]}")
        client.json_tool("wait_frames", {"frames": 2})
        _, debug_image = capture(client, "capture_screen", {"downsample": 1, "name": "check_debug_view"})
        check(not numpy.array_equal(debug_image, full), "a surface debug view changes the image")
        client.json_tool("call", {"name": "debug.view", "arguments": {"Surface": surface_names[0]}})
        client.json_tool("wait_frames", {"frames": 2})
        _, restored = capture(client, "capture_screen", {"downsample": 1, "name": "check_debug_off"})
        check(numpy.array_equal(restored, full), "turning the debug view off restores the image byte for byte")

        for tool, arguments, label in (
            ("capture_screen", {"name": "../escape"}, "a name with a path is refused"),
            ("capture_region", {"x": width - 10, "y": 0, "width": 20, "height": 20}, "an area past the edge is refused"),
            ("capture_pixel", {"x": -1, "y": 0}, "a negative pixel is refused"),
            ("capture_screen", {"downsample": 0}, "a downsample of 0 is refused"),
            ("set", {"path": "ocean.sea.WindForce", "value": 13}, "a wind force above 12 is refused"),
            ("call", {"name": "debug.view", "arguments": {"Surface": "NoSuchView"}}, "an unknown debug view is refused"),
        ):
            is_error, text, _ = client.tool(tool, arguments)
            check(is_error, f"{label}: {text.splitlines()[0] if text else ''}")

        lines = validation_lines(client)
        stopped = client.json_tool("engine_stop", {})
        check(stopped["exit_code"] == 0, f"the engine exited with {stopped['exit_code']}")
        check(not lines, f"{len(lines)} validation lines")


# found by trying five states on a contact sheet. A fully developed gale reads as gentle undulation from this camera,
# because its waves are 150 to 200 m long. Shallow water and a short fetch read as rough, and no wind reads as glass
STATES = {
    "vanilla": {},
    "shallow_gale": {"WindForce": 9, "Openness": 0.1, "Spread": 0.15, "Regularity": 0.6, "RippleDetail": 1.0,
                     "Depth": 4, "SwellHeight": 2.5, "SwellPeriod": 13, "SwellDirection": 20, "SwellOrder": 0.4,
                     "HeightScale": 1.5, "FoamAmount": 3, "CrestSharpness": 0.35},
    "glassy_swell": {"WindForce": 0.5, "Openness": 0.2, "RippleDetail": 0.3, "SwellHeight": 3, "SwellPeriod": 16,
                     "SwellDirection": 60, "SwellOrder": 0.9, "Depth": 80},
}


# the views that explain a sea: the surface normal, the Jacobian (where crests fold, which drives the whitecaps), and the
# terms of the foam
DEBUG_VIEWS = ("normal", "jacobian", "foamterms")


def capture_debug_views(client, output, label):
    tiles = []
    for view in DEBUG_VIEWS:
        client.json_tool("call", {"name": "debug.view", "arguments": {"Surface": view}})
        client.json_tool("wait_frames", {"frames": 2})
        reply, image = capture(client, "capture_screen", {"downsample": 2, "name": f"demo_{label}_{view}"})
        shutil.copy(reply["Path"], output / f"{label}_{view}.png")
        tiles.append(image)
    client.json_tool("call", {"name": "debug.view", "arguments": {"Surface": "off"}})
    sheet = numpy.concatenate(tiles, axis=0).astype(numpy.uint8)
    Image.fromarray(sheet).save(output / f"{label}_debug_views.png")


def run_demo(client, output):
    output.mkdir(parents=True, exist_ok=True)
    client.json_tool("engine_launch", {"arguments": ["--borderless", "--size=512", "--dt=0.0166667"]})
    base = client.json_tool("get", {"path": "ocean.sea"})
    client.json_tool("call", {"name": "camera.lookat", "arguments": {"Eye": [-60, 18, 90], "Target": [40, 0, -150]}})
    shots = {}
    seas = {}
    for label, changes in STATES.items():
        for field, value in {**base, **changes}.items():
            client.json_tool("set", {"path": f"ocean.sea.{field}", "value": value})
        # long enough for the waves to develop and for the whitecaps to build up foam
        client.json_tool("wait_frames", {"frames": 900})
        reply, image = capture(client, "capture_screen", {"downsample": 1, "name": f"demo_{label}"})
        shutil.copy(reply["Path"], output / f"{label}.png")
        shots[label] = image
        seas[label] = client.json_tool("get", {"path": "ocean.sea"})
        print(f"{label}: {reply['Path']}", flush=True)
        if label == "shallow_gale":
            capture_debug_views(client, output, label)
    lines = validation_lines(client)
    stopped = client.json_tool("engine_stop", {})
    check(stopped["exit_code"] == 0, f"the engine exited with {stopped['exit_code']}")
    check(not lines, f"{len(lines)} validation lines")
    # a stack at half size, for a quick look
    halves = [Image.fromarray(shots[label].astype(numpy.uint8)).reduce(2) for label in STATES]
    stack = Image.new("RGB", (halves[0].width, halves[0].height * len(halves)))
    for index, half in enumerate(halves):
        stack.paste(half, (0, index * half.height))
    stack.save(output / "comparison.png")
    (output / "sea_states.json").write_text(json.dumps(seas, indent=2))
    for label in list(STATES)[1:]:
        difference = numpy.abs(shots["vanilla"] - shots[label]).mean()
        print(f"mean absolute difference, vanilla against {label}: {difference:.2f} code values", flush=True)
        check(difference > 5.0, f"{label} differs from the default sea")


def main():
    executable, mode = sys.argv[1], sys.argv[2]
    client = McpClient(executable)
    try:
        if mode == "checks":
            run_checks(client, sys.argv[3:])
        else:
            run_demo(client, pathlib.Path(sys.argv[3]))
    finally:
        client.close()
    print(f"{mode.upper()} " + ("PASS" if not failures else "FAIL: " + "; ".join(failures)), flush=True)
    sys.exit(0 if not failures else 1)


if __name__ == "__main__":
    main()
