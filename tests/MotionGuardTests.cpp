#include "fvr/interaction/MotionGuard.h"
#include "Test.h"
#include <iostream>
#include <limits>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
using P=MotionGuard::Position;
int BeginRequiresValidInputs(){
    MotionGuard g;CHECK(!g.Ready()&&g.Check({0,0,0},0)==MotionGuardVerdict::NotStarted);
    const MotionGuardConfig c{};
    CHECK(!g.Begin(c,{std::numeric_limits<float>::quiet_NaN(),0,0},0)&&!g.Ready());
    CHECK(!g.Begin(c,{2e6f,0,0},0));
    CHECK(!g.Begin(c,{0,0,0},-1));
    CHECK(!g.Begin({0,.02f,150*Ms},{0,0,0},0));
    CHECK(!g.Begin({4,-1,150*Ms},{0,0,0},0));
    CHECK(!g.Begin({4,.02f,151*Ms},{0,0,0},0)); // never wider than the 150 ms rule
    CHECK(!g.Begin({4,.02f,0},{0,0,0},0));
    CHECK(g.Begin(c,{0,0,0},5*Ms)&&g.Ready());return 0;
}
int DefaultsMatchKf2(){
    // KF2 defaults: 400 UU/s, 2 UU, 0.15 s with UU = cm.
    const MotionGuardConfig c{};
    CHECK(Near(c.speedLimitMetersPerSecond,4)&&Near(c.positionSlackMeters,.02f)&&c.maxGapNs==150*Ms);
    const auto i=MotionGuardConfig::Insertion();
    CHECK(Near(i.speedLimitMetersPerSecond,10)&&Near(i.positionSlackMeters,.12f)&&i.maxGapNs==TrackedSampleGapNs);
    CHECK(MotionGuard::ValidConfig(c)&&MotionGuard::ValidConfig(i)&&MotionGuard::ValidConfig(MotionGuardConfig::Rack()));return 0;
}
int AcceptsPlausibleStroke(){
    MotionGuard g;CHECK(g.Begin({},{0,0,0},0));
    // 11 ms frames at 3 m/s: 3.3 cm per frame is under 2 cm + 4.4 cm.
    for(int n=1;n<=20;++n)CHECK(g.Check({0,0,.033f*n},11*Ms*n)==MotionGuardVerdict::Accepted);
    // Same timestamp, same position: a duplicate packet is not motion.
    CHECK(g.Check({0,0,.033f*20},220*Ms)==MotionGuardVerdict::Accepted);
    // Just inside the bound passes (slack + speed*dt = 42 cm over 100 ms).
    CHECK(g.Check({0,0,.033f*20+.419f},320*Ms)==MotionGuardVerdict::Accepted);
    CHECK(g.Check({0,0,.033f*20+.419f+.421f},420*Ms)==MotionGuardVerdict::Implausible);return 0;
}
int RejectsAndRequiresBegin(){
    MotionGuard g;
    g.Begin({},{0,0,0},100*Ms);CHECK(g.Check({0,0,0},99*Ms)==MotionGuardVerdict::Backward&&!g.Ready());
    CHECK(g.Check({0,0,0},120*Ms)==MotionGuardVerdict::NotStarted);
    g.Begin({},{0,0,0},100*Ms);CHECK(g.Check({0,0,0},251*Ms)==MotionGuardVerdict::Gap&&!g.Ready());
    g.Begin({},{0,0,0},100*Ms);CHECK(g.Check({0,0,0},250*Ms)==MotionGuardVerdict::Accepted);
    g.Begin({},{0,0,0},100*Ms);CHECK(g.Check({0,0,.001f},100*Ms)==MotionGuardVerdict::Implausible);
    g.Begin({},{0,0,0},100*Ms);CHECK(g.Check({0,0,.07f},110*Ms)==MotionGuardVerdict::Implausible); // 7 cm in 10 ms
    g.Begin({},{0,0,0},100*Ms);CHECK(g.Check({std::numeric_limits<float>::infinity(),0,0},110*Ms)==MotionGuardVerdict::InvalidSample);
    g.Begin({},{0,0,0},100*Ms);CHECK(g.Check({0,0,0},-5)==MotionGuardVerdict::InvalidSample);
    // Recovery is a fresh Begin, never a jump to the new pose.
    CHECK(g.Begin({},{1,1,1},400*Ms)&&g.Check({1,1,1.01f},410*Ms)==MotionGuardVerdict::Accepted);return 0;
}
int InsertionEnvelopeAdmitsSlam(){
    // A 6 m/s slam at 90 Hz: rejected by the default carry guard, admitted by the insertion envelope.
    MotionGuard carry,insert;carry.Begin({},{0,0,0},0);insert.Begin(MotionGuardConfig::Insertion(),{0,0,0},0);
    CHECK(carry.Check({0,0,.2f},11*Ms)==MotionGuardVerdict::Implausible);
    CHECK(insert.Check({0,0,.2f},11*Ms)==MotionGuardVerdict::Accepted);
    // A tracking teleport (40 cm in one frame) is still rejected.
    CHECK(insert.Check({0,0,.6f},22*Ms)==MotionGuardVerdict::Implausible);return 0;
}
}
int main(){
    if(BeginRequiresValidInputs()||DefaultsMatchKf2()||AcceptsPlausibleStroke()||RejectsAndRequiresBegin()||InsertionEnvelopeAdmitsSlam())return 1;
    std::cout<<"MotionGuard tests passed\n";return 0;
}
