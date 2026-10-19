#include "fvr/interaction/ReloadHints.h"
#include "Bc2ReloadGuidance.h"
#include "Test.h"
#include <cmath>
#include <cstdio>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
// A part lights once its step has been pending for 0.75 s, pulses to 0.6
// over 0.3 s and back, and goes dark as soon as the step is done.
int DelayPulseAndClear(){
    ReloadHints h;ReloadHintInput in;in.takeFromPouch=true;
    in.nowNs=1000*Ms;auto o=h.Update(in);CHECK(!o.lit&&o.alpha[1]==0);
    in.nowNs=1749*Ms;o=h.Update(in);CHECK(!o.lit);
    in.nowNs=1750*Ms;o=h.Update(in);CHECK(o.lit==2&&o.changed==2&&o.alpha[1]==0);
    in.nowNs=1900*Ms;o=h.Update(in);CHECK(std::fabs(o.alpha[1]-.3f)<1e-4f&&!o.changed);
    in.nowNs=2050*Ms;o=h.Update(in);CHECK(std::fabs(o.alpha[1]-.6f)<1e-4f);
    in.nowNs=2200*Ms;o=h.Update(in);CHECK(std::fabs(o.alpha[1]-.3f)<1e-4f);
    in.takeFromPouch=false;in.nowNs=2210*Ms;o=h.Update(in);CHECK(!o.lit&&o.changed==2&&o.alpha[1]==0);
    return 0;}
// Each part keeps its own clock; a backwards clock restarts it.
int IndependentClocks(){
    ReloadHints h;ReloadHintInput in;in.rackHandle=true;in.nowNs=100*Ms;h.Update(in);
    in.seatMagazine=true;in.nowNs=600*Ms;h.Update(in);
    in.nowNs=900*Ms;auto o=h.Update(in);CHECK(o.lit==(1u<<3));
    in.nowNs=1400*Ms;o=h.Update(in);CHECK(o.lit==((1u<<3)|(1u<<2)));
    in.nowNs=50*Ms;o=h.Update(in);CHECK(!o.lit);return 0;}
int VisibleZoneUsesOriginalSpaceAndTransform(){
    using namespace fvr;using namespace fvr::bc2;using namespace fvr::graphics;
    math::Matrix4 center{};for(unsigned n=0;n<4;++n)center.values[n][n]=1;center.values[3]={12,34,56,1};
    const HandInteractionOwner who{10,11,12,13};
    for(float units:{1.f,100.f}){
        const auto p=ReloadGuidanceZone(center,.04f,units,.3f,who,1000*Ms,1080*Ms,1020*Ms,1);CHECK(p);
        CHECK(p->geometry==InteractionZonePropKey&&p->opacity==75&&p->spaceGeneration==13);
        CHECK(p->world.values[3]==center.values[3]&&Near(p->world.values[0][0],.04f*units));
        CHECK(p->deadlineNs==1080*Ms&&!BodyPropFresh(*p,1080*Ms));
        CHECK(p->equipmentGeneration==BodyPropWireEpoch(BodyPropSourceKind::Guidance,12,1));
    }
    CHECK(!ReloadGuidanceZone(center,.04f,1,0,who,1000*Ms,1080*Ms,1020*Ms,1));
    CHECK(!ReloadGuidanceZone(center,.04f,1,.3f,who,1000*Ms,1101*Ms,1020*Ms,1));
    auto missing=who;missing.space=0;CHECK(!ReloadGuidanceZone(center,.04f,1,.3f,missing,1000*Ms,1080*Ms,1020*Ms,1));
    const auto mesh=BuildInteractionZoneProp();CHECK(mesh.mesh.vertices.size()==3*32*6);
    for(const auto& v:mesh.mesh.vertices)CHECK(std::hypot(v.x,v.y,v.z)<=1.00001f);
    return 0;
}
}
int main(){if(DelayPulseAndClear()||IndependentClocks()||VisibleZoneUsesOriginalSpaceAndTransform())return 1;
    std::puts("ReloadHints: delay/pulse/clear and per-part clocks passed; rendering unverified.");return 0;}
