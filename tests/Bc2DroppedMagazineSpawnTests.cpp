#include "Bc2DroppedMagazineSpawn.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
using namespace fvr;
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Ms=1000000;
math::Matrix4 At(float x,float y,float z){math::Matrix4 m;for(unsigned i=0;i<4;++i)m.values[i][i]=1;m.values[3]={x,y,z,1};return m;}
// Rotation about world Y by angle a (row-vector basis rows).
math::Matrix4 Yaw(float a,float x,float y,float z){auto m=At(x,y,z);const float c=std::cos(a),s=std::sin(a);
    m.values[0]={c,0,-s,0};m.values[2]={s,0,c,0};return m;}
bool Near(float a,float b,float e){return std::fabs(a-b)<e;}

// Settings: defaults, every switch, and out-of-range values ignored.
void Settings(){
    const auto none=ParseDroppedMagazineSettings([](std::string_view){return std::string_view{};});
    CHECK(none.enabled&&none.lifetimeSeconds==30&&none.maxLive==4&&none.visibleMeters==8);
    std::map<std::string,std::string> env{{"FVR_BC2_DROPPED_MAGS","0"},{"FVR_BC2_DROPPED_MAG_LIFETIME_S","45"},
        {"FVR_BC2_DROPPED_MAG_MAX","6"},{"FVR_BC2_DROPPED_MAG_VISIBLE_M","12.5"}};
    const auto get=[&](std::string_view n)->std::string_view{const auto i=env.find(std::string(n));return i==env.end()?std::string_view{}:std::string_view(i->second);};
    const auto s=ParseDroppedMagazineSettings(get);
    CHECK(!s.enabled&&s.lifetimeSeconds==45&&s.maxLive==6&&s.visibleMeters==12.5f);
    const auto c=DroppedMagazineConfig(s);CHECK(c.lifetimeNs==45000000000ll&&c.maxLive==6&&c.halfExtents.y>.08f);
    env={{"FVR_BC2_DROPPED_MAG_LIFETIME_S","9999"},{"FVR_BC2_DROPPED_MAG_MAX","0"},{"FVR_BC2_DROPPED_MAG_VISIBLE_M","x"}};
    const auto bad=ParseDroppedMagazineSettings(get);CHECK(bad.enabled&&bad.lifetimeSeconds==30&&bad.maxLive==4&&bad.visibleMeters==8);
}
// The regression: the release gather has no raw contact (the hand claim was
// just released). The committed drop still spawns from the carried pose with
// the hand's velocity and spin from the history before it.
void ReleaseWithoutRawContactStillSpawns(){
    DroppedMagazineSpawner s;
    for(int k=0;k<=20;++k){const std::int64_t t=1000*Ms+k*5*Ms;
        const auto wrist=Yaw(.02f*float(k),.01f*float(k),1.2f,0); // 2 m/s along x, 4 rad/s yaw
        s.ObserveWrist(wrist,t);s.ObserveCarried(Yaw(.02f*float(k),.01f*float(k)+.05f,1.15f,0),t);}
    s.Commit(false,1105*Ms);CHECK(s.PendingDrop());
    const auto launch=s.Resolve(1105*Ms,std::nullopt,At(0,1,0));
    CHECK(launch&&launch->fromCarried&&!launch->fromWell);
    CHECK(Near(launch->worldFromItem.values[3][0],.25f,1e-4f)&&Near(launch->worldFromItem.values[3][1],1.15f,1e-4f));
    CHECK(Near(launch->velocity.x,2,.05f)&&Near(launch->velocity.y,0,.01f));
    CHECK(Near(launch->angularVelocity.y,4,.2f));
    CHECK(!s.PendingDrop()&&s.Counts().spawned==1&&s.Counts().lost==0);
    // The full drop trajectory: tumbles, lands, rests and persists 30 s.
    interaction::DroppedProps props(DroppedMagazineConfig({}));
    const auto id=props.Drop(1,launch->worldFromItem,launch->velocity,launch->angularVelocity,0,1105*Ms);CHECK(id);
    bool landed=false;
    for(std::int64_t t=1110*Ms;t<=31000*Ms;t+=10*Ms){
        const auto st=props.Update(t);for(unsigned n=0;n<st.count;++n)landed|=st.notices[n].event==interaction::DroppedPropEvent::Landed;
        CHECK(props.Props()[0]&&props.LowestY(*props.Props()[0])>-.002f);}
    CHECK(landed&&props.Props()[0]->resting&&props.Props()[0]->worldFromProp.values[3][0]>.6f);
    CHECK(props.Update(31106*Ms).count==1&&!props.Props()[0]);
}
// Button eject starts in the well with no hand velocity; a drop with neither a
// carried pose nor contact waits up to 300 ms, then spawns when contact returns.
void EjectAndDeferral(){
    DroppedMagazineSpawner s;s.Commit(true,1000*Ms);
    const auto eject=s.Resolve(1000*Ms,At(0,1,0),At(.1f,1.1f,.2f));
    CHECK(eject&&eject->fromWell&&eject->worldFromItem.values[3][2]==.2f&&eject->velocity.x==0&&eject->angularVelocity.y==0);
    s.Commit(false,2000*Ms);
    CHECK(!s.Resolve(2000*Ms,std::nullopt,std::nullopt)&&s.PendingDrop()&&!s.Resolve(2200*Ms,std::nullopt,std::nullopt));
    const auto late=s.Resolve(2250*Ms,At(.3f,1,0),std::nullopt);CHECK(late&&!late->fromCarried&&late->worldFromItem.values[3][0]==.3f);
    CHECK(s.Counts().deferredGathers==2&&s.Counts().lost==0);
    // Never contact again: counted as lost after 300 ms, never silently.
    s.Commit(false,3000*Ms);CHECK(!s.Resolve(3100*Ms,std::nullopt,std::nullopt));
    CHECK(!s.Resolve(3301*Ms,std::nullopt,std::nullopt)&&!s.PendingDrop()&&s.Counts().lost==1&&s.Counts().committed==3);
    // A stale carried pose (older than 500 ms) is not used.
    DroppedMagazineSpawner t;t.ObserveCarried(At(5,5,5),1000*Ms);t.Commit(false,1600*Ms);
    const auto fresh=t.Resolve(1600*Ms,At(1,1,1),std::nullopt);CHECK(fresh&&!fresh->fromCarried&&fresh->worldFromItem.values[3][0]==1);
}
}
int main(){Settings();ReleaseWithoutRawContactStillSpawns();EjectAndDeferral();
    std::puts("Bc2DroppedMagazineSpawn: settings/kill switch, receipt-keyed spawn without raw contact, hand velocity/spin, eject origin, bounded deferral/loss and 30 s persistence passed; renderer and headset unverified.");return 0;}
