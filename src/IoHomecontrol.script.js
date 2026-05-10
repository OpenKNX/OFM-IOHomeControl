// OFM-IO-Homecontrol -- OpenKNX --
// ETS JavaScript for io-homecontrol pairing workflow
// Uses function properties (objectIndex=160, propertyId=4) for device communication

function IOHC_getChannelPrefix(context) {
    return "IOHC_IOHC" + context.channelIndex;
}

function IOHC_getGlobalPrefix() {
    return "IOHC_IOHC";
}

function IOHC_getParameter(device, name) {
    return device.getParameterByName(name);
}

function IOHC_setParameterValue(device, name, value) {
    var parameter = IOHC_getParameter(device, name);
    if (parameter) {
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

    var targetNodeId = IOHC_getParameter(device, prefix + "OneWayTargetNodeId").value;
    if (!targetNodeId) {
        return paired ? "1W ohne Ziel, gepaart " + IOHC_formatNodeId(pairedNodeId) : "1W Ziel fehlt";
    }

    var targetText = IOHC_formatNodeId(targetNodeId);
    if (!paired) {
        return "1W Ziel " + targetText + ", nicht angelernt";
    }

    var pairedText = IOHC_formatNodeId(pairedNodeId);
    if (pairedNodeId === targetNodeId) {
        return "1W Ziel " + targetText + " aktiv";
    }

    return "1W Ziel " + targetText + ", gepaart " + pairedText;
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

    IOHC_setPairingInfo(device, context, resultText, nodeText, IOHC_buildOneWaySummary(device, context, paired, pairedNodeId), diagText);
}

function IOHC_queryPairingInfo(device, online, progress, context, fallbackResult, fallbackDiag) {
    var channelIndex = context.channelIndex - 1;
    var data = [0x12];
    data = data.concat(channelIndex);
    var resp = online.invokeFunctionProperty(160, 4, data);
    if (!resp || resp.length < 4) {
        throw new Error("io-homecontrol: Pairing-Status konnte nicht gelesen werden");
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
    online.connect();
    try {
        for (var channelIndex = 1; channelIndex <= visibleChannelCount; channelIndex++) {
            IOHC_queryPairingInfo(device, online, null, { channelIndex: channelIndex }, "Status gelesen", "Status über Übersicht gelesen");
            progress.setText("Pairing-Übersicht: Kanal " + channelIndex + " von " + visibleChannelCount + " aktualisiert ...");
            progress.setProgress(Math.floor((channelIndex * 100) / visibleChannelCount));
        }
        IOHC_setParameterValue(device, IOHC_getGlobalPrefix() + "OverviewLastRefresh", IOHC_nowText());
    } finally {
        online.disconnect();
    }

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
    progress.setText("Pairing wird gestartet für Kanal " + (channelIndex + 1) + " ...");
    progress.setProgress(10);
    online.connect();
    try {
        var data = [0x10];
        data = data.concat(channelIndex);
        var resp = online.invokeFunctionProperty(160, 4, data);

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
        var resp = online.invokeFunctionProperty(160, 4, data);

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
