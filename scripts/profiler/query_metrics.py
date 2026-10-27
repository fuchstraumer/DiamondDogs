"""Query profiling database records and generate performance reports."""

import argparse
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

try:
    from .db_store import init_db
except ImportError:
    script_dir = Path(__file__).resolve().parent
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))
    from db_store import init_db


def to_markdown_table(headers: List[str], rows: List[List[str]]) -> str:
    """Format headers and row cells into a GitHub markdown table.

    Return the formatted markdown string.
    """
    if not headers:
        return ""
    col_widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            if i < len(col_widths):
                col_widths[i] = max(col_widths[i], len(str(cell)))

    header_line = "| " + " | ".join(headers[i].ljust(col_widths[i]) for i in range(len(headers))) + " |"
    separator_line = "| " + " | ".join("-" * col_widths[i] for i in range(len(headers))) + " |"
    data_lines = [
        "| " + " | ".join(str(row[i]).ljust(col_widths[i]) for i in range(len(headers))) + " |"
        for row in rows
    ]
    return "\n".join([header_line, separator_line] + data_lines)


def fmt_num(value: Optional[float], spec: str, suffix: str) -> str:
    """Format a number, or a dash when the value is NULL."""
    if value is None:
        return "-"
    return f"{value:{spec}}{suffix}"


def config_label(size: Optional[int], radix: Optional[int], wave: Optional[str]) -> str:
    """Name a benchmark configuration. Return a dash for a pass without one."""
    if size is None and radix is None and wave is None:
        return "-"
    return f"{size}/r{radix}/wave {wave}"


class BenchmarkDB:
    """Query profiler benchmarks, passes, metrics, and bottlenecks from SQLite."""

    def __init__(self, db_path: Union[str, Path]):
        """Initialize the benchmark database connection."""
        self.db_path = Path(db_path)
        self.conn = init_db(self.db_path)

    def close(self) -> None:
        """Close the database connection."""
        if self.conn:
            self.conn.close()

    def __enter__(self) -> "BenchmarkDB":
        """Enter the runtime context and return the database instance."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Exit the runtime context and close the database connection."""
        self.close()

    def get_latest_run(self) -> Optional[Dict[str, Any]]:
        """Fetch the most recent profiler run record.

        Return the record dictionary or None.
        """
        cursor = self.conn.execute("SELECT * FROM runs ORDER BY run_id DESC LIMIT 1;")
        row = cursor.fetchone()
        return dict(row) if row else None

    def get_run_summary(self, run_id: int) -> Optional[Dict[str, Any]]:
        """Fetch metadata, aggregated pass timings, and counts for a run.

        Return the run summary dictionary or None.
        """
        cursor = self.conn.execute("SELECT * FROM runs WHERE run_id = ?;", (run_id,))
        run_row = cursor.fetchone()
        if not run_row:
            return None
        run_dict = dict(run_row)

        pass_cursor = self.conn.execute("""
            SELECT config_size, config_radix, config_wave, pass_name,
                   AVG(duration_ns) as avg_duration_ns,
                   AVG(p95_ns) as avg_p95_ns,
                   MIN(duration_ns) as min_duration_ns,
                   MAX(duration_ns) as max_duration_ns,
                   COUNT(*) as sample_count
            FROM passes
            WHERE run_id = ?
            GROUP BY config_size, config_radix, config_wave, pass_name
            ORDER BY MIN(pass_id) ASC;
        """, (run_id,))
        run_dict["passes"] = [dict(r) for r in pass_cursor.fetchall()]

        bottlenecks = self.get_bottlenecks(run_id)
        run_dict["bottlenecks"] = bottlenecks

        shader_count = self.conn.execute("SELECT COUNT(*) FROM shaders WHERE run_id = ?;", (run_id,)).fetchone()[0]
        metric_count = self.conn.execute("SELECT COUNT(*) FROM metrics WHERE run_id = ?;", (run_id,)).fetchone()[0]
        run_dict["shader_count"] = shader_count
        run_dict["metric_count"] = metric_count

        return run_dict

    def get_bottlenecks(self, run_id: int) -> List[Dict[str, Any]]:
        """Retrieve diagnosed bottlenecks for a specified run.

        Return the list of bottleneck records.
        """
        cursor = self.conn.execute("""
            SELECT bottleneck_id, run_id, shader_id, severity, category, summary, detail, recommendation
            FROM bottlenecks
            WHERE run_id = ?
            ORDER BY bottleneck_id ASC;
        """, (run_id,))
        return [dict(r) for r in cursor.fetchall()]

    def get_metrics(
        self,
        run_id: int,
        shader_id: Optional[int] = None,
        category: Optional[str] = None,
    ) -> List[Dict[str, Any]]:
        """Retrieve recorded metrics for a run with optional filters.

        Return the list of metric records.
        """
        query = """
            SELECT m.metric_id, m.run_id, m.shader_id, s.shader_name,
                   m.category, m.metric_name, m.metric_value, m.unit, m.pct_of_peak
            FROM metrics m
            LEFT JOIN shaders s ON m.shader_id = s.shader_id
            WHERE m.run_id = ?
        """
        params: List[Any] = [run_id]
        if shader_id is not None:
            query += " AND m.shader_id = ?"
            params.append(shader_id)
        if category is not None:
            query += " AND m.category = ?"
            params.append(category)
        query += " ORDER BY m.metric_id ASC;"
        cursor = self.conn.execute(query, tuple(params))
        return [dict(r) for r in cursor.fetchall()]

    def compare_runs(self, run_id_a: int, run_id_b: int) -> Dict[str, Any]:
        """Compare performance metrics and passes between two runs.

        Return the comparison dictionary with calculated deltas.
        """
        summary_a = self.get_run_summary(run_id_a)
        summary_b = self.get_run_summary(run_id_b)
        if not summary_a:
            raise ValueError(f"Run ID {run_id_a} not found.")
        if not summary_b:
            raise ValueError(f"Run ID {run_id_b} not found.")

        time_a = summary_a.get("avg_frame_time_ms")
        time_b = summary_b.get("avg_frame_time_ms")
        time_delta = (time_b - time_a) if time_a is not None and time_b is not None else None
        time_pct = (time_delta / time_a * 100.0) if time_delta is not None and time_a > 0.0 else None

        fps_a = summary_a.get("avg_fps")
        fps_b = summary_b.get("avg_fps")
        fps_delta = (fps_b - fps_a) if fps_a is not None and fps_b is not None else None
        fps_pct = (fps_delta / fps_a * 100.0) if fps_delta is not None and fps_a > 0.0 else None

        def key_of(p: Dict[str, Any]) -> Tuple[Any, ...]:
            return (p["config_size"], p["config_radix"], p["config_wave"], p["pass_name"])

        passes_a = {key_of(p): p for p in summary_a.get("passes", [])}
        passes_b = {key_of(p): p for p in summary_b.get("passes", [])}
        keys = list(passes_a.keys()) + [k for k in passes_b.keys() if k not in passes_a]

        def to_us(p: Optional[Dict[str, Any]], field: str) -> Optional[float]:
            if p is None or p[field] is None:
                return None
            return p[field] / 1000.0

        def delta_pct(x: Optional[float], y: Optional[float]) -> Tuple[Optional[float], Optional[float]]:
            if x is None or y is None:
                return None, None
            d = y - x
            return d, ((d / x * 100.0) if x > 0.0 else None)

        pass_comparisons = []
        for key in keys:
            pa = passes_a.get(key)
            pb = passes_b.get(key)
            med_a, med_b = to_us(pa, "avg_duration_ns"), to_us(pb, "avg_duration_ns")
            p95_a, p95_b = to_us(pa, "avg_p95_ns"), to_us(pb, "avg_p95_ns")
            delta, pct = delta_pct(med_a, med_b)
            p95_delta, p95_pct = delta_pct(p95_a, p95_b)
            pass_comparisons.append({
                "config_size": key[0],
                "config_radix": key[1],
                "config_wave": key[2],
                "config": config_label(key[0], key[1], key[2]),
                "pass_name": key[3],
                "median_us_a": med_a,
                "median_us_b": med_b,
                "delta_us": delta,
                "pct_change": pct,
                "p95_us_a": p95_a,
                "p95_us_b": p95_b,
                "p95_delta_us": p95_delta,
                "p95_pct_change": p95_pct,
            })

        return {
            "run_id_a": run_id_a,
            "run_id_b": run_id_b,
            "run_a": summary_a,
            "run_b": summary_b,
            "frame_time_delta_ms": time_delta,
            "frame_time_pct_change": time_pct,
            "fps_delta": fps_delta,
            "fps_pct_change": fps_pct,
            "pass_comparisons": pass_comparisons,
        }


def format_run_summary_md(summary: Dict[str, Any]) -> str:
    """Format run summary data into GitHub markdown tables.

    Return the markdown text.
    """
    run_headers = ["Property", "Value"]
    run_rows = [
        ["Run ID", str(summary.get("run_id"))],
        ["Mode", str(summary.get("mode") or "-")],
        ["Timestamp", str(summary.get("timestamp"))],
        ["Binary", str(summary.get("binary_name"))],
        ["Binary Path", str(summary.get("binary_path"))],
        ["Command Args", str(summary.get("command_args") or "(none)")],
        ["Git Commit", str(summary.get("git_commit") or "-")],
        ["Git Dirty", {1: "yes", 0: "no"}.get(summary.get("git_dirty"), "-")],
        ["Git Diff Hash", str(summary.get("git_diff_hash") or "-")],
        ["Exe Hash / mtime", f"{summary.get('exe_hash') or '-'} / {summary.get('exe_mtime') or '-'}"],
        ["Shader Manifest Hash / mtime",
         f"{summary.get('shader_manifest_hash') or '-'} / {summary.get('shader_manifest_mtime') or '-'}"],
        ["GPU", str(summary.get("gpu_name"))],
        ["Driver Version", str(summary.get("driver_version"))],
        ["Frame Count", str(summary.get("frame_count") if summary.get("frame_count") is not None else "-")],
        ["Ticks / Config, Warm-up", f"{summary.get('ticks_per_config') or '-'}, {summary.get('warmup_ticks') or '-'}"],
        ["Avg Frame Time", fmt_num(summary.get("avg_frame_time_ms"), ".4f", " ms")],
        ["Avg FPS", fmt_num(summary.get("avg_fps"), ".2f", "")],
    ]
    meta_table = to_markdown_table(run_headers, run_rows)

    passes = summary.get("passes", [])
    if passes:
        pass_headers = [
            "Config", "Pass Name", "Duration (µs)", "P95 (µs)", "Min (µs)", "Max (µs)", "Samples",
        ]
        pass_rows = []
        for p in passes:
            pass_rows.append([
                config_label(p["config_size"], p["config_radix"], p["config_wave"]),
                p["pass_name"],
                fmt_num(p["avg_duration_ns"] / 1000.0 if p["avg_duration_ns"] is not None else None, ".2f", ""),
                fmt_num(p["avg_p95_ns"] / 1000.0 if p["avg_p95_ns"] is not None else None, ".2f", ""),
                fmt_num(p["min_duration_ns"] / 1000.0 if p["min_duration_ns"] is not None else None, ".2f", ""),
                fmt_num(p["max_duration_ns"] / 1000.0 if p["max_duration_ns"] is not None else None, ".2f", ""),
                str(p["sample_count"]),
            ])
        pass_table = to_markdown_table(pass_headers, pass_rows)
    else:
        pass_table = "_No pass timing records recorded for this run._"

    out = [
        f"### Run Summary (Run {summary.get('run_id')})\n",
        meta_table,
        "\n### Pass Timings (submit: median over ticks; replay: mean over iterations)\n",
        pass_table,
    ]
    return "\n".join(out)


def format_bottlenecks_md(run_id: int, bottlenecks: List[Dict[str, Any]]) -> str:
    """Format bottleneck records into a GitHub markdown table.

    Return the markdown text.
    """
    if not bottlenecks:
        return f"### Bottlenecks (Run {run_id})\n\n_No bottlenecks detected for Run {run_id}._"

    headers = ["ID", "Severity", "Category", "Summary", "Detail", "Recommendation"]
    rows = []
    for b in bottlenecks:
        rows.append([
            str(b["bottleneck_id"]),
            str(b["severity"] or "-"),
            str(b["category"] or "-"),
            str(b["summary"] or "-"),
            str(b["detail"] or "-"),
            str(b["recommendation"] or "-"),
        ])
    table = to_markdown_table(headers, rows)
    return f"### Bottlenecks (Run {run_id})\n\n{table}"


def format_metrics_md(run_id: int, metrics: List[Dict[str, Any]]) -> str:
    """Format detailed metric records into a GitHub markdown table.

    Return the markdown text.
    """
    if not metrics:
        return f"### Metrics (Run {run_id})\n\n_No metrics recorded for Run {run_id}._"

    headers = ["ID", "Shader", "Category", "Metric Name", "Value", "Unit", "% Peak"]
    rows = []
    for m in metrics:
        val = m.get("metric_value")
        if val is None:
            val_str = "-"
        elif isinstance(val, float):
            val_str = f"{val:.4f}".rstrip("0").rstrip(".") if "." in f"{val:.4f}" else f"{val:.4f}"
        else:
            val_str = str(val)

        pct = m.get("pct_of_peak")
        pct_str = f"{pct:.2f}%" if pct is not None else "-"

        shader_name = m.get("shader_name")
        shader_id = m.get("shader_id")
        shader_str = shader_name if shader_name else (f"Shader #{shader_id}" if shader_id else "-")

        rows.append([
            str(m.get("metric_id", "")),
            shader_str,
            str(m.get("category") or ""),
            str(m.get("metric_name") or ""),
            val_str,
            str(m.get("unit") or ""),
            pct_str,
        ])
    table = to_markdown_table(headers, rows)
    return f"### Detailed Metrics (Run {run_id})\n\n{table}"


def format_compare_runs_md(cmp: Dict[str, Any]) -> str:
    """Format run comparison data into GitHub markdown tables.

    Return the markdown text.
    """
    run_a = cmp["run_a"]
    run_b = cmp["run_b"]
    id_a = cmp["run_id_a"]
    id_b = cmp["run_id_b"]

    meta_headers = ["Metric", f"Run {id_a}", f"Run {id_b}", "Delta", "% Change"]
    meta_rows = [
        [
            "Avg Frame Time",
            fmt_num(run_a.get("avg_frame_time_ms"), ".4f", " ms"),
            fmt_num(run_b.get("avg_frame_time_ms"), ".4f", " ms"),
            fmt_num(cmp["frame_time_delta_ms"], "+.4f", " ms"),
            fmt_num(cmp["frame_time_pct_change"], "+.2f", "%"),
        ],
        [
            "Avg FPS",
            fmt_num(run_a.get("avg_fps"), ".2f", ""),
            fmt_num(run_b.get("avg_fps"), ".2f", ""),
            fmt_num(cmp["fps_delta"], "+.2f", ""),
            fmt_num(cmp["fps_pct_change"], "+.2f", "%"),
        ],
    ]
    meta_table = to_markdown_table(meta_headers, meta_rows)

    pass_headers = [
        "Config", "Pass Name",
        f"Run {id_a} Median (µs)", f"Run {id_b} Median (µs)", "Delta (µs)", "% Change",
        f"Run {id_a} P95 (µs)", f"Run {id_b} P95 (µs)", "P95 % Change",
    ]
    pass_rows = []
    for pc in cmp["pass_comparisons"]:
        pass_rows.append([
            pc["config"],
            pc["pass_name"],
            fmt_num(pc["median_us_a"], ".2f", ""),
            fmt_num(pc["median_us_b"], ".2f", ""),
            fmt_num(pc["delta_us"], "+.2f", ""),
            fmt_num(pc["pct_change"], "+.2f", "%"),
            fmt_num(pc["p95_us_a"], ".2f", ""),
            fmt_num(pc["p95_us_b"], ".2f", ""),
            fmt_num(pc["p95_pct_change"], "+.2f", "%"),
        ])

    pass_table = to_markdown_table(pass_headers, pass_rows)

    out = [
        f"### Run Comparison: Run {id_a} vs Run {id_b}\n",
        meta_table,
        "\n### Pass Comparison\n",
        pass_table,
    ]
    return "\n".join(out)


def main() -> int:
    """Parse command-line arguments and execute the requested query operation.

    Return the exit status code.
    """
    parser = argparse.ArgumentParser(description="Query profiling database metrics and bottlenecks.")
    parser.add_argument("--db", default="profiler.db", help="Path to SQLite database (default: profiler.db)")
    parser.add_argument("--latest", action="store_true", help="Display summary of the latest run")
    parser.add_argument("--run", type=int, help="Specify run ID to query")
    parser.add_argument("--shader", type=int, help="Filter metrics by shader ID")
    parser.add_argument("--category", help="Filter metrics by category")
    parser.add_argument("--metrics", action="store_true", help="Display detailed metrics table for a run or shader")
    parser.add_argument("--bottlenecks", action="store_true", help="Display diagnosed bottlenecks")
    parser.add_argument("--compare", nargs=2, type=int, metavar=("ID1", "ID2"), help="Compare two profiling runs")
    parser.add_argument("--json", action="store_true", help="Output results in JSON format")

    args = parser.parse_args()

    db = BenchmarkDB(args.db)
    try:
        # 1. Compare runs mode
        if args.compare:
            id_a, id_b = args.compare
            cmp_result = db.compare_runs(id_a, id_b)
            if args.json:
                print(json.dumps(cmp_result, indent=2))
            else:
                print(format_compare_runs_md(cmp_result))
            return 0

        # 2. Determine target run_id
        target_run_id = args.run
        if target_run_id is None:
            latest = db.get_latest_run()
            if latest:
                target_run_id = latest["run_id"]
            else:
                print("No runs found in database.", file=sys.stderr)
                return 1

        # 3. Metrics only mode
        if args.metrics and not args.latest:
            metrics = db.get_metrics(target_run_id, shader_id=args.shader, category=args.category)
            if args.json:
                print(json.dumps(metrics, indent=2))
            else:
                print(format_metrics_md(target_run_id, metrics))
            return 0

        # 4. Bottlenecks only
        if args.bottlenecks and not args.latest:
            bottlenecks = db.get_bottlenecks(target_run_id)
            if args.json:
                print(json.dumps(bottlenecks, indent=2))
            else:
                print(format_bottlenecks_md(target_run_id, bottlenecks))
            return 0

        # 5. Run summary (latest or specified run)
        summary = db.get_run_summary(target_run_id)
        if not summary:
            print(f"Run ID {target_run_id} not found.", file=sys.stderr)
            return 1

        if args.json:
            if args.metrics:
                summary["metrics"] = db.get_metrics(target_run_id, shader_id=args.shader, category=args.category)
            if args.bottlenecks:
                print(json.dumps(summary["bottlenecks"], indent=2))
            else:
                print(json.dumps(summary, indent=2))
        else:
            print(format_run_summary_md(summary))
            if args.bottlenecks:
                print("\n" + format_bottlenecks_md(target_run_id, summary["bottlenecks"]))
            if args.metrics:
                metrics = db.get_metrics(target_run_id, shader_id=args.shader, category=args.category)
                print("\n" + format_metrics_md(target_run_id, metrics))

        return 0
    finally:
        db.close()


if __name__ == "__main__":
    sys.exit(main())
