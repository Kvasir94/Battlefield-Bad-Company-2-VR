#include "Bc2Xm8BoltCycle.h"
#include <cmath>
#include <limits>

namespace fvr::bc2 {
using namespace interaction;
WeaponCycleProfile Xm8BoltCycleProfile(const Xm8BoltAsset& asset)noexcept {
    WeaponCycleProfile p;p.id=0x584d38ull;p.revision=1;p.family=WeaponCycleFamily::Pump;
    // Asset lane contract (reports/xm8-bolt-intake/HANDOFF.md). The tolerances
    // are proposed UX defaults, not engine data; tune them in the headset.
    p.axis=asset.axis;p.stroke=asset.stroke;
    for(unsigned n=0;n<4;++n)p.closedContact.values[n][n]=1;
    for(unsigned k=0;k<3;++k)p.closedContact.values[3][k]=asset.handleRest[k];
    p.rearTolerance=.010f;p.frontTolerance=.008f;p.contactRadius=.06f;p.lateralTolerance=.03f;
    p.unlockRadians=0;p.rotationTolerance=.5f;p.maxStepMeters=.07f;
    p.endpointDwellNs=20000000;p.maximumCycleNs=10000000000ll;
    return p;
}
std::optional<math::Matrix4> Xm8BoltWeaponFromHand(const math::Matrix4& wrist,const math::Matrix4& weapon,const math::Matrix4* palmFromWrist)noexcept {
    const auto inverse=InverseRigid(weapon);if(!inverse||!InverseRigid(wrist))return {};
    const auto weaponFromWrist=Multiply(wrist,*inverse);
    if(!palmFromWrist)return weaponFromWrist;
    if(!InverseRigid(*palmFromWrist))return {};
    return Multiply(*palmFromWrist,weaponFromWrist);
}
WeaponCycleLease Bc2Xm8BoltCycle::Lease(const HandInteractionSample& s,HandInteractionKey weapon)const noexcept {
    // VR presentation lease: identifies this stroke only. It is NOT a native
    // held-cycle receipt and grants nothing outside the adapter.
    return {s.owner,weapon,Xm8BoltContactKey,cycleId_,cycleId_,s.sequence,s.observedNs,s.deadlineNs,true};
}
void Bc2Xm8BoltCycle::Retire(const HandInteractionSample& current,HandInteraction& hands,bool cancelled)noexcept {
    if(claim_){const auto left=hands.Current(InteractionHand::Left);
        if(left&&left->token==claim_->token)hands.Release(current,claim_->token);}
    claim_.reset();
    const auto phase=cycle_.Phase();
    if(phase!=WeaponCyclePhase::Idle&&phase!=WeaponCyclePhase::Complete&&phase!=WeaponCyclePhase::Cancelled)
        cycle_.Cancel(cancelled?WeaponCycleFailure::Released:WeaponCycleFailure::None);
    active_=false;lastTravel_=0;
}
void Bc2Xm8BoltCycle::Cancel(const HandInteractionSample& current,HandInteraction& hands)noexcept {
    if(active_&&claim_)++counters_.cancels;Retire(current,hands,true);rearmed_=false;
}
Xm8BoltCycleResult Bc2Xm8BoltCycle::Tick(const Xm8BoltCycleSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    using namespace weapon_cycle_detail;
    Xm8BoltCycleResult out;++counters_.ticks;
    if(!Valid())return out;
    if(!s.gripPressed)rearmed_=true;
    // The arbiter dropped our claim (release, expiry, identity reset or a
    // transfer): the stroke ends with it.
    if(claim_){const auto left=hands.Current(InteractionHand::Left);
        if(!left||left->token!=claim_->token){claim_.reset();
            if(active_){++counters_.claimLost;++counters_.cancels;out.cancelled=true;Retire(s.input,hands,true);lastFailure_=WeaponCycleFailure::Released;}}}
    if(!s.xm8||!s.weaponFromHand||s.cancel||s.magazineOwnsLeft){
        if(active_){const bool held=bool(claim_);if(s.magazineOwnsLeft)++counters_.yielded;if(held){++counters_.cancels;out.cancelled=true;}
            Retire(s.input,hands,held);if(held)rearmed_=false;}
        out.phase=cycle_.Phase();return out;
    }
    const auto gun=hands.Current(InteractionHand::Right);
    const bool gunHeld=gun&&gun->token.kind==HandClaimKind::GunHold&&gun->token.item==s.weapon&&gun->token.owner==s.input.owner;
    const float distance=Distance(*s.weaponFromHand,profile_.closedContact);
    if(!active_){
        // Hover: a neutral (unsqueezed) hand inside the contact radius opens an
        // AwaitingGrip cycle so WeaponCycle sees the real neutral sample. This
        // takes no claim and blocks nothing.
        const auto left=hands.Current(InteractionHand::Left);
        if(!gunHeld||s.gripPressed||!rearmed_||left||distance>profile_.contactRadius||cycleId_==std::numeric_limits<std::uint64_t>::max())return out;
        ++cycleId_;
        if(!cycle_.Begin(profile_,Lease(s.input,s.weapon),s.input.nowNs))return out;
        active_=true;++counters_.hovers;
    }
    WeaponCycleInput in{s.input,*s.weaponFromHand,s.gripPressed,bool(claim_)};
    if(cycle_.Phase()==WeaponCyclePhase::AwaitingGrip){
        if(distance>profile_.contactRadius*1.5f){const bool held=bool(claim_);if(held){++counters_.cancels;out.cancelled=true;rearmed_=false;}
            Retire(s.input,hands,held);out.phase=cycle_.Phase();return out;}
        if(s.gripPressed&&!claim_){
            // One arbiter attempt per fresh press, on the raw contact's own
            // packet. A refused claim needs a release before another try.
            const auto left=hands.Current(InteractionHand::Left);
            bool accepted=false;
            if(gunHeld&&!left&&s.evidence&&intent!=std::numeric_limits<std::uint64_t>::max()&&distance<=profile_.contactRadius){
                const HandClaimRequest request{s.input.owner,InteractionHand::Left,HandClaimKind::Mechanism,s.weapon,
                    {Xm8BoltContactKey,s.evidence->sequence,s.evidence->deadlineNs,true},++intent,gun->token.id};
                const auto r=hands.AcquireFrom(s.input,*s.evidence,request);
                if(r.accepted&&r.claim){claim_=r.claim;accepted=true;}
            }
            if(!accepted){++counters_.claimRejected;rearmed_=false;Retire(s.input,hands,false);out.phase=cycle_.Phase();return out;}
            in.mechanismClaim=true;
        }
    }else if(claim_&&s.evidence){
        // Renew on the raw contact's packet; stale geometry lets the claim lapse.
        const HandContactProof proof{Xm8BoltContactKey,s.evidence->sequence,s.evidence->deadlineNs,true};
        if(const auto r=hands.RenewFrom(s.input,*s.evidence,claim_->token,proof);r.accepted&&r.claim)claim_=r.claim;
    }
    const auto before=cycle_.Phase();
    const auto result=cycle_.Update(in,Lease(s.input,s.weapon));
    lastFailure_=result.failure;out.phase=result.phase;out.failure=result.failure;
    if(result.phase==WeaponCyclePhase::AwaitingNative){
        // Ordered rear -> forward stroke finished. No native completion exists:
        // retire as presentation-complete and wait for a release.
        ++counters_.completions;out.presentationComplete=true;Retire(s.input,hands,false);rearmed_=false;return out;
    }
    if(result.phase==WeaponCyclePhase::Cancelled){
        const bool held=bool(claim_);if(held){++counters_.cancels;out.cancelled=true;}
        Retire(s.input,hands,held);rearmed_=!s.gripPressed;return out;
    }
    const bool holding=bool(claim_);
    out.grabbed=before==WeaponCyclePhase::AwaitingGrip&&result.phase==WeaponCyclePhase::Rear;if(out.grabbed)++counters_.grabs;
    out.travel=holding&&(result.phase==WeaponCyclePhase::Rear||result.phase==WeaponCyclePhase::Forward)?result.travel:0.f;
    out.blocksFire=holding;out.ownsLeft=holding;lastTravel_=out.travel;
    if(holding)++counters_.fireBlockedTicks;
    return out;
}
void Bc2Xm8BoltCycle::Report(std::ostream& o)const {
    const auto& c=counters_;
    o<<"{\"valid\":"<<(Valid()?"true":"false")<<",\"phase\":"<<unsigned(cycle_.Phase())<<",\"held\":"<<(claim_?"true":"false")
     <<",\"travel\":"<<lastTravel_<<",\"last_failure\":"<<unsigned(lastFailure_)<<",\"ticks\":"<<c.ticks<<",\"hovers\":"<<c.hovers<<",\"grabs\":"<<c.grabs
     <<",\"claim_rejected\":"<<c.claimRejected<<",\"presentation_completions\":"<<c.completions<<",\"cancels\":"<<c.cancels
     <<",\"claim_lost\":"<<c.claimLost<<",\"yielded_to_magazine\":"<<c.yielded<<",\"fire_blocked_ticks\":"<<c.fireBlockedTicks
     <<",\"native_completion\":false}";
}
} // namespace fvr::bc2
