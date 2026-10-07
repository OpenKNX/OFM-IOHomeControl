#!/usr/bin/env python3
"""Generate named ETS objects from the firmware's exact profile registry."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
src=ROOT/'src'
registry=(src/'protocol/IoHomeProfileRegistry.cpp').read_text().split('constexpr IoHomeParameterAlias')[0]
labels={'Position':'Position','LinearSpeed':'Fahrgeschwindigkeit','SlatOrientation':'Lamellenposition','SlatOrientationSpeed':'Lamellen-Drehgeschwindigkeit','CurtainPosition':'Behangposition','UpperCurtainPosition':'Oberer Behang','LowerCurtainPosition':'Unterer Behang','HangerOrientation':'Orientierung der Aufhängung','HangerOrientationSpeed':'Drehgeschwindigkeit der Aufhängung','LightIntensity':'Helligkeit','LightIntensityGradient':'Dimmübergang','LockState':'Verriegelung','SwitchState':'Schaltzustand','AirDemand':'Luftbedarf','EnergyDemand':'Heizleistungsbedarf','EnergyGradient':'Änderung der Heizleistung','ShutterClosure':'Schließgrad','ProjectionAngle':'Ausstellwert (roh)','WindowSecurityMode':'Fenster-Sicherheitsmodus'}
profiles=[]
for code,name,args in re.findall(r'profile\((0x[0-9A-F]+), "([^"]+)", (.*?)\)',registry,re.S):
 sem=re.findall(r'S::(\w+)',args); sem=(sem+['Unsupported']*4)[:4]
 values={i:s for i,s in enumerate(sem) if s!='Unsupported'}
 if code=='0x0082':values[9]='ProjectionAngle'
 if code=='0x0241':values[1]='WindowSecurityMode'
 profiles.append((int(code,16),name,values))
assert profiles and len({p[0] for p in profiles})==len(profiles)
A='%AID%';T='%TT%%CC%'
start=f'''<?xml version="1.0" encoding="utf-8"?>
<KNX xmlns="http://knx.org/xml/project/20" xmlns:op="http://github.com/OpenKNX/OpenKNXproducer">
<ManufacturerData><Manufacturer RefId="M-00FA"><ApplicationPrograms>
<ApplicationProgram Id="{A}" ProgramType="ApplicationProgram" MaskVersion="MV-07B0" Name="IOHCProfileObjects" LoadProcedureStyle="MergedProcedure" PeiType="0" DefaultLanguage="de" DynamicTableManagement="false" Linkable="true" MinEtsVersion="4.0" ApplicationNumber="0" ApplicationVersion="0">
<Static><Parameters><Union SizeInBit="24"><Memory CodeSegment="{A}_RS-04-00000" Offset="0" BitOffset="0"/>
<Parameter Id="{A}_UP-{T}001" Name="c%C%ProfileCode" ParameterType="{A}_PT-IOHCProfileOverride" Offset="0" BitOffset="0" Text="Profilzuordnung für Objekte" Value="0" Access="Read"/>
<Parameter Id="{A}_UP-{T}002" Name="c%C%Enabled" ParameterType="{A}_PT-IOHCCheckBox" Offset="2" BitOffset="0" Text="Separate Profilfunktionen verwenden" Value="1"/>
</Union></Parameters><ParameterRefs>
<ParameterRef Id="{A}_UP-{T}001_R-{T}00101" RefId="{A}_UP-{T}001"/>
<ParameterRef Id="{A}_UP-{T}002_R-{T}00201" RefId="{A}_UP-{T}002"/>
</ParameterRefs><ComObjectTable>
'''
objects=[]
for slot,index in enumerate((0,1,2,3,9)):
 for kind in range(3):
  number=3*slot+kind;label=('setzen','Rückmeldung','gültig')[kind]
  size='1 Bit' if kind==2 else '2 Bytes' if index==9 else '1 Byte';dpt='DPST-1-2' if kind==2 else 'DPST-7-1' if index==9 else 'DPST-5-1'
  write='Enabled' if kind==0 and index!=9 else 'Disabled';read='Disabled' if kind==0 else 'Enabled';tx=read
  objects.append(f'<ComObject Id="{A}_O-{T}{number:03d}" Name="c%C%Parameter{index}{("Set","Feedback","Valid")[kind]}" Text="Kanal %C% {("MP" if index==0 else "FP"+str(index))} {label}" FunctionText="{label}" ObjectSize="{size}" ReadFlag="{read}" WriteFlag="{write}" CommunicationFlag="Enabled" TransmitFlag="{tx}" UpdateFlag="Disabled" ReadOnInitFlag="Disabled" DatapointType="{dpt}" Number="%K{number}%"/>')
for number,name,label,size,dpt in [(15,'Read','Profilwerte lesen','1 Bit','DPST-1-17'),(16,'BinarySet','MP Schaltwert setzen','1 Bit','DPST-1-1'),(17,'BinaryFeedback','MP Schaltwert Rückmeldung','1 Bit','DPST-1-1')]:
 read='Enabled' if number==17 else 'Disabled';write='Disabled' if number==17 else 'Enabled'
 objects.append(f'<ComObject Id="{A}_O-{T}{number:03d}" Name="c%C%{name}" Text="Kanal %C% {label}" FunctionText="{label}" ObjectSize="{size}" ReadFlag="{read}" WriteFlag="{write}" CommunicationFlag="Enabled" TransmitFlag="{read}" UpdateFlag="Disabled" ReadOnInitFlag="Disabled" DatapointType="{dpt}" Number="%K{number}%"/>')
refs=[];ui=[]
def ref(number,variant,label,dpt=None):
 rid=f'{A}_O-{T}{number:03d}_R-{T}{number:03d}{variant:02d}'
 refs.append(f'<ComObjectRef Id="{rid}" RefId="{A}_O-{T}{number:03d}" Text="Kanal %C% {label}" FunctionText="{label}"'+(f' DatapointType="{dpt}"' if dpt else '')+'/>')
 return f'<ComObjectRefRef RefId="{rid.replace(T,"%TT%%PRODUCT_CC%")}"/>'
for variant,(packed,name,values) in enumerate(profiles,1):
 branch=[f'<when test="{packed}">']
 for index,semantic in values.items():
  slot=(0,1,2,3,9).index(index);title=labels[semantic];binary=index==0 and (packed&63==58 or semantic in ('LockState','SwitchState'))
  branch.append(f'<ParameterSeparator Id="{A}_PS-nnn" Text="{("MP" if index==0 else "FP"+str(index))}: {title}"/>')
  if semantic not in ('ProjectionAngle','WindowSecurityMode'):
   branch.append(ref(16 if binary else slot*3,variant,title+' setzen'))
  branch.append(ref(17 if binary else slot*3+1,variant,title+' Rückmeldung','DPST-5-10' if semantic=='WindowSecurityMode' else None))
  branch.append(ref(slot*3+2,variant,title+' gültig'))
 branch.append('</when>');ui.extend(branch)
read=ref(15,1,'Profilwerte lesen')
(src/'IoHomeProfileObjects.templ.xml').write_text(start+'\n'.join(objects)+'</ComObjectTable><ComObjectRefs>\n'+'\n'.join(refs)+'</ComObjectRefs></Static><Dynamic/></ApplicationProgram></ApplicationPrograms></Manufacturer></ManufacturerData></KNX>\n')
PT='%TT%%PRODUCT_CC%';IT='%IOHC_TT%%PRODUCT_CC%'
(src/'IoHomeProfileObjects.ui.xml').write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<KNX xmlns="http://knx.org/xml/project/20"><ParameterCalculations>
<ParameterCalculation Id="{A}_PC-{PT}0100" Language="JavaScript" Name="ProfileObjects%PRODUCT_CC%" LRTransformationFunc="IOHC_profileObjectsProfile" RLTransformationFunc="BASE_Nop">
<LParameters><ParameterRefRef RefId="{A}_UP-{IT}101_R-{IT}10101" AliasName="Override"/><ParameterRefRef RefId="{A}_P-{IT}103_R-{IT}10301" AliasName="Detected"/></LParameters>
<RParameters><ParameterRefRef RefId="{A}_UP-{PT}001_R-{PT}00101" AliasName="Profile"/></RParameters>
</ParameterCalculation></ParameterCalculations><Dynamic>
<ParameterRefRef RefId="{A}_UP-{PT}002_R-{PT}00201" HelpContext="IOHC-Profilobjekte"/>
<choose ParamRefId="{A}_UP-{PT}002_R-{PT}00201"><when test="1">
<choose ParamRefId="{A}_UP-{PT}001_R-{PT}00101">
<when test="0"><ParameterSeparator Id="{A}_PS-nnn" Text="Für separate Profilfunktionen Status / Erkennung lesen oder ein bekanntes manuelles Profil wählen. Danach die Applikation programmieren." UIHint="Information"/></when>
'''+ '\n'.join(ui) +f'''</choose><choose ParamRefId="{A}_UP-{PT}001_R-{PT}00101"><when test="&gt;0">{read}</when></choose>
</when></choose></Dynamic></KNX>
''')
print('Generated independent MP/FP objects for',len(profiles),'exact profiles')