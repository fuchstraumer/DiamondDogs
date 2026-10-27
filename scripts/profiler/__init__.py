"""DiamondDogs profiler package."""

from .db_store import (
    HAS_NCU_REPORT,
    analyze_bottlenecks,
    ingest_ncu_report,
    init_db,
    insert_metric,
    insert_pass,
    insert_run,
    insert_shader,
)

__all__ = [
    "HAS_NCU_REPORT",
    "init_db",
    "insert_run",
    "insert_pass",
    "insert_shader",
    "insert_metric",
    "analyze_bottlenecks",
    "ingest_ncu_report",
    "BenchmarkDB",
]


def __getattr__(name: str):
    if name == "BenchmarkDB":
        from .query_metrics import BenchmarkDB
        return BenchmarkDB
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
