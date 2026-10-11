#include "../src/radio/IoHomeRfAccessPolicy.h"
#include <cassert>
#include <iostream>
int main() {
    IoHomeDutyBudget b;b.advance(0);b.charge(868950000,3404);
    IoHomeRfAccessPolicy p;
    assert(!p.admits(b,868950000,279,0));
    assert(b.used(868800000)==3404 && IoHomeDutyBudget::allowance(1)==3600);
    assert(!p.qualified() && p.mode(0)==IoHomeRfAccessPolicy::Mode::DutyLimited);
#ifdef TEST_NATIVE
    assert(p.simulation());assert(p.admits(b,868950000,279,0));
#else
    assert(!p.simulation());
#endif
    p.off();
#ifdef IOHC_ENABLE_RF_BENCH_UNLIMITED
    assert(!p.bench(0,false));assert(p.bench(UINT32_MAX-100,true));
    assert(p.admits(b,868950000,279,10));b.charge(868950000,279);
    assert(b.used(868950000)==3683);
    p.advance(uint32_t(UINT32_MAX-100+IoHomeRfAccessPolicy::benchDurationMs));
    assert(p.mode(10)==IoHomeRfAccessPolicy::Mode::DutyLimited);
    assert(!p.admits(b,868950000,1,10));
#else
    assert(!p.bench(0,true));
#endif
    // Startup is immediately eligible; only actual charged airtime can block it.
    IoHomeDutyBudget fresh;fresh.advance(100);IoHomeRfAccessPolicy startup;
    assert(startup.admits(fresh,868950000,279,100));
    assert(startup.retryAfter(fresh,868950000,279,100)==0);
    startup.off();assert(startup.admits(fresh,868950000,279,100));
    b.charge(868950000,UINT32_MAX);b.charge(868950000,1);assert(b.used(868950000)==UINT32_MAX);
    assert(b.retryAfter(868950000,279,0)==2*IoHomeDutyBudget::bucketMs);
    b.advance(IoHomeDutyBudget::bucketMs);assert(!b.admits(868950000,1));
    b.advance(2*IoHomeDutyBudget::bucketMs);assert(b.admits(868950000,3600));
    assert(!p.admits(b,433000000,1,0));assert(!p.admits(b,868950000,36001,0));
    std::cout << "RF policy gates, expiry, saturation and immediate startup passed\n";
}
