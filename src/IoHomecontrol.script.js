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

// Bounded project history: the last two distinct explicit recognition adoptions.
// It is not a device-persistence receipt or cryptographic authentication claim.
function IOHC_recordRecognitionAdoption(device,prefix,field,value,discovery) {
    var numbers=[discovery.nodeId,discovery.protocolType,discovery.subtype,discovery.manufacturer],limits=[0xFFFFFF,0xFFFF,255,255];
    for(var n=0;n<numbers.length;n++)if(typeof numbers[n]!=="number"||Math.floor(numbers[n])!==numbers[n]||numbers[n]<0||numbers[n]>limits[n])return;
    if(!discovery.nodeId)return;
    var names=["Type","Orientation","Binary","Dimmable"];
    var lastName=prefix+"Recognition"+names[field]+"Last",previousName=prefix+"Recognition"+names[field]+"Previous";
    var last=IOHC_getParameter(device,lastName);
    if(!last)return; // older ETS products have no history parameters
    var record="v="+value+" n="+IOHC_formatNodeId(discovery.nodeId)+" p="+discovery.protocolType+"/"+discovery.subtype+" m="+discovery.manufacturer;
    if(record.length>40)return; // never store a truncated identity/provenance record
    if(String(last.value)===record)return;
    if(String(last.value).indexOf("v=")===0)IOHC_setParameterValue(device,previousName,String(last.value));
    IOHC_setParameterValue(device,lastName,record);
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
        IOHC_recordRecognitionAdoption(device,prefix,i,fields[i][2],discovery);
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
    IOHC_setParameterValue(device,"IOHC_c"+(channel+1)+"SyncStatus","ETS gesetzt; Gerätebeleg offen");
    var ack=[0x21,channel].concat(receipt.slice(3,10));
    var response=IOHC_invokeFunctionProperty(online,ack);
    if(!response || response[0]!==0) throw new Error("Zuordnung im Gerät gespeichert; ETS-Abgleich erneut ausführen.");
    IOHC_setParameterValue(device,"IOHC_c"+(channel+1)+"SyncStatus","Gerät+ETS gespeichert; Download offen");
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
    var historyNames=["Type","Orientation","Binary","Dimmable"];
    for(var h=0;h<historyNames.length;h++) {
        var last=IOHC_getParameter(device,prefix+"Recognition"+historyNames[h]+"Last");
        var previous=IOHC_getParameter(device,prefix+"Recognition"+historyNames[h]+"Previous");
        if(last&&String(last.value).indexOf("v=")===0) {
            details+=fields[h][2]+" zuletzt übernommen: "+last.value+". ";
            if(previous&&String(previous.value).indexOf("v=")===0)details+="Davor: "+previous.value+". ";
        }
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
function IOHC_sameRecognitionSnapshot(first,last) {
    if(!first||!last||first.length!==12||last.length!==12||first[0]!==0||first[1]!==1)return false;
    for(var i=0;i<12;i++)if(first[i]!==last[i])return false;
    return true;
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
        if(!IOHC_sameRecognitionSnapshot(identity,current)) throw new Error("Kanal während des Lesens geändert; erneut lesen");
        text+="Aktuelle Geräteevidenz; gespeicherte ETS-Übernahmen stehen im Vorgabenbericht. Keine Einstellungen übernommen und keine RF-Abfrage ausgelöst.";
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

        var preview=null,previewParameter=IOHC_getParameter(device,"IOHC_AssignmentPreviewToken");
        if(job&&previewParameter) {
            var chosen=IOHC_getParameter(device,"IOHC_ImportCandidate"),target=IOHC_getParameter(device,"IOHC_ImportTargetChannel");
            preview=IOHC_assignmentPreview(online,device,job,discoveries,channelCount,Number(chosen?chosen.value:0),Number(target?target.value:0));
            if(String(previewParameter.value)!==preview.token) {
                IOHC_setParameterValue(device,"IOHC_AssignmentPreviewToken",preview.token);
                progress.setText("Zuordnungsvorschau: "+preview.text+" Auswahl prüfen; erneut Weiter drücken, um genau diese Zuordnung zu speichern.");return;
            }
            if(!preview.plan.length){progress.setText("Keine Zuordnung: "+preview.text);return;}
        }
        var configuredChannels = 0;
        var alreadyConfigured = 0;
        var unassigned = 0;
        var assignmentResponse = preview ? IOHC_assignPreviewed(online,job,discoveries,preview) : job ? IOHC_assignFrozenCandidates(online,job,discoveries,channelCount) : IOHC_invokeFunctionProperty(online, assignmentRequest);
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
            if(!IOHC_sameRecognitionSnapshot(identity,after))
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
        if(!IOHC_sameRecognitionSnapshot(identity,current))
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

// Bounded explicit continuation; no background ETS polling/dialog API assumed.
function IOHC_continueCommissioning(device,online,progress,context) {
    var job=null;
    online.connect();try{job=IOHC_jobSnapshot(online);}finally{online.disconnect();}
    if(!job)throw new Error("Firmware unterstützt diese Einrichtung nicht");
    if(job.owner===2||job.owner===3) {
        IOHC_startKeyExtract(device,online,progress,{channelCount:context&&context.channelCount?context.channelCount:16});return;
    }
    if(job.owner===1&&job.channel<16&&job.stage===6&&job.node) {
        IOHC_applyPairingResult(device,online,progress,{channelIndex:job.channel+1});return;
    }
    if(job.owner===1&&job.channel<16&&job.stage>=6) {
        IOHC_refreshPairingInfo(device,online,progress,{channelIndex:job.channel+1});return;
    }
    IOHC_readCommissioningStatus(device,online,progress,context);
}

function IOHC_assignmentPreview(online,device,job,discoveries,count,choice,target) {
 if(count<1||count>16||choice<0||choice>discoveries.length||target<0||target>count||Math.floor(choice)!==choice||Math.floor(target)!==target)throw new Error("Ungültige Kandidaten-/Kanalauswahl");
 var channels=[],plan=[],seen={},text="",skipped=0;
 for(var c=0;c<count;c++) {
  var id=IOHC_invokeFunctionProperty(online,[0x1D,c]);
  if(!id||id.length!==12||id[0]!==0||id[1]!==1||id[2]!==c)throw new Error("Kanalzustand nicht verfügbar");
  var active=IOHC_getParameter(device,"IOHC_c"+(c+1)+"Active");
  channels.push({node:IOHC_readNodeId(id,3),used:!!(active&&Number(active.value)===1)});
 }
 var selected=choice?[discoveries[choice-1]]:discoveries;
 if(target&&selected.length!==1)throw new Error("Ein Zielkanal erfordert genau einen gewählten Kandidaten");
 for(var i=0;i<selected.length;i++) {
  var d=selected[i],supported=d.metadataValid&&IOHC_etsDeviceType(d.protocolType,d.subtype)!==0;
  if(!d.nodeId||seen[d.nodeId]){skipped++;continue;}seen[d.nodeId]=true;
  if(!d.passiveAuthVerified&&!d.directedVerified){skipped++;text+="Node "+IOHC_formatNodeId(d.nodeId)+": nicht verifiziert. ";continue;}
  if(!choice&&!supported){skipped++;text+="Node "+IOHC_formatNodeId(d.nodeId)+": manuelle Produktauswahl erforderlich. ";continue;}
  var existing=-1,channel=-1;
  for(var j=0;j<count;j++)if(channels[j].node===d.nodeId){existing=j;break;}
  if(existing>=0){channel=existing;if(target&&target-1!==existing)throw new Error("Node ist bereits einem anderen Kanal zugeordnet");}
  else if(target){channel=target-1;if(channels[channel].node||channels[channel].used)throw new Error("Gewählter Kanal ist belegt");}
  else for(var k=0;k<count;k++)if(!channels[k].node&&!channels[k].used){channel=k;break;}
  if(channel<0){skipped++;text+="Kein freier Kanal für "+IOHC_formatNodeId(d.nodeId)+". ";continue;}
  channels[channel].used=true;plan.push({index:d.index,node:d.nodeId,channel:channel,existing:existing>=0});
  text+=IOHC_formatNodeId(d.nodeId)+" → Kanal "+(channel+1)+(existing>=0?" (vorhanden)":" (neu)")+(!supported?" (manuelle Produktwahl)":"")+". ";
 }
 var serialized=JSON.stringify([choice,target,channels,plan]),hash=2166136261;
 for(var b=0;b<serialized.length;b++)hash=Math.imul(hash^serialized.charCodeAt(b),16777619)>>>0;
 var token=job.token.map(function(v){return ("0"+v.toString(16)).slice(-2);}).join("")+":"+("00000000"+hash.toString(16)).slice(-8);
 var current=IOHC_jobSnapshot(online);
 if(!current||JSON.stringify(current.token)!==JSON.stringify(job.token)||current.stage!==4||current.count!==job.count)throw new Error("Vorschau veraltet; erneut lesen");
 return {token:token,plan:plan,text:plan.length+" Zuordnungen; "+skipped+" ausgelassen. "+text};
}
function IOHC_assignPreviewed(online,job,discoveries,preview) {
 var result=[0,discoveries.length];for(var d=0;d<discoveries.length;d++)result.push(2,255);
 for(var i=0;i<preview.plan.length;i++) {
  var item=preview.plan[i],response=IOHC_invokeFunctionProperty(online,[0x26].concat(job.token,[item.index,item.channel],[(item.node>>16)&255,(item.node>>8)&255,item.node&255]));
  if(!response||response.length!==2||response[0]>1||response[1]!==item.channel)throw new Error("Vorgeprüfter Kanal geändert; gespeicherte Ergebnisse erneut lesen");
  result[2+item.index*2]=response[0];result[3+item.index*2]=response[1];
 }
 return result;
}

function IOHC_applyPairingResult(device,online,progress,context) {
 online.connect();try {
  var job=IOHC_jobSnapshot(online),channel=Number(context.channelIndex)-1;
  if(!job||job.owner!==1||job.stage!==6||job.channel!==channel||!job.node)throw new Error("Kein abgeschlossenes 2W-Pairing für diesen Kanal");
  var identity=IOHC_invokeFunctionProperty(online,[0x1D,channel]);
  if(!identity||identity.length!==12||identity[0]!==0||identity[1]!==1||identity[2]!==channel||IOHC_readNodeId(identity,3)!==job.node||(identity[11]&0x30)!==0x10)throw new Error("Pairing-Zuordnung geändert");
  if(!IOHC_resumeAssignment(device,online,channel))throw new Error("Kein passender gespeicherter Zuordnungsbeleg");
  IOHC_queryRecognition(device,online,context,true);
  progress.setText("Gespeicherte Zuordnung in ETS übernommen. Manuelle Vorgaben bleiben erhalten. Applikation programmieren; Download ist noch offen.");
 } finally {online.disconnect();}
}
function IOHC_recognitionConflicts(device,context,response) {
 var channel=Number(context.channelIndex)-1;
 if(!response||response.length!==14||response[0]!==0||response[1]!==1||response[2]!==channel||response[12]<1||response[12]>14||response[13]>7||(response[11]&0x34)!==0x14)throw new Error("Keine gültige gepaarte 2W-Erkennung");
 var prefix=IOHC_getChannelPrefix(context),names=["DeviceType","OrientationObjects","BinaryOnly","Dimmable"],labels=["Gerätetyp","Orientierung","Binärmodus","Dimmen"],suggested=[response[12],response[13]&1,(response[13]>>1)&1,(response[13]>>2)&1],conflicts=[];
 for(var i=0;i<names.length;i++) {var parameter=IOHC_getParameter(device,prefix+names[i]);if(parameter&&Number(parameter.value)!==suggested[i])conflicts.push(labels[i]+": ETS "+parameter.value+", Erkennung "+suggested[i]);}
 var override=IOHC_getParameter(device,prefix+"ProfileOverride");if(override&&Number(override.value)!==0)conflicts.push("Manuelles Profil "+override.value+" sperrt automatische Übernahme");
 return conflicts;
}
function IOHC_showRecognitionConflicts(device,online,progress,context) {
 online.connect();try {
  var request=[0x1E,Number(context.channelIndex)-1],first=IOHC_invokeFunctionProperty(online,request),conflicts=IOHC_recognitionConflicts(device,context,first),last=IOHC_invokeFunctionProperty(online,request);
  if(JSON.stringify(first)!==JSON.stringify(last))throw new Error("Erkennung während des Lesens geändert");
  progress.setText(conflicts.length?conflicts.join(". ")+". Vorgaben behalten oder automatische Übernahme vorbereiten; nichts geändert.":"ETS-Vorgaben stimmen mit der Erkennung überein; nichts geändert.");
 }finally{online.disconnect();}
}
function IOHC_keepRecognitionSettings(device,online,progress,context) {
 progress.setText("ETS-Vorgaben beibehalten. "+IOHC_effectiveSettingsText(device,context));
}
function IOHC_restoreAutomatic(device,online,progress,context) {
 var prefix=IOHC_getChannelPrefix(context),names=["ProfileOverride","RecognitionTypeAuto","RecognitionOrientationAuto","RecognitionBinaryAuto","RecognitionDimmableAuto"],parameters=[],values=[];
 for(var i=0;i<names.length;i++){var p=IOHC_getParameter(device,prefix+names[i]);if(!p)throw new Error("Automatische Vorgaben nicht verfügbar");parameters.push(p);values.push(Number(p.value));}
 var preview=IOHC_getParameter(device,prefix+"AutomaticResetPreview");if(!preview)throw new Error("Vorschaufunktion nicht verfügbar");
 var token=JSON.stringify(values);
 if(String(preview.value)!==token){preview.value=token;progress.setText("Vorschau: Profil-Override "+values[0]+" → 0; automatische Übernahme für Gerätetyp, Orientierung, Binärmodus und Dimmen erlauben. Erneut drücken zum Bestätigen. Pairing, Schlüssel, Zähler und übrige Expertenwerte bleiben erhalten.");return;}
 parameters[0].value=0;for(var j=1;j<parameters.length;j++)parameters[j].value=1;preview.value="";
 progress.setText("Automatische Übernahme vorbereitet. Erkennung bewusst übernehmen, danach Applikation programmieren. Pairing, Schlüssel und Zähler unverändert.");
}

// Diagnostic controls are ETS-only inputs: no automatic subscription on download.
function IOHC_boundDiagnostic(device,online,progress,context,operation) {
 var c=Number(context.channelIndex)-1;if(c<0||c>=16||Math.floor(c)!==c)throw new Error("Ungültiger Kanal");
 online.connect();try {
  var id=IOHC_invokeFunctionProperty(online,[0x1D,c]);
  if(!id||id.length!==12||id[0]!==0||id[1]!==1||id[2]!==c||(id[11]&0x14)!==0x14||(id[11]&0x20))throw new Error("Gepaarter 2W-Kanal erforderlich");
  return operation(c,id);
 }finally{online.disconnect();}
}
function IOHC_sensorAction(device,online,progress,context,action) {
 return IOHC_boundDiagnostic(device,online,progress,context,function(c,id){
  var prefix=IOHC_getChannelPrefix(context),tail=[];
  if(action===2){var text=String(IOHC_getParameter(device,prefix+"SensorBackbone").value);if(!/^[0-9a-fA-F]{6}$/.test(text)||parseInt(text,16)===0)throw new Error("Explizites sechsstelligen Backbone erforderlich");var b=parseInt(text,16);tail=[b>>16,(b>>8)&255,b&255];}
  if(action===3){var i=Number(IOHC_getParameter(device,prefix+"SensorPollInterval").value),d=Number(IOHC_getParameter(device,prefix+"SensorPollDuration").value);if(i<1||i>600||d<1||d>3600||Math.floor(i)!==i||Math.floor(d)!==d)throw new Error("Intervall 1..600, Dauer 1..3600 Sekunden");tail=[i>>8,i&255,d>>8,d&255];}
  var r=IOHC_invokeFunctionProperty(online,[0x35,c,action].concat(id.slice(3,6),tail));
  if(!r||r.length!==4||r[0]!==0||r[1]!==1||r[2]!==c||r[3]!==action)throw new Error("Aktion blockiert; laufende Einrichtung / Diagnose prüfen");
  progress.setText(action===2?"Default-Abonnement einmalig gestartet; Geräteakzeptanz noch zu prüfen. Physikalische Rohwerteinheit unbekannt.":action===4?"Abfrageplan gestoppt; bereits gesendete Anfrage bleibt bestehen.":"Anfrage gestartet; anschließend Evidenz lesen. Keine automatische Subscription / KNX-Publikation.");
 });
}
function IOHC_sensorInfo(d,o,p,c){return IOHC_sensorAction(d,o,p,c,1);}
function IOHC_sensorSubscribe(d,o,p,c){return IOHC_sensorAction(d,o,p,c,2);}
function IOHC_sensorPoll(d,o,p,c){return IOHC_sensorAction(d,o,p,c,3);}
function IOHC_sensorStop(d,o,p,c){return IOHC_sensorAction(d,o,p,c,4);}

function IOHC_priorityAction(device,online,progress,context,action){
 return IOHC_boundDiagnostic(device,online,progress,context,function(c,id){
  var level=Number(IOHC_getParameter(device,IOHC_getChannelPrefix(context)+"PriorityLevel").value);
  if(level<0||level>7||Math.floor(level)!==level)throw new Error("Prioritätsstufe 0..7 erforderlich");
  var r=IOHC_invokeFunctionProperty(online,[0x36,c,action,level].concat(id.slice(3,6)));
  if(!r||r.length!==4||r[0]!==0||r[1]!==1||r[2]!==c||r[3]!==action)throw new Error("Anfrage blockiert");
  progress.setText("Rohwertanfrage gestartet; anschließend Evidenz lesen. Antwortkorrelation ist keine Authentifizierung.");
 });
}
function IOHC_requestPriorityEvidence(d,o,p,c){return IOHC_priorityAction(d,o,p,c,1);}
function IOHC_requestSensorStatus(d,o,p,c){return IOHC_priorityAction(d,o,p,c,2);}
function IOHC_hexBytes(bytes){return bytes.map(function(v){return (v<16?"0":"")+v.toString(16).toUpperCase();}).join("");}
function IOHC_readSensorEvidence(device,online,progress,context){
 return IOHC_boundDiagnostic(device,online,progress,context,function(c,id){
  var level=Number(IOHC_getParameter(device,IOHC_getChannelPrefix(context)+"PriorityLevel").value);
  if(level<0||level>7||Math.floor(level)!==level)throw new Error("Prioritätsstufe 0..7 erforderlich");
  var priority=IOHC_invokeFunctionProperty(online,[0x29,c,level]),status=IOHC_invokeFunctionProperty(online,[0x2A,c]),info=IOHC_invokeFunctionProperty(online,[0x2B,c]);
  if(!priority||priority.length!==15||!status||status.length!==17||!info||info.length!==26||priority[0]||status[0]||info[0]||priority[1]!==1||status[1]!==1||info[1]!==1||priority[2]!==c||status[2]!==c||info[2]!==c||priority[3]!==level)throw new Error("Ungültige Evidenzantwort");
  var after=IOHC_invokeFunctionProperty(online,[0x1D,c]);if(!IOHC_sameRecognitionSnapshot(id,after))throw new Error("Kanal während Lesen geändert");
  var pvalid=priority[10]===1&&IOHC_readNodeId(priority,4)===IOHC_readNodeId(id,3),svalid=status[12]===1&&IOHC_readNodeId(status,3)===IOHC_readNodeId(id,3);
  var text="Priorität "+level+": gültig="+pvalid+", Alter="+IOHC_read32(priority,11)+" ms, Zeitcode="+priority[7]+", Originator="+priority[8]+", opaque="+priority[9]+". Sensor: gültig="+svalid+", Alter="+IOHC_read32(status,13)+" ms, Status="+status[6]+", Skalierungscode="+status[7]+", raw="+IOHC_hexBytes(status.slice(8,10))+", opaque="+status[10]+"/"+status[11]+". Sensorinfo: gültig="+(info[3]===1)+", Alter="+IOHC_read32(info,4)+" ms, raw="+IOHC_hexBytes(info.slice(8,25))+". Physikalische Einheit unbekannt; korrelierte Rohdaten.";
  IOHC_setParameterValue(device,IOHC_getChannelPrefix(context)+"DiagnosticEvidence","P="+pvalid+" S="+svalid+" raw="+IOHC_hexBytes(status.slice(8,10)));
  progress.setText(text);
 });
}

function IOHC_objectSummary(online,c,id){
 var s=IOHC_invokeFunctionProperty(online,[0x2C]);
 if(!s||s.length!==22||s[0]!==0||s[1]!==1||s[3]!==c||IOHC_readNodeId(s,8)!==IOHC_readNodeId(id,3)||IOHC_read32(s,4)===0)throw new Error("Kein passender Objektlesevorgang");
 return s;
}
function IOHC_objectToken(s){return IOHC_hexBytes(s.slice(18,22).concat(s.slice(4,8),s.slice(8,11)));}
function IOHC_objectAction(device,online,progress,context,action){
 return IOHC_boundDiagnostic(device,online,progress,context,function(c,id){
  var prefix=IOHC_getChannelPrefix(context),summary,r;
  if(action===1){
   var p=String(IOHC_getParameter(device,prefix+"ObjectProvider").value),k=String(IOHC_getParameter(device,prefix+"ObjectKey").value),offset=Number(IOHC_getParameter(device,prefix+"ObjectOffset").value),span=Number(IOHC_getParameter(device,prefix+"ObjectSpan").value);
   if(!/^[0-9a-fA-F]{2}$/.test(p)||! /^[0-9a-fA-F]{4}$/.test(k)||offset<0||offset>65535||span<1||span>1024||offset+span>65536||Math.floor(offset)!==offset||Math.floor(span)!==span)throw new Error("Ungültige Objektparameter");
   p=parseInt(p,16);k=parseInt(k,16);
   r=IOHC_invokeFunctionProperty(online,[0x37,c,p,k>>8,k&255,offset>>8,offset&255,span>>8,span&255].concat(id.slice(3,6)));
   if(!r||r.length!==3||r[0]!==0||r[1]!==1||r[2]!==c)throw new Error("Objektlesen blockiert / Key nicht erlaubt");
   summary=IOHC_objectSummary(online,c,id);IOHC_setParameterValue(device,prefix+"ObjectReadToken",IOHC_objectToken(summary));
  }else{
   summary=IOHC_objectSummary(online,c,id);
   if(IOHC_getParameter(device,prefix+"ObjectReadToken").value!==IOHC_objectToken(summary))throw new Error("Vorgang geändert; zuerst passenden Lesevorgang starten");
   if(action===3){r=IOHC_invokeFunctionProperty(online,[0x38].concat(summary.slice(18,22),summary.slice(4,8)));if(!r||r[0]!==0)throw new Error("Abbruch blockiert");progress.setText("Objektlesen abgebrochen; bereits übertragene Bytes bleiben gesendet.");return;}
   if(action===4){var at=Number(IOHC_getParameter(device,prefix+"ObjectSliceOffset").value);if(at<0||at>65535||Math.floor(at)!==at)throw new Error("Ungültiger Rückleseoffset");
    r=IOHC_invokeFunctionProperty(online,[0x2D].concat(summary.slice(18,22),summary.slice(4,8),[at>>8,at&255,16]));
    if(!r||r.length<10||r[0]!==0||r[1]!==1||r[8]>16||r.length!==10+r[8]||r[9]!==2||IOHC_hexBytes(r.slice(2,6))!==IOHC_hexBytes(summary.slice(4,8))||r[6]!==at>>8||r[7]!==(at&255))throw new Error("Bytes noch nicht abgeschlossen / Identität ungültig");
    progress.setText("Offset "+at+": "+IOHC_hexBytes(r.slice(10))+"; opake korrelierte Metadatenbytes, keine Schreibfreigabe.");return;
   }
  }
  progress.setText("Objektlesen: Stufe="+summary[2]+", Disposition="+summary[11]+", übertragen="+((summary[14]<<8)|summary[15])+", Identität gültig="+summary[16]+". Status erneut lesen; keine automatische Fortsetzung nach Neustart.");
 });
}
function IOHC_objectStart(d,o,p,c){return IOHC_objectAction(d,o,p,c,1);}
function IOHC_objectStatus(d,o,p,c){return IOHC_objectAction(d,o,p,c,2);}
function IOHC_objectCancel(d,o,p,c){return IOHC_objectAction(d,o,p,c,3);}
function IOHC_objectSlice(d,o,p,c){return IOHC_objectAction(d,o,p,c,4);}

// BEGIN GENERATED SEMANTICS
var IOHC_SEMANTICS=[{"product":"heatpump","index":0,"meaning":"temperature setpoint","unit":"C","conversion":"raw*120/51200-40","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"heatpump","index":8,"meaning":"measured temperature","unit":"C","conversion":"round(raw*120/51200-40)","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"generic-heater","index":12,"meaning":"comfort setpoint","unit":"C","conversion":"validated device minimum/maximum interpolation","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"generic-heater","index":13,"meaning":"setback setpoint","unit":"C","conversion":"comfortRaw-raw before device bounds interpolation","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"atlantic-heater","index":12,"meaning":"comfort setpoint","unit":"C","conversion":"7..28 C interpolation","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"atlantic-heater","index":13,"meaning":"numeric setback value","unit":"unknown","conversion":"2..9 numeric interpolation; physical meaning unqualified","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"atlantic-dhw-ck","index":0,"meaning":"temperature","unit":"C","conversion":"raw/100-273.15","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"atlantic-dhw-v2","index":0,"meaning":"temperature setpoint","unit":"C","conversion":"device bounds interpolation; nominal 20..62 C","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"heating-interface","index":0,"meaning":"temperature setpoint","unit":"C","conversion":"validated device bounds interpolation","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"pergola","index":0,"meaning":"slat orientation","unit":"percent","conversion":"raw/512","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"pergola","index":1,"meaning":"movement speed","unit":"percent","conversion":"raw/512","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"dual-shutter","index":1,"meaning":"upper closure","unit":"percent","conversion":"raw/512","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"dual-shutter","index":2,"meaning":"lower closure","unit":"percent","conversion":"raw/512","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"alarm","index":0,"meaning":"armed zone set","unit":"zone mask","conversion":"special word lookup only","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"sliding-lock","index":9,"meaning":"lock state","unit":"enum","conversion":"C800 locked; other values opaque","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"atlantic-ventilation","index":16,"meaning":"ventilation mode","unit":"enum","conversion":"FC00 standard; FC01 comfort; FC02 eco","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"atlantic-dhw","index":16,"meaning":"absence/relaunch modes","unit":"enum","conversion":"4600 absence; 4A00 normal; 4900 relaunch; 4000 opaque","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"heatpump","index":16,"meaning":"global/mode bits","unit":"enum","conversion":"on/off pair semantics scoped to heatpump","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"heating-interface","index":0,"meaning":"heating level","unit":"enum","conversion":"FC00/01/02/03/04/05/07/3F lookup","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"siren","index":9,"meaning":"sequence 1 sound","unit":"packed","conversion":"100 ms duration low11; duty upper5","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"rgb","index":10,"meaning":"red and green","unit":"byte pair","conversion":"red high8; green low8","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"rgb","index":11,"meaning":"blue","unit":"byte","conversion":"blue low8; upper byte reserved","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"white","index":14,"meaning":"white color temperature","unit":"K","conversion":"2700..6500 K interpolation","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"heatpump","index":15,"meaning":"mode capabilities","unit":"bitmask","conversion":"bits0..4 heating/cooling/away/pool/DHW","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"atlantic-dhw","index":15,"meaning":"DHW capabilities","unit":"bitmask","conversion":"bits0/2/3/15 rate/absence/relaunch/energy demand","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"siren","index":10,"meaning":"sequence 1 options","unit":"packed","conversion":"repetitions low10; volume bits10..12; visual bits13..15","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"siren","index":11,"meaning":"sequence 2 sound","unit":"packed","conversion":"duration/duty","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"siren","index":12,"meaning":"sequence 2 options","unit":"packed","conversion":"repetitions low10; volume bits10..12; visual bits13..15","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"siren","index":13,"meaning":"sequence 3 sound","unit":"packed","conversion":"duration/duty","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false},{"product":"siren","index":14,"meaning":"sequence 3 options","unit":"packed","conversion":"repetitions low10; volume bits10..12; visual bits13..15","specials":"Special/unknown words require product-specific handling","source":"FIRMWARE-DUMP-ANALYSIS-2026-10-04.md: OVPd product definitions","rfWrite":false,"knxPublish":false,"automaticBinding":false}];
// END GENERATED SEMANTICS

function IOHC_showSemanticHelp(device,online,progress,context){
 var name=String(IOHC_getParameter(device,IOHC_getChannelPrefix(context)+"SemanticProduct").value),rows=IOHC_SEMANTICS.filter(function(r){return r.product===name;});
 if(!rows.length)throw new Error("Keine bekannte Produktdefinition. Beispiele: heatpump, generic-heater, pergola, alarm, siren. Keine automatische Variantenbindung.");
 progress.setText(rows.map(function(r){return (r.index?"FP"+r.index:"MP")+": "+r.meaning+" ["+r.unit+"], "+r.conversion+"; "+r.specials;}).join(". ")+". Diagnoseauswahl erteilt keine RF-Schreib- oder KNX-Publikationsfreigabe.");
}
