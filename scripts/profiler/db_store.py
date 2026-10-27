"""Database storage and analysis for automated GPU profiling metrics."""

import sqlite3
from pathlib import Path
from typing import Any, Dict, Optional, Union


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
        conn.execute("DELETE FROM bottlenecks WHERE run_id = ?;", (run_id,))

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
