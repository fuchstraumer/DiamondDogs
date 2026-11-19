"""DiamondDogs profiler package."""

from .db_store import (
    HAS_NCU_REPORT,
    SCHEMA_VERSION,
    ingest_gpu_trace,
    ingest_ncu_report,
    init_db,
    insert_bottleneck,
    insert_metric,
    insert_pass,
    insert_run,
    insert_shader,
    insert_shader_line,
    insert_shader_stall,
)
from .ngfx_mcp import NgfxMcpClient, NgfxMcpError, locate_ngfx_tool, read_shader_profile

__all__ = [
    "insert_shader_line",
    "insert_shader_stall",
    "NgfxMcpClient",
    "NgfxMcpError",
    "locate_ngfx_tool",
    "read_shader_profile",
    "HAS_NCU_REPORT",
    "SCHEMA_VERSION",
    "init_db",
    "insert_run",
    "insert_pass",
    "insert_shader",
    "insert_metric",
    "insert_bottleneck",
    "ingest_ncu_report",
    "ingest_gpu_trace",
    "BenchmarkDB",
]


def __getattr__(name: str):
    if name == "BenchmarkDB":
        from .query_metrics import BenchmarkDB
        return BenchmarkDB
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
