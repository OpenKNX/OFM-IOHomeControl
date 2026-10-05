import json
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

    def test_status_keeps_protocol_and_product_identity_separate(self) -> None:
        module_source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        self.assertIn("Protocol: ioAddress=", module_source)
        self.assertIn("Product identification: manufacturerSubType=", module_source)
        self.assertIn("productFamily=%s confidence=%s", module_source)

    def test_manual_metadata_refresh_does_not_repair_or_change_keys(self) -> None:
        source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        refresh = source.split("void IoHomecontrol::processMetadataRefresh()", 1)[1].split(
            "bool IoHomecontrol::startRadioDiagnostic", 1
        )[0]
        for command in ("GetName", "GetGeneralInfo1", "GetGeneralInfo2"):
            self.assertIn(command, refresh)
        self.assertNotIn("setEncryptionKey", refresh)
        self.assertNotIn("startPairing", refresh)
        self.assertIn("ioHomeMetadataRefreshStepIntervalMs", refresh)
        self.assertIn('lSub.rfind("metadata refresh", 0)', source)

    def test_unconfigured_module_cannot_run_controller_or_radio_diagnostics(self) -> None:
        source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        loop = source.split("void IoHomecontrol::loop()", 1)[1].split("void IoHomecontrol::", 1)[0]
        guard = loop.index("if (!knx.configured() || !openknx.afterStartupDelay())")
        early_return = loop.index("return;", guard)
        self.assertLess(early_return, loop.index("mController.loop()"))
        self.assertLess(early_return, loop.index("processRadioDiagnostic()"))

    def test_explicit_product_temperature_console_codec_never_transmits(self) -> None:
        source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        codec = source.split('if (lSub == "codec temp"', 1)[1].split('if (lSub == "fp raw"', 1)[0]
        self.assertIn("ioHomeTemperatureProductByName", codec)
        self.assertIn("ioHomeDecodeProductTemperature", codec)
        self.assertIn("lContext.hasComfort", codec)
        self.assertNotIn("mController.", codec)
        self.assertNotIn("flash.save", codec)

    def test_channel_button_ids_are_unique(self) -> None:
        ids = [button.get("Id") for button in self.template.findall(".//k:Button", NS)]
        self.assertEqual(len(ids), len(set(ids)))

    def test_profile_auto_and_manual_override_are_exposed(self) -> None:
        selection = self.share.find(
            ".//k:ParameterType[@Name='IOHCChannelSelection']", NS
        )
        auto = selection.find(".//k:Enumeration[@Value='1']", NS)
        self.assertEqual(auto.get("Text"), "Automatisch (Discovery)")
        override = self.template.find(
            ".//k:Parameter[@Name='c%C%ProfileOverride']", NS
        )
        self.assertEqual(override.get("Value"), "0")
        self.assertEqual(override.get("Offset"), "66")
        expert = self.template.find(
            ".//k:ParameterBlock[@Name='ExpertSettings']", NS
        )
        self.assertIsNotNone(expert.find(
            "k:ParameterRefRef[@RefId='%AID%_UP-%TT%%CC%101_R-%TT%%CC%10101']", NS
        ))
        script = (ROOT / "src" / "IoHomecontrol.script.js").read_text()
        self.assertIn('IOHC_getParameter(device, prefix + "ProfileOverride")', script)
        self.assertNotIn('prefix + "ProfileOverride", 0', script)
        channel = (ROOT / "src" / "IoHomecontrolChannel.cpp").read_text()
        controller = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()
        self.assertIn("setManualProfileOverride(static_cast<uint16_t>(ParamIOHC_cProfileOverride))", channel)
        self.assertIn("getEffectiveProfileDescriptor()", channel)
        self.assertIn("getEffectiveProfileDescriptor()", controller)
        self.assertIn("mProtocolIdentity = lIdentity", channel)
        selection_ref = "%AID%_P-%TT%%CC%096_R-%TT%%CC%09601"
        ko_choice = next(
            choice for choice in self.template.findall(
                f".//k:choose[@ParamRefId='{selection_ref}']", NS
            ) if choice.find("k:when[@test='15']", NS) is not None
        )
        self.assertIsNone(ko_choice.find("k:when[@test='1']", NS))

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
            {str(value) for value in range(16)},
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
        self.assertIn("openknx.flash.writeByte(18)", write_flash)
        self.assertIn("encodeProtocolIdentity(", write_flash)
        self.assertIn("lMetadata.nodeClass", write_flash)

        read_flash = source.split("void IoHomecontrol::readFlash", 1)[1]
        current_layout_branch = read_flash.split("else if", 1)[0]
        self.assertIn("lVersion == 18", current_layout_branch)
        self.assertIn("kFlashRecordV18 = 58 + IOHC_ENRICHED_FLASH_SIZE", current_layout_branch)
        self.assertIn("kFlashRecordV17 = 58", current_layout_branch)
        self.assertIn("decodeProtocolIdentity(lDiscoveryRaw, lDecodeLen)", current_layout_branch)
        self.assertIn("decodeIoHomeNodeClass(openknx.flash.readByte())", current_layout_branch)
        self.assertIn("lRestoredEnrichment.nameResponse", current_layout_branch)
        self.assertIn("lRestoredEnrichment.generalInfo1", current_layout_branch)
        self.assertIn("lRestoredEnrichment.generalInfo2", current_layout_branch)
        self.assertIn("restoreProductIdentityEvidence", source)
        self.assertIn("decodeProtocolIdentityMib(lState.protocolIdentity, lMib)", read_flash)
        self.assertIn("lState.protocolIdentity.discoveryTimestamp = lDiscoveryTimestamp", read_flash)

    def test_discovery_consumers_share_complete_metadata_model(self) -> None:
        header = (ROOT / "src" / "IoHomecontrol.h").read_text()
        key_import = header.split("struct KeyImportDevice", 1)[1].split("};", 1)[0]
        self.assertIn("IoHomeProtocolIdentity protocolIdentity", key_import)
        self.assertNotIn("uint16_t deviceType", key_import)
        self.assertNotIn("uint8_t subtype", key_import)
        self.assertNotIn("uint8_t manufacturer", key_import)
        self.assertNotIn("uint8_t powerClass", key_import)

        module_source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        discovery_handler = module_source.split(
            "void IoHomecontrol::onDiscoveryResponse", 1
        )[1].split("void IoHomecontrol::processKeyImportWorkflow", 1)[0]
        self.assertIn("const IoHomeProtocolIdentity &iMetadata", discovery_handler)
        self.assertNotIn("decodeProtocolIdentity(", discovery_handler)

        controller_source = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()
        pending_discovery_handler = controller_source.split(
            "void IoHomeController::processPendingDiscoveryResponses()", 1
        )[1].split("void IoHomeController::", 1)[0]
        self.assertIn(
            "captureProtocolIdentity(channelForNode(lPending.source), lPending.frame,",
            pending_discovery_handler,
        )
        self.assertIn(
            "mModule->onDiscoveryResponse(lPending.frame, lMetadata)",
            pending_discovery_handler,
        )
        self.assertIn(
            "rememberProtocolIdentity(iFrame.getSrcNodeId(), lMetadata)",
            controller_source,
        )

        discovery_handler = module_source.split(
            "void IoHomecontrol::onDiscoveryResponse", 1
        )[1].split("void IoHomecontrol::processKeyImportWorkflow", 1)[0]
        self.assertIn("ioHomeShouldAcceptProtocolIdentity", discovery_handler)
        self.assertIn("lDevice->protocolIdentity = iMetadata", discovery_handler)

    def test_protocol_identity_uses_one_structured_persisted_object(self) -> None:
        header = (ROOT / "src" / "IoHomecontrolChannel.h").read_text()
        channel_source = (ROOT / "src" / "IoHomecontrolChannel.cpp").read_text()
        controller_source = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()

        self.assertIn("onProtocolIdentity(uint32_t iIoAddress", header)
        self.assertIn("getProtocolIdentity() const", header)
        self.assertNotIn("onDeviceInfo", header)
        self.assertNotIn("getDeviceMetadata", header)
        self.assertNotIn("getDiscoveryMetadata", header)
        module_source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        write_flash = module_source.split("void IoHomecontrol::writeFlash()", 1)[1].split(
            "void IoHomecontrol::readFlash", 1
        )[0]
        self.assertIn("getProtocolIdentity()", write_flash)
        self.assertIn("encodeProtocolIdentity(", write_flash)
        discovery_handler = channel_source.split(
            "void IoHomecontrolChannel::onProtocolIdentity", 1
        )[1].split("void IoHomecontrolChannel::clearProtocolIdentity", 1)[0]
        self.assertIn("mProtocolIdentity = lIdentity", discovery_handler)
        self.assertIn("lIdentity.ioAddress = mIoAddress", discovery_handler)

        dispatch = controller_source.split(
            "void IoHomeController::dispatchRxFrame()", 1
        )[1]
        info1 = dispatch.split(
            "case IoHomeCommand::GetGeneralInfo1Response", 1
        )[1].split("case IoHomeCommand::GetGeneralInfo2Response", 1)[0]
        info2 = dispatch.split(
            "case IoHomeCommand::GetGeneralInfo2Response", 1
        )[1].split("case IoHomeCommand::GetGeneralInfo3Response", 1)[0]
        self.assertIn("onPostPairEnrichmentResponse", info1)
        self.assertIn("onPostPairEnrichmentResponse", info2)
        self.assertIn("applyGeneralInfo2TiltInfo", info2)
        self.assertNotIn("onDeviceInfo", info1)
        self.assertNotIn("onDeviceInfo", info2)
        self.assertIn("openknx.flash.save()", info1)
        self.assertIn("openknx.flash.save()", info2)

        enrichment = channel_source.split(
            "void IoHomecontrolChannel::onPostPairEnrichmentResponse", 1
        )[1].split("void IoHomecontrolChannel::clearProductIdentityEvidence", 1)[0]
        self.assertIn("GetGeneralInfo1Response", enrichment)
        self.assertIn("GetGeneralInfo2Response", enrichment)
        self.assertIn("iData[10]", enrichment)
        self.assertIn("iData[11]", enrichment)
        self.assertIn("generalInfo2MatchesDiscovery", enrichment)
        self.assertNotIn("onDeviceInfo", enrichment)

    def test_post_pair_enrichment_is_optional_and_precedes_setconfig(self) -> None:
        header = (ROOT / "src" / "controller" / "IoHomeController.h").read_text()
        controller_source = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()

        self.assertIn("PairSendEnrichment", header)
        self.assertIn("PairWaitEnrichment", header)
        self.assertIn("kPairEnrichmentStepTimeoutMs", header)
        finalize = controller_source.split(
            "void IoHomeController::finalize2WPairingKey()", 1
        )[1].split("IoHomeCommand IoHomeController::pairEnrichmentRequest", 1)[0]
        self.assertIn("PairingOutcome::Success", finalize)
        self.assertIn("ControllerState::PairSendEnrichment", finalize)
        sequence = controller_source.split(
            "IoHomeCommand IoHomeController::pairEnrichmentRequest", 1
        )[1].split("IoHomeCommand IoHomeController::pairEnrichmentResponse", 1)[0]
        self.assertLess(sequence.index("IoHomeCommand::GetName"),
                        sequence.index("IoHomeCommand::GetGeneralInfo1"))
        self.assertLess(sequence.index("IoHomeCommand::GetGeneralInfo1"),
                        sequence.index("IoHomeCommand::GetGeneralInfo2"))
        self.assertLess(sequence.index("IoHomeCommand::GetGeneralInfo2"),
                        sequence.index("IoHomeCommand::GetGeneralInfo3"))

    def test_protocol_identity_uses_canonical_somfy_vocabulary(self) -> None:
        commands = (ROOT / "src" / "protocol" / "IoHomeCommands.h").read_text()
        channel_header = (ROOT / "src" / "IoHomecontrolChannel.h").read_text()
        controller_header = (ROOT / "src" / "controller" / "IoHomeController.h").read_text()
        controller = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()

        for field in (
            "ioAddress", "ioBackboneAddress", "profile", "subProfile",
            "manufacturerId", "multiInfoByte", "powerSaveMode",
            "ioMembershipFlag", "rfSupportInNode", "responseTimeClass",
            "keyState",
        ):
            self.assertIn(field, commands)
        self.assertIn("IoHomeKeyState::Unknown", commands)
        self.assertIn("IOHC_DISCOVERY_UNKNOWN_BIT4_MASK", commands)
        self.assertIn("bit4=%u[unknown]", controller)
        self.assertIn("bit5=%u[provisional SyncCtrlGrp candidate]", controller)
        self.assertIn("KLFHint=%ums VelocetHint=%us", controller)
        self.assertIn("getProtocolIdentity", channel_header)
        self.assertIn("protocolIdentityForIoAddress", controller_header)

    def test_identity_capabilities_and_product_evidence_are_separate(self) -> None:
        commands = (ROOT / "src" / "protocol" / "IoHomeCommands.h").read_text()
        identity = commands.split("struct IoHomeProtocolIdentity", 1)[1].split(
            "inline bool ioHomeKeyStateKnown", 1
        )[0]
        capabilities = commands.split("struct IoHomeGenericCapabilities", 1)[1].split(
            "static constexpr uint8_t IOHC_DEVICE_INFO_RAW_MAX_SIZE", 1
        )[0]
        product = commands.split("struct IoHomeProductIdentityEvidence", 1)[1].split(
            "static constexpr uint8_t IOHC_PRODUCT_SIGNATURE_SIZE", 1
        )[0]

        self.assertIn("profile", identity)
        self.assertNotIn("generalInfo1", identity)
        self.assertIn("position", capabilities)
        self.assertNotIn("manufacturerSubType", capabilities)
        self.assertIn("generalInfo1", product)
        self.assertNotIn("profile =", product)
        for legacy_name in (
            "IoHomeDiscoveryMetadata", "IoHomeDeviceMetadata",
            "IoHomePostPairEnrichment", "encodePackedDeviceType",
            "decodePackedDeviceType", "decodePackedDeviceSubtype",
        ):
            self.assertNotIn(legacy_name, commands)

    def test_node_class_is_explicit_and_gi3_remains_raw_only(self) -> None:
        commands = (ROOT / "src" / "protocol" / "IoHomeCommands.h").read_text()
        channel = (ROOT / "src" / "IoHomecontrolChannel.cpp").read_text()
        controller = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()

        node_class = commands.split("enum class IoHomeNodeClass", 1)[1].split("};", 1)[0]
        for value in ("Unknown", "Actuator", "Sensor", "Controller", "Stack", "Beacon"):
            self.assertIn(value, node_class)

        identity = commands.split("struct IoHomeProtocolIdentity", 1)[1].split(
            "inline bool ioHomeKeyStateKnown", 1
        )[0]
        self.assertIn("IoHomeNodeClass nodeClass", identity)
        self.assertIn("IoHomeNodeClass::Unknown", identity)
        self.assertNotIn("profile ==", node_class)

        controls = channel.split("bool IoHomecontrolChannel::allowsActuatorControls", 1)[1].split(
            "void IoHomecontrolChannel::onBatteryLevel", 1
        )[0]
        self.assertIn("IoHomeNodeClass::Unknown", controls)
        self.assertIn("IoHomeNodeClass::Actuator", controls)

        dispatch = controller.split("void IoHomeController::dispatchRxFrame()", 1)[1]
        info3 = dispatch.split("case IoHomeCommand::GetGeneralInfo3Response", 1)[1].split(
            "case IoHomeCommand::Private2Response", 1
        )[0]
        self.assertIn("onPostPairEnrichmentResponse", info3)
        self.assertNotIn("onPositionFeedback", info3)
        self.assertNotIn("onStatusUpdate", info3)

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
        self.assertEqual(refs[1].get("HelpContext"), "IOHC-Beschreibung")

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
            == {str(value) for value in range(2, 16)}
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
                "%AID%_O-%TT%%CC%010_R-%TT%%CC%01001",
                "%AID%_O-%TT%%CC%019_R-%TT%%CC%01901",
            },
        )
        orientation = roller.find(
            "k:choose[@ParamRefId='%AID%_P-%TT%%CC%102_R-%TT%%CC%10201']", NS
        )
        self.assertEqual(
            {ref.get("RefId") for ref in orientation.findall("k:when[@test='1']/k:ComObjectRefRef", NS)},
            {"%AID%_O-%TT%%CC%008_R-%TT%%CC%00801", "%AID%_O-%TT%%CC%009_R-%TT%%CC%00901"},
        )

        light = ko_choice.find("k:when[@test='7']", NS)
        dimmable = light.find(
            "k:choose[@ParamRefId='%AID%_P-%TT%%CC%105_R-%TT%%CC%10501']"
            "/k:when[@test='0']/k:choose", NS
        )
        self.assertIsNotNone(dimmable)
        self.assertEqual(
            {ref.get("RefId") for ref in dimmable.findall("k:when[@test='0']/k:ComObjectRefRef", NS)},
            {
                "%AID%_O-%TT%%CC%003_R-%TT%%CC%00301",
                "%AID%_O-%TT%%CC%006_R-%TT%%CC%00601",
            },
        )
        self.assertNotIn("%AID%_O-%TT%%CC%001_R-%TT%%CC%00101", {
            ref.get("RefId") for ref in dimmable.findall("k:when[@test='1']/k:ComObjectRefRef", NS)
        })
        self.assertEqual(
            {ref.get("RefId") for ref in ko_choice.findall("k:when[@test='9']/k:ComObjectRefRef", NS)},
            {"%AID%_O-%TT%%CC%003_R-%TT%%CC%00301", "%AID%_O-%TT%%CC%007_R-%TT%%CC%00701"},
        )
        self.assertEqual(
            {ref.get("RefId") for ref in ko_choice.findall("k:when[@test='12']/k:ComObjectRefRef", NS)},
            {"%AID%_O-%TT%%CC%000_R-%TT%%CC%00001"},
        )
        self.assertEqual(
            {ref.get("RefId") for ref in ko_choice.findall("k:when[@test='14']/k:ComObjectRefRef", NS)},
            {"%AID%_O-%TT%%CC%000_R-%TT%%CC%00001"},
        )
        self.assertEqual(
            {ref.get("RefId") for ref in ko_choice.findall("k:when[@test='15']/k:ComObjectRefRef", NS)},
            {"%AID%_O-%TT%%CC%003_R-%TT%%CC%00301"},
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
        self.assertEqual(children[1].get("HelpContext"), "IOHC-Beschreibung")
        self.assertEqual(children[2].get("RefId"), "%AID%_UP-%TT%%CC%008_R-%TT%%CC%00801")
        self.assertEqual(children[2].get("HelpContext"), "IOHC-Suspendiert")

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
        self.assertIn("Weiter drücken, Zuordnung prüfen", information_text)
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
        for command in ("[0x17]", "[0x18]", "[0x19, resultIndex]", "[0x1C, channelCount]", "[0x1B]"):
            self.assertIn(command, workflow)
        self.assertIn('Number(activeParameter.value) == 1', workflow)
        self.assertNotIn('[0x12, channelIndex]', workflow)
        self.assertNotIn('[0x1A, discoveries[d].index, targetChannel]', workflow)
        self.assertIn('prefix + "Active", 1', script)
        self.assertIn('["RecognitionTypeAuto", "DeviceType", etsType]', script)
        self.assertIn("function IOHC_syncChannelSelection", script)
        self.assertIn("neu programmiert werden", workflow)
        self.assertLess(workflow.index("[0x18]"), workflow.index("[0x17]"))
        self.assertNotIn("IOHC_waitMilliseconds", workflow)
        self.assertNotIn("workflowTimeoutMs", workflow)
        self.assertIn("diese Schaltfläche danach erneut drücken", workflow)
        self.assertIn("Schlüssel extrahiert; Prüfung läuft", workflow)
        self.assertIn("Node-Verifikation authentifiziert", workflow)
        self.assertIn("Node-Verifikation beantwortet", workflow)
        self.assertIn("Node-Verifikation nicht beobachtet", workflow)
        self.assertIn("Gateway-/System-Node-ID", workflow)
        self.assertIn("temporäre Extraction-Device-ID", workflow)
        self.assertIn("Schlüssel extrahiert; keine Geräte gefunden", workflow)
        self.assertIn("authentifizierte Gerätesuche läuft", workflow)

        controller_source = (ROOT / "src" / "controller" / "IoHomeController.cpp").read_text()
        module_source = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        self.assertIn("KeyExtract: armed", controller_source)
        self.assertIn("KeyExtract: hub locked", controller_source)
        self.assertIn("KeyExtract: key captured", controller_source)
        self.assertIn("KeyImport scan tx:", controller_source)
        self.assertIn("mController.setOwnNodeId(mKeyImportHubNodeId)", module_source)
        self.assertNotIn("mController.setOwnNodeId(mKeyImportExtractionNodeId)", module_source)
        self.assertIn("case 0x1C: // Batch-assign", module_source)
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
        self.assertEqual(self.template.find(".//k:Union", NS).get("SizeInBit"), "544")

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
        enrollment = self.share.find(
            ".//k:ParameterType[@Name='IOHCOneWayEnrollmentDestination']", NS
        )
        self.assertIsNotNone(enrollment)
        self.assertEqual(
            {item.get("Value"): item.get("Text") for item in enrollment.findall(".//k:Enumeration", NS)},
            {
                "0": "Automatisch nach Hersteller",
                "1": "Alle Geräte (00003F)",
                "2": "Typ-Broadcast",
            },
        )
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
        self.assertEqual(parameters["c%C%OneWayEnrollmentDestination"].get("Offset"), "0")
        self.assertEqual(parameters["c%C%OneWayEnrollmentDestination"].get("BitOffset"), "4")
        self.assertEqual(parameters["c%C%OneWayEnrollmentDestination"].get("Value"), "0")
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
        for suffix in ("091", "092", "106"):
            self.assertIn(f"%AID%_UP-%TT%%CC%{suffix}_R-%TT%%CC%{suffix}01", shown)

        channel = (ROOT / "src" / "IoHomecontrolChannel.cpp").read_text()
        self.assertIn("ParamIOHC_cOneWayEnrollmentDestination", channel)
        status = (ROOT / "src" / "IoHomecontrol.cpp").read_text()
        for label in ("executeDst=", "pairingProfile=", "removeDst=", "addDst="):
            self.assertIn(label, status)

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
            ".//k:ParameterBlock[@Name='ExpertSettings']/k:choose[@ParamRefId='%AID%_UP-%TT%%CC%009_R-%TT%%CC%00901']", NS
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
        page = self.template.find(".//k:ParameterBlock[@Name='IOHCChannel%C%Page']", NS)
        expert = page.find("k:ParameterBlock[@Name='ExpertOptions']", NS)
        self.assertEqual(expert.get("Text"), "Expertenoptionen")
        self.assertIsNotNone(expert.find("k:ParameterBlock[@Name='ExpertSettings']", NS))
        self.assertEqual(expert.findall('.//k:Assign', NS), [])
        commissioning = page.find("k:ParameterBlock[@Name='Commissioning']", NS)
        for ref in commissioning.findall('.//k:ParameterRefRef', NS):
            self.assertFalse(any(f"%CC%{number:03d}_R-" in ref.get('RefId') for number in range(110,139)))

    def test_product_functions_are_inside_enabled_channel(self) -> None:
        channel = self.template.find(".//k:ParameterBlock[@Name='Channel']", NS)
        enabled = channel.find("k:choose/k:when[@test='>0']", NS)
        page = enabled.find("k:ParameterBlock[@Name='IOHCChannel%C%Page']", NS)
        products = page.find("k:ParameterBlock[@Name='ProductFunctions']", NS)
        self.assertIsNotNone(products.find("op:usePart[@name='IOHCProducts']", NS))
        wrapper = parse('IoHomeProduct.dynamic.part.xml')
        includes = wrapper.findall('.//op:include', NS)
        self.assertEqual([i.get('prefix') for i in includes], ['PIC', 'PVX'])
        self.assertTrue(all(i.get('IsInner') == 'true' and i.get('type') is None for i in includes))
        part = self.template.find(".//op:part[@name='IOHCProducts']", NS)
        self.assertEqual(part.find("op:param[@name='PRODUCT_CC']", NS).get('value'), '%CC%')
        count = 0
        for name in ('IoHomeProductPIC.ui.xml','IoHomeProductPVX.ui.xml'):
            fragment = parse(name)
            refs = fragment.findall('.//k:ComObjectRefRef', NS)
            count += len(refs)
            self.assertTrue(all('%TT%%PRODUCT_CC%' in ref.get('RefId') for ref in refs))
            self.assertTrue(all('-25' not in ref.get('RefId') and '-27' not in ref.get('RefId') for ref in refs))
        self.assertEqual(count, 12)

    def test_diagnostics_are_grouped_outside_expert_configuration(self) -> None:
        page = self.template.find(".//k:ParameterBlock[@Name='IOHCChannel%C%Page']", NS)
        diagnostics = page.find("k:ParameterBlock[@Name='Diagnostics']", NS)
        self.assertEqual(diagnostics.get('Text'), 'Diagnose')
        expert = page.find("k:ParameterBlock[@Name='ExpertOptions']", NS)
        self.assertIsNone(expert.find(".//k:ParameterBlock[@Name='Diagnostics']", NS))
        for name in ('Overview','Sensors','Objects','Products'):
            self.assertIsNotNone(diagnostics.find(f".//k:ParameterBlock[@Name='Diagnostic{name}']", NS))
        diagnostic_refs = {ref.get('RefId') for ref in diagnostics.findall('.//k:ParameterRefRef', NS)}
        for number in (14,15,103,104,*range(114,123),*range(124,139)):
            self.assertIn(f'%AID%_P-%TT%%CC%{number:03d}_R-%TT%%CC%{number:03d}01',diagnostic_refs)
        for number in range(110,114):
            self.assertIsNotNone(expert.find(f".//k:ParameterRefRef[@RefId='%AID%_P-%TT%%CC%{number:03d}_R-%TT%%CC%{number:03d}01']", NS))
        two_way = diagnostics.find("k:choose[@ParamRefId='%AID%_UP-%TT%%CC%009_R-%TT%%CC%00901']/k:when[@test='0']", NS)
        self.assertEqual(len(two_way.findall('k:ParameterBlock', NS)), 3)

    def test_visible_settings_have_existing_contextual_help(self) -> None:
        for filename in ('IoHomecontrol.templ.xml','IoHomecontrol.share.xml','IoHomecontrol.scene.part.xml','IoHomeProductPIC.ui.xml','IoHomeProductPVX.ui.xml'):
            for ref in parse(filename).findall('.//k:Dynamic//k:ParameterRefRef', NS):
                help_id = ref.get('HelpContext')
                self.assertIsNotNone(help_id, (filename, ref.get('RefId')))
                self.assertTrue((ROOT/'src/Baggages/Help_de'/f'{help_id}.md').is_file(), help_id)

    def test_import_target_supports_automatic_free_channel_selection(self) -> None:
        target = self.share.find(".//k:Parameter[@Name='ImportTargetChannel']", NS)
        number = self.share.find(".//k:ParameterType[@Name='IOHCImportTargetChannel']/k:TypeNumber", NS)
        self.assertEqual(target.get('Value'), '0')
        self.assertEqual(number.get('minInclusive'), '0')
        channels = self.share.find(".//k:ParameterType[@Name='IOHCNumChannels']/k:TypeNumber", NS)
        self.assertEqual(channels.get('minInclusive'), '1')

    def test_new_channels_allow_automatic_recognition(self) -> None:
        for field in ('Type','Orientation','Binary','Dimmable'):
            param = self.template.find(f".//k:Parameter[@Name='c%C%Recognition{field}Auto']", NS)
            self.assertEqual(param.get('Value'), '1')

    def test_main_page_keeps_suspend_and_groups_configuration(self) -> None:
        page = self.template.find(".//k:ParameterBlock[@Name='IOHCChannel%C%Page']", NS)
        self.assertIsNotNone(page.find("k:ParameterRefRef[@RefId='%AID%_UP-%TT%%CC%008_R-%TT%%CC%00801']", NS))
        for name in ('Functions', 'Commissioning'):
            self.assertIsNotNone(page.find(f"k:ParameterBlock[@Name='{name}']", NS))
        self.assertIsNotNone(page.find(".//k:ParameterBlock[@Name='Scenes']", NS))
        functions = page.find("k:ParameterBlock[@Name='Functions']", NS)
        self.assertIsNotNone(functions.find("k:choose/k:when[@test='0']/k:ParameterRefRef[@RefId='%AID%_UP-%TT%%CC%003_R-%TT%%CC%00301']", NS))

    def test_global_navigation_separates_overview_and_online_tools(self) -> None:
        channel = self.share.find(".//k:Channel[@Name='IOHC_Global']", NS)
        texts = {block.get('Text') for block in channel.findall('k:ParameterBlock', NS)}
        self.assertTrue({'Übersicht', 'Kanalauswahl', 'Inbetriebnahme', 'Diagnose und Funkmonitor'} <= texts)

    def test_unpublished_feedback_objects_are_not_offered(self) -> None:
        param = self.template.find(".//k:Parameter[@Name='c%C%DiagnosticObjects']", NS)
        self.assertEqual(param.get('Value'), '0')
        dynamic_refs = {r.get('RefId') for r in self.template.findall('.//k:Dynamic//k:ComObjectRefRef', NS)}
        for number in (12, 13, 21):
            self.assertFalse(any(f"%CC%{number:03d}_R-" in ref for ref in dynamic_refs))

    def test_import_uses_exact_profile_and_subprofile_for_ets_presentation(self) -> None:
        script = (ROOT / "src" / "IoHomecontrol.script.js").read_text()
        self.assertIn("IOHC_PRESENTATIONS[(p<<6)|s]", script)
        manifest = json.loads((ROOT / "src/protocol/recognition.json").read_text())
        self.assertTrue({0x0540,0x057A} <= {r["packed"] for r in manifest})
        self.assertIn('"OrientationObjects"', script)
        self.assertIn('"Dimmable"', script)
        self.assertIn('prefix + "ImportedProfile"', script)
        self.assertIn('prefix + "ImportedManufacturer"', script)
        self.assertIn('"BinaryOnly"', script)
        registry = (ROOT / "src" / "protocol" / "IoHomeProfileRegistry.cpp").read_text()
        registry_rows = registry.split("constexpr IoHomeProfileDescriptor kProfiles[] = {", 1)[1].split("};", 1)[0]
        registry_ids = {int(value, 16) for value in re.findall(r"profile\(0x([0-9A-Fa-f]{4})", registry_rows)}
        imported_ids = {r["packed"] for r in manifest}
        self.assertTrue(registry_ids <= imported_ids)
        self.assertIn(0x0380, imported_ids)  # Legacy Atlantic Cozy presentation.

    def test_on_off_subprofiles_show_binary_kos_without_position_kos(self) -> None:
        script = (ROOT / "src" / "IoHomecontrol.script.js").read_text()
        manifest = json.loads((ROOT / "src/protocol/recognition.json").read_text())
        binary_ids={r["packed"] for r in manifest if r["flags"]&2}
        self.assertTrue({0x017A,0x01BA,0x01FA,0x057A} <= binary_ids)
        ref = "%AID%_P-%TT%%CC%105_R-%TT%%CC%10501"
        for selection in ("5", "7", "8"):
            device = next(node for node in self.template.findall(".//k:choose/k:when", NS)
                          if node.get("test") == selection and
                          node.find(f"k:choose[@ParamRefId='{ref}']", NS) is not None)
            binary_choice = device.find(f"k:choose[@ParamRefId='{ref}']/k:when[@test='1']", NS)
            refs = {item.get("RefId") for item in binary_choice.findall("k:ComObjectRefRef", NS)}
            self.assertTrue(any("%CC%003_R" in item for item in refs))
            self.assertTrue(any("%CC%006_R" in item for item in refs))
            self.assertFalse(any("%CC%000_R" in item for item in refs))

    def test_two_way_secured_ventilation_is_guarded_before_transmit(self) -> None:
        source = (ROOT / "src" / "IoHomecontrolChannel.cpp").read_text()
        ventilation = source.split("void IoHomecontrolChannel::sendVentilationPosition()", 1)[1].split(
            "void IoHomecontrolChannel::handleSceneRecall", 1
        )[0]
        self.assertIn('if (!mIs1W)', ventilation)
        self.assertIn('VENTILATION disabled for 2W', ventilation)
        self.assertNotIn('sendRawTwoWayExecute', ventilation)
        self.assertLess(ventilation.index('if (!mIs1W)'), ventilation.index('mController.sendCommand'))

    def test_application_help_uses_relative_ko_references_only(self) -> None:
        documentation = (ROOT / "doc" / "Applikationsbeschreibung-IoHomecontrol.md").read_text()
        self.assertRegex(documentation, r"\bKn\+\d+\b")
        self.assertIsNone(re.search(r"\bKO\s*[-#:]?\s*\d+\b", documentation))
        self.assertIsNone(re.search(r"^\|\s*\d+\s*\|", documentation, re.MULTILINE))


if __name__ == "__main__":
    unittest.main()
