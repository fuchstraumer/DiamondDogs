"""Tests for the GPU Trace export parser, its ingest, the schema migration and the scope views."""

import contextlib
import io
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_DIR = SCRIPT_DIR.parents[1]
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import db_store
import gpu_trace
from query_metrics import BenchmarkDB, format_scopes_md

FIXTURE_DIR = SCRIPT_DIR / "test_data" / "gputrace_sample"
FIXTURE_BASE = FIXTURE_DIR / "BASE"
IFFT_PATH = "Ocean simulation/IFFT"
DRAM_PCT = "dramc__throughput.avg.pct_of_peak_sustained_elapsed"
SCOPE_COUNT = 10
REGIMES_METRIC_COUNT = 122
FRAME_METRIC_COUNT = 122


def fresh_db(tmp_dir: Path) -> sqlite3.Connection:
    """Open an empty profiler database in the temporary folder."""
    return db_store.init_db(tmp_dir / "trace.db")


class TestGpuTraceParser(unittest.TestCase):
    """The export parser keeps the scope tree, the metrics and the repro information."""

    def setUp(self):
        self.export = gpu_trace.parse_gpu_trace_export(FIXTURE_BASE)
        self.scopes = self.export["scopes"]
        self.by_path = {scope["path"]: scope for scope in self.scopes}

    def test_scope_count_and_order(self):
        self.assertEqual(len(self.scopes), SCOPE_COUNT)
        self.assertEqual(self.scopes[0]["path"], "Ocean simulation")
        self.assertEqual(self.scopes[-1]["path"], "Present or capture")

    def test_nesting_comes_from_indentation(self):
        self.assertEqual(self.by_path["Ocean simulation"]["depth"], 0)
        self.assertEqual(self.by_path[IFFT_PATH]["depth"], 1)
        self.assertEqual(self.by_path["Ocean pass/Surface"]["depth"], 1)

    def test_scope_time_is_gpu_milliseconds(self):
        self.assertAlmostEqual(self.by_path[IFFT_PATH]["time_ms"], 0.300704)

    def test_frame_time_and_repro_info(self):
        self.assertAlmostEqual(self.export["frame_time_ms"], 3.95398)
        self.assertEqual(self.export["repro_info"]["GPU Clocks"], "Locked to Base")
        self.assertEqual(self.export["repro_info"]["Device Name"], "NVIDIA GeForce RTX 4080")
        self.assertEqual(self.export["repro_info"]["Driver Version"], "617.14")

    def test_scope_metrics_are_matched_by_path(self):
        ifft_metrics = {m["metric_name"]: m["value"] for m in self.export["scope_metrics"][IFFT_PATH]}
        self.assertEqual(len(self.export["scope_metrics"][IFFT_PATH]), REGIMES_METRIC_COUNT)
        self.assertAlmostEqual(ifft_metrics[DRAM_PCT], 80.7791, places=3)
        self.assertEqual(self.export["unmatched_scopes"], [])
        self.assertEqual(self.export["skipped_cells"], 0)

    def test_frame_metrics(self):
        frame_metrics = self.export["frame_metrics"]
        self.assertEqual(len(frame_metrics), FRAME_METRIC_COUNT)
        self.assertEqual(frame_metrics[0]["raw_name"], "HOST.TriageAC.gpu__engine_cycles_active_any_syncce"
                                                       ".avg.pct_of_peak_sustained_elapsed")

    def test_normalize_drops_unit_and_set_tokens(self):
        self.assertEqual(
            gpu_trace.normalize_metric_name("SM_A.TriageAC.l1tex__throughput.avg.pct_of_peak_sustained_elapsed"),
            "l1tex__throughput.avg.pct_of_peak_sustained_elapsed",
        )
        self.assertEqual(gpu_trace.normalize_metric_name("gpc__cycles_elapsed.avg"), "gpc__cycles_elapsed.avg")
        self.assertEqual(gpu_trace.normalize_metric_name("no_double_underscore"), "no_double_underscore")


class TestGpuTraceIngest(unittest.TestCase):
    """One export becomes one run, one pass per scope, and linked metrics."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.tmp_dir = Path(self.tmp.name)
        self.conn = fresh_db(self.tmp_dir)
        self.provenance = {
            "git_commit": "0" * 40,
            "git_dirty": 0,
            "git_diff_hash": "abc",
            "exe_hash": "exe",
            "exe_mtime": "2026-10-26T00:00:00+00:00",
            "shader_manifest_hash": None,
            "shader_manifest_mtime": None,
        }

    def tearDown(self):
        self.conn.close()
        self.tmp.cleanup()

    def ingest(self, app_args: str, provenance) -> int:
        """Ingest the fixture into the scratch database and return the run identifier."""
        with contextlib.redirect_stdout(io.StringIO()):
            return db_store.ingest_gpu_trace(self.conn, FIXTURE_DIR, None, app_args, provenance)

    def test_run_row(self):
        run_id = self.ingest("--frames=1500 --validation=off", self.provenance)
        run = dict(self.conn.execute("SELECT * FROM runs WHERE run_id = ?;", (run_id,)).fetchone())
        self.assertEqual(run["mode"], "gputrace")
        self.assertEqual(run["gpu_clocks"], "Locked to Base")
        self.assertEqual(run["gpu_name"], "NVIDIA GeForce RTX 4080")
        self.assertEqual(run["frame_count"], 1)
        self.assertAlmostEqual(run["avg_frame_time_ms"], 3.95398)
        self.assertEqual(run["command_args"], "--frames=1500 --validation=off")
        self.assertIsNone(run["artifact_path"])
        self.assertEqual(run["git_commit"], "0" * 40)
        self.assertEqual(run["exe_hash"], "exe")

    def test_passes_and_metrics_are_linked(self):
        run_id = self.ingest("--frames=1500 --validation=off", self.provenance)
        pass_count = self.conn.execute("SELECT COUNT(*) FROM passes WHERE run_id = ?;", (run_id,)).fetchone()[0]
        self.assertEqual(pass_count, SCOPE_COUNT)

        linked = self.conn.execute(
            "SELECT COUNT(*) FROM metrics WHERE run_id = ? AND pass_id IS NOT NULL;", (run_id,)
        ).fetchone()[0]
        self.assertEqual(linked, SCOPE_COUNT * REGIMES_METRIC_COUNT)

        frame_only = self.conn.execute(
            "SELECT COUNT(*) FROM metrics WHERE run_id = ? AND pass_id IS NULL;", (run_id,)
        ).fetchone()[0]
        self.assertEqual(frame_only, FRAME_METRIC_COUNT)

        ifft = self.conn.execute(
            "SELECT pass_id, duration_ns, queue_family FROM passes WHERE run_id = ? AND pass_name = ?;",
            (run_id, IFFT_PATH),
        ).fetchone()
        self.assertAlmostEqual(ifft["duration_ns"], 300704.0, places=3)
        self.assertEqual(ifft["queue_family"], "Graphics")

        dram = self.conn.execute(
            "SELECT metric_value, pct_of_peak, unit, category, raw_name FROM metrics "
            "WHERE pass_id = ? AND metric_name = ?;",
            (ifft["pass_id"], DRAM_PCT),
        ).fetchone()
        self.assertAlmostEqual(dram["metric_value"], 80.7791, places=3)
        self.assertAlmostEqual(dram["pct_of_peak"], 80.7791, places=3)
        self.assertEqual(dram["unit"], "%")
        self.assertEqual(dram["category"], "dramc")
        self.assertTrue(dram["raw_name"].startswith("FBSP.TriageAC.dramc__throughput"))

    def test_provenance_is_optional(self):
        run_id = self.ingest("", None)
        run = dict(self.conn.execute("SELECT * FROM runs WHERE run_id = ?;", (run_id,)).fetchone())
        self.assertIsNone(run["git_commit"])
        self.assertTrue(run["command_args"].endswith("--frames=1500 --size=1024 --validation=off"))
        self.assertEqual(run["binary_name"], "OceanFft.exe")

    def test_validation_warning(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            db_store.ingest_gpu_trace(self.conn, FIXTURE_DIR, None, "--frames=1500", None)
        self.assertIn("--validation=off", output.getvalue())

    def test_compare_two_traces_without_config(self):
        run_a = self.ingest("--validation=off", self.provenance)
        run_b = self.ingest("--validation=off", self.provenance)
        self.conn.commit()
        with BenchmarkDB(self.tmp_dir / "trace.db") as db:
            comparison = db.compare_runs(run_a, run_b)
        self.assertEqual(len(comparison["pass_comparisons"]), SCOPE_COUNT)
        for pc in comparison["pass_comparisons"]:
            self.assertEqual(pc["config"], "-")
            self.assertAlmostEqual(pc["pct_change"], 0.0)

    def test_scopes_view(self):
        run_id = self.ingest("--validation=off", self.provenance)
        self.conn.commit()
        with BenchmarkDB(self.tmp_dir / "trace.db") as db:
            rows = db.get_scope_rows(run_id)
        self.assertEqual(len(rows), SCOPE_COUNT)
        ifft = next(r for r in rows if r["scope"] == IFFT_PATH)
        self.assertAlmostEqual(ifft["time_ms"], 0.300704)
        self.assertAlmostEqual(ifft[DRAM_PCT], 80.7791, places=3)
        self.assertIn(IFFT_PATH, format_scopes_md(run_id, rows))


class TestSchemaMigration(unittest.TestCase):
    """A version 2 database gains the version 3 columns and keeps its rows."""

    def test_v2_database_migrates(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
            path = Path(tmp) / "v2.db"
            conn = sqlite3.connect(str(path))
            conn.executescript("""
                CREATE TABLE runs (run_id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT,
                                   mode TEXT, gpu_name TEXT, avg_frame_time_ms REAL);
                CREATE TABLE metrics (metric_id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER,
                                      shader_id INTEGER, category TEXT, metric_name TEXT,
                                      metric_value REAL, unit TEXT, pct_of_peak REAL);
                INSERT INTO runs (timestamp, mode, gpu_name, avg_frame_time_ms)
                    VALUES ('2026-10-01T00:00:00+00:00', 'present', 'NVIDIA GeForce RTX 4080', 1.5);
                INSERT INTO metrics (run_id, category, metric_name, metric_value)
                    VALUES (1, 'sm', 'sm__warps_active', 12.5);
                PRAGMA user_version = 2;
            """)
            conn.commit()
            conn.close()

            migrated = db_store.init_db(path)
            version = migrated.execute("PRAGMA user_version;").fetchone()[0]
            self.assertEqual(version, 4)
            self.assertEqual(db_store.SCHEMA_VERSION, 4)

            run_columns = {row["name"] for row in migrated.execute("PRAGMA table_info(runs);")}
            metric_columns = {row["name"] for row in migrated.execute("PRAGMA table_info(metrics);")}
            self.assertIn("gpu_clocks", run_columns)
            self.assertIn("pass_id", metric_columns)
            self.assertIn("raw_name", metric_columns)

            run = migrated.execute("SELECT * FROM runs WHERE run_id = 1;").fetchone()
            self.assertEqual(run["avg_frame_time_ms"], 1.5)
            self.assertIsNone(run["gpu_clocks"])

            metric = migrated.execute("SELECT * FROM metrics WHERE metric_id = 1;").fetchone()
            self.assertEqual(metric["metric_value"], 12.5)
            self.assertIsNone(metric["pass_id"])
            migrated.close()

            reopened = db_store.init_db(path)
            self.assertEqual(reopened.execute("PRAGMA user_version;").fetchone()[0], 4)
            reopened.close()


class TestCommandLineIngest(unittest.TestCase):
    """The --gputrace-dir option ingests a folder without running anything."""

    def test_gputrace_dir_ingests_fixture(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
            db_path = Path(tmp) / "cli.db"
            result = subprocess.run(
                [sys.executable, "-m", "scripts.profiler.capture_runner",
                 "--gputrace-dir", str(FIXTURE_DIR), "--db", str(db_path)],
                cwd=REPO_DIR, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("as run 1", result.stdout)

            conn = sqlite3.connect(str(db_path))
            mode = conn.execute("SELECT mode FROM runs WHERE run_id = 1;").fetchone()[0]
            conn.close()
            self.assertEqual(mode, "gputrace")


if __name__ == "__main__":
    unittest.main()
