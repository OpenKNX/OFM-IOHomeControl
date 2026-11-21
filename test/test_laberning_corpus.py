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
        self.assertEqual(len(self.loaded),246)
        snapshot = [(e, d) for e, d in self.loaded if e["in_upstream_snapshot"]]
        self.assertEqual(len(snapshot), 239)
        self.assertEqual(sum(len(d["frames"]) for e, d in snapshot), 1606)
        report = corpus.inventory_report(self.manifest)
        self.assertEqual(sum(change["status"] == "added" for change in report["changes"]), 62)
        self.assertTrue(all(change["classification"] == "original_imported" for change in report["changes"] if change["status"] == "added"))
        self.assertEqual(sum(e["reused_existing"] for e,d in self.loaded),16)
        self.assertEqual(sum(len(d["frames"]) for e,d in self.loaded),1616)

    def test_velux_keyed_rollcall_is_five_crc_preserved_receive_only_replies(self):
        _, doc = self.by_id["velux_window_discovery_keyed_rollcall_flipper_cc1101"]
        frames = doc["frames"]
        self.assertEqual(len(frames), 5)
        self.assertEqual({frame["freq"] for frame in frames}, {868250000, 868950000})
        self.assertEqual(len({bytes.fromhex(frame["hex"])[5:8] for frame in frames}), 5)
        for frame in frames:
            self.assertEqual((frame["dir"], frame["crc"]), ("rx", "present"))
            raw = bytes.fromhex(frame["hex"])
            self.assertEqual(raw[2:5], bytes.fromhex("C0 EE 11"))
            self.assertEqual(raw[8:11], bytes.fromhex("2B 01 01"))
            self.assertEqual(raw[11:14], raw[5:8])
            self.assertEqual(raw[14], 1)

    def test_guelma_evidence_has_no_requests_or_calypso_target(self):
        for identity in (
            "sauter_guelma_exchange_register_read",
            "sauter_guelma_exchange_register_write",
            "sauter_guelma_probe_status_fp_high",
        ):
            entry, doc = self.by_id[identity]
            self.assertIn("ca3ffd62ee1408cc15fde6fed3e1a3488dd31c21", entry["source_url"])
            self.assertEqual(doc["key"], "unknown")
            self.assertTrue(all((f["dir"], f["crc"]) == ("rx", "absent") for f in doc["frames"]))
        frames = self.by_id["sauter_guelma_probe_status_fp_high"][1]["frames"]
        high, ordinary = (bytes.fromhex(frame["hex"])[9:] for frame in frames)
        self.assertEqual(high[-4:], b"\xff" * 4)
        self.assertEqual(ordinary[2:4], b"\0" * 2)

    def test_unenrolled_bursts_are_phy_evidence(self):
        import re

        for radio in ("sx1276", "sx1262", "lr1121"):
            for burst, flags in (("low_power", [True, False, False, False]),
                                 ("short", [False] * 4), ("somfy_default", [False] * 4)):
                entry, doc = self.by_id[f"unenrolled_window_opener_oneway_stop_{burst}_burst_{radio}"]
                frames = doc["frames"]
                self.assertEqual(len(frames), 4)
                self.assertTrue(all(f["dir"] == "tx" and f["freq"] == 868950000 for f in frames))
                wire = [bytes.fromhex(f["hex"]) for f in frames]
                self.assertEqual([bool(f[1] & 0x20) for f in wire], flags)
                self.assertEqual(len({f[:1]+f[2:] for f in wire}), 1)
                durations = [float(re.search(r"preamble ([0-9.]+) ms", f["note"])[1]) for f in frames]
                expected = [True] * 4 if burst == "somfy_default" else flags
                self.assertEqual([duration > 100 for duration in durations], expected)
                self.assertEqual(entry["physical_motion_evidence"], "unknown")
                if burst == "somfy_default":
                    self.assertEqual(entry["policy_era"], "historical_pre_ADR0119")

    def test_original_sml_correspondence_keeps_ack_separate_from_queued_polls(self):
        ack = self.by_id["velux_sml_exchange_execute_ack_waiting_for_power_sx1262"][1]
        self.assertTrue(any(bytes.fromhex(f["hex"])[8:10] == bytes([4, 3]) for f in ack["frames"]))
        for author in ("hub", "remote"):
            doc = self.by_id[f"velux_sml_statuspoll_queued_by_{author}_sx1262"][1]
            queued = [bytes.fromhex(f["hex"])[9:] for f in doc["frames"]
                      if bytes.fromhex(f["hex"])[8:10] == bytes([4, 3])]
            self.assertTrue(queued)
            self.assertTrue(any(len(body) >= 8 and body[6:8] == bytes([0, 5]) for body in queued))

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
        seen = "\n".join("100644 blob " + e["git_blob_sha1"] + "\t" + e["upstream_path"] for e in self.manifest["entries"] if e["in_upstream_snapshot"])
        seen += "\n100644 blob " + "a"*40 + "\ttests/corpus/captures/new_capture.yaml\n"
        with patch.object(corpus.subprocess,"check_output",return_value=seen):
            result=corpus.compare_upstream(self.manifest,Path("unused"))
        self.assertEqual(result["new_upstream_files"],["tests/corpus/captures/new_capture.yaml"])
        corpus.check_inventory(self.manifest)

if __name__=="__main__":unittest.main()
