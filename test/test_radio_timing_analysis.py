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

    def test_scan_flood_is_not_owned_latency_and_auth_shares_capture(self):
        old = timing.analyze(["IOHC_TIMING compiled=1 dropped=24649",
            "IOHC_TIMING_CSV 332816328,0,0,0,12,0,868250000,2,6208"])
        self.assertEqual(old["capture_status"], "no_owned_tx")
        self.assertEqual(old["unowned_rows"], 1)
        self.assertEqual(old["summary"]["isr_to_rx_write_us"]["status"], "unavailable")
        lines = ["IOHC_TIMING compiled=1 dropped=0 ignored_scan_events=104000 preamble_isr=not_available sync_isr=not_available"]
        for tx in (1, 2):
            for event in (0, 2, 8, 10, 14):
                lines.append(f"IOHC_TIMING_CSV {tx*100+event},42,{tx},1,{event},868950000,868950000,2,0")
        lines.append("IOHC_TIMING_CSV 250,42,2,1,24,868950000,868950000,2,0")
        report = timing.analyze(lines)
        self.assertEqual(report["capture_status"], "complete")
        self.assertEqual(report["captures"][0]["tx_ids"], [1, 2])
        self.assertEqual(report["captures"][0]["outcomes"], ["04_received"])
        self.assertEqual(report["ignored_scan_events"], 104000)
        self.assertEqual(report["attempts"][0]["physical_authorization_or_actuation"], "unknown")

    def test_mismatched_ids_and_manual_stop_remain_partial(self):
        report = timing.analyze(["IOHC_TIMING compiled=1 dropped=0",
            "IOHC_TIMING_CSV 10,42,1,1,0,1,1,2",
            "IOHC_TIMING_CSV 20,99,1,1,24,1,1,2"])
        self.assertTrue(report["id_conflicts"])
        self.assertEqual(report["capture_status"], "incomplete")
        self.assertIsNone(report["attempts"][1]["isr_to_rx_write_us"])


if __name__ == "__main__":
    unittest.main()
