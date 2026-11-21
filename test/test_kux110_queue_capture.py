import json
from pathlib import Path
import unittest


class Kux110QueueCapture(unittest.TestCase):
    def test_waiting_time_freezes_before_a_separate_execution_countdown(self):
        corpus = json.loads(
            (Path(__file__).parent / "corpus/kux110-queue-issue230.json").read_text()
        )
        countdowns = {"short": [7, 4, 1], "long": [21, 18, 15, 12, 9, 6, 3]}
        self.assertEqual({capture["name"] for capture in corpus["captures"]}, set(countdowns))
        for capture in corpus["captures"]:
            with self.subTest(capture=capture["name"]):
                rows = capture["frames"]
                frames = [bytes.fromhex(row["hex"]) for row in rows]
                self.assertEqual(len({frame[2:8] for frame in frames}), 1)
                self.assertTrue(all(frame[8] == 4 for frame in frames))
                self.assertEqual([frame[9] for frame in frames],
                                 [3] * 10 + [4] * len(countdowns[capture["name"]]) + [5])
                self.assertEqual(len(frames[0]), 15)
                self.assertTrue(all((frame[0] & 31) + 1 == len(frame) for frame in frames))
                self.assertEqual([int.from_bytes(frame[15:17], "big") for frame in frames[1:]],
                                 [5] * 9 + countdowns[capture["name"]] + [0])
                self.assertTrue(all(b["log_time_ms"] > a["log_time_ms"] for a, b in zip(rows, rows[1:])))


if __name__ == "__main__":
    unittest.main()
