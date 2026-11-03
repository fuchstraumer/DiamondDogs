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
)

__all__ = [
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
