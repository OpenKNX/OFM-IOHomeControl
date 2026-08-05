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
        return "Starte Key-Transfer";
    case 11:
        return "Warte Key-Transfer";
    case 12:
        return "1W Challenge senden";
    case 13:
        return "Warte 1W Challenge";
    case 14:
        return "1W Remove senden";
    case 15:
        return "Warte 1W Remove";
    case 16:
        return "1W Key senden";
    case 17:
        return "Warte 1W Key";
    case 18:
        return "Key init senden";
    case 19:
        return "Warte Challenge";
    case 20:
        return "Key senden";
    case 21:
        return "Key-Antwort senden";
    case 22:
        return "Warte Key-Bestaetigung";
    case 23:
        return "SetConfig1 senden";
    case 24:
        return "Warte SetConfig1";
    case 25:
        return "SetConfig1 Auth senden";
    case 26:
        return "Warte SetConfig1 final";
    case 27:
        return "Pairing abgeschlossen";
    case 28:
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
        return paired ? "1W Typ " + typeText + ", gepaart " + IOHC_formatNodeId(pairedNodeId) : "1W Typ " + typeText + ", Ziel fehlt";
    }

    var targetText = IOHC_formatNodeId(targetNodeId);
    if (!paired) {
        return "1W Typ " + typeText + ", Ziel " + targetText + ", nicht angelernt";
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
    var nodeText = paired ? IOHC_formatNodeId(pairedNodeId) : "nicht angelernt";
    var resultText = fallbackResult;
    var diagText = fallbackDiag;

    if (!resultText) {
        if (paired) {
            resultText = "Angelernt";
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
        } else if (lastStartStatus == 2) {
            diagText = "1W Ziel-Node-ID fehlt";
        } else {
            diagText = paired ? "Gerät ist angelernt" : "Kein aktiver Pairing-Vorgang";
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
    var visibleChannelCount = IOHC_getParameter(device, IOHC_getGlobalPrefix() + "VisibleChannels").value;
    if (!visibleChannelCount || visibleChannelCount < 1) {
        throw new Error("io-homecontrol: Keine sichtbaren Kanäle für die Pairing-Übersicht konfiguriert");
    }

    progress.setText("Pairing-Übersicht wird für " + visibleChannelCount + " Kanäle aktualisiert ...");
    progress.setProgress(5);
    // Read each channel in its own connect/disconnect cycle, exactly like the
    // per-channel "Pairing-Status auslesen" button. Holding a single connection
    // open across all reads does not work reliably; only the first read succeeds.
    for (var channelIndex = 1; channelIndex <= visibleChannelCount; channelIndex++) {
        online.connect();
        try {
            IOHC_queryPairingInfo(device, online, null, { channelIndex: channelIndex }, "Status gelesen", "Status über Übersicht gelesen");
        } finally {
            online.disconnect();
        }
        progress.setText("Pairing-Übersicht: Kanal " + channelIndex + " von " + visibleChannelCount + " aktualisiert ...");
        progress.setProgress(Math.floor((channelIndex * 100) / visibleChannelCount));
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

        if (resp[0] == 3) {
            IOHC_setPairingInfo(device, context, "Start abgelehnt", "nicht angelernt", IOHC_buildOneWaySummary(device, context, false, 0), "1W Ziel-Node-ID fehlt");
            throw new Error("io-homecontrol: 1W Pairing benötigt eine Ziel-Node-ID im ETS-Parameter");
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
    progress.setProgress(20);
    online.connect();
    try {
        var resp = IOHC_invokeFunctionProperty(online, [0x17]);
        if (!resp || resp.length < 1) {
            throw new Error("io-homecontrol: Keine Antwort beim Start der 2W-Schlüsselextraktion");
        }
        if (resp[0] == 0) {
            progress.setText("2W-Schlüsselextraktion aktiv. Jetzt am Fremd-Gateway 'Gerät hinzufügen' starten.");
            progress.setProgress(100);
            return;
        }
        throw new Error("io-homecontrol: 2W-Schlüsselextraktion ist gerade blockiert");
    } finally {
        online.disconnect();
    }
}
