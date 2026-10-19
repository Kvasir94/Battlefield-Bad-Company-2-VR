#include "Test.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/AimFrame.h"
#include "../src/games/bc2/Bc2InputBinding.h"
#include "../src/games/bc2/Bc2ReloadButtonGesture.h"
#include "../src/games/bc2/Bc2ArsenalChord.h"
#include <cmath>
#include <initializer_list>
#include <limits>
#include <cstring>
using namespace fvr;
namespace {
int ArsenalChordTests(){
    using S=bc2::ArsenalStep;bc2::ArsenalChord c;
    CHECK(c.Update(true,0,0,0).step==S::None); // arms after neutral
    CHECK(c.Update(true,0,1,0).step==S::None&&!c.Update(true,0,1,0).suppress); // no chord: stick untouched
    CHECK(c.Update(true,0,0,0).step==S::None);
    CHECK(c.Update(true,1,1,0).step==S::Next&&c.Update(true,1,1,0).suppress); // one step per flick
    CHECK(c.Update(true,1,.5f,0).step==S::None&&c.Update(true,1,1,0).step==S::None); // needs release below .35
    CHECK(c.Update(true,1,0,0).step==S::None);
    CHECK(c.Update(true,1,-1,0).step==S::Previous);
    CHECK(c.Update(true,1,0,0).step==S::None);
    CHECK(c.Update(true,1,0,1).step==S::None); // vertical never fires
    CHECK(c.Update(true,1,0,0).step==S::None);
    CHECK(c.Update(true,.5f,1,0).step==S::None); // partial trigger is not the chord
    CHECK(c.Update(true,1,1,0).step==S::None); // flick began before the chord completed
    CHECK(c.Update(false,1,0,0).step==S::None&&c.Update(true,1,1,0).step==S::None); // lost tracking requires neutral
    c.Update(true,1,0,0);CHECK(c.Update(true,1,1,0).step==S::Next);
    CHECK(c.Update(true,0,1,0).suppress); // releasing LT must not replay the captured flick as turn
    CHECK(!c.Update(true,0,0,0).suppress);
    CHECK(c.Update(true,1,0,0,true).step==S::None);
    CHECK(c.Update(true,1,1,0).step==S::None); // competing action requires neutral again
    c.Update(true,1,0,0);CHECK(c.Update(true,1,-1,0).step==S::Previous);
    return 0;
}
int ReloadButtonTests(){
    bc2::ReloadInputRouting routing;
    using Lane=bc2::ReloadInputLane;
    interaction::HandInteractionOwner routeOwner{1,2,3,4};
    CHECK(routing.Update(routeOwner,1,Lane::Native,true));
    CHECK(!routing.NativeAllowed()); // Selection while held cannot replay Reload.
    CHECK(!routing.Update(routeOwner,1,Lane::Native,false));
    CHECK(routing.NativeAllowed());
    routing.Update(routeOwner,1,Lane::Native,true);CHECK(routing.NativeAllowed());
    routing.Update(routeOwner,1,Lane::Unavailable,true);CHECK(!routing.NativeAllowed());
    routing.Update(routeOwner,1,Lane::Physical,false);CHECK(!routing.NativeAllowed());
    routing.Update(routeOwner,1,Lane::Native,true);CHECK(!routing.NativeAllowed());
    routing.Update(routeOwner,1,Lane::Native,false);CHECK(routing.NativeAllowed());
    routing.Update(routeOwner,2,Lane::Native,true);CHECK(!routing.NativeAllowed()); // Address reuse.
    routing.Update(routeOwner,2,Lane::Native,false);CHECK(routing.NativeAllowed());
    ++routeOwner.equipGeneration;
    routing.Update(routeOwner,2,Lane::Native,true);CHECK(!routing.NativeAllowed());
    // Instant native lane: one pulse on the press edge after a neutral packet; held never repeats.
    routing.Reset();
    routing.Update(routeOwner,5,Lane::Native,true);CHECK(!routing.NativePressEdge()); // Held across admission.
    routing.Update(routeOwner,5,Lane::Native,true);CHECK(!routing.NativePressEdge());
    routing.Update(routeOwner,5,Lane::Native,false);CHECK(!routing.NativePressEdge());
    routing.Update(routeOwner,5,Lane::Native,true);CHECK(routing.NativePressEdge());
    routing.Update(routeOwner,5,Lane::Native,true);CHECK(!routing.NativePressEdge());
    routing.Update(routeOwner,5,Lane::Native,false);routing.Update(routeOwner,5,Lane::Native,true);CHECK(routing.NativePressEdge());
    routing.Update(routeOwner,6,Lane::Native,true);CHECK(!routing.NativePressEdge()); // Weapon change while held.
    routing.Update(routeOwner,6,Lane::Physical,false);routing.Update(routeOwner,6,Lane::Physical,true);CHECK(!routing.NativePressEdge());
    bc2::ReloadButtonGesture gesture;
    interaction::HandInteractionOwner owner{1,2,3,4};
    std::uint64_t sequence=1;std::int64_t time=1000000000;
    const auto tick=[&](bool held,bool eligible=true,std::int64_t dt=10000000){
        ++sequence;time+=dt;return gesture.Update(owner,sequence,time,eligible,held);
    };
    using Event=bc2::ReloadButtonEvent;
    for(auto lane:{Lane::Unavailable,Lane::Native,Lane::Physical})for(auto event:{Event::None,Event::ShortRelease,Event::LongHold}){
        const auto dispatch=bc2::DispatchReloadButton(lane,event);
        CHECK(dispatch.nativePulse==(lane==Lane::Native&&event==Event::LongHold));
        CHECK(dispatch.physicalShort==(lane==Lane::Physical&&event==Event::ShortRelease));
        CHECK(dispatch.physicalAutomatic==(lane==Lane::Physical&&event==Event::LongHold));
    }
    // Physical incarnation replacement while held resets a partial hold; it
    // cannot turn into a delayed long reload on the next owned item.
    routing.Reset();
    const auto routed=[&](std::uint64_t incarnation,bool held){
        if(routing.Update(owner,incarnation,Lane::Physical,held))gesture.Reset();
        return tick(held);
    };
    routed(1,false);routed(1,false);routed(1,true);
    CHECK(routed(2,true)==Event::None);
    for(int i=0;i<70;++i)CHECK(routed(2,true)==Event::None);
    CHECK(routed(2,false)==Event::None);
    CHECK(routed(2,true)==Event::None);
    CHECK(routed(2,false)==Event::ShortRelease);
    gesture.Reset();
    auto event=tick(false);CHECK(event==Event::None);event=tick(false);CHECK(event==Event::None);
    event=tick(true);CHECK(event==Event::None);event=tick(false);CHECK(event==Event::ShortRelease);
    event=tick(false);CHECK(event==Event::None);
    event=tick(true);CHECK(event==Event::None);
    for(int i=0;i<5;++i){event=tick(true,true,100000000);CHECK(event==Event::None);}
    event=tick(true,true,100000000);CHECK(event==Event::LongHold);
    event=gesture.Update(owner,sequence,time,true,true);CHECK(event==Event::None);
    event=tick(true,true,100000000);CHECK(event==Event::None);
    event=tick(false);CHECK(event==Event::None); // Long hold never replays short eject/rack.
    event=tick(true);CHECK(event==Event::None);
    // Lost gun ownership/focus cancels instead of treating loss as a release.
    event=tick(false,false);CHECK(event==Event::None);
    event=tick(true);CHECK(event==Event::None);event=tick(false);CHECK(event==Event::None);
    event=tick(true);CHECK(event==Event::None);++owner.equipGeneration;
    event=tick(false);CHECK(event==Event::None);event=tick(false);CHECK(event==Event::None);
    event=tick(true);CHECK(event==Event::None);event=tick(false,true,150000001);CHECK(event==Event::None);
    event=tick(false);CHECK(event==Event::None);event=tick(true);CHECK(event==Event::None);
    time-=20000000;event=tick(false);CHECK(event==Event::None);
    event=tick(false);CHECK(event==Event::None);event=tick(true);CHECK(event==Event::None);
    // A native gather with reused source generation cannot lengthen a hold.
    event=gesture.Update(owner,sequence,time+100000000,true,true);CHECK(event==Event::None);
    event=tick(false);CHECK(event==Event::ShortRelease);
    return 0;
}
struct SmoothControllers {
    interaction::InputFrame frame{};
    interaction::InputOwner owner{123,1,true,true};
    interaction::ControllerActions actions;
    interaction::TurnOptions options{true,90,.2f};
    SmoothControllers(){
        frame.generation=1;frame.spaceGeneration=1;frame.predictedNs=1000000000;frame.focused=frame.headValid=true;
        for(auto& h:frame.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
        Tick();Tick();Tick();
    }
    interaction::ActionOutput Tick(std::int64_t dt=10000000){
        ++frame.generation;frame.predictedNs+=dt;
        return actions.Update(frame,owner,frame.predictedNs,options);
    }
};
int ArsenalReloadRoutingTests(){
    using S=bc2::ArsenalStep;
    SmoothControllers f;bc2::ArsenalChord chord;
    interaction::InputFrame action{};interaction::ActionOutput output{};
    const auto tick=[&](bool available=true,std::uint64_t actor=1,std::int64_t dt=10000000){
        ++f.frame.generation;f.frame.predictedNs+=dt;
        action=f.frame;action.hands[0].trigger=0; // live physical-reload masking
        const auto routed=chord.Route(f.frame,action,available,actor);
        output=f.actions.Update(action,f.owner,action.predictedNs,f.options);
        return routed;
    };
    auto& l=f.frame.hands[0];auto& r=f.frame.hands[1];
    tick();tick();l.trigger=1;tick();r.stickX=1;
    CHECK(tick().step==S::Next); // regression: masked LH stays zero, raw LH still reaches chord
    CHECK(action.hands[0].trigger==0&&f.frame.hands[0].trigger==1&&f.frame.hands[1].stickX==1);
    CHECK(action.hands[1].stickX==0&&!output.turnDegrees&&!(output.held&(interaction::Jump|interaction::PreviousWeapon|interaction::Fire)));
    CHECK(tick().step==S::None);
    l.trigger=0;CHECK(tick().suppress&&!output.turnDegrees);
    r.stickX=0;CHECK(!tick().suppress);r.stickX=1;CHECK(tick().step==S::None&&output.turnDegrees);
    r.stickX=0;l.trigger=1;tick();r.stickX=-1;CHECK(tick().step==S::Previous);
    // Fire, A reload, menu/back/face and stick-click/recenter gestures all block
    // catalog selection. Their release with a held flick never selects late.
    for(unsigned conflict=0;conflict<9;++conflict){
        r.stickX=0;r.trigger=0;l.held=r.held=0;tick();
        if(conflict==0)r.trigger=.6f;
        else if(conflict<=4)l.held=std::uint32_t(interaction::Primary)<<(conflict-1);
        else r.held=std::uint32_t(interaction::Primary)<<(conflict-5);
        r.stickX=1;CHECK(tick().step==S::None);
        r.trigger=0;l.held=r.held=0;CHECK(tick().step==S::None);
        r.stickX=0;tick();r.stickX=1;CHECK(tick().step==S::Next);
    }
    // Tracking/admission loss, a stale source gap, or actor/reference-space
    // replacement must not carry an already-held catalog flick into recovery.
    for(unsigned loss=0;loss<8;++loss){
        r.stickX=0;tick();r.stickX=1;CHECK(tick().step==S::Next);
        if(loss==0){l.gripTracked=false;CHECK(tick().step==S::None);l.gripTracked=true;}
        else if(loss==1){r.aimTracked=false;CHECK(tick().step==S::None);r.aimTracked=true;}
        else if(loss==2){f.frame.headValid=false;CHECK(tick().step==S::None);f.frame.headValid=true;}
        else if(loss==3)CHECK(tick(false).step==S::None);
        else if(loss==4)CHECK(tick(true,1,160000000).step==S::None);
        else if(loss==5){++f.frame.spaceGeneration;CHECK(tick().step==S::None);}
        else if(loss==6)CHECK(tick(true,2).step==S::None);
        else {f.frame.focused=false;CHECK(tick().step==S::None);f.frame.focused=true;}
        CHECK(tick().step==S::None);r.stickX=0;tick();r.stickX=-1;CHECK(tick().step==S::Previous);
    }
    // Outside the catalog chord, exact shipped A/UP/DOWN mappings remain.
    l.trigger=0;r.stickX=0;r.stickY=0;tick();tick();
    r.held=interaction::Primary;tick();CHECK(output.held==interaction::Reload);
    r.held=0;tick();r.stickY=1;tick();CHECK(output.held==interaction::Jump);
    r.stickY=0;tick();r.stickY=-1;tick();CHECK(output.held==interaction::PreviousWeapon);
    return 0;
}
// This proves HMD/analog intent reaches the native action cache. The game's
// downstream sprint eligibility/speed remains outside this CPU fixture.
int SprintMovementBasisTests(){
    constexpr float pi=3.141592653589793f;
    CHECK(!interaction::SprintOwnsHeading(0)&&interaction::SprintOwnsHeading(interaction::Sprint));
    CHECK(!interaction::SprintOwnsHeading(interaction::Sprint|interaction::Fire)&&!interaction::SprintOwnsHeading(interaction::Sprint|interaction::AlternateFire));
    for(float headYaw:{0.f,pi*.5f,-.7f})for(bool sprint:{false,true}){
        SmoothControllers f;f.options={};auto& left=f.frame.hands[0];
        const float pitch=.6f,cy=std::cos(headYaw*.5f),sy=std::sin(headYaw*.5f);
        const float cp=std::cos(pitch*.5f),sp=std::sin(pitch*.5f);
        f.frame.head.orientation={cy*sp,-sy*cp,sy*sp,cy*cp};
        left.held=sprint?interaction::StickClick:0;
        for(bool tracked:{true,false}){
            f.frame.hands[1].aimTracked=tracked;
            for(unsigned direction=0;direction<8;++direction){
                const float stickAngle=direction*pi*.25f;
                left.stickY=.65f*std::cos(stickAngle);left.stickX=.65f*std::sin(stickAngle);
                auto out=f.Tick();CHECK(out.active&&bool(out.held&interaction::Sprint)==sprint);
                const float strength=(.65f-.18f)/.82f;
                const float expectedBodyForward=strength*std::cos(stickAngle+headYaw);
                const float expectedBodyStrafe=strength*std::sin(stickAngle+headYaw);
                CHECK(Near(out.forward,expectedBodyForward)&&Near(out.strafe,expectedBodyStrafe));
                // Sprint rebases the native yaw onto the HMD heading, so the
                // native axes are the plain stick vector for any gun/room yaw
                // while the world heading is still body + head + stick.
                CHECK(interaction::SprintOwnsHeading(out.held)==sprint);
                for(float bodyYaw:{0.f,.4f,-2.5f,pi}){
                    const auto aim=interaction::ControllerAim(f.frame.referenceHead,f.frame.head);CHECK(aim&&Near(aim->yaw,headYaw));
                    const float sprintYaw=interaction::SprintNativeYaw(bodyYaw,aim->yaw);
                    auto native=out;interaction::MovementToNativeHeading(bodyYaw,sprintYaw,native.forward,native.strafe);
                    CHECK(Near(native.forward,strength*std::cos(stickAngle))&&Near(native.strafe,strength*std::sin(stickAngle)));
                    const float wf=native.forward*std::cos(sprintYaw)-native.strafe*std::sin(sprintYaw);
                    const float ws=native.forward*std::sin(sprintYaw)+native.strafe*std::cos(sprintYaw);
                    CHECK(Near(wf,strength*std::cos(stickAngle+headYaw+bodyYaw))&&Near(ws,strength*std::sin(stickAngle+headYaw+bodyYaw)));
                }
                // A changed native yaw and a turned body need current absolute
                // headings even while the gun-hand tracker is unavailable.
                const float bodyYaw=tracked?.4f:1.1f,nativeYaw=tracked?-1.2f:2.3f;
                interaction::MovementToNativeHeading(bodyYaw,nativeYaw,out.forward,out.strafe);
                std::array<std::byte,bc2::InputBytes> cache{};bc2::InputOverride patch;
                CHECK(patch.Apply(cache,out));patch.Commit();
                float forward=0,strafe=0;std::uint32_t buttons=0;
                std::memcpy(&forward,cache.data()+8,4);std::memcpy(&strafe,cache.data()+12,4);
                std::memcpy(&buttons,cache.data()+0x98,4);CHECK(bool(buttons&(1u<<31))==sprint);
                const float worldForward=forward*std::cos(nativeYaw)-strafe*std::sin(nativeYaw);
                const float worldStrafe=forward*std::sin(nativeYaw)+strafe*std::cos(nativeYaw);
                CHECK(Near(worldForward,strength*std::cos(stickAngle+headYaw+bodyYaw)));
                CHECK(Near(worldStrafe,strength*std::sin(stickAngle+headYaw+bodyYaw)));
                CHECK(Near(std::hypot(forward,strafe),strength));
            }
        }
    }
    return 0;
}
// Right stick click cycle: short click forward on release, long click backward
// at the threshold, chords/duplicates/gaps/bounce/recovery never switch.
int StickCycleTests(){
    constexpr auto cycle=interaction::Equipment|interaction::EquipmentBack;
    SmoothControllers f;f.options={};auto& left=f.frame.hands[0];auto& right=f.frame.hands[1];
    const auto hold=[&](int samples,std::int64_t dt=50000000){std::uint32_t seen=0;for(int i=0;i<samples;++i)seen|=f.Tick(dt).pressed;return seen;};
    f.Tick();f.Tick();
    // Short click: nothing while held, Equipment exactly once on release.
    right.held=interaction::StickClick;CHECK(!(hold(4)&cycle));                 // 200 ms
    right.held=0;auto out=f.Tick();CHECK(out.pressed==interaction::Equipment);
    out=f.Tick();CHECK(out.released==interaction::Equipment&&!out.pressed);
    // Long click: EquipmentBack once at 450 ms, nothing on release.
    f.Tick(50000000);right.held=interaction::StickClick;CHECK(!(hold(9)&cycle)); // press + 400 ms
    out=f.Tick(50000000);CHECK(out.pressed==interaction::EquipmentBack);         // 450 ms
    CHECK(!(hold(10)&cycle));right.held=0;out=f.Tick();CHECK(!(out.pressed&cycle));
    // Duplicate packets cannot lengthen a hold into a backward cycle.
    f.Tick(50000000);right.held=interaction::StickClick;f.Tick();
    for(int i=0;i<20;++i){out=f.actions.Update(f.frame,f.owner,f.frame.predictedNs,f.options);CHECK(!(out.pressed&cycle));}
    right.held=0;CHECK(f.Tick().pressed==interaction::Equipment);
    // Left click during the press (recenter chord / sprint) spoils it either way.
    f.Tick(50000000);right.held=interaction::StickClick;f.Tick();left.held=interaction::StickClick;
    CHECK(!(hold(12)&cycle));left.held=0;right.held=0;CHECK(!(f.Tick().pressed&cycle));
    // Sprint still works on the left click alone.
    f.Tick(50000000);left.held=interaction::StickClick;CHECK(f.Tick().held==interaction::Sprint);left.held=0;f.Tick();
    // Contact bounce: a press 10 ms after a release is ignored until released.
    f.Tick(50000000);right.held=interaction::StickClick;f.Tick();right.held=0;CHECK(f.Tick(10000000).pressed==interaction::Equipment);
    right.held=interaction::StickClick;f.Tick(10000000);right.held=0;CHECK(!(f.Tick(10000000).pressed&cycle));
    // A sample gap over 150 ms while held cannot be measured: no switch.
    f.Tick(50000000);right.held=interaction::StickClick;f.Tick();
    out=f.Tick(160000000);CHECK(!(out.pressed&cycle));right.held=0;out=f.Tick();CHECK(!(out.pressed&cycle));
    // Right tracking loss while held needs a fresh release/press after recovery.
    f.Tick(50000000);right.held=interaction::StickClick;f.Tick();right.aimTracked=false;f.Tick();right.aimTracked=true;
    f.Tick();f.Tick();right.held=0;CHECK(!(f.Tick().pressed&cycle));
    f.Tick(50000000);right.held=interaction::StickClick;f.Tick();right.held=0;CHECK(f.Tick().pressed==interaction::Equipment);
    // Kill switch: no backward; any release after a lone press is Equipment.
    interaction::StickCycleOptions legacy{};legacy.backward=false;f.actions.SetStickCycle(legacy);
    f.Tick(50000000);right.held=interaction::StickClick;CHECK(!(hold(20)&cycle));
    right.held=0;CHECK(f.Tick().pressed==interaction::Equipment);
    // A configured threshold is honored.
    interaction::StickCycleOptions quick{};quick.backwardHoldNs=250000000;f.actions.SetStickCycle(quick);
    f.Tick(50000000);right.held=interaction::StickClick;f.Tick();CHECK(!(hold(4)&cycle));
    CHECK(f.Tick(50000000).pressed==interaction::EquipmentBack);right.held=0;f.Tick();
    return 0;
}
int SmoothControllerTests(){
    constexpr auto weapons=interaction::Jump|interaction::NextWeapon|interaction::PreviousWeapon;
    SmoothControllers f;auto& left=f.frame.hands[0];auto& right=f.frame.hands[1];
    right.stickX=.4f;right.stickY=.6f;auto out=f.Tick();CHECK(out.active&&!out.turnDegrees&&!(out.held&weapons));
    right.stickY=-.6f;out=f.Tick();CHECK(!out.turnDegrees&&!(out.held&weapons));
    right.stickX=.6f;right.stickY=.8f;out=f.Tick();CHECK(out.pressed==interaction::Jump&&!out.turnDegrees);
    out=f.actions.Update(f.frame,f.owner,f.frame.predictedNs,f.options);CHECK(!out.pressed&&!out.turnDegrees&&out.held==interaction::Jump);
    right.stickX=.9f;right.stickY=.6f;out=f.Tick();CHECK(out.held==interaction::Jump&&!out.pressed&&!out.turnDegrees);
    right.stickX=right.stickY=0;out=f.Tick();CHECK(out.released==interaction::Jump);
    right.stickX=-.6f;right.stickY=-.8f;out=f.Tick();CHECK(out.pressed==interaction::PreviousWeapon&&!out.turnDegrees);
    right.stickX=-.9f;right.stickY=-.6f;out=f.Tick();CHECK(out.held==interaction::PreviousWeapon&&!out.turnDegrees);
    right.stickX=right.stickY=0;f.Tick();
    right.stickX=right.stickY=1;out=f.Tick();CHECK(Near(out.turnDegrees,.9f)&&!(out.held&weapons));
    right.stickX=right.stickY=-1;out=f.Tick();CHECK(Near(out.turnDegrees,-.9f)&&!(out.held&weapons));
    right.stickX=right.stickY=0;f.Tick();

    // Smooth mode retains release-triggered equipment selection and click chord
    // suppression. Left sprint/movement do not rearm when only turn settings change.
    right.held=interaction::StickClick;out=f.Tick();CHECK(!(out.held&interaction::Equipment));
    right.held=0;out=f.Tick();CHECK(out.pressed==interaction::Equipment);
    out=f.Tick();CHECK(out.released==interaction::Equipment);
    right.held=interaction::StickClick;f.Tick();left.held=interaction::StickClick;f.Tick();
    left.held=right.held=0;out=f.Tick();CHECK(!(out.held&interaction::Equipment)&&!(out.pressed&interaction::Equipment));
    left.held=interaction::StickClick;left.stickY=1;right.stickX=1;out=f.Tick();
    CHECK(out.held==interaction::Sprint&&out.forward>.99f&&Near(out.turnDegrees,.9f));
    f.options.degreesPerSecond=180;out=f.Tick();CHECK(out.held==interaction::Sprint&&out.forward>.99f&&!out.turnDegrees);
    out=f.Tick();CHECK(out.held==interaction::Sprint&&out.forward>.99f&&!out.turnDegrees);
    right.stickX=0;f.Tick();right.stickX=1;out=f.Tick();CHECK(out.held==interaction::Sprint&&Near(out.turnDegrees,1.8f));
    left.held=0;left.stickY=0;right.stickX=0;f.Tick();

    // A reference-space or owner-generation change cannot carry yaw across the
    // transition, even when a stick remains held throughout recovery.
    for(int loss=0;loss<5;++loss){
        SmoothControllers context;auto& hand=context.frame.hands[1];hand.stickX=1;
        out=context.Tick();CHECK(Near(out.turnDegrees,.9f));
        if(loss==0)++context.frame.spaceGeneration;
        else if(loss==1)++context.owner.generation;
        else if(loss==2)hand.aimTracked=false;
        else if(loss==3){context.frame.focused=false;for(auto& h:context.frame.hands){h.active=h.held=0;h.gripTracked=h.aimTracked=false;h.stickX=h.stickY=0;}}
        else context.frame.predictedNs-=20000000;
        out=context.Tick();CHECK(!out.turnDegrees);
        context.frame.focused=true;
        for(auto& h:context.frame.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}
        hand.stickX=1;context.Tick();out=context.Tick();CHECK(!out.turnDegrees);
        hand.stickX=0;context.Tick();context.Tick();hand.stickX=1;
        out=context.Tick();CHECK(Near(out.turnDegrees,.9f));
    }
    return 0;
}
}
int main(){
    CHECK(SprintMovementBasisTests()==0);
    CHECK(ReloadButtonTests()==0);
    CHECK(ArsenalChordTests()==0);
    CHECK(ArsenalReloadRoutingTests()==0);
    CHECK(SmoothControllerTests()==0);
    CHECK(StickCycleTests()==0);
    interaction::InputFrame f{};f.generation=1;f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
    for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    CHECK(interaction::ValidInput(f));auto bad=f;bad.hands[1].trigger=std::numeric_limits<float>::quiet_NaN();CHECK(!interaction::ValidInput(bad));
    bad=f;bad.focused=false;CHECK(!interaction::ValidInput(bad));bad=f;bad.hands[0].active=0;bad.hands[0].held=interaction::Primary;CHECK(!interaction::ValidInput(bad));
    interaction::ControllerActions actions;interaction::InputOwner owner{123,1,true,true};
    auto step=[&](){++f.generation;f.predictedNs+=11000000;return actions.Update(f,owner,f.predictedNs);};
    step();step();step(); // Context transition, owner transition, neutral arming.
    f.hands[1].trigger=.9f;f.hands[0].stickX=f.hands[0].stickY=1;
    auto out=step();CHECK(out.pressed==interaction::Fire&&out.owner==123);CHECK(std::abs(std::hypot(out.forward,out.strafe)-1)<.0001f);
    out=actions.Update(f,owner,f.predictedNs);CHECK(out.held==interaction::Fire&&!out.pressed);
    out=actions.Update(f,owner,f.predictedNs+100000001);CHECK(out.released==interaction::Fire&&!out.held);
    step();step();CHECK(!step().held); // Holding through recovery must not re-fire.
    f.hands[1].trigger=0;f.hands[0].stickX=f.hands[0].stickY=0;step();f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    ++owner.generation;out=step();CHECK(out.released==interaction::Fire);CHECK(!step().held);CHECK(!step().held);
    f.hands[1].trigger=0;step();f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    ++f.spaceGeneration;CHECK(step().released==interaction::Fire);CHECK(!step().held);
    f.hands[1].trigger=0;step();step();f.hands[1].trigger=1;CHECK(step().held==interaction::Fire);
    // Trigger hysteresis: .55f below pull threshold does not fire, .62f fires, .50f stays held, .40f releases.
    f.hands[1].trigger=0;step();step();
    f.hands[1].trigger=.55f;out=step();CHECK(!out.held&&!out.pressed);
    f.hands[1].trigger=.62f;out=step();CHECK(out.pressed==interaction::Fire&&out.held==interaction::Fire);
    f.hands[1].trigger=.50f;out=step();CHECK(out.held==interaction::Fire&&!out.pressed&&!out.released);
    f.hands[1].trigger=.40f;out=step();CHECK(out.released==interaction::Fire&&!out.held);
    f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    f.focused=false;f.hands={};CHECK(step().released==interaction::Fire);
    // Physical left turn maps stick forward to native left strafe. Head pitch
    // cannot invent a vertical movement axis; stick drift stays neutral.
    f.focused=f.headValid=true;f.hands={};for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    step();step();step();f.hands[0].stickY=.1f;out=step();CHECK(!out.forward&&!out.strafe);
    f.hands[0].stickY=1;f.head.orientation={0,std::sqrt(.5f),0,std::sqrt(.5f)};
    out=step();CHECK(std::abs(out.forward)<.0001f&&out.strafe<-.999f);
    f.hands[0].stickY=0;f.head={};step();
    f.hands[0].held=interaction::Primary|interaction::Secondary;f.hands[1].held=interaction::Primary|interaction::Secondary;
    out=step();CHECK(out.held==(interaction::Use|interaction::Crouch|interaction::Reload));
    f.hands[0].held=f.hands[1].held=0;step();
    f.hands[1].held=interaction::Primary;CHECK(step().held==interaction::Reload); // A: reload only.
    f.hands[1].held=0;step();
    f.hands[1].held=interaction::Secondary;CHECK(!step().held); // B: no gameplay action.
    f.hands[1].held=0;step();
    // A fully pulled off-hand trigger is pose input, not ADS/AlternateFire.
    // It cannot hold the action policy unarmed or interfere with the gun hand.
    f.hands[0].held=f.hands[1].held=0;f.hands[0].trigger=1;f.hands[1].trigger=0;
    out=step();CHECK(!out.held&&!out.pressed&&f.hands[0].trigger==1);
    out=actions.Update(f,owner,f.predictedNs);CHECK(!out.held&&!out.pressed);
    f.hands[1].trigger=1;out=step();CHECK(out.held==interaction::Fire&&out.pressed==interaction::Fire);
    f.hands[1].trigger=0;f.hands[0].held=interaction::Primary;
    out=step();CHECK(out.held==interaction::Use&&out.pressed==interaction::Use);
    f.hands[0].gripTracked=false;CHECK(step().released==interaction::Use);
    f.hands[0].gripTracked=true;f.hands[0].held=0;step();step();
    out=step();CHECK(!out.held&&!out.pressed);
    f.hands[0].trigger=0;step();
    // Left tracking loss releases only left actions while the held gun trigger
    // continues. Recovery with a held left button requires that hand's neutral.
    f.hands[0].held=f.hands[1].held=0;f.hands[1].trigger=0;step();
    f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    f.hands[0].held=interaction::Primary;CHECK(step().pressed==interaction::Use);
    f.hands[0].gripTracked=false;out=step();
    CHECK(out.active&&out.held==interaction::Fire&&out.released==interaction::Use&&!out.pressed);
    f.hands[0].gripTracked=true;step();step();out=step();CHECK(out.held==interaction::Fire);
    f.hands[0].held=0;step();f.hands[0].held=interaction::Primary;CHECK(step().pressed==interaction::Use);
    f.hands[0].held=0;f.hands[0].stickY=1;out=step();CHECK(out.forward>.99f);
    // Right tracking loss releases fire while left movement continues. A held
    // trigger cannot re-fire until released after right-hand recovery.
    f.hands[1].aimTracked=false;out=step();CHECK(out.active&&out.forward>.99f&&out.released==interaction::Fire&&!out.held);
    f.hands[1].aimTracked=true;step();step();out=step();CHECK(!out.held&&out.forward>.99f);
    f.hands[1].trigger=0;step();f.hands[1].trigger=1;CHECK(step().pressed==interaction::Fire);
    f.hands[0].gripTracked=false;f.hands[1].aimTracked=false;out=step();CHECK(!out.active&&!out.held&&!out.forward);
    f.hands={};
    // One vertical flick per return to center, including duplicate packets,
    // held-stick equip transitions, diagonal turns, and right tracking recovery.
    f.hands={};for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    step();step();step();f.hands[1].stickY=1;out=step();CHECK(out.pressed==interaction::Jump&&!out.turnDegrees);
    CHECK(!actions.Update(f,owner,f.predictedNs).pressed);CHECK(!step().pressed);
    f.hands[1].stickY=.6f;CHECK(step().held==interaction::Jump);
    ++owner.generation;step();step();CHECK(!step().pressed);
    f.hands[1].stickY=0;step();step();f.hands[1].stickY=-1;CHECK(step().pressed==interaction::PreviousWeapon);
    f.hands[1].aimTracked=false;CHECK(step().released==interaction::PreviousWeapon);
    f.hands[1].aimTracked=true;step();step();CHECK(!step().pressed);
    f.hands[1].stickY=0;step();step();f.hands[1].stickX=f.hands[1].stickY=1;out=step();CHECK(!(out.held&(interaction::Jump|interaction::PreviousWeapon))&&out.turnDegrees);
    f.hands[1].stickX=f.hands[1].stickY=0;step();f.hands[1].stickY=1;CHECK(step().pressed==interaction::Jump);
    // Right stick click selects equipment on release; the recenter chord never does.
    f.hands[1].stickY=0;step();f.hands[1].held=interaction::StickClick;CHECK(!(step().held&interaction::Equipment));
    f.hands[1].held=0;CHECK(step().pressed==interaction::Equipment);CHECK(step().released==interaction::Equipment);
    f.hands[1].held=interaction::StickClick;step();f.hands[0].held=interaction::StickClick;step();
    f.hands[0].held=f.hands[1].held=0;out=step();CHECK(!(out.held&interaction::Equipment)&&!(out.pressed&interaction::Equipment));
    // Reference yaw, pitch-independent gravity, native eye scale is separate.
    math::Pose p{};p.position={.2f,0,-.5f};p.orientation={std::sin(.2f),0,0,std::cos(.2f)};
    const auto ref=interaction::UprightReference(p);CHECK(ref&&ref->position.y==0&&ref->orientation.x==0&&ref->orientation.w==1);
    p.orientation={0,std::sin(.4f),0,std::cos(.4f)};const auto yaw=interaction::UprightReference(p);CHECK(yaw&&std::abs(yaw->orientation.y-p.orientation.y)<.0001f);
    p.orientation={std::sqrt(.5f),0,0,std::sqrt(.5f)};CHECK(!interaction::UprightReference(p));
    // Gesture is not a repeated button edge; duplicate samples and tracking gaps cannot complete it.
    interaction::RecenterGesture gesture;f.focused=f.headValid=true;f.hands={};
    for(auto& h:f.hands){h.gripTracked=true;h.active=interaction::StickClick;}
    auto g=[&](std::int64_t dt=11000000){++f.generation;f.predictedNs+=dt;return gesture.Update(f);};
    CHECK(!g());CHECK(!g());for(auto& h:f.hands)h.held=interaction::StickClick;
    CHECK(!g());CHECK(!gesture.Update(f));for(int i=0;i<9;++i)CHECK(!g(100000000));CHECK(g(100000000));CHECK(!g(100000000));
    for(auto& h:f.hands)h.held=0;CHECK(!g());for(auto& h:f.hands)h.held=interaction::StickClick;CHECK(!g());CHECK(!g(2000000000));
    for(int i=0;i<12;++i)CHECK(!g(100000000));
    return 0;
}
