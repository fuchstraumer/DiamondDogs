"""Database storage and analysis for automated GPU profiling metrics."""

import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Optional, Union

try:
    import ncu_report
    HAS_NCU_REPORT = True
except ImportError:
    ncu_report = None
    HAS_NCU_REPORT = False



def init_db(db_path: Union[str, Path]) -> sqlite3.Connection:
    """Initialize SQLite database with required tables for profiler runs."""
    path = Path(db_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(path))
    conn.row_factory = sqlite3.Row

    with conn:
        conn.execute("PRAGMA foreign_keys = ON;")
        conn.execute("PRAGMA journal_mode = WAL;")

        conn.execute("""
            CREATE TABLE IF NOT EXISTS runs (
                run_id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT,
                binary_name TEXT,
                binary_path TEXT,
                command_args TEXT,
                git_commit TEXT,
                gpu_name TEXT,
                driver_version TEXT,
                frame_count INTEGER,
                avg_frame_time_ms REAL,
                avg_fps REAL
            );
        """)

        conn.execute("""
            CREATE TABLE IF NOT EXISTS passes (
                pass_id INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id INTEGER,
                pass_name TEXT,
                queue_family TEXT,
                duration_ns REAL,
                iteration INTEGER,
                FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE
            );
        """)

        conn.execute("""
            CREATE TABLE IF NOT EXISTS shaders (
                shader_id INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id INTEGER,
                pass_id INTEGER,
                stage TEXT,
                shader_name TEXT,
                entry_point TEXT,
                register_count INTEGER,
                shared_mem_bytes INTEGER,
                local_mem_bytes INTEGER,
                subgroup_size INTEGER,
                FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE,
                FOREIGN KEY (pass_id) REFERENCES passes(pass_id) ON DELETE SET NULL
            );
        """)

        conn.execute("""
            CREATE TABLE IF NOT EXISTS metrics (
                metric_id INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id INTEGER,
                shader_id INTEGER,
                category TEXT,
                metric_name TEXT,
                metric_value REAL,
                unit TEXT,
                pct_of_peak REAL,
                FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE,
                FOREIGN KEY (shader_id) REFERENCES shaders(shader_id) ON DELETE SET NULL
            );
        """)

        conn.execute("""
            CREATE TABLE IF NOT EXISTS bottlenecks (
                bottleneck_id INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id INTEGER,
                shader_id INTEGER,
                severity TEXT,
                category TEXT,
                summary TEXT,
                detail TEXT,
                recommendation TEXT,
                FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE,
                FOREIGN KEY (shader_id) REFERENCES shaders(shader_id) ON DELETE SET NULL
            );
        """)

    return conn


def insert_run(conn: sqlite3.Connection, run_data: Dict[str, Any]) -> int:
    """Insert a new run entry and return its run_id."""
    query = """
        INSERT INTO runs (
            timestamp, binary_name, binary_path, command_args,
            git_commit, gpu_name, driver_version, frame_count,
            avg_frame_time_ms, avg_fps
        ) VALUES (
            :timestamp, :binary_name, :binary_path, :command_args,
            :git_commit, :gpu_name, :driver_version, :frame_count,
            :avg_frame_time_ms, :avg_fps
        );
    """
    params = {
        "timestamp": run_data.get("timestamp"),
        "binary_name": run_data.get("binary_name"),
        "binary_path": run_data.get("binary_path"),
        "command_args": run_data.get("command_args"),
        "git_commit": run_data.get("git_commit"),
        "gpu_name": run_data.get("gpu_name"),
        "driver_version": run_data.get("driver_version"),
        "frame_count": run_data.get("frame_count"),
        "avg_frame_time_ms": run_data.get("avg_frame_time_ms"),
        "avg_fps": run_data.get("avg_fps"),
    }
    with conn:
        cursor = conn.execute(query, params)
        return int(cursor.lastrowid)


def insert_pass(conn: sqlite3.Connection, pass_data: Dict[str, Any]) -> int:
    """Insert a pass timing record and return its pass_id."""
    query = """
        INSERT INTO passes (
            run_id, pass_name, queue_family, duration_ns, iteration
        ) VALUES (
            :run_id, :pass_name, :queue_family, :duration_ns, :iteration
        );
    """
    params = {
        "run_id": pass_data.get("run_id"),
        "pass_name": pass_data.get("pass_name"),
        "queue_family": pass_data.get("queue_family"),
        "duration_ns": pass_data.get("duration_ns"),
        "iteration": pass_data.get("iteration"),
    }
    with conn:
        cursor = conn.execute(query, params)
        return int(cursor.lastrowid)


def insert_shader(conn: sqlite3.Connection, shader_data: Dict[str, Any]) -> int:
    """Insert shader static analysis metadata and return its shader_id."""
    query = """
        INSERT INTO shaders (
            run_id, pass_id, stage, shader_name, entry_point,
            register_count, shared_mem_bytes, local_mem_bytes, subgroup_size
        ) VALUES (
            :run_id, :pass_id, :stage, :shader_name, :entry_point,
            :register_count, :shared_mem_bytes, :local_mem_bytes, :subgroup_size
        );
    """
    params = {
        "run_id": shader_data.get("run_id"),
        "pass_id": shader_data.get("pass_id"),
        "stage": shader_data.get("stage"),
        "shader_name": shader_data.get("shader_name"),
        "entry_point": shader_data.get("entry_point"),
        "register_count": shader_data.get("register_count"),
        "shared_mem_bytes": shader_data.get("shared_mem_bytes"),
        "local_mem_bytes": shader_data.get("local_mem_bytes"),
        "subgroup_size": shader_data.get("subgroup_size"),
    }
    with conn:
        cursor = conn.execute(query, params)
        return int(cursor.lastrowid)


def insert_metric(conn: sqlite3.Connection, metric_data: Dict[str, Any]) -> int:
    """Insert profiling metric counter and return its metric_id."""
    query = """
        INSERT INTO metrics (
            run_id, shader_id, category, metric_name,
            metric_value, unit, pct_of_peak
        ) VALUES (
            :run_id, :shader_id, :category, :metric_name,
            :metric_value, :unit, :pct_of_peak
        );
    """
    params = {
        "run_id": metric_data.get("run_id"),
        "shader_id": metric_data.get("shader_id"),
        "category": metric_data.get("category"),
        "metric_name": metric_data.get("metric_name"),
        "metric_value": metric_data.get("metric_value"),
        "unit": metric_data.get("unit"),
        "pct_of_peak": metric_data.get("pct_of_peak"),
    }
    with conn:
        cursor = conn.execute(query, params)
        return int(cursor.lastrowid)


def analyze_bottlenecks(conn: sqlite3.Connection, run_id: int) -> None:
    """Analyze profiling data for a given run and insert diagnosed bottlenecks."""
    with conn:
        # Clear previous analysis for this run to keep analysis idempotent
        conn.execute("DELETE FROM bottlenecks WHERE run_id = ? AND category != 'ncu_rule';", (run_id,))

        # Heuristic 1: LDS bank conflicts
        # If shared_mem_bank_conflict_stalls > 15.0 or lds_bank_conflicts > 0:
        cursor = conn.execute("""
            SELECT shader_id, metric_name, metric_value, unit
            FROM metrics
            WHERE run_id = ?
              AND (
                (metric_name = 'shared_mem_bank_conflict_stalls' AND metric_value > 15.0)
                OR (metric_name = 'lds_bank_conflicts' AND metric_value > 0)
              );
        """, (run_id,))
        for row in cursor.fetchall():
            shader_id = row["shader_id"]
            metric_name = row["metric_name"]
            metric_value = row["metric_value"]
            unit = row["unit"] or ""
            detail = f"Metric '{metric_name}' is {metric_value:.1f}{' ' + unit if unit else ''}."
            conn.execute("""
                INSERT INTO bottlenecks (
                    run_id, shader_id, severity, category, summary, detail, recommendation
                ) VALUES (
                    ?, ?, 'critical', 'lds_conflict',
                    'High LDS bank conflicts detected in compute shader',
                    ?,
                    'Pad group shared memory allocations with `index + (index >> 4)` or change AoS to SoA layout'
                );
            """, (run_id, shader_id, detail))

        # Heuristic 2: Long scoreboard stalls
        # If long_scoreboard_stalls > 30.0:
        cursor = conn.execute("""
            SELECT shader_id, metric_name, metric_value, unit
            FROM metrics
            WHERE run_id = ?
              AND metric_name = 'long_scoreboard_stalls'
              AND metric_value > 30.0;
        """, (run_id,))
        for row in cursor.fetchall():
            shader_id = row["shader_id"]
            metric_value = row["metric_value"]
            unit = row["unit"] or "%"
            detail = f"Metric 'long_scoreboard_stalls' is {metric_value:.1f}{unit} (> 30.0%)."
            conn.execute("""
                INSERT INTO bottlenecks (
                    run_id, shader_id, severity, category, summary, detail, recommendation
                ) VALUES (
                    ?, ?, 'critical', 'memory_latency',
                    'Compute warps stalled on global memory fetch latency',
                    ?,
                    'Use 16-bit half precision, cache tiles in shared memory, or increase latency hiding'
                );
            """, (run_id, shader_id, detail))

        # Heuristic 3: Register count
        # If register_count > 32:
        checked_shaders = set()
        cursor = conn.execute("""
            SELECT shader_id, shader_name, register_count
            FROM shaders
            WHERE run_id = ? AND register_count > 32;
        """, (run_id,))
        for row in cursor.fetchall():
            shader_id = row["shader_id"]
            shader_name = row["shader_name"] or f"Shader #{shader_id}"
            reg_count = row["register_count"]
            checked_shaders.add(shader_id)
            detail = f"Shader '{shader_name}' uses {reg_count} registers per thread (> 32)."
            conn.execute("""
                INSERT INTO bottlenecks (
                    run_id, shader_id, severity, category, summary, detail, recommendation
                ) VALUES (
                    ?, ?, 'warning', 'occupancy',
                    'Register count per thread limits active warp occupancy',
                    ?,
                    'Spill temporary variables, decompose large functions, or hoist constants into push constants'
                );
            """, (run_id, shader_id, detail))

        cursor = conn.execute("""
            SELECT shader_id, metric_value
            FROM metrics
            WHERE run_id = ? AND metric_name = 'register_count' AND metric_value > 32;
        """, (run_id,))
        for row in cursor.fetchall():
            shader_id = row["shader_id"]
            if shader_id not in checked_shaders:
                checked_shaders.add(shader_id)
                reg_count = int(row["metric_value"])
                detail = f"Register count is {reg_count} per thread (> 32)."
                conn.execute("""
                    INSERT INTO bottlenecks (
                        run_id, shader_id, severity, category, summary, detail, recommendation
                    ) VALUES (
                        ?, ?, 'warning', 'occupancy',
                        'Register count per thread limits active warp occupancy',
                        ?,
                        'Spill temporary variables, decompose large functions, or hoist constants into push constants'
                    );
                """, (run_id, shader_id, detail))

        # Heuristic 4: Barrier stalls
        # If barrier_stalls > 15.0:
        cursor = conn.execute("""
            SELECT shader_id, metric_name, metric_value, unit
            FROM metrics
            WHERE run_id = ?
              AND metric_name = 'barrier_stalls'
              AND metric_value > 15.0;
        """, (run_id,))
        for row in cursor.fetchall():
            shader_id = row["shader_id"]
            metric_value = row["metric_value"]
            unit = row["unit"] or "%"
            detail = f"Metric 'barrier_stalls' is {metric_value:.1f}{unit} (> 15.0%)."
            conn.execute("""
                INSERT INTO bottlenecks (
                    run_id, shader_id, severity, category, summary, detail, recommendation
                ) VALUES (
                    ?, ?, 'warning', 'sync',
                    'Workgroup barrier latency is high',
                    ?,
                    'Use wave-level shuffle operations (`WaveActiveSum`) to eliminate group sync barriers'
                );
            """, (run_id, shader_id, detail))

        # Heuristic 5: Pass name == 'IFFT' and pass duration exceeds 50% of total frame time
        run_row = conn.execute("SELECT avg_frame_time_ms FROM runs WHERE run_id = ?;", (run_id,)).fetchone()
        avg_frame_time_ns = (run_row["avg_frame_time_ms"] * 1_000_000.0) if run_row and run_row["avg_frame_time_ms"] else None

        cursor = conn.execute("""
            SELECT pass_id, pass_name, duration_ns, iteration
            FROM passes
            WHERE run_id = ? AND pass_name = 'IFFT'
            ORDER BY pass_id ASC;
        """, (run_id,))
        ifft_passes = cursor.fetchall()

        for p in ifft_passes:
            iteration = p["iteration"]
            duration_ns = p["duration_ns"]
            frame_ns = None

            if iteration is not None:
                # First check for Tick pass in the same iteration
                tick_row = conn.execute("""
                    SELECT duration_ns FROM passes
                    WHERE run_id = ? AND iteration = ? AND pass_name = 'Tick';
                """, (run_id, iteration)).fetchone()
                if tick_row and tick_row["duration_ns"] and tick_row["duration_ns"] > 0:
                    frame_ns = tick_row["duration_ns"]
                else:
                    sum_row = conn.execute("""
                        SELECT SUM(duration_ns) as total_ns FROM passes
                        WHERE run_id = ? AND iteration = ?;
                    """, (run_id, iteration)).fetchone()
                    if sum_row and sum_row["total_ns"] and sum_row["total_ns"] > 0:
                        frame_ns = sum_row["total_ns"]

            if frame_ns is None or frame_ns <= 0:
                frame_ns = avg_frame_time_ns

            if frame_ns and frame_ns > 0 and (duration_ns / frame_ns) > 0.5:
                pct = (duration_ns / frame_ns) * 100.0
                it_str = f" in iteration {iteration}" if iteration is not None else ""
                detail = f"Pass 'IFFT'{it_str} duration ({duration_ns / 1_000_000.0:.3f} ms) consumes {pct:.1f}% of total frame time ({frame_ns / 1_000_000.0:.3f} ms)."
                conn.execute("""
                    INSERT INTO bottlenecks (
                        run_id, shader_id, severity, category, summary, detail, recommendation
                    ) VALUES (
                        ?, NULL, 'critical', 'compute_dominant',
                        'IFFT pass consumes dominant portion of frame budget',
                        ?,
                        'Optimize LDS scatter in radix-4 kernel and eliminate redundant butterfly calculation'
                    );
                """, (run_id, detail))


def ingest_ncu_report(
    conn: sqlite3.Connection,
    report_path: Union[str, Path],
    run_id: Optional[int] = None,
) -> int:
    """Ingest NVIDIA Nsight Compute (.ncu-rep) report file into profiler database."""
    if not HAS_NCU_REPORT or ncu_report is None:
        raise RuntimeError("ncu_report module is not available")

    path = Path(report_path)
    if not path.exists():
        raise FileNotFoundError(f"Report file not found: {report_path}")

    ctx = ncu_report.load_report(str(report_path))

    if run_id is None:
        run_data = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "binary_name": path.name,
            "binary_path": str(path.resolve()),
            "command_args": "ncu_report import",
            "git_commit": "",
            "gpu_name": "NVIDIA GPU",
            "driver_version": "",
            "frame_count": 1,
            "avg_frame_time_ms": 0.0,
            "avg_fps": 0.0,
        }
        run_id = insert_run(conn, run_data)

    num_ranges = ctx.num_ranges() if hasattr(ctx, "num_ranges") else len(ctx)
    for range_idx in range(num_ranges):
        rng = ctx.range_by_idx(range_idx)
        num_actions = rng.num_actions() if hasattr(rng, "num_actions") else len(rng)
        for action_idx in range(num_actions):
            action = rng.action_by_idx(action_idx)
            kernel_name = action.name() if hasattr(action, "name") else f"kernel_{action_idx}"

            # Extract duration metric if present
            duration_ns = 0.0
            for dur_candidate in (
                "gpu__time_duration.sum",
                "gpu__time_duration.avg",
                "gpu__time_duration",
                "Duration",
            ):
                try:
                    m_dur = action.metric_by_name(dur_candidate)
                    if m_dur is not None and getattr(m_dur, "has_value", lambda: True)():
                        val = m_dur.as_double()
                        if val > 0:
                            unit = (m_dur.unit() or "").lower()
                            if "nsecond" in unit or "ns" in unit:
                                duration_ns = val
                            elif "usecond" in unit or "us" in unit or "µs" in unit:
                                duration_ns = val * 1000.0
                            elif "msecond" in unit or "ms" in unit:
                                duration_ns = val * 1_000_000.0
                            elif "second" in unit or unit == "s":
                                duration_ns = val * 1_000_000_000.0
                            else:
                                duration_ns = val
                            break
                except Exception:
                    pass

            pass_id = insert_pass(conn, {
                "run_id": run_id,
                "pass_name": kernel_name,
                "queue_family": "Compute",
                "duration_ns": duration_ns,
                "iteration": action_idx,
            })

            # Extract static metrics if present
            register_count = None
            try:
                m_reg = action.metric_by_name("launch__registers_per_thread")
                if m_reg is not None and getattr(m_reg, "has_value", lambda: True)():
                    register_count = int(m_reg.as_uint64())
            except Exception:
                pass

            shared_mem_bytes = None
            try:
                m_shmem = action.metric_by_name("launch__shared_mem_per_block_allocated")
                if m_shmem is not None and getattr(m_shmem, "has_value", lambda: True)():
                    shared_mem_bytes = int(m_shmem.as_uint64())
            except Exception:
                pass

            subgroup_size = None
            for sg_name in ("launch__waves_per_multiprocessor", "launch__block_size"):
                try:
                    m_sg = action.metric_by_name(sg_name)
                    if m_sg is not None and getattr(m_sg, "has_value", lambda: True)():
                        subgroup_size = int(m_sg.as_uint64())
                        break
                except Exception:
                    pass

            shader_id = insert_shader(conn, {
                "run_id": run_id,
                "pass_id": pass_id,
                "stage": "compute",
                "shader_name": kernel_name,
                "entry_point": kernel_name,
                "register_count": register_count,
                "shared_mem_bytes": shared_mem_bytes,
                "local_mem_bytes": None,
                "subgroup_size": subgroup_size,
            })

            # Extract all metrics from action.metric_names()
            metric_names = action.metric_names() if hasattr(action, "metric_names") else []
            for name in metric_names:
                try:
                    m = action.metric_by_name(name)
                    if m is None:
                        continue

                    val = None
                    has_val = m.has_value() if hasattr(m, "has_value") else True
                    if has_val:
                        kind = m.kind() if hasattr(m, "kind") else None
                        valkind_double = getattr(ncu_report.IMetric, "ValueKind_DOUBLE", 4) if ncu_report else 4
                        valkind_float = getattr(ncu_report.IMetric, "ValueKind_FLOAT", 3) if ncu_report else 3
                        valkind_uint32 = getattr(ncu_report.IMetric, "ValueKind_UINT32", 5) if ncu_report else 5
                        valkind_uint64 = getattr(ncu_report.IMetric, "ValueKind_UINT64", 6) if ncu_report else 6
                        valkind_string = getattr(ncu_report.IMetric, "ValueKind_STRING", 2) if ncu_report else 2

                        if kind in (valkind_double, valkind_float):
                            d = m.as_double()
                            val = float(d) if isinstance(d, (int, float)) else None
                        elif kind in (valkind_uint32, valkind_uint64):
                            u = m.as_uint64()
                            val = float(u) if isinstance(u, (int, float)) else None
                        elif kind == valkind_string:
                            s_val = m.as_string()
                            try:
                                val = float(s_val) if s_val is not None else None
                            except (ValueError, TypeError):
                                val = None
                        else:
                            if hasattr(m, "as_double"):
                                try:
                                    d = m.as_double()
                                    if isinstance(d, (int, float)):
                                        val = float(d)
                                except Exception:
                                    pass
                            if val is None and hasattr(m, "as_uint64"):
                                try:
                                    u = m.as_uint64()
                                    if isinstance(u, (int, float)):
                                        val = float(u)
                                except Exception:
                                    pass
                            if val is None and hasattr(m, "as_string"):
                                try:
                                    s = m.as_string()
                                    if isinstance(s, str):
                                        val = float(s)
                                except Exception:
                                    pass

                    name_lower = name.lower()
                    if "stall" in name_lower or "pcsamp" in name_lower:
                        category = "stall"
                    elif "pipe" in name_lower or "l1tex" in name_lower or "sm" in name_lower:
                        category = "throughput"
                    else:
                        category = "general"

                    unit_str = m.unit() if hasattr(m, "unit") else ""

                    pct_val = None
                    if hasattr(m, "pct_of_peak"):
                        try:
                            p = m.pct_of_peak() if callable(m.pct_of_peak) else m.pct_of_peak
                            if isinstance(p, (int, float)):
                                pct_val = float(p)
                        except Exception:
                            pct_val = None
                    elif hasattr(m, "metric_subtype"):
                        try:
                            subtype = m.metric_subtype()
                            pct_subtypes = (
                                getattr(ncu_report.IMetric, "MetricSubtype_PCT_OF_PEAK_SUSTAINED_ACTIVE", 13),
                                getattr(ncu_report.IMetric, "MetricSubtype_PCT_OF_PEAK_SUSTAINED_ELAPSED", 14),
                                getattr(ncu_report.IMetric, "MetricSubtype_PCT", 16),
                            ) if ncu_report else ()
                            if subtype in pct_subtypes:
                                d = m.as_double()
                                if isinstance(d, (int, float)):
                                    pct_val = float(d)
                        except Exception:
                            pct_val = None

                    insert_metric(conn, {
                        "run_id": run_id,
                        "shader_id": shader_id,
                        "category": category,
                        "metric_name": name,
                        "metric_value": val,
                        "unit": unit_str,
                        "pct_of_peak": pct_val,
                    })
                except Exception:
                    pass

            # Extract rule diagnostics from action.rule_results_as_dicts()
            rule_results = action.rule_results_as_dicts() if hasattr(action, "rule_results_as_dicts") else []
            for r in rule_results:
                rule_name = r.get("name") or "NCU Rule"
                rule_msg = r.get("rule_message") or {}
                msg = rule_msg.get("message", "") if isinstance(rule_msg, dict) else str(rule_msg)
                title = rule_msg.get("title", rule_name) if isinstance(rule_msg, dict) else rule_name
                if not title:
                    title = rule_name

                speedup_est = r.get("speedup_estimation")
                if speedup_est and isinstance(speedup_est, dict):
                    sp = speedup_est.get("speedup")
                    if sp is not None:
                        sp_type = speedup_est.get("type")
                        sp_info = f" (Estimated speedup: {sp:.1f}%{f' - {sp_type}' if sp_type else ''})"
                        msg = f"{msg}{sp_info}".strip()

                conn.execute("""
                    INSERT INTO bottlenecks (
                        run_id, shader_id, severity, category, summary, detail, recommendation
                    ) VALUES (
                        ?, ?, 'critical', 'ncu_rule', ?, ?, ?
                    );
                """, (run_id, shader_id, title, msg, title))

    # Update runs avg_frame_time_ms and avg_fps if they are 0
    total_pass_ns = conn.execute(
        "SELECT SUM(duration_ns) FROM passes WHERE run_id = ?;", (run_id,)
    ).fetchone()[0] or 0.0
    if total_pass_ns > 0:
        conn.execute("""
            UPDATE runs
            SET avg_frame_time_ms = ?, avg_fps = ?
            WHERE run_id = ? AND (avg_frame_time_ms IS NULL OR avg_frame_time_ms = 0.0);
        """, (
            total_pass_ns / 1_000_000.0,
            (1_000_000_000.0 / total_pass_ns) if total_pass_ns > 0 else 0.0,
            run_id,
        ))

    analyze_bottlenecks(conn, run_id)
    return run_id

