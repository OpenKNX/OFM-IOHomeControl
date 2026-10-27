"""Original-source integrity, security and evidence-boundary regressions."""
from pathlib import Path
import copy
import importlib.util
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("laberning_corpus", ROOT / "tools/laberning_corpus.py")
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)

class OriginalCorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.loaded = corpus.load_captures()
        cls.by_id = {doc["id"]:(entry,doc) for entry,doc in cls.loaded}

    def test_pinned_original_inventory_and_existing_copies(self):
        self.assertEqual(len(self.loaded),177)
        self.assertEqual(sum(e["reused_existing"] for e,d in self.loaded),17)
        self.assertEqual(sum(len(d["frames"]) for e,d in self.loaded),1104)

    def test_missing_inventory_entry_is_not_silent(self):
        manifest = copy.deepcopy(self.manifest)
        manifest["entries"].pop()
        with self.assertRaisesRegex(ValueError,"inventory regressed"):
            corpus.check_inventory(manifest)

    def test_changed_original_bytes_are_rejected(self):
        entry, document = self.loaded[0]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = (ROOT / entry["local_path"]).read_bytes()
            (root / "capture.yaml").write_bytes(original + b"\n")
            mutated = dict(entry, local_path="capture.yaml")
            with self.assertRaisesRegex(ValueError,"hash changed"):
                corpus.read_capture(root,mutated)

    def test_credentials_and_raw_key_fields_are_rejected(self):
        doc = copy.deepcopy(self.loaded[0][1])
        doc["system_key"] = "not-a-real-key"
        with self.assertRaisesRegex(ValueError,"unreviewed capture fields"):
            corpus.validate_document(doc,"synthetic negative")
        doc.pop("system_key")
        doc["key"] = "unreviewed-key"
        with self.assertRaisesRegex(ValueError,"public corpus keys"):
            corpus.validate_document(doc,"synthetic negative")

    def test_recoverable_key_material_must_be_public_rekeyed(self):
        entry, doc = self.by_id["somfy_izymo_dimmer_enrollment_add_controller_with_mac_sx1276"]
        doc = copy.deepcopy(doc)
        raw = bytearray.fromhex(doc["frames"][0]["hex"])
        raw[9] ^= 1
        doc["frames"][0]["hex"] = raw.hex()
        with self.assertRaisesRegex(Exception,"SECURITY"):
            corpus.validate_document(doc,"synthetic corrupted public key wrap")

    def test_upstream_transport_accept_is_not_physical_acceptance(self):
        for identity in ["wisniowski_moto1000_exchange_execute_partial_position_refused","wisniowski_moto1000_exchange_execute_parameter_unknown"]:
            entry, doc = self.by_id[identity]
            self.assertEqual(doc["expect"]["exchange"]["outcome"],"success")
            self.assertEqual(bytes.fromhex(doc["frames"][-1]["hex"])[8:],bytes([0xFE,0x28]))
            self.assertEqual(entry["physical_motion_evidence"],"unknown")

    def test_long_observer_capture_is_bounded_without_invented_subscription(self):
        entry,doc = self.by_id["velux_klf200_statuspoll_roundrobin_868_sx1262"]
        self.assertEqual(len(doc["frames"]),239)
        selected = corpus.sample_indices(doc["frames"])
        self.assertLessEqual(len(selected),32)
        self.assertIn(0,selected);self.assertIn(238,selected)
        self.assertEqual({f["freq"] for f in doc["frames"]},{868250000,868950000,869850000})
        self.assertEqual({bytes.fromhex(f["hex"])[8] for f in doc["frames"]},{3,4})
        self.assertEqual(doc["frames"][-1]["t_ms"],1434052)
        for f in doc["frames"]:
            raw = bytes.fromhex(f["hex"])
            if raw[8]==3:self.assertEqual(raw[9:],bytes.fromhex("01 FE 01 01 01 01 01 01 01 00"))

    def test_foreign_keyed_frames_remain_unknown(self):
        entry,doc = self.by_id["multi_somfy_unsolicited_status_burst"]
        self.assertEqual(doc["key"],"unknown")
        self.assertEqual(len(doc["frames"]),3)
        self.assertEqual({bytes.fromhex(f["hex"])[8] for f in doc["frames"]},{0x71})

    def test_masked_pairing_is_not_completed_by_the_importer(self):
        entry,doc = self.by_id["velux_ssl_pairing_key_exchange_setconfig_unanswered"]
        self.assertEqual([bytes.fromhex(f["hex"])[8] for f in doc["frames"]],[0x31,0x33,0x6F,0x6F,0x6F])
        self.assertEqual(doc["key"],"unknown")
        self.assertEqual(entry["origin"],"github-issue")

    def test_new_upstream_files_are_reported_without_failing_pinned_inventory(self):
        # Comparison is advisory: only an expected local fixture regression fails.
        from unittest.mock import patch
        seen = "\n".join("100644 blob " + e["git_blob_sha1"] + "\t" + e["upstream_path"] for e in self.manifest["entries"])
        seen += "\n100644 blob " + "a"*40 + "\ttests/corpus/captures/new_capture.yaml\n"
        with patch.object(corpus.subprocess,"check_output",return_value=seen):
            result=corpus.compare_upstream(self.manifest,Path("unused"))
        self.assertEqual(result["new_upstream_files"],["tests/corpus/captures/new_capture.yaml"])
        corpus.check_inventory(self.manifest)

if __name__=="__main__":unittest.main()
