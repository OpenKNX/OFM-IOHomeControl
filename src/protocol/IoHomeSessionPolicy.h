#pragma once
#include "IoHomeCommands.h"

struct IoHomeSessionPolicy {
    uint8_t mode, rfAttempts, stateAttempts, wholeSessionAttempts;
    uint16_t totalBudgetMs;
    bool unresolvedExecuteSelector;
};
inline IoHomeSessionPolicy ioHomeSessionPolicy(IoHomeCommand command, uint8_t main = 0, int16_t matchingBeaconNodes = -1) {
    // Mode 3 counts SetBeacon-marked BasicNodes for the current SystemId.
    // -1 means inventory/SystemId evidence unavailable: conservative five.
    if (command == IoHomeCommand::Execute)
        return {3,5,uint8_t(main==0xD8 ? 1 : matchingBeaconNodes==0 ? 9 : 5),1,
            uint16_t(matchingBeaconNodes==0?20000:9000),matchingBeaconNodes<0};
    if (command == IoHomeCommand::Private || command == IoHomeCommand::WritePrivate)
        return {0,5,3,1,5000,false}; // Host policy; PRIVATE modes 3/4 need explicit producer context.
    if (command == IoHomeCommand::DiscoverRequest || command == IoHomeCommand::DiscoverSPERequest)
        return {0,5,1,1,20000,false}; // Multi-return discovery owns its sweep lifecycle.
    return {0,5,3,1,5000,false}; // Pair/key state machines retain their own policy.
}
