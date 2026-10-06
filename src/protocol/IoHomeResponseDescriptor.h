#pragma once
#include "IoHomeCommands.h"

// Descriptor acceptance is separate from application interpretation/key proof.
enum class IoHomeResponseDisposition : uint8_t { Ignore, Challenge, Accepted, Rejected, FoldedWithoutKeyProof };
struct IoHomeResponseDescriptor {IoHomeCommand request;uint8_t response;bool challenge;};
inline IoHomeResponseDisposition ioHomeResponseDisposition(IoHomeCommand request, IoHomeCommand response, uint8_t length) {
    if(response==IoHomeCommand::ErrorResponse) {
        if(request==IoHomeCommand::KeyInitTransfer) return IoHomeResponseDisposition::Ignore; // opening 0x31 requires 0x3C
        if(request==IoHomeCommand::Confirmation) return IoHomeResponseDisposition::Accepted;
        if(request==IoHomeCommand::KeyTransfer) return IoHomeResponseDisposition::FoldedWithoutKeyProof;
        return IoHomeResponseDisposition::Rejected;
    }
    static constexpr IoHomeResponseDescriptor descriptors[]={
        {IoHomeCommand::Execute,0x04,true},{IoHomeCommand::ActivateMode,0x04,true},
        {IoHomeCommand::DirectCommand,0x04,true},{IoHomeCommand::Private,0x04,true},
        {IoHomeCommand::Private2,0x0D,true},{IoHomeCommand::PriorityLevelRequest,0x1A,true},
        {IoHomeCommand::Identify,0x1F,true},{IoHomeCommand::WritePrivate,0x21,true},
        {IoHomeCommand::DiscoverRequest,0x29,false},{IoHomeCommand::DiscoverSPERequest,0x2B,false},
        {IoHomeCommand::Confirmation,0x2D,false},{IoHomeCommand::Discover2ERequest,0x2F,true},
        {IoHomeCommand::KeyInitTransfer,0x3C,true},{IoHomeCommand::KeyTransfer,0x33,true},
        {IoHomeCommand::ChallengeRequest,0x3D,false},{IoHomeCommand::NodeVerifyRequest,0x37,true},{IoHomeCommand::LaunchKeyTransfer,0x32,false},
        {IoHomeCommand::Unknown46Request,0x47,true},{IoHomeCommand::Unknown4ARequest,0x4B,false},
        {IoHomeCommand::GetName,0x51,true},{IoHomeCommand::SetName,0x53,true},
        {IoHomeCommand::GetGeneralInfo1,0x55,true},{IoHomeCommand::GetGeneralInfo2,0x57,true},
        {IoHomeCommand::GetGeneralInfo3,0x59,true},{IoHomeCommand::SetConfig1,0x70,true},
        {IoHomeCommand::DiscoverSensorRequest,0x95,false},{IoHomeCommand::DiscoverSensorInSystemRequest,0x97,false},
        {IoHomeCommand::LimitationStatusRequest,0x26,true},{IoHomeCommand::SensorStatusRequest,0x85,true},{IoHomeCommand::SensorSubscribeRequest,0x8C,true}};
    for(const auto &d:descriptors) if(d.request==request) {
        if(response==IoHomeCommand::ChallengeRequest&&d.challenge&&length>=6) return IoHomeResponseDisposition::Challenge;
        if(uint8_t(response)==d.response) return IoHomeResponseDisposition::Accepted;
        if(request==IoHomeCommand::Execute&&response==IoHomeCommand::StatusUpdate) return IoHomeResponseDisposition::Accepted;
        return IoHomeResponseDisposition::Ignore;
    }
    return IoHomeResponseDisposition::Ignore;
}
