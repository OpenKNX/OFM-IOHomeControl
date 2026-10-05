// Execute after IoHomecontrol.script.js (Node or QuickJS); no ETS installation needed.
var testsPassed = 0;
function check(condition, message) {
    if (!condition) throw new Error(message);
}
function deviceWith(values) {
    var params = {};
    var names = ["Name", "Active", "DeviceType", "ChannelSelection", "OrientationObjects",
                 "BinaryOnly", "Dimmable", "ProfileOverride", "TwoWayPowerClass", "Suspend",
                 "ProtocolMode", "PairingLastResult", "PairedNodeIdDisplay", "OneWaySummary",
                 "PairingDiag", "ImportedProfile", "ImportedManufacturer", "TwoWayDiscoveryCommand"];
    for (var i = 0; i < names.length; i++) {
        params["IOHC_c1" + names[i]] = {value: values[names[i]] === undefined ? 0 : values[names[i]]};
    }
    return {params: params, getParameterByName: function(name) { return params[name]; }};
}
function value(device, name) { return device.params["IOHC_c1" + name].value; }
function snapshot(flags, profile, subtype) {
    return [0, 1, 0, 0x12, 0x34, 0x56, profile & 255, profile >> 8, subtype, 2, 2, flags];
}
function onlineWith(response) {
    return {invokeFunctionProperty: function(object, property, data) {
        check(object === 160 && property === 10 && (data[0] === 0x1D || data[0] === 0x1E) && data[1] === 0, "snapshot request ABI");
        if (data[0] === 0x1E) return [3];
        return response;
    }};
}
function test(name, run) { run(); testsPassed++; }

test("read-only snapshot preserves channel settings", function() {
    var d = deviceWith({Name: "Office", DeviceType: 7, ProfileOverride: 0, TwoWayPowerClass: 1, Suspend: 1});
    check(!IOHC_queryRecognition(d, onlineWith(snapshot(0x1C, 1, 0)), {channelIndex: 1}, false), "read must not apply");
    check(value(d, "DeviceType") === 7 && value(d, "Name") === "Office", "read mutated settings");
    check(value(d, "ImportedProfile") === "Profil 1/0", "profile display");
});
test("explicit apply configures known profile and preserves expert values", function() {
    var d = deviceWith({Name: "Office", DeviceType: 7, TwoWayPowerClass: 1, Suspend: 1, TwoWayDiscoveryCommand: 2});
    check(IOHC_queryRecognition(d, onlineWith(snapshot(0x1C, 1, 0)), {channelIndex: 1}, true), "known profile not applied");
    check(value(d, "DeviceType") === 1 && value(d, "OrientationObjects") === 1, "blind recognition");
    check(value(d, "Name") === "Office" && value(d, "TwoWayPowerClass") === 1 && value(d, "Suspend") === 1 && value(d, "TwoWayDiscoveryCommand") === 2, "expert/name reset");
});
test("manual profile blocks automatic category changes", function() {
    var d = deviceWith({DeviceType: 7, ProfileOverride: 448});
    check(!IOHC_queryRecognition(d, onlineWith(snapshot(0x1C, 1, 0)), {channelIndex: 1}, true), "override ignored");
    check(value(d, "DeviceType") === 7 && value(d, "ProfileOverride") === 448, "override reset");
});
test("unknown, incomplete, unpaired and one-way snapshots cannot apply", function() {
    var responses = [snapshot(0x10, 1, 0), snapshot(0x0C, 1, 0), snapshot(0x3C, 1, 0), snapshot(0x1C, 999, 0)];
    for (var i = 0; i < responses.length; i++) {
        var d = deviceWith({DeviceType: 7});
        check(!IOHC_queryRecognition(d, onlineWith(responses[i]), {channelIndex: 1}, true), "unsafe apply " + i);
        check(value(d, "DeviceType") === 7, "fallback erased manual type");
    }
});
test("old firmware and malformed schema keep current settings", function() {
    var responses = [[0], [3], snapshot(0x1C, 1, 0), snapshot(0x1C, 1, 0)];
    responses[2][1] = 2;
    responses[3][2] = 1;
    for (var i = 0; i < responses.length; i++) {
        var d = deviceWith({DeviceType: 7});
        check(!IOHC_queryRecognition(d, onlineWith(responses[i]), {channelIndex: 1}, true), "bad API accepted");
        check(value(d, "DeviceType") === 7, "bad API changed settings");
    }
});
test("repeated import preserves name, manual profile, power and suspend", function() {
    var d = deviceWith({Name: "Garage", DeviceType: 7, ProfileOverride: 448, TwoWayPowerClass: 1, Suspend: 1});
    var discovery = {nodeId: 0x123456, metadataValid: true, protocolType: 1, subtype: 0, manufacturer: 2, powerClass: 2};
    IOHC_configureImportedChannel(d, 1, discovery);
    IOHC_configureImportedChannel(d, 1, discovery);
    check(value(d, "Name") === "Garage" && value(d, "ProfileOverride") === 448 && value(d, "DeviceType") === 7, "reimport reset manual data");
    check(value(d, "TwoWayPowerClass") === 1 && value(d, "Suspend") === 1, "reimport reset expert values");
});
test("binary-only profile has no percentage mode", function() {
    var d = deviceWith({});
    check(IOHC_queryRecognition(d, onlineWith(snapshot(0x1C, 6, 58)), {channelIndex: 1}, true), "binary profile");
    check(value(d, "BinaryOnly") === 1 && value(d, "Dimmable") === 0 && value(d, "OrientationObjects") === 0, "binary controls");
});

test("firmware presentation takes precedence over legacy fallback", function() {
    var d=deviceWith({});
    var response=snapshot(0x1C,999,0).concat([6,4]);
    var online={invokeFunctionProperty:function(o,p,data) { check(data[0]===0x1E,"extended snapshot");return response; }};
    check(IOHC_queryRecognition(d,online,{channelIndex:1},true),"firmware resolver ignored");
    check(value(d,"DeviceType")===6 && value(d,"Dimmable")===1,"firmware hints");
});
