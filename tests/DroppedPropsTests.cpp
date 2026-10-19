#include "fvr/interaction/DroppedProps.h"
#include "Test.h"
#include <cstdio>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
fvr::math::Matrix4 At(float x,float y,float z){fvr::math::Matrix4 m;for(unsigned i=0;i<4;++i)m.values[i][i]=1;m.values[3]={x,y,z,1};return m;}
// Falls from hand height with the hand's velocity, bounces, rests on the floor
// and expires 12 s after the drop; the rotation at release is kept.
int FallsRestsAndExpires(){
    DroppedProps d;auto m=At(0,1.2f,0);m.values[0][1]=.6f;
    const auto id=d.Drop(7,m,{1,0,0},0,1000*Ms);CHECK(id&&*id==1);
    unsigned landed=0;std::int64_t landedAt=0;
    for(std::int64_t t=1011*Ms;t<=3000*Ms;t+=11*Ms){const auto s=d.Update(t);
        for(unsigned n=0;n<s.count;++n)if(s.notices[n].event==DroppedPropEvent::Landed){++landed;landedAt=t;}}
    const auto& p=*d.Props()[0];
    CHECK(landed==1&&p.resting&&p.worldFromProp.values[3][1]==0&&p.worldFromProp.values[0][1]==.6f);
    CHECK(landedAt>1400*Ms&&landedAt<2200*Ms);                     // ~0.5 s fall plus small bounces
    CHECK(p.worldFromProp.values[3][0]>.4f&&p.worldFromProp.values[3][0]<.7f); // carried forward, then friction
    CHECK(d.Update(12999*Ms).count==0&&d.Props()[0]);
    const auto e=d.Update(13000*Ms);CHECK(e.count==1&&e.notices[0].event==DroppedPropEvent::Expired&&e.notices[0].item==7&&!d.Props()[0]);
    const auto s=d.Stats();CHECK(s.dropped==1&&s.landed==1&&s.expired==1);return 0;}
// Three cosmetic slots: a fourth drop recycles the oldest. Non-finite input
// is rejected; a release at or below the floor rests at once.
int SlotsAndSafety(){
    DroppedProps d;for(unsigned n=0;n<3;++n)CHECK(d.Drop(n,At(0,1,0),{},0,(100+n)*Ms));
    CHECK(!d.TakeRecycled());const auto fourth=d.Drop(3,At(0,1,0),{},0,200*Ms);CHECK(fourth&&*fourth==4&&d.TakeRecycled()==1u);
    CHECK(!d.Drop(9,At(NAN,1,0),{},0,300*Ms)&&!d.Drop(9,At(0,1,0),{},0,0)&&d.Stats().rejected==2);
    DroppedProps g;CHECK(g.Drop(1,At(0,-.1f,0),{0,-3,0},0,10*Ms)&&g.Props()[0]->resting&&g.Props()[0]->worldFromProp.values[3][1]==0&&g.Stats().landed==1);
    // A huge throw is clamped, and a backwards clock never integrates.
    DroppedProps f;f.Drop(1,At(0,2,0),{100,0,0},0,10*Ms);CHECK(std::fabs(f.Props()[0]->velocity.x-8)<1e-4f);
    f.Update(5*Ms);CHECK(f.Props()[0]->worldFromProp.values[3][1]==2);return 0;}
}
namespace {
// Magazine profile: a released magazine keeps the hand's linear and angular
// velocity, tumbles while falling, bounces, comes to rest lying on a face with
// its box on the floor, and stays (never vanishes) for the full 30 s lifetime.
float Up(const fvr::math::Matrix4& m,unsigned r){return m.values[r][1];}
int MagazineTumblesRestsAndPersists(){
    DroppedProps d(MagazineDropConfig());
    auto m=At(.2f,1.3f,-.1f); // magazine upright (local +Y up), as when pulled from the well
    const auto id=d.Drop(11,m,{.8f,.4f,0},{0,0,6},.05f,1000*Ms);CHECK(id);
    bool rotated=false,everBelow=false,present=true;float maxX=.2f;std::int64_t landedAt=0;
    for(std::int64_t t=1011*Ms;t<30990*Ms;t+=11*Ms){
        const auto s=d.Update(t);
        for(unsigned n=0;n<s.count;++n)if(s.notices[n].event==DroppedPropEvent::Landed)landedAt=t;
        if(!d.Props()[0]){present=false;break;}
        const auto& p=*d.Props()[0];
        if(std::fabs(Up(p.worldFromProp,1)-1)>.3f)rotated=true;
        if(d.LowestY(p)<.05f-.002f)everBelow=true;
        maxX=std::max(maxX,p.worldFromProp.values[3][0]);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)CHECK(std::isfinite(p.worldFromProp.values[r][c]));
    }
    CHECK(present&&rotated&&!everBelow);
    std::printf("magazine landed at %.3f s, max x %.3f\n",double(landedAt-1000*Ms)*1e-9,double(maxX));
    CHECK(landedAt>1300*Ms&&landedAt<5100*Ms);  // falls ~1.2 m, bounces, settles
    CHECK(maxX>.45f);                             // carried forward by the hand velocity
    const auto& p=*d.Props()[0];CHECK(p.resting&&Near(d.LowestY(p),.05f,.001f));
    // Lies on a face: one local axis is vertical.
    float best=0;for(unsigned r=0;r<3;++r)best=std::max(best,std::fabs(Up(p.worldFromProp,r)));CHECK(best>.999f);
    // Orthonormal basis after integration.
    for(unsigned r=0;r<3;++r){float l=0;for(unsigned c=0;c<3;++c)l+=p.worldFromProp.values[r][c]*p.worldFromProp.values[r][c];CHECK(Near(l,1,.001f));}
    const auto e=d.Update(31000*Ms);CHECK(e.count==1&&e.notices[0].event==DroppedPropEvent::Expired&&!d.Props()[0]);
    CHECK(d.Stats().landed==1&&d.Stats().expired==1&&d.Stats().forcedSettles==0&&d.Stats().bounces>=1);return 0;}
// Four magazines live, the fifth recycles the oldest; a release already under
// the floor rests on it at once; a slow flat drop does not jitter.
int MagazineCapAndFloor(){
    DroppedProps d(MagazineDropConfig());
    for(unsigned n=0;n<4;++n)CHECK(d.Drop(n,At(float(n),1,0),{},0,(100+n)*Ms));
    CHECK(d.Live()==4&&!d.TakeRecycled());
    CHECK(d.Drop(4,At(9,1,0),{},0,200*Ms)&&d.TakeRecycled()==1u&&d.Live()==4);
    DroppedProps g(MagazineDropConfig());CHECK(g.Drop(1,At(0,-.3f,0),{0,0,0},0,10*Ms));
    {const auto& p=*g.Props()[0];CHECK(p.resting&&Near(g.LowestY(p),0,.001f));}
    // Lying flat (thin local X vertical), 2 cm above the floor: rests within half a second.
    DroppedProps h(MagazineDropConfig());auto flat=At(0,.0378f,0);flat.values[0]={0,1,0,0};flat.values[1]={-1,0,0,0};
    CHECK(h.Drop(2,flat,{},0,1000*Ms));std::int64_t rest=0;
    for(std::int64_t t=1005*Ms;t<2000*Ms&&!rest;t+=5*Ms){const auto s=h.Update(t);if(s.count&&s.notices[0].event==DroppedPropEvent::Landed)rest=t;}
    CHECK(rest&&rest<1500*Ms&&Near(h.LowestY(*h.Props()[0]),0,.001f)&&std::fabs(Up(h.Props()[0]->worldFromProp,0))>.999f);
    // Non-finite spin rejects.
    CHECK(!h.Drop(3,At(0,1,0),{},{NAN,0,0},0,3000*Ms));return 0;}
}
int main(){if(FallsRestsAndExpires()||SlotsAndSafety()||MagazineTumblesRestsAndPersists()||MagazineCapAndFloor())return 1;
    std::puts("DroppedProps: point fall/bounce/rest/12 s, magazine tumble/bounce/face rest/30 s persistence, cap recycle and input safety passed; renderer and headset unverified.");return 0;}
