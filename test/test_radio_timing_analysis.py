import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("timing", Path(__file__).resolve().parents[1] / "tools/analyze_radio_timing.py")
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class Analysis(unittest.TestCase):
    def test_wrap_missing_edges_and_drops(self):
        report = timing.analyze([
            "IOHC_TIMING compiled=1 dropped=3",
            "IOHC_TIMING_CSV 4294967280,42,1,1,2,868950000,868950000,2",
            "IOHC_TIMING_CSV 20,42,1,1,10,868950000,868950000,0",
            "IOHC_TIMING_CSV 120,42,1,1,14,868950000,868950000,0",
            "IOHC_TIMING_CSV 200,42,2,2,3,868950000,868950000,2",
            "IOHC_TIMING_CSV 300,42,2,2,14,868950000,868950000,0"])
        self.assertEqual(report["trace_drops"], 3)
        first, second = report["attempts"]
        self.assertEqual(first["isr_to_observer_us"], 36)
        self.assertEqual(first["isr_to_rx_write_us"], 136)
        self.assertIsNone(second["isr_to_rx_write_us"])
        self.assertEqual(second["completion_source"], "irq_poll_only")
        self.assertEqual(second["poll_to_rx_write_us"], 100)
        self.assertNotIn("p95", report["summary"]["isr_to_rx_write_us"])

    def test_separate_capture_segments_and_timestamp_zero(self):
        report = timing.analyze(["IOHC_TIMING compiled=1 dropped=0",
            "IOHC_TIMING_CSV 0,1,1,1,2,1,1,2", "IOHC_TIMING_CSV 50,1,1,1,14,1,1,0",
            "IOHC_TIMING compiled=1 dropped=0", "IOHC_TIMING_CSV 70,1,1,1,14,1,1,0"])
        self.assertEqual(report["attempts"][0]["isr_to_rx_write_us"], 50)
        self.assertIsNone(report["attempts"][1]["isr_to_rx_write_us"])


if __name__ == "__main__":
    unittest.main()
