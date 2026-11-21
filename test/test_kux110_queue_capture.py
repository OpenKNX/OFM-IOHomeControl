import json
import sys
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


def generate_header(path):
    captures = json.loads((Path(__file__).parent / "corpus/kux110-queue-issue230.json").read_text())["captures"]
    lines = ["#pragma once", "#include <cstdint>", "namespace KuxQueueCapture {",
             "struct Frame { uint32_t millis; uint8_t length; uint8_t wire[32]; };"]
    for capture in captures:
        lines.append(f'inline constexpr Frame {capture["name"]}_frames[] = {{')
        start = capture["frames"][0]["log_time_ms"]
        for row in capture["frames"]:
            raw = bytes.fromhex(row["hex"])
            values = ",".join(f"0x{value:02x}" for value in raw)
            lines.append(f'    {{{row["log_time_ms"] - start}, {len(raw)}, {{{values}}}}},')
        lines.append("};")
    lines.append("}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--generate":
        generate_header(Path(sys.argv[2]))
    else:
        unittest.main()
