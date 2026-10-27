"""Database query and reporting CLI for profiling metrics and bottleneck analysis."""

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
    """Format headers and row cells into a clean GitHub markdown table."""
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


class BenchmarkDB:
    """Interface for querying profiler benchmarks, passes, metrics, and bottlenecks."""

    def __init__(self, db_path: Union[str, Path]):
        self.db_path = Path(db_path)
        self.conn = init_db(self.db_path)

    def close(self) -> None:
        """Close database connection."""
        if self.conn:
            self.conn.close()

    def __enter__(self) -> "BenchmarkDB":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def get_latest_run(self) -> Optional[Dict[str, Any]]:
        """Fetch the most recently recorded run record."""
        cursor = self.conn.execute("SELECT * FROM runs ORDER BY run_id DESC LIMIT 1;")
        row = cursor.fetchone()
        return dict(row) if row else None

    def get_run_summary(self, run_id: int) -> Optional[Dict[str, Any]]:
        """Fetch full metadata, pass aggregations, and counts for a given run."""
        cursor = self.conn.execute("SELECT * FROM runs WHERE run_id = ?;", (run_id,))
        run_row = cursor.fetchone()
        if not run_row:
            return None
        run_dict = dict(run_row)

        pass_cursor = self.conn.execute("""
            SELECT pass_name,
                   AVG(duration_ns) as avg_duration_ns,
                   MIN(duration_ns) as min_duration_ns,
                   MAX(duration_ns) as max_duration_ns,
                   COUNT(*) as sample_count
            FROM passes
            WHERE run_id = ?
            GROUP BY pass_name
            ORDER BY avg_duration_ns DESC;
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
        """Retrieve all diagnosed bottlenecks for a given run."""
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
        """Retrieve all recorded metrics for a given run, optionally filtered by shader and category."""
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
        """Compare performance characteristics and passes between two runs."""
        summary_a = self.get_run_summary(run_id_a)
        summary_b = self.get_run_summary(run_id_b)
        if not summary_a:
            raise ValueError(f"Run ID {run_id_a} not found.")
        if not summary_b:
            raise ValueError(f"Run ID {run_id_b} not found.")

        time_a = summary_a.get("avg_frame_time_ms") or 0.0
        time_b = summary_b.get("avg_frame_time_ms") or 0.0
        time_delta = time_b - time_a
        time_pct = (time_delta / time_a * 100.0) if time_a > 0.0 else 0.0

        fps_a = summary_a.get("avg_fps") or 0.0
        fps_b = summary_b.get("avg_fps") or 0.0
        fps_delta = fps_b - fps_a
        fps_pct = (fps_delta / fps_a * 100.0) if fps_a > 0.0 else 0.0

        passes_a = {p["pass_name"]: p for p in summary_a.get("passes", [])}
        passes_b = {p["pass_name"]: p for p in summary_b.get("passes", [])}
        all_pass_names = sorted(set(passes_a.keys()) | set(passes_b.keys()))

        pass_comparisons = []
        for name in all_pass_names:
            pa = passes_a.get(name)
            pb = passes_b.get(name)
            dur_a_us = (pa["avg_duration_ns"] / 1000.0) if pa else None
            dur_b_us = (pb["avg_duration_ns"] / 1000.0) if pb else None
            if dur_a_us is not None and dur_b_us is not None:
                delta = dur_b_us - dur_a_us
                pct = (delta / dur_a_us * 100.0) if dur_a_us > 0.0 else 0.0
            else:
                delta = None
                pct = None
            pass_comparisons.append({
                "pass_name": name,
                "avg_us_a": dur_a_us,
                "avg_us_b": dur_b_us,
                "delta_us": delta,
                "pct_change": pct,
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
    """Format run summary dictionary into GitHub markdown tables."""
    run_headers = ["Property", "Value"]
    run_rows = [
        ["Run ID", str(summary.get("run_id"))],
        ["Timestamp", str(summary.get("timestamp"))],
        ["Binary", str(summary.get("binary_name"))],
        ["Binary Path", str(summary.get("binary_path"))],
        ["Command Args", str(summary.get("command_args") or "(none)")],
        ["Git Commit", str(summary.get("git_commit"))],
        ["GPU", str(summary.get("gpu_name"))],
        ["Driver Version", str(summary.get("driver_version"))],
        ["Frame Count", str(summary.get("frame_count"))],
        ["Avg Frame Time", f"{summary.get('avg_frame_time_ms', 0.0):.4f} ms"],
        ["Avg FPS", f"{summary.get('avg_fps', 0.0):.2f}"],
    ]
    meta_table = to_markdown_table(run_headers, run_rows)

    passes = summary.get("passes", [])
    if passes:
        pass_headers = ["Pass Name", "Avg Duration (µs)", "Min Duration (µs)", "Max Duration (µs)", "Samples"]
        pass_rows = []
        for p in passes:
            pass_rows.append([
                p["pass_name"],
                f"{p['avg_duration_ns'] / 1000.0:.2f}",
                f"{p['min_duration_ns'] / 1000.0:.2f}",
                f"{p['max_duration_ns'] / 1000.0:.2f}",
                str(p["sample_count"]),
            ])
        pass_table = to_markdown_table(pass_headers, pass_rows)
    else:
        pass_table = "_No pass timing records recorded for this run._"

    out = [
        f"### Run Summary (Run {summary.get('run_id')})\n",
        meta_table,
        "\n### Pass Timings\n",
        pass_table,
    ]
    return "\n".join(out)


def format_bottlenecks_md(run_id: int, bottlenecks: List[Dict[str, Any]]) -> str:
    """Format bottlenecks list into a GitHub markdown table."""
    if not bottlenecks:
        return f"### Bottlenecks (Run {run_id})\n\n_No bottlenecks detected for Run {run_id}._"

    headers = ["ID", "Severity", "Category", "Summary", "Detail", "Recommendation"]
    rows = []
    for b in bottlenecks:
        rows.append([
            str(b["bottleneck_id"]),
            b["severity"],
            b["category"],
            b["summary"],
            b["detail"],
            b["recommendation"],
        ])
    table = to_markdown_table(headers, rows)
    return f"### Bottlenecks (Run {run_id})\n\n{table}"


def format_metrics_md(run_id: int, metrics: List[Dict[str, Any]]) -> str:
    """Format metrics list into a GitHub markdown table."""
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
    """Format run comparison dictionary into GitHub markdown tables."""
    run_a = cmp["run_a"]
    run_b = cmp["run_b"]
    id_a = cmp["run_id_a"]
    id_b = cmp["run_id_b"]

    meta_headers = ["Metric", f"Run {id_a}", f"Run {id_b}", "Delta", "% Change"]
    meta_rows = [
        [
            "Avg Frame Time",
            f"{run_a.get('avg_frame_time_ms', 0.0):.4f} ms",
            f"{run_b.get('avg_frame_time_ms', 0.0):.4f} ms",
            f"{cmp['frame_time_delta_ms']:+.4f} ms",
            f"{cmp['frame_time_pct_change']:+.2f}%",
        ],
        [
            "Avg FPS",
            f"{run_a.get('avg_fps', 0.0):.2f}",
            f"{run_b.get('avg_fps', 0.0):.2f}",
            f"{cmp['fps_delta']:+.2f}",
            f"{cmp['fps_pct_change']:+.2f}%",
        ],
    ]
    meta_table = to_markdown_table(meta_headers, meta_rows)

    pass_headers = ["Pass Name", f"Run {id_a} Avg (µs)", f"Run {id_b} Avg (µs)", "Delta (µs)", "% Change"]
    pass_rows = []
    for pc in cmp["pass_comparisons"]:
        a_str = f"{pc['avg_us_a']:.2f}" if pc["avg_us_a"] is not None else "-"
        b_str = f"{pc['avg_us_b']:.2f}" if pc["avg_us_b"] is not None else "-"
        d_str = f"{pc['delta_us']:+.2f}" if pc["delta_us"] is not None else "-"
        pct_str = f"{pc['pct_change']:+.2f}%" if pc["pct_change"] is not None else "-"
        pass_rows.append([pc["pass_name"], a_str, b_str, d_str, pct_str])

    pass_table = to_markdown_table(pass_headers, pass_rows)

    out = [
        f"### Run Comparison: Run {id_a} vs Run {id_b}\n",
        meta_table,
        "\n### Pass Comparison\n",
        pass_table,
    ]
    return "\n".join(out)


def main() -> int:
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
