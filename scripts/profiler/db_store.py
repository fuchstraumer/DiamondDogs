"""Store GPU profiling records in SQLite.

The insert helpers do not commit. The caller wraps one ingest in `with conn:`,
so one run is one transaction.
"""

import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path
from types import ModuleType
from typing import Any, Dict, Optional, Sequence, Set, Union

try:
    from .gpu_trace import parse_gpu_trace_export
except ImportError:
    script_dir = Path(__file__).resolve().parent
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))
    from gpu_trace import parse_gpu_trace_export

NCU_INSTALL_ROOT = Path(r"C:\Program Files\NVIDIA Corporation")

NCU_NOT_AVAILABLE_MESSAGE = (
    "The ncu_report module is not importable, and no Nsight Compute install with an extras/python "
    "folder was found. Nsight Compute is not installed on this machine. It profiles CUDA kernels, "
    "not Vulkan dispatches, so it cannot profile the Vulkan shaders of this engine."
)


def _load_ncu_report() -> Optional[ModuleType]:
    """Import ncu_report, with a search of the Nsight Compute install folders.

    Return the module or None.
    """
    try:
        import ncu_report as module
        return module
    except ImportError:
        pass

    if NCU_INSTALL_ROOT.exists():
        for extras in sorted(NCU_INSTALL_ROOT.glob("Nsight Compute*/extras/python")):
            if str(extras) not in sys.path:
                sys.path.insert(0, str(extras))
            try:
                import ncu_report as module
                return module
            except ImportError:
                continue

    return None


ncu_report = _load_ncu_report()
HAS_NCU_REPORT = ncu_report is not None

# PRAGMA user_version of a database that this code created or migrated.
SCHEMA_VERSION = 4

RUN_COLUMNS: Dict[str, str] = {
    "timestamp": "TEXT",
    "binary_name": "TEXT",
    "binary_path": "TEXT",
    "command_args": "TEXT",
    "git_commit": "TEXT",
    "gpu_name": "TEXT",
    "gpu_clocks": "TEXT",
    "driver_version": "TEXT",
    "frame_count": "INTEGER",
    "avg_frame_time_ms": "REAL",
    "avg_fps": "REAL",
    "mode": "TEXT",
    "git_dirty": "INTEGER",
    "git_diff_hash": "TEXT",
    "shader_manifest_hash": "TEXT",
    "shader_manifest_mtime": "TEXT",
    "exe_hash": "TEXT",
    "exe_mtime": "TEXT",
    "ticks_per_config": "INTEGER",
    "warmup_ticks": "INTEGER",
    "artifact_path": "TEXT",
}

PASS_COLUMNS: Dict[str, str] = {
    "run_id": "INTEGER",
    "pass_name": "TEXT",
    "queue_family": "TEXT",
    "duration_ns": "REAL",
    "iteration": "INTEGER",
    "config_size": "INTEGER",
    "config_radix": "INTEGER",
    "config_wave": "TEXT",
    "config_path": "TEXT",
    "p95_ns": "REAL",
    "cpu_submit_ms": "REAL",
    "cpu_finish_ms": "REAL",
}

SHADER_COLUMNS: Dict[str, str] = {
    "run_id": "INTEGER",
    "pass_id": "INTEGER",
    "stage": "TEXT",
    "shader_name": "TEXT",
    "entry_point": "TEXT",
    "register_count": "INTEGER",
    "shared_mem_bytes": "INTEGER",
    "local_mem_bytes": "INTEGER",
    "subgroup_size": "INTEGER",
    "block_size": "INTEGER",
}

METRIC_COLUMNS: Dict[str, str] = {
    "run_id": "INTEGER",
    "shader_id": "INTEGER",
    "pass_id": "INTEGER",
    "category": "TEXT",
    "metric_name": "TEXT",
    "raw_name": "TEXT",
    "metric_value": "REAL",
    "unit": "TEXT",
    "pct_of_peak": "REAL",
}

BOTTLENECK_COLUMNS: Dict[str, str] = {
    "run_id": "INTEGER",
    "shader_id": "INTEGER",
    "severity": "TEXT",
    "category": "TEXT",
    "summary": "TEXT",
    "detail": "TEXT",
    "recommendation": "TEXT",
}

# Table name -> (primary key name, columns, foreign key clauses).
TABLES: Dict[str, Any] = {
    "runs": ("run_id", RUN_COLUMNS, []),
    "passes": (
        "pass_id",
        PASS_COLUMNS,
        ["FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE"],
    ),
    "shaders": (
        "shader_id",
        SHADER_COLUMNS,
        [
            "FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE",
            "FOREIGN KEY (pass_id) REFERENCES passes(pass_id) ON DELETE SET NULL",
        ],
    ),
    "metrics": (
        "metric_id",
        METRIC_COLUMNS,
        [
            "FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE",
            "FOREIGN KEY (shader_id) REFERENCES shaders(shader_id) ON DELETE SET NULL",
            "FOREIGN KEY (pass_id) REFERENCES passes(pass_id) ON DELETE SET NULL",
        ],
    ),
    "bottlenecks": (
        "bottleneck_id",
        BOTTLENECK_COLUMNS,
        [
            "FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE",
            "FOREIGN KEY (shader_id) REFERENCES shaders(shader_id) ON DELETE SET NULL",
        ],
    ),
}


def init_db(db_path: Union[str, Path]) -> sqlite3.Connection:
    """Open the SQLite database, create missing tables, and add missing columns.

    An older database keeps its rows. New columns hold NULL for old rows.
    Return the database connection.
    """
    path = Path(db_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(path))
    conn.row_factory = sqlite3.Row

    conn.execute("PRAGMA foreign_keys = ON;")
    conn.execute("PRAGMA journal_mode = WAL;")

    with conn:
        for table, (pk_name, columns, foreign_keys) in TABLES.items():
            column_defs = [f"{pk_name} INTEGER PRIMARY KEY AUTOINCREMENT"]
            column_defs += [f"{name} {sql_type}" for name, sql_type in columns.items()]
            column_defs += foreign_keys
            conn.execute(f"CREATE TABLE IF NOT EXISTS {table} ({', '.join(column_defs)});")

            existing = {row["name"] for row in conn.execute(f"PRAGMA table_info({table});")}
            for name, sql_type in columns.items():
                if name not in existing:
                    conn.execute(f"ALTER TABLE {table} ADD COLUMN {name} {sql_type};")

        version = conn.execute("PRAGMA user_version;").fetchone()[0]
        if version < SCHEMA_VERSION:
            conn.execute(f"PRAGMA user_version = {SCHEMA_VERSION};")

    return conn


def _insert(conn: sqlite3.Connection, table: str, columns: Sequence[str], data: Dict[str, Any]) -> int:
    """Insert one row without a commit. Missing keys become NULL.

    Return the new row identifier.
    """
    names = ", ".join(columns)
    marks = ", ".join(f":{c}" for c in columns)
    params = {c: data.get(c) for c in columns}
    cursor = conn.execute(f"INSERT INTO {table} ({names}) VALUES ({marks});", params)
    return int(cursor.lastrowid)


def insert_run(conn: sqlite3.Connection, run_data: Dict[str, Any]) -> int:
    """Insert a run entry. Do not commit.

    Return the run identifier.
    """
    return _insert(conn, "runs", list(RUN_COLUMNS), run_data)


def insert_pass(conn: sqlite3.Connection, pass_data: Dict[str, Any]) -> int:
    """Insert a pass timing record. Do not commit.

    Return the pass identifier.
    """
    return _insert(conn, "passes", list(PASS_COLUMNS), pass_data)


def insert_shader(conn: sqlite3.Connection, shader_data: Dict[str, Any]) -> int:
    """Insert shader metadata. Do not commit.

    Return the shader identifier.
    """
    return _insert(conn, "shaders", list(SHADER_COLUMNS), shader_data)


def insert_metric(conn: sqlite3.Connection, metric_data: Dict[str, Any]) -> int:
    """Insert a profiling metric. Do not commit.

    Return the metric identifier.
    """
    return _insert(conn, "metrics", list(METRIC_COLUMNS), metric_data)


def insert_bottleneck(conn: sqlite3.Connection, bottleneck_data: Dict[str, Any]) -> int:
    """Insert a bottleneck record. Do not commit.

    Return the bottleneck identifier.
    """
    return _insert(conn, "bottlenecks", list(BOTTLENECK_COLUMNS), bottleneck_data)


def _find_trace_artifact(out_dir: Path) -> Optional[Path]:
    """Find the .ngfx-gputrace file in the export folder, or return None."""
    matches = sorted(out_dir.glob("*.ngfx-gputrace"), key=lambda p: p.stat().st_mtime)
    return matches[-1] if matches else None


def _warn_if_validation_on(app_args: str) -> None:
    """Print a warning when the app runs with the validation layer on.

    The validation layer changes GPU times, so the trace times are then not comparable.
    """
    if "--validation=off" not in app_args.split():
        print("[Profiler] Warning: the app arguments do not contain --validation=off. "
              "The validation layer changes GPU times (Mips: 0.87 ms on, 0.18 ms off).")


def _trace_metric_row(run_id: int, pass_id: Optional[int], metric: Dict[str, Any]) -> Dict[str, Any]:
    """Build the metrics row of one GPU Trace metric record."""
    name = metric["metric_name"]
    return {
        "run_id": run_id,
        "pass_id": pass_id,
        "category": _metric_category(name),
        "metric_name": name,
        "raw_name": metric["raw_name"],
        "metric_value": metric["value"],
        "unit": "%" if ".pct" in name else None,
        "pct_of_peak": metric["value"] if "pct_of_peak" in name else None,
    }


def ingest_gpu_trace(
    conn: sqlite3.Connection,
    out_dir: Path,
    exe_path: Optional[Path],
    app_args: str,
    provenance: Optional[Dict[str, Any]],
) -> int:
    """Ingest a GPU Trace export folder as one transaction.

    The run stores one frame. Each debug-label scope becomes a pass, and each scope metric
    is linked to its pass. Whole-frame metrics have no pass. The run stores the provenance
    dictionary only when the caller passes one. The export folder must contain a BASE folder.
    Return the run identifier.
    """
    export = parse_gpu_trace_export(out_dir / "BASE")
    repro = export["repro_info"]
    effective_args = app_args or repro.get("Command Line", "")
    _warn_if_validation_on(effective_args)

    frame_time_ms = export["frame_time_ms"]
    avg_fps = 1000.0 / frame_time_ms if frame_time_ms else None
    if exe_path is not None:
        binary_path = str(exe_path.resolve())
        binary_name = exe_path.name
    else:
        binary_path = repro.get("Process File Name")
        binary_name = Path(binary_path).name if binary_path else None

    artifact = _find_trace_artifact(out_dir)
    attached_paths: Set[str] = set()
    metric_count = 0

    with conn:
        run_id = insert_run(conn, {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "binary_name": binary_name,
            "binary_path": binary_path,
            "command_args": effective_args,
            "gpu_name": repro.get("Device Name"),
            "driver_version": repro.get("Driver Version"),
            "gpu_clocks": repro.get("GPU Clocks"),
            "frame_count": 1,
            "avg_frame_time_ms": frame_time_ms,
            "avg_fps": avg_fps,
            "mode": "gputrace",
            "artifact_path": str(artifact) if artifact else None,
            **(provenance or {}),
        })

        for scope in export["scopes"]:
            path = scope["path"]
            pass_id = insert_pass(conn, {
                "run_id": run_id,
                "pass_name": path,
                "queue_family": "Graphics",
                "duration_ns": scope["time_ms"] * 1_000_000.0,
            })

            # A repeated scope path has one regimes row, so its metrics go to the first pass only.
            if path in attached_paths:
                continue

            attached_paths.add(path)
            for metric in export["scope_metrics"][path]:
                insert_metric(conn, _trace_metric_row(run_id, pass_id, metric))
                metric_count += 1

        for metric in export["frame_metrics"]:
            insert_metric(conn, _trace_metric_row(run_id, None, metric))
            metric_count += 1

    unmatched = export["unmatched_scopes"]
    print(f"[Profiler] GPU Trace ingest: {len(export['scopes'])} scopes, {metric_count} metrics, "
          f"{export['skipped_cells']} cells skipped.")
    if unmatched:
        print(f"[Profiler] Warning: no regimes row for scopes: {', '.join(unmatched)}")
    return run_id


def _duration_to_ns(value: float, unit: str) -> float:
    """Convert an NCU duration value to nanoseconds from its unit text."""
    unit_lower = unit.lower()
    if "nsecond" in unit_lower or unit_lower == "ns":
        return value
    if "usecond" in unit_lower or unit_lower == "us" or "µs" in unit_lower:
        return value * 1000.0
    if "msecond" in unit_lower or unit_lower == "ms":
        return value * 1_000_000.0
    if "second" in unit_lower or unit_lower == "s":
        return value * 1_000_000_000.0
    return value


def _metric_value(m: Any) -> Optional[float]:
    """Read the numeric value of an NCU metric by its value kind.

    Return None when the metric has no numeric value.
    """
    if hasattr(m, "has_value") and not m.has_value():
        return None

    metric_type = ncu_report.IMetric
    kind = m.kind() if hasattr(m, "kind") else None

    if kind in (getattr(metric_type, "ValueKind_DOUBLE", 4), getattr(metric_type, "ValueKind_FLOAT", 3)):
        value = m.as_double()
        return float(value) if isinstance(value, (int, float)) else None

    if kind in (getattr(metric_type, "ValueKind_UINT32", 5), getattr(metric_type, "ValueKind_UINT64", 6)):
        value = m.as_uint64()
        return float(value) if isinstance(value, (int, float)) else None

    if kind == getattr(metric_type, "ValueKind_STRING", 2):
        text = m.as_string()
        try:
            return float(text) if text is not None else None
        except (ValueError, TypeError):
            return None

    return None


def _metric_pct_of_peak(m: Any) -> Optional[float]:
    """Read the percent-of-peak value of an NCU metric.

    Return None when the metric has none.
    """
    if hasattr(m, "pct_of_peak"):
        pct = m.pct_of_peak() if callable(m.pct_of_peak) else m.pct_of_peak
        return float(pct) if isinstance(pct, (int, float)) else None

    if hasattr(m, "metric_subtype"):
        metric_type = ncu_report.IMetric
        pct_subtypes = (
            getattr(metric_type, "MetricSubtype_PCT_OF_PEAK_SUSTAINED_ACTIVE", 13),
            getattr(metric_type, "MetricSubtype_PCT_OF_PEAK_SUSTAINED_ELAPSED", 14),
            getattr(metric_type, "MetricSubtype_PCT", 16),
        )
        if m.metric_subtype() in pct_subtypes:
            value = m.as_double()
            return float(value) if isinstance(value, (int, float)) else None

    return None


def _metric_category(name: str) -> str:
    """Return the category of an NCU metric: the text before the first double underscore.

    A name without a double underscore has the category general.
    """
    if "__" in name:
        return name.split("__", 1)[0]
    return "general"


def _read_uint(action: Any, name: str, stats: Dict[str, int]) -> Optional[int]:
    """Read one unsigned NCU metric. Count a failed read in stats.

    Return the value or None.
    """
    try:
        m = action.metric_by_name(name)
        if m is not None and getattr(m, "has_value", lambda: True)():
            return int(m.as_uint64())
    except Exception:
        stats["skipped"] += 1
    return None


def _rule_severity(rule: Dict[str, Any], rule_msg: Any) -> str:
    """Take the severity from the rule's own type or severity field, else info."""
    sources = [rule]
    if isinstance(rule_msg, dict):
        sources.append(rule_msg)

    for source in sources:
        for key in ("severity", "type"):
            value = source.get(key)
            if isinstance(value, str) and value:
                return value.lower()

    return "info"


def ingest_ncu_report(
    conn: sqlite3.Connection,
    report_path: Union[str, Path],
    run_id: Optional[int] = None,
) -> int:
    """Ingest an NVIDIA Nsight Compute report as one transaction.

    NCU profiles CUDA kernels. It does not profile Vulkan dispatches.
    Metric category is the text before the first double underscore of the metric name.
    Return the run identifier.
    """
    path = Path(report_path)
    if not path.exists():
        raise FileNotFoundError(f"Report file not found: {report_path}")

    if not HAS_NCU_REPORT or ncu_report is None:
        raise RuntimeError(NCU_NOT_AVAILABLE_MESSAGE)

    ctx = ncu_report.load_report(str(report_path))
    stats = {"skipped": 0, "no_value": 0}

    with conn:
        if run_id is None:
            run_id = insert_run(conn, {
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "binary_name": path.name,
                "binary_path": str(path.resolve()),
                "command_args": "ncu_report import",
                "mode": "ncu",
                "artifact_path": str(path.resolve()),
            })

        num_ranges = ctx.num_ranges() if hasattr(ctx, "num_ranges") else len(ctx)
        for range_idx in range(num_ranges):
            rng = ctx.range_by_idx(range_idx)
            num_actions = rng.num_actions() if hasattr(rng, "num_actions") else len(rng)
            for action_idx in range(num_actions):
                action = rng.action_by_idx(action_idx)
                _ingest_ncu_action(conn, run_id, action, action_idx, stats)

    print(f"[Profiler] NCU ingest skipped {stats['skipped']} metrics that raised errors "
          f"and stored {stats['no_value']} metrics without a numeric value as NULL.")
    return run_id


def _ingest_ncu_action(
    conn: sqlite3.Connection,
    run_id: int,
    action: Any,
    action_idx: int,
    stats: Dict[str, int],
) -> None:
    """Store one NCU kernel action: pass, shader, metrics and rule results."""
    kernel_name = action.name() if hasattr(action, "name") else f"kernel_{action_idx}"

    duration_ns = 0.0
    for candidate in ("gpu__time_duration.sum", "gpu__time_duration.avg", "gpu__time_duration", "Duration"):
        try:
            m_dur = action.metric_by_name(candidate)
            if m_dur is not None and getattr(m_dur, "has_value", lambda: True)():
                val = m_dur.as_double()
                if val > 0:
                    duration_ns = _duration_to_ns(val, m_dur.unit() or "")
                    break
        except Exception:
            stats["skipped"] += 1

    pass_id = insert_pass(conn, {
        "run_id": run_id,
        "pass_name": kernel_name,
        "queue_family": "Compute",
        "duration_ns": duration_ns,
        "iteration": action_idx,
    })

    shader_id = insert_shader(conn, {
        "run_id": run_id,
        "pass_id": pass_id,
        "stage": "compute",
        "shader_name": kernel_name,
        "entry_point": kernel_name,
        "register_count": _read_uint(action, "launch__registers_per_thread", stats),
        "shared_mem_bytes": _read_uint(action, "launch__shared_mem_per_block_allocated", stats),
        "local_mem_bytes": None,
        "subgroup_size": None,
        "block_size": _read_uint(action, "launch__block_size", stats),
    })

    metric_names = action.metric_names() if hasattr(action, "metric_names") else []
    for name in metric_names:
        try:
            m = action.metric_by_name(name)
            if m is None:
                continue

            val = _metric_value(m)
            if val is None:
                stats["no_value"] += 1

            insert_metric(conn, {
                "run_id": run_id,
                "shader_id": shader_id,
                "category": _metric_category(name),
                "metric_name": name,
                "metric_value": val,
                "unit": m.unit() if hasattr(m, "unit") else "",
                "pct_of_peak": _metric_pct_of_peak(m),
            })
        except Exception:
            stats["skipped"] += 1

    rule_results = action.rule_results_as_dicts() if hasattr(action, "rule_results_as_dicts") else []
    for r in rule_results:
        rule_name = r.get("name") or "NCU Rule"
        rule_msg = r.get("rule_message") or {}
        msg = rule_msg.get("message", "") if isinstance(rule_msg, dict) else str(rule_msg)
        title = (rule_msg.get("title") if isinstance(rule_msg, dict) else None) or rule_name

        speedup_est = r.get("speedup_estimation")
        if speedup_est and isinstance(speedup_est, dict):
            sp = speedup_est.get("speedup")
            if sp is not None:
                sp_type = speedup_est.get("type")
                sp_info = f" (Estimated speedup: {sp:.1f}%{f' - {sp_type}' if sp_type else ''})"
                msg = f"{msg}{sp_info}".strip()

        insert_bottleneck(conn, {
            "run_id": run_id,
            "shader_id": shader_id,
            "severity": _rule_severity(r, rule_msg),
            "category": "ncu_rule",
            "summary": title,
            "detail": msg,
            "recommendation": None,
        })
