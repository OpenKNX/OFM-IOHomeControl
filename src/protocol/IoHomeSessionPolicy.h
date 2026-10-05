#pragma once
#include "IoHomeCommands.h"

struct IoHomeSessionPolicy {
    uint8_t mode, rfAttempts, stateAttempts, wholeSessionAttempts;
    uint16_t totalBudgetMs;
    bool unresolvedExecuteSelector;
};
inline IoHomeSessionPolicy ioHomeSessionPolicy(IoHomeCommand command, uint8_t main = 0) {
    // The database discriminator selecting 9 versus 5 is not recovered here.
    // Use the smaller recovered EXECUTE budget, never invent that discriminator.
    if (command == IoHomeCommand::Execute)
        return {3,5,uint8_t(main==0xD8 ? 1 : 5),1,9000,true};
    if (command == IoHomeCommand::Private || command == IoHomeCommand::WritePrivate)
        return {0,5,3,1,5000,false}; // Host policy; PRIVATE modes 3/4 need explicit producer context.
    if (command == IoHomeCommand::DiscoverRequest || command == IoHomeCommand::DiscoverSPERequest)
        return {0,5,1,1,20000,false}; // Multi-return discovery owns its sweep lifecycle.
    return {0,5,3,1,5000,false}; // Pair/key state machines retain their own policy.
}
