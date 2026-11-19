"""Read the shader profiler data of a GPU Trace file through ngfx-mcp, the MCP server of Nsight Graphics.

The text export of a GPU Trace (`--auto-export`) has scope times and counters only. The stall reasons and the
per-line samples of the Real-Time Shader Profiler are only in the .ngfx-gputrace file. Nsight Graphics 2026.4 and
later ship `ngfx-mcp.exe`, which reads them. This module starts it with the Native profile on stdio, sends JSON-RPC
requests, and returns plain Python values. It does not write to the database: `db_store.ingest_shader_profile` does.

The Native profile does not use the MCP initialize handshake. Each request must carry `params._meta` with the
protocol version, the client information and the client capabilities, or the server rejects it with
"Invalid request metadata". The docs are in `<Nsight install>/host/windows-desktop-nomad-x64/docs/ngfx-mcp`.
"""

import json
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional

NSIGHT_ROOT = Path(r"C:\Program Files\NVIDIA Corporation")
NGFX_HOST_SUBDIR = Path("host") / "windows-desktop-nomad-x64"
MCP_PROTOCOL_VERSION = "2026-07-28"
REQUEST_META = {
    "io.modelcontextprotocol/protocolVersion": MCP_PROTOCOL_VERSION,
    "io.modelcontextprotocol/clientInfo": {"name": "diamonddogs-profiler", "version": "1.0"},
    "io.modelcontextprotocol/clientCapabilities": {},
}

# The largest page that the hotspot and catalog queries accept.
MAX_PAGE = 10000
DEVICE_INDEX = 0


class NgfxMcpError(RuntimeError):
    """ngfx-mcp returned a JSON-RPC error or a tool error, or the server closed."""


def nsight_install_dirs() -> List[Path]:
    """Return the Nsight Graphics host folders on this machine, newest version first.

    The folder name holds the version ("Nsight Graphics 2026.4.0"), so a reverse sort puts the newest first.
    """
    if not NSIGHT_ROOT.exists():
        return []
    installs = sorted(NSIGHT_ROOT.glob("Nsight Graphics *"), key=lambda p: p.name, reverse=True)
    return [install / NGFX_HOST_SUBDIR for install in installs if (install / NGFX_HOST_SUBDIR).is_dir()]


def locate_ngfx_tool(exe_name: str) -> Path:
    """Return the path of an Nsight Graphics host tool (for example ngfx.exe) in the newest install.

    Raise FileNotFoundError when no install has the tool.
    """
    for host_dir in nsight_install_dirs():
        candidate = host_dir / exe_name
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"No Nsight Graphics install under {NSIGHT_ROOT} has {exe_name}.")


class NgfxMcpClient:
    """A stdio connection to ngfx-mcp with the Native profile. Use it as a context manager."""

    def __init__(self, exe_path: Path, timeout_s: int):
        self.exe_path = exe_path
        self.timeout_s = timeout_s
        self.proc: Optional[subprocess.Popen] = None
        self.next_id = 0

    def __enter__(self) -> "NgfxMcpClient":
        self.proc = subprocess.Popen(
            [str(self.exe_path), "--profile", "native", "--transport", "stdio",
             "--tool-discovery", "flat-compact", "--log-level", "error"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8", bufsize=1,
        )
        self.request("server/discover", {})
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        if self.proc is None:
            return

        try:
            self.proc.stdin.close()
            self.proc.wait(timeout=self.timeout_s)
        except (OSError, subprocess.TimeoutExpired):
            self.proc.kill()
        self.proc = None

    def request(self, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Send one JSON-RPC request and return its result object.

        Raise NgfxMcpError on a JSON-RPC error or when the server closes its output.
        """
        if self.proc is None:
            raise NgfxMcpError("The client is not started. Use it in a with statement.")

        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method,
                   "params": {**params, "_meta": REQUEST_META}}
        self.proc.stdin.write(json.dumps(message) + "\n")
        self.proc.stdin.flush()

        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise NgfxMcpError(f"ngfx-mcp closed its output during '{method}'.")

            reply = json.loads(line)
            if reply.get("id") != self.next_id:
                continue

            if "error" in reply:
                error = reply["error"]
                raise NgfxMcpError(f"{method}: {error.get('message')} {json.dumps(error.get('data'))}")

            return reply.get("result", {})

    def call(self, tool: str, arguments: Dict[str, Any]) -> Dict[str, Any]:
        """Call one tool and return its structured result.

        Raise NgfxMcpError when the tool reports an error (isError, or an error_code in the result).
        """
        result = self.request("tools/call", {"name": tool, "arguments": arguments})
        payload = result.get("structuredContent")
        if payload is None:
            texts = [item.get("text", "") for item in result.get("content", []) if item.get("type") == "text"]
            payload = json.loads(texts[0]) if len(texts) == 1 else {"content": texts}

        if result.get("isError") or "error_code" in payload:
            raise NgfxMcpError(f"{tool}: {payload.get('error_code')} {payload.get('message', payload)}")

        return payload


def read_shader_profile(client: NgfxMcpClient, trace_path: Path) -> Dict[str, Any]:
    """Read the shader profiler data of one .ngfx-gputrace file over the whole trace time range.

    Return a dictionary with these keys:
    - available: False when the trace has no shader profiler samples (the profiler was off).
    - total_samples: all samples in the trace, unattributed ones included.
    - unattributed: the performance record of the samples that no shader owns, or None.
    - stall_types: type_index -> {"name", "short_name"}.
    - shaders: one record per shader: the catalog fields, its performance record and its hotspot lines.
    """
    opened = client.call("gpu_trace.open_trace", {"path": str(trace_path)})
    trace_id = opened["trace_id"]
    try:
        return _read_open_trace(client, trace_id)
    finally:
        client.call("gpu_trace.close_trace", {"trace_id": trace_id})


def _read_open_trace(client: NgfxMcpClient, trace_id: str) -> Dict[str, Any]:
    """Read the shader profiler data of an open trace. See read_shader_profile."""
    base = {"trace_id": trace_id, "device_index": DEVICE_INDEX}

    stall_reply = client.call("gpu_trace.get_shader_catalog",
                              {**base, "query": "stall_reason_types", "limit": MAX_PAGE})
    stall_types = {r["type_index"]: {"name": r["name"], "short_name": r["short_name"]}
                   for r in stall_reply.get("records", [])}

    catalog = client.call("gpu_trace.get_shader_catalog", {**base, "query": "shaders", "limit": MAX_PAGE})
    shaders: List[Dict[str, Any]] = []
    total_samples = 0
    available = False

    # the profiler range is the whole trace for every query below, so the server keeps its range state
    for record in catalog.get("records", []):
        index = record["shader_index"]
        perf_reply = client.call("gpu_trace.get_shader_profile",
                                 {**base, "query": "performance", "kind": "shader", "shader_index": index})
        if not perf_reply.get("available", False):
            continue

        available = True
        total_samples = perf_reply.get("sample_summary", {}).get("total_sample_count", total_samples)
        hotspot_reply = client.call("gpu_trace.get_shader_profile",
                                    {**base, "query": "hotspots_shader", "shader_index": index,
                                     "include_source_line_text": True, "limit": MAX_PAGE})
        shaders.append({
            "shader_index": index,
            "name": record.get("name"),
            "type": record.get("type"),
            "main_filename": record.get("main_filename"),
            "module_hash": record.get("shader_module_hash"),
            "performance": perf_reply.get("performance") or {},
            "hotspots": hotspot_reply.get("records", []),
            "hotspots_truncated": hotspot_reply.get("records_truncated", False),
        })

    unattributed = None
    if available:
        unattributed_reply = client.call("gpu_trace.get_shader_profile",
                                         {**base, "query": "performance", "kind": "unattributed"})
        unattributed = unattributed_reply.get("performance")

    return {
        "available": available,
        "total_samples": total_samples,
        "unattributed": unattributed,
        "stall_types": stall_types,
        "shaders": shaders,
    }


def hotspot_location(hotspot: Dict[str, Any]) -> Dict[str, Any]:
    """Return the file, line, source text and function scope path of one hotspot record.

    The function scope is the inline chain as Nsight gives it, for example "LoadRotation > BoundReadBuffer.Load"
    for a buffer load inlined through LoadRotation. Nsight lists one scope per inline site, so a name repeats when
    the line is inlined at several sites; repeats next to each other are merged here.
    """
    location = hotspot.get("source_location") or {}
    source_line = location.get("source_line") or {}
    scopes: List[str] = []
    for scope in hotspot.get("function_scopes", []):
        name = scope.get("name", "")
        if not scopes or scopes[-1] != name:
            scopes.append(name)

    return {
        "file_name": location.get("file_name"),
        "line_number": location.get("line_number"),
        "source_text": source_line.get("text"),
        "function_scope": " > ".join(scopes) if scopes else None,
    }
