"""Tests for the benchmark parser, the replay CSV parser, schema migration and code-state records."""

import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import capture_runner
import db_store
from query_metrics import BenchmarkDB

# Output of OceanFftBenchmark.exe, as captured on 2026-10-26. The Vulkan loader messages are on stderr.
BENCHMARK_STDOUT = """\
Device queues setup - Graphics: 2, Compute: 2, Transfer: 2, Sparse: 2
NVIDIA GeForce RTX 4080, timestamp period 1 ns. 256 ticks per configuration after 64 warm-up ticks. Times in microseconds, median / p95
 size   radix  wave |            Tick |   Spectrum tick |            IFFT |         Resolve |            Mips
  128       4   off |    21.5    22.5 |     4.1     4.1 |     6.1     7.2 |     4.1     5.1 |     6.1     7.2
  128       4    32 |    20.5    21.5 |     4.1     4.1 |     6.1     7.2 |     4.1     4.1 |     6.1     7.2
  256       4   off |    35.8    37.9 |     6.1     7.2 |    13.3    14.3 |     6.1     7.2 |     9.2    10.2
  256       4    32 |    34.8    35.8 |     6.1     6.1 |    13.3    14.3 |     6.1     7.2 |     9.2    10.2
  512       4   off |    99.3   101.4 |    15.4    15.4 |    50.2    51.2 |    15.4    15.4 |    18.4    19.5
  512       4    32 |    97.3   298.0 |    15.4    16.4 |    47.1    51.2 |    15.4    15.4 |    18.4    19.5
 1024       4   off |   864.3  1351.7 |   172.0   383.0 |   287.7   587.8 |   219.1   485.4 |   179.2   393.2
 1024       4    32 |   841.7  1316.9 |   162.8   342.0 |   264.2   551.9 |   226.3   491.5 |   178.2   200.7
"""

# Output of OceanFftBenchmark.exe with the path column: each size and wave setting runs fused, then unfused.
# A scope that the path does not record shows a dash.
BENCHMARK_TWO_PATH_STDOUT = """NVIDIA GeForce RTX 4080, timestamp period 1 ns. 256 ticks per configuration after 64 warm-up ticks. Times in microseconds, median / p95
 size   radix  wave     path |            Tick | Tick + IFFT rows | IFFT columns + Resolve |   Spectrum tick |            IFFT |         Resolve |            Mips
  256       4    32    fused |    30.0    31.0 |    14.0    15.0 |    13.0    14.0 |       -       - |       -       - |       -       - |     9.2    10.2
  256       4    32  unfused |    34.8    35.8 |       -       - |       -       - |     6.1     6.1 |    13.3    14.3 |     6.1     7.2 |     9.2    10.2
"""

REPLAY_CSV = (
    "i, replayTotalFps, replayAdjustedFps, msResetTimePerFrame, msResetFinishTimePerFrame, msSubmitTime, "
    "msFinishTime, msFrameTime, msGpuTime, numFrames, mae, maeToleranceOverride\n"
    "0, 100.000, 200.000, 0.500, 5.000, 1.000, 9.000, 10.000, -1.000, 1, -1.00000, 0.00000\n"
    "1, 160.462, 299.401, 0.140, 2.752, 0.327, 3.013, 3.340, -1.000, 1, -1.00000, 0.00000\n"
    "2, 161.000, 300.000, 0.140, 2.700, 0.300, 3.000, 3.300, 0.250, 1, -1.00000, 0.00000\n"
)


class TestBenchmarkParser(unittest.TestCase):
    """The stdout table parser keeps the configuration, the median and the p95."""

    def setUp(self):
        self.parsed = capture_runner.parse_benchmark_output(BENCHMARK_STDOUT)

    def test_header_and_metadata(self):
        self.assertEqual(self.parsed["gpu_name"], "NVIDIA GeForce RTX 4080")
        self.assertEqual(self.parsed["ticks_per_config"], 256)
        self.assertEqual(self.parsed["warmup_ticks"], 64)
        self.assertEqual(self.parsed["config_columns"], ["size", "radix", "wave"])
        self.assertEqual(self.parsed["scopes"], ["Tick", "Spectrum tick", "IFFT", "Resolve", "Mips"])

    def test_rows_keep_configuration(self):
        rows = self.parsed["rows"]
        self.assertEqual(len(rows), 8)
        self.assertEqual([r["config"]["size"] for r in rows], ["128", "128", "256", "256", "512", "512", "1024", "1024"])
        self.assertEqual([r["config"]["wave"] for r in rows], ["off", "32"] * 4)
        self.assertTrue(all(r["config"]["radix"] == "4" for r in rows))

    def test_median_and_p95_are_both_kept(self):
        rows = self.parsed["rows"]
        ifft_1024_wave32 = rows[7]["timings"]["IFFT"]
        self.assertEqual(ifft_1024_wave32, {"median_us": 264.2, "p95_us": 551.9})

        tick_512_wave32 = rows[5]["timings"]["Tick"]
        self.assertEqual(tick_512_wave32, {"median_us": 97.3, "p95_us": 298.0})

    def test_text_before_header_is_ignored(self):
        parsed = capture_runner.parse_benchmark_output("a | b | c\n1 | 2 3 | 4 5\n")
        self.assertEqual(parsed["rows"], [])


class TestReplayCsvParser(unittest.TestCase):
    """The iteration_times.csv parser reads by exact column name."""

    def test_columns_by_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv_path = Path(tmp) / "iteration_times.csv"
            csv_path.write_text(REPLAY_CSV, encoding="utf-8")
            iterations = capture_runner.parse_iteration_times(csv_path)

        self.assertEqual([it["iteration"] for it in iterations], [0, 1, 2])
        self.assertEqual(iterations[1]["submit_ms"], 0.327)
        self.assertEqual(iterations[1]["finish_ms"], 3.013)
        self.assertEqual(iterations[1]["frame_ms"], 3.340)
        self.assertIsNone(iterations[1]["gpu_ms"])
        self.assertEqual(iterations[2]["gpu_ms"], 0.250)

    def test_missing_column_raises(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv_path = Path(tmp) / "iteration_times.csv"
            csv_path.write_text("i, msFrameTime\n0, 1.0\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                capture_runner.parse_iteration_times(csv_path)


OLD_SCHEMA = [
    """CREATE TABLE runs (run_id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT, binary_name TEXT,
       binary_path TEXT, command_args TEXT, git_commit TEXT, gpu_name TEXT, driver_version TEXT,
       frame_count INTEGER, avg_frame_time_ms REAL, avg_fps REAL);""",
    """CREATE TABLE passes (pass_id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER, pass_name TEXT,
       queue_family TEXT, duration_ns REAL, iteration INTEGER);""",
    """CREATE TABLE shaders (shader_id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER, pass_id INTEGER,
       stage TEXT, shader_name TEXT, entry_point TEXT, register_count INTEGER, shared_mem_bytes INTEGER,
       local_mem_bytes INTEGER, subgroup_size INTEGER);""",
    """CREATE TABLE metrics (metric_id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER, shader_id INTEGER,
       category TEXT, metric_name TEXT, metric_value REAL, unit TEXT, pct_of_peak REAL);""",
    """CREATE TABLE bottlenecks (bottleneck_id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER,
       shader_id INTEGER, severity TEXT, category TEXT, summary TEXT, detail TEXT, recommendation TEXT);""",
]


class TestSchemaMigration(unittest.TestCase):
    """An old database gains the new columns and keeps its rows."""

    def test_old_database_is_migrated(self):
        with tempfile.TemporaryDirectory() as tmp:
            db_path = Path(tmp) / "old.db"
            old = sqlite3.connect(str(db_path))
            for statement in OLD_SCHEMA:
                old.execute(statement)
            old.execute("INSERT INTO runs (binary_name, avg_frame_time_ms, avg_fps) VALUES ('old.exe', 2.0, 500.0);")
            old.execute("INSERT INTO passes (run_id, pass_name, duration_ns, iteration) VALUES (1, 'Tick', 2000.0, 0);")
            old.commit()
            self.assertEqual(old.execute("PRAGMA user_version;").fetchone()[0], 0)
            old.close()

            conn = db_store.init_db(db_path)
            try:
                self.assertEqual(conn.execute("PRAGMA user_version;").fetchone()[0], db_store.SCHEMA_VERSION)

                run_columns = {r["name"] for r in conn.execute("PRAGMA table_info(runs);")}
                for name in ("git_dirty", "git_diff_hash", "shader_manifest_hash", "exe_hash", "ticks_per_config"):
                    self.assertIn(name, run_columns)

                pass_columns = {r["name"] for r in conn.execute("PRAGMA table_info(passes);")}
                for name in ("config_size", "config_radix", "config_wave", "config_path", "p95_ns", "cpu_submit_ms"):
                    self.assertIn(name, pass_columns)

                shader_columns = {r["name"] for r in conn.execute("PRAGMA table_info(shaders);")}
                self.assertIn("block_size", shader_columns)

                row = conn.execute("SELECT * FROM runs WHERE run_id = 1;").fetchone()
                self.assertEqual(row["binary_name"], "old.exe")
                self.assertIsNone(row["git_dirty"])
            finally:
                conn.close()

            with BenchmarkDB(db_path) as db:
                summary = db.get_run_summary(1)
                self.assertEqual(summary["passes"][0]["pass_name"], "Tick")
                self.assertIsNone(summary["passes"][0]["config_size"])


def git(repo: Path, *git_args: str) -> None:
    """Run git in a temporary repository."""
    subprocess.run(
        ["git", "-c", "user.name=t", "-c", "user.email=t@example.com", *git_args],
        cwd=repo, check=True, capture_output=True,
    )


class TestCodeState(unittest.TestCase):
    """A run records whether the tree was dirty and which diff it measured."""

    def test_dirty_tree_is_recorded(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            git(repo, "init")
            (repo / "shader.slang").write_text("one\n")
            git(repo, "add", "shader.slang")
            git(repo, "commit", "-m", "first")

            clean = capture_runner.get_code_state(repo)
            self.assertEqual(clean["git_dirty"], 0)

            (repo / "shader.slang").write_text("two\n")
            dirty_a = capture_runner.get_code_state(repo)
            (repo / "shader.slang").write_text("three\n")
            dirty_b = capture_runner.get_code_state(repo)

            self.assertEqual(dirty_a["git_dirty"], 1)
            self.assertEqual(dirty_a["git_commit"], clean["git_commit"])
            self.assertNotEqual(dirty_a["git_diff_hash"], dirty_b["git_diff_hash"])
            self.assertEqual(len(dirty_a["git_diff_hash"]), 16)

            db_path = repo / "out" / "prof.db"
            conn = db_store.init_db(db_path)
            try:
                with conn:
                    run_id = db_store.insert_run(conn, {"binary_name": "x.exe", **dirty_a})
                row = conn.execute("SELECT git_dirty, git_diff_hash FROM runs WHERE run_id = ?;", (run_id,)).fetchone()
                self.assertEqual(row["git_dirty"], 1)
                self.assertEqual(row["git_diff_hash"], dirty_a["git_diff_hash"])
            finally:
                conn.close()


class TestBenchmarkStoreAndCompare(unittest.TestCase):
    """Compare lines up the same configuration and pass in both runs."""

    def test_compare_uses_configuration_and_pass(self):
        parsed = capture_runner.parse_benchmark_output(BENCHMARK_STDOUT)

        with tempfile.TemporaryDirectory() as tmp:
            conn = db_store.init_db(Path(tmp) / "prof.db")
            try:
                for scale in (1.0, 2.0):
                    with conn:
                        run_id = db_store.insert_run(conn, {"binary_name": "b.exe", "mode": "submit"})
                        for row in parsed["rows"]:
                            cfg = row["config"]
                            for scope, t in row["timings"].items():
                                db_store.insert_pass(conn, {
                                    "run_id": run_id,
                                    "pass_name": scope,
                                    "duration_ns": t["median_us"] * 1000.0 * scale,
                                    "p95_ns": t["p95_us"] * 1000.0 * scale,
                                    "config_size": int(cfg["size"]),
                                    "config_radix": int(cfg["radix"]),
                                    "config_wave": cfg["wave"],
                                })
            finally:
                conn.close()

            with BenchmarkDB(Path(tmp) / "prof.db") as db:
                cmp = db.compare_runs(1, 2)

        self.assertIsNone(cmp["frame_time_delta_ms"])
        self.assertEqual(len(cmp["pass_comparisons"]), 8 * 5)

        target = [p for p in cmp["pass_comparisons"]
                  if p["config_size"] == 1024 and p["config_wave"] == "32" and p["pass_name"] == "IFFT"]
        self.assertEqual(len(target), 1)
        self.assertAlmostEqual(target[0]["median_us_a"], 264.2)
        self.assertAlmostEqual(target[0]["median_us_b"], 528.4)
        self.assertAlmostEqual(target[0]["p95_us_b"], 1103.8)


class TestBenchmarkPathColumn(unittest.TestCase):
    """The path column is a configuration column, and it is part of the pass identity."""

    def setUp(self):
        self.parsed = capture_runner.parse_benchmark_output(BENCHMARK_TWO_PATH_STDOUT)

    def test_path_is_a_config_column(self):
        self.assertEqual(self.parsed["config_columns"], ["size", "radix", "wave", "path"])
        self.assertEqual([r["config"]["path"] for r in self.parsed["rows"]], ["fused", "unfused"])

    def test_dash_scopes_are_skipped(self):
        fused, unfused = self.parsed["rows"]
        self.assertEqual(fused["timings"]["Tick + IFFT rows"], {"median_us": 14.0, "p95_us": 15.0})
        self.assertNotIn("IFFT", fused["timings"])
        self.assertNotIn("Tick + IFFT rows", unfused["timings"])
        self.assertEqual(unfused["timings"]["IFFT"], {"median_us": 13.3, "p95_us": 14.3})

    def test_compare_keeps_the_two_paths_apart(self):
        with tempfile.TemporaryDirectory() as tmp:
            conn = db_store.init_db(Path(tmp) / "prof.db")
            try:
                for scale in (1.0, 2.0):
                    with conn:
                        run_id = db_store.insert_run(conn, {"binary_name": "b.exe", "mode": "submit"})
                        for row in self.parsed["rows"]:
                            cfg = row["config"]
                            for scope, t in row["timings"].items():
                                db_store.insert_pass(conn, {
                                    "run_id": run_id,
                                    "pass_name": scope,
                                    "duration_ns": t["median_us"] * 1000.0 * scale,
                                    "p95_ns": t["p95_us"] * 1000.0 * scale,
                                    "config_size": int(cfg["size"]),
                                    "config_radix": int(cfg["radix"]),
                                    "config_wave": cfg["wave"],
                                    "config_path": cfg["path"],
                                })
            finally:
                conn.close()

            with BenchmarkDB(Path(tmp) / "prof.db") as db:
                cmp = db.compare_runs(1, 2)

        tick = [p for p in cmp["pass_comparisons"] if p["pass_name"] == "Tick"]
        self.assertEqual(sorted(p["config_path"] for p in tick), ["fused", "unfused"])
        by_path = {p["config_path"]: p for p in tick}
        self.assertAlmostEqual(by_path["fused"]["median_us_a"], 30.0)
        self.assertAlmostEqual(by_path["unfused"]["median_us_a"], 34.8)
        self.assertTrue(by_path["fused"]["config"].endswith("/fused"))
        self.assertEqual(len(cmp["pass_comparisons"]), 4 + 5)


if __name__ == "__main__":
    unittest.main()
