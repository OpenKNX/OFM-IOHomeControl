#!/usr/bin/env python3
"""Regression checks for the shared OpenKNX channel-selection convention."""

from pathlib import Path
import re
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

    def test_all_channels_are_selected_by_device_type(self) -> None:
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

    def test_every_channel_is_disabled_by_default(self) -> None:
        activity = self.template.find(".//k:Parameter[@Name='c%C%Active']", NS)
        self.assertIsNotNone(activity)
        self.assertEqual(activity.get("Value"), "0")
        device_type = self.template.find(".//k:Parameter[@Name='c%C%DeviceType']", NS)
        self.assertIsNotNone(device_type)
        self.assertEqual(device_type.get("Value"), "1")
        selection = self.template.find(".//k:Parameter[@Name='c%C%ChannelSelection']", NS)
        self.assertIsNotNone(selection)
        self.assertEqual(selection.get("Value"), "0")

        parameter_type = self.share.find(
            ".//k:ParameterType[@Name='IOHCChannelSelection']", NS
        )
        choices = parameter_type.findall(".//k:Enumeration", NS)
        self.assertEqual((choices[0].get("Text"), choices[0].get("Value")), ("Deaktiviert", "0"))
        self.assertEqual(
            {choice.get("Value") for choice in choices},
            {str(value) for value in range(14)},
        )

        overrides = [
            ref for ref in self.share.findall(".//k:ParameterRef", NS)
            if ref.get("RefId", "").endswith("001")
        ]
        self.assertEqual(overrides, [])

    def test_current_flash_layout_is_accepted_by_restore(self) -> None:
        source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        write_flash = source.split("void IoHomecontrol::writeFlash()", 1)[1].split(
            "void IoHomecontrol::readFlash", 1
        )[0]
        self.assertIn("openknx.flash.writeByte(13)", write_flash)

        read_flash = source.split("void IoHomecontrol::readFlash", 1)[1]
        current_layout_branch = read_flash.split("else if", 1)[0]
        self.assertIn("lVersion == 13", current_layout_branch)

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
            ["Kanal", "Gerätetyp", "Beschreibung"],
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
        self.assertEqual(refs[0].get("RefId"), "%AID%_P-%TT%%CC%096_R-%TT%%CC%09601")
        self.assertEqual(refs[1].get("HelpContext"), "BASE-ChannelName")

    def test_device_type_dynamics_follow_visible_selection_directly(self) -> None:
        selection_ref = "%AID%_P-%TT%%CC%096_R-%TT%%CC%09601"
        device_type_ref = "%AID%_UP-%TT%%CC%002_R-%TT%%CC%00201"

        # ETS must not have to propagate a calculated DeviceType change before
        # rebuilding parameter or communication-object visibility.
        self.assertEqual(
            self.template.findall(f".//k:choose[@ParamRefId='{device_type_ref}']", NS),
            [],
        )

        selector_choices = self.template.findall(
            f".//k:choose[@ParamRefId='{selection_ref}']", NS
        )
        ko_choice = next(
            choice
            for choice in selector_choices
            if {when.get("test") for when in choice.findall("k:when", NS)}
            == {str(value) for value in range(1, 14)}
        )

        roller = ko_choice.find("k:when[@test='2']", NS)
        roller_refs = {ref.get("RefId") for ref in roller.findall("k:ComObjectRefRef", NS)}
        self.assertEqual(
            roller_refs,
            {
                "%AID%_O-%TT%%CC%000_R-%TT%%CC%00001",
                "%AID%_O-%TT%%CC%001_R-%TT%%CC%00101",
                "%AID%_O-%TT%%CC%002_R-%TT%%CC%00201",
                "%AID%_O-%TT%%CC%004_R-%TT%%CC%00401",
                "%AID%_O-%TT%%CC%005_R-%TT%%CC%00501",
                "%AID%_O-%TT%%CC%008_R-%TT%%CC%00801",
                "%AID%_O-%TT%%CC%009_R-%TT%%CC%00901",
                "%AID%_O-%TT%%CC%010_R-%TT%%CC%01001",
                "%AID%_O-%TT%%CC%019_R-%TT%%CC%01901",
            },
        )

        light = ko_choice.find("k:when[@test='7']", NS)
        dimmable = light.find("k:choose", NS)
        self.assertIsNotNone(dimmable)
        self.assertEqual(
            {ref.get("RefId") for ref in dimmable.findall("k:when[@test='0']/k:ComObjectRefRef", NS)},
            {
                "%AID%_O-%TT%%CC%003_R-%TT%%CC%00301",
                "%AID%_O-%TT%%CC%006_R-%TT%%CC%00601",
            },
        )

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
            choose.get("ParamRefId"), "%AID%_P-%TT%%CC%096_R-%TT%%CC%09601"
        )
        page = choose.find("k:when/k:ParameterBlock", NS)
        children = list(page)
        self.assertEqual(children[0].get("Text"), "Kanal %C%")
        self.assertEqual(children[1].get("RefId"), "%AID%_P-%TT%%CC%000_R-%TT%%CC%00001")
        self.assertEqual(children[1].get("HelpContext"), "BASE-ChannelName")
        self.assertEqual(children[2].get("RefId"), "%AID%_UP-%TT%%CC%008_R-%TT%%CC%00801")
        self.assertEqual(children[2].get("HelpContext"), "BASE-ChannelSuspended")

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
            choose.get("ParamRefId"), "%AID%_P-%TT%%CC%096_R-%TT%%CC%09601"
        )
        self.assertIsNone(
            overview.find(
                ".//k:choose[@ParamRefId='%AID%_UP-%TT%%CC%008_R-%TT%%CC%00801']",
                NS,
            )
        )
        row = choose.find("k:when[@test='>0']/k:ParameterBlock", NS)
        self.assertIsNotNone(row)
        self.assertEqual(row.find("k:ParameterSeparator", NS).get("Text"), "%C%")

        script = (ROOT / "src" / "IoHomecontrol.script.js").read_text()
        refresh = script.split("function IOHC_refreshAllPairingInfo", 1)[1].split(
            "/**", 1
        )[0]
        self.assertIn('"IOHC_c" + channelIndex + "Active"', refresh)
        self.assertIn("activeChannels.push(channelIndex)", refresh)
        self.assertNotIn("Suspend", refresh)

    def test_key_extraction_import_workflow_is_visible_and_safe(self) -> None:
        commissioning = self.share.find(
            ".//k:ParameterBlock[@Name='CommissioningGlobal']", NS
        )
        self.assertIsNotNone(commissioning)
        button = commissioning.find("k:Button[@EventHandler='IOHC_startKeyExtract']", NS)
        self.assertIsNotNone(button)
        self.assertIn("channelCount", button.get("EventHandlerParameters", ""))
        self.assertEqual(
            button.get("Text"), "2W-Extraktion starten / Ergebnis übernehmen"
        )
        information = commissioning.find(
            "k:ParameterSeparator[@UIHint='Information']", NS
        )
        self.assertIsNotNone(information)
        information_text = information.get("Text", "")
        self.assertLessEqual(len(information_text), 255)
        self.assertIn("Status wird automatisch aktualisiert", information_text)
        self.assertIn("Ergebnis übernehmen", information_text)
        self.assertIn("60 s", information_text)
        self.assertIn("Abschlussprüfung", information.get("Text", ""))

        result_names = {
            parameter.get("Name")
            for parameter in self.share.findall(".//k:Parameter", NS)
            if parameter.get("Name", "").startswith("Extraction")
        }
        self.assertEqual(
            result_names,
            {
                "ExtractionLastResult",
                "ExtractionNodeIds1",
                "ExtractionNodeIds2",
                "ExtractionNodeIds3",
                "ExtractionNodeIds4",
            },
        )

        script = (ROOT / "src" / "IoHomecontrol.script.js").read_text()
        workflow = script.split("function IOHC_startKeyExtract", 1)[1]
        for command in ("[0x17]", "[0x18]", "[0x19, resultIndex]", "[0x1A, discoveries[d].index, targetChannel]", "[0x1B]"):
            self.assertIn(command, workflow)
        self.assertIn('Number(activeParameter.value) == 1', workflow)
        self.assertIn('[0x12, channelIndex]', workflow)
        self.assertIn('IOHC_readNodeId(channelStatus, 1) == 0', workflow)
        self.assertIn('prefix + "Active", 1', script)
        self.assertIn('prefix + "DeviceType", etsType', script)
        self.assertIn("function IOHC_syncChannelSelection", script)
        self.assertIn("neu programmiert werden", workflow)
        self.assertLess(workflow.index("[0x18]"), workflow.index("[0x17]"))
        self.assertNotIn("IOHC_waitMilliseconds", workflow)
        self.assertNotIn("workflowTimeoutMs", workflow)
        self.assertIn("diese Schaltfläche danach erneut drücken", workflow)
        self.assertIn("Schlüssel extrahiert; Prüfung läuft", workflow)
        self.assertIn("Extraktion beendet; Gerätesuche", workflow)
        self.assertIn("authentifizierte Gerätesuche läuft", workflow)

        controller_source = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()
        module_source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        self.assertIn("KeyExtract: armed", controller_source)
        self.assertIn("KeyExtract: hub locked", controller_source)
        self.assertIn("KeyExtract: key captured", controller_source)
        self.assertNotIn("key=%02X", module_source)

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

    def test_two_way_discovery_controls_are_independent_and_default_auto(self) -> None:
        expected_types = {
            "IOHCTwoWayDiscoveryCommand": {
                "0": "Automatisch (0x28)", "1": "Discover 0x28", "2": "Alternativ 0x2E",
                "3": "Authentifiziert SPE 0x2A"
            },
            "IOHCTwoWayDiscoveryDestination": {
                "0": "Automatisch nach Befehl", "1": "0x00003B", "2": "0x00003F",
                "3": "Licht 0x0001BB", "4": "Licht 0x0001BF"
            },
            "IOHCTwoWayDiscoveryListenChannels": {
                "0": "Anfragekanal überspringen", "1": "Alle drei Kanäle"
            },
            "IOHCTwoWayDiscoveryFlag": {
                "0": "Automatisch nach Befehl", "1": "Aus", "2": "Ein"
            },
            "IOHCTwoWayDiscoveryPreamble": {
                "0": "Automatisch nach Befehl", "1": "Lang (1024)",
                "2": "Normal (32)", "3": "Kurz (8)"
            },
        }
        for name, expected in expected_types.items():
            parameter_type = self.share.find(
                f".//k:ParameterType[@Name='{name}']", NS
            )
            self.assertIsNotNone(parameter_type)
            self.assertEqual(
                {item.get("Value"): item.get("Text") for item in parameter_type.findall(".//k:Enumeration", NS)},
                expected,
            )

        parameters = {
            parameter.get("Name"): parameter
            for parameter in self.template.findall(".//k:Parameter", NS)
        }
        for name, offset in {
            "c%C%TwoWayDiscoveryCommand": "54",
            "c%C%TwoWayDiscoveryDestination": "55",
            "c%C%TwoWayDiscoveryAck": "56",
            "c%C%TwoWayDiscoveryLowPower": "57",
            "c%C%TwoWayDiscoveryPreamble": "58",
            "c%C%TwoWayDiscoveryListenChannels": "65",
        }.items():
            self.assertIn(name, parameters)
            self.assertEqual(parameters[name].get("Value"), "0")
            self.assertEqual(parameters[name].get("Offset"), offset)

        protocol_choice = self.template.find(
            ".//k:choose[@ParamRefId='%AID%_UP-%TT%%CC%009_R-%TT%%CC%00901']", NS
        )
        expert = self.template.find(".//k:ParameterBlock[@Name='ExpertSettings']", NS)
        two_way = expert.find("k:choose/k:when[@test='0']", NS)
        shown = {ref.get("RefId") for ref in two_way.findall("k:ParameterRefRef", NS)}
        for suffix in ("086", "087", "088", "089", "090", "100"):
            self.assertIn(f"%AID%_UP-%TT%%CC%{suffix}_R-%TT%%CC%{suffix}01", shown)

    def test_two_way_discovery_confirmation_policy_and_delay(self) -> None:
        mode_type = self.share.find(
            ".//k:ParameterType[@Name='IOHCTwoWayDiscoverConfirmMode']", NS
        )
        self.assertIsNotNone(mode_type)
        labels = {
            item.get("Value"): item.get("Text")
            for item in mode_type.findall(".//k:Enumeration", NS)
        }
        self.assertEqual(labels, {"0": "Überspringen", "1": "Senden", "2": "Senden + ACK"})

        delay_type = self.share.find(
            ".//k:ParameterType[@Name='IOHCTwoWayKeyInitDelay']", NS
        )
        self.assertIsNotNone(delay_type)
        number = delay_type.find("k:TypeNumber", NS)
        self.assertEqual(number.get("SizeInBit"), "16")
        self.assertEqual(number.get("minInclusive"), "0")
        self.assertEqual(number.get("maxInclusive"), "10000")

        mode = self.template.find(".//k:Parameter[@Name='c%C%TwoWayDiscoverConfirmMode']", NS)
        delay = self.template.find(".//k:Parameter[@Name='c%C%TwoWayKeyInitDelay']", NS)
        self.assertEqual(mode.get("Value"), "1")
        self.assertEqual(mode.get("Offset"), "53")
        self.assertEqual(mode.get("BitOffset"), "6")
        self.assertEqual(delay.get("Value"), "300")
        self.assertEqual(delay.get("Offset"), "63")
        self.assertEqual(self.template.find(".//k:Union", NS).get("SizeInBit"), "528")

        expert = self.template.find(".//k:ParameterBlock[@Name='ExpertSettings']", NS)
        two_way = expert.find("k:choose/k:when[@test='0']", NS)
        refs = {ref.get("RefId"): ref for ref in two_way.findall("k:ParameterRefRef", NS)}
        expected = {
            "%AID%_UP-%TT%%CC%098_R-%TT%%CC%09801": "IOHC-2W-Discovery-Bestaetigung",
            "%AID%_UP-%TT%%CC%099_R-%TT%%CC%09901": "IOHC-2W-KeyInit-Verzoegerung",
        }
        for ref_id, help_context in expected.items():
            self.assertIn(ref_id, refs)
            self.assertEqual(refs[ref_id].get("HelpContext"), help_context)

    def test_one_way_wire_profile_overrides_are_persistent_and_default_auto(self) -> None:
        destination = self.share.find(
            ".//k:ParameterType[@Name='IOHCOneWayExecuteDestination']", NS
        )
        self.assertIsNotNone(destination)
        self.assertEqual(
            {item.get("Value"): item.get("Text") for item in destination.findall(".//k:Enumeration", NS)},
            {
                "0": "Automatisch (Typ-Broadcast)",
                "1": "Typ-Broadcast",
                "2": "Alle Geräte (00003F)",
            },
        )

        classes = self.share.find(
            ".//k:ParameterType[@Name='IOHCOneWayEnrollmentClasses']", NS
        )
        self.assertIsNotNone(classes)
        class_values = {item.get("Value") for item in classes.findall(".//k:Enumeration", NS)}
        self.assertEqual(class_values, {str(value) for value in range(9)})

        power_class = self.share.find(
            ".//k:ParameterType[@Name='IOHCOneWayPowerClass']", NS
        )
        self.assertIsNotNone(power_class)
        self.assertEqual(
            {item.get("Value") for item in power_class.findall(".//k:Enumeration", NS)},
            {"0", "1", "2"},
        )

        parameters = {
            parameter.get("Name"): parameter
            for parameter in self.template.findall(".//k:Parameter", NS)
        }
        self.assertEqual(parameters["c%C%OneWayExecuteDestination"].get("Offset"), "59")
        self.assertEqual(parameters["c%C%OneWayEnrollmentClasses"].get("Offset"), "60")
        self.assertEqual(parameters["c%C%OneWayPowerClass"].get("Offset"), "61")
        self.assertEqual(parameters["c%C%OneWayExecuteDestination"].get("Value"), "0")
        self.assertEqual(parameters["c%C%OneWayEnrollmentClasses"].get("Value"), "0")
        self.assertEqual(parameters["c%C%OneWayPowerClass"].get("Value"), "0")

        protocol_choice = self.template.find(
            ".//k:choose[@ParamRefId='%AID%_UP-%TT%%CC%009_R-%TT%%CC%00901']", NS
        )
        one_way = protocol_choice.find("k:when[@test='1']", NS)
        expert = self.template.find(".//k:ParameterBlock[@Name='ExpertSettings']", NS)
        advanced_one_way = expert.find("k:choose/k:when[@test='1']", NS)
        shown = {ref.get("RefId") for ref in advanced_one_way.findall("k:ParameterRefRef", NS)}
        for suffix in ("091", "092"):
            self.assertIn(f"%AID%_UP-%TT%%CC%{suffix}_R-%TT%%CC%{suffix}01", shown)

        own_profile = one_way.find(
            "k:choose[@ParamRefId='%AID%_UP-%TT%%CC%018_R-%TT%%CC%01801']/k:when[@test='0']",
            NS,
        )
        own_profile_refs = {ref.get("RefId") for ref in own_profile.findall("k:ParameterRefRef", NS)}
        self.assertIn("%AID%_UP-%TT%%CC%093_R-%TT%%CC%09301", own_profile_refs)

    def test_two_way_command_profile_is_per_channel_and_defaults_to_somfy(self) -> None:
        profile_type = self.share.find(
            ".//k:ParameterType[@Name='IOHCTwoWayCommandProfile']", NS
        )
        self.assertIsNotNone(profile_type)
        self.assertEqual(
            {item.get("Value"): item.get("Text") for item in profile_type.findall(".//k:Enumeration", NS)},
            {
                "103": "Standard / Somfy (0x67)",
                "99": "Alternative / KIG300-Capture (0x63)",
            },
        )

        parameter = self.template.find(".//k:Parameter[@Name='c%C%TwoWayAcei']", NS)
        self.assertIsNotNone(parameter)
        self.assertEqual(parameter.get("Offset"), "62")
        self.assertEqual(parameter.get("Value"), "103")

        protocol_choice = self.template.find(
            ".//k:choose[@ParamRefId='%AID%_UP-%TT%%CC%009_R-%TT%%CC%00901']", NS
        )
        two_way = protocol_choice.find("k:when[@test='0']", NS)
        self.assertIsNotNone(
            two_way.find(
                "k:ParameterRefRef[@RefId='%AID%_UP-%TT%%CC%097_R-%TT%%CC%09701']",
                NS,
            )
        )

    def test_expert_visibility_does_not_allocate_or_reset_device_configuration(self) -> None:
        param = self.template.find(".//k:Parameter[@Name='c%C%ExpertView']", NS)
        self.assertEqual(param.get("Value"), "0")
        self.assertIsNone(param.find("k:Memory", NS))
        self.assertIsNone(self.template.find(".//k:Union/k:Parameter[@Name='c%C%ExpertView']", NS))
        gate = self.template.find(".//k:choose[@ParamRefId='%AID%_P-%TT%%CC%094_R-%TT%%CC%09401']", NS)
        self.assertIsNotNone(gate.find("k:when[@test='1']/k:ParameterBlock[@Name='ExpertSettings']", NS))
        self.assertEqual(gate.findall('.//k:Assign', NS), [])

    def test_main_page_keeps_suspend_and_groups_configuration(self) -> None:
        page = self.template.find(".//k:ParameterBlock[@Name='IOHCChannel%C%Page']", NS)
        self.assertIsNotNone(page.find("k:ParameterRefRef[@RefId='%AID%_UP-%TT%%CC%008_R-%TT%%CC%00801']", NS))
        for name in ('Functions', 'Scenes', 'Commissioning'):
            self.assertIsNotNone(page.find(f"k:ParameterBlock[@Name='{name}']", NS))
        functions = page.find("k:ParameterBlock[@Name='Functions']", NS)
        self.assertIsNotNone(functions.find("k:choose/k:when[@test='0']/k:ParameterRefRef[@RefId='%AID%_UP-%TT%%CC%003_R-%TT%%CC%00301']", NS))

    def test_global_navigation_separates_overview_and_online_tools(self) -> None:
        channel = self.share.find(".//k:Channel[@Name='IOHC_Global']", NS)
        texts = {block.get('Text') for block in channel.findall('k:ParameterBlock', NS)}
        self.assertTrue({'Übersicht', 'Kanalauswahl', 'Inbetriebnahme', 'Diagnose und Funkmonitor'} <= texts)

    def test_diagnostic_objects_remain_enabled_by_default(self) -> None:
        param = self.template.find(".//k:Parameter[@Name='c%C%DiagnosticObjects']", NS)
        self.assertEqual(param.get('Value'), '1')
        gate = self.template.find(".//k:choose[@ParamRefId='%AID%_P-%TT%%CC%095_R-%TT%%CC%09501']", NS)
        refs = {r.get('RefId') for r in gate.findall('k:when/k:ComObjectRefRef', NS)}
        self.assertEqual(refs, {'%AID%_O-%TT%%CC%012_R-%TT%%CC%01201', '%AID%_O-%TT%%CC%013_R-%TT%%CC%01301'})

    def test_application_help_uses_relative_ko_references_only(self) -> None:
        documentation = (ROOT / "doc" / "Applikationsbeschreibung-IoHomecontrol.md").read_text()
        self.assertRegex(documentation, r"\bKn\+\d+\b")
        self.assertIsNone(re.search(r"\bKO\s*[-#:]?\s*\d+\b", documentation))
        self.assertIsNone(re.search(r"^\|\s*\d+\s*\|", documentation, re.MULTILINE))


if __name__ == "__main__":
    unittest.main()
