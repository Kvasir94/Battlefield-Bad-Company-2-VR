#pragma once
#include "Bc2MagazineNativeProfile.h"
#include "Bc2ManualEmptyFamily.h"
#include "Bc2ReloadRequestCycle.h"
#include <algorithm>
#include <array>
#include <optional>
#include <cstdint>

namespace fvr::bc2 {
// Only the verified Step state-2 reload-entry branch consumes this override.
// Never wrap Update: its shot subscribers also receive the complete context.
inline std::int64_t MagazineEmptyEvidenceDeadline(std::int64_t observed,std::int64_t ownerDeadline)noexcept {
    // Every branch also consumes the original server-owner read (200 ms).
    if(observed<=0||observed>INT64_MAX-200000000||ownerDeadline<=observed)return 0;
    return std::min(ownerDeadline,observed+200000000);
}
struct MagazineEmptyStep {
    ReloadHoldIdentity identity{};
    NativeMagazineProfileId profile=NativeMagazineProfileId::ScopedXm8;
    std::uint64_t ownerRevision=0,sourceSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned branch=3;
    ReloadFiringObservation before{};
};
inline bool ManualEmptyStepBoundaryEligible(const MagazineEmptyStep& s,
    ReloadRequestCyclePhase phase,const ReloadUpdateContext& context,std::int64_t now)noexcept {
    const auto& b=s.before;const auto& o=s.identity.owner;
    const bool inactive=phase==ReloadRequestCyclePhase::Idle||phase==ReloadRequestCyclePhase::Finished||
        phase==ReloadRequestCyclePhase::Cancelled;
    // Arming authorizes the explicit native Reload pulse, not an unrelated
    // empty-magazine entry. Preserve that pulse while suppressing automatic
    // entry during the pending physical interaction's neutral/fire callbacks.
    const bool unsolicitedArming=phase==ReloadRequestCyclePhase::Arming&&
        !context.reloadRequested&&!(context.inputFlags&4u);
    // The byte (+0x28) is also what Step 2 tests before accepting a Reload,
    // so it is written only where native would auto-reload by itself: an
    // empty copy. Live runs 075702/075836 wrote it every Step at 29-30 rounds
    // (16,881 rows) and the physical start pulse intermittently never reached
    // 2 -> 10 on a copy whose context lacked the Reload bit. A stock Reload
    // press with rounds left is already masked in the action layer.
    return (inactive||unsolicitedArming)&&b.loaded==0&&
        o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&
        o.actorGeneration&&o.equipGeneration&&o.space&&s.identity.serverPlayer>=0x10000&&
        s.identity.serverSoldier>=0x10000&&s.identity.serverItem>=0x10000&&
        s.identity.firing[0]>=0x10000&&s.identity.firing[1]>=0x10000&&s.identity.firing[2]>=0x10000&&
        s.identity.firing[0]!=s.identity.firing[1]&&s.identity.firing[0]!=s.identity.firing[2]&&s.identity.firing[1]!=s.identity.firing[2]&&
        s.ownerRevision&&s.sourceSequence&&s.observedNs>0&&s.observedNs<=now&&now<s.deadlineNs&&
        s.deadlineNs==MagazineEmptyEvidenceDeadline(s.observedNs,s.deadlineNs)&&s.branch<3&&b.address==s.identity.firing[s.branch]&&
        b.address>=0x10000&&b.wrapperOffset==(s.branch==0?0x3cu:s.branch==1?0x40u:0x10u)&&
        b.currentState==2&&b.nextState==2&&std::isfinite(b.phaseTimer)&&b.phaseTimer>=0&&
        b.loaded>=0&&b.loaded<=1000000&&b.reserve>=0&&b.reserve<=1000000&&!(b.flagsA8&(8|16))&&
        std::isfinite(context.deltaSeconds)&&context.deltaSeconds>0&&context.deltaSeconds<=.05f&&
        context.reloadTimeMultiplier==1&&!context.orderRequested&&!(context.inputFlags&~5u)&&
        context.fireRequested==bool(context.inputFlags&1)&&context.reloadRequested==bool(context.inputFlags&4)&&
        context.flags24Through28[0]&&!context.flags24Through28[2]&&!context.flags24Through28[4];
}
// Diagnostic only: which ManualEmptyStepBoundaryEligible predicates fail
// (0 = all hold). 1 cycle phase, 2 loaded != 0, 4 owner, 8 server/firing
// identity, 16 evidence window, 32 branch/address/wrapper, 64 state/timer,
// 128 count range or flags_a8, 256 delta, 512 multiplier/order, 1024 input
// bits, 2048 fire/reload decode, 4096 context flag bytes.
inline unsigned ManualEmptyStepFailures(const MagazineEmptyStep& s,
    ReloadRequestCyclePhase phase,const ReloadUpdateContext& context,std::int64_t now)noexcept {
    const auto& b=s.before;const auto& o=s.identity.owner;unsigned f=0;
    const bool inactive=phase==ReloadRequestCyclePhase::Idle||phase==ReloadRequestCyclePhase::Finished||phase==ReloadRequestCyclePhase::Cancelled;
    const bool unsolicitedArming=phase==ReloadRequestCyclePhase::Arming&&!context.reloadRequested&&!(context.inputFlags&4u);
    if(!(inactive||unsolicitedArming))f|=1;if(b.loaded!=0)f|=2;
    if(!(o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&o.actorGeneration&&o.equipGeneration&&o.space))f|=4;
    if(!(s.identity.serverPlayer>=0x10000&&s.identity.serverSoldier>=0x10000&&s.identity.serverItem>=0x10000&&
        s.identity.firing[0]>=0x10000&&s.identity.firing[1]>=0x10000&&s.identity.firing[2]>=0x10000&&
        s.identity.firing[0]!=s.identity.firing[1]&&s.identity.firing[0]!=s.identity.firing[2]&&s.identity.firing[1]!=s.identity.firing[2]))f|=8;
    if(!(s.ownerRevision&&s.sourceSequence&&s.observedNs>0&&s.observedNs<=now&&now<s.deadlineNs&&
        s.deadlineNs==MagazineEmptyEvidenceDeadline(s.observedNs,s.deadlineNs)))f|=16;
    if(!(s.branch<3&&b.address==s.identity.firing[s.branch]&&b.address>=0x10000&&b.wrapperOffset==(s.branch==0?0x3cu:s.branch==1?0x40u:0x10u)))f|=32;
    if(!(b.currentState==2&&b.nextState==2&&std::isfinite(b.phaseTimer)&&b.phaseTimer>=0))f|=64;
    if(!(b.loaded>=0&&b.loaded<=1000000&&b.reserve>=0&&b.reserve<=1000000&&!(b.flagsA8&(8|16))))f|=128;
    if(!(std::isfinite(context.deltaSeconds)&&context.deltaSeconds>0&&context.deltaSeconds<=.05f))f|=256;
    if(!(context.reloadTimeMultiplier==1&&!context.orderRequested))f|=512;
    if(context.inputFlags&~5u)f|=1024;
    if(!(context.fireRequested==bool(context.inputFlags&1)&&context.reloadRequested==bool(context.inputFlags&4)))f|=2048;
    if(!(context.flags24Through28[0]&&!context.flags24Through28[2]&&!context.flags24Through28[4]))f|=4096;
    return f;
}
// Fail-closed empty gate (FVR_BC2_EMPTY_GATE_FAILCLOSED=1, request mode).
// The lease gate above lets the native auto-reload run on any failed check
// (runs 114713, 131709, 134305). KF2 blocks whenever in doubt, so this blocks
// Step-2 entry on any of the player's three copies that is empty at 2/2 unless
// our own start token is pending (the Arming Reload pulse, or the stock
// fallback when the physical path cannot start) or a cycle is running.
// Identity is the last published owner's copies, kept across owner clears
// (a failed state read, focus or head loss, stale input) and replaced only by
// a publication of another owner; the hook re-checks each copy structurally
// (vtable, firing data, ammo address). Support comes from that published
// config, not the request cycle's selection. Families without a physical
// reload stay native.
struct ClosedEmptyStep {
    unsigned firing=0;std::array<unsigned,3> copies{};bool structural=false;
    bool callerMatches=false,supported=false,stateKnown=false;
    unsigned current=0,next=0;std::int32_t loaded=-1;
    ReloadRequestCyclePhase phase=ReloadRequestCyclePhase::Disabled;bool cancelPending=false,reloadBit=false;
    std::int64_t tokenAgeNs=-1; // time since this cycle entered Arming
    bool stockToken=false;       // fail-stuck fallback: a plain native reload granted
};
// The start token is the Arming cycle's own Reload pulse (100 ms) up to the
// native 2 -> 10 commit, which follows within a few frames. A Reload bit later
// in a long Arming wait is not our pulse.
inline constexpr std::int64_t ClosedEmptyTokenMaxAgeNs=250000000;
// 0 = block. 1 not an identified player copy (or wrong caller), 2 family has
// no physical reload, 4 not at 2/2, 8 not empty, 16 our token pending or a
// cycle running.
inline unsigned ClosedEmptyStepPass(const ClosedEmptyStep& s)noexcept {
    unsigned f=0;
    if(!(s.firing>=0x10000&&s.callerMatches&&s.structural&&
         std::find(s.copies.begin(),s.copies.end(),s.firing)!=s.copies.end()))f|=1;
    if(!s.supported)f|=2;
    if(!(s.stateKnown&&s.current==2&&s.next==2))f|=4;
    if(!(s.stateKnown&&s.loaded==0))f|=8;
    const auto p=s.cancelPending?ReloadRequestCyclePhase::Cancelled:s.phase;
    const bool inactive=p==ReloadRequestCyclePhase::Idle||p==ReloadRequestCyclePhase::Finished||p==ReloadRequestCyclePhase::Cancelled;
    const bool token=p==ReloadRequestCyclePhase::Arming&&s.reloadBit&&s.tokenAgeNs>=0&&s.tokenAgeNs<=ClosedEmptyTokenMaxAgeNs;
    if(!(inactive||(p==ReloadRequestCyclePhase::Arming&&!token)))f|=16;
    else if(s.stockToken&&s.reloadBit)f|=16;
    return f;
}
inline bool MagazineEmptyStepEligible(const MagazineEmptyStep& s,const ReloadObservedConfig& config,
    ReloadRequestCyclePhase phase,const ReloadUpdateContext& context,std::int64_t now)noexcept {
    const auto* profile=ResolveMagazineNativeProfile(s.profile);
    return profile&&profile->Matches(config)&&config.fireLogicType==2&&config.reloadType==1&&
        ManualEmptyStepBoundaryEligible(s,phase,context,now);
}
inline bool ManualEmptyFamilyStepEligible(const MagazineEmptyStep& s,ReloadNativeFamily family,
    const ReloadObservedConfig& config,ReloadRequestCyclePhase phase,
    const ReloadUpdateContext& context,std::int64_t now)noexcept {
    if(family==ReloadNativeFamily::Xm8Magazine)return MagazineEmptyStepEligible(s,config,phase,context,now);
    return ManualEmptyFamilyConfig(family,s.profile,config)&&ManualEmptyStepBoundaryEligible(s,phase,context,now);
}
struct MagazineEmptyByteAccess {
    void* context=nullptr;
    bool (*compareExchange)(void*,std::uint8_t expected,std::uint8_t replacement,std::uint8_t& observed)=nullptr;
};
struct MagazineEmptyByteOverride {
    std::uint8_t original=0,beforeRestore=0;
    bool applied=false,restored=false,unexpectedNativeWrite=false;
    bool Apply(const MagazineEmptyByteAccess& a,std::uint8_t expected)noexcept {
        // A native true value is already inhibited and is never normalized.
        if(applied||expected!=0||!a.compareExchange)return false;
        std::uint8_t actual=0;
        if(!a.compareExchange(a.context,expected,1,actual)||actual!=expected)return false;
        original=expected;applied=true;return true;
    }
    bool Restore(const MagazineEmptyByteAccess& a)noexcept {
        if(!applied||restored||!a.compareExchange)return false;
        if(!a.compareExchange(a.context,1,original,beforeRestore))return false;
        unexpectedNativeWrite=beforeRestore!=1;
        restored=!unexpectedNativeWrite;return restored;
    }
};
inline void RunMagazineEmptyByteOverride(const MagazineEmptyByteAccess& access,std::uint8_t expected,bool request,
    void (*original)(void*),void* context,MagazineEmptyByteOverride& receipt) {
    if(request)receipt.Apply(access,expected);
#if defined(_MSC_VER)
    __try {original(context);}
    __finally {if(receipt.applied)receipt.Restore(access);}
#else
    struct Cleanup {const MagazineEmptyByteAccess& a;MagazineEmptyByteOverride& r;~Cleanup(){if(r.applied)r.Restore(a);}} cleanup{access,receipt};
    original(context);
#endif
}
// Applied/restored native Step receipts only. This never grants a reload hold,
// ammunition transfer, replacement object, or successful cancellation.
class MagazineEmptyControlReceipts {
public:
    void Clear()noexcept{rows_={};}
    bool Observe(const MagazineEmptyStep& s,const ReloadFiringObservation& after,
        const MagazineEmptyByteOverride& patch,bool ownerRetained,std::int64_t now)noexcept {
        if(s.branch>=3)return false;
        rows_[s.branch].reset();
        if(!ownerRetained||!patch.applied||!patch.restored||patch.unexpectedNativeWrite||
            now<s.observedNs||now>=s.deadlineNs||s.deadlineNs!=MagazineEmptyEvidenceDeadline(s.observedNs,s.deadlineNs)||after.address!=s.before.address||
            after.wrapperOffset!=s.before.wrapperOffset||after.currentState!=2||after.nextState!=2||
            after.loaded!=s.before.loaded||after.reserve!=s.before.reserve||after.flagsA8!=s.before.flagsA8)return false;
        rows_[s.branch]=s;return true;
    }
    std::optional<std::int64_t> EmptyDeadline(const ReloadHoldIdentity& id,NativeMagazineProfileId profile,
        std::uint64_t revision,int reserve,std::int64_t now)const noexcept {
        std::int64_t deadline=INT64_MAX;
        for(unsigned n=0;n<3;++n){const auto& s=rows_[n];
            if(!s||s->identity!=id||s->profile!=profile||s->ownerRevision!=revision||
                s->branch!=n||s->before.loaded!=0||s->before.reserve!=reserve||
                now<s->observedNs||now>=s->deadlineNs)return {};
            deadline=std::min(deadline,s->deadlineNs);
        }
        return deadline;
    }
private:
    std::array<std::optional<MagazineEmptyStep>,3> rows_{};
};
} // namespace fvr::bc2
