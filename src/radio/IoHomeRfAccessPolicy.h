#pragma once
#include "IoHomeDutyBudget.h"

// All OpenKNX debug builds expose the default, non-expiring bypass.
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
    static constexpr uint32_t qualifiedFairnessTargetMs=36000U; // target only; NOT qualified/enabled
    static const char *name(Mode mode) {
        switch(mode) {
        case Mode::QualifiedPoliteAccess:return "QualifiedPoliteAccess";
        case Mode::BenchUnlimited:return "BenchUnlimited";
        case Mode::SimulationUnlimited:return "SimulationUnlimited";
        default:return "DutyLimited";
        }
    }
    IoHomeRfAccessPolicy() { resetToDefault(); }
    void resetToDefault() {
#ifdef OPENKNX_DEBUG
        bench(0,true);
#else
        off();
#endif
    }
    void advance(uint32_t now) { (void)now; } // No time-based expiry.
    Mode mode(uint32_t now) const { (void)now; return active; }
    bool bypass(uint32_t now) const {auto m=mode(now);return m==Mode::BenchUnlimited||m==Mode::SimulationUnlimited;}
    // Deliberately narrower than simulation/general duty bypass: release RF
    // safeguards must not inherit the debug-only CCA/quota exemptions.
    bool debugBypass(uint32_t now) const {
#ifdef OPENKNX_DEBUG
        return bypass(now);
#else
        (void)now;return false;
#endif
    }
    bool bench(uint32_t now,bool confirmed) {
#if defined(IOHC_ENABLE_RF_BENCH_UNLIMITED)
        (void)now;
        if(!confirmed)return false;
        active=Mode::BenchUnlimited;++session;return true;
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
        if(IoHomeDutyBudget::band(hz)>=3)return false; // unsupported frequency remains invalid
        if(debugBypass(now))return true;
        if(airtime>36000U)return false;
        if(bypass(now))return true;
        return budget.admits(hz,airtime);
    }
    uint32_t retryAfter(const IoHomeDutyBudget &budget,uint32_t hz,uint32_t airtime,uint32_t now)const {
        if(admits(budget,hz,airtime,now))return 0;
        return budget.retryAfter(hz,airtime,now);
    }
    // No qualification evidence exists for either production PHY. Requests
    // fail closed; a console switch or a partial LBT check is not evidence.
    bool qualified() {off();return false;}
private:
    Mode active=Mode::DutyLimited;
    uint32_t session=0;
};
