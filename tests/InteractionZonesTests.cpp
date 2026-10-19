#include "fvr/interaction/InteractionZones.h"
#include "Test.h"
#include <cstdio>
using namespace fvr;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
using K=InteractionZoneKind;
math::Matrix4 Pose(float x,float y,float z,float yaw=0){math::Matrix4 m;for(unsigned i=0;i<4;++i)m.values[i][i]=1;
    const float c=std::cos(yaw),s=std::sin(yaw);m.values[0]={c,0,-s,0};m.values[2]={s,0,c,0};m.values[3]={x,y,z,1};return m;}
float Dist(const math::Matrix4& a,const math::Matrix4& b){return std::hypot(a.values[3][0]-b.values[3][0],a.values[3][1]-b.values[3][1],a.values[3][2]-b.values[3][2]);}
float Yaw(const math::Matrix4& m){return std::atan2(-m.values[0][2],m.values[0][0]);}

// One table: every zone valid, capture < release, and at least as generous as
// the hard-coded values it replaces.
int Table(){
    const auto t=DefaultInteractionZones();CHECK(ValidInteractionZones(t));
    CHECK(t[K::Support].captureMeters>=.16f&&t[K::Support].releaseMeters>=.28f&&t[K::Support].gripOn<=.7f);
    CHECK(t[K::MagazineGrab].captureMeters>=.09f&&t[K::MagazineInsert].captureMeters>=.07f&&t[K::MagazineInsert].releaseMeters>=.15f);
    CHECK(t[K::MagazineInsert].captureRadians>=.785f&&t[K::MagazineInsert].releaseRadians>=1.309f);
    CHECK(t[K::Pouch].captureMeters>=.18f&&t[K::Holster].captureMeters>=.17f);
    for(const auto& z:t.zones)CHECK(z.releaseMeters>z.captureMeters&&z.gripOn>z.gripOff);
    const auto big=ScaleInteractionZones(t,1.5f);CHECK(Near(big[K::Support].captureMeters,.30f)&&Near(big[K::Support].releaseMeters,.48f)&&ValidInteractionZones(big));
    const auto ignored=ScaleInteractionZones(t,5.f);CHECK(ignored[K::Support].captureMeters==t[K::Support].captureMeters);
    auto bad=t;bad[K::Pouch].releaseMeters=bad[K::Pouch].captureMeters;CHECK(!ValidInteractionZones(bad));
    return 0;}
// Capture at the capture radius, hold out to the release radius, and an
// angular cone with its own hysteresis.
int Hysteresis(){
    const auto z=DefaultInteractionZones()[K::Support];ZoneHysteresis h;
    CHECK(!h.Update(.21f,z)&&h.Update(.20f,z)&&h.Update(.31f,z)&&h.Update(.32f,z)&&!h.Update(.321f,z));
    CHECK(!h.Update(.25f,z)&&h.Entries()==1); // outside capture again: no re-entry until within .20
    CHECK(!h.Update(NAN,z)&&!h.Inside());
    const auto w=DefaultInteractionZones()[K::MagazineInsert];ZoneHysteresis a;
    CHECK(!a.Update(.05f,w,1.2f)&&a.Update(.05f,w,1.1f)&&a.Update(.15f,w,1.45f)&&!a.Update(.05f,w,1.5f));
    GripHysteresis g;CHECK(!g.Update(.9f,z).held);                      // held from the start: needs a neutral first
    CHECK(!g.Update(.3f,z).held&&!g.Update(.59f,z).held);
    auto r=g.Update(.6f,z);CHECK(r.held&&r.pressed);CHECK(g.Update(.36f,z).held);
    r=g.Update(.35f,z);CHECK(!r.held&&r.released);CHECK(!g.Update(NAN,z).held);return 0;}
// Shortest-arc rotation blend with exact endpoints, including a half turn.
int Blend(){
    const auto a=Pose(0,0,0),b=Pose(1,2,3,1.f);
    CHECK(Dist(*BlendTransform(a,b,0),a)<1e-6f&&Dist(*BlendTransform(a,b,1),b)<1e-6f&&Near(Yaw(*BlendTransform(a,b,1)),1.f,1e-4f));
    const auto mid=*BlendTransform(a,b,.5f);CHECK(Near(Yaw(mid),.5f,1e-4f)&&Near(mid.values[3][1],1.f));
    const auto half=*BlendTransform(a,Pose(0,0,0,3.1f),.5f);CHECK(Near(std::fabs(Yaw(half)),1.55f,1e-3f));
    auto degenerate=a;degenerate.values[1]={0,0,0,0};CHECK(!BlendTransform(degenerate,b,.5f));return 0;}
// Snap in over magnetIn, release decays in the tracked-hand frame (the
// returning hand does not follow the moving gun), and a re-grab mid-return
// starts from the visible hand without a jump.
int Magnet(){
    auto z=DefaultInteractionZones()[K::Support];z.magnetInNs=60*Ms;z.magnetOutNs=100*Ms;
    ContactMagnet m;const auto free=Pose(0,0,0);const auto gun=Pose(.5f,0,0);const auto local=Pose(0,.1f,0,.5f);
    auto r=m.Update(1000*Ms,free,local,gun,z,7);CHECK(r.contact&&r.weight==0&&Dist(r.target,free)<1e-6f);
    r=m.Update(1030*Ms,free,local,gun,z,7);CHECK(r.blending&&r.weight>.4f&&r.weight<.6f);
    const float halfway=Dist(r.target,free);CHECK(halfway>.1f&&halfway<.42f);
    r=m.Update(1060*Ms,free,local,gun,z,7);CHECK(r.weight==1&&Dist(r.target,Multiply(local,gun))<1e-6f);
    // Gun moves while held: the hand stays on the contact.
    const auto gun2=Pose(.6f,0,0);r=m.Update(1070*Ms,free,local,gun2,z,7);CHECK(Dist(r.target,Multiply(local,gun2))<1e-6f);
    const auto visible=r.target;
    // Release: same instant shows the held pose; then decays toward the tracked hand.
    r=m.Update(1071*Ms,free,std::nullopt,gun2,z);CHECK(!r.contact&&r.weight>.99f&&Dist(r.target,visible)<1e-3f);
    // The gun jumps away after release: the returning hand ignores it.
    r=m.Update(1121*Ms,free,std::nullopt,Pose(5,5,5),z);CHECK(r.blending&&r.weight>.4f&&r.weight<.6f&&Dist(r.target,free)<Dist(visible,free));
    // The tracked hand moves: the correction rides with it.
    const auto moved=Pose(0,0,1);r=m.Update(1131*Ms,moved,std::nullopt,gun2,z);CHECK(Dist(r.target,moved)<Dist(visible,free)&&r.target.values[3][2]>.9f);
    // Re-grab mid-return: the first frame matches the visible pose (no pop).
    const auto before=m.Update(1140*Ms,moved,std::nullopt,gun2,z).target;
    r=m.Update(1141*Ms,moved,local,gun2,z,8);CHECK(r.contact&&Dist(r.target,before)<.02f);
    r=m.Update(1210*Ms,moved,local,gun2,z,8);CHECK(r.weight==1);
    r=m.Update(1211*Ms,moved,std::nullopt,gun2,z);r=m.Update(1320*Ms,moved,std::nullopt,gun2,z);
    CHECK(r.weight==0&&Dist(r.target,moved)<1e-6f&&!m.Active());
    // Zero timing snaps; a backwards clock resets to the tracked hand.
    auto snap=z;snap.magnetInNs=snap.magnetOutNs=0;ContactMagnet s;
    CHECK(s.Update(10*Ms,free,local,gun,snap).weight==1&&s.Update(11*Ms,free,std::nullopt,gun,snap).weight==0);
    CHECK(Dist(s.Update(12*Ms,free,local,gun,snap).target,Multiply(local,gun))<1e-6f&&Dist(s.Update(5*Ms,free,local,gun,snap).target,free)<1e-6f);
    return 0;}
}
int main(){if(Table()||Hysteresis()||Blend()||Magnet())return 1;
    std::puts("InteractionZones: tuning table, capture/release and grip hysteresis, shortest-arc blend, magnet snap-in, hand-frame release decay and continuous re-grab passed; headset feel unverified.");return 0;}
