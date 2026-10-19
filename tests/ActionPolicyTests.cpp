#include "Test.h"
#include "fvr/interaction/ActionPolicy.h"
#include <initializer_list>
#include <limits>
using namespace fvr::interaction;
namespace {
struct SmoothFixture {
    ActionPolicy policy;
    TurnOptions options{true,90,.2f};
    ActionSample sample{};
    SmoothFixture(){
        sample.owner=1;sample.generation=1;sample.timeNs=sample.frameNs=1000000000;
        sample.focused=sample.tracked=sample.playing=sample.bindingsVerified=true;
        policy.Update(sample,30,options);Tick();
    }
    ActionOutput Tick(std::int64_t dt=10000000){
        ++sample.generation;sample.frameNs=sample.timeNs+=dt;
        return policy.Update(sample,30,options);
    }
};
int SmoothTests(){
    // Cadence changes do not change a constant signed rate, provided intervals
    // stay within the deliberate 50 ms stall cap.
    for(const auto dt:{1000000LL,10000000LL,50000000LL}){
        SmoothFixture f;f.sample.turn=1.f;float total=0;
        for(std::int64_t elapsed=0;elapsed<1000000000;elapsed+=dt)total+=f.Tick(dt).turnDegrees;
        CHECK(Near(total,90,.002f));
    }
    // Proportional deflection: .6f turn (50% deflection above .2f deadzone) yields 45 deg/s.
    for(const auto dt:{10000000LL,50000000LL}){
        SmoothFixture f;f.sample.turn=.6f;float total=0;
        for(std::int64_t elapsed=0;elapsed<1000000000;elapsed+=dt)total+=f.Tick(dt).turnDegrees;
        CHECK(Near(total,45,.002f));
    }
    SmoothFixture variable;variable.sample.turn=1;float total=0;
    for(int cycle=0;cycle<20;++cycle)for(const auto dt:{1000000LL,7000000LL,13000000LL,29000000LL})total+=variable.Tick(dt).turnDegrees;
    CHECK(Near(total,90,.002f));
    SmoothFixture stalled;stalled.sample.turn=1;total=0;
    for(int i=0;i<10;++i)total+=stalled.Tick(100000000).turnDegrees;
    CHECK(Near(total,45)); // Each 100 ms sample discards 50 ms permanently.
    auto out=stalled.Tick();CHECK(Near(out.turnDegrees,.9f));

    SmoothFixture signedRate;signedRate.sample.turn=.2f;out=signedRate.Tick();CHECK(!out.turnDegrees);
    signedRate.sample.turn=.21f;out=signedRate.Tick();CHECK(Near(out.turnDegrees,.9f*(.01f/.8f),.0001f));
    signedRate.sample.turn=.6f;out=signedRate.Tick();CHECK(Near(out.turnDegrees,.45f));
    signedRate.sample.turn=1;out=signedRate.Tick();CHECK(Near(out.turnDegrees,.9f));
    signedRate.sample.turn=-.6f;out=signedRate.Tick();CHECK(Near(out.turnDegrees,-.45f));
    signedRate.sample.turn=-.21f;out=signedRate.Tick();CHECK(Near(out.turnDegrees,-.9f*(.01f/.8f),.0001f));
    signedRate.sample.turn=-.2f;out=signedRate.Tick();CHECK(!out.turnDegrees);

    SmoothFixture duplicate;duplicate.sample.turn=1;duplicate.sample.held=Fire;
    out=duplicate.Tick();CHECK(Near(out.turnDegrees,.9f)&&out.pressed==Fire);
    out=duplicate.policy.Update(duplicate.sample,30,duplicate.options);CHECK(!out.turnDegrees&&!out.pressed&&out.held==Fire);
    duplicate.sample.frameNs+=20000000;
    out=duplicate.policy.Update(duplicate.sample,30,duplicate.options);CHECK(!out.turnDegrees&&!out.pressed);
    ++duplicate.sample.generation; // Another generation at the same source time.
    out=duplicate.policy.Update(duplicate.sample,30,duplicate.options);CHECK(!out.turnDegrees&&!out.pressed);
    duplicate.sample.timeNs+=10000000;duplicate.sample.frameNs=duplicate.sample.timeNs;
    out=duplicate.policy.Update(duplicate.sample,30,duplicate.options);CHECK(!out.turnDegrees); // Same generation, later timestamp.
    out=duplicate.Tick();CHECK(Near(out.turnDegrees,1.8f)); // Source interval was never consumed by the duplicate.
    duplicate.sample.frameNs+=20000000;
    ++duplicate.sample.generation;duplicate.sample.timeNs+=10000000;
    out=duplicate.policy.Update(duplicate.sample,30,duplicate.options);CHECK(Near(out.turnDegrees,.9f)); // Render age is not dt.

    // A new generation at the same source time with a changed button level is
    // still a repeat: it neither presses nor releases. The next source sample
    // settles the level, so nothing stays held (Bc2ContextInteract menu case).
    SmoothFixture level;level.sample.held=Use;out=level.Tick();CHECK(out.pressed==Use);
    ++level.sample.generation;level.sample.held=0;out=level.policy.Update(level.sample,30,level.options);
    CHECK(out.held==Use&&!out.released&&!out.pressed);
    out=level.Tick();CHECK(!out.held&&out.released==Use);

    // Mode, rate and deadzone changes release held requests and require neutral.
    for(int setting=0;setting<3;++setting){
        SmoothFixture f;f.sample.turn=1;f.sample.held=Fire;out=f.Tick();CHECK(out.pressed==Fire);
        if(setting==0)f.options.smooth=false;
        else if(setting==1)f.options.degreesPerSecond=180;
        else f.options.deadzone=.3f;
        out=f.Tick();CHECK(out.released==Fire&&!out.active&&!out.turnDegrees);
        out=f.Tick();CHECK(!out.active&&!out.pressed&&!out.turnDegrees);
        f.sample.turn=0;f.sample.held=0;out=f.Tick();CHECK(!out.turnDegrees);
        f.sample.turn=1;f.sample.held=Fire;out=f.Tick();CHECK(out.pressed==Fire);
        CHECK(Near(out.turnDegrees,setting==0?30.f:(setting==1?1.8f:.9f)));
    }
    SmoothFixture fromSnap;fromSnap.options.smooth=false;fromSnap.sample.turn=0;fromSnap.Tick();fromSnap.Tick();
    fromSnap.sample.turn=1;out=fromSnap.Tick();CHECK(Near(out.turnDegrees,30));
    fromSnap.options.smooth=true;out=fromSnap.Tick();CHECK(!out.turnDegrees&&!out.active);
    out=fromSnap.Tick();CHECK(!out.turnDegrees&&!out.active);
    fromSnap.sample.turn=0;fromSnap.Tick();fromSnap.sample.turn=1;out=fromSnap.Tick();CHECK(Near(out.turnDegrees,.9f));

    // All policy losses, owner changes, backwards clocks and long sample gaps
    // drop elapsed turn time. A held stick cannot resume until neutral arrives.
    for(int loss=0;loss<8;++loss){
        SmoothFixture f;f.sample.turn=1;f.sample.held=Fire;out=f.Tick();CHECK(out.pressed==Fire);
        if(loss==0)f.sample.focused=false;
        else if(loss==1)f.sample.tracked=false;
        else if(loss==2)f.sample.playing=false;
        else if(loss==3)f.sample.bindingsVerified=false;
        else if(loss==4)++f.sample.owner;
        else if(loss==5){f.sample.timeNs-=20000000;f.sample.frameNs=f.sample.timeNs;}
        else if(loss==6)--f.sample.generation;
        if(loss==5||loss==6)out=f.policy.Update(f.sample,30,f.options);
        else out=f.Tick(loss==7?150000001:10000000);
        CHECK(out.released==Fire&&!out.active&&!out.turnDegrees&&out.owner==1);
        f.sample.focused=f.sample.tracked=f.sample.playing=f.sample.bindingsVerified=true;
        out=f.Tick();CHECK(!out.active&&!out.pressed&&!out.turnDegrees);
        out=f.Tick();CHECK(!out.active&&!out.turnDegrees);
        f.sample.turn=0;f.sample.held=0;f.Tick();
        f.sample.turn=1;f.sample.held=Fire;out=f.Tick();CHECK(Near(out.turnDegrees,.9f)&&out.pressed==Fire);
    }
    SmoothFixture gapBoundary;gapBoundary.sample.turn=1;out=gapBoundary.Tick(150000000);CHECK(Near(out.turnDegrees,4.5f)&&out.active);

    const auto nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(const auto speed:{29.99f,360.01f,0.f,-90.f,nan,inf}){
        SmoothFixture f;f.sample.turn=1;f.sample.held=Fire;f.Tick();f.options.degreesPerSecond=speed;
        out=f.Tick();CHECK(out.released==Fire&&!out.active&&!out.turnDegrees);
        out=f.Tick();CHECK(!out.active&&!out.turnDegrees);
    }
    for(const auto deadzone:{.099f,.401f,-.2f,nan,inf}){
        SmoothFixture f;f.sample.turn=1;f.sample.held=Fire;f.Tick();f.options.deadzone=deadzone;
        out=f.Tick();CHECK(out.released==Fire&&!out.active&&!out.turnDegrees);
        out=f.Tick();CHECK(!out.active&&!out.turnDegrees);
    }
    for(const auto speed:{30.f,360.f})for(const auto deadzone:{.1f,.4f}){
        SmoothFixture f;f.options.degreesPerSecond=speed;f.options.deadzone=deadzone;f.Tick();f.Tick();
        f.sample.turn=deadzone;out=f.Tick();CHECK(out.active&&!out.turnDegrees);
        f.sample.turn=1;out=f.Tick();CHECK(Near(out.turnDegrees,speed*.01f));
    }
    return 0;
}
}
int main(){
    CHECK(SmoothTests()==0);
    ActionPolicy p;ActionSample s{};s.owner=1;s.generation=1;s.timeNs=s.frameNs=1000000000;s.focused=s.tracked=s.playing=s.bindingsVerified=true;
    auto next=[&](){++s.generation;s.frameNs=s.timeNs+=10000000;return p.Update(s);};
    CHECK(!p.Update(s).held);CHECK(!next().held);s.held=Fire;s.forward=s.strafe=1;s.turn=1;auto out=next();CHECK(out.pressed==Fire&&out.held==Fire&&Near(out.turnDegrees,30));CHECK(Near(std::hypot(out.forward,out.strafe),1));
    out=p.Update(s);CHECK(out.held==Fire&&!out.pressed&&!out.turnDegrees&&out.forward>0);s.focused=false;out=next();CHECK(out.released==Fire&&!out.held&&out.owner==1);
    s.focused=true;CHECK(!next().pressed);CHECK(!next().pressed);s.held=0;s.forward=s.strafe=s.turn=0;next();s.held=Fire;CHECK(next().pressed==Fire);
    s.owner=2;out=next();CHECK(out.released==Fire&&out.owner==1);CHECK(!next().held);s.held=0;next();s.held=Fire;CHECK(next().pressed==Fire);
    s.frameNs+=160000001;out=p.Update(s);CHECK(out.released==Fire&&!out.held);s.held=0;s.frameNs=s.timeNs;next();next();s.held=Fire;CHECK(next().pressed==Fire);
    s.bindingsVerified=false;CHECK(next().released==Fire);CHECK(!next().held);
    return 0;
}
