#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <cmath>
namespace fvr::bc2 {
// Test-arsenal chord: left trigger fully pulled + right stick flick left/right.
// Edge-triggered: one step per flick, the stick must return below 0.35 before the
// next. A flick already under way when the chord starts never fires.
enum class ArsenalStep { None, Next, Previous };
struct ArsenalChordResult {bool suppress=false;ArsenalStep step=ArsenalStep::None;};
class ArsenalChord {
public:
    static constexpr float TriggerThreshold=.9f,FlickThreshold=.75f,ResetThreshold=.35f;
    ArsenalChordResult Update(bool tracked,float leftTrigger,float stickX,float stickY,bool blocked=false)noexcept {
        if(!tracked){Reset();return {};}
        const float ax=std::fabs(stickX),ay=std::fabs(stickY);
        if(ax<ResetThreshold&&ay<ResetThreshold){latched_=false;suppressUntilNeutral_=false;}
        const bool held=leftTrigger>=TriggerThreshold;
        suppressUntilNeutral_|=held;
        ArsenalChordResult out;out.suppress=suppressUntilNeutral_;
        // A competing button/fire gesture must finish and the stick must be
        // neutral before catalog selection can arm again. Never replay a flick.
        if(blocked){latched_=true;return out;}
        const bool flick=ax>=FlickThreshold&&ax>ay+.15f;
        if(flick){
            if(held&&!latched_)out.step=stickX>0?ArsenalStep::Next:ArsenalStep::Previous;
            latched_=true;
        }
        return out;
    }
    // Read the source packet, not actionInput: physical reload intentionally
    // masks actionInput's left trigger before semantic actions are derived.
    ArsenalChordResult Route(const interaction::InputFrame& source,interaction::InputFrame& actionInput,
        bool available,std::uint64_t actorGeneration)noexcept {
        if(actor_!=actorGeneration||space_!=source.spaceGeneration||
           (lastNs_&&(source.predictedNs<lastNs_||source.predictedNs-lastNs_>150000000)))Reset();
        actor_=actorGeneration;space_=source.spaceGeneration;lastNs_=source.predictedNs;
        const auto& l=source.hands[0];const auto& r=source.hands[1];
        const bool tracked=available&&actorGeneration&&interaction::ValidInput(source)&&source.focused&&source.headValid&&
            l.gripTracked&&r.aimTracked&&(l.active&interaction::Trigger)&&(r.active&interaction::Trigger)&&(r.active&interaction::Stick);
        const bool blocked=r.trigger>.25f||l.held||r.held;
        const auto out=Update(tracked,l.trigger,r.stickX,r.stickY,blocked);
        if(out.suppress)actionInput.hands[1].stickX=actionInput.hands[1].stickY=0;
        return out;
    }
    void Reset()noexcept{latched_=true;suppressUntilNeutral_=false;}
private:
    bool latched_=true,suppressUntilNeutral_=false;
    std::uint64_t actor_=0,space_=0;std::int64_t lastNs_=0;
};
}
