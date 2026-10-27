"""Reject lost high bytes, unqualified ACK timing and stale interpolation rewrites."""
from pathlib import Path
import tempfile,subprocess,os,shlex
root=Path(__file__).resolve().parents[1]
source=(root/'src/controller/IoHomeController.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='iohc-remaining-mutation-') as d:
 d=Path(d)
 mutants=[('lost_high_byte',source.replace('lCh->onEstimate(lRemaining.raw);','lCh->onEstimate(mRxFrame.data[7]);',1)),
          ('unqualified_execute_ack',source.replace('                if (!mTrustRxPosition || !mCorrelatedStatusReply)','                if(mRxFrame.dataLen>=8)lCh->onEstimate((uint16_t(mRxFrame.data[6])<<8)|mRxFrame.data[7]);\n                if (!mTrustRxPosition || !mCorrelatedStatusReply)',1))]
 for name,mutated in mutants:
  assert mutated!=source
  p=d/(name+'.cpp');p.write_text(mutated);binary=d/name
  command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-DTEST_NATIVE','-I'+str(root/'src'),'-I'+str(root/'src/controller'),'-I'+str(root/'test'),'-o',str(binary),str(root/'test/test_protocol.cpp'),str(root/'test/test_suite_main.cpp'),str(p)]
  command += [str(root/f) for f in ['src/protocol/IoHomeCrypto.cpp','src/protocol/IoHomeFrame.cpp','src/protocol/IoHomeProfileRegistry.cpp','src/IoHomeRemoteMap.cpp']]
  subprocess.run(command,check=True,capture_output=True,text=True)
  result=subprocess.run([str(binary),'--filter','controller_remaining_time_only_owned_correlated_main_info_poll'],capture_output=True,text=True)
  assert result.returncode==1 and '0 passed, 1 failed' in result.stdout,result.stdout+result.stderr
  print(name+': rejected as expected')
 script=(root/'test/test_wake_production.py').read_text().replace('root=Path(__file__).resolve().parents[1]','root=Path('+repr(str(root))+')',1)
 statement="channel=(root/'src/IoHomecontrolChannel.cpp').read_text()"
 marker='            mTravelStartPosition=mCurrentPosition;'
 assert marker in (root/'src/IoHomecontrolChannel.cpp').read_text()
 rewrite='            mCurrentPosition=clampPercent(estimateCurrentPosition());\n'+marker
 script=script.replace(statement,statement+'\nchannel=channel.replace('+repr(marker)+','+repr(rewrite)+',1)',1)
 p=d/'stale_position.py';p.write_text(script)
 result=subprocess.run([os.sys.executable,str(p)],capture_output=True,text=True)
 assert result.returncode!=0 and 'Assertion' in result.stderr,result.stdout+result.stderr
 print('stale_interpolation_overwrites_measurement: rejected as expected')
