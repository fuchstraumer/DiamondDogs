"""Tests for the shader profiler path: the ngfx-mcp record helpers, the shader profile ingest, the views, the
queries and the GPU Trace command line.

The fixture `test_data/shader_profile_sample.json` is the output of `ngfx_mcp.read_shader_profile` on a real
PlanetGenBench trace (100 000 points, RTX 4080), cut to two shaders and three hot lines each. The run that needs
ngfx-mcp itself is not here: it needs Nsight Graphics and a trace file, so it is a manual check (see the README).
"""

import argparse
import contextlib
import io
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import capture_runner
import db_store
import ngfx_mcp
from query_metrics import BenchmarkDB, format_hotspots_md, format_shader_stalls_md

FIXTURE_DIR = SCRIPT_DIR / "test_data" / "gputrace_sample"
PROFILE_FIXTURE = SCRIPT_DIR / "test_data" / "shader_profile_sample.json"
TOTAL_SAMPLES = 33594
UNATTRIBUTED_SAMPLES = 16871
OWNERS_SAMPLES = 7630
NEAREST_SAMPLES = 4029


def load_profile() -> dict:
    """Load the fixture profile. JSON keys are text, so the stall type keys become integers again."""
    profile = json.loads(PROFILE_FIXTURE.read_text(encoding="utf-8"))
    profile["stall_types"] = {int(k): v for k, v in profile["stall_types"].items()}
    return profile


def ingest(conn: sqlite3.Connection, profile: dict) -> int:
    """Ingest the GPU Trace export fixture with the profile. Return the run identifier."""
    with contextlib.redirect_stdout(io.StringIO()):
        return db_store.ingest_gpu_trace(conn, FIXTURE_DIR, None, "--validation=off", None, profile)


class TestHotspotLocation(unittest.TestCase):
    """hotspot_location reads the file, line, text and inline chain of one hotspot record."""

    def test_fields(self):
        hotspot = load_profile()["shaders"][0]["hotspots"][0]
        location = ngfx_mcp.hotspot_location(hotspot)
        self.assertTrue(location["file_name"].endswith("Owners.slang"))
        self.assertEqual(location["line_number"], 57)
        self.assertEqual(location["source_text"].strip(), "if (LoadAlive(flat) == 0u)")
        self.assertEqual(location["function_scope"], "OwnersCS")

    def test_repeated_scopes_are_merged(self):
        hotspot = {"function_scopes": [{"name": "LoadRotation"}, {"name": "BoundReadBuffer.Load"},
                                       {"name": "BoundReadBuffer.Load"}]}
        self.assertEqual(ngfx_mcp.hotspot_location(hotspot)["function_scope"],
                         "LoadRotation > BoundReadBuffer.Load")

    def test_missing_location(self):
        location = ngfx_mcp.hotspot_location({})
        self.assertIsNone(location["file_name"])
        self.assertIsNone(location["function_scope"])


class TestShaderProfileIngest(unittest.TestCase):
    """The shader profile goes into shaders, shader_lines and shader_stalls, in the same run as the export."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.conn = db_store.init_db(Path(self.tmp.name) / "profile.db")
        self.profile = load_profile()
        self.run_id = ingest(self.conn, self.profile)

    def tearDown(self):
        self.conn.close()
        self.tmp.cleanup()

    def test_run_row(self):
        run = self.conn.execute("SELECT * FROM runs WHERE run_id = ?;", (self.run_id,)).fetchone()
        self.assertEqual(run["profiler_samples"], TOTAL_SAMPLES)
        self.assertEqual(run["unattributed_samples"], UNATTRIBUTED_SAMPLES)
        # the export fixture is an older OceanFft trace with the profiler off; the run keeps what it says
        self.assertEqual(run["shader_profiler"], "Disabled")
        self.assertEqual(run["metric_set"], "Async Compute Triage")

    def test_shader_rows(self):
        rows = {r["shader_name"]: r for r in self.conn.execute(
            "SELECT * FROM shaders WHERE run_id = ?;", (self.run_id,))}
        self.assertEqual(set(rows), {"OwnersCS", "NearestFrontPointCS"})
        self.assertEqual(rows["OwnersCS"]["sample_count"], OWNERS_SAMPLES)
        self.assertAlmostEqual(rows["OwnersCS"]["pct_of_samples"], 22.712, places=2)
        self.assertEqual(rows["NearestFrontPointCS"]["sample_count"], NEAREST_SAMPLES)
        self.assertTrue(rows["OwnersCS"]["source_file"].endswith("Owners.slang"))

    def test_shader_stalls_add_up(self):
        for shader in self.profile["shaders"]:
            total = self.conn.execute("""
                SELECT SUM(st.sample_count) FROM shader_stalls st JOIN shaders s ON st.shader_id = s.shader_id
                WHERE s.shader_name = ? AND st.line_id IS NULL;
            """, (shader["name"],)).fetchone()[0]
            self.assertEqual(total, shader["performance"]["sample_count"])

    def test_line_stalls_belong_to_their_line(self):
        rows = self.conn.execute("""
            SELECT stall_short, pct_of_line FROM v_line_stalls
            WHERE shader_name = 'OwnersCS' AND line_number = 57 ORDER BY stall_samples DESC;
        """).fetchall()
        self.assertEqual(rows[0]["stall_short"], "LGSB")
        self.assertAlmostEqual(rows[0]["pct_of_line"], 89.32, places=1)

    def test_shader_view(self):
        top = self.conn.execute("""
            SELECT stall_short, pct_of_shader FROM v_shader_stalls
            WHERE shader_name = 'NearestFrontPointCS' ORDER BY stall_samples DESC LIMIT 1;
        """).fetchone()
        self.assertEqual(top["stall_short"], "WAIT")
        self.assertAlmostEqual(top["pct_of_shader"], 31.9, delta=0.5)

    def test_no_profile_stores_no_shaders(self):
        run_id = ingest(self.conn, None)
        count = self.conn.execute("SELECT COUNT(*) FROM shaders WHERE run_id = ?;", (run_id,)).fetchone()[0]
        self.assertEqual(count, 0)

    def test_shader_links_to_its_dispatch_scope(self):
        # the bench names each dispatch scope after its entry point; the fixture scope "IFFT" stands in for one
        profile = load_profile()
        profile["shaders"][0]["name"] = "IFFT"
        run_id = ingest(self.conn, profile)
        linked = self.conn.execute("""
            SELECT p.pass_name FROM shaders s JOIN passes p ON s.pass_id = p.pass_id
            WHERE s.run_id = ? AND s.shader_name = 'IFFT';
        """, (run_id,)).fetchone()
        self.assertEqual(linked["pass_name"], "Ocean simulation/IFFT")


class TestShaderQueries(unittest.TestCase):
    """The BenchmarkDB queries and their markdown tables."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp.name) / "profile.db"
        conn = db_store.init_db(self.db_path)
        self.run_id = ingest(conn, load_profile())
        conn.close()
        self.db = BenchmarkDB(self.db_path)

    def tearDown(self):
        self.db.close()
        self.tmp.cleanup()

    def test_stalls_are_ordered_by_samples(self):
        records = self.db.get_shader_stalls(self.run_id, None)
        self.assertEqual([r["shader_name"] for r in records], ["OwnersCS", "NearestFrontPointCS"])
        self.assertEqual(records[0]["stalls"][0]["stall_short"], "LGSB")

    def test_shader_filter(self):
        records = self.db.get_shader_stalls(self.run_id, "nearest")
        self.assertEqual([r["shader_name"] for r in records], ["NearestFrontPointCS"])

    def test_hotspots(self):
        lines = self.db.get_hotspot_lines(self.run_id, None, 2)
        self.assertEqual(len(lines), 2)
        self.assertEqual((lines[0]["shader_name"], lines[0]["line_number"]), ("OwnersCS", 57))
        self.assertEqual((lines[1]["shader_name"], lines[1]["line_number"]), ("NearestFrontPointCS", 110))
        table = format_hotspots_md(self.run_id, lines)
        self.assertIn("Owners.slang:57", table)
        self.assertIn("LGSB 89%", table)

    def test_compare_against_itself(self):
        records = self.db.compare_shader_samples(self.run_id, self.run_id)
        self.assertTrue(all(r["pct_a"] == r["pct_b"] for r in records))

    def test_empty_run_says_why(self):
        self.assertIn("shader profiler on", format_shader_stalls_md(99, []))


class TestTraceCommand(unittest.TestCase):
    """build_trace_command turns the profiler on, and starts on submits for a headless app."""

    def make_args(self, start_submits):
        return argparse.Namespace(trace_args="--steps=10", trace_start_submits=start_submits, trace_start_frame=120,
                                  trace_architecture="Ada", trace_metric_set=1, multi_pass_metrics=False)

    def test_submits(self):
        cmd = capture_runner.build_trace_command(Path("ngfx.exe"), Path("app.exe"), Path("out"),
                                                 self.make_args(100))
        self.assertIn("--start-after-submits", cmd)
        self.assertEqual(cmd[cmd.index("--limit-to-submits") + 1], "1")
        self.assertNotIn("--start-after-frames", cmd)

    def test_frames_and_profiler_options(self):
        cmd = capture_runner.build_trace_command(Path("ngfx.exe"), Path("app.exe"), Path("out"),
                                                 self.make_args(None))
        self.assertEqual(cmd[cmd.index("--start-after-frames") + 1], "120")
        # without these three, ngfx records "Real-Time Shader Profiler: Disabled"
        self.assertEqual(cmd[cmd.index("--architecture") + 1], "Ada")
        self.assertEqual(cmd[cmd.index("--metric-set-id") + 1], "1")
        self.assertIn("--real-time-shader-profiler", cmd)
        self.assertNotIn("--multi-pass-metrics", cmd)

    def test_profiler_off_reads_nothing(self):
        # the fixture trace says "Real-Time Shader Profiler: Disabled", so ngfx-mcp is not started
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertIsNone(capture_runner.read_profile_if_enabled(FIXTURE_DIR, 10))
        self.assertIn("Disabled", err.getvalue())


if __name__ == "__main__":
    unittest.main()
