#pragma once
#include "ControllerInput.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace fvr::interaction {
// ReloadSeated: a magazine latched in the well (ReloadInsertionHaptic::Seated),
// before and independent of the native receipt that ReloadApplied reports.
// KF2 VR / AS2 cues (docs/BC2_RELOAD_PARITY.md F2, M7): ReloadGrab an ammo
// object or magazine taken (KF2 0.3/10 ms), ReloadFailed an insertion that
// fell out (two 0.3/30 ms peaks 60 ms apart), ReloadRack a pump or charging
// handle stroke completed (1.0/100 ms), ReloadGunSeat the gun hand's share of
// a seat (0.35/30 ms), ReloadHover the empty loading hand entering a pouch
// or shell-belt contact (0.1/12 ms tick; AmmoSupply applies the hysteresis).
// KF2 VRManualPump (MP:525-526, MP:545): PumpRear the support hand's rear-stop
// knock (0.7/45 ms) with PumpGunKnock on the gun hand (0.3/30 ms); PumpForward
// the forward lock on both hands (1.0/80 ms).
// Stick weapon selection: SwitchSelected one light tick on the gun hand when
// the server actually selected the requested item (0.25/12 ms); SwitchRejected
// two 0.2/25 ms peaks 50 ms apart when a request found nothing to select or a
// running reload owns the weapon. Design defaults, not headset-tuned. An older
// host rejects kinds above its range: no pulse, no other effect.
enum class FeedbackKind : std::uint32_t {ReloadCapture=1,ReloadApplied=2,ReloadSeated=3,ReloadGrab=4,ReloadFailed=5,ReloadRack=6,ReloadGunSeat=7,ReloadHover=8,PumpRear=9,PumpGunKnock=10,PumpForward=11,
    SwitchSelected=12,SwitchRejected=13,ReloadReady=14,GunFire=15,DryFire=16,
    StopAll=17}; // StopAll: control event, no pulse; cancels every running/pending haptic (weapon switch, death, pause)
// Runtime-neutral event. Times are monotonic host/native shared-clock
// nanoseconds, NOT predicted XR display time. No native object crosses IPC.
struct FeedbackEvent {
    std::uint64_t id=0,inputSequence=0,space=0;
    std::int64_t observedNs=0,deadlineNs=0;
    FeedbackKind kind{};std::uint32_t hand=0;
    float recoilWeight=-1.f;bool recoilSupport=false;
};
inline bool ValidFeedback(const FeedbackEvent& e,std::int64_t now)noexcept {
    return e.id&&e.inputSequence&&e.space&&e.hand<2&&
        unsigned(e.kind)>=1&&unsigned(e.kind)<=17&&
        ((e.recoilWeight==-1.f&&!e.recoilSupport)||(e.kind==FeedbackKind::GunFire&&
            std::isfinite(e.recoilWeight)&&e.recoilWeight>=0.f&&e.recoilWeight<=1.f))&&
        e.observedNs>0&&now>=e.observedNs&&e.deadlineNs>now&&
        e.deadlineNs>e.observedNs&&e.deadlineNs-e.observedNs<=100000000;
}
// A delayed pulse owns its origin; there need not be a new event on its frame.
struct HapticPulse {unsigned hand=0;float amplitude=0;std::int64_t durationNs=0;
    std::uint64_t eventId=0;FeedbackKind kind{};};
struct HapticFeedbackResult {bool stop=false,stopHaptics=false;std::optional<HapticPulse> pulse;};
// One instance per XR session. Consume IDs even on rejected delivery; no event
// is retried across focus/menu/tracking loss, reference changes or reconnect.
class HapticFeedbackPolicy {
public:
    void Suspend()noexcept {barrier_=std::max(barrier_,lastInput_);second_.reset();}
    HapticFeedbackResult Update(const InputFrame& input,std::int64_t now,const FeedbackEvent* event=nullptr)noexcept {
        HapticFeedbackResult out;
        const bool transition=space_&&space_!=input.spaceGeneration;
        const bool reversed=(lastNow_&&now<lastNow_)||input.generation<lastInput_;
        const bool safe=now>0&&ValidInput(input)&&input.focused&&input.headValid&&
            input.hands[0].gripTracked&&input.hands[1].gripTracked&&!reversed;
        if(transition){barrier_=input.generation?input.generation-1:0;out.stop=true;second_.reset();}
        if(!safe){barrier_=std::max(barrier_,input.generation);out.stop=true;second_.reset();}
        // Validate transitions before emitting a scheduled pulse, including
        // reversed input/clock and a suspend between the two peaks.
        if(second_&&now>=second_->dueNs){out.pulse=second_->pulse;second_.reset();}
        space_=input.spaceGeneration;lastInput_=input.generation;lastNow_=now;
        if(!event||!event->id||event->id<=seen_)return out;
        seen_=event->id;
        if(!safe||!ValidFeedback(*event,now)||event->space!=space_||
           event->inputSequence<=barrier_||event->inputSequence>input.generation)return out;
        // Explicit stop: cancel the delayed second peak and every running pulse, without
        // raising the input barrier (a switch tick may follow on the same input frame).
        if(event->kind==FeedbackKind::StopAll){second_.reset();out.stopHaptics=true;return out;}
        if(event->kind==FeedbackKind::ReloadCapture){
            if(lastCapture_&&now-lastCapture_<60000000)return out;
            // Stronger shell cues after headset feedback; authority and freshness are unchanged.
            lastCapture_=now;out.pulse=HapticPulse{event->hand,.55f,35000000};
        }else if(event->kind==FeedbackKind::ReloadSeated){
            // AS2 InsertAmmo / KF2 Seat: longer loading-hand latch pulse,
            // independent of the native receipt and gun-hand seat knock.
            out.pulse=HapticPulse{event->hand,.5f,100000000};
        }else if(event->kind==FeedbackKind::ReloadGrab)out.pulse=HapticPulse{event->hand,.3f,10000000};
        else if(event->kind==FeedbackKind::ReloadFailed){out.pulse=HapticPulse{event->hand,.3f,30000000};
            second_=Delayed{HapticPulse{event->hand,.3f,30000000,event->id,event->kind},now+60000000};}
        else if(event->kind==FeedbackKind::ReloadRack)out.pulse=HapticPulse{event->hand,1.f,100000000};
        else if(event->kind==FeedbackKind::ReloadGunSeat)out.pulse=HapticPulse{event->hand,.35f,30000000};
        else if(event->kind==FeedbackKind::ReloadHover)out.pulse=HapticPulse{event->hand,.1f,12000000};
        // Owned ready confirmation, not a fictional rack/animation event.
        // Two small clicks differ from the empty-seat thump and rack stroke.
        else if(event->kind==FeedbackKind::ReloadReady){out.pulse=HapticPulse{event->hand,.18f,12000000};
            second_=Delayed{HapticPulse{event->hand,.18f,12000000,event->id,event->kind},now+40000000};}
        else if(event->kind==FeedbackKind::PumpRear)out.pulse=HapticPulse{event->hand,.7f,45000000};
        else if(event->kind==FeedbackKind::PumpGunKnock)out.pulse=HapticPulse{event->hand,.3f,30000000};
        else if(event->kind==FeedbackKind::PumpForward)out.pulse=HapticPulse{event->hand,1.f,80000000};
        else if(event->kind==FeedbackKind::SwitchSelected)out.pulse=HapticPulse{event->hand,.25f,12000000};
        else if(event->kind==FeedbackKind::SwitchRejected){out.pulse=HapticPulse{event->hand,.2f,25000000};
            second_=Delayed{HapticPulse{event->hand,.2f,25000000,event->id,event->kind},now+50000000};}
        else if(event->kind==FeedbackKind::GunFire){
            if(event->recoilWeight<0.f)out.pulse=HapticPulse{event->hand,.85f,50000000};
            else {
                // KF2 WeaponHaptics curve; the native producer supplies its
                // bounded BC2 data weight, and marks an actual bracing hand.
                const float amplitude=.35f+.65f*event->recoilWeight;
                const double duration=.025+.075*double(event->recoilWeight);
                out.pulse=HapticPulse{event->hand,amplitude*(event->recoilSupport?.65f:1.f),
                    std::int64_t(std::llround(duration*(event->recoilSupport?.8:1.)*1000000000.))};
            }
        }
        else if(event->kind==FeedbackKind::DryFire)out.pulse=HapticPulse{event->hand,.35f,15000000};
        else out.pulse=HapticPulse{event->hand,.9f,75000000};
        out.pulse->eventId=event->id;out.pulse->kind=event->kind;
        return out;
    }
private:
    struct Delayed {HapticPulse pulse{};std::int64_t dueNs=0;};
    std::optional<Delayed> second_;
    std::uint64_t seen_=0,space_=0,barrier_=0,lastInput_=0;
    std::int64_t lastNow_=0,lastCapture_=0;
};
}
