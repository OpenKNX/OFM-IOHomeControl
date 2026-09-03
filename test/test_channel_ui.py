#!/usr/bin/env python3
"""Regression checks for the shared OpenKNX channel-selection convention."""

from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
KNX = "http://knx.org/xml/project/20"
OP = "http://github.com/OpenKNX/OpenKNXproducer"
NS = {"k": KNX, "op": OP}


def parse(name: str) -> ET.Element:
    return ET.parse(ROOT / "src" / name).getroot()


class ChannelUiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.share = parse("IoHomecontrol.share.xml")
        cls.template = parse("IoHomecontrol.templ.xml")

    def test_all_channels_are_selected_by_activity(self) -> None:
        visible = self.share.find(".//k:Parameter[@Name='VisibleChannels']", NS)
        self.assertIsNotNone(visible)
        self.assertEqual(visible.get("Access"), "None")
        self.assertEqual(visible.get("Value"), "%N%")
        self.assertIsNone(
            self.share.find(
                ".//k:ParameterRef[@RefId='%AID%_P-%TT%00001']", NS
            )
        )
        self.assertFalse(
            any(
                choice.get("ParamRefId") == "%AID%_P-%TT%00001_R-%TT%0000101"
                for choice in self.share.findall(".//k:choose", NS)
            )
        )

        settings = self.template.find(
            ".//k:ChannelIndependentBlock/k:ParameterBlock[@Name='Settings']", NS
        )
        self.assertIsNotNone(settings)
        self.assertIsNone(settings.find("k:choose", NS))

    def test_first_channel_is_enabled_by_default(self) -> None:
        activity = self.template.find(".//k:Parameter[@Name='c%C%Active']", NS)
        self.assertIsNotNone(activity)
        self.assertEqual(activity.get("Value"), "0")
        self.assertIsNone(
            self.template.find(
                ".//k:ParameterRef[@RefId='%AID%_UP-%TT%%CC%001']", NS
            )
        )

        defaults = {}
        for ref in self.share.findall(".//k:ParameterRef", NS):
            ref_id = ref.get("RefId", "")
            if ref_id.startswith("%AID%_UP-%TT%") and ref_id.endswith("001"):
                channel = int(ref_id[len("%AID%_UP-%TT%") : -3])
                defaults[channel] = ref.get("Value")

        self.assertEqual(defaults, {1: "1", **{channel: "0" for channel in range(2, 17)}})

    def test_selection_table_matches_shared_layout(self) -> None:
        selection = self.share.find(
            ".//k:ParameterBlock[@Text='Kanalauswahl']", NS
        )
        self.assertIsNotNone(selection)
        self.assertEqual(selection.get("HelpContext"), "BASE-ChannelSelect")

        header = selection.find("k:ParameterBlock", NS)
        self.assertIsNotNone(header)
        self.assertEqual(
            [column.get("Width") for column in header.findall("k:Columns/k:Column", NS)],
            ["10%", "35%", "55%"],
        )
        self.assertEqual(
            [item.get("Text") for item in header.findall("k:ParameterSeparator", NS)],
            ["Kanal", "Kanalaktivität", "Beschreibung"],
        )

        include = selection.find("op:include", NS)
        self.assertIsNotNone(include)
        self.assertIn("[@Name='Settings']", include.get("xpath"))

        settings = self.template.find(
            ".//k:ChannelIndependentBlock/k:ParameterBlock[@Name='Settings']", NS
        )
        row = settings.find("k:ParameterBlock", NS)
        self.assertEqual(
            [column.get("Width") for column in row.findall("k:Columns/k:Column", NS)],
            ["10%", "35%", "55%"],
        )
        self.assertEqual(row.find("k:ParameterSeparator", NS).get("Text"), "Kanal %C%")
        refs = row.findall("k:ParameterRefRef", NS)
        self.assertEqual(refs[0].get("RefId"), "%AID%_UP-%TT%%CC%001_R-%TT%%CC%00101")
        self.assertEqual(refs[1].get("HelpContext"), "BASE-ChannelName")

    def test_suspension_uses_shared_radio_type_and_header_order(self) -> None:
        suspended = self.template.find(".//k:Parameter[@Name='c%C%Suspend']", NS)
        self.assertIsNotNone(suspended)
        self.assertEqual(suspended.get("ParameterType"), "%AID%_PT-Suspended")
        self.assertIsNone(
            self.share.find(".//k:ParameterType[@Name='IOHCSuspended']", NS)
        )

        channel = self.template.find(
            ".//k:ChannelIndependentBlock/k:ParameterBlock[@Name='Channel']", NS
        )
        self.assertIsNotNone(channel)
        choose = channel.find("k:choose", NS)
        self.assertEqual(
            choose.get("ParamRefId"), "%AID%_UP-%TT%%CC%001_R-%TT%%CC%00101"
        )
        page = choose.find("k:when/k:ParameterBlock", NS)
        children = list(page)
        self.assertEqual(children[0].get("Text"), "Kanal %C%")
        self.assertEqual(children[1].get("RefId"), "%AID%_P-%TT%%CC%000_R-%TT%%CC%00001")
        self.assertEqual(children[1].get("HelpContext"), "BASE-ChannelName")
        self.assertEqual(children[2].get("RefId"), "%AID%_UP-%TT%%CC%002_R-%TT%%CC%00201")
        self.assertEqual(children[3].get("RefId"), "%AID%_UP-%TT%%CC%008_R-%TT%%CC%00801")
        self.assertEqual(children[3].get("HelpContext"), "BASE-ChannelSuspended")

    def test_pairing_overview_only_shows_activated_channels(self) -> None:
        overview_include = next(
            (
                include
                for include in self.share.findall(".//op:include", NS)
                if "PairingOverview" in include.get("xpath", "")
            ),
            None,
        )
        self.assertIsNotNone(overview_include)

        overview = self.template.find(
            ".//k:ChannelIndependentBlock/k:ParameterBlock[@Name='PairingOverview']",
            NS,
        )
        self.assertIsNotNone(overview)
        choose = overview.find("k:choose", NS)
        self.assertEqual(
            choose.get("ParamRefId"), "%AID%_UP-%TT%%CC%001_R-%TT%%CC%00101"
        )
        self.assertIsNone(
            overview.find(
                ".//k:choose[@ParamRefId='%AID%_UP-%TT%%CC%008_R-%TT%%CC%00801']",
                NS,
            )
        )
        row = choose.find("k:when[@test='1']/k:ParameterBlock", NS)
        self.assertIsNotNone(row)
        self.assertEqual(row.find("k:ParameterSeparator", NS).get("Text"), "%C%")

        script = (ROOT / "src" / "IoHomecontrol.script.js").read_text()
        refresh = script.split("function IOHC_refreshAllPairingInfo", 1)[1].split(
            "/**", 1
        )[0]
        self.assertIn('"IOHC_c" + channelIndex + "Active"', refresh)
        self.assertIn("activeChannels.push(channelIndex)", refresh)
        self.assertNotIn("Suspend", refresh)

    def test_one_way_enrollment_finalizer_is_labeled_stop_runter(self) -> None:
        finalizer = self.share.find(
            ".//k:ParameterType[@Name='IOHCOneWayEnrollmentFinalizer']", NS
        )
        self.assertIsNotNone(finalizer)
        labels = {
            item.get("Value"): item.get("Text")
            for item in finalizer.findall(".//k:Enumeration", NS)
        }
        self.assertEqual(
            labels,
            {
                "0": "Automatisch (VELUX: STOP + RUNTER, sonst keiner)",
                "1": "Keiner",
                "2": "STOP + RUNTER",
            },
        )
        self.assertFalse(any("UP" in label or "AUF" in label for label in labels.values()))

    def test_two_way_power_class_override_is_available_only_for_2w(self) -> None:
        power_class = self.share.find(
            ".//k:ParameterType[@Name='IOHCTwoWayPowerClass']", NS
        )
        self.assertIsNotNone(power_class)
        labels = {
            item.get("Value"): item.get("Text")
            for item in power_class.findall(".//k:Enumeration", NS)
        }
        self.assertEqual(
            labels,
            {"0": "Automatisch", "1": "Immer aktiv", "2": "Energiesparend"},
        )

        parameter = self.template.find(
            ".//k:Parameter[@Name='c%C%TwoWayPowerClass']", NS
        )
        self.assertIsNotNone(parameter)
        self.assertEqual(parameter.get("Value"), "0")
        self.assertEqual(parameter.get("Offset"), "53")
        self.assertEqual(parameter.get("BitOffset"), "4")

        protocol_choice = self.template.find(
            ".//k:choose[@ParamRefId='%AID%_UP-%TT%%CC%009_R-%TT%%CC%00901']", NS
        )
        two_way = protocol_choice.find("k:when[@test='0']", NS)
        self.assertIsNotNone(two_way)
        ref = two_way.find(
            "k:ParameterRefRef[@RefId='%AID%_UP-%TT%%CC%085_R-%TT%%CC%08501']", NS
        )
        self.assertIsNotNone(ref)
        self.assertEqual(ref.get("HelpContext"), "IOHC-2W-Energieklasse")


if __name__ == "__main__":
    unittest.main()
