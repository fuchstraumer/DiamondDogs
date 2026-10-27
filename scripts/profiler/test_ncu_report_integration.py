"""Integration and verification test suite for ncu_report ingestion into DiamondDogs profiler database."""

import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

# Ensure profiler module can be imported
SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import db_store
from db_store import (
    HAS_NCU_REPORT,
    init_db,
    ingest_ncu_report,
)
from query_metrics import BenchmarkDB


class TestNCUReportIntegration(unittest.TestCase):
    """Test suite for Nsight Compute report ingestion and metrics querying."""

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.temp_dir.name) / "test_profiler.db"
        self.conn = init_db(self.db_path)

    def tearDown(self):
        self.conn.close()
        self.temp_dir.cleanup()

    def test_01_ncu_report_import(self):
        """1. Verify import ncu_report succeeds."""
        print("\n[Test 1] Verifying ncu_report module availability...")
        import ncu_report
        self.assertTrue(HAS_NCU_REPORT, "HAS_NCU_REPORT must be True")
        self.assertIsNotNone(ncu_report, "ncu_report module must not be None")
        self.assertTrue(hasattr(ncu_report, "load_report"), "ncu_report must expose load_report")
        print("  -> ncu_report successfully imported and verified.")

    def test_02_nonexistent_report_raises_filenotfound(self):
        """2. Verify ingest_ncu_report function exists and handles non-existent report gracefully with FileNotFoundError."""
        print("\n[Test 2] Verifying ingest_ncu_report handles non-existent files with FileNotFoundError...")
        self.assertTrue(callable(ingest_ncu_report), "ingest_ncu_report must be callable")

        non_existent_path = Path(self.temp_dir.name) / "missing_kernel_profile.ncu-rep"
        self.assertFalse(non_existent_path.exists())

        with self.assertRaises(FileNotFoundError) as ctx:
            ingest_ncu_report(self.conn, non_existent_path)

        print(f"  -> Successfully caught FileNotFoundError: {ctx.exception}")

    def test_03_ingest_mock_ncu_report_end_to_end(self):
        """3. Verify end-to-end ingestion of mock NCU report into database."""
        print("\n[Test 3] Verifying end-to-end report ingestion...")
        dummy_file = Path(self.temp_dir.name) / "mock_profile.ncu-rep"
        dummy_file.write_text("DUMMY_NCU_REPORT_BYTES")

        # Construct realistic NCU mock objects
        mock_ctx = MagicMock()
        mock_ctx.num_ranges.return_value = 1

        mock_range = MagicMock()
        mock_range.num_actions.return_value = 1
        mock_ctx.range_by_idx.return_value = mock_range

        mock_action = MagicMock()
        mock_action.name.return_value = "OceanIFFTRadix4_Forward"

        # Duration metric
        mock_duration = MagicMock()
        mock_duration.has_value.return_value = True
        mock_duration.as_double.return_value = 1250000.0  # 1.25 ms
        mock_duration.unit.return_value = "nsecond"
        mock_duration.kind.return_value = getattr(db_store.ncu_report.IMetric, "ValueKind_DOUBLE", 4)

        # Static metrics
        mock_reg = MagicMock()
        mock_reg.has_value.return_value = True
        mock_reg.as_uint64.return_value = 40  # > 32 triggers occupancy warning
        mock_reg.kind.return_value = getattr(db_store.ncu_report.IMetric, "ValueKind_UINT64", 6)

        mock_shmem = MagicMock()
        mock_shmem.has_value.return_value = True
        mock_shmem.as_uint64.return_value = 16384
        mock_shmem.kind.return_value = getattr(db_store.ncu_report.IMetric, "ValueKind_UINT64", 6)

        mock_block_size = MagicMock()
        mock_block_size.has_value.return_value = True
        mock_block_size.as_uint64.return_value = 256
        mock_block_size.kind.return_value = getattr(db_store.ncu_report.IMetric, "ValueKind_UINT64", 6)

        # Profiling metrics
        metric_dict = {
            "gpu__time_duration.sum": mock_duration,
            "launch__registers_per_thread": mock_reg,
            "launch__shared_mem_per_block_allocated": mock_shmem,
            "launch__block_size": mock_block_size,
        }

        # Extra metrics
        mock_stall = MagicMock()
        mock_stall.has_value.return_value = True
        mock_stall.as_double.return_value = 35.5  # > 30% triggers memory latency heuristic
        mock_stall.unit.return_value = "%"
        mock_stall.kind.return_value = getattr(db_store.ncu_report.IMetric, "ValueKind_DOUBLE", 4)
        mock_stall.pct_of_peak.return_value = 35.5
        metric_dict["smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct"] = mock_stall

        mock_throughput = MagicMock()
        mock_throughput.has_value.return_value = True
        mock_throughput.as_double.return_value = 82.4
        mock_throughput.unit.return_value = "%"
        mock_throughput.kind.return_value = getattr(db_store.ncu_report.IMetric, "ValueKind_DOUBLE", 4)
        mock_throughput.pct_of_peak.return_value = 82.4
        metric_dict["sm__throughput.avg.pct_of_peak_sustained_active"] = mock_throughput

        mock_general = MagicMock()
        mock_general.has_value.return_value = True
        mock_general.as_uint64.return_value = 524288
        mock_general.unit.return_value = "byte"
        mock_general.kind.return_value = getattr(db_store.ncu_report.IMetric, "ValueKind_UINT64", 6)
        metric_dict["dram__bytes_read.sum"] = mock_general

        mock_action.metric_by_name.side_effect = lambda name: metric_dict.get(name, None)
        mock_action.metric_names.return_value = list(metric_dict.keys())

        # Rule results
        mock_action.rule_results_as_dicts.return_value = [
            {
                "name": "MemoryWorkloadAnalysis",
                "rule_message": {
                    "title": "High Memory Workload Stall Latency",
                    "message": "Memory access pattern is causing warp stalls waiting on L1TEX cache.",
                },
                "speedup_estimation": {
                    "type": "GLOBAL",
                    "speedup": 14.8,
                },
            }
        ]

        mock_range.action_by_idx.return_value = mock_action

        with patch.object(db_store.ncu_report, "load_report", return_value=mock_ctx):
            run_id = ingest_ncu_report(self.conn, dummy_file)

        self.assertIsInstance(run_id, int)
        self.assertGreater(run_id, 0)
        print(f"  -> Ingested report into run_id={run_id}")

        # Check runs table
        run_row = self.conn.execute("SELECT * FROM runs WHERE run_id = ?", (run_id,)).fetchone()
        self.assertEqual(run_row["binary_name"], "mock_profile.ncu-rep")
        self.assertEqual(run_row["command_args"], "ncu_report import")
        self.assertAlmostEqual(run_row["avg_frame_time_ms"], 1.25, places=2)

        # Check passes table
        pass_row = self.conn.execute("SELECT * FROM passes WHERE run_id = ?", (run_id,)).fetchone()
        self.assertEqual(pass_row["pass_name"], "OceanIFFTRadix4_Forward")
        self.assertEqual(pass_row["duration_ns"], 1250000.0)

        # Check shaders table
        shader_row = self.conn.execute("SELECT * FROM shaders WHERE run_id = ?", (run_id,)).fetchone()
        self.assertEqual(shader_row["shader_name"], "OceanIFFTRadix4_Forward")
        self.assertEqual(shader_row["register_count"], 40)
        self.assertEqual(shader_row["shared_mem_bytes"], 16384)
        self.assertEqual(shader_row["subgroup_size"], 256)

        # Check metrics table
        metrics = self.conn.execute("SELECT * FROM metrics WHERE run_id = ? ORDER BY metric_id ASC", (run_id,)).fetchall()
        self.assertGreaterEqual(len(metrics), 4)

        categories = {m["metric_name"]: m["category"] for m in metrics}
        self.assertEqual(categories["smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct"], "stall")
        self.assertEqual(categories["sm__throughput.avg.pct_of_peak_sustained_active"], "throughput")
        self.assertEqual(categories["dram__bytes_read.sum"], "general")

        # Check bottlenecks table (NCU rule + heuristic analysis)
        bottlenecks = self.conn.execute("SELECT * FROM bottlenecks WHERE run_id = ?", (run_id,)).fetchall()
        rule_bottlenecks = [b for b in bottlenecks if b["category"] == "ncu_rule"]
        self.assertEqual(len(rule_bottlenecks), 1)
        self.assertEqual(rule_bottlenecks[0]["summary"], "High Memory Workload Stall Latency")
        self.assertIn("Estimated speedup: 14.8%", rule_bottlenecks[0]["detail"])

        # Occupancy heuristic triggered (register_count > 32)
        occupancy_bottlenecks = [b for b in bottlenecks if b["category"] == "occupancy"]
        self.assertEqual(len(occupancy_bottlenecks), 1)

        print(f"  -> Ingestion verified: {len(metrics)} metrics and {len(bottlenecks)} bottlenecks stored.")

    def test_04_query_metrics_table(self):
        """4. Verify query_metrics.py --metrics works and generates formatted markdown table."""
        print("\n[Test 4] Verifying query_metrics.py --metrics...")
        # Populate test database with a run and some metrics
        run_id = db_store.insert_run(self.conn, {
            "timestamp": "2026-10-26T20:00:00Z",
            "binary_name": "OceanSimulation",
            "command_args": "--ncu-rep import",
            "gpu_name": "NVIDIA RTX 4090",
            "avg_frame_time_ms": 2.15,
            "avg_fps": 465.1,
        })
        shader_id = db_store.insert_shader(self.conn, {
            "run_id": run_id,
            "stage": "compute",
            "shader_name": "FFTHorizontalKernel",
            "register_count": 32,
        })
        db_store.insert_metric(self.conn, {
            "run_id": run_id,
            "shader_id": shader_id,
            "category": "throughput",
            "metric_name": "sm__throughput.avg.pct_of_peak_sustained_active",
            "metric_value": 78.5,
            "unit": "%",
            "pct_of_peak": 78.5,
        })
        db_store.insert_metric(self.conn, {
            "run_id": run_id,
            "shader_id": shader_id,
            "category": "stall",
            "metric_name": "smsp__warp_issue_stalled_barrier_per_warp_active.pct",
            "metric_value": 12.3,
            "unit": "%",
            "pct_of_peak": 12.3,
        })

        with BenchmarkDB(self.db_path) as db:
            metrics = db.get_metrics(run_id)
            self.assertEqual(len(metrics), 2)
            self.assertEqual(metrics[0]["metric_name"], "sm__throughput.avg.pct_of_peak_sustained_active")
            self.assertEqual(metrics[0]["shader_name"], "FFTHorizontalKernel")

            # Test filter by category
            throughput_metrics = db.get_metrics(run_id, category="throughput")
            self.assertEqual(len(throughput_metrics), 1)

            # Test filter by shader
            shader_metrics = db.get_metrics(run_id, shader_id=shader_id)
            self.assertEqual(len(shader_metrics), 2)

        # Test CLI execution of query_metrics.py --metrics
        cli_path = SCRIPT_DIR / "query_metrics.py"
        cmd = [
            sys.executable,
            str(cli_path),
            "--db", str(self.db_path),
            "--run", str(run_id),
            "--metrics",
        ]
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
        self.assertIn("Detailed Metrics (Run 1)", res.stdout)
        self.assertIn("sm__throughput.avg.pct_of_peak_sustained_active", res.stdout)
        self.assertIn("78.50%", res.stdout)
        self.assertIn("FFTHorizontalKernel", res.stdout)

        # Test CLI --json
        cmd_json = cmd + ["--json"]
        res_json = subprocess.run(cmd_json, capture_output=True, text=True, check=True)
        json_data = json.loads(res_json.stdout)
        self.assertEqual(len(json_data), 2)
        self.assertEqual(json_data[0]["metric_name"], "sm__throughput.avg.pct_of_peak_sustained_active")

        print("  -> query_metrics.py --metrics table and JSON output verified successfully.")

    def test_05_capture_runner_ncu_rep_cli(self):
        """5. Verify capture_runner.py --ncu-rep CLI argument and execution."""
        print("\n[Test 5] Verifying capture_runner.py --ncu-rep CLI...")
        cli_path = SCRIPT_DIR / "capture_runner.py"

        # Missing file should fail gracefully with non-zero exit
        cmd_missing = [
            sys.executable,
            str(cli_path),
            "--db", str(self.db_path),
            "--ncu-rep", str(Path(self.temp_dir.name) / "nonexistent.ncu-rep"),
        ]
        res_missing = subprocess.run(cmd_missing, capture_output=True, text=True)
        self.assertNotEqual(res_missing.returncode, 0)
        self.assertIn("FileNotFoundError", res_missing.stderr)

        print("  -> capture_runner.py --ncu-rep CLI handling verified.")


def main():
    suite = unittest.TestLoader().loadTestsFromTestCase(TestNCUReportIntegration)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
