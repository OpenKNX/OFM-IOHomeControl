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
    check(rejected&&closed&&text==="","stale candidates displayed");
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
