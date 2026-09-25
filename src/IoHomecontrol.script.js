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

function IOHC_etsDeviceType(protocolType) {
    switch (protocolType) {
    case 0x01: // Venetian blind
    case 0x02: // Roller shutter
    case 0x0A: // Blind
    case 0x0D: // Dual shutter
    case 0x11: // External Venetian blind
    case 0x12: // Louvre blind
    case 0x18: // Swinging shutter
        return 1;
    case 0x04:
        return 2;
    case 0x03:
        return 3;
    case 0x05:
    case 0x08:
        return 4;
    case 0x0E:
    case 0x15:
    case 0x16:
        return 5;
    case 0x06:
        return 6;
    case 0x07:
        return 7;
    case 0x09:
        return 8;
    case 0x10:
        return 9;
    case 0x13:
        return 10;
    case 0x14:
        return 11;
    case 0x0F:
        return 12;
    default:
        return 0;
    }
}

function IOHC_importDeviceLabel(etsType) {
    switch (etsType) {
    case 1: return "Rollladen/Jalousie";
    case 2: return "Fenster";
    case 3: return "Markise";
    case 4: return "Garagentor";
    case 5: return "Thermostat";
    case 6: return "Licht";
    case 7: return "Tor";
    case 8: return "Schloss";
    case 9: return "Sonnenschutz";
    case 10: return "Vorhangschiene";
    case 11: return "Lüftung";
    case 12: return "Schalter";
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

function IOHC_configureImportedChannel(device, channelNumber, discovery) {
    var prefix = "IOHC_c" + channelNumber;
    var etsType = IOHC_etsDeviceType(discovery.protocolType);
    IOHC_setParameterValue(device, prefix + "Name",
                           IOHC_importDeviceLabel(etsType) + " " + IOHC_formatNodeId(discovery.nodeId));
    IOHC_setParameterValue(device, prefix + "ProtocolMode", 0);
    IOHC_setParameterValue(device, prefix + "DeviceType", etsType);
    IOHC_setParameterValue(device, prefix + "TwoWayPowerClass", discovery.powerClass);
    IOHC_setParameterValue(device, prefix + "Suspend", 0);
    IOHC_setParameterValue(device, prefix + "PairingLastResult", "Automatisch importiert");
    IOHC_setParameterValue(device, prefix + "PairedNodeIdDisplay", IOHC_formatNodeId(discovery.nodeId));
    IOHC_setParameterValue(device, prefix + "OneWaySummary", "2W (bidirektional)");
    IOHC_setParameterValue(device, prefix + "PairingDiag", "Programmierung erforderlich");
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
            var verificationCompleted = (status[25] || 0) != 0;
            IOHC_setExtractionResult(device,
                                     verificationCompleted
                                         ? "Schlüssel extrahiert; Gateway-Prüfung erfolgreich"
                                         : "Schlüssel extrahiert; Prüfung läuft",
                                     []);
            progress.setText("2W-Schlüssel erfolgreich extrahiert (Gateway-/System-Node-ID " +
                             IOHC_formatNodeId(hubNode) + ", temporäre Extraction-Device-ID " +
                             IOHC_formatNodeId(extractionNode) + "). " +
                             (verificationCompleted
                                  ? "Die Gateway-Prüfung wurde erfolgreich beantwortet. "
                                  : "Warte auf die Abschlussprüfung des Fremd-Gateways. ") +
                             "Diese Schaltfläche danach erneut drücken.");
            progress.setProgress(100);
            return;
        }
        if (phase == 3) {
            var gatewayVerificationCompleted = (status[25] || 0) != 0;
            IOHC_setExtractionResult(device,
                                     gatewayVerificationCompleted
                                         ? "Schlüssel extrahiert; Gateway-Prüfung erfolgreich"
                                         : "Schlüssel extrahiert; Gateway-Prüfung nicht vollständig beobachtet",
                                     []);
            progress.setText("2W-Schlüsselextraktion abgeschlossen. " +
                             (gatewayVerificationCompleted
                                  ? "Die Gateway-Prüfung wurde erfolgreich beantwortet. "
                                  : "Die Gateway-Prüfung wurde nicht vollständig beobachtet. ") +
                             "Die authentifizierte Gerätesuche läuft; diese Schaltfläche danach erneut drücken.");
            progress.setProgress(100);
            return;
        }
        if (phase != 4) {
            throw new Error("io-homecontrol: Unbekannter Status der 2W-Schlüsselübernahme");
        }

        var resultCount = status[8] || 0;
        var overflow = (status[10] || 0) != 0;
        var discoveries = [];
        var nodeIds = [];
        for (var resultIndex = 0; resultIndex < resultCount; resultIndex++) {
            var found = IOHC_invokeFunctionProperty(online, [0x19, resultIndex]);
            if (!found || found.length < 11 || found[0] != 0) {
                throw new Error("io-homecontrol: Scan-Ergebnis " + (resultIndex + 1) + " konnte nicht gelesen werden");
            }
            var discovery = {
                index: resultIndex,
                nodeId: IOHC_readNodeId(found, 3),
                protocolType: (found[6] || 0) | ((found[7] || 0) << 8),
                subtype: found[8] || 0,
                manufacturer: found[9] || 0,
                powerClass: found[10] || 0
            };
            discoveries.push(discovery);
            nodeIds.push(discovery.nodeId);
        }

        var channelCount = context && context.channelCount ? context.channelCount : 16;
        var channelActive = [];
        var freeChannels = [];
        for (var channelIndex = 0; channelIndex < channelCount; channelIndex++) {
            var activeParameter = IOHC_getParameter(device, "IOHC_c" + (channelIndex + 1) + "Active");
            var isActive = activeParameter && Number(activeParameter.value) == 1;
            channelActive[channelIndex] = isActive;
            var channelStatus = IOHC_invokeFunctionProperty(online, [0x12, channelIndex]);
            if (!channelStatus || channelStatus.length < 4) {
                throw new Error("io-homecontrol: Kanalstatus " + (channelIndex + 1) + " konnte nicht gelesen werden");
            }
            var runtimeUnused = (channelStatus[0] || 0) == 0 && IOHC_readNodeId(channelStatus, 1) == 0;
            if (runtimeUnused) {
                freeChannels.push(channelIndex);
            }
        }

        var configuredChannels = 0;
        var alreadyConfigured = 0;
        var unassigned = 0;
        var unusableChannels = {};
        for (var d = 0; d < discoveries.length; d++) {
            var assigned = false;
            while (freeChannels.length > 0 && !assigned) {
                var targetChannel = freeChannels.shift();
                if (unusableChannels[targetChannel]) {
                    continue;
                }
                var assignedResp = IOHC_invokeFunctionProperty(online, [0x1A, discoveries[d].index, targetChannel]);
                if (!assignedResp || assignedResp.length < 2) {
                    throw new Error("io-homecontrol: Keine Antwort beim Zuordnen von " + IOHC_formatNodeId(discoveries[d].nodeId));
                }
                if (assignedResp[0] == 0) {
                    IOHC_configureImportedChannel(device, targetChannel + 1, discoveries[d]);
                    channelActive[targetChannel] = true;
                    configuredChannels++;
                    assigned = true;
                } else if (assignedResp[0] == 1) {
                    var existingChannel = assignedResp[1];
                    if (existingChannel < channelCount && !channelActive[existingChannel]) {
                        IOHC_configureImportedChannel(device, existingChannel + 1, discoveries[d]);
                        channelActive[existingChannel] = true;
                        unusableChannels[existingChannel] = true;
                        configuredChannels++;
                    } else {
                        alreadyConfigured++;
                    }
                    // The proposed target was untouched because the node was
                    // already stored elsewhere, so it remains available.
                    freeChannels.push(targetChannel);
                    assigned = true;
                } else if (assignedResp[0] == 2) {
                    unusableChannels[targetChannel] = true;
                } else {
                    throw new Error("io-homecontrol: Scan-Ergebnis " + (d + 1) + " ist nicht mehr verfügbar");
                }
            }
            if (!assigned) {
                unassigned++;
            }
            progress.setProgress(80 + Math.floor(((d + 1) * 15) / Math.max(1, discoveries.length)));
        }

        var saveResp = IOHC_invokeFunctionProperty(online, [0x1B]);
        if (!saveResp || saveResp.length < 1 || saveResp[0] != 0) {
            throw new Error("io-homecontrol: Schlüsselübernahme konnte nicht abgeschlossen werden");
        }

        var nodeText = nodeIds.length ? nodeIds.map(IOHC_formatNodeId).join(", ") : "keine";
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
