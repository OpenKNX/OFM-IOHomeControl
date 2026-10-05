#include "radio/RadioSX1276.h"
#include "protocol/IoHomeFrame.h"
#include "radio/sx1276Regs-Fsk.h"
#include <cassert>
#include <deque>
#include <vector>
#include <cstdio>
struct Bus {
 uint8_t registers[128]{};std::deque<uint8_t> rx;std::vector<uint8_t> tx;
 static uint8_t read(void *p,uint8_t a){auto &b=*static_cast<Bus*>(p);if(a==REG_FIFO){assert(!b.rx.empty());auto v=b.rx.front();b.rx.pop_front();return v;}if(a==REG_IRQFLAGS2)return (b.registers[a]&~RF_IRQFLAGS2_FIFOEMPTY)|(b.rx.empty()?RF_IRQFLAGS2_FIFOEMPTY:0);return b.registers[a];}
 static void write(void *p,uint8_t a,uint8_t v){auto &b=*static_cast<Bus*>(p);if(a==REG_FIFO)b.tx.push_back(v);else if(a==REG_IRQFLAGS2&&(v&RF_IRQFLAGS2_FIFOOVERRUN)){b.rx.clear();b.tx.clear();b.registers[a]&=~RF_IRQFLAGS2_FIFOOVERRUN;}else b.registers[a]=v;}
};
int main(){
 Bus b;b.registers[REG_VERSION]=0x12;RadioSX1276 radio;radio.setNativeTransport(&b,Bus::read,Bus::write);radio.init(1,2,3,4);assert(radio.isInitialized());
 assert(b.registers[REG_DIOMAPPING1]==0x39&&b.registers[REG_DIOMAPPING2]==0xF1);
 assert(radio.setReceiveBandwidths(50000,83333)==RadioError::None&&b.registers[REG_RXBW]==0x0B&&b.registers[REG_AFCBW]==0x12);
 assert(radio.setReceiveBandwidths(12345,83333)==RadioError::InvalidParam);
 radio.setPreambleLength(12);assert(b.registers[REG_PREAMBLELSB]==12);
 uint8_t frame[]={0x0A,0x12,0x34};b.rx={9,8,7};assert(radio.startTransmit(frame,3)==RadioError::None);assert(b.rx.empty()&&b.tx==std::vector<uint8_t>(frame,frame+3));
 assert(radio.setReceiveBandwidths(50000,83333)==RadioError::Busy);b.registers[REG_IRQFLAGS2]=RF_IRQFLAGS2_PACKETSENT;assert(radio.isTxDone()&&radio.txDoneCount()==1);
 radio.startReceive();b.rx={1,2,3};b.registers[REG_IRQFLAGS2]=RF_IRQFLAGS2_PAYLOADREADY|RF_IRQFLAGS2_CRCOK;assert(radio.isPacketAvailable());uint8_t out[8]{};assert(radio.readPacket(out,8)==3&&out[0]==1&&out[2]==3&&radio.lastReceiveEvidence().admissible());
 b.rx={1,2,3};b.registers[REG_IRQFLAGS2]=RF_IRQFLAGS2_PAYLOADREADY|RF_IRQFLAGS2_CRCOK;assert(radio.readPacket(out,2)==0&&b.rx.empty()&&radio.lastReceiveEvidence().truncated);
 b.rx={1,2};b.registers[REG_IRQFLAGS2]=RF_IRQFLAGS2_PAYLOADREADY;assert(radio.readPacket(out,8)==0&&!radio.lastReceiveEvidence().hardwareCrcValid);
 assert(!radio.superviseIdleReceive(5000));
 radio.setFrequency(IOHC_FREQ_1);const uint8_t frf[]={b.registers[REG_FRFMSB],b.registers[REG_FRFMID],b.registers[REG_FRFLSB]};
 b.registers[REG_OPMODE]=1;assert(radio.superviseIdleReceive(10000)&&radio.recoveryAttempts()==1&&radio.state()==RadioState::Receiving);
 assert(radio.recoveryReason()==RadioSX1276::RecoveryReason::ReceiveMode&&radio.recoverySuccesses()==1&&radio.watchdogTriggers()==1);
 assert(b.registers[REG_RXBW]==0x0B&&b.registers[REG_AFCBW]==0x12);
 assert(b.registers[REG_FRFMSB]==frf[0]&&b.registers[REG_FRFMID]==frf[1]&&b.registers[REG_FRFLSB]==frf[2]);
 assert(radio.setSupervisionIntervalMs(3000)&&!radio.setSupervisionIntervalMs(4000));assert(radio.setSupervisionIntervalMs(5000));
 radio.sleep();assert(!radio.superviseIdleReceive(15000)&&radio.recoveryAttempts()==1);radio.startReceive();
 b.registers[REG_VERSION]=0;assert(!radio.superviseIdleReceive(15000));assert(!radio.superviseIdleReceive(20000)&&radio.recoveryExhausted());assert(!radio.superviseIdleReceive(25000)&&radio.recoveryAttempts()==3);
 Bus edges;edges.registers[REG_VERSION]=0x12;RadioSX1276 captured;captured.setNativeTransport(&edges,Bus::read,Bus::write);captured.init(1,2,3,4,5,true);assert(edges.registers[REG_DIOMAPPING1]==0x3D);captured.startReceive();captured.nativeReceiveEdge(false,0xFFFFFFF0);captured.nativeReceiveEdge(true,10);edges.rx={1};edges.registers[REG_IRQFLAGS2]=RF_IRQFLAGS2_PAYLOADREADY|RF_IRQFLAGS2_CRCOK;assert(captured.readPacket(out,8)==1);assert(captured.lastReceiveEvidence().preambleTimestampValid&&captured.lastReceiveEvidence().syncTimestampValid&&captured.lastReceiveEvidence().preambleTimestampUs==0xFFFFFFF0);
 edges.rx={2};assert(captured.readPacket(out,8)==1&&!captured.lastReceiveEvidence().syncTimestampValid);
 Bus missing;RadioSX1276 absent;absent.setNativeTransport(&missing,Bus::read,Bus::write);absent.init(1,2,3);assert(!absent.isInitialized());
 puts("SX1276 actual adapter register/FIFO scenarios passed (SPI electrical timing unqualified)");
}
