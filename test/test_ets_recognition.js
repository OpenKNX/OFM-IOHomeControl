// Execute after IoHomecontrol.script.js (Node or QuickJS); no ETS installation needed.
var testsPassed = 0;
function check(condition, message) {
    if (!condition) throw new Error(message);
}
function deviceWith(values) {
    var params = {};
    var names = ["Name", "Active", "DeviceType", "ChannelSelection", "OrientationObjects",
                 "BinaryOnly", "Dimmable", "ProfileOverride", "TwoWayPowerClass", "Suspend",
                 "RecognitionTypeAuto", "RecognitionOrientationAuto", "RecognitionBinaryAuto", "RecognitionDimmableAuto", "ProtocolMode", "PairingLastResult", "PairedNodeIdDisplay", "OneWaySummary",
                 "PairingDiag", "ImportedProfile", "ImportedManufacturer", "TwoWayDiscoveryCommand"];
    for (var i = 0; i < names.length; i++) {
        params["IOHC_c1" + names[i]] = {value: values[names[i]] === undefined ? (names[i].indexOf("Recognition")===0 ? 1:0) : values[names[i]]};
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

test("per-field manual choices survive recognition", function() {
    var d=deviceWith({RecognitionOrientationAuto:0,RecognitionDimmableAuto:0,OrientationObjects:1,Dimmable:1});
    check(IOHC_queryRecognition(d,onlineWith(snapshot(0x1C,6,58)),{channelIndex:1},true),"allowed fields");
    check(value(d,"OrientationObjects")===1 && value(d,"Dimmable")===1,"manual fields overwritten");
    check(value(d,"BinaryOnly")===1,"allowed binary field not applied");
});

test("assignment resume acknowledges only after project application",function() {
    var d=deviceWith({Name:"Office",RecognitionTypeAuto:0,DeviceType:7});var ack=false;
    var receipt=[0,1,0,0,0,0,8,0x12,0x34,0x56,6,0,58,1,2,0];
    var online={invokeFunctionProperty:function(o,p,data) {
        if(data[0]===0x1D)return snapshot(6,58,2);
        if(data.length===2)return receipt;
        check(value(d,"Active")===1 && value(d,"Name")==="Office" && value(d,"DeviceType")===7,"manual/project state");
        check(data.length===9 && data[5]===8 && data[6]===0x12,"receipt identity ACK");ack=true;return receipt;
    }};
    check(IOHC_resumeAssignment(d,online,0) && ack,"resume failed");
});

test("stale assignment receipt does not mutate project",function() {
    var d=deviceWith({Name:"Office",Active:0});
    var receipt=[0,1,0,0,0,0,8,0x65,0x43,0x21,6,0,58,1,2,0];
    var online={invokeFunctionProperty:function(o,p,data){return data[0]===0x21?receipt:snapshot(6,58,2);}};
    check(!IOHC_resumeAssignment(d,online,0),"stale receipt accepted");
    check(value(d,"Active")===0,"stale receipt changed project");
});

test("job snapshot rejects changed boot identity",function() {
    var caps=[0,1,0,0,0,15,0,0,0,9],job=new Array(26).fill(0);job[1]=1;job[23]=10;
    var online={invokeFunctionProperty:function(o,p,data){return data[0]===0x23?caps:job;}};
    var rejected=false;try{IOHC_jobSnapshot(online);}catch(e){rejected=true;}check(rejected,"changed boot accepted");
});
test("frozen assignment carries identity and stops on stale token",function() {
    var seen=0;var job={token:[0,0,0,9,0,0,0,2,0,0,0,1]};
    var online={invokeFunctionProperty:function(o,p,data){seen++;check(data.length===18&&data[0]===0x26&&data[13]===0&&data[15]===0x12,"frozen request");return [4];}};
    var rejected=false;try{IOHC_assignFrozenCandidates(online,job,[{index:0,nodeId:0x123456}],2);}catch(e){rejected=true;}
    check(rejected&&seen===1,"stale assignment retried");
});

test("commissioning status displays frozen candidates without project mutation",function() {
    var d=deviceWith({Name:"Office",ProfileOverride:448}),before=JSON.stringify(d.params),text="",disconnected=false;
    var caps=[0,1,0,0,0,15,0,0,0,9],job=new Array(26).fill(0);job[1]=1;job[2]=2;job[3]=4;job[7]=2;job[8]=255;job[15]=1;job[23]=9;job[25]=3;
    var online={connect:function(){},disconnect:function(){disconnected=true;},invokeFunctionProperty:function(o,p,data){
        if(data[0]===0x23)return caps;if(data[0]===0x24)return job;
        check(data[0]===0x25&&data.length===14,"frozen candidate request");
        return [0,1,data[13],0x12,0x34,0x56,6,0,1,2,1,0x1C,0,0,0,2,0,0,0,1];
    }};
    IOHC_readCommissioningStatus(d,online,{setText:function(v){text=v;}},{});
    check(text.indexOf("Kandidaten 3")>=0&&text.indexOf("Ergebnisrevision 1")>=0&&text.indexOf("gerichtet verifiziert")>=0&&text.indexOf("Profil 6/1")>=0,"job details missing");
    check(disconnected&&JSON.stringify(d.params)===before,"status mutated project or leaked connection");
});
test("channel evidence preserves manual fields and labels correlated samples",function() {
    var d=deviceWith({Name:"Office",RecognitionTypeAuto:0,ProfileOverride:448}),before=JSON.stringify(d.params),text="",disconnected=false;
    var identity=snapshot(0x1C,6,1);
    var online={connect:function(){},disconnect:function(){disconnected=true;},invokeFunctionProperty:function(o,p,data){
        if(data[0]===0x1D)return identity;
        if(data[0]===0x22)return [0,1,0,1,0,0];
        check(data[0]===0x28&&[0,10,11].indexOf(data[2])>=0,"unexpected request");
        return [0,1,0,data[2],0x12,0x34,0x56,2,0x12,0x34,0,0,0,7,0,0,0,8,1,1];
    }};
    IOHC_readChannelEvidence(d,online,{setText:function(v){text=v;}},{channelIndex:1});
    check(text.indexOf("Gerätetyp: manuell")>=0&&text.indexOf("korreliert")>=0&&text.indexOf("Aktuelle Geräteevidenz")>=0,"authority/trust omitted");
    check(disconnected&&JSON.stringify(d.params)===before,"evidence mutated project");
});
test("channel evidence refuses changed identity and closes connection",function() {
    var count=0,disconnected=false,text="",d=deviceWith({}),before=JSON.stringify(d.params);
    var online={connect:function(){},disconnect:function(){disconnected=true;},invokeFunctionProperty:function(o,p,data){
        if(data[0]===0x22)return [3];
        var result=snapshot(0x1C,6,1);if(++count===2)result[5]=0x57;return result;
    }};
    var rejected=false;try{IOHC_readChannelEvidence(d,online,{setText:function(v){text=v;}},{channelIndex:1});}catch(e){rejected=true;}
    check(rejected&&disconnected&&text===""&&JSON.stringify(d.params)===before,"stale evidence displayed or applied");
});

test("product query enqueues bound RGB selectors and preserves project",function() {
    var d=deviceWith({Name:"Office",ProfileOverride:448}),before=JSON.stringify(d.params),text="",closed=false,seen=[];
    var identity=snapshot(0x1C,6,1);
    var online={connect:function(){},disconnect:function(){closed=true;},invokeFunctionProperty:function(o,p,data){
        if(data[0]===0x23)return [0,1,0,0,0,31,0,0,0,9];
        if(data[0]===0x1D)return identity;
        if(data[0]===0x22)return [0,1,0,1,0,0];
        check(data[0]===0x2E&&data.length===6&&IOHC_readNodeId(data,3)===0x123456,"unbound RF request");
        seen.push(data[2]);return [0,1,0,data[2]];
    }};
    IOHC_requestProductObservations(d,online,{setText:function(v){text=v;}},{channelIndex:1});
    check(JSON.stringify(seen)==="[0,10,11]"&&text.indexOf("keine atomare RGB")>=0,"selector/coherence policy");
    check(closed&&JSON.stringify(d.params)===before,"project mutated/connection leaked");
});
test("product query rejects legacy firmware before any RF request",function() {
    var calls=0,closed=false,rejected=false;
    var online={connect:function(){},disconnect:function(){closed=true;},invokeFunctionProperty:function(o,p,data){calls++;check(data[0]===0x23,"legacy RF request");return [0,1,0,0,0,15,0,0,0,9];}};
    try{IOHC_requestProductObservations(deviceWith({}),online,{setText:function(){}},{channelIndex:1});}catch(e){rejected=true;}
    check(rejected&&closed&&calls===1,"legacy query accepted");
});

test("commissioning candidate view rejects stale snapshot without displaying results",function() {
    var caps=[0,1,0,0,0,31,0,0,0,9],job=new Array(26).fill(0);job[1]=1;job[2]=2;job[3]=4;job[7]=2;job[8]=255;job[15]=1;job[23]=9;job[25]=1;
    var text="",closed=false,rejected=false;
    var online={connect:function(){},disconnect:function(){closed=true;},invokeFunctionProperty:function(o,p,data){
        if(data[0]===0x23)return caps;if(data[0]===0x24)return job;
        return [0,1,0,0x12,0x34,0x56,6,0,1,2,1,0x1C,0,0,0,2,0,0,0,2];
    }};
    try{IOHC_readCommissioningStatus(deviceWith({}),online,{setText:function(v){text=v;}},{});}catch(e){rejected=true;}
    check(rejected&&closed&&text.indexOf("fehlgeschlagen")>=0&&text.indexOf("0x25")>=0,"stale candidate failure missing");
});

test("persistence evidence remains read only and checks boot and live receipt",function() {
    var d=deviceWith({Name:"Office"}),before=JSON.stringify(d.params),text="",closed=false;
    var caps=[0,1,0,0,0,63,0,0,0,9],record=[0,1,0,189,0x83,0x1F,0x2A,0x12,0x34,0x56,1,1,0,0,0,2,0,0,0,9,0];
    var online={connect:function(){},disconnect:function(){closed=true;},invokeFunctionProperty:function(o,p,data){return data[0]===0x23?caps:record;}};
    IOHC_readPersistenceEvidence(d,online,{setText:function(v){text=v;}},{channelIndex:1});
    check(closed&&JSON.stringify(d.params)===before&&text.indexOf("Revision 2")>=0&&text.indexOf("kein Löschen")>=0,"unsafe persistence UI");
});

test("combined RGB read sends one identity-bound selector",function() {
    var requests=0,text="",d=deviceWith({});
    var online={connect:function(){},disconnect:function(){},invokeFunctionProperty:function(o,p,data) {
        if(data[0]===0x23)return [0,1,0,0,0,127,0,0,0,1];
        if(data[0]===0x1D)return snapshot(0x1C,6,1);
        if(data[0]===0x22)return [0,1,0,1,0,0];
        check(data[0]===0x32&&data.join(",")==="50,0,6,0,18,52,86","combined RGB selector");requests++;return [0,1,0,6,0];
    }};
    IOHC_requestProductObservations(d,online,{setText:function(t){text=t;}},{channelIndex:1});
    check(requests===1&&text.indexOf("gemeinsame")>=0,"one snapshot request");
});

test("offline effective settings disclose overrides without changing project",function() {
    var d=deviceWith({ProfileOverride:448,RecognitionTypeAuto:1,RecognitionOrientationAuto:1,RecognitionBinaryAuto:1,RecognitionDimmableAuto:1,DeviceType:7,Suspend:1});
    var before=JSON.stringify(d.params),text="";
    IOHC_showEffectiveSettings(d,{connect:function(){throw new Error("unexpected connection");}}, {setText:function(t){text=t;}},{channelIndex:1});
    check(text.indexOf("4 von 4")>=0&&text.indexOf("Gerätetyp: 7")>=0&&text.indexOf("Profil-Override 448")>=0,"override summary");
    check(text.indexOf("auch wenn ausgeblendet")>=0&&JSON.stringify(d.params)===before,"expert visibility or mutation");
    d.params.IOHC_c1ProfileOverride.value=0;d.params.IOHC_c1RecognitionTypeAuto.value=0;
    check(IOHC_effectiveSettingsText(d,{channelIndex:1}).indexOf("1 von 4")>=0,"independent manual ownership");
});

test("recognition history retains two distinct adoptions and manual ownership",function() {
    var d=deviceWith({RecognitionOrientationAuto:0});
    var names=["Type","Orientation","Binary","Dimmable"];
    for(var i=0;i<names.length;i++)for(var j=0;j<2;j++)d.params["IOHC_c1Recognition"+names[i]+(j?"Previous":"Last")]={value:"keine dokumentierte Übernahme"};
    var discovery={nodeId:0x123456,metadataValid:true,protocolType:6,subtype:1,manufacturer:2,presentationType:6,presentationFlags:4};
    check(IOHC_applyRecognitionSettings(d,"IOHC_c1",discovery),"first adoption");
    var first=value(d,"RecognitionTypeLast");check(first.indexOf("n=123456")>=0,"identity missing");
    check(value(d,"RecognitionOrientationLast")==="keine dokumentierte Übernahme","manual field acquired history");
    IOHC_applyRecognitionSettings(d,"IOHC_c1",discovery);check(value(d,"RecognitionTypePrevious")==="keine dokumentierte Übernahme","duplicate advanced history");
    discovery.nodeId=0x654321;IOHC_applyRecognitionSettings(d,"IOHC_c1",discovery);
    check(value(d,"RecognitionTypePrevious")===first,"previous adoption lost");
    var before=JSON.stringify(d.params);d.params.IOHC_c1ProfileOverride.value=448;before=JSON.stringify(d.params);
    check(!IOHC_applyRecognitionSettings(d,"IOHC_c1",discovery)&&JSON.stringify(d.params)===before,"override changed history");
    check(IOHC_effectiveSettingsText(d,{channelIndex:1}).indexOf("Davor:")>=0,"history absent from summary");
});

test("channel evidence refuses same-node profile change during reads",function() {
    var count=0,text="",closed=false,d=deviceWith({});
    var online={connect:function(){},disconnect:function(){closed=true;},invokeFunctionProperty:function(o,p,data) {
        if(data[0]===0x22)return [3];
        var identity=snapshot(0x1C,6,1);if(++count===2)identity[8]=2;return identity;
    }};
    var failed=false;try{IOHC_readChannelEvidence(d,online,{setText:function(t){text=t;}},{channelIndex:1});}catch(e){failed=true;}
    check(failed&&closed&&text==="","mixed semantic evidence displayed");
    check(!IOHC_sameRecognitionSnapshot(snapshot(0x1C,6,1),snapshot(0x0C,6,1)),"pairing change ignored");
});

test("guided continuation reads active job without starting another operation",function(){
 var calls=[],closed=0,online={connect:function(){},disconnect:function(){closed++;},invokeFunctionProperty:function(o,p,d){calls.push(d[0]);if(d[0]===0x23)return [0,1,0,0,0,127,0,0,0,1];var a=[0,1,1,2,0,0,0,1,0,0x12,0x34,0x56,0,0,0,0,0,0,1,0,0,0,0,1,0,0];return a;}};
 var text="";IOHC_continueCommissioning(deviceWith({}),online,{setText:function(t){text=t;}},{});
 check(calls.join(",")==="35,36,35,36"&&closed===2&&text.indexOf("Suche")>=0,"continuation restarted RF");
});
test("assignment preview does not write and detects occupied target",function(){
 var d=deviceWith({Active:0}),job={token:[0,0,0,1,0,0,0,1,0,0,0,1],stage:4,count:1};
 var online={invokeFunctionProperty:function(o,p,data){check(data[0]===0x1D,"preview wrote assignment");return snapshot(0,0,0).map(function(v,i){return i>=3&&i<=5?0:v;});}};
 var old=IOHC_jobSnapshot;IOHC_jobSnapshot=function(){return job;};
 try {
  var found=[{index:0,nodeId:0x123456,metadataValid:true,protocolType:1,subtype:0,passiveAuthVerified:true}];
  var preview=IOHC_assignmentPreview(online,d,job,found,1,0,0);check(preview.plan.length===1&&preview.plan[0].channel===0&&preview.token.length<=40,"preview identity");
  d.params.IOHC_c1Active.value=1;var failed=false;try{IOHC_assignmentPreview(online,d,job,found,1,1,1);}catch(e){failed=true;}check(failed,"occupied target accepted");
 } finally {IOHC_jobSnapshot=old;}
});

test("receipt synchronization never claims a completed download",function(){
 var d=deviceWith({});d.params.IOHC_c1SyncStatus={value:"ungeprüft"};
 var receipt=[0,1,0,0,0,0,8,0x12,0x34,0x56,1,0,0,1,2,0];
 var online={invokeFunctionProperty:function(o,p,data){if(data[0]===0x1D)return snapshot(0x1C,1,0);if(data.length===2)return receipt;return [0];}};
 check(IOHC_resumeAssignment(d,online,0)&&value(d,"SyncStatus")==="Gerät+ETS gespeichert; Download offen","download conflated with ACK");
});
test("recognition conflict report preserves manual values and rejects 1W",function(){
 var d=deviceWith({DeviceType:7,ProfileOverride:448}),before=JSON.stringify(d.params),response=snapshot(0x1C,1,0).concat([1,1]);
 check(IOHC_recognitionConflicts(d,{channelIndex:1},response).length>=2,"conflicts omitted");check(JSON.stringify(d.params)===before,"conflict report mutated values");
 response[11]|=32;var failed=false;try{IOHC_recognitionConflicts(d,{channelIndex:1},response);}catch(e){failed=true;}check(failed,"1W recognition invented");
});
test("restore automatic requires unchanged preview and preserves unrelated settings",function(){
 var d=deviceWith({ProfileOverride:448,RecognitionTypeAuto:0,TwoWayPowerClass:2,Name:"Office"});d.params.IOHC_c1AutomaticResetPreview={value:""};var progress={setText:function(){}};
 IOHC_restoreAutomatic(d,null,progress,{channelIndex:1});check(value(d,"ProfileOverride")===448&&value(d,"RecognitionTypeAuto")===0,"preview mutated ownership");
 d.params.IOHC_c1ProfileOverride.value=449;IOHC_restoreAutomatic(d,null,progress,{channelIndex:1});check(value(d,"ProfileOverride")===449,"stale preview applied");
 IOHC_restoreAutomatic(d,null,progress,{channelIndex:1});check(value(d,"ProfileOverride")===0&&value(d,"RecognitionTypeAuto")===1,"confirmed ownership unchanged");
 check(value(d,"TwoWayPowerClass")===2&&value(d,"Name")==="Office","expert settings reset");
});
test("sensor controls require explicit backbone and bound 2W node",function(){
 var d=deviceWith({});d.params.IOHC_c1SensorBackbone={value:"000000"};var calls=[],closed=0;
 var online={connect:function(){},disconnect:function(){closed++;},invokeFunctionProperty:function(o,p,a){calls.push(a);if(a[0]===0x1D)return snapshot(0x1C,1,0);return [0,1,0,a[2]];}};
 var failed=false;try{IOHC_sensorSubscribe(d,online,{setText:function(){}},{channelIndex:1});}catch(e){failed=true;}
 check(failed&&calls.length===1&&closed===1,"missing backbone queued a write");
 d.params.IOHC_c1SensorBackbone.value="123456";IOHC_sensorSubscribe(d,online,{setText:function(){}},{channelIndex:1});
 check(calls[2].join(",")==="53,0,2,18,52,86,18,52,86","subscription node/backbone ABI");
});
test("sensor evidence reports invalid and raw states without project type changes",function(){
 var d=deviceWith({DeviceType:7});d.params.IOHC_c1PriorityLevel={value:2};d.params.IOHC_c1DiagnosticEvidence={value:""};var text="";
 var online={connect:function(){},disconnect:function(){},invokeFunctionProperty:function(o,p,a){if(a[0]===0x1D)return snapshot(0x1C,1,0);if(a[0]===0x29)return [0,1,0,2,0x12,0x34,0x56,0,1,0,0,0,0,0,0];if(a[0]===0x2A)return [0,1,0,0x12,0x34,0x56,0,3,0x12,0x34,9,8,1,0,0,0,2];return [0,1,0,0,0,0,0,0].concat(new Array(17).fill(0),[0]);}};
 IOHC_readSensorEvidence(d,online,{setText:function(t){text=t;}},{channelIndex:1});
 check(text.indexOf("raw=1234")>=0&&text.indexOf("Einheit unbekannt")>=0&&value(d,"DeviceType")===7,"sensor semantics invented");
});
test("object cancellation refuses a stale boot-token binding",function(){
 var d=deviceWith({});d.params.IOHC_c1ObjectReadToken={value:"old"};var calls=[];
 var online={connect:function(){},disconnect:function(){},invokeFunctionProperty:function(o,p,a){calls.push(a[0]);if(a[0]===0x1D)return snapshot(0x1C,1,0);return [0,1,1,0,0,0,0,9,0x12,0x34,0x56,0,0,0,0,0,1,0,0,0,0,1];}};
 var failed=false;try{IOHC_objectCancel(d,online,{setText:function(){}},{channelIndex:1});}catch(e){failed=true;}
 check(failed&&calls.join(",")==="29,44","stale object cancellation sent");
});


test("normal status read adopts known functions and asks for download", function() {
    var d=deviceWith({DeviceType:7}),closed=false,message="";
    var online={connect:function(){},disconnect:function(){closed=true;},invokeFunctionProperty:function(o,p,data){
        if(data[0]===0x12)return [1,0x12,0x34,0x56,0,0];
        if(data[0]===0x1E)return [3];
        check(data[0]===0x1D,"unexpected status request");return snapshot(0x1C,6,58);
    }};
    IOHC_refreshPairingInfo(d,online,{setText:function(t){message=t;},setProgress:function(){}},{channelIndex:1});
    check(closed && value(d,"DeviceType")===6 && value(d,"BinaryOnly")===1,"normal setup did not adopt recognition");
    check(message.indexOf("Applikation programmieren")>=0,"missing download instruction");
});
test("normal status read preserves explicit manual choices", function() {
    var d=deviceWith({DeviceType:7,ProfileOverride:448});
    var online={connect:function(){},disconnect:function(){},invokeFunctionProperty:function(o,p,data){
        if(data[0]===0x12)return [1,0x12,0x34,0x56,0,0];
        return data[0]===0x1E?[3]:snapshot(0x1C,6,58);
    }};
    IOHC_refreshPairingInfo(d,online,{setText:function(){},setProgress:function(){}},{channelIndex:1});
    check(value(d,"DeviceType")===7 && value(d,"ProfileOverride")===448,"manual override overwritten");
});

test("profile objects use exact detection or a known manual override",function(){
 var out={};IOHC_profileObjectsProfile({Override:0,Detected:"Profil 17/0"},out,{});check(out.Profile===1088,"exterior blind mapping missing");
 IOHC_profileObjectsProfile({Override:129,Detected:"Profil 17/0"},out,{});check(out.Profile===129,"manual subtype lost");
 IOHC_profileObjectsProfile({Override:0,Detected:"Profil 2/2"},out,{});check(out.Profile===130,"projection subtype lost");
 IOHC_profileObjectsProfile({Override:0,Detected:"Profil 1/1"},out,{});check(out.Profile===0,"unknown subtype borrowed another profile");
 IOHC_profileObjectsProfile({Override:65535,Detected:"Profil 17/0"},out,{});check(out.Profile===0,"unsupported manual profile exposed objects");
 IOHC_profileObjectsProfile({Override:0,Detected:"1W: keine bestätigte Aktor-Erkennung"},out,{});check(out.Profile===0,"1W inferred an actuator profile");
});


test("all detailed selections configure their profile and retain their exact ETS choice", function() {
 for(var i=0;i<IOHC_CHANNEL_SELECTIONS.length;i++) {
  var row=IOHC_CHANNEL_SELECTIONS[i],configured={},roundtrip={};
  IOHC_syncChannelSelection({Selection:row.value},configured,{});
  check(configured.Active===1&&configured.DeviceType===row.type&&configured.Override===row.packed,"preset mapping "+row.label);
  check(configured.Orientation===(row.flags&1)&&configured.Binary===((row.flags>>1)&1)&&configured.Dimmable===((row.flags>>2)&1),"capability mapping "+row.label);
  check(configured.TypeAuto===0&&configured.OrientationAuto===0&&configured.BinaryAuto===0&&configured.DimmableAuto===0,"manual ownership "+row.label);
  IOHC_syncChannelSelection(configured,roundtrip,{});
  check(roundtrip.Selection===row.value,"round trip "+row.label);
 }
});
test("automatic selection resets manual ownership and disabling preserves the profile", function() {
 var out={};IOHC_syncChannelSelection({Selection:1},out,{});
 check(out.Active===1&&out.Override===0&&out.TypeAuto===1&&out.OrientationAuto===1&&out.BinaryAuto===1&&out.DimmableAuto===1,"automatic choice did not relinquish overrides");
 var disabled={};IOHC_syncChannelSelection({Selection:0},disabled,{});
 check(disabled.Active===0&&disabled.DeviceType===undefined&&disabled.Override===undefined,"disable erases preset");
 var inactive={};IOHC_syncChannelSelection({Active:0,DeviceType:1,Override:129},inactive,{});
 check(inactive.Selection===0,"inactive detailed preset activates channel");
 var automatic={};IOHC_syncChannelSelection({Active:1,DeviceType:1,Override:0},automatic,{});
 check(automatic.Selection===1,"discovered device selects a removed category");
 var d=deviceWith({Active:1});
 IOHC_queryRecognition(d,onlineWith(snapshot(0x1C,1,0)),{channelIndex:1},true);
 IOHC_syncChannelSelection({Active:1,DeviceType:value(d,"DeviceType"),Override:value(d,"ProfileOverride")},automatic,{});
 check(automatic.Selection===1&&value(d,"OrientationObjects")===1&&value(d,"ProfileOverride")===0&&value(d,"RecognitionTypeAuto")===1,"discovery must retain automatic ownership and enable slats");
});

// Model an ETS host collection: only indexed properties and length, no Array methods.
function hostBytes(bytes) {
 var host={length:bytes.length};for(var i=0;i<bytes.length;i++)host[i]=String(bytes[i]);return host;
}
function importFixture() {
 var d=deviceWith({Active:0}),nodes=[0x155D81,0xE50470,0x562292],assigned=[0,0,0],acks=0,writes=0,closed=0,text="";
 for(var c=2;c<=3;c++) {var extra=deviceWith({Active:0});for(var name in extra.params)d.params[name.replace("c1","c"+c)]=extra.params[name];}
 d.params.IOHC_AssignmentPreviewToken={value:""};
 var caps=[0,1,0,0,0,127,0,0,0,9],job=[0,1,2,4,0,0,0,2,255,0,0,0,0,0,0,1,0,0,0,0,0,0,0,9,0,3];
 var online={connect:function(){},disconnect:function(){closed++;},invokeFunctionProperty:function(o,p,data){
  var cmd=data[0],c=data[1],r;
  if(cmd===0x18)r=[0,4,0,0,0,0,0,0,3,0,0];
  else if(cmd===0x23)r=caps;
  else if(cmd===0x24)r=job;
  else if(cmd===0x25){var i=data[13],n=nodes[i];r=[0,1,i,n>>16,(n>>8)&255,n&255,1,0,0,2,1,0x1D,0,0,0,2,0,0,0,1];}
  else if(cmd===0x1D){var n=assigned[c];r=[0,1,c,n>>16,(n>>8)&255,n&255,1,0,0,2,1,n?0x1C:0];}
  else if(cmd===0x26){var i=data[13],c=data[14];if(assigned[c]===nodes[i])r=[1,c];else{writes++;assigned[c]=nodes[i];r=[0,c];}}
  else if(cmd===0x21){var n=assigned[c];if(data.length>2)acks++;r=[0,1,c,0,0,0,2,n>>16,(n>>8)&255,n&255,1,0,0,1,2,0];}
  else if(cmd===0x1B)r=[0];
  else throw new Error("Unexpected import command "+cmd);
  return hostBytes(r);
 }};
 return {device:d,online:online,progress:{setText:function(t){text=t;},setProgress:function(){}},
  text:function(){return text;},writes:function(){return writes;},acks:function(){return acks;},closed:function(){return closed;}};
}
function withLegacyRuntime(run) {
 var oldJSON=JSON,oldMap=Array.prototype.map,oldImul=Math.imul;
 try{JSON=undefined;Array.prototype.map=undefined;Math.imul=undefined;run();}
 finally{JSON=oldJSON;Array.prototype.map=oldMap;Math.imul=oldImul;}
}
test("host collections import three candidates without JSON map or imul",function(){
 var f=importFixture();withLegacyRuntime(function(){
  IOHC_continueCommissioning(f.device,f.online,f.progress,{channelCount:3});
  check(f.writes()===0&&f.text().indexOf("3 Zuordnungen")>=0,"preview must not assign");
  IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
  check(f.writes()===3&&f.acks()===3&&f.text().indexOf("3 importiert")>=0,"confirmed import failed");
  for(var c=1;c<=3;c++)check(f.device.params["IOHC_c"+c+"Active"].value===1,"ETS channel missing");
  IOHC_readCommissioningStatus(f.device,f.online,f.progress,{});
  check(f.text().indexOf("Kandidaten 3")>=0&&f.closed()===4,"status/connection handling");
 });
});
test("ETS setter failure keeps firmware assignment recoverable without ACK",function(){
 var f=importFixture(),saved=f.device.params.IOHC_c1Active,fail=true;
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
 Object.defineProperty(saved,"value",{get:function(){return 0;},set:function(v){if(fail)throw new Error("test setter rejected");},configurable:true});
 var rejected=false;try{IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});}catch(e){rejected=true;}
 check(rejected&&f.writes()===3&&f.acks()===0&&f.closed()===2,"receipt lost or premature ACK");
 check(f.text().indexOf("ETS-Kanalparameter")>=0&&f.text().indexOf("test setter rejected")>=0,"setter stage missing");
 f.device.params.IOHC_c1Active={value:0};
 withLegacyRuntime(function(){check(IOHC_resumeAssignment(f.device,f.online,0),"receipt recovery failed");});
 check(f.acks()===1&&f.device.params.IOHC_c1Active.value===1&&f.writes()===3,"recovery repeated RF assignment");
});
test("connection failures identify the failing step and disconnect",function(){
 var closed=0,text="",online={connect:function(){throw new Error("offline");},disconnect:function(){closed++;}};
 var p={setText:function(t){text=t;},setProgress:function(){}};
 var handlers=[IOHC_startKeyExtract,IOHC_continueCommissioning,IOHC_readCommissioningStatus];
 for(var i=0;i<handlers.length;i++){var rejected=false;try{handlers[i](deviceWith({}),online,p,{});}catch(e){rejected=true;}check(rejected&&text.indexOf("Verbindung")>=0&&text.indexOf("offline")>=0,"connect diagnostic");}
 check(closed===3,"failed connection cleanup");
});
test("preview token binds choice target occupancy node and existing flag",function(){
 var f=importFixture(),job=IOHC_jobSnapshot(f.online),found=[{index:0,nodeId:0x155D81,metadataValid:true,protocolType:1,subtype:0,passiveAuthVerified:true}];
 var first=IOHC_assignmentPreview(f.online,f.device,job,found,3,0,0).token;
 check(first!==IOHC_assignmentPreview(f.online,f.device,job,found,3,1,0).token,"choice unbound");
 check(first!==IOHC_assignmentPreview(f.online,f.device,job,found,3,0,1).token,"target unbound");
 f.device.params.IOHC_c3Active.value=1;
 check(first!==IOHC_assignmentPreview(f.online,f.device,job,found,3,0,0).token,"occupancy unbound");
 f.device.params.IOHC_c3Active.value=0;found[0].nodeId++;
 check(first!==IOHC_assignmentPreview(f.online,f.device,job,found,3,0,0).token,"node unbound");
 found[0].nodeId--;found[0].index=1;
 check(first!==IOHC_assignmentPreview(f.online,f.device,job,found,3,0,0).token,"index unbound");
});

test("changed job token prevents preview assignment before any write",function(){
 var f=importFixture(),job=IOHC_jobSnapshot(f.online),preview={plan:[{index:0,channel:0,node:0x155D81}]};job.token[11]++;
 var rejected=false;try{IOHC_assignPreviewed(f.online,job,[],preview);}catch(e){rejected=true;}
 check(rejected&&f.writes()===0,"stale preview wrote assignment");
});
test("host response normalization rejects malformed bytes and lengths",function(){
 var bad=[{length:1,0:undefined},{length:1,0:256},{length:1,0:-1},{length:1,0:1.5},{length:-1},{}];
 for(var i=0;i<bad.length;i++){var rejected=false;try{IOHC_invokeFunctionProperty({invokeFunctionProperty:function(){return bad[i];}},[0x23]);}catch(e){rejected=true;}check(rejected,"malformed host response accepted");}
});

test("pergola automatically recognizes MP cover without slat or ventilation",function(){
 var d=deviceWith({OrientationObjects:1});
 check(IOHC_queryRecognition(d,onlineWith(snapshot(0x1C,29,0)),{channelIndex:1},true),"pergola not recognized");
 check(value(d,"DeviceType")===1&&value(d,"OrientationObjects")===0&&value(d,"BinaryOnly")===0,"pergola inferred slats/binary");
 var preset=IOHC_CHANNEL_SELECTIONS[IOHC_CHANNEL_SELECTIONS.length-1];
 check(preset.value===62&&preset.packed===1856&&preset.flags===0,"explicit pergola preset missing");
});

function withImportMetadata(f,flags,type) {
 var original=f.online.invokeFunctionProperty;
 f.online.invokeFunctionProperty=function(o,p,data){
  var result=original(o,p,data);
  if(data[0]===0x25&&data[13]===2){result[11]=String(flags.value);if(type!==undefined){result[6]=String(type&255);result[7]=String(type>>8);}}
  return result;
 };
}
test("missing import metadata is distinct from channel capacity and retry adds only missing node",function(){
 var f=importFixture(),flags={value:1};withImportMetadata(f,flags);
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
 check(f.text().indexOf("Metadaten fehlen")>=0&&f.writes()===0,"missing metadata preview");
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
 check(f.writes()===2&&f.text().indexOf("1 wegen fehlender Metadaten ausgelassen")>=0,"missing metadata reason lost");
 check(f.text().indexOf("ohne freien Kanal")<0&&f.device.params.IOHC_c3Active.value===0,"false capacity or activation");
 flags.value=0x1D;
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
 check(f.writes()===2,"changed metadata assigned without preview confirmation");
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
 check(f.writes()===3&&f.device.params.IOHC_c3Active.value===1,"retry failed to add missing node or duplicated existing nodes");
 check(f.text().indexOf("1 importiert, 2 vorhanden")>=0,"duplicate import count");
});
test("unknown supported-profile mapping is distinct from missing metadata",function(){
 var f=importFixture();withImportMetadata(f,{value:0x1D},0xFFFF);
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
 check(f.text().indexOf("Profil nicht automatisch zuordenbar")>=0,"unknown profile preview");
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:3});
 check(f.writes()===2&&f.text().indexOf("1 mit nicht automatisch zuordenbarem Profil")>=0,"unknown profile result");
 check(f.text().indexOf("ohne freien Kanal")<0&&f.text().indexOf("wegen fehlender Metadaten")<0,"wrong skip reason");
});
test("genuine capacity and deliberately unselected import candidates are counted separately",function(){
 var f=importFixture();IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:2});
 IOHC_startKeyExtract(f.device,f.online,f.progress,{channelCount:2});
 check(f.writes()===2&&f.text().indexOf("1 ohne freien Kanal")>=0,"capacity reason missing");
 var chosen=importFixture();chosen.device.params.IOHC_ImportCandidate={value:1};
 IOHC_startKeyExtract(chosen.device,chosen.online,chosen.progress,{channelCount:3});
 IOHC_startKeyExtract(chosen.device,chosen.online,chosen.progress,{channelCount:3});
 check(chosen.writes()===1&&chosen.text().indexOf("2 nicht ausgewählt")>=0,"selection reported as capacity");
 check(chosen.text().indexOf("ohne freien Kanal")<0,"false capacity for selection");
});
test("legacy assignment rejection does not invent a no-free-channel cause",function(){
 var summary=IOHC_importAssignmentSummary(0,0,[{}],[0,1,2,255],null);
 check(summary.indexOf("Speicherung abgelehnt")>=0&&summary.indexOf("ohne freien Kanal")<0,"legacy rejection guessed capacity");
});
