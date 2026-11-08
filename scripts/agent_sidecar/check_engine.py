"""End-to-end checks of the sidecar and the engine's capture tools, through MCP as an agent sees them.

    python check_engine.py <OceanFft.exe> checks [--window=WxH | --borderless]
    python check_engine.py <OceanFft.exe> demo <output folder>

checks compares independent results: two captures of a paused sea with TAA off, a region against a crop of the full
capture, the downsample against a linear box average, pixels against the PNG, one held time reached by a jump and by
steps, raw targets against each other (Hi-Z against the depth it reduces), and the auto exposure against numpy and
against the same exposure set by hand. It needs the MCP SDK, numpy and Pillow, so run it with the Python that has them.
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


def held_capture(client, name):
    reply = client.json_tool("capture_screen", {"downsample": 1, "name": name, "include_image": False})
    return reply["Path"]


def diff(client, path_a, path_b):
    is_error, text, image = client.tool("capture_diff", {"path_a": path_a, "path_b": path_b})
    if is_error:
        raise RuntimeError(f"capture_diff failed: {text}")
    return json.loads(text), image


# AP1 luminance weights, as ddColor and the auto exposure pass use them
AP1_Y = numpy.array([0.2722287, 0.6740818, 0.0536895])
# the GPU sums float logs in 256 threads, so its mean differs from numpy's double mean by rounding only. A wrong
# weight or clamp moves the mean by more than 1e-3
MEAN_LOG_TOLERANCE = 1.0e-4


def target(client, name):
    """Returns the capture_target report and the values as a height x width x channels array."""
    report = client.json_tool("capture_target", {"target": name, "include_image": False})
    values = numpy.fromfile(report["Path"], dtype=report["Dtype"])
    return report, values.reshape(report["Height"], report["Width"], report["Channels"])


def run_target_checks(client, screen):
    """Raw targets at a held time. The exposure checks compare the GPU's metering with numpy, and the composite's image
    at the metered exposure with the image at the same exposure set by hand."""
    width, height = screen
    expected_sizes = {"scene": (width, height, 4, 2), "normal_sigma": (width, height, 4, 2),
                      "ocean_depth": (width, height, 1, 4), "hiz": ((width + 1) // 2, (height + 1) // 2, 1, 4),
                      "exposure_state": (1, 1, 4, 4), "exposure_ev": (1, 1, 1, 4)}

    for name, (target_width, target_height, channels, bytes_per_channel) in expected_sizes.items():
        report, values = target(client, name)
        size = pathlib.Path(report["Path"]).stat().st_size
        check(values.shape == (target_height, target_width, channels) and
              size == target_width * target_height * channels * bytes_per_channel,
              f"capture_target {name} is {report['Width']}x{report['Height']}x{report['Channels']} {report['Dtype']}, "
              f"{size} bytes")

    _, first = target(client, "scene")
    _, second = target(client, "scene")
    check(first.tobytes() == second.tobytes(), "two raw captures of the held scene are identical")

    # Hi-Z keeps the nearest depth of each 2x2 block, and reverse Z makes that the largest. An odd size clamps the block
    _, depth = target(client, "ocean_depth")
    _, hiz = target(client, "hiz")
    padded = numpy.pad(depth[..., 0], ((0, height % 2), (0, width % 2)), mode="edge")
    blocks = padded.reshape(padded.shape[0] // 2, 2, padded.shape[1] // 2, 2).max(axis=(1, 3))
    check(numpy.array_equal(blocks, hiz[..., 0]) and depth.max() > 0.0,
          f"hiz equals the 2x2 maximum of ocean_depth in every texel (depth max {float(depth.max()):.6f})")

    luminance = first[..., :3].astype(numpy.float64) @ AP1_Y
    mean_log = numpy.clip(numpy.log2(numpy.maximum(1e-4, luminance)), -8.0, 4.0).mean()
    _, state = target(client, "exposure_state")
    _, metered = target(client, "exposure_ev")
    adapted, gpu_mean = float(state[0, 0, 0]), float(state[0, 0, 3])
    metered_ev = float(metered[0, 0, 0])
    check(abs(gpu_mean - mean_log) < MEAN_LOG_TOLERANCE,
          f"the GPU's mean log2 luminance {gpu_mean:.7f} equals numpy's {mean_log:.7f} (difference "
          f"{gpu_mean - mean_log:.2e})")
    check(abs(adapted / 2.0 ** gpu_mean - 1.0) < 1e-6,
          f"repeatable mode adapts at once: adapted luminance {adapted:.7f}, target {2.0 ** gpu_mean:.7f}")
    check(abs(metered_ev - numpy.log2(0.18 / adapted)) < 1e-5,
          f"the metered exposure {metered_ev:.6f} EV maps the adapted luminance to 18% gray")

    auto_image = held_capture(client, "check_exposure_auto")
    client.json_tool("set", {"path": "ocean.exposure.Auto", "value": False})
    client.json_tool("set", {"path": "ocean.exposure.Ev", "value": metered_ev})
    client.json_tool("wait_frames", {"frames": 2})
    _, manual = target(client, "exposure_ev")
    check(manual.tobytes() == metered.tobytes(), f"ocean.exposure.Ev {float(manual[0, 0, 0]):.6f} stores the "
                                                 f"metered float")
    report, _ = diff(client, auto_image, held_capture(client, "check_exposure_manual"))
    check(report["identical"], f"the composite at the metered exposure set by hand is identical to auto exposure: "
                               f"{report['changed_pixels']} pixels changed")

    client.json_tool("set", {"path": "ocean.exposure.Ev", "value": 0.0})
    client.json_tool("wait_frames", {"frames": 2})
    report, _ = diff(client, auto_image, held_capture(client, "check_exposure_zero"))
    check(abs(metered_ev) < 0.1 or not report["identical"],
          f"exposure 0 differs from the metered {metered_ev:.3f} EV: mean difference "
          f"{report['mean_difference']:.1f} code values")

    client.json_tool("set", {"path": "ocean.exposure.Auto", "value": True})
    client.json_tool("set", {"path": "ocean.exposure.Ev", "value": 1.0})
    client.json_tool("wait_frames", {"frames": 2})
    _, compensated = target(client, "exposure_ev")
    check(abs(float(compensated[0, 0, 0]) - metered_ev - 1.0) < 1e-5,
          f"a compensation of +1 EV gives {float(compensated[0, 0, 0]):.6f} EV")
    client.json_tool("set", {"path": "ocean.exposure.Ev", "value": 0.0})
    client.json_tool("wait_frames", {"frames": 2})

    is_error, text, _ = client.tool("capture_target", {"target": "no_such_target"})
    check(is_error and "exposure_ev" in text, "an unknown target is refused, and the error lists the targets")


def run_repeatable_checks(client, window_arguments):
    """A held time must give the same bytes whatever came before it, and a different time must not. This launch keeps
    TAA and auto exposure on, as the demo starts, because repeatable mode exists to remove their history."""
    client.json_tool("engine_launch", {"arguments": [*window_arguments, "--size=128"]})
    client.json_tool("wait_frames", {"frames": 60})
    held = client.json_tool("hold_time", {"time": 10.0})
    check(held["SimulationTime"] == 10.0 and held["Paused"] and held["Repeatable"],
          f"hold_time 10 pauses in repeatable mode at {held['SimulationTime']}")
    direct = held_capture(client, "check_time_direct")

    # TAA jitter cycles through 8 offsets, so three captures that agree rule out a jitter that only matched by chance
    for index in (1, 2):
        report, _ = diff(client, direct, held_capture(client, f"check_time_again_{index}"))
        check(report["identical"], f"capture {index + 1} of one held time is identical: {report['changed_pixels']} "
                                   f"pixels changed")

    # reach the same time after a bright debug view far away, so that exposure and TAA carry a different history
    client.json_tool("hold_time", {"time": 37.3, "settle_frames": 0})
    client.json_tool("call", {"name": "debug.view", "arguments": {"Surface": "normal"}})
    client.json_tool("wait_frames", {"frames": 30})
    client.json_tool("call", {"name": "debug.view", "arguments": {"Surface": "off"}})
    client.json_tool("hold_time", {"time": 9.5})
    stepped = client.json_tool("step_time", {"frames": 16, "seconds": 0.03125})
    check(stepped["SimulationTime"] == 10.0, f"16 steps of 1/32 s from 9.5 reach {stepped['SimulationTime']}")
    report, heat_map = diff(client, direct, held_capture(client, "check_time_stepped"))
    check(report["identical"] and heat_map is None,
          f"time 10 after another history, by steps, is identical: {report['changed_pixels']} pixels changed, "
          f"max {report['max_difference_rgb']}")

    # 3.3 and 1/60 have no exact binary form. Steps added to the float base land one float away from the double sum,
    # and a settle of 0 makes the reply come right after the last step
    client.json_tool("hold_time", {"time": 3.3})
    awkward = client.json_tool("step_time", {"frames": 7, "seconds": 1.0 / 60.0, "settle_frames": 0})
    expected = client.json_tool("hold_time", {"time": 3.3 + 7 * (1.0 / 60.0)})
    check(awkward["SimulationTime"] == expected["SimulationTime"],
          f"7 steps of 1/60 s from 3.3 store the float of 3.3 + 7/60: {awkward['SimulationTime']} and "
          f"{expected['SimulationTime']}")

    client.json_tool("hold_time", {"time": 10.5})
    report, heat_map = diff(client, direct, held_capture(client, "check_time_later"))
    check(not report["identical"] and heat_map is not None and report["changed_fraction"] > 0.1,
          f"time 10.5 differs from time 10: {report['changed_fraction']:.3f} of the pixels, "
          f"max {report['max_difference_rgb']}")

    # the raw targets and the exposure, at a held time with TAA and auto exposure on
    client.json_tool("hold_time", {"time": 10.0})
    frame = client.json_tool("call", {"name": "frame.info"})
    run_target_checks(client, (frame["ScreenWidth"], frame["ScreenHeight"]))

    off = client.json_tool("call", {"name": "frame.repeatable", "arguments": {"Enabled": False}})
    check(not off["Repeatable"], "frame.repeatable turns off")

    for tool, arguments, label in (
        ("hold_time", {"time": -1}, "a negative time is refused"),
        ("step_time", {"frames": 1, "seconds": 0}, "a step of 0 s is refused"),
        ("call", {"name": "frame.step", "arguments": {"Frames": 0, "Seconds": 0.1}}, "a step of 0 frames is refused"),
    ):
        is_error, text, _ = client.tool(tool, arguments)
        check(is_error, f"{label}: {text.splitlines()[0] if text else ''}")

    lines = validation_lines(client)
    stopped = client.json_tool("engine_stop", {})
    check(stopped["exit_code"] == 0, f"the second engine exited with {stopped['exit_code']}")
    check(not lines, f"{len(lines)} validation lines in the second engine")


def run_checks(client, window_arguments):
    with tempfile.TemporaryDirectory() as folder:
        script = pathlib.Path(folder) / "taa_off.txt"
        # TAA jitters the projection by up to a pixel each frame, and auto exposure keeps adapting, so a paused sea
        # differs between frames with either on. The second launch checks them in repeatable mode
        script.write_text("taa off\nauto_exposure off\n")
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

    run_repeatable_checks(client, window_arguments)


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
