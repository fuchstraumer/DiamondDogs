"""Headless simulation + multi-layer capture + quantitative diagnostic probe for PlanetGenLoom."""

from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time


class JsonRpcClient:
    def __init__(self, host: str, port: int):
        self.sock = socket.create_connection((host, port), timeout=120.0)
        self.file = self.sock.makefile("r", encoding="utf-8", newline="\n")
        self.next_id = 1

    def request(self, method: str, params: dict):
        req_id = self.next_id
        self.next_id += 1
        payload = json.dumps({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "params": params,
        }) + "\n"
        self.sock.sendall(payload.encode("utf-8"))
        line = self.file.readline()
        if not line:
            raise RuntimeError(f"EOF waiting for reply to {method}")
        resp = json.loads(line)
        if "error" in resp:
            raise RuntimeError(f"{method} failed: {resp['error']}")
        return resp["result"]

    def call(self, name: str, arguments: dict | None = None):
        return self.request("tool.call", {"name": name, "arguments": arguments or {}})

    def set_value(self, path: str, value):
        return self.request("tool.set", {"path": path, "value": value})

    def close(self) -> None:
        self.file.close()
        self.sock.close()


def _resolve_exe(build_dir: str, debug: bool) -> str:
    config = "Debug" if debug else "RelWithDebInfo"
    return os.path.abspath(
        os.path.join(build_dir, "applications", "PlanetGen", config, "PlanetGenLoom.exe")
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run PlanetGenLoom probe and print diagnostic scorecard.")
    parser.add_argument("--reference", default="")
    parser.add_argument("--preset", default="")
    parser.add_argument("--order", type=int, default=8)
    parser.add_argument("--steps", type=int, default=150)
    parser.add_argument("--dt", type=float, default=0.5)
    parser.add_argument("--plumes", default="default")
    parser.add_argument("--shaping", default="")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--plates", type=int, default=0)
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=512)
    parser.add_argument("--out", default="build/planet_probe")
    parser.add_argument("--build-dir", default="build/ninja-clang-cl")
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--shaping-json", default="")
    parser.add_argument("--set", action="append", default=[], help="Override tool field key=value after connect")
    parser.add_argument("--compare", nargs="*", default=[])
    args = parser.parse_args()

    if not args.reference and not args.preset:
        parser.error("supply either --reference=<dir> or --preset=atoll|earth|reference")

    exe = _resolve_exe(args.build_dir, args.debug)
    if not os.path.exists(exe):
        print(f"Missing executable: {exe}", file=sys.stderr)
        return 1

    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    env = os.environ.copy()
    env["VK_LOADER_LAYERS_DISABLE"] = "VK_LAYER_OBS_HOOK"

    cmd = [
        exe,
        f"--order={args.order}",
        f"--dt={args.dt}",
        f"--width={args.width}",
        f"--height={args.height}",
        f"--plumes={args.plumes}",
        "--tool-port=0",
        "--timestamps",
        f"--capture-dir={out_dir}",
    ]
    if args.reference:
        cmd.append(f"--reference={args.reference}")
    if args.preset:
        cmd.append(f"--preset={args.preset}")
    if args.shaping:
        cmd.append(f"--shaping={args.shaping}")
    if args.seed != 42:
        cmd.append(f"--seed={args.seed}")
    if args.plates > 0:
        cmd.append(f"--plates={args.plates}")

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
        env=env,
    )

    port = None
    deadline = time.time() + 30.0
    startup_logs: list[str] = []
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            break
        startup_logs.append(line.rstrip())
        m = re.search(r"ToolServer listening on 127\.0\.0\.1:(\d+)", line)
        if m:
            port = int(m.group(1))
            break

    if port is None:
        out_rest, err_rest = proc.communicate(timeout=10)
        print("\n".join(startup_logs), file=sys.stderr)
        print(out_rest, file=sys.stderr)
        print(err_rest, file=sys.stderr)
        return 1

    t0 = time.time()
    client = JsonRpcClient("127.0.0.1", port)
    try:
        if args.shaping_json:
            with open(args.shaping_json, "r", encoding="utf-8") as f:
                overrides = json.load(f)
            for k, v in overrides.items():
                path = k if "." in k else f"planet.shaping.{k}"
                client.set_value(path, v)

        for kv in args.set:
            if "=" not in kv:
                raise RuntimeError(f"Expected key=value in --set: {kv}")
            key, raw = kv.split("=", 1)
            val = json.loads(raw) if raw.lower() in ("true", "false") or raw[0].isdigit() or raw[0] == "-" else raw
            path = key if "." in key else f"planet.shaping.{key}"
            client.set_value(path, val)

        if args.steps > 0:
            chunk = 25
            done = 0
            while done < args.steps:
                take = min(chunk, args.steps - done)
                st = client.call("planet.step", {"Steps": take, "Dt": args.dt})
                done += take

        cap_elev = client.call("planet.capture", {"Downsample": 1, "Mode": 0, "Name": "elevation"})
        cap_plates = client.call("planet.capture", {"Downsample": 1, "Mode": 1, "Name": "plates"})
        cap_relief = client.call("planet.capture", {"Downsample": 1, "Mode": 3, "Name": "relief"})
        cap_plumes = client.call("planet.capture", {"Downsample": 1, "Mode": 6, "Name": "plumes"})
        timings = client.call("planet.timings")
        client.call("planet.quit")
    finally:
        client.close()

    out_rest, err_rest = proc.communicate(timeout=30)
    elapsed = time.time() - t0
    if proc.returncode != 0:
        print(f"PlanetGenLoom exited with {proc.returncode}", file=sys.stderr)
        print(out_rest, file=sys.stderr)
        print(err_rest, file=sys.stderr)
        return 1

    for marker in ("Validation Error", "VUID-", "VUID_NAME"):
        if marker in out_rest or marker in err_rest:
            print(f"Vulkan validation error detected: {marker}", file=sys.stderr)
            print(err_rest, file=sys.stderr)
            return 1

    label = f"probe(order={args.order}, steps={args.steps}, t={cap_elev['Time']:.1f}My, preset={args.preset or 'ref'})"
    print(f"Probe complete in {elapsed:.2f}s | exit=0 | validation=clean | timings={timings.get('TotalUs', 0.0):.1f} us (top={timings.get('TopLevelUs', 0.0):.1f} us, dropped={timings.get('DroppedScopes', 0)})")
    for p in timings.get("Passes", []):
        if p["TimeUs"] >= 15.0 or "Plume" in p["Name"] or "RenderMap" in p["Name"] or "Uplift" in p["Name"]:
            print(f"  pass {p['Name']:<36} {p['TimeUs']:7.2f} us")

    diag_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "planet_diagnostics.py")
    if args.compare:
        diag_cmd = [
            sys.executable,
            diag_script,
            "compare-targets",
            cap_elev["Path"],
            *args.compare,
            f"--plumes={cap_plumes['Path']}",
            f"--plates={cap_plates['Path']}",
        ]
    else:
        diag_cmd = [
            sys.executable,
            diag_script,
            "analyze-frame",
            cap_elev["Path"],
            f"--plumes={cap_plumes['Path']}",
            f"--plates={cap_plates['Path']}",
            f"--label={label}",
        ]
    res = subprocess.run(diag_cmd, check=False)
    _ = cap_relief
    return res.returncode


if __name__ == "__main__":
    raise SystemExit(main())
