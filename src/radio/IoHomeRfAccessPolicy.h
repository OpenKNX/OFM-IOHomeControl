#pragma once
#include "IoHomeDutyBudget.h"

// All OpenKNX debug builds expose the temporary, explicitly confirmed bypass.
#if defined(OPENKNX_DEBUG) && !defined(IOHC_ENABLE_RF_BENCH_UNLIMITED)
#define IOHC_ENABLE_RF_BENCH_UNLIMITED
#endif
#if defined(IOHC_ENABLE_RF_BENCH_UNLIMITED) && !defined(OPENKNX_DEBUG) && !defined(TEST_NATIVE)
#error "RF bench bypass requires an OPENKNX_DEBUG build"
#endif


// Access decisions never own/reset the accounting ledger or protocol guards.
class IoHomeRfAccessPolicy {
public:
    enum class Mode : uint8_t { DutyLimited, QualifiedPoliteAccess, BenchUnlimited, SimulationUnlimited };
    static constexpr uint32_t benchDurationMs=300000U;
    static constexpr uint32_t qualifiedFairnessTargetMs=36000U; // target only; NOT qualified/enabled
    static const char *name(Mode mode) {
        switch(mode) {
        case Mode::QualifiedPoliteAccess:return "QualifiedPoliteAccess";
        case Mode::BenchUnlimited:return "BenchUnlimited";
        case Mode::SimulationUnlimited:return "SimulationUnlimited";
        default:return "DutyLimited";
        }
    }
    void beginRecovery(uint32_t now) { recoveryStart=now;recovering=true;off(); }
    uint32_t recoveryRemaining(uint32_t now) const {
        // One full observation window plus maximum packet tail, measured only
        // by this boot's uptime. Every reboot/brownout restarts the quiet period.
        const uint32_t elapsed=uint32_t(now-recoveryStart);
        return recovering && elapsed<IoHomeDutyBudget::bucketMs ? IoHomeDutyBudget::bucketMs-elapsed:0;
    }
    void advance(uint32_t now) {
        if(recovering&&!recoveryRemaining(now))recovering=false;
        if(active==Mode::BenchUnlimited&&uint32_t(now-benchStart)>=benchDurationMs)off();
    }
    Mode mode(uint32_t now) const {
        return active==Mode::BenchUnlimited&&uint32_t(now-benchStart)>=benchDurationMs?Mode::DutyLimited:active;
    }
    bool bypass(uint32_t now) const {auto m=mode(now);return m==Mode::BenchUnlimited||m==Mode::SimulationUnlimited;}
    bool bench(uint32_t now,bool confirmed) {
#if defined(IOHC_ENABLE_RF_BENCH_UNLIMITED)
        if(!confirmed)return false;
        active=Mode::BenchUnlimited;benchStart=now;++session;return true;
#else
        (void)now;(void)confirmed;return false;
#endif
    }
    bool simulation() {
#ifdef TEST_NATIVE
        active=Mode::SimulationUnlimited;++session;return true;
#else
        return false;
#endif
    }
    void off() {active=Mode::DutyLimited;}
    uint32_t sessionId() const {return session;}
    bool admits(const IoHomeDutyBudget &budget,uint32_t hz,uint32_t airtime,uint32_t now)const {
        if(IoHomeDutyBudget::band(hz)>=3||airtime>36000U)return false;
        if(bypass(now))return true;
        return !recoveryRemaining(now)&&budget.admits(hz,airtime);
    }
    uint32_t retryAfter(const IoHomeDutyBudget &budget,uint32_t hz,uint32_t airtime,uint32_t now)const {
        if(admits(budget,hz,airtime,now))return 0;
        const auto wait=budget.retryAfter(hz,airtime,now),recovery=recoveryRemaining(now);
        return wait>recovery?wait:recovery;
    }
    // No qualification evidence exists for either production PHY. Requests
    // fail closed; a console switch or a partial LBT check is not evidence.
    bool qualified() {off();return false;}
private:
    Mode active=Mode::DutyLimited;
    uint32_t benchStart=0,recoveryStart=0,session=0;
    bool recovering=false;
};
