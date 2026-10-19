#include "fvr/interaction/ActionPolicy.h"
#include <algorithm>
#include <cmath>
namespace fvr::interaction {
ActionOutput ActionPolicy::Update(const ActionSample& s,float snapDegrees,const TurnOptions& options) noexcept {
    ActionOutput out{};out.owner=owner_;
    const bool clockValid=s.timeNs>0&&s.frameNs>0;
    const auto age=clockValid?(s.frameNs>=s.timeNs?s.frameNs-s.timeNs:s.timeNs-s.frameNs):INT64_MAX;
    const bool finite=std::isfinite(s.forward)&&std::isfinite(s.strafe)&&std::isfinite(s.turn)&&std::isfinite(snapDegrees)&&snapDegrees>=0&&snapDegrees<=180
        &&std::isfinite(options.degreesPerSecond)&&options.degreesPerSecond>=30&&options.degreesPerSecond<=360
        &&std::isfinite(options.deadzone)&&options.deadzone>=.1f&&options.deadzone<=.4f;
    const bool valid=s.owner&&s.generation&&s.focused&&s.tracked&&s.playing&&s.bindingsVerified&&clockValid&&age<=150000000&&finite;
    const bool switched=s.owner!=owner_;
    const bool backwards=!switched&&(s.timeNs<time_||s.generation<generation_);
    const bool optionsChanged=options.smooth!=turnOptions_.smooth||options.degreesPerSecond!=turnOptions_.degreesPerSecond||options.deadzone!=turnOptions_.deadzone;
    if(!valid||switched||backwards||optionsChanged){
        out.released=held_;held_=0;forward_=strafe_=0;armed_=false;turn_={};generation_=0;time_=0;
        owner_=valid?s.owner:0;turnOptions_=options;
        // Suppress transition samples; ownership and turn settings need neutral.
        return out;
    }
    out.owner=owner_;
    if(s.generation==generation_||s.timeNs==time_){
        // Repeated packets cannot integrate time or generate another action edge.
        generation_=s.generation;out.active=armed_;out.held=held_;out.forward=forward_;out.strafe=strafe_;return out;
    }
    const bool longGap=time_&&s.timeNs-time_>150000000;
    const auto elapsed=time_?s.timeNs-time_:0;
    generation_=s.generation;time_=s.timeNs;
    if(longGap){out.released=held_;held_=0;forward_=strafe_=0;armed_=false;turn_={};return out;}
    constexpr std::uint32_t mask=Fire|AlternateFire|Use|Reload|Sprint|Menu|Jump|Crouch|NextWeapon|PreviousWeapon|Equipment|EquipmentBack;
    const bool turnNeutral=options.smooth?std::abs(s.turn)<=options.deadzone:std::abs(s.turn)<.2f;
    if(!armed_){if(!(s.held&mask)&&turnNeutral&&std::abs(s.forward)<.2f&&std::abs(s.strafe)<.2f){armed_=true;turn_.Update(true,0,s.timeNs,snapDegrees);}return out;}
    out.active=true;out.held=s.held&mask;out.pressed=out.held&~held_;out.released=held_&~out.held;held_=out.held;
    out.forward=std::clamp(s.forward,-1.f,1.f);out.strafe=std::clamp(s.strafe,-1.f,1.f);
    const auto length=std::hypot(out.forward,out.strafe);if(length>1){out.forward/=length;out.strafe/=length;}
    forward_=out.forward;strafe_=out.strafe;
    if(options.smooth){
        // Integrate each original sample interval once. Discard stalled-frame
        // time above 50 ms so a delayed packet cannot cause a large yaw jump.
        // Scale speed proportionally with stick deflection beyond deadzone.
        if(!turnNeutral){
            const float deflection=std::clamp((std::abs(s.turn)-options.deadzone)/(1.f-options.deadzone),0.f,1.f);
            out.turnDegrees=std::copysign(options.degreesPerSecond*deflection,s.turn)*float((std::min)(elapsed,std::int64_t{50000000}))*1.e-9f;
        }
    }else out.turnDegrees=turn_.Update(true,std::clamp(s.turn,-1.f,1.f),s.timeNs,snapDegrees);
    return out;
}
}
