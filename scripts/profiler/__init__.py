"""DiamondDogs profiler package."""

from .db_store import (
    analyze_bottlenecks,
    init_db,
    insert_metric,
    insert_pass,
    insert_run,
    insert_shader,
)

__all__ = [
    "init_db",
    "insert_run",
    "insert_pass",
    "insert_shader",
    "insert_metric",
    "analyze_bottlenecks",
    "BenchmarkDB",
]


def __getattr__(name: str):
    if name == "BenchmarkDB":
        from .query_metrics import BenchmarkDB
        return BenchmarkDB
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
