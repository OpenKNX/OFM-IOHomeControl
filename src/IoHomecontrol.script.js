// OFM-IO-Homecontrol -- OpenKNX --
// ETS JavaScript for io-homecontrol pairing workflow
// Uses function properties (objectIndex=160, propertyId=10) for device communication

var IOHC_FUNCTION_PROPERTY_OBJECT_INDEX = 160;
var IOHC_FUNCTION_PROPERTY_ID = 10;

function IOHC_invokeFunctionProperty(online, data) {
    return online.invokeFunctionProperty(IOHC_FUNCTION_PROPERTY_OBJECT_INDEX, IOHC_FUNCTION_PROPERTY_ID, data);
}

function IOHC_getChannelPrefix(context) {
    return "IOHC_c" + context.channelIndex;
}

function IOHC_getGlobalPrefix() {
    return "IOHC_";
}

function IOHC_getParameter(device, name) {
    return device.getParameterByName(name);
}

function IOHC_setParameterValue(device, name, value) {
    var parameter = IOHC_getParameter(device, name);
    if (parameter) {
        if (typeof value === "string" && value.length > 40) {
            value = value.substring(0, 40);
        }
        parameter.value = value;
    }
}

function IOHC_pad2(value) {
    return value < 10 ? "0" + value : "" + value;
}

function IOHC_nowText() {
    var now = new Date();
    return now.getFullYear() + "-" +
           IOHC_pad2(now.getMonth() + 1) + "-" +
           IOHC_pad2(now.getDate()) + " " +
           IOHC_pad2(now.getHours()) + ":" +
           IOHC_pad2(now.getMinutes()) + ":" +
           IOHC_pad2(now.getSeconds());
}

function IOHC_formatNodeId(nodeId) {
    if (!nodeId) {
        return "nicht angelernt";
    }

    var text = nodeId.toString(16).toUpperCase();
    while (text.length < 6) {
        text = "0" + text;
    }
    return text;
}

function IOHC_readNodeId(resp, startIndex) {
    return ((resp[startIndex] || 0) << 16) |
           ((resp[startIndex + 1] || 0) << 8) |
           (resp[startIndex + 2] || 0);
}

function IOHC_appendNodeId(data, nodeId) {
    var normalizedNodeId = nodeId || 0;
    data.push((normalizedNodeId >> 16) & 0xFF);
    data.push((normalizedNodeId >> 8) & 0xFF);
    data.push(normalizedNodeId & 0xFF);
}

// BEGIN GENERATED RECOGNITION
var IOHC_PRESENTATIONS = {
    64: [1, 1],
    128: [1, 0],
    129: [1, 1],
    130: [1, 0],
    192: [3, 0],
    256: [2, 0],
    257: [2, 0],
    320: [4, 0],
    378: [4, 2],
    384: [6, 4],
    442: [6, 2],
    448: [7, 0],
    506: [7, 2],
    576: [8, 0],
    577: [8, 0],
    640: [1, 0],
    832: [1, 0],
    896: [5, 0],
    960: [12, 0],
    1024: [9, 0],
    1088: [1, 1],
    1152: [1, 1],
    1216: [10, 0],
    1280: [11, 0],
    1281: [11, 0],
    1282: [11, 0],
    1283: [11, 0],
    1344: [13, 0],
    1402: [14, 2],
    1536: [1, 0],
    1537: [1, 0]
};
function IOHC_etsDeviceType(p,s) { return (IOHC_PRESENTATIONS[(p<<6)|s] || [0,0])[0]; }
function IOHC_hasOrientationObjects(p,s) { return (IOHC_PRESENTATIONS[(p<<6)|s] || [0,0])[1]&1; }
function IOHC_isBinaryOnly(p,s) { return ((IOHC_PRESENTATIONS[(p<<6)|s] || [0,0])[1]>>1)&1; }
// END GENERATED RECOGNITION

function IOHC_importDeviceLabel(etsType) {
    switch (etsType) {
    case 1: return "Rollladen/Jalousie";
    case 2: return "Fenster";
    case 3: return "Markise";
    case 4: return "Garagentor";
    case 5: return "Cozy-Thermostat";
    case 6: return "Licht";
    case 7: return "Tor";
    case 8: return "Schloss";
    case 9: return "Sonnenschutz";
    case 10: return "Vorhangschiene";
    case 11: return "Lüftung";
    case 12: return "Schalter";
    case 13: return "Heizung Stellwert";
    case 14: return "Heizung Ein/Aus";
    default: return "ioHC-Gerät";
    }
}

function IOHC_setExtractionResult(device, statusText, nodeIds) {
    IOHC_setParameterValue(device, "IOHC_ExtractionLastResult", statusText);
    for (var part = 0; part < 4; part++) {
        var first = part * 5;
        var values = [];
        for (var i = first; i < nodeIds.length && i < first + 5; i++) {
            values.push(IOHC_formatNodeId(nodeIds[i]));
        }
        IOHC_setParameterValue(device, "IOHC_ExtractionNodeIds" + (part + 1),
                               values.length ? values.join(", ") : "-");
    }
}

// Apply only documented ETS categories. Expert overrides and power policy stay intact.
function IOHC_applyRecognitionSettings(device, prefix, discovery) {
    var override = IOHC_getParameter(device, prefix + "ProfileOverride");
    if ((override && Number(override.value) != 0) || !discovery.metadataValid) return false;
    var etsType = discovery.presentationType !== undefined ? discovery.presentationType :
                  IOHC_etsDeviceType(discovery.protocolType, discovery.subtype);
    if (!etsType) return false;
    var fields = [
        ["RecognitionTypeAuto", "DeviceType", etsType],
        ["RecognitionOrientationAuto", "OrientationObjects", discovery.presentationFlags !== undefined ? discovery.presentationFlags&1 : IOHC_hasOrientationObjects(discovery.protocolType,discovery.subtype)],
        ["RecognitionBinaryAuto", "BinaryOnly", discovery.presentationFlags !== undefined ? (discovery.presentationFlags>>1)&1 : IOHC_isBinaryOnly(discovery.protocolType,discovery.subtype)],
        ["RecognitionDimmableAuto", "Dimmable", discovery.presentationFlags !== undefined ? (discovery.presentationFlags>>2)&1 : discovery.protocolType===6 && discovery.subtype===0 ? 1:0]
    ];
    var changed=false;
    for (var i=0;i<fields.length;i++) {
        var permission=IOHC_getParameter(device,prefix+fields[i][0]);
        if (!permission || Number(permission.value)!==1) continue;
        IOHC_setParameterValue(device,prefix+fields[i][1],fields[i][2]);
        if (i===0) IOHC_setParameterValue(device,prefix+"ChannelSelection",etsType+1);
        changed=true;
    }
    return changed;
}

function IOHC_queryRecognition(device, online, context, applySettings) {
    var channel = context.channelIndex - 1;
    var prefix = "IOHC_c" + context.channelIndex;
    var extended = IOHC_invokeFunctionProperty(online, [0x1E, channel]);
    var hasPresentation = extended && extended.length === 14 && extended[0] === 0 && extended[1] === 1 && extended[2] === channel && extended[12] >= 0 && extended[12] <= 14 && extended[13] >= 0 && extended[13] <= 7;
    var response = hasPresentation ? extended.slice(0,12) : IOHC_invokeFunctionProperty(online, [0x1D, channel]);
    // Older firmware has no snapshot API. Keep the existing status workflow usable.
    if (!response || response.length != 12 || response[0] != 0 ||
        response[1] != 1 || response[2] != channel) {
        IOHC_setParameterValue(device, prefix + "ImportedProfile", "Erkennung nicht verfügbar (Firmware)");
        IOHC_setParameterValue(device, prefix + "ImportedManufacturer", "Nicht aus aktueller Firmware gelesen");
        return false;
    }
    var discovery = {
        nodeId: IOHC_readNodeId(response, 3),
        protocolType: response[6] | (response[7] << 8),
        subtype: response[8], manufacturer: response[9], powerClass: response[10],
        metadataValid: (response[11] & 0x04) != 0,
        metadataComplete: (response[11] & 0x08) != 0
    };
    if (hasPresentation) { discovery.presentationType=extended[12]; discovery.presentationFlags=extended[13]; }
    var oneWay = (response[11] & 0x20) != 0;
    var ready = (response[11] & 0x10) != 0;
    IOHC_setParameterValue(device, prefix + "ImportedProfile",
        oneWay ? "1W: keine bestätigte Aktor-Erkennung" :
        discovery.metadataValid ? "Profil " + discovery.protocolType + "/" + discovery.subtype :
        "Metadaten ausstehend");
    IOHC_setParameterValue(device, prefix + "ImportedManufacturer",
        discovery.metadataValid && !oneWay ? "Hersteller " + discovery.manufacturer +
        ", Energieklasse " + discovery.powerClass : "Hersteller und Energieklasse unbekannt");
    if (!applySettings) return false;
    var applied = !oneWay && ready && discovery.nodeId != 0 &&
                  IOHC_applyRecognitionSettings(device, prefix, discovery);
    IOHC_setParameterValue(device, prefix + "PairingDiag", applied ?
        "Gerätetyp übernommen; ETS laden" : "Keine Übernahme: Profil/Override/Pairing");
    return applied;
}

function IOHC_resumeAssignment(device,online,channel) {
    var receipt=IOHC_invokeFunctionProperty(online,[0x21,channel]);
    if(!receipt || receipt.length!==16 || receipt[0]!==0 || receipt[1]!==1 || receipt[2]!==channel) return false;
    var discovery={nodeId:IOHC_readNodeId(receipt,7),protocolType:receipt[10]|receipt[11]<<8,
                   subtype:receipt[12],manufacturer:receipt[14],powerClass:receipt[15]===1?2:receipt[15]===0?1:0,
                   metadataValid:(receipt[13]&1)!==0};
    // Refuse stale receipts before any ETS mutation, not only at ACK time.
    var current=IOHC_invokeFunctionProperty(online,[0x1D,channel]);
    if(!current || current.length!==12 || current[0]!==0 || current[1]!==1 || current[2]!==channel ||
       IOHC_readNodeId(current,3)!==discovery.nodeId || (current[11]&0x20)!==0) return false;
    IOHC_configureImportedChannel(device,channel+1,discovery);
    var ack=[0x21,channel].concat(receipt.slice(3,10));
    var response=IOHC_invokeFunctionProperty(online,ack);
    if(!response || response[0]!==0) throw new Error("Zuordnung im Gerät gespeichert; ETS-Abgleich erneut ausführen.");
    return true;
}

function IOHC_applyRecognizedType(device, online, progress, context) {
    online.connect();
    try {
        IOHC_resumeAssignment(device,online,context.channelIndex-1);
        IOHC_queryPairingInfo(device, online, progress, context, "Status gelesen", "Status direkt vom Gerät gelesen");
        var applied = IOHC_queryRecognition(device, online, context, true);
        progress.setText(applied ? "Erkannten Gerätetyp übernommen. Applikation programmieren." :
                         "Keine Änderung: Erkennung fehlt oder manuelles Profil ist gesetzt.");
    } finally {
        online.disconnect();
    }
}

function IOHC_configureImportedChannel(device, channelNumber, discovery) {
    var prefix = "IOHC_c" + channelNumber;
    var etsType = discovery.metadataValid
                      ? IOHC_etsDeviceType(discovery.protocolType, discovery.subtype) : 0;
    var name = IOHC_getParameter(device, prefix + "Name");
    if (!name || !String(name.value || "").length) {
        IOHC_setParameterValue(device, prefix + "Name",
                               (discovery.metadataValid ? IOHC_importDeviceLabel(etsType) : "2W-Gerät") +
                               " " + IOHC_formatNodeId(discovery.nodeId));
    }
    IOHC_setParameterValue(device, prefix + "ProtocolMode", 0);
    IOHC_applyRecognitionSettings(device, prefix, discovery);
    IOHC_setParameterValue(device, prefix + "PairingLastResult",
                           discovery.metadataValid ? "Automatisch importiert" :
                           "Authentifiziert; Metadaten ausstehend");
    IOHC_setParameterValue(device, prefix + "PairedNodeIdDisplay", IOHC_formatNodeId(discovery.nodeId));
    IOHC_setParameterValue(device, prefix + "OneWaySummary", "2W (bidirektional)");
    IOHC_setParameterValue(device, prefix + "PairingDiag",
                           discovery.metadataValid ? "Programmierung erforderlich" :
                           "Authentifiziert; Metadaten ausstehend");
    var packedType = (discovery.protocolType << 6) | discovery.subtype;
    var packedText = packedType.toString(16).toUpperCase();
    while (packedText.length < 4) packedText = "0" + packedText;
    IOHC_setParameterValue(device, prefix + "ImportedProfile",
                           discovery.metadataValid
                               ? "Profil " + discovery.protocolType + "/" + discovery.subtype +
                                 " (0x" + packedText + ")"
                               : "Metadaten ausstehend");
    IOHC_setParameterValue(device, prefix + "ImportedManufacturer",
                           discovery.metadataValid
                               ? "Hersteller " + discovery.manufacturer +
                                 ", Energieklasse " + discovery.powerClass
                               : "Hersteller und Energieklasse unbekannt");
    // Activate last so all settings are coherent before the calculated
    // channel selector refreshes the dynamic view.
    IOHC_setParameterValue(device, prefix + "Active", 1);
}

function IOHC_syncChannelSelection(input, output, context) {
    if (input.Selection !== undefined) {
        var selection = Number(input.Selection);
        output.Active = selection > 0 ? 1 : 0;
        if (selection > 0) {
            output.DeviceType = selection - 1;
        }
        return;
    }

    output.Selection = Number(input.Active) == 1
        ? Number(input.DeviceType) + 1
        : 0;
}

function IOHC_controllerStateText(state) {
    switch (state) {
    case 0:
        return "Idle";
    case 6:
        return "Discovery senden";
    case 7:
        return "Warte Discovery";
    case 8:
        return "Discovery bestaetigen";
    case 9:
        return "Warte Discovery-Ack";
    case 10:
        return "Pause vor Key-Init";
    case 11:
        return "Starte Key-Transfer";
    case 12:
        return "Warte Key-Transfer";
    case 13:
        return "1W Challenge senden";
    case 14:
        return "Warte 1W Challenge";
    case 15:
        return "1W Ankuendigung senden";
    case 16:
        return "Warte 1W Ankuendigung";
    case 17:
        return "1W Remove senden";
    case 18:
        return "Warte 1W Remove";
    case 19:
        return "1W Key senden";
    case 20:
        return "Warte 1W Key";
    case 21:
        return "1W STOP senden";
    case 22:
        return "Warte 1W STOP";
    case 23:
        return "1W Abschluss-Pause";
    case 24:
        return "1W AB senden";
    case 25:
        return "Warte 1W AB";
    case 26:
        return "Key init senden";
    case 27:
        return "Warte Challenge";
    case 28:
        return "Key senden";
    case 29:
        return "Key-Antwort senden";
    case 30:
        return "Warte Key-Bestaetigung";
    case 31:
        return "SetConfig1 senden";
    case 32:
        return "Warte SetConfig1";
    case 33:
        return "SetConfig1 Auth senden";
    case 34:
        return "Warte SetConfig1 final";
    case 35:
        return "Pairing abgeschlossen";
    case 36:
        return "Pairing fehlgeschlagen";
    default:
        return "State " + state;
    }
}

function IOHC_setPairingInfo(device, context, resultText, nodeText, oneWaySummary, diagText) {
    var prefix = IOHC_getChannelPrefix(context);
    IOHC_setParameterValue(device, prefix + "PairingLastResult", resultText);
    IOHC_setParameterValue(device, prefix + "PairedNodeIdDisplay", nodeText);
    IOHC_setParameterValue(device, prefix + "OneWaySummary", oneWaySummary);
    IOHC_setParameterValue(device, prefix + "PairingDiag", diagText);
}

function IOHC_buildOneWaySummary(device, context, paired, pairedNodeId) {
    var prefix = IOHC_getChannelPrefix(context);
    var protocolMode = IOHC_getParameter(device, prefix + "ProtocolMode").value;
    if (protocolMode != 1) {
        return "2W (bidirektional)";
    }

    var broadcastType = IOHC_getParameter(device, prefix + "OneWayBroadcastType").value;
    var typeText = broadcastType;
    if (broadcastType == 255) {
        var deviceType = IOHC_getParameter(device, prefix + "DeviceType").value;
        typeText = "auto->" + ((deviceType == 3 || deviceType == 9) ? "3" : "2");
    }
    var targetNodeId = IOHC_getParameter(device, prefix + "OneWayTargetNodeId").value;
    if (!targetNodeId) {
        if (!paired) {
            return "1W Typ " + typeText + ", Rundruf, nicht angelernt";
        }
        return pairedNodeId
            ? "1W Typ " + typeText + ", gepaart " + IOHC_formatNodeId(pairedNodeId)
            : "1W Typ " + typeText + ", angelernt (Rundruf)";
    }

    var targetText = IOHC_formatNodeId(targetNodeId);
    if (!paired) {
        return "1W Typ " + typeText + ", Ziel " + targetText + ", nicht angelernt";
    }

    if (!pairedNodeId) {
        return "1W Typ " + typeText + ", Ziel " + targetText + ", angelernt";
    }

    var pairedText = IOHC_formatNodeId(pairedNodeId);
    if (pairedNodeId === targetNodeId) {
        return "1W Typ " + typeText + ", Ziel " + targetText + " aktiv";
    }

    return "1W Typ " + typeText + ", Ziel " + targetText + ", gepaart " + pairedText;
}

function IOHC_applyStatusResponse(device, context, resp, fallbackResult, fallbackDiag) {
    var paired = (resp[0] || 0) !== 0;
    var pairedNodeId = IOHC_readNodeId(resp, 1);
    var controllerState = resp.length > 4 ? resp[4] : 0;
    var lastStartStatus = resp.length > 5 ? resp[5] : 0;
    var statusPrefix = IOHC_getChannelPrefix(context);
    var isOneWay = IOHC_getParameter(device, statusPrefix + "ProtocolMode").value == 1;
    // 1W has no peer node id: the channel reports its controller enrollment
    // state instead of a paired actuator address.
    var nodeText = isOneWay
        ? (paired ? "1W angelernt (ohne Node-ID)" : "nicht angelernt")
        : (paired ? IOHC_formatNodeId(pairedNodeId) : "nicht angelernt");
    var resultText = fallbackResult;
    var diagText = fallbackDiag;

    if (!resultText) {
        if (paired) {
            resultText = isOneWay ? "1W Anmeldung gesendet" : "Angelernt";
        } else if (controllerState >= 6 && controllerState <= 28) {
            resultText = "Pairing aktiv";
        } else {
            resultText = "Nicht angelernt";
        }
    }

    if (!diagText) {
        if (controllerState >= 6 && controllerState <= 28) {
            diagText = IOHC_controllerStateText(controllerState);
        } else if (lastStartStatus == 1) {
            diagText = "Controller war blockiert";
        } else if (paired) {
            diagText = isOneWay
                ? "1W Anmeldesequenz gesendet; Annahme durch den Aktor nicht rückmeldbar"
                : "Gerät ist angelernt";
        } else {
            diagText = "Kein aktiver Pairing-Vorgang";
        }
    }

    var oneWaySummary = IOHC_buildOneWaySummary(device, context, paired, pairedNodeId);
    var prefix = IOHC_getChannelPrefix(context);
    var protocolMode = IOHC_getParameter(device, prefix + "ProtocolMode").value;
    if (protocolMode == 1 && resp.length >= 13) {
        var broadcastType = resp.length >= 14 ? resp[13] : IOHC_getParameter(device, prefix + "OneWayBroadcastType").value;
        var profileChannel = resp[6] || 0;
        var profileNodeId = IOHC_readNodeId(resp, 7);
        var manufacturer = resp[10] || 0;
        var sequence = ((resp[11] || 0) << 8) | (resp[12] || 0);
        oneWaySummary = "1W T" + broadcastType + " P" + profileChannel + " " +
                        IOHC_formatNodeId(profileNodeId) + " M" + manufacturer + " S" + sequence;
    }

    IOHC_setPairingInfo(device, context, resultText, nodeText, oneWaySummary, diagText);
}

function IOHC_queryPairingInfo(device, online, progress, context, fallbackResult, fallbackDiag) {
    var channelIndex = context.channelIndex - 1;
    var data = [0x12];
    data = data.concat(channelIndex);

    // Distinguish two very different failure modes:
    //  - invokeFunctionProperty THROWS  -> the device answered the connection
    //    but the property read itself failed (bus error, timeout, busy). The
    //    application is present, so this is a genuine read failure.
    //  - it returns empty / too short    -> the io-homecontrol function property
    //    does not exist, i.e. only the .knxprod was loaded and the application
    //    was never downloaded to the device ("not programmed").
    var resp = null;
    var readError = null;
    try {
        resp = IOHC_invokeFunctionProperty(online, data);
    } catch (error) {
        readError = error;
    }

    if (readError) {
        // Do not abort the whole script (red "failed" in ETS); report per channel.
        IOHC_setPairingInfo(
            device,
            context,
            "Lesen fehlgeschlagen",
            "unbekannt",
            IOHC_buildOneWaySummary(device, context, false, 0),
            "Statusabfrage fehlgeschlagen");
        if (progress) {
            progress.setProgress(100);
        }
        return null;
    }

    if (!resp || resp.length < 4) {
        IOHC_setPairingInfo(
            device,
            context,
            "Nicht programmiert",
            "unbekannt",
            IOHC_buildOneWaySummary(device, context, false, 0),
            "Applikation nicht programmiert?");
        if (progress) {
            progress.setProgress(100);
        }
        return null;
    }

    IOHC_applyStatusResponse(device, context, resp, fallbackResult, fallbackDiag);
    if (progress) {
        progress.setProgress(100);
    }
    return resp;
}

function IOHC_refreshAllPairingInfo(device, online, progress, context) {
    var channelCount = context.channelCount;
    if (!channelCount || channelCount < 1) {
        throw new Error("io-homecontrol: Keine Kanäle für die Pairing-Übersicht konfiguriert");
    }

    var activeChannels = [];
    for (var channelIndex = 1; channelIndex <= channelCount; channelIndex++) {
        var activity = IOHC_getParameter(device, "IOHC_c" + channelIndex + "Active");
        if (activity && activity.value == 1) {
            activeChannels.push(channelIndex);
        }
    }

    if (activeChannels.length == 0) {
        IOHC_setParameterValue(device, IOHC_getGlobalPrefix() + "OverviewLastRefresh", IOHC_nowText());
        progress.setText("Pairing-Übersicht: Keine aktivierten Kanäle.");
        progress.setProgress(100);
        return;
    }

    progress.setText("Pairing-Übersicht wird für " + activeChannels.length + " aktivierte Kanäle aktualisiert ...");
    progress.setProgress(5);
    // Read each channel in its own connect/disconnect cycle, exactly like the
    // per-channel "Pairing-Status auslesen" button. Holding a single connection
    // open across all reads does not work reliably; only the first read succeeds.
    for (var activeIndex = 0; activeIndex < activeChannels.length; activeIndex++) {
        var channelIndex = activeChannels[activeIndex];
        online.connect();
        try {
            IOHC_queryPairingInfo(device, online, null, { channelIndex: channelIndex }, "Status gelesen", "Status über Übersicht gelesen");
        } finally {
            online.disconnect();
        }
        progress.setText("Pairing-Übersicht: Kanal " + channelIndex + " aktualisiert (" + (activeIndex + 1) + " von " + activeChannels.length + ") ...");
        progress.setProgress(Math.floor(((activeIndex + 1) * 100) / activeChannels.length));
    }
    IOHC_setParameterValue(device, IOHC_getGlobalPrefix() + "OverviewLastRefresh", IOHC_nowText());

    progress.setText("Pairing-Übersicht aktualisiert.");
    progress.setProgress(100);
}

/**
 * Start pairing on the current channel.
 * Called from ETS Button with EventHandlerOnline="ConnectionOriented".
 *
 * @param {Object} device - ETS device object (parameter access)
 * @param {Object} online - Online connection to the device
 * @param {Object} progress - Progress bar control
 * @param {Object} context - Context with parameter references
 */
function IOHC_startPairing(device, online, progress, context) {
    var channelIndex = context.channelIndex - 1;
    var prefix = IOHC_getChannelPrefix(context);
    progress.setText("Pairing wird gestartet für Kanal " + (channelIndex + 1) + " ...");
    progress.setProgress(10);
    online.connect();
    try {
        var data = [0x10];
        data = data.concat(channelIndex);
        if (IOHC_getParameter(device, prefix + "ProtocolMode").value == 1) {
            IOHC_appendNodeId(data, IOHC_getParameter(device, prefix + "OneWayTargetNodeId").value);
        }
        var resp = IOHC_invokeFunctionProperty(online, data);

        if (resp[0] == 0) {
            progress.setText("Pairing gestartet. Gerät jetzt in den Lernmodus versetzen und Konsole oder Pairing-Status beobachten.");
            IOHC_queryPairingInfo(device, online, progress, context, "Pairing gestartet", "ETS hat den Start ausgelöst");
            return;
        }

        if (resp[0] == 4) {
            IOHC_setPairingInfo(device, context, "Start blockiert", "nicht angelernt", IOHC_buildOneWaySummary(device, context, false, 0), "Controller ist gerade beschaeftigt");
            throw new Error("io-homecontrol: Pairing ist gerade durch einen anderen Controller-Vorgang blockiert");
        }

        IOHC_setPairingInfo(device, context, "Start fehlgeschlagen", "nicht angelernt", IOHC_buildOneWaySummary(device, context, false, 0), "Unbekannter Startfehler");
        throw new Error("io-homecontrol: Pairing konnte nicht gestartet werden");
    } finally {
        online.disconnect();
    }
}

/**
 * Remove pairing (unpair) for the current channel.
 * Called from ETS Button with EventHandlerOnline="ConnectionOriented".
 *
 * @param {Object} device - ETS device object
 * @param {Object} online - Online connection to the device
 * @param {Object} progress - Progress bar control
 * @param {Object} context - Context with parameter references
 */
function IOHC_unpair(device, online, progress, context) {
    var channelIndex = context.channelIndex - 1;
    progress.setText("Pairing wird entfernt für Kanal " + (channelIndex + 1) + " ...");
    progress.setProgress(30);
    online.connect();
    try {
        var data = [0x13];
        data = data.concat(channelIndex);
        var resp = IOHC_invokeFunctionProperty(online, data);

        if (resp[0] == 0) {
            progress.setText("Pairing entfernt für Kanal " + (channelIndex + 1) + ".");
            IOHC_queryPairingInfo(device, online, progress, context, "Pairing entfernt", "Pairing und Schluessel geloescht");
            return;
        }

        IOHC_setPairingInfo(device, context, "Entfernen fehlgeschlagen", "unbekannt", IOHC_buildOneWaySummary(device, context, false, 0), "Unpair hat keinen Erfolg gemeldet");
        throw new Error("io-homecontrol: Entfernen des Pairings fehlgeschlagen");
    } finally {
        online.disconnect();
    }
}

function IOHC_refreshPairingInfo(device, online, progress, context) {
    var channelIndex = context.channelIndex - 1;
    progress.setText("Pairing-Status wird gelesen für Kanal " + (channelIndex + 1) + " ...");
    progress.setProgress(25);
    online.connect();
    try {
        IOHC_queryPairingInfo(device, online, progress, context, "Status gelesen", "Status direkt vom Gerät gelesen");
        IOHC_queryRecognition(device, online, context, false);
    } finally {
        online.disconnect();
    }
    progress.setText("Pairing-Status aktualisiert für Kanal " + (channelIndex + 1) + ".");
}

function IOHC_generateOneWayProfile(device, online, progress, context) {
    var channelIndex = context.channelIndex - 1;
    progress.setText("Neues eigenes 1W-Controllerprofil wird erzeugt für Kanal " + (channelIndex + 1) + " ...");
    progress.setProgress(20);
    online.connect();
    try {
        var resp = IOHC_invokeFunctionProperty(online, [0x15, channelIndex]);
        if (!resp || resp.length < 1) {
            throw new Error("io-homecontrol: Keine Antwort beim Erzeugen des 1W-Profils");
        }
        if (resp[0] == 0) {
            IOHC_queryPairingInfo(device, online, progress, context, "Neues 1W-Profil erzeugt", "Eigenes 1W-Profil bereit");
            progress.setText("Neues eigenes 1W-Controllerprofil für Kanal " + (channelIndex + 1) + " erzeugt.");
            return;
        }
        if (resp[0] == 3) {
            throw new Error("io-homecontrol: Profil wird von mindestens einem gepaarten Kanal verwendet. Pairing zuerst entfernen.");
        }
        if (resp[0] == 4) {
            throw new Error("io-homecontrol: Kanal verwendet ein geteiltes Profil. In ETS zuerst 'eigenes Profil' wählen.");
        }
        throw new Error("io-homecontrol: Neues 1W-Profil konnte nicht erzeugt werden");
    } finally {
        online.disconnect();
    }
}

function IOHC_startOneWayClone(device, online, progress, context) {
    var channelIndex = context.channelIndex - 1;
    progress.setText("1W-Klonen wird vorbereitet für Kanal " + (channelIndex + 1) + " ...");
    progress.setProgress(20);
    online.connect();
    try {
        var resp = IOHC_invokeFunctionProperty(online, [0x16, channelIndex]);
        if (!resp || resp.length < 1) {
            throw new Error("io-homecontrol: Keine Antwort beim Start des 1W-Klonens");
        }
        if (resp[0] == 0) {
            IOHC_setPairingInfo(device, context, "1W-Klon aktiv", "nicht angelernt", IOHC_buildOneWaySummary(device, context, false, 0), "Originalfernbedienung jetzt kopieren");
            progress.setText("1W-Klonen aktiv. Jetzt an der Original-Fernbedienung die Copy-Funktion auslösen.");
            progress.setProgress(100);
            return;
        }
        if (resp[0] == 4) {
            throw new Error("io-homecontrol: 1W-Klonen ist gerade durch einen anderen Controller-Vorgang blockiert");
        }
        throw new Error("io-homecontrol: Kanal ist nicht für 1W-Klonen geeignet");
    } finally {
        online.disconnect();
    }
}

function IOHC_read32(data,offset) {
    return data[offset]*16777216+data[offset+1]*65536+data[offset+2]*256+data[offset+3];
}
function IOHC_jobSnapshot(online) {
    var caps=IOHC_invokeFunctionProperty(online,[0x23]);
    if(!caps || caps[0]!==0) return null; // explicit legacy firmware fallback
    if(caps.length!==10 || caps[1]!==1) throw new Error("Ungültige Job-Fähigkeiten");
    var job=IOHC_invokeFunctionProperty(online,[0x24]);
    if(!job || job.length!==26 || job[0]!==0 || job[1]!==1 || IOHC_read32(job,20)!==IOHC_read32(caps,6))
        throw new Error("Job-Status hat sich geändert; erneut lesen");
    return {owner:job[2],stage:job[3],channel:job[8],node:IOHC_readNodeId(job,9),generation:IOHC_read32(job,4),revision:IOHC_read32(job,12),
            remainingMs:IOHC_read32(job,16),error:job[24],count:job[25],token:job.slice(20,24).concat(job.slice(4,8),job.slice(12,16))};
}
function IOHC_readCommissioningStatus(device,online,progress,context) {
    online.connect();
    try {
        var job=IOHC_jobSnapshot(online);
        if(!job) throw new Error("Firmware unterstützt keinen erweiterten Einrichtungsstatus");
        var owners=["Keine Einrichtung","Pairing","Schlüsselimport","Schlüsselaufzeichnung","1W-Fernbedienung kopieren"];
        var stages=["Inaktiv","Vorbereitung","Suche","Prüfung","Ergebnis bereit","Speichern","Abgeschlossen","Abgebrochen","Fehlgeschlagen","Unbestätigt"];
        var errors=["kein Fehler","Zeitlimit","Peer/Transport","Speicherfehler","Veraltetes Ergebnis"];
        if(job.owner>=owners.length||job.stage>=stages.length||job.error>=errors.length) throw new Error("Unbekanntes Job-Statusschema");
        var text=owners[job.owner]+": "+stages[job.stage]+"; "+errors[job.error]+". Generation "+job.generation+", Ergebnisrevision "+job.revision;
        if(job.channel<16) text+="; Kanal "+(job.channel+1);
        if(job.node) text+="; Node "+IOHC_formatNodeId(job.node);
        if(job.stage>=1&&job.stage<=3) text+="; Restzeit "+Math.ceil(job.remainingMs/1000)+" s";
        if(job.owner===2&&job.stage===4) {
            if(job.count>24) throw new Error("Ungültige Kandidatenzahl");
            text+="; Kandidaten "+job.count+"; Ergebnis übernehmen";
            for(var index=0;index<job.count;index++) {
                var candidate=IOHC_invokeFunctionProperty(online,[0x25].concat(job.token,[index]));
                if(!candidate||candidate.length!==20||candidate[0]!==0||candidate[1]!==1||candidate[2]!==index||
                   IOHC_read32(candidate,12)!==job.generation||IOHC_read32(candidate,16)!==job.revision)
                    throw new Error("Kandidatenliste geändert; erneut lesen");
                var flags=candidate[11];
                text+=". "+(index+1)+": Node "+IOHC_formatNodeId(IOHC_readNodeId(candidate,3));
                text+=(flags&4)?", Profil "+(candidate[6]|candidate[7]<<8)+"/"+candidate[8]+", Hersteller "+candidate[9]:", Profil unbekannt";
                text+=", "+((flags&16)?"gerichtet verifiziert":(flags&1)?"passiv authentifiziert":"nicht verifiziert");
                text+=", "+((flags&8)?"vollständige":"teilweise")+" Metadaten";
            }
            var current=IOHC_jobSnapshot(online);
            if(!current||JSON.stringify(current.token)!==JSON.stringify(job.token)||current.count!==job.count||current.stage!==job.stage)
                throw new Error("Einrichtung während des Lesens geändert; erneut lesen");
        }
        if(job.owner===3||job.owner===4) text+=". Aufzeichnung/Kopie bestätigt keine Aktor-Anmeldung";
        text+=". Statuslesen ändert keine ETS-Einstellungen und bestätigt keinen Geräte-Download.";
        progress.setText(text);
    } finally {online.disconnect();}
}
// Project settings and permission ownership only; no fabricated adoption history.
function IOHC_effectiveSettingsText(device,context) {
    var channel=Number(context.channelIndex);
    if(channel<1||channel>16||Math.floor(channel)!==channel) throw new Error("Ungültiger Kanal");
    var prefix=IOHC_getChannelPrefix(context),override=IOHC_getParameter(device,prefix+"ProfileOverride");
    var forced=override&&Number(override.value)!==0;
    var fields=[["RecognitionTypeAuto","DeviceType","Gerätetyp"],
                ["RecognitionOrientationAuto","OrientationObjects","Orientierung"],
                ["RecognitionBinaryAuto","BinaryOnly","Binärmodus"],
                ["RecognitionDimmableAuto","Dimmable","Dimmen"]];
    var retained=0,details="";
    for(var i=0;i<fields.length;i++) {
        var permission=IOHC_getParameter(device,prefix+fields[i][0]),setting=IOHC_getParameter(device,prefix+fields[i][1]);
        var automatic=!forced&&permission&&Number(permission.value)===1;
        if(!automatic)retained++;
        details+=fields[i][2]+": "+(setting?String(setting.value):"nicht verfügbar")+
            (automatic?" (Übernahme erlaubt)":" (ETS-Vorgabe bleibt erhalten)")+". ";
    }
    var text="Kanal "+channel+": "+retained+" von 4 Erkennungsfeldern behalten ihre ETS-Vorgabe. "+details;
    if(forced)text+="Profil-Override "+override.value+" sperrt automatische Übernahme. ";
    var expert=[["ProtocolMode","Protokollmodus"],["TwoWayPowerClass","2W-Energieklasse"],
                ["Suspend","Suspendiert"]];
    for(var j=0;j<expert.length;j++) {
        var value=IOHC_getParameter(device,prefix+expert[j][0]);
        if(value)text+=expert[j][1]+": "+value.value+". ";
    }
    return text+"Experteneinstellungen bleiben wirksam, auch wenn ausgeblendet. Werte aus dem ETS-Projekt; Geräte-Download und Herkunft früherer Änderungen sind hier nicht bestätigt.";
}
function IOHC_showEffectiveSettings(device,online,progress,context) {
    progress.setText(IOHC_effectiveSettingsText(device,context));
}
function IOHC_readChannelEvidence(device,online,progress,context) {
    var channel=Number(context.channelIndex)-1;
    if(channel<0||channel>=16||Math.floor(channel)!==channel) throw new Error("Ungültiger Kanal");
    online.connect();
    try {
        var identity=IOHC_invokeFunctionProperty(online,[0x1D,channel]);
        if(!identity||identity.length!==12||identity[0]!==0||identity[1]!==1||identity[2]!==channel) throw new Error("Ungültiger Erkennungsstatus");
        var node=IOHC_readNodeId(identity,3),prefix=IOHC_getChannelPrefix(context);
        var text=IOHC_effectiveSettingsText(device,context)+" Node "+IOHC_formatNodeId(node)+". ";
        if((identity[11]&4)!==0) text+="Profil "+(identity[6]|identity[7]<<8)+"/"+identity[8]+", Hersteller "+identity[9]+", "+((identity[11]&8)!==0?"vollständige":"teilweise")+" Metadaten. ";
        else text+="Keine bestätigten Profilmetadaten. ";
        var override=IOHC_getParameter(device,prefix+"ProfileOverride");
        if(override&&Number(override.value)!==0) text+="Manueller Profil-Override aktiv. ";
        var fields=[["RecognitionTypeAuto","Gerätetyp"],["RecognitionOrientationAuto","Orientierung"],["RecognitionBinaryAuto","Binärmodus"],["RecognitionDimmableAuto","Dimmen"]];
        for(var i=0;i<fields.length;i++) {
            var permission=IOHC_getParameter(device,prefix+fields[i][0]);
            text+=fields[i][1]+": "+(permission&&Number(permission.value)===1?"Übernahme erlaubt":"manuell")+". ";
        }
        var binding=IOHC_invokeFunctionProperty(online,[0x22,channel]);
        if(binding&&binding[0]===0) {
            if(binding.length!==6||binding[1]!==1||binding[2]!==channel||binding[3]>4||binding[4]>4||binding[5]!==0) throw new Error("Unbekanntes Produkt-Bindungsschema");
            var families=["unbekannt","RGB-Licht","Tunable White","Atlantic PassAPC Wärmepumpe","Atlantic PassAPC Hybrid"];
            var reasons=["Familie erkannt","Identität fehlt","Aktor-Klasse fehlt","Widersprüchliche Metadaten","Keine belegte Produktdefinition"];
            text+="Produktfamilie: "+families[binding[3]]+" ("+reasons[binding[4]]+"). Modell/Generation und RF-Schreibqualifikation unbekannt. ";
            var indexes=binding[3]===1?[0,10,11]:binding[3]===2?[0,14]:[];
            var trusts=["keine","passiv","korreliert","authentifiziert"];
            for(var j=0;j<indexes.length;j++) {
                var sample=IOHC_invokeFunctionProperty(online,[0x28,channel,indexes[j]]);
                if(!sample||sample.length!==20||sample[0]!==0||sample[1]!==1||sample[2]!==channel||sample[3]!==indexes[j]||IOHC_readNodeId(sample,4)!==node||sample[7]>3||sample[18]>1||sample[19]>1) throw new Error("Produktbeobachtung geändert/ungültig");
                text+=(indexes[j]===0?"MP":"FP"+indexes[j])+": ";
                text+=sample[18]?"raw "+((sample[8]<<8)|sample[9])+", "+trusts[sample[7]]+", Generation "+IOHC_read32(sample,10)+", Alter "+IOHC_read32(sample,14)+" ms, "+(sample[19]?"frisch":"veraltet"):"keine Beobachtung";
                text+=". ";
            }
        } else text+="Produktbindung von dieser Firmware nicht verfügbar. ";
        var current=IOHC_invokeFunctionProperty(online,[0x1D,channel]);
        if(!current||current.length!==12||current[0]!==0||current[1]!==1||current[2]!==channel||IOHC_readNodeId(current,3)!==node) throw new Error("Kanal während des Lesens geändert; erneut lesen");
        text+="Nur aktuelle Evidenz, keine Historie. Keine Einstellungen übernommen und keine RF-Abfrage ausgelöst.";
        progress.setText(text);
    } finally {online.disconnect();}
}
function IOHC_cancelCommissioning(device,online,progress,context) {
    online.connect();
    try {
        var job=IOHC_jobSnapshot(online);
        if(!job) throw new Error("Firmware unterstützt keinen sicheren Job-Abbruch");
        var response=IOHC_invokeFunctionProperty(online,[0x27].concat(job.token.slice(0,8)));
        if(!response || response[0]!==0) throw new Error("Job bereits beendet oder geändert; Status aktualisieren");
        progress.setText("Einrichtung abgebrochen. Bereits übertragene Schlüssel und gespeicherte Zuordnungen bleiben bestehen.");
    } finally {online.disconnect();}
}
function IOHC_assignFrozenCandidates(online,job,discoveries,channelCount) {
    var result=[0,discoveries.length];
    for(var d=0;d<discoveries.length;d++) {
        var candidate=discoveries[d],response=null;
        for(var c=0;c<channelCount;c++) {
            var node=[(candidate.nodeId>>16)&255,(candidate.nodeId>>8)&255,candidate.nodeId&255];
            response=IOHC_invokeFunctionProperty(online,[0x26].concat(job.token,[candidate.index,c],node));
            if(!response || response.length!==2 || response[0]>2) throw new Error("Zuordnung veraltet/fehlgeschlagen; gespeicherte Ergebnisse erneut lesen");
            if(response[0]!==2)break;
        }
        result.push(response?response[0]:2,response?response[1]:255);
    }
    return result;
}

function IOHC_startKeyExtract(device, online, progress, context) {
    progress.setText("2W-Schlüsselextraktion wird vorbereitet ...");
    progress.setProgress(5);
    online.connect();
    try {
        var status = IOHC_invokeFunctionProperty(online, [0x18]);
        if (!status || status.length < 11 || status[0] != 0) {
            throw new Error("io-homecontrol: Ungültige Statusantwort während der 2W-Übernahme");
        }

        var phase = status[1] || 0;
        if (phase == 0 || phase == 5 || phase == 6) {
            var resp = IOHC_invokeFunctionProperty(online, [0x17]);
            if (!resp || resp.length < 1) {
                throw new Error("io-homecontrol: Keine Antwort beim Start der 2W-Schlüsselextraktion");
            }
            if (resp[0] != 0) {
                throw new Error("io-homecontrol: 2W-Schlüsselextraktion ist gerade blockiert");
            }

            IOHC_setExtractionResult(device, "Extraktion läuft", []);
            progress.setText("2W-Schlüsselextraktion aktiv. Jetzt am Fremd-Gateway 'Gerät hinzufügen' starten und diese Schaltfläche danach erneut drücken.");
            progress.setProgress(100);
            return;
        }

        if (phase == 1) {
            progress.setText("Schlüsselextraktion läuft. Am Fremd-Gateway 'Gerät hinzufügen' starten und diese Schaltfläche danach erneut drücken.");
            progress.setProgress(100);
            return;
        }
        if (phase == 2) {
            var hubNode = IOHC_readNodeId(status, 2);
            var extractionNode = IOHC_readNodeId(status, 5);
            var nodeVerificationSeen = (status[24] || 0) != 0;
            var nodeVerificationAuthDone = (status[25] || 0) != 0;
            IOHC_setExtractionResult(device,
                                     nodeVerificationAuthDone
                                         ? "Schlüssel extrahiert; Node-Verifikation authentifiziert"
                                         : nodeVerificationSeen
                                         ? "Schlüssel extrahiert; Node-Verifikation beantwortet"
                                         : "Schlüssel extrahiert; Prüfung läuft",
                                     []);
            progress.setText("2W-Schlüssel erfolgreich extrahiert (Gateway-/System-Node-ID " +
                             IOHC_formatNodeId(hubNode) + ", temporäre Extraction-Device-ID " +
                             IOHC_formatNodeId(extractionNode) + "). " +
                             (nodeVerificationAuthDone
                                  ? "Die Node-Verifikation wurde authentifiziert. "
                                  : nodeVerificationSeen
                                  ? "Die Node-Verifikation wurde beantwortet. "
                                  : "Warte auf die Abschlussprüfung des Fremd-Gateways. ") +
                             "Diese Schaltfläche danach erneut drücken.");
            progress.setProgress(100);
            return;
        }
        if (phase == 3) {
            var nodeVerificationSeen = (status[24] || 0) != 0;
            var nodeVerificationAuthDone = (status[25] || 0) != 0;
            IOHC_setExtractionResult(device,
                                     nodeVerificationAuthDone
                                         ? "Schlüssel extrahiert; Node-Verifikation authentifiziert"
                                         : nodeVerificationSeen
                                         ? "Schlüssel extrahiert; Node-Verifikation beantwortet"
                                         : "Schlüssel extrahiert; Node-Verifikation nicht beobachtet",
                                     []);
            progress.setText("2W-Schlüsselextraktion abgeschlossen. " +
                             (nodeVerificationAuthDone
                                  ? "Die Node-Verifikation wurde authentifiziert. "
                                  : nodeVerificationSeen
                                  ? "Die Node-Verifikation wurde beantwortet; eine zusätzliche Authentifizierungsrunde war optional. "
                                  : "Es wurde keine Node-Verifikation beobachtet; der Schlüssel wurde dennoch erfolgreich extrahiert. ") +
                             "Die authentifizierte Gerätesuche läuft; diese Schaltfläche danach erneut drücken.");
            progress.setProgress(100);
            return;
        }
        if (phase != 4) {
            throw new Error("io-homecontrol: Unbekannter Status der 2W-Schlüsselübernahme");
        }

        var job=IOHC_jobSnapshot(online);
        if(job && (job.owner!==2 || job.stage!==4 || !job.revision || job.count!==(status[8]||0)))
            throw new Error("Ergebnisliste geändert; erneut lesen");
        var resultCount = status[8] || 0;
        var overflow = (status[10] || 0) != 0;
        var discoveries = [];
        var nodeIds = [];
        for (var resultIndex = 0; resultIndex < resultCount; resultIndex++) {
            var found = IOHC_invokeFunctionProperty(online, job ? [0x25].concat(job.token,[resultIndex]) : [0x19, resultIndex]);
            if(job && (!found || found.length!==20 || found[1]!==1 || found[2]!==resultIndex ||
                IOHC_read32(found,12)!==job.generation || IOHC_read32(found,16)!==job.revision))
                throw new Error("Ergebnisgeneration geändert; erneut lesen");
            if (!found || found.length < 11 || found[0] != 0) {
                throw new Error("io-homecontrol: Scan-Ergebnis " + (resultIndex + 1) + " konnte nicht gelesen werden");
            }
            var discovery = {
                index: resultIndex,
                nodeId: IOHC_readNodeId(found, 3),
                protocolType: (found[6] || 0) | ((found[7] || 0) << 8),
                subtype: found[8] || 0,
                manufacturer: found[9] || 0,
                powerClass: found[10] || 0,
                passiveAuthVerified: found.length > 11 && (found[11] & 0x01) != 0,
                speResponseSeen: found.length > 11 && (found[11] & 0x02) != 0,
                directedVerified: found.length > 11 && (found[11] & 0x10) != 0,
                metadataValid: found.length > 11 ? (found[11] & 0x04) != 0 : false,
                metadataComplete: found.length > 11 ? (found[11] & 0x08) != 0 : false
            };
            discoveries.push(discovery);
            nodeIds.push(discovery.nodeId);
        }

        var channelCount = context && context.channelCount ? context.channelCount : 16;
        var channelActive = [];
        var assignmentRequest = [0x1C, channelCount];
        for (var channelIndex = 0; channelIndex < channelCount; channelIndex++) {
            var activeParameter = IOHC_getParameter(device, "IOHC_c" + (channelIndex + 1) + "Active");
            var isActive = activeParameter && Number(activeParameter.value) == 1;
            channelActive[channelIndex] = isActive;
            assignmentRequest.push(channelIndex);
        }

        var configuredChannels = 0;
        var alreadyConfigured = 0;
        var unassigned = 0;
        var assignmentResponse = job ? IOHC_assignFrozenCandidates(online,job,discoveries,channelCount) : IOHC_invokeFunctionProperty(online, assignmentRequest);
        if (!assignmentResponse || assignmentResponse.length < 2 || assignmentResponse[0] != 0 ||
            assignmentResponse[1] != discoveries.length ||
            assignmentResponse.length < 2 + (discoveries.length * 2)) {
            throw new Error("io-homecontrol: Geräte konnten nicht gesammelt zugeordnet werden");
        }
        for (var d = 0; d < discoveries.length; d++) {
            var assignmentStatus = assignmentResponse[2 + (d * 2)];
            var assignedChannel = assignmentResponse[3 + (d * 2)];
            if (assignmentStatus == 0) {
                if(job) IOHC_resumeAssignment(device,online,assignedChannel);
                else IOHC_configureImportedChannel(device, assignedChannel + 1, discoveries[d]);
                channelActive[assignedChannel] = true;
                configuredChannels++;
            } else if (assignmentStatus == 1) {
                if (assignedChannel < channelCount && !channelActive[assignedChannel]) {
                    IOHC_configureImportedChannel(device, assignedChannel + 1, discoveries[d]);
                    channelActive[assignedChannel] = true;
                    configuredChannels++;
                } else {
                    alreadyConfigured++;
                }
            } else if (assignmentStatus == 2) {
                unassigned++;
            } else {
                throw new Error("io-homecontrol: Scan-Ergebnis " + (d + 1) + " ist nicht mehr verfügbar");
            }
            progress.setProgress(80 + Math.floor(((d + 1) * 15) / Math.max(1, discoveries.length)));
        }

        var saveResp = IOHC_invokeFunctionProperty(online, [0x1B]);
        if (!saveResp || saveResp.length < 1 || saveResp[0] != 0) {
            throw new Error("io-homecontrol: Schlüsselübernahme konnte nicht abgeschlossen werden");
        }

        var nodeText = discoveries.length ? discoveries.map(function (entry) {
            var source = entry.passiveAuthVerified && entry.speResponseSeen ? "passiv+SPE" :
                         entry.passiveAuthVerified ? "passiv authentifiziert" :
                         entry.directedVerified ? "gerichtet authentifiziert" : "SPE";
            var metadata = entry.metadataComplete ? "Metadaten vollständig" :
                           entry.metadataValid ? "Metadaten teilweise" : "Metadaten ausstehend";
            return IOHC_formatNodeId(entry.nodeId) + " (" + source + ", " + metadata + ")";
        }).join(", ") : "keine";
        var summary = configuredChannels + " importiert, " + alreadyConfigured + " vorhanden";
        if (unassigned > 0) {
            summary += ", " + unassigned + " ohne freien Kanal";
        }
        if (overflow) {
            summary += ", weitere Ergebnisse verworfen";
        }
        var resultStatus = resultCount == 0
                               ? "Schlüssel extrahiert; keine Geräte gefunden"
                               : (configuredChannels > 0
                                      ? "Erfolgreich; Programmierung nötig"
                                      : "Erfolgreich; keine Änderung");
        IOHC_setExtractionResult(device, resultStatus, nodeIds);

        if (resultCount == 0) {
            progress.setText("2W-Schlüssel erfolgreich extrahiert, aber die anschließende authentifizierte Gerätesuche hat keine Geräte gefunden.");
        } else if (configuredChannels > 0) {
            progress.setText("2W-Schlüssel erfolgreich extrahiert. Gefundene Node-IDs: " + nodeText +
                             ". " + summary + ". Das KNX-Gerät muss jetzt in ETS neu programmiert werden, bevor die Geräte gesteuert werden können.");
        } else {
            progress.setText("2W-Schlüssel erfolgreich extrahiert. Geräte wurden gefunden, es waren jedoch keine ETS-Änderungen notwendig. Gefundene Node-IDs: " +
                             nodeText + ". " + summary + ".");
        }
        progress.setProgress(100);
    } finally {
        online.disconnect();
    }
}

// Explicit read action, kept separate from the passive evidence view.
function IOHC_requestProductObservations(device,online,progress,context) {
    var channel=Number(context.channelIndex)-1;
    if(channel<0||channel>=16||Math.floor(channel)!==channel) throw new Error("Ungültiger Kanal");
    online.connect();
    try {
        var caps=IOHC_invokeFunctionProperty(online,[0x23]);
        if(!caps||caps.length!==10||caps[0]!==0||caps[1]!==1||(IOHC_read32(caps,2)&16)===0)
            throw new Error("Firmware unterstützt diese Produktabfrage nicht");
        var identity=IOHC_invokeFunctionProperty(online,[0x1D,channel]);
        if(!identity||identity.length!==12||identity[0]!==0||identity[1]!==1||identity[2]!==channel||
           (identity[11]&0x14)!==0x14||(identity[11]&0x20)!==0) throw new Error("Gültiger gepaarter 2W-Kanal erforderlich");
        var node=IOHC_readNodeId(identity,3),binding=IOHC_invokeFunctionProperty(online,[0x22,channel]);
        if(!node||!binding||binding.length!==6||binding[0]!==0||binding[1]!==1||binding[2]!==channel||binding[3]>4||binding[4]>4)
            throw new Error("Ungültige Produktbindung");
        if((IOHC_read32(caps,2)&64)!==0) {
            var mask=binding[3]===1?0x0600:binding[3]===2?0x2000:0;
            var request=[0x32,channel,mask>>8,mask&255].concat(identity.slice(3,6));
            var combined=IOHC_invokeFunctionProperty(online,request);
            if(!combined||combined.length!==5||combined[0]!==0||combined[1]!==1||combined[2]!==channel||combined[3]!==request[2]||combined[4]!==request[3])
                throw new Error("Produkt-Snapshot blockiert; Einrichtungs-/Diagnosestatus prüfen");
            var after=IOHC_invokeFunctionProperty(online,[0x1D,channel]);
            if(!after||after.length!==12||after[0]!==0||after[1]!==1||after[2]!==channel||IOHC_readNodeId(after,3)!==node)
                throw new Error("Kanal geändert; gestartete Abfrage bleibt an die ursprüngliche Identität gebunden");
            progress.setText("Eine gemeinsame MP/FP-Abfrage gestartet. Anschließend Produktevidenz lesen. Gemeinsame Antwortgeneration bedeutet keine Authentifizierung oder Schreibfreigabe.");
            return;
        }
        var indexes=binding[3]===1?[0,10,11]:binding[3]===2?[0,14]:[0],queued=0;
        for(var i=0;i<indexes.length;i++) {
            var result=IOHC_invokeFunctionProperty(online,[0x2E,channel,indexes[i]].concat(identity.slice(3,6)));
            if(!result||result.length!==4||result[0]!==0||result[1]!==1||result[2]!==channel||result[3]!==indexes[i]) {
                progress.setText(queued+" Einzelabfragen gestartet; weitere Abfrage blockiert. Bereits gestartete Abfragen bleiben aktiv.");
                throw new Error("Produktabfrage blockiert; Einrichtungs-/Diagnosestatus prüfen");
            }
            queued++;
        }
        var current=IOHC_invokeFunctionProperty(online,[0x1D,channel]);
        if(!current||current.length!==12||current[0]!==0||current[1]!==1||current[2]!==channel||IOHC_readNodeId(current,3)!==node)
            throw new Error("Kanal geändert; gestartete Abfragen sind an ihre ursprüngliche Identität gebunden");
        progress.setText(queued+" Einzelabfragen gestartet. Anschließend Produktevidenz lesen. Separate Antworten bilden keine atomare RGB-Messung; keine Schreibfreigabe oder ETS-Änderung.");
    } finally {online.disconnect();}
}

function IOHC_readPersistenceEvidence(device,online,progress,context) {
    var channel=Number(context.channelIndex)-1;
    if(channel<0||channel>=16||Math.floor(channel)!==channel) throw new Error("Ungültiger Kanal");
    online.connect();
    try {
        var caps=IOHC_invokeFunctionProperty(online,[0x23]);
        if(!caps||caps.length!==10||caps[0]!==0||caps[1]!==1||(IOHC_read32(caps,2)&32)===0) throw new Error("Firmware unterstützt diese Speicherprüfung nicht");
        var record=IOHC_invokeFunctionProperty(online,[0x2F,channel]);
        if(!record||record.length!==21||record[0]!==0||record[1]!==1||record[2]!==channel||record[10]>4||record[11]>1||record[20]>4||IOHC_read32(record,16)!==IOHC_read32(caps,6))
            throw new Error("Ungültiger oder veralteter Speicherstatus");
        var receipts=["fehlt","vorhanden","beschädigt","Backend nicht verfügbar","entfernte Zuordnung"],f=record[3];
        var text="2W-Journal: "+((f&1)?"Backend bereit":"Backend nicht bereit")+", "+((f&2)?"TX-Sperre nach Fehler":"keine gespeicherte Fehlersperre")+", "+((f&4)?"Commit vorhanden":"noch kein Commit")+". ";
        text+="Controlleridentität "+((f&8)?"entspricht Commit":"nicht als aktueller Commit bestätigt")+". Kanalbindung "+((f&32)?"entspricht Commit":"nicht als aktueller 2W-Commit bestätigt")+". ";
        text+="Zuordnungsbeleg: "+receipts[record[10]]+", Revision "+IOHC_read32(record,12)+", "+(record[11]?"aktuell":"nicht aktuell")+". Globaler 1W-Wiederherstellungsstatus "+record[20]+". ";
        text+="Nur Statusprüfung: kein Löschen oder Reparieren, kein Download- oder Stromausfallnachweis.";
        var current=IOHC_invokeFunctionProperty(online,[0x2F,channel]);
        if(!current||current.length!==21||JSON.stringify(current)!==JSON.stringify(record))throw new Error("Speicherstatus während des Lesens geändert; erneut lesen");
        progress.setText(text);
    } finally {online.disconnect();}
}
