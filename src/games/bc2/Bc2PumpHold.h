#pragma once
#include "fvr/interaction/MotionGuard.h"
// SPAS physical pump (docs/BC2_RELOAD_PARITY.md S3). Two portable pieces:
//
// PumpHoldGate decides, per firing copy and Update, whether the native
// bolt/pump hold (state 7, next 8, after a shot from 6) is kept waiting. The
// adapter applies the hold with the existing exact delta transaction (frame
// delta 0 for one original Update, restored after), the same write the reload
// hold uses, so nothing native is written that is not restored. Releasing it
// lets the native BoltActionDelay, the state-8 pump start (+0x30, animation and
// sound) and 8 -> 1 -> 2 run as the game would. Static RE: docs/re
// BC2_DISASM_PUMP_AND_DRY_FIRE.md (SPAS: HoldUntilFire/ZoomRelease false,
// BoltActionDelay 0.5 s, so natively the SPAS never waits at 7).
//
// PumpStroke recognizes a KF2 VR style manual pump from the support hand's
// position along the barrel: a rear stroke to the rear stop (KF2 RearStop 95%)
// and a return to the forward lock (5%). It never touches native state; the
// adapter calls PumpHoldGate::Rack when it completes.
#include "Bc2ReloadHold.h"
#include "Bc2EquipmentIdentity.h"
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/PhysicalWeaponReadiness.h"
#include "fvr/math/StereoMath.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
namespace fvr::bc2 {
struct PumpHoldStats {
    std::uint64_t owed=0,held=0,racked=0,advances=0,timeouts=0,reloadReleases=0,stateReleases=0,identityResets=0,contention=0,ownerResets=0,early=0,orderWaits=0;
    // After a rack: a copy whose own Update first saw it already out of 7 (moved
    // by something other than its advance), per branch.
    std::array<std::uint64_t,3> leftSevenUnadvanced{};
};
class PumpHoldGate {
public:
    // Hold from the first Update at state 7 (live run 082048: waiting for the
    // last 60 ms of BoltActionDelay missed every coherent read). On the rack,
    // each copy's next Update runs once with its frame delta raised by its
    // remaining delay (the seat fast-forward transaction), so the pump
    // animation still starts right after the rack.
    struct Decision {bool hold=false;float advance=0;};
    // Safety: a pump that is never racked is released (native pumps itself).
    static constexpr std::int64_t TimeoutNs=8000000000ll,OrderWaitNs=50000000;
    void Enable(bool on)noexcept{enabled_.store(on,std::memory_order_release);}
    bool Enabled()const noexcept{return enabled_.load(std::memory_order_acquire);}
    // in.branch is the copy this Update belongs to; in.verified and the three
    // branch reads are the PrepareHold cohort. spas = exact SPAS config proof.
    Decision Evaluate(const ReloadHoldInput& in,bool spas,std::int64_t now)noexcept {
        if(!Enabled()||!in.verified||in.branch>=3)return {};
        Lock lock(gate_);if(!lock.held){++stats_.contention;return {};}
        if(!spas||in.identity!=identity_){if(owed_)++stats_.identityResets;owed_=false;advance_=false;cycling_=false;identity_=in.identity;}
        if(!spas)return {};
        const auto& b=in.branches[in.branch];
        // The server copy in its shot states (5/6) with a shell left will owe a
        // pump at 7. KF2 lets the stroke start right after the shot; natively
        // 6 lasts ~0.7 s (live run 091024: shot at tick 104, 7 at tick 239), so
        // a stroke made then is banked by Rack() for this shot.
        if(in.branch==2){const bool shot=(b.currentState==5||b.currentState==6)&&b.loaded>0&&rackedShot_!=ShotKey(in);
            if(shot&&!cycling_){cycling_=true;cyclingShot_=ShotKey(in);}
            else if(cycling_&&!shot&&b.currentState!=7)cycling_=false;}
        const bool atSeven=b.currentState==7&&b.nextState==8;
        // Racked: skip what is left of this copy's delay, once.
        if(!owed_&&advance_&&ShotKey(in)==rackedShot_&&atSeven&&!advanced_[in.branch]&&std::isfinite(b.phaseTimer)&&b.phaseTimer>0&&b.phaseTimer<=1.f){
            // client0 (+0x3c, the branch with the firing-effects listener) goes
            // first: advanced after client1 it was moved 7 -> 2 without its own
            // state 8 (runs 092813/093006), so its cycle notification never ran.
            // The others wait a bounded moment for it.
            if(!rackNs_)rackNs_=now;
            if(in.branch!=0&&!advanced_[0]&&now-rackNs_<OrderWaitNs){++stats_.orderWaits;++stats_.held;return {true,0};}
            advanced_[in.branch]=true;++stats_.advances;return {false,b.phaseTimer+.001f};}
        if(!owed_&&advance_&&ShotKey(in)==rackedShot_&&!advanced_[in.branch]&&b.currentState!=7&&b.previousState==7){
            advanced_[in.branch]=true;++stats_.leftSevenUnadvanced[in.branch];}
        // A new shot owes a pump: the copy just committed 6 -> 7 with ammo left.
        if(!owed_&&atSeven&&b.previousState==6&&b.loaded>0&&rackedShot_!=ShotKey(in)){owed_=true;cycling_=false;owedNs_=now;shot_=ShotKey(in);seenSeven_={};++stats_.owed;}
        if(!owed_)return {};
        if(in.context.reloadRequested){owed_=false;rackedShot_=shot_;++stats_.reloadReleases;return {};}
        if(now-owedNs_>TimeoutNs){owed_=false;rackedShot_=shot_;++stats_.timeouts;return {};}
        // Any copy that left 7/8 (sprint interrupt, switch, restore) is not held;
        // the server copy leaving it ends the owed pump.
        // A release settles this shot for every copy: copies still at 7 (prev 6)
        // after the server moved on must not re-owe it (live run 080623: owed 2).
        // Only a server copy that was already seen at 7 can release it: the
        // copies reach 7 a few ms apart, and the server still at 6 when a client
        // owed the pump is arriving, not leaving (live run 083755).
        if(!atSeven){if(in.branch==2&&seenSeven_[2]&&b.currentState!=8){owed_=false;rackedShot_=shot_;++stats_.stateReleases;}return {};}
        seenSeven_[in.branch]=true;
        if(!std::isfinite(b.phaseTimer))return {};
        ++stats_.held;return {true,0};
    }
    // The physical pump finished: release every copy on its next Update. A
    // stroke finished before the copies reach 7 settles that shot early: no
    // copy owes it, and each skips its remaining delay once it arrives at 7.
    bool Rack()noexcept {
        Lock lock(gate_);if(!lock.held)return false;
        if(owed_){owed_=false;rackedShot_=shot_;advance_=true;advanced_={};rackNs_=0;++stats_.racked;return true;}
        if(!cycling_)return false;
        cycling_=false;rackedShot_=cyclingShot_;advance_=true;advanced_={};rackNs_=0;++stats_.racked;++stats_.early;return true;
    }
    bool Owed()const noexcept{Lock lock(gate_);return lock.held&&owed_;}
    bool Advancing()const noexcept{Lock lock(gate_);return lock.held&&advance_&&!(advanced_[0]&&advanced_[1]&&advanced_[2]);}
    // Owed, or about to be (the shot's recovery): the adapter tracks the stroke.
    bool Expected()const noexcept{Lock lock(gate_);return lock.held&&(owed_||cycling_);}
    // The native owner was cleared (input lost, tape ended, weapon change):
    // nothing is held any more, so forget the owed pump.
    void OwnerCleared()noexcept{Lock lock(gate_);if(!lock.held)return;if(owed_){owed_=false;rackedShot_=shot_;++stats_.ownerResets;}advance_=false;cycling_=false;}
    PumpHoldStats Stats()const noexcept{Lock lock(gate_);return stats_;}
private:
    struct Lock {std::atomic_flag& f;bool held=false;
        explicit Lock(std::atomic_flag& x)noexcept:f(x){for(unsigned n=0;n<64&&!held;++n)held=!f.test_and_set(std::memory_order_acquire);}
        ~Lock(){if(held)f.clear(std::memory_order_release);}};
    // Loaded count after the shot identifies it (one pump per shot).
    static std::int64_t ShotKey(const ReloadHoldInput& in)noexcept{
        return (std::int64_t(in.branches[2].loaded)<<32)|std::uint32_t(in.branches[2].reserve);}
    std::atomic<bool> enabled_{false};
    mutable std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    ReloadHoldIdentity identity_{};bool owed_=false,advance_=false,cycling_=false;std::array<bool,3> advanced_{},seenSeven_{};std::int64_t owedNs_=0,rackNs_=0,shot_=-1,rackedShot_=-1,cyclingShot_=-1;
    PumpHoldStats stats_{};
};

// The support hand's position along the gun hand's aim axis, positive toward
// the shooter (OpenXR aim +Z is backward), in tracking metres. Independent of
// the weapon model's local axis sign and of where the hand sits on the gun:
// PumpStroke measures travel from where the stroke started.
inline float PumpAlong(const math::Vec3& support,const math::Pose& gunAim)noexcept{
    const auto& q=gunAim.orientation;
    const float bx=2*(q.x*q.z+q.w*q.y),by=2*(q.y*q.z-q.w*q.x),bz=1-2*(q.x*q.x+q.y*q.y);
    return (support.x-gunAim.position.x)*bx+(support.y-gunAim.position.y)*by+(support.z-gunAim.position.z)*bz;
}
// Independent raw controller position in the same gun frame as axial travel.
// Full 3D continuity catches sideways jumps that a bore projection cannot see.
inline std::optional<math::Vec3> PumpPosition(const math::Vec3& support,const math::Pose& gunAim)noexcept {
    math::Pose hand{};hand.position=support;
    const auto local=math::MakeRelativePose(gunAim,hand);
    return local?std::optional{local->position}:std::nullopt;
}
// The support hand's distance from the gun hand's aim axis, tracking metres.
inline float PumpLateral(const math::Vec3& support,const math::Pose& gunAim)noexcept{
    const float along=PumpAlong(support,gunAim);const auto& q=gunAim.orientation;
    const float bx=2*(q.x*q.z+q.w*q.y),by=2*(q.y*q.z-q.w*q.x),bz=1-2*(q.x*q.x+q.y*q.y);
    const float dx=support.x-gunAim.position.x-along*bx,dy=support.y-gunAim.position.y-along*by,dz=support.z-gunAim.position.z-along*bz;
    return std::sqrt(dx*dx+dy*dy+dz*dz);
}
// KF2 VRManualPump zone: the stroke engages only when the grip closes on the
// fore-end, forward of the gun hand and near the barrel line, not anywhere
// (a grip on the pouch or the sling used to rack). Once engaged it follows
// the hand. Generous until headset tuning: 5-85 cm forward, within 20 cm.
struct PumpZoneConfig {float nearMeters=.05f,farMeters=.85f,lateralMeters=.20f;};
inline bool PumpInZone(const math::Vec3& support,const math::Pose& gunAim,PumpZoneConfig c={})noexcept{
    const float forward=-PumpAlong(support,gunAim),lateral=PumpLateral(support,gunAim);
    return std::isfinite(forward)&&std::isfinite(lateral)&&forward>=c.nearMeters&&forward<=c.farMeters&&lateral<=c.lateralMeters;
}
// KF2 VR VRManualPump: rear stop at 95% of the stroke, forward lock at 5%.
// Positions are the support hand's travel along the barrel in metres, positive
// toward the shooter (rear), relative to where the stroke started.
struct PumpStrokeConfig {float strokeMeters=.06f,rearStop=.95f,forwardLock=.05f,maxStepMeters=.12f;std::int64_t maxGapNs=150000000;float maxSpeedMetersPerSecond=10.f,travelScale=1.f;};
enum class PumpStrokePhase:std::uint8_t {Idle,Rearward,Forward};
class PumpStroke {
public:
    explicit PumpStroke(PumpStrokeConfig c={})noexcept:c_(c){}
    // grip: support hand squeezing on the fore-end. Returns true on the sample
    // that completes rear-then-forward.
    struct Result {bool rearStop=false,completed=false;float travel=0;PumpStrokePhase phase=PumpStrokePhase::Idle;bool returning=false;};
    Result Update(bool grip,const math::Vec3& position,std::int64_t now)noexcept {
        if(!grip){Reset();return {};}
        const interaction::MotionGuard::Position raw{position.x,position.y,position.z};
        const bool accepted=motion_.Ready()?
            motion_.Check(raw,now)==interaction::MotionGuardVerdict::Accepted:
            motion_.Begin(interaction::MotionGuardConfig::Rack(),raw,now);
        if(!accepted){Reset();return {};}
        return Update(true,position.z,now);
    }
    Result Update(bool grip,float along,std::int64_t now)noexcept {
        Result r;
        // Several native gathers may consume one tracked packet. A duplicate
        // preserves the held pose without repeating either stop event.
        if(grip&&last_&&now==last_&&along==lastAlong_)return {false,false,lastTravel_,phase_,returning_};
        const float allowedStep=last_&&now>last_?std::max(c_.maxStepMeters,
            c_.maxSpeedMetersPerSecond*float(double(now-last_)*1e-9)):c_.maxStepMeters;
        if(!grip||!std::isfinite(along)||now<=0||(last_&&(now<=last_||now-last_>c_.maxGapNs||std::fabs(along-lastAlong_)>allowedStep))){
            Reset();if(grip&&std::isfinite(along)&&now>0){origin_=along;last_=now;lastAlong_=along;phase_=PumpStrokePhase::Rearward;}
            r.phase=phase_;return r;}
        if(phase_==PumpStrokePhase::Idle){origin_=along;phase_=PumpStrokePhase::Rearward;}
        // Like KF2, moving forward before pulling finds the forward stop;
        // it must not add that approach distance to the required rear stroke.
        origin_=std::min(origin_,along);
        last_=now;lastAlong_=along;
        const float travel=std::clamp((along-origin_)*c_.travelScale/c_.strokeMeters,0.f,1.f);r.travel=lastTravel_=travel;
        if(phase_==PumpStrokePhase::Rearward&&travel>=c_.rearStop){phase_=PumpStrokePhase::Forward;r.rearStop=true;rearPeak_=travel;}
        else if(phase_==PumpStrokePhase::Forward){
            rearPeak_=std::max(rearPeak_,travel);
            // Reaching the rear latch does not mean the hand is returning.
            // Keep the outbound native wrist/fingers at the rear until actual
            // forward movement, with a small deadband for controller noise.
            if(travel<rearPeak_-.02f)returning_=true;
            if(travel<=c_.forwardLock){r.completed=true;phase_=PumpStrokePhase::Rearward;origin_=along;}}
        r.phase=phase_;r.returning=returning_||r.completed;
        if(r.completed){returning_=false;rearPeak_=0;}
        return r;
    }
    void Reset()noexcept{phase_=PumpStrokePhase::Idle;last_=0;origin_=lastAlong_=lastTravel_=rearPeak_=0;returning_=false;motion_.Reset();}
    PumpStrokePhase Phase()const noexcept{return phase_;}
    bool Returning()const noexcept{return returning_;}
    float PoseFraction(float travel)const noexcept {
        return std::clamp((travel-c_.forwardLock)/(c_.rearStop-c_.forwardLock),0.f,1.f);
    }
private:
    PumpStrokeConfig c_;PumpStrokePhase phase_=PumpStrokePhase::Idle;std::int64_t last_=0;float origin_=0,lastAlong_=0,lastTravel_=0;
    float rearPeak_=0;bool returning_=false;
    interaction::MotionGuard motion_;
};

// Presentation is temporary; the physical weapon ledger owns whether a pump
// is owed. Changing equipment or tracking space never completes a stroke.
inline bool PhysicalPumpExpected(const interaction::PhysicalWeaponSnapshot& state,bool suspended=false)noexcept {
    return !suspended&&interaction::PhysicalPumpActionOwed(state);
}
struct PumpPresentationOwner {
    interaction::PhysicalWeaponIdentity weapon{};
    interaction::HandInteractionOwner hand{};
    WeaponEquipmentIdentity equipment{};
    bool operator==(const PumpPresentationOwner&)const=default;
};
class PumpPresentationState {
public:
    explicit PumpPresentationState(PumpStrokeConfig config={})noexcept:stroke(config){}
    bool Observe(const PumpPresentationOwner& owner)noexcept {
        if(owner_&&*owner_==owner)return false;
        Reset();owner_=owner;return true;
    }
    void Reset()noexcept {
        stroke.Reset();travel=0;zoneHeld=false;handLatched=false;settleUntilNs=0;owner_.reset();
    }
    bool Matches(const PumpPresentationOwner& owner)const noexcept{return owner_&&*owner_==owner;}
    PumpStroke stroke;float travel=0;bool zoneHeld=false,handLatched=false;std::int64_t settleUntilNs=0;
private:
    std::optional<PumpPresentationOwner> owner_;
};
// A swap resets only presentation. Keep every submitted command receipt until
// consumed, and let an old weapon's pending command block only that weapon.
class PumpCommandQueue {
public:
    struct Entry {std::uint64_t id=0;PumpPresentationOwner owner{};};
    static constexpr std::size_t Capacity=256;
    bool PendingFor(const interaction::PhysicalWeaponIdentity& weapon)const noexcept {
        for(const auto& entry:entries_)if(entry.id&&entry.owner.weapon==weapon)return true;
        return false;
    }
    bool CanSubmit(const interaction::PhysicalWeaponIdentity& weapon)const noexcept {
        if(PendingFor(weapon))return false;
        for(const auto& entry:entries_)if(!entry.id)return true;
        return false;
    }
    bool Remember(std::uint64_t id,const PumpPresentationOwner& owner)noexcept {
        if(!id||!CanSubmit(owner.weapon))return false;
        for(auto& entry:entries_)if(!entry.id){entry={id,owner};return true;}
        return false;
    }
    void Consumed(std::uint64_t id)noexcept {
        for(auto& entry:entries_)if(entry.id==id&&id){entry={};return;}
    }
    const auto& Entries()const noexcept{return entries_;}
private:
    std::array<Entry,Capacity> entries_{};
};
}
