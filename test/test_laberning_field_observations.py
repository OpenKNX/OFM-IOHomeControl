import json
from pathlib import Path
import unittest


class LaberningFieldObservations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.corpus = json.loads(
            (Path(__file__).parent / "corpus/laberning-field-observations-20261121.json").read_text()
        )

    def test_sunea_missing_final_reply_and_later_success_are_distinct(self):
        rows = self.corpus["sunea"]["frames"]
        commands = []
        for row in rows:
            data = bytes.fromhex(row.get("hex") or row["header_hex"])
            command = data[8]
            commands.append(command)
            self.assertEqual((data[0] & 31) + 1, row["length"])
            if command in (0x3C, 0x3D):
                self.assertNotIn("hex", row)
                self.assertEqual(len(data), 9)
                self.assertEqual(row["masked_bytes"], 6)
            else:
                self.assertEqual(len(data), row["length"])
        self.assertEqual(commands, [3, 0x3C, 0x3D, 3, 3, 0x3C, 0x3D, 3, 0x3C, 0x3D, 4])
        self.assertEqual(rows[-1]["line"], self.corpus["sunea"]["final_response_line"])

    def test_hekla_preserves_ack_and_command_observations_separately(self):
        frames = [bytes.fromhex(row["hex"]) for row in self.corpus["hekla"]["frames"]]
        self.assertEqual([data[8] for data in frames], [0x21, 0x21, 0x20, 0x20, 0x20, 0x20])
        self.assertEqual([data[13] for data in frames[:2]], [1, 0])
        self.assertEqual([int.from_bytes(data[13:], "little") for data in frames[2:]], [100, 1000, 2000, 10000])

    def test_restart_preserves_raw_decrease(self):
        values = [int.from_bytes(bytes.fromhex(raw), "big")
                  for raw in self.corpus["radiator_restart"]["values_hex"]]
        self.assertEqual(values, [3262, 3253])


if __name__ == "__main__":
    unittest.main()
