#pragma once
#include <stdint.h>
class IoHomeRadioPolicy {
public:
 enum class Tx:uint8_t {TimingBestEffort,Strict,Forced};
 bool configure(Tx tx,uint32_t cadenceMs){if(uint8_t(tx)>2||(cadenceMs&&(cadenceMs<100||cadenceMs>60000)))return false;mTx=tx;mCadence=cadenceMs;mLastSample=0;return true;}
 Tx tx()const{return mTx;}uint32_t cadenceMs()const{return mCadence;}
 bool due(uint32_t now){if(!mCadence||uint32_t(now-mLastSample)<mCadence)return false;mLastSample=now;return true;}
 void sample(uint8_t channel,int16_t rssi,bool valid){
  if(channel>=3||!valid)return;
  if(rssi<-81){if(mAggregate){for(auto &c:mCounters)c=0;mAggregate=false;}mCounters[channel]=0;return;}
  if(mCounters[channel]<255)++mCounters[channel];
  const uint8_t thresholds[]={15,40,15};if(mCounters[channel]==thresholds[channel]&&mReports[channel]<0xFFFFFFFF)++mReports[channel];
  if(!mAggregate&&mCounters[0]>5&&mCounters[1]>5&&mCounters[2]>5){mAggregate=true;for(auto &c:mCounters)c=255;if(mAggregateReports<0xFFFFFFFF)++mAggregateReports;}
 }
 void forced(uint8_t operation){if(mForced[operation]<255)++mForced[operation];if(mForcedTotal<0xFFFFFFFF)++mForcedTotal;}
 uint8_t counter(uint8_t c)const{return c<3?mCounters[c]:0;}uint32_t reports(uint8_t c)const{return c<3?mReports[c]:0;}
 bool aggregate()const{return mAggregate;}uint32_t aggregateReports()const{return mAggregateReports;}
 uint8_t forcedCount(uint8_t opcode)const{return mForced[opcode];}uint32_t forcedTotal()const{return mForcedTotal;}
private:
 Tx mTx=Tx::TimingBestEffort;uint32_t mCadence=0,mLastSample=0,mReports[3]{},mAggregateReports=0,mForcedTotal=0;uint8_t mCounters[3]{},mForced[256]{};bool mAggregate=false;
};
