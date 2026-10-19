#include "Bc2PreholdCleanup.h"
#include "Bc2PumpHold.h"
#include "Bc2PreholdOrder.h"
#include "Bc2PreholdOperationJournal.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2TestRangeHandoff.h"
#include "Bc2PhysicalWeaponRuntime.h"
#include "Bc2ReloadDeferredCompletion.h"
#include "Bc2DiagnosticFireReserve.h"
#include "Bc2OwnerPublication.h"
#include "Bc2ReloadAbort.h"
#include "Bc2MagazineEmptyControl.h"
#include "Bc2ReloadCommandBoard.h"
#include "Bc2ManualEmptyFamily.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadInvocationEntry.h"
#include "Bc2ReloadPhaseRetry.h"
#include "Bc2ReloadPolicyLock.h"
#include "Bc2PreholdObservationCommit.h"
#include "Bc2PreholdStartPreread.h"
#include "Bc2ReserveFinalValidation.h"
#include "Bc2ReserveReadEvidence.h"
#include "Bc2ReloadLockBodyTelemetry.h"
#include "Bc2ReloadPreentryDeferral.h"
#include "Bc2ReloadCohortRetry.h"
#include "Bc2ReloadServer.h"
#include "Bc2ReloadRoundGate.h"
#include "Bc2GunEventLog.h"
#include <algorithm>
#include <cmath>
#include <bit>
#include <cstring>
#include <limits>
namespace fvr::bc2 {
namespace {
template<class T>T Word(const std::byte* p,std::size_t at){T out{};std::memcpy(&out,p+at,sizeof(out));return out;}
struct Reader {
    const ReloadStateMemory& m;
    bool Read(std::uint64_t at,void* out,std::size_t size)const noexcept{
        return m.read&&at>=0x10000&&size&&size<=4096&&at+size<=UINT32_MAX&&m.read(m.context,unsigned(at),out,size);
    }
    template<class T>bool Get(std::uint64_t at,T& value)const noexcept{return Read(at,&value,sizeof(value));}
    bool Equals(std::uint64_t at,unsigned wanted)const noexcept{unsigned value=0;return Get(at,value)&&value==wanted;}
};
struct ScopeBytes {
    std::uint8_t playerFlags=0,soldierFlags=0;unsigned inventory=0,begin=0,end=0,slot=0;
    std::array<unsigned,64> items{};std::array<unsigned,2> branches{};
    bool operator==(const ScopeBytes&)const=default;
};
bool Scope(const Reader& r,const ReloadStateSnapshot& s,ScopeBytes& b)noexcept{
    const auto& o=s.owner;
    if(!r.Get(std::uint64_t(o.player)+0xccd,b.playerFlags)||!(b.playerFlags&8)||
       !r.Equals(std::uint64_t(o.player)+0xc54,o.weak)||!r.Equals(o.weak,o.soldier+4)||
       !r.Equals(std::uint64_t(o.soldier)+0x220,o.player)||!r.Equals(std::uint64_t(o.player)+0xc68,o.soldier)||
       !r.Get(std::uint64_t(o.soldier)+0x114,b.soldierFlags)||
       !r.Get(std::uint64_t(o.soldier)+((b.soldierFlags&1)?0x24c:0x248),b.inventory)||b.inventory!=s.inventory||
       !r.Get(std::uint64_t(o.soldier)+0x260,b.begin)||!r.Get(std::uint64_t(o.soldier)+0x264,b.end)||
       b.end<=b.begin||b.end-b.begin>256||(b.end-b.begin)%4||
       !r.Get(std::uint64_t(b.inventory)+0x14c,b.slot)||b.slot!=s.selectedSlot||b.slot>=(b.end-b.begin)/4||
       !r.Read(b.begin,b.items.data(),b.end-b.begin)||b.items[b.slot]!=o.weapon||
       std::count(b.items.begin(),b.items.end(),o.weapon)!=1||
       !r.Get(std::uint64_t(o.weapon)+0x3c,b.branches[0])||!r.Get(std::uint64_t(o.weapon)+0x40,b.branches[1])||
       b.branches[0]!=s.branches[0].address||b.branches[1]!=s.branches[1].address||
       !r.Equals(std::uint64_t(o.weapon)+4,s.config.weaponData)||
       !r.Equals(std::uint64_t(s.config.weaponData)+0x98,s.config.firingData)||
       !r.Equals(std::uint64_t(s.config.firingData)+0x40,s.config.primaryFire)||
       !r.Equals(std::uint64_t(s.config.primaryFire)+0x24,unsigned(s.config.fireLogicType))||
       !r.Equals(std::uint64_t(s.config.primaryFire)+0x20,unsigned(s.config.reloadType)))return false;
    return true;
}
bool OwnerValid(const ReloadStateOwner& o)noexcept{return o.player>=0x10000&&o.soldier>=0x10000&&o.soldier<=UINT32_MAX-4&&
    o.weak>=0x10000&&o.weapon>=0x10000&&o.actorGeneration&&o.equipGeneration&&o.space;}
bool SameIdentity(const ReloadFlowBoundary& a,const ReloadFlowBoundary& b)noexcept{
    return a.owner==b.owner&&a.firing==b.firing&&a.branch==b.branch&&a.wrapperOffset==b.wrapperOffset&&a.serverPlayer==b.serverPlayer&&a.serverSoldier==b.serverSoldier&&a.serverItem==b.serverItem;
}
}
bool ReloadReserveCopiesAgree(const ReloadHoldIdentity& identity,const std::array<ReloadFlowBoundary,3>& first,
    const std::array<ReloadFlowBoundary,3>& second,const std::array<int,3>& capacities,const std::array<int,3>& again)noexcept {
    if(!OwnerValid(identity.owner)||!identity.serverPlayer||!identity.serverSoldier||!identity.serverItem)return false;
    for(unsigned n=0;n<3;++n){const auto& a=first[n];const auto& b=second[n];
        if(a.owner!=identity.owner||a.firing!=identity.firing[n]||a.branch!=n||a.wrapperOffset!=(n==0?0x3cu:n==1?0x40u:0x10u)||
           (n==2&&(a.serverPlayer!=identity.serverPlayer||a.serverSoldier!=identity.serverSoldier||a.serverItem!=identity.serverItem))||
           !SameIdentity(a,b)||capacities[n]<=0||capacities[n]>1000000||capacities[n]!=again[n]||
           a.current>15||a.previous>15||a.next>15||!std::isfinite(a.timer)||a.loaded<0||a.loaded>capacities[n]||a.reserve<0||a.reserve>1000000||(a.flagsA8&(8|16))||
           a.current!=b.current||a.previous!=b.previous||a.next!=b.next||a.timer!=b.timer||a.loaded!=b.loaded||a.reserve!=b.reserve||a.flagsA8!=b.flagsA8||
           a.soldierFlags!=b.soldierFlags||a.snapshotSequence!=b.snapshotSequence||
           a.loaded!=first[0].loaded||a.reserve!=first[0].reserve||capacities[n]!=capacities[0])return false;
    }return true;
}
constexpr float IdleFrameTimer=.05f;
bool ReloadReserveIdle(const ReloadHoldIdentity& identity,const std::array<ReloadFlowBoundary,3>& first,
    const std::array<ReloadFlowBoundary,3>& second,const std::array<int,3>& capacities,const std::array<int,3>& again)noexcept {
    if(!ReloadReserveCopiesAgree(identity,first,second,capacities,again))return false;
    // Native Step writes timer=dt on entry to every state (static RE, pump and
    // dry-fire report section 0), so an idle state-2 copy read between its
    // Step and Tick, or after a short Update, holds one frame's delta. Live
    // XM8 runs saw 2/2 with timer ~0.007 s and readiness flickered off. A
    // state-2 timer of at most one frame (50 ms) is idle; longer is not.
    for(unsigned n=0;n<3;++n)if(first[n].current!=2||first[n].next!=2||!(first[n].timer>=0&&first[n].timer<=IdleFrameTimer))return false;
    return true;
}
bool ReloadReserveInputReady(const ReloadHoldIdentity& identity,const std::array<ReloadFlowBoundary,3>& first,
    const std::array<ReloadFlowBoundary,3>& second,const std::array<int,3>& capacities,const std::array<int,3>& again)noexcept {
    if(!ReloadReserveIdle(identity,first,second,capacities,again))return false;
    // The verified SPAS state machine processes Reload in state2. Pump state7
    // ignores the start pulse (native225746); counts alone do not imply ready.
    for(unsigned n=0;n<3;++n)if(first[n].loaded>=capacities[n]||first[n].reserve<=0)return false;
    return true;
}
std::optional<ReloadFlowBoundary> ReadReloadFlowBoundary(const ReloadStateMemory& memory,const ReloadFlowBinding& binding,
    unsigned base,const ReloadStateSnapshot& s,std::int64_t deadline,unsigned firing,std::int64_t now,ReloadFlowBoundaryDiagnostic* diagnostic)noexcept{
    if(diagnostic)*diagnostic={};
    const auto fail=[&](ReloadFlowBoundaryFailure why)->std::optional<ReloadFlowBoundary>{if(diagnostic)diagnostic->failure=why;return {};};
    if(base!=binding.state.preferredBase||!base||!OwnerValid(s.owner)||!s.sequence||s.observedNs<=0||now<s.observedNs||
       deadline<=s.observedNs||deadline-s.observedNs>250000000||now>=deadline||
       !s.branches[0].address||!s.branches[1].address||s.branches[0].address==s.branches[1].address||
       s.branches[0].wrapperOffset!=0x3c||s.branches[1].wrapperOffset!=0x40||
       !s.config.weaponData||!s.config.firingData||!s.config.primaryFire||
       std::uint64_t(s.config.primaryFire)+0x170!=s.config.ammoAddress||
       std::uint64_t(base)+binding.state.firingVtableRva>UINT32_MAX)return fail(ReloadFlowBoundaryFailure::Lease);
    unsigned branch=2;for(unsigned n=0;n<2;++n)if(s.branches[n].address==firing)branch=n;if(branch==2)return fail(ReloadFlowBoundaryFailure::Branch);
    const Reader r{memory};ScopeBytes before{},after{};std::array<std::byte,0xb0> raw{},again{};
    if(!Scope(r,s,before))return fail(ReloadFlowBoundaryFailure::ScopeBefore);
    if(diagnostic)diagnostic->beforeSoldierFlags=before.soldierFlags;
    if(!r.Read(firing,raw.data(),raw.size()))return fail(ReloadFlowBoundaryFailure::StateReadBefore);
    if(!Scope(r,s,after))return fail(ReloadFlowBoundaryFailure::ScopeAfter);
    if(diagnostic){diagnostic->afterSoldierFlags=after.soldierFlags;auto same=after;same.soldierFlags=before.soldierFlags;
        diagnostic->differsOnlySoldierFlags=before!=after&&same==before;}
    if(before!=after)return fail(ReloadFlowBoundaryFailure::ChangedScope);
    if(!r.Read(firing,again.data(),again.size()))return fail(ReloadFlowBoundaryFailure::StateReadAfter);
    if(raw!=again){if(diagnostic){const auto offset=unsigned(std::mismatch(raw.begin(),raw.end(),again.begin()).first-raw.begin());
        diagnostic->changedOffset=offset;diagnostic->changedBefore=std::to_integer<std::uint8_t>(raw[offset]);diagnostic->changedAfter=std::to_integer<std::uint8_t>(again[offset]);}
        return fail(ReloadFlowBoundaryFailure::ChangedState);}
    if(Word<unsigned>(raw.data(),0)!=base+binding.state.firingVtableRva||
       Word<unsigned>(raw.data(),8)!=s.config.firingData||Word<unsigned>(raw.data(),12)!=s.config.ammoAddress)return fail(ReloadFlowBoundaryFailure::StateIdentity);
    ReloadFlowBoundary out;out.owner=s.owner;out.snapshotSequence=s.sequence;out.firing=firing;out.branch=std::uint8_t(branch);
    out.wrapperOffset=branch?0x40u:0x3cu;out.soldierFlags=before.soldierFlags;
    out.current=Word<unsigned>(raw.data(),0x3c);out.previous=Word<unsigned>(raw.data(),0x40);out.next=Word<unsigned>(raw.data(),0x44);
    out.timer=Word<float>(raw.data(),0x50);out.loaded=Word<std::int32_t>(raw.data(),0x7c);out.reserve=Word<std::int32_t>(raw.data(),0x80);
    out.flagsA8=Word<std::uint8_t>(raw.data(),0xa8);
    if(out.current>15||out.previous>15||out.next>15||!std::isfinite(out.timer)||std::abs(out.timer)>1000000||
       out.loaded< -1||out.loaded>1000000||out.reserve< -1||out.reserve>1000000)return fail(ReloadFlowBoundaryFailure::StateRange);
    return out;
}
bool ReadReloadFlowOwner(const ReloadStateMemory& memory,const ReloadFlowBinding& binding,
    unsigned base,const ReloadStateSnapshot& s,std::int64_t deadline,unsigned firing,std::int64_t now,ReloadFlowBoundaryDiagnostic* diagnostic)noexcept{
    if(diagnostic)*diagnostic={};
    const auto fail=[&](ReloadFlowBoundaryFailure why){if(diagnostic)diagnostic->failure=why;return false;};
    if(base!=binding.state.preferredBase||!base||!OwnerValid(s.owner)||!s.sequence||s.observedNs<=0||now<s.observedNs||
       deadline<=s.observedNs||deadline-s.observedNs>250000000||now>=deadline||
       !s.branches[0].address||!s.branches[1].address||s.branches[0].address==s.branches[1].address||
       s.branches[0].wrapperOffset!=0x3c||s.branches[1].wrapperOffset!=0x40||
       !s.config.weaponData||!s.config.firingData||!s.config.primaryFire||
       std::uint64_t(s.config.primaryFire)+0x170!=s.config.ammoAddress||
       std::uint64_t(base)+binding.state.firingVtableRva>UINT32_MAX)return fail(ReloadFlowBoundaryFailure::Lease);
    if(s.branches[0].address!=firing&&s.branches[1].address!=firing)return fail(ReloadFlowBoundaryFailure::Branch);
    const Reader r{memory};ScopeBytes before{},after{};
    const auto identity=[&]{return r.Equals(firing,base+binding.state.firingVtableRva)&&
        r.Equals(std::uint64_t(firing)+8,s.config.firingData)&&r.Equals(std::uint64_t(firing)+12,s.config.ammoAddress);};
    if(!Scope(r,s,before))return fail(ReloadFlowBoundaryFailure::ScopeBefore);
    if(diagnostic)diagnostic->beforeSoldierFlags=before.soldierFlags;
    if(!identity())return fail(ReloadFlowBoundaryFailure::StateIdentity);
    if(!Scope(r,s,after))return fail(ReloadFlowBoundaryFailure::ScopeAfter);
    if(diagnostic){diagnostic->afterSoldierFlags=after.soldierFlags;auto same=after;same.soldierFlags=before.soldierFlags;
        diagnostic->differsOnlySoldierFlags=before!=after&&same==before;}
    if(before!=after)return fail(ReloadFlowBoundaryFailure::ChangedScope);
    return identity()||fail(ReloadFlowBoundaryFailure::StateIdentity);
}
bool ReloadRestoreMatched(const ReloadFlowRecord& record)noexcept{
    if(record.entry.kind!=ReloadFlowEvent::Restore||!record.finished||!record.identityRetained||!record.exit.boundary||
       !record.entry.snapshotCopied||!record.exit.snapshotCopied||record.entry.copiedSnapshot!=record.exit.copiedSnapshot)return false;
    const auto source=DecodeReloadFiringSnapshot(record.entry.copiedSnapshot);if(!source)return false;
    const auto& after=*record.exit.boundary;
    return after.current==source->current&&after.next==source->next&&after.previous==record.entry.boundary.current&&
        after.timer==source->phaseTimer&&after.loaded==source->loaded&&after.reserve==source->reserve;
}
ReloadFlowRecords::ReloadFlowRecords()noexcept=default;
std::uint64_t ReloadFlowRecords::Begin(const ReloadFlowEventInput& in)noexcept{
    if(!in.thread||in.nowNs<=0||!in.depth||in.depth>8||!OwnerValid(in.boundary.owner)||!in.boundary.firing||
       unsigned(in.kind)>unsigned(ReloadFlowEvent::Restore)){++rejected_;return 0;}
    if(in.parent){
        if(in.parent>count_){++rejected_;return 0;}
        const auto& parent=records_[std::size_t(in.parent)-1];
        if(parent.finished||parent.entry.thread!=in.thread||parent.entry.depth>=in.depth||
           !SameIdentity(parent.entry.boundary,in.boundary)){++rejected_;return 0;}
    }
    if(in.update){
        if(in.update>count_){++rejected_;return 0;}
        const auto& update=records_[std::size_t(in.update)-1];
        if(update.finished||update.entry.kind!=ReloadFlowEvent::Update||update.entry.thread!=in.thread||
           !SameIdentity(update.entry.boundary,in.boundary)){++rejected_;return 0;}
    }
    if(count_==Capacity){++dropped_;return 0;}
    auto& record=records_[count_];record.id=++count_;record.entry=in;return record.id;
}
bool ReloadFlowRecords::End(std::uint64_t id,const ReloadFlowEventEnd& end)noexcept{
    if(!id||id>count_){++rejected_;return false;}auto& record=records_[std::size_t(id)-1];
    if(record.finished||end.thread!=record.entry.thread||end.nowNs<record.entry.nowNs){++rejected_;return false;}
    record.exit=end;record.finished=true;record.identityRetained=end.boundary&&SameIdentity(record.entry.boundary,*end.boundary);return true;
}
std::uint64_t ReloadFlowInvocationIds::Next()noexcept {
    auto previous=previous_.load(std::memory_order_relaxed);
    while(previous!=UINT64_MAX){if(previous_.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed))return previous+1;}
    return 0;
}
bool ReloadFlowInvocation::Begin(std::uint64_t id,const ReloadFlowEventInput& source,const ReloadFlowInvocation* parent)noexcept {
    if(record_.id||!id||!source.thread||source.nowNs<=0||!source.depth||source.depth>8||!OwnerValid(source.boundary.owner)||
        !source.boundary.firing||unsigned(source.kind)>unsigned(ReloadFlowEvent::Restore)||source.parent||source.update)return false;
    auto entry=source;
    if(parent){const auto& p=parent->record_;
        if(!p.id||p.id>=id||p.finished||p.entry.thread!=entry.thread||p.entry.depth>=entry.depth||!SameIdentity(p.entry.boundary,entry.boundary))return false;
        entry.parent=p.id;entry.update=p.entry.kind==ReloadFlowEvent::Update?p.id:p.entry.update;
    }
    if(entry.kind==ReloadFlowEvent::Update)entry.update=id;
    entry.nativeInvocation=id;entry.nativeParent=entry.parent;entry.nativeUpdate=entry.update;
    record_={id,entry,{},false,false};return true;
}
std::optional<ReloadFlowRecord> ReloadFlowInvocation::Finish(const ReloadFlowEventEnd& end)noexcept {
    if(!record_.id||record_.finished)return {};
    record_.finished=true;record_.exit=end;
    if(end.thread!=record_.entry.thread||end.nowNs<record_.entry.nowNs||!end.boundary)return {};
    const auto kind=record_.entry.kind;
    if((kind==ReloadFlowEvent::Update||kind==ReloadFlowEvent::Commit)&&(!record_.entry.contextCopied||!end.contextCopied))return {};
    if(kind==ReloadFlowEvent::Restore&&(!record_.entry.snapshotCopied||!end.snapshotCopied))return {};
    record_.identityRetained=SameIdentity(record_.entry.boundary,*end.boundary);
    if(!record_.identityRetained)return {};
    return record_;
}
}

#if defined(_M_IX86)
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
namespace fvr::bc2::reloadFlowRuntime {
namespace {
using UpdateFn=void(__thiscall*)(void*,void*,unsigned);
using StepFn=void(__thiscall*)(void*,void*,float);
using CommitFn=void(__thiscall*)(void*,void*);
using TransferFn=void(__thiscall*)(void*,unsigned);
using AbortFn=void(__thiscall*)(void*,unsigned); // inspected ECX self, stack bool, ret4
#ifdef FVR_BC2_PREHOLD_CLEANUP
PreholdCleanupLedger preholdMonitor;
using PreholdMonitorBranchReport=PreholdOperationBranchReceipt;
std::array<PreholdMonitorBranchReport,3> preholdMonitorBranches{};
std::atomic<std::int64_t> preholdFirstFailureNs=0;
std::atomic<unsigned> preholdFirstFailureReason=0,preholdFirstFailureBranch=3;
std::atomic<std::int64_t> preholdFirstServerDeniedNs=0;std::atomic<unsigned> preholdFirstServerDeniedReason=0;
void PreholdServerDenied(unsigned reason,std::int64_t now)noexcept {
    std::int64_t expected=0;if(preholdFirstServerDeniedNs.compare_exchange_strong(expected,now))preholdFirstServerDeniedReason=reason;
}

std::atomic<bool> preholdMonitorStarted=false,preholdMonitorCancelled=false,preholdMonitorFailed=false;
void PreholdMonitorFailure(unsigned reason,unsigned branch,std::int64_t now)noexcept {
    std::int64_t expected=0;if(preholdFirstFailureNs.compare_exchange_strong(expected,now)){
        preholdFirstFailureReason=reason;preholdFirstFailureBranch=branch;
    }preholdMonitorFailed=true;
}
std::atomic<unsigned> preholdMonitorCalled=0,preholdMonitorExact=0,preholdMonitorIdle=0;
// Distinct own-server original-Update receipt, never a helper/held receipt.
std::atomic<std::int64_t> preholdAuthoritativeIdleNs=0;
std::optional<ReloadPreholdEntryObservation> preholdEntryObservation; // policy exclusion only
PreholdOperationJournal preholdOperationJournal;
std::optional<PreholdCleanupEvidence> preholdLastCoherent; // already-read Update evidence only
#endif
AbortFn nativeAbort=nullptr;ReloadHoldCodeProof abortCode{};ReloadAbortCleanup abortCleanup;
struct AbortRecord {std::uint64_t cycle=0,invocation=0,revision=0,dispatchEpoch=0;std::int64_t begin=0,end=0,deadline=0;unsigned branch=3;ReloadFiringObservation before{},after{};bool called=false,helperExact=false,contextUnchanged=false,ownerRetained=false,completed=false;};
std::array<AbortRecord,32> abortRecords{};std::atomic<unsigned> abortRecordCount=0,abortArmed=0,abortCalls=0,abortCompleted=0;
StepFn originalStep=nullptr;
UpdateFn originalUpdate=nullptr;CommitFn originalCommit=nullptr,originalRestore=nullptr;TransferFn originalTransfer=nullptr;
ReloadFlowBinding binding{};std::optional<ReloadServerBinding> serverBinding;ReloadStateMemory memory{};unsigned base=0;
std::array<void*,5> hooks{};ReloadFlowRecords records;ReloadDeferredCompletions recordCompletions;
std::atomic_flag recordGate=ATOMIC_FLAG_INIT,ownerGate=ATOMIC_FLAG_INIT;
std::atomic<bool> enabled=false;std::atomic<unsigned> active=0;
std::atomic<std::uint64_t> ownerRevision=1;
struct Lease {ReloadStateSnapshot snapshot{};std::int64_t deadline=0;std::uint64_t revision=0;std::optional<ReloadServerSnapshot> server;};Lease published;
// One bounded, coherent descriptor rejection retained for the existing report.
// ownerGate protects it; no diagnostic-time native reads or relaxed admission.
struct MagazineProfileRejection {ReloadStateSnapshot snapshot{};bool serverKnown=false,requestEnabled=false;};
std::optional<MagazineProfileRejection> magazineProfileRejection;
std::int64_t frequency=0,startNs=0;bool installed=false,started=false,drained=false;
std::atomic<unsigned> ownerDrops=0,recordBusy=0,recordBeginBusy=0,recordEndBusy=0,ownerMisses=0,readMisses=0,contextMisses=0,nestingMisses=0,windowExpired=0;
std::array<std::atomic<unsigned>,4> calls{},matches{},serverMatches{};
std::atomic<unsigned> serverPublishAttempts=0,serverPublishMisses=0,serverReadMisses=0;
bool diagnosticHold=false,diagnosticRound=false,requestMode=false,holdCodeVerified=false,pumpDiagnostic=false;
Bc2ReloadNativePolicy requestCycle;ReloadFlowInvocationIds invocationIds;
bool combinedFamilies=false;
MagazineEmptyControlReceipts emptyControlReceipts; // requestGate only
std::atomic_flag emptyDiagnosticGate=ATOMIC_FLAG_INIT,interactionDiagnosticGate=ATOMIC_FLAG_INIT;
MagazineEmptyDiagnosticJournal emptyDiagnosticJournal;
MagazineInteractionDiagnostic latestInteractionDiagnostic;
std::atomic<unsigned> emptyDiagnosticLockDrops=0,interactionDiagnosticLockDrops=0;

std::array<std::atomic<unsigned>,3> emptyRequested{},emptyApplied{},emptyRestored{};
std::atomic<unsigned> emptyPatchFailures=0,emptyRestoreFailures=0,emptyReceiptFailures=0;
std::atomic<std::uint64_t> familyEpoch=0;
struct RetiredFamily {ReloadNativeFamily family{};ReloadHoldIdentity identity{};std::uint64_t cycle=0,epoch=0,event=0;};
std::optional<RetiredFamily> retiredFamily;
std::atomic<unsigned> familySelectAttempts=0,familySelectChanges=0,familySelectRejected=0;
// Bounded stage counts distinguish dispatcher rejection from native count gaps.
std::array<std::atomic<unsigned>,10> familySelectFailures{},reserveReadStages{};std::atomic<unsigned> reserveQuietRetries=0,reserveQuietWins=0,reserveQuietTimeouts=0;
ReserveReadEvidenceJournal reserveReadEvidence;
std::atomic<unsigned> familyRetirementReuses=0;
std::atomic_flag requestGate=ATOMIC_FLAG_INIT,requestEntryGate=ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> requestCancelEpoch=0;std::uint64_t requestAppliedCancel=0;
// Fail-closed empty gate: lock-free mirrors of the request cycle (written at
// the end of every WithRequest body) and of the last published owner.
std::atomic<std::uint32_t> localServerInventory{0},localServerFiring{0};std::atomic<ULONGLONG> localServerInventoryTick{0};
bool closedEmptyGate=false;
std::atomic<unsigned> requestPhaseMirror=0,requestFamilyMirror=0;std::atomic<std::uint64_t> requestAppliedCancelMirror=0;
std::atomic<std::int64_t> requestArmingSinceNs=0;
// Event mailbox (design step 3b, FVR_BC2_RELOAD_MAILBOX=1, magazine cycles):
// the Update hook applies only the board's command for its copy and pushes a
// receipt; the gather replays receipts through the cycle and republishes.
bool mailboxMode=false;ReloadCommandBoard mailboxBoard;
struct MailboxReceipt {ReloadHoldInput input{};bool timing=false,coherent=false,finished=false,retained=false;
    std::uint64_t update=0,transferParent=0;ReloadCommandKind applied=ReloadCommandKind::None;ReloadDeltaOverride delta{};
    ReloadFiringObservation after{};std::int64_t nowNs=0,afterNs=0;std::optional<ReloadRoundTransfer> transfer;};
std::array<ReceiptRing<MailboxReceipt,64>,3> mailboxRings{};std::array<std::uint64_t,3> mailboxCursor{};
thread_local MailboxReceipt* mailboxPending=nullptr;thread_local std::uint64_t mailboxPendingCycle=0;
std::array<std::atomic<unsigned>,3> mailboxHolds{},mailboxAdvances{};std::array<float,3> mailboxTimer{};std::array<bool,3> mailboxWaiting{};
unsigned mailboxReplays=0,mailboxReleases=0,mailboxStale=0,mailboxLost=0,mailboxPublishes=0,mailboxPumpBusy=0;
std::atomic<unsigned> evaluateContentionSkips=0;
std::atomic<unsigned> submitRetries=0,submitRetried=0,submitRetryExpired=0; // SubmitRequest transient retries
unsigned abortRestamped=0,abortArmFailures=0,abortReleases=0,abortAfterTransfer=0,stopRepeats=0;std::uint64_t stopHandledCycle=0;
std::atomic<std::int64_t> stopArmedAtNs=0;
std::atomic<std::uint64_t> requestCycleMirror=0; // CancelRequestCycle stop outcomes (requestGate)
// Lease-free stop. ClearReloadOwner (focus or head loss, stale input, a failed
// state read) runs mid-reload: the abort is armed, but the owner revision
// bumps and no owner lease exists, so the normal TryAbort cannot claim and the
// unheld native reload finishes into a free refill. For 3 s after a stop is
// armed, an Update on one of the stopped copies with no owner lease aborts it
// directly when the copy still structurally belongs to that weapon (vtable,
// firing data, ammo address) and still holds the stopped counts at 10/11.
struct OrphanStop {std::uint64_t cycle=0;std::array<unsigned,3> firing{};unsigned firingData=0,ammoAddress=0;std::int32_t loaded=-1,reserve=-1;std::int64_t untilNs=0;};
SeqValue<OrphanStop> orphanStop;std::atomic<unsigned> orphanCalls=0,orphanExact=0,orphanRejects=0,orphanDeferred=0;

// Magazine profile ids are 64-bit keys (F2000 0xa83a3c5288dee2d4); a packed
// 32-bit mirror truncated them and read F2000 as unsupported (run 145846).
std::atomic<std::uint64_t> requestProfileMirror=0;
// Token passes: an empty 2/2 player copy let through for our own start pulse.
struct TokenPass {std::int64_t nowNs=0,ageNs=-1;unsigned branch=3,stage=0,input=0,gate=0;}; // gate 0 lease, 1 closed, 2 unsupported family, 3 stock fallback
std::array<TokenPass,16> tokenPasses{};std::atomic<unsigned> tokenPassCount=0;
void RecordTokenPass(TokenPass row)noexcept{const auto n=tokenPassCount.fetch_add(1,std::memory_order_relaxed);tokenPasses[n%tokenPasses.size()]=row;}
std::int64_t Now()noexcept;
struct ClosedIdentity {std::array<unsigned,3> copies{};std::int64_t observedNs=0;unsigned firingData=0,ammoAddress=0;bool supported=false;};
// Fail-stuck guard: gameplay grants a short window in which a Reload press
// reaches the native reload (the physical path did not start).
std::atomic<std::int64_t> stockTokenUntilNs=0;std::atomic<unsigned> stockTokenGrants=0,closedStateMisses=0,closedPatchFailures=0;
std::atomic<unsigned> closedIdentitySeq=0;ClosedIdentity closedIdentity{};
std::atomic<unsigned> closedApplied[3]{},closedRestoreFailures=0,closedTokenPasses=0,closedUnsupportedPasses=0,closedIdentityMisses=0,closedContextMisses=0;
std::optional<ClosedIdentity> ReadClosedIdentity()noexcept {
    // F3: a writer-contended read falls back to this thread's last good copy;
    // the hook's structural check still applies to it.
    thread_local std::optional<ClosedIdentity> lastGood;
    for(unsigned n=0;n<8;++n){const auto s=closedIdentitySeq.load(std::memory_order_acquire);
        if(s&1u){YieldProcessor();continue;}
        ClosedIdentity c=closedIdentity;std::atomic_thread_fence(std::memory_order_acquire);
        if(closedIdentitySeq.load(std::memory_order_relaxed)==s){lastGood=c;return c;}}
    return lastGood;
}
std::atomic<unsigned> requestContention=0,requestReadFailures=0;
std::atomic<unsigned> requestLockWaits=0,requestLockRecovered=0,requestLockSampleCount=0;
std::int64_t requestLockMaxRecoveredWaitNs=0; // requestGate, reported only after drain.
struct RequestLockSample {unsigned thread=0,firing=0;ReloadPolicyLockEvidence evidence{};unsigned site=0;};
std::array<RequestLockSample,16> requestLockSamples{};
ReloadLockBodyTelemetry requestBodyTelemetry;
std::atomic<unsigned> requestOwnerLockWaits=0,requestOwnerLockRecovered=0,requestOwnerLockFailed=0;
std::int64_t requestOwnerLockMaxRecoveredWaitNs=0; // ownerGate, reported only after drain.
std::atomic<unsigned> requestUndeliveredAcknowledgements=0;
std::atomic<std::uint64_t> requestLastUndelivered=0;
std::array<std::atomic<unsigned>,3> requestTargets{};
ReloadRoundGate roundGate;std::atomic_flag roundGateLock=ATOMIC_FLAG_INIT;
std::atomic<bool> roundCancelled=false;
std::atomic<std::uint64_t> callbackRevision=0;
std::atomic<unsigned> roundContention=0;std::array<std::atomic<unsigned>,3> roundTargets{};
ReloadHoldProbe holdProbe;
std::array<std::atomic<unsigned>,3> holdApplied{},holdRestored{},seatAdvances{},pumpHoldApplied{};std::atomic<unsigned> pumpHoldFailures=0;PumpHoldGate pumpGate;
std::array<unsigned,3> requestTransferCounts{};
std::array<std::uint64_t,3> requestLastTransfer{};
ReloadFlowInvocationIds reserveSequence;
ReloadRetirementReceipts retirementReceipts;
std::atomic<unsigned> retireAttempts=0,retireBusy=0,retireWrongCycle=0,retireUnquiet=0,retireDropped=0,retireSuccesses=0;
std::atomic<std::uint64_t> retireLastEvent=0,retireLastCycle=0;
std::atomic<std::int64_t> retireLastObserved=0,retireLastDeadline=0;
std::atomic<unsigned> holdOriginalCalls=0,holdPatchFailures=0,holdRestoreFailures=0,holdSampleFailures=0;
struct HoldReadRejection {
    std::int64_t now=0;unsigned callerBranch=3,readBranch=3,firing=0;bool boundaryOkay=false,capacityOkay=false;int capacity=0;
    ReloadServerBoundaryDiagnostic server{};
    ReloadFlowBoundaryDiagnostic client{};
};
std::array<std::array<std::array<std::atomic<unsigned>,2>,3>,3> holdReadRejected{};
std::array<HoldReadRejection,16> holdReadRejectionSamples{};std::atomic<unsigned> holdReadRejectionCount=0;
std::array<std::atomic<unsigned>,6> holdPrepareRejected{}; // boundary,stack,owner/config,state-read,context,lease
std::array<std::atomic<unsigned>,3> cohortRetryAttempts{},cohortRetryRecovered{},cohortRetryFailed{};
struct CohortRetrySample {unsigned firing=0;std::uint64_t sequence=0;std::int64_t deadline=0;ReloadCohortRetryAttempt attempt{};};
std::array<CohortRetrySample,16> cohortRetrySamples{};std::atomic<unsigned> cohortRetrySampleCount=0;

struct Frame {Frame* previous=nullptr;std::uint64_t id=0,update=0,ownerRevision=0;unsigned firing=0,depth=0;ReloadStateOwner owner{};ReloadFlowInvocation invocation;};
thread_local Frame* currentFrame=nullptr;
struct RequestBoundaryEvidence {unsigned stage=0,readBranch=3;ReloadFlowBoundaryDiagnostic client{};ReloadServerBoundaryDiagnostic server{};};
enum class PhaseRetryPath:unsigned {ClientBoundary,ServerBoundary,ClientOwner,ServerOwner};
struct PhaseRetrySample {PhaseRetryPath path{};unsigned firing=0;std::uint64_t sequence=0;std::int64_t deadline=0;ReloadPhaseRetryAttempt attempt{};};
std::array<std::atomic<unsigned>,4> phaseRetryAttempts{},phaseRetryRecovered{},phaseRetryFailed{};
std::atomic<unsigned> phaseRetrySampleCount=0;std::array<PhaseRetrySample,16> phaseRetrySamples{};
struct RequestReadMiss {std::int64_t now=0;unsigned firing=0,stage=0,kind=0,phase=0;bool deferred=false;RequestBoundaryEvidence boundary{};};
struct RequestCancellation {std::int64_t now=0;unsigned reason=0;std::uint64_t epoch=0;};
std::array<RequestReadMiss,32> requestReadMisses{};std::atomic<unsigned> requestReadMissCount=0,requestDeferredMisses=0;
std::array<RequestCancellation,16> requestCancellations{};std::atomic<unsigned> requestCancellationCount=0;
std::int64_t Now()noexcept;
void RequestCancelAsync(unsigned reason=0)noexcept;
// A hook arriving while a short exclusive section (cycle inspection/retire on
// the gather thread) holds the entry gate passed its Update through ungated,
// so an empty copy could auto-reload natively on that Update (F2000 runs
// 131709/134305: server-only 2 -> 10 at empty). Wait briefly, without
// counting as active, for the section to end before entering.
std::atomic<unsigned> entryWaits=0,entryWaitWins=0,entryDenied=0;
void WaitForEntryGate()noexcept {
    if(!requestEntryGate.test(std::memory_order_acquire))return;++entryWaits;
    LARGE_INTEGER f{},a{},b{};QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
    while(requestEntryGate.test(std::memory_order_acquire)){QueryPerformanceCounter(&b);if((b.QuadPart-a.QuadPart)*1000000>f.QuadPart*1000)return;YieldProcessor();}
    ++entryWaitWins;
}
struct Active {
    bool requestEntryAllowed=true;
    Active(){
        if(!requestMode){if(diagnosticRound||pumpDiagnostic)callbackRevision.fetch_add(1,std::memory_order_acq_rel);active.fetch_add(1,std::memory_order_acq_rel);return;}
        WaitForEntryGate();
        requestEntryAllowed=EnterReloadInvocation(active,callbackRevision,requestEntryGate);if(!requestEntryAllowed)++entryDenied;
        if(!requestEntryAllowed)RequestCancelAsync(1);
    }
    ~Active(){if(requestMode)ExitReloadInvocation(active,callbackRevision);
        else {if(diagnosticRound||pumpDiagnostic)callbackRevision.fetch_add(1,std::memory_order_acq_rel);active.fetch_sub(1,std::memory_order_acq_rel);}}
};
struct Gate {std::atomic_flag& flag;bool held;explicit Gate(std::atomic_flag& f):flag(f),held(!f.test_and_set(std::memory_order_acquire)){}~Gate(){if(held)flag.clear(std::memory_order_release);}};
struct OwnerCopyGate {
    ReloadPolicyLock lock;const bool held;
    OwnerCopyGate():lock(ownerGate,Now,[]{YieldProcessor();},requestMode),held(lock.Held()) {
        const auto& e=lock.Evidence();if(!requestMode||!e.contended)return;
        ++requestOwnerLockWaits;if(e.held){++requestOwnerLockRecovered;
            requestOwnerLockMaxRecoveredWaitNs=std::max(requestOwnerLockMaxRecoveredWaitNs,e.endNs-e.beginNs);
        }else ++requestOwnerLockFailed;
    }
};
bool RequestTarget(unsigned firing)noexcept{return firing&&std::any_of(requestTargets.begin(),requestTargets.end(),[&](const auto& id){return id.load(std::memory_order_acquire)==firing;});}
void RequestCancelAsync(unsigned reason)noexcept{const auto epoch=requestCancelEpoch.fetch_add(1,std::memory_order_acq_rel)+1;const auto n=requestCancellationCount.fetch_add(1);if(n<requestCancellations.size())requestCancellations[n]={Now(),reason,epoch};}
template<class F>bool WithRequest(F&& f,bool cancelOnContention,bool* deferred=nullptr,unsigned site=0)noexcept {
    if(!requestMode)return false;
    ReloadPolicyLock lock(requestGate,Now,[]{YieldProcessor();});
    const auto& evidence=lock.Evidence();
    if(evidence.contended){++requestLockWaits;if(evidence.held)++requestLockRecovered;
        const auto wait=evidence.endNs>=evidence.beginNs?evidence.endNs-evidence.beginNs:0;
        if(evidence.held)requestLockMaxRecoveredWaitNs=std::max(requestLockMaxRecoveredWaitNs,wait);
        const auto slot=requestLockSampleCount.fetch_add(1);if(slot<requestLockSamples.size())
            requestLockSamples[slot]={GetCurrentThreadId(),currentFrame?currentFrame->firing:0,evidence,site};}
    if(!lock.Held()){++requestContention;if(cancelOnContention)RequestCancelAsync(2);else if(deferred)*deferred=true;return false;}
    struct BodyTimer {unsigned site,thread,firing;std::int64_t begin;
        ~BodyTimer(){requestBodyTelemetry.Observe(site,thread,firing,begin,Now());}};
    BodyTimer body{site,GetCurrentThreadId(),currentFrame?currentFrame->firing:0,Now()};
    struct Mirror {~Mirror(){const auto phase=unsigned(requestCycle.Phase());
        if(phase==unsigned(ReloadRequestCyclePhase::Arming)&&requestPhaseMirror.load(std::memory_order_relaxed)!=phase)requestArmingSinceNs.store(Now(),std::memory_order_release);
        requestPhaseMirror.store(phase,std::memory_order_release);requestCycleMirror.store(requestCycle.Cycle(),std::memory_order_release);
        requestFamilyMirror.store(unsigned(requestCycle.Family()),std::memory_order_release);
        requestProfileMirror.store(std::uint64_t(requestCycle.MagazineProfileId()),std::memory_order_release);
        requestAppliedCancelMirror.store(requestAppliedCancel,std::memory_order_release);}} mirror;
    const auto epoch=requestCancelEpoch.load(std::memory_order_acquire);
    if(epoch!=requestAppliedCancel){requestCycle.Cancel(ReloadRequestCycleFailure::Owner);requestAppliedCancel=epoch;}
    f();
    if(epoch!=requestCancelEpoch.load(std::memory_order_acquire)){requestCycle.Cancel(ReloadRequestCycleFailure::Owner);return false;}
    return true;
}
void RequestMissing(unsigned firing,unsigned stage,unsigned kind,const RequestBoundaryEvidence* evidence=nullptr)noexcept {
    if(!RequestTarget(firing))return;
    ++requestReadFailures;const auto index=requestReadMissCount.fetch_add(1);
    RequestReadMiss miss{Now(),firing,stage,kind};if(evidence)miss.boundary=*evidence;
    WithRequest([&]{miss.phase=unsigned(requestCycle.Phase());miss.deferred=requestCycle.MissingEvidence(Now());
        if(miss.deferred)++requestDeferredMisses;else RequestCancelAsync(3);},true);
    if(index<requestReadMisses.size())requestReadMisses[index]=miss;
}
std::int64_t Now()noexcept{LARGE_INTEGER value{};if(frequency<=0||!QueryPerformanceCounter(&value)||value.QuadPart<=0)return 0;
    return (value.QuadPart/frequency)*1000000000+(value.QuadPart%frequency)*1000000000/frequency;}
bool IsCurrent(const Lease& lease,std::int64_t now)noexcept{
    return enabled.load(std::memory_order_acquire)&&lease.revision==ownerRevision.load(std::memory_order_acquire)&&
        now>=lease.snapshot.observedNs&&now<lease.deadline;
}
std::optional<Lease> Owner(unsigned firing,std::int64_t now)noexcept{
    OwnerCopyGate gate;if(!gate.held){++ownerDrops;return {};}
    if(!IsCurrent(published,now)||!IsCurrent(published,Now())||(published.snapshot.branches[0].address!=firing&&published.snapshot.branches[1].address!=firing&&(!published.server||published.server->links.firing!=firing)))return {};
    return published;
}
void RecordPhaseRetry(PhaseRetryPath path,unsigned firing,const Lease& lease,std::int64_t deadline,const ReloadPhaseRetryAttempt& attempt)noexcept {
    if(!attempt.attempted)return;const auto index=unsigned(path);++phaseRetryAttempts[index];
    if(attempt.recovered)++phaseRetryRecovered[index];else ++phaseRetryFailed[index];
    const auto slot=phaseRetrySampleCount.fetch_add(1);if(slot<phaseRetrySamples.size())phaseRetrySamples[slot]={path,firing,lease.snapshot.sequence,deadline,attempt};
}
std::optional<ReloadFlowBoundary> BoundaryFor(const Lease& lease,unsigned firing,std::int64_t now,ReloadServerBoundaryDiagnostic* diagnostic=nullptr,ReloadFlowBoundaryDiagnostic* clientDiagnostic=nullptr)noexcept{
    if(lease.server&&serverBinding&&lease.server->links.firing==firing){
        const auto deadline=std::min(lease.deadline,lease.snapshot.observedNs+200000000ll);
        ReloadServerBoundaryDiagnostic final;ReloadPhaseRetryAttempt retry;
        const auto read=ReadReloadPhaseCoherent([&](std::int64_t at,ReloadServerBoundaryDiagnostic& d){
            return ReadReloadServerBoundary(memory,*serverBinding,binding.state,base,*lease.server,deadline,at,&d);},Now,now,final,retry);
        RecordPhaseRetry(PhaseRetryPath::ServerBoundary,firing,lease,deadline,retry);if(diagnostic)*diagnostic=final;
        if(!read)return {};ReloadFlowBoundary out;out.owner=lease.snapshot.owner;out.snapshotSequence=lease.snapshot.sequence;
        out.firing=firing;out.wrapperOffset=0x10;out.branch=2;out.soldierFlags=lease.snapshot.soldierFlags;
        out.serverPlayer=lease.server->links.player;out.serverSoldier=lease.server->links.soldier;out.serverItem=lease.server->links.item;
        out.current=read->currentState;out.previous=read->previousState;out.next=read->nextState;out.timer=read->phaseTimer;
        out.loaded=read->loaded;out.reserve=read->reserve;out.flagsA8=read->flagsA8;return out;
    }
    ReloadFlowBoundaryDiagnostic final;ReloadPhaseRetryAttempt retry;
    const auto read=ReadReloadPhaseCoherent([&](std::int64_t at,ReloadFlowBoundaryDiagnostic& d){
        return ReadReloadFlowBoundary(memory,binding,base,lease.snapshot,lease.deadline,firing,at,&d);},Now,now,final,retry);
    RecordPhaseRetry(PhaseRetryPath::ClientBoundary,firing,lease,lease.deadline,retry);if(clientDiagnostic)*clientDiagnostic=final;return read;
}
template<std::size_t N>bool Context(unsigned address,std::array<std::byte,N>& out)noexcept{
    return address>=0x10000&&address<=UINT32_MAX-out.size()&&memory.read&&memory.read(memory.context,address,out.data(),out.size());
}
bool RequestOwnerEvidence(const Lease& lease,std::int64_t now,RequestBoundaryEvidence* evidence=nullptr)noexcept {
    if(!lease.server||!serverBinding||!IsCurrent(lease,now))return false;
    // Ownership is structural. Requiring a sibling's mutable timer/state to
    // stand still here falsely cancels valid overlapping client/server updates.
    // Each own-event boundary and PrepareHold still require full state evidence.
    for(unsigned branch=0;branch<3;++branch){RequestBoundaryEvidence read;read.readBranch=branch;
        ReloadPhaseRetryAttempt retry;bool valid=false;
        if(branch==2){const auto deadline=std::min(lease.deadline,lease.snapshot.observedNs+200000000ll);
            valid=ReadReloadPhaseCoherent([&](std::int64_t at,ReloadServerBoundaryDiagnostic& d){
                return ReadReloadServerOwner(memory,*serverBinding,binding.state,base,*lease.server,deadline,at,&d);},Now,Now(),read.server,retry);
            RecordPhaseRetry(PhaseRetryPath::ServerOwner,lease.server->links.firing,lease,deadline,retry);
        }else {const auto firing=lease.snapshot.branches[branch].address;
            valid=ReadReloadPhaseCoherent([&](std::int64_t at,ReloadFlowBoundaryDiagnostic& d){
                return ReadReloadFlowOwner(memory,binding,base,lease.snapshot,lease.deadline,firing,at,&d);},Now,Now(),read.client,retry);
            RecordPhaseRetry(PhaseRetryPath::ClientOwner,firing,lease,lease.deadline,retry);}
        if(!valid){if(evidence)*evidence=read;return false;}}
    return IsCurrent(lease,Now());
}
// Live gun-event rows (docs/GUN-EVENT-LOG.md). Built only from boundary values
// these hooks already read for their own checks: no extra native read or write,
// no lock held, inert unless NativeProbe opened the log.
GunCopyState GunState(const ReloadFlowBoundary& b)noexcept{GunCopyState s;s.firing=b.firing;s.current=b.current;s.next=b.next;
    s.timer=b.timer;s.loaded=b.loaded;s.reserve=b.reserve;s.flagsA8=b.flagsA8;return s;}
GunCopyState GunState(const ReloadFiringObservation& b)noexcept{GunCopyState s;s.firing=b.address;s.current=b.currentState;s.next=b.nextState;
    s.timer=b.phaseTimer;s.loaded=b.loaded;s.reserve=b.reserve;s.flagsA8=b.flagsA8;return s;}
GunWeapon GunWeaponOf(const Lease& lease)noexcept{const auto& a=lease.snapshot.config.assetName;
    return MakeGunWeapon(std::string_view(a.data(),std::size_t(std::find(a.begin(),a.end(),'\0')-a.begin())),lease.snapshot.owner.weapon);}
std::string_view GunTransferPath(const std::optional<ReloadTransferPath>& path)noexcept{
    if(!path)return "unclassified";
    switch(*path){case ReloadTransferPath::OrdinaryState12:return "state12";case ReloadTransferPath::TimedInterruption:return "timed_interruption";
        case ReloadTransferPath::SpecialLogic4:return "special_logic4";case ReloadTransferPath::ZeroDurationPreparation:return "zero_duration_preparation";}
    return "unclassified";
}
struct Observation {
    Frame frame{};std::optional<Lease> lease;unsigned context=0,snapshot=0;
    ReloadDeltaOverride hold{};bool holdRequested=false,finished=false;ReloadFlowEventInput entry{};RequestBoundaryEvidence evidence{};
    std::optional<ReloadFlowBoundary> gunEntry;GunWeapon gunWeapon{};ReloadFlowEvent gunKind=ReloadFlowEvent::Update;std::optional<ReloadTransferPath> gunPath;
    bool gunFire=false,gunEmit=false,gunContext=false; // Update entry context: fire bit (+0x2c bit0), emit (+0x24)
    Observation(ReloadFlowEvent kind,unsigned firing,unsigned caller,unsigned ctx,unsigned argument)noexcept{
        frame.previous=currentFrame;frame.firing=firing;frame.depth=frame.previous?frame.previous->depth+1:1;currentFrame=&frame;
        if(!enabled.load(std::memory_order_acquire))return;
        ++calls[unsigned(kind)];const auto now=Now();
        const bool recordWindow=now>0&&now>=startNs&&now-startNs<20000000000ll;
        if(!recordWindow){++windowExpired;if(!requestMode||now<=0||(!RequestTarget(firing)&&!combinedFamilies))return;}
        if(frame.depth>8){evidence.stage=1;++nestingMisses;return;}
        lease=Owner(firing,now);if(!lease){evidence.stage=2;++ownerMisses;return;}
        const bool server=lease->server&&lease->server->links.firing==firing;
        evidence.readBranch=server?2:(firing==lease->snapshot.branches[0].address?0:1);
        const auto state=BoundaryFor(*lease,firing,now,&evidence.server,&evidence.client);
        if(!state)evidence.stage=3;
        else if(!IsCurrent(*lease,Now()))evidence.stage=4;
        else if(requestMode&&!RequestOwnerEvidence(*lease,Now(),&evidence))evidence.stage=5;
        if(evidence.stage){if(server)++serverReadMisses;else ++readMisses;lease.reset();return;}
        ReloadFlowEventInput input;input.kind=kind;input.thread=GetCurrentThreadId();input.depth=frame.depth;
        input.caller=caller;input.context=ctx;input.argument=argument;input.nowNs=now;input.tickMs=GetTickCount64();input.boundary=*state;
        if(kind==ReloadFlowEvent::Restore){snapshot=ctx;input.snapshotCopied=Context(ctx,input.copiedSnapshot);if(!input.snapshotCopied)++contextMisses;}
        else {context=ctx;if(ctx){input.contextCopied=Context(ctx,input.copiedContext);if(!input.contextCopied)++contextMisses;}}
        if(kind==ReloadFlowEvent::Transfer){
            if(server){for(const auto& site:binding.transfers)if(std::uint64_t(base)+site.returnRva==caller&&
                (site.path!=ReloadTransferPath::SpecialLogic4||lease->snapshot.config.fireLogicType==4))input.transferPath=site.path;}
            else {const auto site=ClassifyReloadTransfer(binding,base,caller,firing,lease->snapshot);if(site)input.transferPath=site->path;}
        }
        if(GunEvents().Enabled()){gunEntry=*state;gunKind=kind;gunWeapon=GunWeaponOf(*lease);gunPath=input.transferPath;
            if(kind==ReloadFlowEvent::Update&&input.contextCopied)if(const auto c=DecodeReloadUpdateContext(input.copiedContext)){
                gunContext=true;gunFire=c->fireRequested;gunEmit=c->flags24Through28[0];}
            if(kind==ReloadFlowEvent::Update||kind==ReloadFlowEvent::Commit)GunNativeTracker().Observe(GunEvents(),gunWeapon,GunCopy(state->branch),GunState(*state));}
        frame.owner=state->owner;frame.ownerRevision=lease->revision;
        if(requestMode){const ReloadFlowInvocation* parent=nullptr;
            if(frame.previous&&frame.previous->invocation.Record().id&&frame.previous->firing==firing&&frame.previous->owner==state->owner)parent=&frame.previous->invocation;
            if(!frame.invocation.Begin(invocationIds.Next(),input,parent)){RequestMissing(firing,1,unsigned(kind),&evidence);return;}
            entry=frame.invocation.Record().entry;
            input.nativeInvocation=entry.nativeInvocation;input.nativeParent=entry.nativeParent;input.nativeUpdate=entry.nativeUpdate;
        }
        if(!recordWindow)return;
        if(frame.previous&&frame.previous->id&&frame.previous->firing==firing&&frame.previous->owner==state->owner){
            input.parent=frame.previous->id;input.update=frame.previous->update;}
        Gate gate(recordGate);if(!gate.held){++recordBusy;++recordBeginBusy;return;}
        recordCompletions.Drain(records);
        frame.id=records.Begin(input);frame.owner=state->owner;frame.update=kind==ReloadFlowEvent::Update?frame.id:input.update;
        if(frame.id){if(!requestMode)entry=input;++matches[unsigned(kind)];if(server)++serverMatches[unsigned(kind)];}
    }
    std::optional<ReloadFlowRecord> Finish()noexcept {
        if(finished)return {};finished=true;
        if((frame.id||frame.invocation.Record().id)&&lease){ReloadFlowEventEnd out;out.thread=GetCurrentThreadId();out.nowNs=Now();out.tickMs=GetTickCount64();
            evidence={};evidence.readBranch=lease->server&&lease->server->links.firing==frame.firing?2:(frame.firing==lease->snapshot.branches[0].address?0:1);
            if(IsCurrent(*lease,out.nowNs))out.boundary=BoundaryFor(*lease,frame.firing,out.nowNs,&evidence.server,&evidence.client);
            if(!out.boundary)evidence.stage=6;
            if(!IsCurrent(*lease,Now())){if(!evidence.stage)evidence.stage=8;out.boundary.reset();}
            else if(requestMode){RequestBoundaryEvidence ownerEvidence;
                if(!RequestOwnerEvidence(*lease,Now(),&ownerEvidence)){if(!evidence.stage){evidence=ownerEvidence;evidence.stage=7;}out.boundary.reset();}}
            if(!out.boundary){if(lease->server&&lease->server->links.firing==frame.firing)++serverReadMisses;else ++readMisses;}
            if(context){out.contextCopied=Context(context,out.copiedContext);if(!out.contextCopied)++contextMisses;}
            if(snapshot){out.snapshotCopied=Context(snapshot,out.copiedSnapshot);if(!out.snapshotCopied)++contextMisses;}
            out.hold=hold;out.holdRequested=holdRequested;
            const auto independent=requestMode?frame.invocation.Finish(out):std::nullopt;
            if(gunEntry&&out.boundary&&out.boundary->branch==gunEntry->branch){
                bool matched=false;
                if(gunKind==ReloadFlowEvent::Restore){if(independent)matched=ReloadRestoreMatched(*independent);
                    else if(frame.id&&!requestMode)matched=ReloadRestoreMatched(ReloadFlowRecord{frame.id,entry,out,true,SameIdentity(entry.boundary,*out.boundary)});}
                GunExit(*out.boundary,matched);
            }
            if(frame.id){Gate gate(recordGate);if(!gate.held){++recordBusy;++recordEndBusy;
                    recordCompletions.Publish(frame.id,out);if(!requestMode)return {};}
                else {recordCompletions.Drain(records);if(!records.End(frame.id,out)&&!requestMode)return {};}}
            if(requestMode)return independent;
            const bool retained=out.boundary&&SameIdentity(entry.boundary,*out.boundary);
            return ReloadFlowRecord{frame.id,entry,out,true,retained};
        }return {};
    }
    void GunExit(const ReloadFlowBoundary& after,bool matched)noexcept {
        auto& log=GunEvents();auto& tracker=GunNativeTracker();const auto copy=GunCopy(after.branch);const auto b=GunState(*gunEntry),a=GunState(after);
        switch(gunKind){
        case ReloadFlowEvent::Update:tracker.Observe(log,gunWeapon,copy,a);tracker.Freeze(log,gunWeapon,copy,hold.Holding(),a);
            if(gunContext)tracker.Trigger(log,gunWeapon,copy,b,a,gunFire,gunEmit);break;
        case ReloadFlowEvent::Commit:tracker.Commit(log,gunWeapon,copy,b,a);tracker.Observe(log,gunWeapon,copy,a);break;
        case ReloadFlowEvent::Transfer:tracker.Transfer(log,gunWeapon,copy,b,a,GunTransferPath(gunPath));break;
        case ReloadFlowEvent::Restore:tracker.Restore(log,gunWeapon,copy,b,a,matched);break;
        }
    }
    ~Observation(){if(!finished){const auto ended=Finish();if(requestMode&&frame.invocation.Record().id&&!ended)RequestMissing(frame.firing,2,unsigned(frame.invocation.Record().entry.kind),&evidence);}currentFrame=frame.previous;}
};
ReloadFiringObservation RoundState(const ReloadFlowBoundary& b)noexcept {
    ReloadFiringObservation s;s.address=b.firing;s.wrapperOffset=b.wrapperOffset;s.currentState=b.current;s.previousState=b.previous;
    s.nextState=b.next;s.phaseTimer=b.timer;s.loaded=b.loaded;s.reserve=b.reserve;s.flagsA8=b.flagsA8;return s;
}
ReloadHoldIdentity RoundIdentity(const Lease& lease)noexcept {
    ReloadHoldIdentity id;id.owner=lease.snapshot.owner;id.firing[0]=lease.snapshot.branches[0].address;id.firing[1]=lease.snapshot.branches[1].address;
    if(lease.server){id.firing[2]=lease.server->links.firing;id.serverPlayer=lease.server->links.player;id.serverSoldier=lease.server->links.soldier;id.serverItem=lease.server->links.item;}return id;
}
template<class F>bool WithRound(F&& f)noexcept {
    Gate lock(roundGateLock);if(!lock.held){++roundContention;roundCancelled.store(true,std::memory_order_release);return false;}
    if(roundCancelled.load(std::memory_order_acquire)){roundGate.Cancel(ReloadRoundGateFailure::OwnerOrRead);return false;}
    f();if(roundGate.FirstBegin()>0&&!roundTargets[0].load(std::memory_order_acquire)){
        for(unsigned n=0;n<3;++n)roundTargets[n].store(roundGate.Identity().firing[n],std::memory_order_release);}
    if(roundGate.Phase()==ReloadRoundGatePhase::Aborted)roundCancelled.store(true,std::memory_order_release);return true;
}
void RoundMissing(unsigned firing)noexcept {
    if(!firing||std::none_of(roundTargets.begin(),roundTargets.end(),[&](const auto& id){return id.load(std::memory_order_acquire)==firing;}))return;
    WithRound([&]{if(roundGate.FirstBegin()>0){const auto& ids=roundGate.Identity().firing;
        if(std::find(ids.begin(),ids.end(),firing)!=ids.end())roundGate.Cancel(ReloadRoundGateFailure::OwnerOrRead);}});
}
void RoundTransfer(const Observation& observation,const std::optional<ReloadFlowRecord>& record)noexcept {
    if(!observation.frame.id)return;
    WithRound([&]{const auto phase=roundGate.Phase();if(phase==ReloadRoundGatePhase::Waiting||phase==ReloadRoundGatePhase::Disabled||phase==ReloadRoundGatePhase::Released||phase==ReloadRoundGatePhase::Aborted)return;
        if(!record||!observation.lease||!record->exit.boundary){roundGate.Cancel(ReloadRoundGateFailure::OwnerOrRead);return;}
        const auto& e=record->entry;const auto& a=*record->exit.boundary;
        ReloadRoundTransfer transfer{RoundIdentity(*observation.lease),1,record->id,e.nowNs,record->exit.nowNs,e.boundary.branch,
            e.boundary.loaded,e.boundary.reserve,a.loaded,a.reserve,e.transferPath==ReloadTransferPath::OrdinaryState12,record->identityRetained};
        roundGate.Transfer(transfer,e.update);
    });
}
void RequestTransfer(const Observation& observation,const std::optional<ReloadFlowRecord>& record)noexcept {
    if(!RequestTarget(observation.frame.firing))return;
    if(mailboxMode&&mailboxPending){
        // Mailbox: the enclosing Update's receipt carries the transfer; the
        // gather replays it between that Update's Evaluate and Finish.
        if(record&&observation.lease&&record->exit.boundary){const auto& e=record->entry;const auto& a=*record->exit.boundary;
            mailboxPending->transfer=ReloadRoundTransfer{RoundIdentity(*observation.lease),mailboxPendingCycle,record->id,e.nowNs,record->exit.nowNs,e.boundary.branch,
                e.boundary.loaded,e.boundary.reserve,a.loaded,a.reserve,e.transferPath==ReloadTransferPath::OrdinaryState12,record->identityRetained};
            mailboxPending->transferParent=e.update;}
        return;}
    if(!record||!observation.lease||!record->exit.boundary){RequestMissing(observation.frame.firing,3,unsigned(ReloadFlowEvent::Transfer),&observation.evidence);return;}
    WithRequest([&]{const auto& e=record->entry;const auto& a=*record->exit.boundary;
        ReloadRoundTransfer transfer{RoundIdentity(*observation.lease),requestCycle.Cycle(),record->id,e.nowNs,record->exit.nowNs,e.boundary.branch,
            e.boundary.loaded,e.boundary.reserve,a.loaded,a.reserve,e.transferPath==ReloadTransferPath::OrdinaryState12,record->identityRetained};
        if(requestCycle.Transfer(transfer,e.update)){
            ++requestTransferCounts[e.boundary.branch];requestLastTransfer[e.boundary.branch]=record->id;
        }
    },true);
}
bool DeltaCompare(void* address,unsigned expected,unsigned replacement,unsigned& observed)noexcept {
    __try {observed=unsigned(InterlockedCompareExchange(static_cast<volatile LONG*>(address),LONG(replacement),LONG(expected)));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool DeltaRestore(void* address,unsigned value,unsigned& previous)noexcept {
    __try {previous=unsigned(InterlockedExchange(static_cast<volatile LONG*>(address),LONG(value)));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::optional<int> Capacity(unsigned firing,int authored)noexcept {
    float multiplier=0,again=0;int overrideValue=0,overrideAgain=0;
    if(!memory.read||!memory.read(memory.context,firing+0x74,&multiplier,4)||!memory.read(memory.context,firing+0x98,&overrideValue,4)||
       !memory.read(memory.context,firing+0x74,&again,4)||!memory.read(memory.context,firing+0x98,&overrideAgain,4)||
       multiplier!=again||overrideValue!=overrideAgain||!std::isfinite(multiplier)||multiplier<0||multiplier>1024||overrideValue< -1||overrideValue>1000000)return {};
    // The engine truncates: trunc(+0x74 x MagazineCapacity) in float via _ftol2
    // (static RE). A non-integer product is a real capacity, not a bad read.
    if(overrideValue>=0)return overrideValue;const float value=float(authored)*multiplier;
    if(authored<0||!std::isfinite(value)||value<0||value>1000000)return {};return int(value);
}
bool FamilyConfig(ReloadNativeFamily family,NativeMagazineProfileId id,const ReloadObservedConfig& c)noexcept {
    const auto* profile=ResolveMagazineNativeProfile(id);
    return family==ReloadNativeFamily::Xm8Magazine?(profile&&profile->Matches(c)):
        family==ReloadNativeFamily::SpasTube&&IsDiagnosticSpasConfig(c);
}
bool FamilyTiming(ReloadNativeFamily family,NativeMagazineProfileId id,const ReloadObservedConfig& c)noexcept {
    const auto* profile=ResolveMagazineNativeProfile(id);
    return family==ReloadNativeFamily::Xm8Magazine?(profile&&profile->ReadTiming(memory,c)):
        family==ReloadNativeFamily::SpasTube&&ReadReloadRoundTiming(memory,c);
}
bool RequestConfig(const ReloadObservedConfig& c)noexcept {
    return requestCycle.MatchesSelectedConfig(c);
}
bool RequestTiming(const ReloadObservedConfig& c)noexcept {
    return requestCycle.IsMagazine()?requestCycle.MagazineProfile().ReadTiming(memory,c):ReadReloadRoundTiming(memory,c);
}
bool PrepareHoldOnce(const Observation& observation,unsigned firing,unsigned caller,unsigned context,ReloadHoldInput& input,unsigned& deltaBits,
    ReloadCohortRejection& rejected)noexcept {
    rejected={};const auto reject=[&](unsigned reason){rejected.stage=reason;++holdPrepareRejected[reason];return false;};
    if(!(requestMode?observation.frame.invocation.Record().id:observation.frame.id)||!observation.lease||observation.frame.depth!=1||!holdCodeVerified||
       caller!=base+binding.code[1].rva+0x2f||!context||(context&3))return reject(0);
    ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
    if(context<low||std::uint64_t(context)+0x30>high)return reject(1);
    const auto& lease=*observation.lease;
    if(!lease.server||!serverBinding||!IsCurrent(lease,Now())||!RequestConfig(lease.snapshot.config))return reject(2);
    input.identity.owner=lease.snapshot.owner;
    input.identity.firing={lease.snapshot.branches[0].address,lease.snapshot.branches[1].address,lease.server->links.firing};
    input.identity.serverPlayer=lease.server->links.player;input.identity.serverSoldier=lease.server->links.soldier;input.identity.serverItem=lease.server->links.item;
    input.config=lease.snapshot.config;input.branch=3;
    for(unsigned n=0;n<3;++n)if(input.identity.firing[n]==firing)input.branch=n;
    rejected.callerBranch=input.branch;
    for(unsigned n=0;n<3;++n){
        ReloadServerBoundaryDiagnostic diagnostic;ReloadFlowBoundaryDiagnostic clientDiagnostic;
        const auto boundary=BoundaryFor(lease,input.identity.firing[n],Now(),&diagnostic,&clientDiagnostic);
        const auto capacity=Capacity(input.identity.firing[n],input.config.baseCapacity);
        if(!boundary||!capacity){
            rejected.readBranch=n;rejected.boundaryOkay=bool(boundary);rejected.capacityOkay=bool(capacity);
            rejected.server=diagnostic;rejected.client=clientDiagnostic;
            if(input.branch<3){if(!boundary)++holdReadRejected[input.branch][n][0];if(!capacity)++holdReadRejected[input.branch][n][1];}
            const auto index=holdReadRejectionCount.fetch_add(1,std::memory_order_relaxed);
            if(index<holdReadRejectionSamples.size())holdReadRejectionSamples[index]={Now(),input.branch,n,input.identity.firing[n],bool(boundary),bool(capacity),capacity.value_or(-1),diagnostic,clientDiagnostic};
            return reject(3);
        }
        auto& state=input.branches[n];state.address=boundary->firing;state.wrapperOffset=boundary->wrapperOffset;
        state.currentState=boundary->current;state.previousState=boundary->previous;state.nextState=boundary->next;state.phaseTimer=boundary->timer;
        state.loaded=boundary->loaded;state.reserve=boundary->reserve;state.flagsA8=boundary->flagsA8;input.capacities[n]=*capacity;
    }
    input.contextObservedNs=Now();std::array<std::byte,0x30> raw{};if(!Context(context,raw))return reject(4);
    const auto decoded=DecodeReloadUpdateContext(raw);if(!decoded)return reject(4);
    input.context=*decoded;deltaBits=Word<unsigned>(raw.data(),0x18);input.nowNs=Now();
    input.leaseDeadlineNs=std::min(lease.deadline,lease.snapshot.observedNs+200000000ll);
    input.verified=input.branch<3&&IsCurrent(lease,input.nowNs)&&input.nowNs<input.leaseDeadlineNs;return input.verified?true:reject(5);
}
bool PrepareHold(Observation& observation,unsigned firing,unsigned caller,unsigned context,ReloadHoldInput& input,unsigned& deltaBits)noexcept {
    ReloadCohortRejection rejected;
    if(!requestMode)return PrepareHoldOnce(observation,firing,caller,context,input,deltaBits,rejected);
    struct Prepared {ReloadHoldInput input{};unsigned delta=0;};
    ReloadCohortRetryAttempt attempt;
    const auto result=ReadReloadCohortCoherent([&](std::int64_t at,ReloadCohortRejection& failure)->std::optional<Prepared>{
        failure={};Prepared fresh;
        // The captured lease is never renewed or replaced between attempts.
        if(!observation.lease||!IsCurrent(*observation.lease,at)){failure.stage=5;return {};}
        if(!PrepareHoldOnce(observation,firing,caller,context,fresh.input,fresh.delta,failure))return {};
        return fresh;
    },Now,Now(),rejected,attempt);
    if(attempt.attempted){const auto branch=attempt.first.callerBranch;++cohortRetryAttempts[branch];
        if(attempt.recovered)++cohortRetryRecovered[branch];else ++cohortRetryFailed[branch];
        const auto slot=cohortRetrySampleCount.fetch_add(1);
        if(slot<cohortRetrySamples.size())cohortRetrySamples[slot]={firing,observation.lease?observation.lease->snapshot.sequence:0,
            observation.lease?std::min(observation.lease->deadline,observation.lease->snapshot.observedNs+200000000ll):0,attempt};}
    if(!result){observation.evidence={9,rejected.readBranch,rejected.client,rejected.server};return false;}
    input=result->input;deltaBits=result->delta;return true;
}
// Native helper has no callbacks or external calls. SEH containment does not
// fabricate success: both exact helper bytes and the original Update result
// must be observed. Original Update still executes exactly once on all paths.
bool CallNativeAbort(void* self)noexcept {
    __try {nativeAbort(self,1u);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AbortConfig(const ReloadObservedConfig& c)noexcept {
    unsigned logic=~0u,again=~0u;
    return nativeAbort&&RequestConfig(c)&&RequestTiming(c)&&
        memory.read(memory.context,c.primaryFire+0x2c,&logic,4)&&logic==0&&
        memory.read(memory.context,c.primaryFire+0x2c,&again,4)&&again==0&&
        ValidateReloadHoldCodeLive(memory,abortCode,base,binding.state.imageSize);
}
#ifdef FVR_BC2_PREHOLD_CLEANUP
std::optional<PreholdCleanupEvidence> PreholdEvidenceConfigured(const Lease& lease,std::uint64_t cycle,bool excluded,bool configMatches)noexcept {
    if(!configMatches||!IsCurrent(lease,Now())||!RequestOwnerEvidence(lease,Now()))return {};
    PreholdCleanupEvidence e;e.revision=lease.revision;e.operation=cycle;e.sequence=lease.snapshot.sequence;
    e.source.identity=RoundIdentity(lease);e.source.config=lease.snapshot.config;
    e.configVerified=true;e.callbacksExcluded=excluded;e.source.verified=true;
    for(unsigned n=0;n<3;++n){const auto b=BoundaryFor(lease,e.source.identity.firing[n],Now());
        const auto cap=Capacity(e.source.identity.firing[n],e.source.config.baseCapacity);
        if(!b||!cap)return {};e.source.branches[n]=RoundState(*b);e.source.capacities[n]=*cap;}
    for(unsigned n=0;n<3;++n){const auto b=BoundaryFor(lease,e.source.identity.firing[n],Now());
        const auto cap=Capacity(e.source.identity.firing[n],e.source.config.baseCapacity);const auto& old=e.source.branches[n];
        if(!b||!cap||*cap!=e.source.capacities[n]||b->current!=old.currentState||b->next!=old.nextState||
           b->loaded!=old.loaded||b->reserve!=old.reserve||b->timer!=old.phaseTimer||b->flagsA8!=old.flagsA8)return {};}
    e.observedNs=Now();e.deadlineNs=std::min(lease.deadline,e.observedNs+PreholdCleanupLedger::ObservationNs);
    e.source.nowNs=e.observedNs;e.source.leaseDeadlineNs=e.deadlineNs;e.coherent=IsCurrent(lease,Now())&&RequestOwnerEvidence(lease,Now());return e;
}
// Existing authority paths call this only under policy exclusion.
std::optional<PreholdCleanupEvidence> PreholdEvidence(const Lease& lease,std::uint64_t cycle,bool excluded)noexcept {
    return PreholdEvidenceConfigured(lease,cycle,excluded,RequestConfig(lease.snapshot.config));
}
// No interactions are renewed by cleanup. A new operation must not erase a
// cancelled Arming operation while its ORIGINAL cleanup window remains open.
// Called only inside policy exclusion plus quiet native callback exclusion.
bool PreholdAllowsNewOperation()noexcept {
    return !preholdMonitorStarted||!requestCycle.CancelledFromArming()||
        Now()>=preholdMonitor.OperationDeadlineNs();
}
// Called under policy lock and quiet callback exclusion, or final hook drain.
// No new native reads: copy only original and last EXISTING Update evidence.
std::optional<PreholdOperationRecord> SnapshotPreholdOperation()noexcept {
    if(!requestCycle.Cycle())return {};
    PreholdOperationRecord r;r.epoch=familyEpoch;r.operation=requestCycle.Cycle();
    r.family=requestCycle.Family();r.profile=requestCycle.MagazineProfileId();r.policyPhase=requestCycle.Phase();r.policyFailure=unsigned(requestCycle.Failure());
    r.cancellation=requestCycle.CancellationOrigin();
    if(preholdMonitorStarted&&preholdMonitor.Original().operation==r.operation)r.original=preholdMonitor.Original();
    if(preholdLastCoherent&&preholdLastCoherent->operation==r.operation)r.lastCoherent=preholdLastCoherent;
    r.cleanupDeadlineNs=preholdMonitor.OperationDeadlineNs();r.ledgerPhase=unsigned(preholdMonitor.Phase());
    r.entered=preholdMonitor.EnteredMask();r.called=preholdMonitorCalled.load();r.exact=preholdMonitorExact.load();r.idle=preholdMonitorIdle.load();
    r.failed=preholdMonitorFailed.load();r.firstFailureNs=preholdFirstFailureNs.load();r.firstFailureReason=preholdFirstFailureReason.load();
    r.authoritativeIdleNs=preholdAuthoritativeIdleNs.load();r.branches=preholdMonitorBranches;return r;
}
void ResetPreholdOperation()noexcept {
    preholdMonitor=PreholdCleanupLedger{};preholdMonitorBranches={};preholdEntryObservation.reset();preholdLastCoherent.reset();
    preholdMonitorStarted=false;preholdMonitorCancelled=false;preholdMonitorFailed=false;
    preholdMonitorCalled=0;preholdMonitorExact=0;preholdMonitorIdle=0;preholdAuthoritativeIdleNs=0;
    preholdFirstFailureNs=0;preholdFirstFailureReason=0;preholdFirstFailureBranch=3;
    preholdFirstServerDeniedNs=0;preholdFirstServerDeniedReason=0;
}
struct PreholdStartPreread {Lease lease;PreholdObservationKey key;PreholdCleanupEvidence evidence;};
PreholdObservationKey PreholdStartKey(const Lease& lease)noexcept {
 return {requestCycle.Identity(),requestCycle.Cycle(),lease.revision,requestCancelEpoch.load(),callbackRevision.load(),
     unsigned(requestCycle.Family()),unsigned(requestCycle.MagazineProfileId()),unsigned(requestCycle.Phase()),0};
}
std::optional<PreholdStartPreread> ReadPreholdStartPreread(const ReloadCycleControl& control)noexcept {
 std::optional<Lease> lease;{OwnerCopyGate gate;if(gate.held&&IsCurrent(published,Now())&&published.server)lease=published;}
 if(!lease||RoundIdentity(*lease)!=control.identity)return {};
 std::optional<PreholdObservationKey> key;
 WithRequest([&]{if(IsCurrent(*lease,Now())&&RequestConfig(lease->snapshot.config))key=PreholdStartKey(*lease);},false,nullptr,13);
 if(!key)return {};
 const auto family=static_cast<ReloadNativeFamily>(key->family);const auto profile=static_cast<NativeMagazineProfileId>(key->profile);
 if(!FamilyTiming(family,profile,lease->snapshot.config))return {};
 const auto evidence=PreholdEvidenceConfigured(*lease,control.cycle,false,FamilyConfig(family,profile,lease->snapshot.config));
 if(!evidence||callbackRevision.load()!=key->callbackRevision||requestCancelEpoch.load()!=key->cancelEpoch||!IsCurrent(*lease,Now()))return {};
 return PreholdStartPreread{*lease,*key,*evidence};
}
bool PreholdStartPublishedCurrent(const PreholdStartPreread& captured)noexcept {
 OwnerCopyGate gate;return gate.held&&IsCurrent(published,Now())&&RoundIdentity(published)==captured.evidence.source.identity&&
     PreholdStartPublishedMatches(captured.evidence,published.snapshot);
}
bool PreparePreholdMonitor(const PreholdCleanupEvidence& evidence)noexcept {
 ResetPreholdOperation();auto e=evidence;e.callbacksExcluded=true;
 preholdMonitorStarted=preholdMonitor.Prepare(e,Now());return preholdMonitorStarted.load();
}
// Optional shared cleanup, not a synthetic cancellation experiment. Entry
// observations never create a held lease or authorize ammunition credit.
void ObservePreholdMonitor(const ReloadHoldInput& input,const std::optional<PreholdCleanupEvidence>& e)noexcept {
    if(!preholdMonitorStarted||input.branch>=3)return;
    if(e&&e->coherent&&e->configVerified&&e->source.verified)preholdLastCoherent=e; // telemetry only, no deadline extension
    if(e&&preholdMonitorCancelled){
        for(unsigned n=0;n<3;++n){
            const bool reload=(e->source.branches[n].currentState>=10&&e->source.branches[n].currentState<=12)||e->source.branches[n].nextState==10;
            // A previously cleaned branch entering again is not an invitation
            // to repeat helper dispatch. Fence this experiment and retain it.
            const bool sameOperation=preholdMonitor.ObserveCancelledEntry(*e,n,Now());
            if(n==2&&sameOperation&&reload&&preholdAuthoritativeIdleNs.load()){
                preholdAuthoritativeIdleNs=0;
                if(preholdMonitorCalled.load()&3u)PreholdMonitorFailure(6,n,Now());
            }
            if(sameOperation&&reload&&(preholdMonitorCalled.load()&(1u<<n))&&(preholdMonitorIdle.load()&(1u<<n)))PreholdMonitorFailure(1,n,Now());
        }return;
    }
    if(e){
        if(preholdMonitor.ObserveEntry(*e,input.branch,Now()))
            preholdEntryObservation=ReloadPreholdEntryObservation{e->source.identity,e->operation,e->sequence,e->observedNs,
                std::min(e->deadlineNs,preholdMonitor.OperationDeadlineNs()),preholdMonitor.EnteredMask(),requestCycle.Phase()};
        if(requestCycle.CancelledFromArming())
            preholdMonitorCancelled=preholdMonitor.Cancel(requestCycle.Cycle());
    }
}
// Observation only: policy reads/copy and commit are short. Expensive native
// cohort reads never hold requestGate. Supersession withholds this observation.
void ObservePreholdOutsidePolicy(const Lease& lease,const ReloadHoldInput& input)noexcept {
    const auto key=[&]{return PreholdObservationKey{requestCycle.Identity(),requestCycle.Cycle(),lease.revision,
        requestCancelEpoch.load(),callbackRevision.load(),unsigned(requestCycle.Family()),unsigned(requestCycle.MagazineProfileId()),
        unsigned(requestCycle.Phase()),preholdMonitor.OperationDeadlineNs()};};
    std::optional<PreholdObservationKey> captured;
    WithRequest([&]{if(preholdMonitorStarted&&input.branch<3&&IsCurrent(lease,Now())&&
        requestCycle.Identity()==RoundIdentity(lease)&&RequestConfig(lease.snapshot.config))captured=key();},false,nullptr,12);
    if(!captured)return;
    const bool config=FamilyConfig(static_cast<ReloadNativeFamily>(captured->family),
        static_cast<NativeMagazineProfileId>(captured->profile),lease.snapshot.config);
    const auto evidence=PreholdEvidenceConfigured(lease,captured->cycle,false,config);
    if(!evidence)return;
    WithRequest([&]{if(!IsCurrent(lease,Now())||!RequestConfig(lease.snapshot.config)||
        !PreholdObservationMayCommit(*captured,key(),*evidence,lease.snapshot.config,Now()))return;
        ObservePreholdMonitor(input,evidence);},false,nullptr,2);
}
bool TryPreholdMonitor(const Lease& lease,const ReloadHoldInput& input,void* self,void* context,const std::array<std::byte,0x30>& expectedContext,std::uint64_t callbackEpoch,std::uint64_t invocation)noexcept {
    if(preholdMonitorFailed||!preholdMonitorCancelled||input.branch>=3||(preholdMonitorCalled&(1u<<input.branch)))return false;
    const auto& c=input.context;
    if(!std::isfinite(c.deltaSeconds)||c.deltaSeconds<=0||c.deltaSeconds>.05f||c.reloadTimeMultiplier!=1||
       c.inputFlags||c.fireRequested||c.orderRequested||c.reloadRequested)return false;
    std::optional<PreholdCleanupEvidence> e;
    std::optional<PreholdCleanupCandidate> candidate;
    const bool prepared=WithRequest([&]{
        if(!requestCycle.CancelledFromArming()||!preholdMonitor.CleanupObservationOpen(Now()))return;
        if(!AbortConfig(input.config))return;
        const auto cycle=requestCycle.Cycle();
        e=PreholdEvidence(lease,cycle,false);
        if(e)candidate=preholdMonitor.ObserveCleanup(*e,Now());
    },true,nullptr,8);
    if(!prepared||!e||!candidate){if(input.branch==2)PreholdServerDenied(!prepared?1:!e?2:3,Now());return false;}
    const auto& server=e->source.branches[2];
    const bool serverIdle=(server.currentState==1||server.currentState==2)&&(server.nextState==1||server.nextState==2);
    const auto serverIdleNs=preholdAuthoritativeIdleNs.load(std::memory_order_acquire);
    const auto orderNow=Now();
    const bool authoritativeIdle=serverIdleNs>0&&orderNow>=serverIdleNs&&
        orderNow-serverIdleNs<=ReloadHoldProbe::ContextFreshNs;
    if(!PreholdServerFirstOrder(input.branch,preholdMonitorCalled.load(),preholdMonitorExact.load(),preholdMonitorIdle.load(),serverIdle,authoritativeIdle))return false;
    const auto& b=e->source.branches[input.branch];
    if(b.address!=unsigned(self)||((b.currentState<10||b.currentState>12)&&b.nextState!=10))return false;
    std::array<std::byte,0xac> before{},after{};std::array<std::byte,0x30> ctx{},ctxAfter{};
    if(!Context(unsigned(self),before)||!Context(unsigned(context),ctx)||ctx!=expectedContext||
       Word<unsigned>(before.data(),0x3c)!=b.currentState||Word<unsigned>(before.data(),0x44)!=b.nextState||
       Word<int>(before.data(),0x7c)!=b.loaded||Word<int>(before.data(),0x80)!=b.reserve||
       Word<float>(before.data(),0x50)!=b.phaseTimer||Word<unsigned char>(before.data(),0xa8)!=b.flagsA8||
       ownerRevision.load(std::memory_order_acquire)!=candidate->revision||!IsCurrent(lease,Now())||
       !RequestOwnerEvidence(lease,Now())||Now()>=candidate->deadlineNs)return false;
    const auto adjacent=BoundaryFor(lease,unsigned(self),Now());
    if(!adjacent||adjacent->current!=b.currentState||adjacent->next!=b.nextState||adjacent->timer!=b.phaseTimer||
       adjacent->loaded!=b.loaded||adjacent->reserve!=b.reserve||adjacent->flagsA8!=b.flagsA8||
       (input.branch!=2&&(active.load(std::memory_order_acquire)!=1||callbackRevision.load(std::memory_order_acquire)!=callbackEpoch)))return false;
    if(preholdMonitorCalled.fetch_or(1u<<input.branch)&(1u<<input.branch))return false;
    auto& branchReport=preholdMonitorBranches[input.branch];branchReport.called=true;branchReport.reason=1;
    branchReport.invocation=invocation;branchReport.before=b;
    const bool returned=CallNativeAbort(self);
    const bool exact=returned&&Context(unsigned(self),after)&&ReloadAbortHelperPostcondition(before,after)&&
        Context(unsigned(context),ctxAfter)&&ctx==ctxAfter;
    branchReport.helperExact=exact;branchReport.reason=exact?2:3;
    if(Context(unsigned(self),after)){
        auto& a=branchReport.helperAfter;a.address=unsigned(self);a.wrapperOffset=b.wrapperOffset;
        a.currentState=Word<unsigned>(after.data(),0x3c);a.nextState=Word<unsigned>(after.data(),0x44);
        a.phaseTimer=Word<float>(after.data(),0x50);a.loaded=Word<int>(after.data(),0x7c);a.reserve=Word<int>(after.data(),0x80);
        a.flagsA8=Word<unsigned char>(after.data(),0xa8);
    }
    if(exact)preholdMonitorExact.fetch_or(1u<<input.branch);else PreholdMonitorFailure(2,input.branch,Now());
    return exact;
}
#endif
ReloadAbortDecision TryAbort(Observation& observation,const ReloadHoldInput& input,
    void* self,void* context,AbortRecord& report)noexcept {
    ReloadAbortDecision decision;
    if(!requestMode||!observation.lease)return decision;
    bool wanted=false;WithRequest([&]{wanted=abortCleanup.Active();},false,nullptr,3);if(!wanted)return decision;
    const auto& lease=*observation.lease;
    const bool config=AbortConfig(input.config);
    WithRequest([&]{decision=abortCleanup.Claim(input,lease.revision,requestCycle.Cycle(),
        requestCycle.Phase()==ReloadRequestCyclePhase::Cancelled,observation.frame.invocation.Record().id,config,Now());},true);
    if(!decision.call)return decision;
    report.dispatchEpoch=decision.dispatchEpoch;report.cycle=decision.cycle;report.invocation=decision.invocation;report.revision=lease.revision;
    report.begin=Now();report.deadline=decision.deadlineNs;report.branch=decision.branch;report.before=decision.before;
    std::array<std::byte,0xac> before{},after{};std::array<std::byte,0x30> originalContext{},afterContext{};
    const auto fresh=BoundaryFor(lease,unsigned(self),Now());
    bool allowed=false;
    const bool policy=WithRequest([&]{allowed=abortCleanup.Allows(decision,ownerRevision.load(std::memory_order_acquire),
        requestCycle.Cycle(),requestCycle.Phase()==ReloadRequestCyclePhase::Cancelled,Now());},true);
    if(policy&&allowed&&fresh&&fresh->current==decision.before.currentState&&fresh->next==decision.before.nextState&&
       fresh->timer==decision.before.phaseTimer&&fresh->loaded==decision.before.loaded&&fresh->reserve==decision.before.reserve&&
       fresh->flagsA8==decision.before.flagsA8&&IsCurrent(lease,Now())&&RequestOwnerEvidence(lease,Now())&&
       Context(unsigned(self),before)&&Context(unsigned(context),originalContext)&&
       originalContext==observation.entry.copiedContext&&Word<unsigned>(before.data(),0x3c)==decision.before.currentState&&
       Word<unsigned>(before.data(),0x44)==decision.before.nextState&&Word<float>(before.data(),0x50)==decision.before.phaseTimer&&
       Word<int>(before.data(),0x7c)==decision.before.loaded&&Word<int>(before.data(),0x80)==decision.before.reserve&&
       Word<unsigned char>(before.data(),0xa8)==decision.before.flagsA8&&
       IsCurrent(lease,Now())&&Now()<decision.deadlineNs){
        report.called=true;++abortCalls;
        const bool returned=CallNativeAbort(self);
        report.contextUnchanged=Context(unsigned(context),afterContext)&&originalContext==afterContext;
        report.helperExact=returned&&Context(unsigned(self),after)&&ReloadAbortHelperPostcondition(before,after)&&report.contextUnchanged;
    }
    return decision;
}
void FinishAbort(const ReloadAbortDecision& decision,AbortRecord& report,const std::optional<ReloadFlowRecord>& completed)noexcept {
    if(!decision.call)return;
    report.end=Now();report.ownerRetained=completed&&completed->identityRetained&&bool(completed->exit.boundary);
    if(report.ownerRetained)report.after=RoundState(*completed->exit.boundary);
    WithRequest([&]{
        report.ownerRetained=report.ownerRetained&&ownerRevision.load(std::memory_order_acquire)==decision.ownerRevision&&
            requestCycle.Cycle()==decision.cycle&&requestCycle.Phase()==ReloadRequestCyclePhase::Cancelled;
        report.completed=abortCleanup.Finish(decision,report.after,report.ownerRetained,report.helperExact,report.end);
        if(!abortCleanup.Active())stopArmedAtNs.store(0,std::memory_order_release); // stop landed (or ended): release held Fire
    },true);
    if(report.completed)++abortCompleted;
    const auto slot=abortRecordCount.fetch_add(1);if(slot<abortRecords.size())abortRecords[slot]=report;
}
bool EmptyByteCompare(void* address,std::uint8_t expected,std::uint8_t replacement,std::uint8_t& observed)noexcept {
    __try {observed=std::uint8_t(_InterlockedCompareExchange8(static_cast<volatile char*>(address),char(replacement),char(expected)));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void StoreMagazineInteractionDiagnostic(const MagazineInteractionDiagnostic& value)noexcept {
    Gate gate(interactionDiagnosticGate);if(!gate.held){++interactionDiagnosticLockDrops;return;}
    latestInteractionDiagnostic=value;
}
void RecordMagazineEmptyDiagnostic(MagazineEmptyDiagnosticRecord row)noexcept {
    {Gate source(interactionDiagnosticGate);
     if(source.held&&MagazineInteractionDiagnosticFresh(latestInteractionDiagnostic,row.sample.identity.owner,
        row.sample.sourceSequence,row.nowNs)){row.interaction=latestInteractionDiagnostic;row.hasInteraction=true;}}
    Gate gate(emptyDiagnosticGate);if(!gate.held){++emptyDiagnosticLockDrops;return;}
    emptyDiagnosticJournal.Observe(row);
}
struct EmptyLeak {std::int64_t nowNs=0;unsigned branch=3,stage=0,mask=0,phase=0,input=0,flags=0;};
std::array<EmptyLeak,32> emptyLeaks{};std::atomic<unsigned> emptyLeakCount=0;
void __fastcall Step(void* self,void*,void* context,float delta){
    if(physicalWeaponRuntime::Managed(unsigned(self))){
        std::array<std::byte,4> stateBytes{};unsigned before=~0u;
        if(Context(unsigned(self)+0x3c,stateBytes))std::memcpy(&before,stateBytes.data(),sizeof before);
        originalStep(self,context,delta);
        physicalWeaponRuntime::AfterStep(unsigned(self),before);
        return;
    }
    Active activeScope;const auto firing=unsigned(self),caller=unsigned(_ReturnAddress());
    MagazineEmptyStep sample;MagazineEmptyByteOverride patch;bool requested=false,magazineControl=false;unsigned failureMask=0,leakPhase=~0u,leakInput=0;
    MagazineEmptyDiagnosticRecord diagnostic;diagnostic.sample.before.address=firing;diagnostic.sample.branch=3;diagnostic.stepDeltaBits=std::bit_cast<unsigned>(delta);
    bool diagnose=ManualEmptyDiagnosticAdmitted(combinedFamilies,requestMode,RequestTarget(firing),false);

    const auto cancelEpoch=requestCancelEpoch.load(std::memory_order_acquire);
    std::optional<Lease> lease;std::array<std::byte,0x30> bytes{};
    // Current invocation stack and exact whole-function/call-link proof. Other
    // Step callers and every non-input-processing state pass through untouched.
    const auto parent=currentFrame;
    ULONG_PTR stackLow=0,stackHigh=0;GetCurrentThreadStackLimits(&stackLow,&stackHigh);
    const ManualEmptyDiagnosticContextBoundary boundary{combinedFamilies,requestMode,holdCodeVerified,
       activeScope.requestEntryAllowed,bool(parent),parent&&parent->invocation.Record().entry.kind==ReloadFlowEvent::Update,
       parent?parent->depth:0,firing,parent?parent->firing:0,unsigned(context),
       parent?parent->invocation.Record().entry.context:0,caller,base+binding.code[0].rva+0x168,
       parent?parent->invocation.Record().id:0,stackLow,stackHigh};
    // Shared read boundary is unchanged; delta affects control, not observation.
    if(ManualEmptyDiagnosticContextReadAllowed(boundary)&&Context(unsigned(context),bytes)){
        diagnostic.stage=MagazineEmptyDiagnosticStage::OwnerOrContext;
        lease=Owner(firing,Now());
        // Selected local callbacks are observable before a manual cycle ever
        // registers RequestTarget. This grants no native control permission.
        diagnose=ManualEmptyDiagnosticAdmitted(combinedFamilies,requestMode,diagnose,
            lease&&lease->snapshot.owner==parent->owner&&lease->revision==parent->ownerRevision);
        const auto decoded=DecodeReloadUpdateContext(bytes);
        diagnostic.boundaryReasonKnown=true;diagnostic.boundaryReason=unsigned(ManualEmptyBoundaryControlReason(delta,decoded));
        diagnostic.rawContextKnown=true;diagnostic.contextDeltaBits=Word<unsigned>(bytes.data(),0x18);
        diagnostic.contextMultiplierBits=Word<unsigned>(bytes.data(),0x20);diagnostic.contextInputFlags=Word<unsigned>(bytes.data(),0x2c);
        for(unsigned n=0;n<5;++n)diagnostic.rawContextFlags[n]=std::to_integer<unsigned>(bytes[0x24+n]);
        if(decoded){diagnostic.context=*decoded;diagnostic.hasContext=true;}
        if(lease){diagnostic.sample.identity=RoundIdentity(*lease);diagnostic.sample.ownerRevision=lease->revision;
            diagnostic.sample.branch=firing==lease->snapshot.branches[0].address?0:firing==lease->snapshot.branches[1].address?1:2;
            diagnostic.sample.sourceSequence=lease->snapshot.sequence;diagnostic.sample.observedNs=lease->snapshot.observedNs;
            diagnostic.sample.deadlineNs=MagazineEmptyEvidenceDeadline(lease->snapshot.observedNs,lease->deadline);}

        if(lease&&lease->snapshot.owner==parent->owner&&lease->revision==parent->ownerRevision&&
           RequestOwnerEvidence(*lease,Now())){
            diagnostic.stage=MagazineEmptyDiagnosticStage::FiringRead;
            const auto before=BoundaryFor(*lease,firing,Now());
            if(before){
                diagnostic.hasBefore=true;diagnostic.sample.before=RoundState(*before);diagnostic.sample.branch=before->branch;
                diagnostic.stage=MagazineEmptyDiagnosticStage::Policy;
                sample.identity=RoundIdentity(*lease);sample.ownerRevision=lease->revision;
                sample.sourceSequence=lease->snapshot.sequence;sample.observedNs=lease->snapshot.observedNs;
                sample.deadlineNs=MagazineEmptyEvidenceDeadline(sample.observedNs,lease->deadline);
                sample.branch=before->branch;sample.before=RoundState(*before);
                // Diagnostics may observe a denied delta/context. Control still
                // requires every original delta/decode predicate unchanged.
                if(std::isfinite(delta)&&delta>0&&delta<=.05f&&decoded&&delta<=decoded->deltaSeconds){
                const bool checked=WithRequest([&]{
                    if(!IsCurrent(*lease,Now()))return;
                    const auto family=requestCycle.Family();diagnostic.family=unsigned(family);diagnostic.familyKnown=true;magazineControl=requestCycle.IsMagazine();
                    sample.profile=requestCycle.MagazineProfileId();diagnostic.phase=unsigned(requestCycle.Phase());
                    diagnostic.stage=MagazineEmptyDiagnosticStage::Eligibility;
                    requested=ManualEmptyFamilyStepEligible(sample,family,lease->snapshot.config,requestCycle.Phase(),*decoded,Now());
                    if(!requested){failureMask=ManualEmptyStepFailures(sample,requestCycle.Phase(),*decoded,Now());
                        if(!ManualEmptyFamilyConfig(family,sample.profile,lease->snapshot.config))failureMask|=8192;
                        leakPhase=unsigned(requestCycle.Phase());leakInput=decoded->inputFlags;}
                    if(requested){diagnostic.stage=MagazineEmptyDiagnosticStage::ProfileOrTiming;
                        requested=requestCycle.MatchesSelectedConfig(lease->snapshot.config)&&ReadManualEmptyFamilyTiming(memory,family,sample.profile,lease->snapshot.config);}
                    diagnostic.sample=sample;
                },true,nullptr,9);
                requested=requested&&checked;if(!checked)diagnostic.stage=MagazineEmptyDiagnosticStage::PolicyLock;
                }
            }else if(diagnostic.boundaryReason==unsigned(ManualEmptyBoundaryDiagnosticReason::None))
                diagnostic.boundaryReason=unsigned(ManualEmptyBoundaryDiagnosticReason::BeforeReadFailed);
        }else if(lease){diagnostic.stage=MagazineEmptyDiagnosticStage::OwnerEvidence;
            if(diagnostic.boundaryReason==unsigned(ManualEmptyBoundaryDiagnosticReason::None))
                diagnostic.boundaryReason=unsigned(ManualEmptyBoundaryDiagnosticReason::OwnerEvidenceFailed);}
    }
    // Fail-stuck: a granted stock fallback lets the Reload press through.
    if(requested&&Now()<stockTokenUntilNs.load(std::memory_order_acquire)&&diagnostic.hasContext&&
       (diagnostic.context.reloadRequested||(diagnostic.context.inputFlags&4u))){requested=false;
        if(!closedEmptyGate)RecordTokenPass({Now(),-1,sample.branch,unsigned(diagnostic.stage),diagnostic.context.inputFlags,3});}
    if(requested){++emptyRequested[sample.branch];diagnostic.stage=MagazineEmptyDiagnosticStage::AdjacentRecheck;
        // Last owner check is adjacent to the only one-byte write. No policy
        // lock or callback-exclusion lock spans the original native function.
        const auto current=lease?BoundaryFor(*lease,firing,Now()):std::nullopt;
        requested=lease&&current&&current->current==2&&current->next==2&&
            current->loaded==sample.before.loaded&&current->reserve==sample.before.reserve&&
            current->timer==sample.before.phaseTimer&&current->flagsA8==sample.before.flagsA8&&
            IsCurrent(*lease,Now())&&RequestOwnerEvidence(*lease,Now())&&Now()<sample.deadlineNs&&
            cancelEpoch==requestCancelEpoch.load(std::memory_order_acquire);
    }
    // Fail-closed gate: block whatever the lease gate did not, on an
    // identified player copy, unless our token is pending. No receipt.
    bool closedOnly=false;unsigned closedBranch=3,closedMask=0;ClosedEmptyStep closed;
    if(closedEmptyGate&&!requested){
        closed.firing=firing;closed.callerMatches=caller==base+binding.code[0].rva+0x168;
        const auto id=ReadClosedIdentity();if(!id)++closedIdentityMisses;
        if(id&&enabled.load(std::memory_order_acquire)&&std::find(id->copies.begin(),id->copies.end(),firing)!=id->copies.end()){
            // F1: no owner-revision or age requirement; the copy itself must
            // still be the published weapon's firing object.
            closed.copies=id->copies;closed.supported=id->supported;
            std::array<std::byte,16> head{};
            closed.structural=Context(firing,head)&&Word<unsigned>(head.data(),0)==base+binding.state.firingVtableRva&&
                Word<unsigned>(head.data(),8)==id->firingData&&Word<unsigned>(head.data(),12)==id->ammoAddress;
        }
        closedMask=ClosedEmptyStepPass(closed);
        if(!(closedMask&1u)){
            for(unsigned n=0;n<3;++n)if(closed.copies[n]==firing)closedBranch=n;
            std::array<std::byte,0x48> state{};
            if(Context(firing+0x3c,state)){closed.stateKnown=true;closed.current=Word<unsigned>(state.data(),0);
                closed.next=Word<unsigned>(state.data(),8);closed.loaded=Word<std::int32_t>(state.data(),0x40);}
            const bool contextKnown=diagnostic.rawContextKnown||Context(unsigned(context),bytes);
            closed.phase=ReloadRequestCyclePhase(requestPhaseMirror.load(std::memory_order_acquire));
            closed.cancelPending=requestCancelEpoch.load(std::memory_order_acquire)!=requestAppliedCancelMirror.load(std::memory_order_acquire);
            const auto decoded=DecodeReloadUpdateContext(bytes);
            closed.reloadBit=(decoded&&decoded->reloadRequested)||(Word<unsigned>(bytes.data(),0x2c)&4u);
            if(const auto since=requestArmingSinceNs.load(std::memory_order_acquire);since>0){const auto now=Now();if(now>=since)closed.tokenAgeNs=now-since;}
            closed.stockToken=Now()<stockTokenUntilNs.load(std::memory_order_acquire);
            closedMask=ClosedEmptyStepPass(closed);
            if(!closed.stateKnown)++closedStateMisses;
            if(closedMask==0&&!contextKnown){closedMask=32;++closedContextMisses;}
            if(closedMask==0){requested=true;closedOnly=true;}
            else if(closed.stateKnown&&closed.current==2&&closed.loaded==0){
                if(closedMask&16u){++closedTokenPasses;if(closed.phase==ReloadRequestCyclePhase::Arming||(closed.stockToken&&closed.reloadBit))
                    RecordTokenPass({Now(),closed.tokenAgeNs,closedBranch,unsigned(diagnostic.stage),Word<unsigned>(bytes.data(),0x2c),
                        closed.stockToken&&closed.reloadBit&&closed.phase!=ReloadRequestCyclePhase::Arming?3u:1u});}
                else if(closedMask&2u){++closedUnsupportedPasses;
                    RecordTokenPass({Now(),-1,closedBranch,unsigned(diagnostic.stage),Word<unsigned>(bytes.data(),0x2c),2});}}
        }
    }
    diagnostic.requested=requested;
    // Empty-gate leak record: an empty, ready copy whose Step ran without the
    // auto-reload block (the reason mask says which eligibility check failed;
    // 1<<16 means no eligibility evaluation: stage before it). Fail-closed:
    // the closed mask with 1<<18; token-pending and unsupported-family passes
    // are intended and counted apart.
    if(closedEmptyGate&&!requested){
        const bool player=!(closedMask&1u)||diagnostic.hasBefore;
        const bool empty=(closed.stateKnown&&closed.current==2&&closed.loaded==0)||
            (diagnostic.hasBefore&&diagnostic.sample.before.loaded==0&&diagnostic.sample.before.currentState==2);
        // F4: an identified copy whose state could not be read is recorded too.
        const bool unknown=!(closedMask&1u)&&!closed.stateKnown&&!diagnostic.hasBefore;
        if(unknown)closedMask|=64u;
        if(player&&(empty||unknown)&&!(closedMask&(2u|16u))){const auto n=emptyLeakCount.fetch_add(1,std::memory_order_relaxed);
            emptyLeaks[n%emptyLeaks.size()]={Now(),closedBranch<3?closedBranch:diagnostic.sample.branch,unsigned(diagnostic.stage),closedMask|(1u<<18),
                unsigned(closed.phase),Word<unsigned>(bytes.data(),0x2c),closed.stateKnown?0u:diagnostic.sample.before.flagsA8};}
    }else if(!requested&&diagnostic.hasBefore&&diagnostic.sample.before.loaded==0&&diagnostic.sample.before.currentState==2&&
             leakPhase==unsigned(ReloadRequestCyclePhase::Arming)&&(leakInput&4u)&&failureMask==1u){
        // Lease gate: the only failed check is the cycle phase, Arming with
        // the Reload bit: our own start pulse (runs 144622/144756/144928, at
        // begin, 2 -> 10 within 15 ms). Not a leak.
        const auto since=requestArmingSinceNs.load(std::memory_order_acquire);const auto now=Now();
        RecordTokenPass({now,since>0&&now>=since?now-since:-1,diagnostic.sample.branch,unsigned(diagnostic.stage),leakInput,0});
    }else if(!requested&&diagnostic.hasBefore&&diagnostic.sample.before.loaded==0&&diagnostic.sample.before.currentState==2){
        const auto n=emptyLeakCount.fetch_add(1,std::memory_order_relaxed);
        emptyLeaks[n%emptyLeaks.size()]={Now(),diagnostic.sample.branch,unsigned(diagnostic.stage),failureMask?failureMask:(1u<<16),leakPhase,leakInput,diagnostic.sample.before.flagsA8};}
    struct Call {void* self;void* context;float delta;} call{self,context,delta};
    const MagazineEmptyByteAccess access{reinterpret_cast<void*>(unsigned(context)+0x28),EmptyByteCompare};
    RunMagazineEmptyByteOverride(access,std::to_integer<std::uint8_t>(bytes[0x28]),requested,[](void* p){
        const auto& c=*static_cast<Call*>(p);originalStep(c.self,c.context,c.delta);
    },&call,patch);
    if(requested&&!patch.applied){++emptyPatchFailures;if(closedOnly)++closedPatchFailures;}
    if(patch.applied&&closedOnly){if(closedBranch<3)++closedApplied[closedBranch];if(!patch.restored||patch.unexpectedNativeWrite)++closedRestoreFailures;}
    else if(patch.applied){++emptyApplied[sample.branch];
        if(lease&&GunEvents().Enabled())GunNativeTracker().AutoReloadSuppressed(GunEvents(),GunWeaponOf(*lease),GunCopy(sample.branch),GunState(sample.before));
        if(patch.restored&&!patch.unexpectedNativeWrite)++emptyRestored[sample.branch];else ++emptyRestoreFailures;
        const auto after=lease?BoundaryFor(*lease,firing,Now()):std::nullopt;
        const bool retained=lease&&IsCurrent(*lease,Now())&&RequestOwnerEvidence(*lease,Now())&&
            cancelEpoch==requestCancelEpoch.load(std::memory_order_acquire);
        diagnostic.ownerRetained=retained;if(after){diagnostic.after=RoundState(*after);diagnostic.hasAfter=true;}
        // Magazine receipts authorize only the existing magazine downstream path.
        // Shell entry inhibition never fabricates a magazine receipt or reload hold.
        if(magazineControl)WithRequest([&]{
            if(!emptyControlReceipts.Observe(sample,after?RoundState(*after):ReloadFiringObservation{},patch,
                retained&&requestCycle.IsMagazine()&&requestCycle.MagazineProfileId()==sample.profile,Now()))++emptyReceiptFailures;
        },true);
    }
    if(diagnose){
        diagnostic.nowNs=Now();diagnostic.applied=patch.applied;diagnostic.restored=patch.restored;
        if(patch.applied)diagnostic.stage=MagazineEmptyDiagnosticStage::Applied;
        else if(requested)diagnostic.stage=MagazineEmptyDiagnosticStage::PatchFailed;
        // One bounded read only near empty/reload entry when no override ran.
        // Never creates a receipt or changes the original native outcome.
        if(!patch.applied&&diagnostic.hasBefore&&diagnostic.sample.before.loaded<=1&&lease&&IsCurrent(*lease,diagnostic.nowNs)){
            const auto after=BoundaryFor(*lease,firing,diagnostic.nowNs);
            diagnostic.ownerRetained=IsCurrent(*lease,Now())&&RequestOwnerEvidence(*lease,Now())&&
                cancelEpoch==requestCancelEpoch.load(std::memory_order_acquire);
            if(after){diagnostic.after=RoundState(*after);diagnostic.hasAfter=true;}
        }
        diagnostic.nowNs=Now();RecordMagazineEmptyDiagnostic(diagnostic);
    }
}
// SPAS physical pump (Bc2PumpHold.h): a SPAS copy outside any reload request
// cycle. Same coherent cohort read and the same exact delta transaction as the
// reload hold; never a Commit, timer or state write.
std::array<std::atomic<unsigned>,3> pumpAdvanceUnready{};
void PumpUpdate(Observation& observation,void* self,void* context,unsigned extra,unsigned caller)noexcept {
    const auto firing=unsigned(self);ReloadHoldInput input;unsigned deltaBits=0;
    const bool ready=PrepareHold(observation,firing,caller,unsigned(context),input,deltaBits);
    if(!ready&&pumpGate.Advancing()){unsigned branch=3;
        if(const auto& l=observation.lease){if(l->snapshot.branches[0].address==firing)branch=0;else if(l->snapshot.branches[1].address==firing)branch=1;
            else if(l->server&&l->server->links.firing==firing)branch=2;}
        if(branch<3)++pumpAdvanceUnready[branch];}
    const auto decision=ready?pumpGate.Evaluate(input,IsDiagnosticSpasConfig(input.config),Now()):PumpHoldGate::Decision{};
    const bool hold=(decision.hold||decision.advance>0)&&observation.lease&&IsCurrent(*observation.lease,Now());
    observation.holdRequested=hold;
    struct Original {void* self;void* context;unsigned extra;} original{self,context,extra};
    const ReloadDeltaAccess access{reinterpret_cast<void*>(unsigned(context)+0x18),DeltaCompare,DeltaRestore};
    RunReloadDeltaOverride(access,deltaBits,hold,[](void* value){
        const auto& c=*static_cast<Original*>(value);originalUpdate(c.self,c.context,c.extra);
    },&original,observation.hold,decision.hold?0.f:decision.advance);
    if(hold){if(observation.hold.applied&&observation.hold.restored&&!observation.hold.unexpectedNativeWrite)++pumpHoldApplied[input.branch];else ++pumpHoldFailures;}
    observation.Finish();
}
void OrphanStopUpdate(void* self,void* context)noexcept {
    const auto stop=orphanStop.Read();const auto firing=unsigned(self);
    if(!stop||Now()>=stop->untilNs||std::find(stop->firing.begin(),stop->firing.end(),firing)==stop->firing.end())return;
    // Only the stopped cycle, while it is still the cancelled current cycle:
    // a stale stop never touches a later reload (run 175312).
    if(requestCycleMirror.load(std::memory_order_acquire)!=stop->cycle||
       requestPhaseMirror.load(std::memory_order_acquire)!=unsigned(ReloadRequestCyclePhase::Cancelled))return;
    std::array<std::byte,0xac> before{},after{};std::array<std::byte,0x30> ctx{},ctxAfter{};
    if(!Context(firing,before)||!Context(unsigned(context),ctx)){++orphanRejects;return;}
    const auto current=Word<unsigned>(before.data(),0x3c),next=Word<unsigned>(before.data(),0x44);const auto timer=Word<float>(before.data(),0x50);
    if(current!=10&&current!=11)return; // already idle or past the reload: nothing to stop
    if(Word<unsigned>(before.data(),0)!=base+binding.state.firingVtableRva||Word<unsigned>(before.data(),8)!=stop->firingData||
       Word<unsigned>(before.data(),12)!=stop->ammoAddress||Word<int>(before.data(),0x7c)!=stop->loaded||Word<int>(before.data(),0x80)!=stop->reserve||
       (next<10||next>12)||!std::isfinite(timer)||timer<0||timer>10||(Word<unsigned char>(before.data(),0xa8)&(8|16))){++orphanRejects;return;}
    const auto decoded=DecodeReloadUpdateContext(ctx);
    if(!decoded||!std::isfinite(decoded->deltaSeconds)||decoded->deltaSeconds<=0||decoded->deltaSeconds>.05f||decoded->reloadTimeMultiplier!=1){++orphanRejects;return;}
    if(decoded->inputFlags||decoded->fireRequested||decoded->orderRequested||decoded->reloadRequested){++orphanDeferred;return;} // neutral callback only
    ++orphanCalls;const bool returned=CallNativeAbort(self);
    if(returned&&Context(firing,after)&&ReloadAbortHelperPostcondition(before,after)&&Context(unsigned(context),ctxAfter)&&ctx==ctxAfter)++orphanExact;
}
void __fastcall Update(void* self,void*,void* context,unsigned extra){
    // Declared before the callback's policy scopes: their destructors settle
    // first. The fixture's existing selected-server-copy check then receives
    // exactly one post-original boundary, including pass-through early returns.
    testRangeHandoff::FiringPostScope fixturePost{std::uint32_t(self)};
    Active activeScope;const auto firing=unsigned(self),caller=unsigned(_ReturnAddress());
    Observation observation(ReloadFlowEvent::Update,firing,caller,unsigned(context),extra);
    if(physicalWeaponRuntime::Managed(firing)){
        physicalWeaponRuntime::BeforeUpdate(firing);
        originalUpdate(self,context,extra);
        physicalWeaponRuntime::AfterUpdate(firing);
        observation.Finish();return;
    }
    if(requestMode&&activeScope.requestEntryAllowed&&!RequestTarget(firing)&&pumpGate.Enabled()&&observation.lease&&
       IsDiagnosticSpasConfig(observation.lease->snapshot.config)){PumpUpdate(observation,self,context,extra,caller);return;}
    if(requestMode&&!closedEmptyGate&&!activeScope.requestEntryAllowed&&RequestTarget(firing)){
        // Still ungated: record it when this copy is empty and ready (leak mask 1<<17).
        std::uint32_t current=0,loaded=0;
        if(memory.read&&memory.read(memory.context,firing+0x3c,&current,4)&&memory.read(memory.context,firing+0x7c,&loaded,4)&&current==2&&loaded==0){
            const auto n=emptyLeakCount.fetch_add(1,std::memory_order_relaxed);emptyLeaks[n%emptyLeaks.size()]={Now(),3,0,1u<<17,0,0,0};}}
    if(requestMode&&!observation.lease)OrphanStopUpdate(self,context);
    if((!diagnosticHold&&!diagnosticRound&&!requestMode)||(requestMode&&(!activeScope.requestEntryAllowed||!RequestTarget(firing)))){originalUpdate(self,context,extra);return;}
    const auto revision=callbackRevision.load(std::memory_order_acquire);
    const bool quietBefore=active.load(std::memory_order_acquire)==1;
    ReloadHoldInput input;unsigned deltaBits=0;bool ready=false;
    if(observation.lease){ready=PrepareHold(observation,firing,caller,unsigned(context),input,deltaBits);
        if(!ready)++holdSampleFailures;}
    if(pumpDiagnostic&&(!quietBefore||active.load(std::memory_order_acquire)!=1||revision!=callbackRevision.load(std::memory_order_acquire)))ready=false;
    if(mailboxMode&&requestMode&&requestCycle.IsMagazine()){
        // Mailbox: no request lock or cycle evaluation here. Act only on the
        // board's live command for this copy, and only on a held copy (11).
        AbortRecord abortReport;ReloadAbortDecision abortDecision;
        if(ready)abortDecision=TryAbort(observation,input,self,context,abortReport);
        ReloadHookCommand command;if(const auto board=mailboxBoard.Read())command=ResolveReloadCommand(*board,firing,Now());
        MailboxReceipt receipt;receipt.nowNs=Now();receipt.input=input;receipt.timing=ready&&RequestTiming(input.config);
        receipt.coherent=quietBefore&&active.load(std::memory_order_acquire)==1&&revision==callbackRevision.load(std::memory_order_acquire);
        const bool waiting=ready&&input.branch<3&&command.branch==input.branch&&input.branches[input.branch].currentState==11;
        float advance=0;
        if(waiting&&command.kind==ReloadCommandKind::Hold)receipt.applied=ReloadCommandKind::Hold;
        else if(waiting&&command.kind==ReloadCommandKind::AdvanceOnce&&command.advanceSeconds>0&&mailboxBoard.ConsumeAdvance(command.branch,command.serial)){
            receipt.applied=ReloadCommandKind::AdvanceOnce;advance=command.advanceSeconds;}
        observation.holdRequested=receipt.applied!=ReloadCommandKind::None&&observation.lease&&IsCurrent(*observation.lease,Now());
        if(!observation.holdRequested)receipt.applied=ReloadCommandKind::None;
        mailboxPending=&receipt;mailboxPendingCycle=command.cycle;
        struct Original {void* self;void* context;unsigned extra;} original{self,context,extra};
        const ReloadDeltaAccess access{reinterpret_cast<void*>(unsigned(context)+0x18),DeltaCompare,DeltaRestore};
        RunReloadDeltaOverride(access,deltaBits,observation.holdRequested,[](void* value){
            const auto& c=*static_cast<Original*>(value);originalUpdate(c.self,c.context,c.extra);
        },&original,observation.hold,receipt.applied==ReloadCommandKind::AdvanceOnce?advance:0.f);
        mailboxPending=nullptr;
        if(observation.holdRequested&&observation.hold.applied&&input.branch<3){
            if(receipt.applied==ReloadCommandKind::Hold)++mailboxHolds[input.branch];else{++mailboxAdvances[input.branch];++seatAdvances[input.branch];}}
        const auto completed=observation.Finish();FinishAbort(abortDecision,abortReport,completed);
        receipt.update=observation.frame.invocation.Record().id;receipt.delta=observation.hold;
        if(completed&&completed->exit.boundary){receipt.finished=true;receipt.after=RoundState(*completed->exit.boundary);
            receipt.afterNs=completed->exit.nowNs;receipt.retained=completed->identityRetained;}
        if(ready&&input.branch<3)mailboxRings[input.branch].Push(receipt);
        return;
    }
#ifdef FVR_BC2_PREHOLD_CLEANUP
    bool preholdHelperExact=false;
    if(preholdMonitorCancelled&&!ready&&observation.lease&&observation.lease->server&&firing==observation.lease->server->links.firing)PreholdServerDenied(4,Now());
    if(ready&&requestMode&&observation.lease&&(input.branch==2||(quietBefore&&active.load(std::memory_order_acquire)==1&&revision==callbackRevision.load(std::memory_order_acquire)))){
        ObservePreholdOutsidePolicy(*observation.lease,input);
        preholdHelperExact=TryPreholdMonitor(*observation.lease,input,self,context,observation.entry.copiedContext,revision,observation.frame.invocation.Record().id);
    }
#endif
    AbortRecord abortReport;ReloadAbortDecision abortDecision;
    if(ready&&requestMode)abortDecision=TryAbort(observation,input,self,context,abortReport);
    ReloadRoundGateDecision roundDecision;
    ReloadRequestDecision requestDecision;
    if(requestMode){
        const auto cancelEpoch=requestCancelEpoch.load(std::memory_order_acquire);
        if(ready){const bool timing=RequestTiming(input.config);bool evaluateDeferred=false;
            WithRequest([&]{const bool coherent=quietBefore&&active.load(std::memory_order_acquire)==1&&revision==callbackRevision.load(std::memory_order_acquire);
                requestDecision=requestCycle.Evaluate(input,timing,coherent,observation.frame.invocation.Record().id,Now());},false,&evaluateDeferred,4);
            // A contended Evaluate skips this frame's hold (the native timer
            // moves one frame); it no longer cancels the held reload.
            if(evaluateDeferred)++evaluateContentionSkips;
        }else RequestMissing(firing,4,unsigned(ReloadFlowEvent::Update),&observation.evidence);
        if(requestDecision.hold||requestDecision.advanceSeconds>0){bool allowed=false;
            const bool checked=WithRequest([&]{allowed=requestCycle.Allows(requestDecision,Now());},true);
            observation.holdRequested=checked&&allowed&&observation.lease&&IsCurrent(*observation.lease,Now())&&
                cancelEpoch==requestCancelEpoch.load(std::memory_order_acquire);
            if(!observation.holdRequested)RequestMissing(firing,5,unsigned(ReloadFlowEvent::Update));
        }
    }else if(diagnosticRound){
        if(ready){const bool timing=RequestTiming(input.config);
            WithRound([&]{const bool coherent=quietBefore&&active.load(std::memory_order_acquire)==1&&revision==callbackRevision.load(std::memory_order_acquire);
                roundDecision=roundGate.Evaluate(input,timing,coherent,observation.frame.id);});
        }else RoundMissing(firing);
        observation.holdRequested=roundDecision.hold&&observation.lease&&IsCurrent(*observation.lease,Now())&&
            !roundCancelled.load(std::memory_order_acquire)&&Now()<roundDecision.deadlineNs;
        if(roundDecision.hold&&!observation.holdRequested)WithRound([&]{roundGate.Cancel(ReloadRoundGateFailure::OwnerOrRead);});
    }else {
        if(ready)observation.holdRequested=holdProbe.Evaluate(input);
        else if(holdProbe.Targets(firing))holdProbe.Abort(ReloadHoldReason::OwnerOrRead);
        observation.holdRequested=observation.holdRequested&&observation.lease&&IsCurrent(*observation.lease,Now())&&holdProbe.Allows(Now());
    }
    struct Original {void* self;void* context;unsigned extra;} original{self,context,extra};
    const ReloadDeltaAccess access{reinterpret_cast<void*>(unsigned(context)+0x18),DeltaCompare,DeltaRestore};
    RunReloadDeltaOverride(access,deltaBits,observation.holdRequested,[](void* value){
        const auto& c=*static_cast<Original*>(value);originalUpdate(c.self,c.context,c.extra);
    },&original,observation.hold,requestMode&&!requestDecision.hold?requestDecision.advanceSeconds:0.f);
    if(observation.holdRequested&&!requestDecision.hold&&observation.hold.applied)++seatAdvances[input.branch<3?input.branch:0];
    if(observation.holdRequested){++holdOriginalCalls;
        if(!observation.hold.applied){++holdPatchFailures;holdProbe.Abort(ReloadHoldReason::PatchFailed);}
        else {++holdApplied[input.branch];if(observation.hold.restored&&!observation.hold.unexpectedNativeWrite)++holdRestored[input.branch];
            else {++holdRestoreFailures;holdProbe.Abort(ReloadHoldReason::RestoreFailed);}}
    }
    if(diagnosticRound&&roundDecision.tracked){const auto completed=observation.Finish();
        WithRound([&]{if(!completed||!completed->exit.boundary){roundGate.Cancel(ReloadRoundGateFailure::OwnerOrRead);return;}
            roundGate.Finish(roundDecision,RoundState(*completed->exit.boundary),completed->exit.nowNs,completed->identityRetained,observation.hold);});
    }
    if(requestMode){const auto completed=observation.Finish();
        FinishAbort(abortDecision,abortReport,completed);
#ifdef FVR_BC2_PREHOLD_CLEANUP
        if(preholdMonitorCancelled&&observation.lease&&quietBefore&&active.load(std::memory_order_acquire)==1&&revision==callbackRevision.load(std::memory_order_acquire)){
            WithRequest([&]{
                if(requestCycle.Phase()!=ReloadRequestCyclePhase::Cancelled||
                   !preholdMonitor.CleanupObservationOpen(Now()))return;
                const auto finalEvidence=PreholdEvidence(*observation.lease,requestCycle.Cycle(),false);
                if(finalEvidence)preholdMonitor.ObserveCleanup(*finalEvidence,Now());
            },false,nullptr,6);
        }
#endif
#ifdef FVR_BC2_PREHOLD_CLEANUP
        // Observe already-idle authoritative server only AFTER its own validated
        // OriginalUpdate. Current fresh cohort must retain original operation
        // owner/config/revision/counts; never synthesize helper-called bit4.
        if(preholdMonitorCancelled&&observation.lease&&
           PreholdAuthoritativeIdleInvocation(ready,input.branch,preholdHelperExact,
               completed&&completed->identityRetained,completed&&completed->exit.boundary.has_value())){
            const auto& b=*completed->exit.boundary;
            const auto& before=input.branches[2];
            const bool neutral=input.context.deltaSeconds>0&&input.context.deltaSeconds<=.05f&&
                input.context.reloadTimeMultiplier==1&&!input.context.inputFlags&&!input.context.fireRequested&&
                !input.context.orderRequested&&!input.context.reloadRequested;
            if(neutral&&(before.currentState==1||before.currentState==2)&&(before.nextState==1||before.nextState==2)&&
               (b.current==1||b.current==2)&&(b.next==1||b.next==2)&&b.loaded==before.loaded&&b.reserve==before.reserve){
                WithRequest([&]{
                    if(!requestCycle.CancelledFromArming()||preholdMonitorFailed||
                       !preholdMonitor.CleanupObservationOpen(Now()))return;
                    const auto current=PreholdEvidence(*observation.lease,requestCycle.Cycle(),false);
                    if(!current||!preholdMonitor.ObserveCleanup(*current,Now()))return;
                    const auto& server=current->source.branches[2];
                    if((server.currentState==1||server.currentState==2)&&(server.nextState==1||server.nextState==2)&&
                       server.loaded==b.loaded&&server.reserve==b.reserve)
                        preholdAuthoritativeIdleNs.store(current->observedNs,std::memory_order_release);
                },true);
            }
        }
        if(preholdHelperExact&&(!completed||!completed->identityRetained||!completed->exit.boundary))PreholdMonitorFailure(3,input.branch,Now());
        if(preholdHelperExact&&completed&&completed->identityRetained&&completed->exit.boundary){
            const auto& b=*completed->exit.boundary;
            auto& report=preholdMonitorBranches[input.branch];report.updateKnown=true;report.updateAfter=RoundState(b);
            report.updateIdle=(b.current==1||b.current==2)&&(b.next==1||b.next==2)&&b.loaded==report.before.loaded&&b.reserve==report.before.reserve;
            report.reason=report.updateIdle?4:5;
            if((b.current==1||b.current==2)&&(b.next==1||b.next==2)&&b.loaded==input.branches[input.branch].loaded&&
               b.reserve==input.branches[input.branch].reserve)preholdMonitorIdle.fetch_or(1u<<input.branch);else PreholdMonitorFailure(4,input.branch,Now());
        }
#endif
        if(!completed||!completed->exit.boundary){RequestMissing(firing,6,unsigned(ReloadFlowEvent::Update),&observation.evidence);
            if(requestDecision.tracked)WithRequest([&]{requestCycle.Finish(requestDecision,{},Now(),false,observation.hold);},true);return;}
        if(requestDecision.tracked)WithRequest([&]{requestCycle.Finish(requestDecision,RoundState(*completed->exit.boundary),completed->exit.nowNs,completed->identityRetained,observation.hold,Now());},true);
    }
}
void __fastcall Commit(void* self,void*,void* context){
    Active activeScope;Observation observation(ReloadFlowEvent::Commit,unsigned(self),unsigned(_ReturnAddress()),unsigned(context),0);
    originalCommit(self,context);
    if(requestMode&&!observation.Finish())RequestMissing(unsigned(self),7,unsigned(ReloadFlowEvent::Commit),&observation.evidence);
}
void __fastcall Transfer(void* self,void*,unsigned rawArgument){
    // Physical commands publish the server projection directly. No competing
    // stock transfer (including zero-duration activation) may credit rounds.
    if(physicalWeaponRuntime::Managed(unsigned(self))){
        const auto transfer=physicalWeaponRuntime::BeginNativeReloadTransfer(unsigned(self),unsigned(_ReturnAddress()));
        if(transfer){originalTransfer(self,rawArgument);physicalWeaponRuntime::FinishNativeReloadTransfer(*transfer);}
        return;
    }
    Active activeScope;Observation observation(ReloadFlowEvent::Transfer,unsigned(self),unsigned(_ReturnAddress()),0,rawArgument);
    originalTransfer(self,rawArgument);
    if(diagnosticRound)RoundTransfer(observation,observation.Finish());
    if(requestMode)RequestTransfer(observation,observation.Finish());
}
void __fastcall Restore(void* self,void*,void* snapshot){
    Active activeScope;Observation observation(ReloadFlowEvent::Restore,unsigned(self),unsigned(_ReturnAddress()),unsigned(snapshot),0);
    originalRestore(self,snapshot);
    if(requestMode){const auto complete=observation.Finish();if(!complete||!ReloadRestoreMatched(*complete))RequestMissing(unsigned(self),8,unsigned(ReloadFlowEvent::Restore),&observation.evidence);
        else if(observation.lease&&complete->exit.boundary&&RequestTarget(unsigned(self))){
            // Existing signature-verified Restore hook + immutable snapshot exact
            // match; never infer this receipt from a generic client count revert.
            const auto& entry=complete->entry;const auto& after=*complete->exit.boundary;
            WithRequest([&]{if(!IsCurrent(*observation.lease,Now())||requestCycle.Identity()!=RoundIdentity(*observation.lease)||!requestCycle.MatchesSelectedConfig(observation.lease->snapshot.config))return;
                {OwnerCopyGate gate;if(!gate.held||!IsCurrent(published,Now())||
                    RoundIdentity(published)!=RoundIdentity(*observation.lease)||published.snapshot.config!=observation.lease->snapshot.config)return;}
                ReloadMagazinePredictionRestore e{RoundIdentity(*observation.lease),requestCycle.Cycle(),complete->id,
                    entry.nowNs,complete->exit.nowNs,entry.boundary.branch,entry.caller,
                    entry.boundary.loaded,entry.boundary.reserve,after.loaded,after.reserve,true,complete->identityRetained};
                e.config=observation.lease->snapshot.config;requestCycle.ObserveMagazinePredictionRestore(e,Now());},true);
        }}
}
void Boundary(std::ostream& out,const ReloadFlowBoundary& b){
    out<<"{\"snapshot_sequence\":"<<b.snapshotSequence<<",\"player\":"<<b.owner.player<<",\"soldier\":"<<b.owner.soldier
       <<",\"weak\":"<<b.owner.weak<<",\"weapon\":"<<b.owner.weapon<<",\"actor_generation\":"<<b.owner.actorGeneration
       <<",\"equip_generation\":"<<b.owner.equipGeneration<<",\"space\":"<<b.owner.space<<",\"firing\":"<<b.firing
       <<",\"server_player\":"<<b.serverPlayer<<",\"server_soldier\":"<<b.serverSoldier<<",\"server_item\":"<<b.serverItem
       <<",\"branch\":"<<unsigned(b.branch)<<",\"wrapper_offset\":"<<b.wrapperOffset<<",\"soldier_flags\":"<<unsigned(b.soldierFlags)
       <<",\"current\":"<<b.current<<",\"previous\":"<<b.previous<<",\"next\":"<<b.next<<",\"timer\":"<<b.timer
       <<",\"loaded\":"<<b.loaded<<",\"reserve\":"<<b.reserve<<",\"flags_a8\":"<<unsigned(b.flagsA8)<<"}";
}
void Snapshot(std::ostream& out,const std::array<std::byte,0x40>& bytes,bool copied){
    if(!copied){out<<"null";return;}out<<"{\"bytes\":[";for(unsigned n=0;n<bytes.size();++n){if(n)out<<',';out<<std::to_integer<unsigned>(bytes[n]);}
    const auto decoded=DecodeReloadFiringSnapshot(bytes);out<<"],\"decoded_valid\":"<<(decoded?"true":"false");
    if(decoded)out<<",\"current\":"<<decoded->current<<",\"next\":"<<decoded->next<<",\"timer\":"<<decoded->phaseTimer
        <<",\"loaded\":"<<decoded->loaded<<",\"reserve\":"<<decoded->reserve;
    out<<"}";
}
void Copied(std::ostream& out,const std::array<std::byte,0x30>& bytes,bool copied){
    if(!copied){out<<"null";return;}out<<"{\"bytes\":[";for(unsigned n=0;n<bytes.size();++n){if(n)out<<',';out<<std::to_integer<unsigned>(bytes[n]);}
    const auto decoded=DecodeReloadUpdateContext(bytes);out<<"],\"decoded_valid\":"<<(decoded?"true":"false");
    if(decoded)out<<",\"delta_seconds\":"<<decoded->deltaSeconds<<",\"reload_multiplier\":"<<decoded->reloadTimeMultiplier
        <<",\"raw_1c\":"<<decoded->rawWord1c<<",\"input_flags\":"<<decoded->inputFlags;
    out<<"}";
}
}
void PublishMagazineInteractionDiagnostic(const MagazineInteractionDiagnostic& value)noexcept {StoreMagazineInteractionDiagnostic(value);}
MagazineEmptyControlCounters ReadMagazineEmptyControlCounters()noexcept {
    MagazineEmptyControlCounters out;for(unsigned n=0;n<3;++n){out.requested[n]=emptyRequested[n].load();
        out.applied[n]=emptyApplied[n].load();out.restored[n]=emptyRestored[n].load();}
    out.patchFailures=emptyPatchFailures.load();out.restoreFailures=emptyRestoreFailures.load();out.receiptFailures=emptyReceiptFailures.load();return out;
}
bool EnableMagazineRequestCycles()noexcept {
    if(!installed||started||enabled.load()||!requestMode||diagnosticHold||diagnosticRound||
       !holdCodeVerified||requestCycle.IsMagazine())return false;
    combinedFamilies=true;return true;
}
bool SelectRequestProfile(ReloadNativeFamily family,NativeMagazineProfileId profileId,const ReloadStateOwner& expected)noexcept {
    ++familySelectAttempts;
    const auto reject=[](unsigned stage)noexcept{++familySelectRejected;++familySelectFailures[stage];return false;};
    if(!combinedFamilies||!requestMode||!enabled.load(std::memory_order_acquire))return reject(0);
    Active activeScope;if(!activeScope.requestEntryAllowed)return reject(1);
    std::optional<Lease> lease;
    {OwnerCopyGate lock;if(!lock.held)return reject(2);
        if(IsCurrent(published,Now())&&published.server&&published.snapshot.owner==expected)lease=published;}
    if(!lease)return reject(3);
    if(!FamilyConfig(family,profileId,lease->snapshot.config))return reject(4);
    if(!FamilyTiming(family,profileId,lease->snapshot.config))return reject(5);
    if(!RequestOwnerEvidence(*lease,Now()))return reject(6);
    ReloadInvocationExclusion entry(requestEntryGate,active,callbackRevision);
    if(!entry.held||!entry.Quiet())return reject(7);
    bool accepted=false;
    const bool locked=WithRequest([&]{
        if(!entry.Quiet()||!IsCurrent(*lease,Now()))return;
        if(requestCycle.Family()==family&&(family!=ReloadNativeFamily::Xm8Magazine||requestCycle.MagazineProfileId()==profileId)){accepted=true;return;}
        const bool retired=retiredFamily&&retiredFamily->family==requestCycle.Family()&&
            retiredFamily->identity==requestCycle.Identity()&&retiredFamily->cycle==requestCycle.Cycle()&&
            retiredFamily->epoch==familyEpoch&&retiredFamily->event;
        const bool reusedStop=requestCycle.Cycle()!=0&&!retired;
#ifdef FVR_BC2_PREHOLD_CLEANUP
        if(!PreholdAllowsNewOperation())return;
#endif
#ifdef FVR_BC2_PREHOLD_CLEANUP
        const auto priorPrehold=SnapshotPreholdOperation();
#endif
        if(familyEpoch==UINT64_MAX||!(family==ReloadNativeFamily::Xm8Magazine?
            requestCycle.SelectMagazineProfile(profileId,true,retired):requestCycle.SelectFamily(family,true,retired)))return;
#ifdef FVR_BC2_PREHOLD_CLEANUP
        if(priorPrehold)preholdOperationJournal.Archive(*priorPrehold,Now(),entry.Quiet());
#endif
        if(reusedStop)++familyRetirementReuses;
        abortCleanup.Abandon(ReloadAbortFailure::NewCycle,Now());
        ++familyEpoch;
#ifdef FVR_BC2_PREHOLD_CLEANUP
        ResetPreholdOperation();
#endif
        if(!abortCleanup.BeginDispatchEpoch(familyEpoch,true))return;
        for(auto& target:requestTargets)target.store(0,std::memory_order_release);
        retiredFamily.reset();++familySelectChanges;accepted=true;
    },true);
    if(!locked||!accepted)return reject(8);if(!entry.Quiet())return reject(9);return true;
}
bool SelectRequestFamily(ReloadNativeFamily family,const ReloadStateOwner& expected)noexcept {
    return SelectRequestProfile(family,NativeMagazineProfileId::ScopedXm8,expected);
}
std::optional<NativeMagazineProfileId> ReadMagazineRequestProfile(const ReloadStateOwner& expected)noexcept {
    if(!combinedFamilies||!requestMode||!enabled.load(std::memory_order_acquire))return {};
    Active activeScope;if(!activeScope.requestEntryAllowed)return {};
    std::optional<Lease> lease;
    {OwnerCopyGate lock;if(!lock.held)return {};
        if(IsCurrent(published,Now())&&published.server&&published.snapshot.owner==expected)lease=published;}
    if(!lease)return {};
    const auto selected=FindMagazineNativeProfile(lease->snapshot.config);
    // Selection is data only. Reuse the original coherent published snapshot;
    // do not turn a transient live timer read into loss of interaction identity.
    // SelectRequestProfile/ReadReserve/Start still perform all native reads.
    if(!selected||!IsCurrent(*lease,Now()))return {};
    return selected->id;
}
bool SelectMagazineRequestProfile(NativeMagazineProfileId profile,const ReloadStateOwner& expected)noexcept {
    return SelectRequestProfile(ReloadNativeFamily::Xm8Magazine,profile,expected);
}
std::optional<ReloadHoldIdentity> RequestIdentity()noexcept {
    if(!requestMode||!enabled.load(std::memory_order_acquire))return {};
    Active activeScope;if(!activeScope.requestEntryAllowed)return {};std::optional<Lease> lease;
    {OwnerCopyGate lock;if(!lock.held){++ownerDrops;return {};} // No new identity; serialization contention is not native owner loss.
        if(IsCurrent(published,Now())&&published.server)lease=published;}
    if(!lease||!RequestConfig(lease->snapshot.config)||!RequestTiming(lease->snapshot.config)){
        RequestCancelAsync(4);return {};}
    const auto identity=RoundIdentity(*lease);
    if(!RequestOwnerEvidence(*lease,Now())){RequestCancelAsync(4);return {};}
    return identity;
}
// Observation unavailable due to lock contention grants no new identity and
// cannot cancel a still-bounded cycle. Genuine owner/config/expiry rejection
// retains the original cancellation path.
std::optional<ReloadHoldIdentity> RequestIdentityForObservation(bool* deferred=nullptr)noexcept {
    if(!requestMode||!enabled.load(std::memory_order_acquire))return {};
    Active activeScope;if(!activeScope.requestEntryAllowed)return {};std::optional<Lease> lease;
    {OwnerCopyGate lock;if(!lock.held){++ownerDrops;if(deferred)*deferred=true;return {};}
        if(IsCurrent(published,Now())&&published.server)lease=published;}
    if(!lease||!RequestConfig(lease->snapshot.config)||!RequestTiming(lease->snapshot.config)){
        RequestCancelAsync(4);return {};}
    const auto identity=RoundIdentity(*lease);
    if(!RequestOwnerEvidence(*lease,Now())){RequestCancelAsync(4);return {};}
    return identity;
}
std::optional<RequestProbeSnapshot> ReadRequestProbeSnapshot()noexcept {
    if(!requestMode||!enabled.load(std::memory_order_acquire))return {};
    Active activeScope;if(!activeScope.requestEntryAllowed)return {};std::optional<Lease> lease;
    {OwnerCopyGate lock;if(!lock.held)return {};if(IsCurrent(published,Now())&&published.server)lease=published;}
    if(!lease)return {};RequestProbeSnapshot result;result.identity=RoundIdentity(*lease);result.observedNs=Now();
    for(unsigned n=0;n<3;++n){const auto b=BoundaryFor(*lease,result.identity.firing[n],Now());if(!b)return {};result.branches[n]=RoundState(*b);}
    if(!IsCurrent(*lease,Now()))return {};
    {Gate lock(requestGate);if(!lock.held)return {};
        result.cycle=requestCycle.Cycle();result.phase=unsigned(requestCycle.Phase());result.failure=unsigned(requestCycle.Failure());result.family=unsigned(requestCycle.Family());
        result.pendingRequest=requestCycle.PendingRequest();result.unresolved=requestCycle.UnresolvedRequest();
        result.transfers=requestTransferCounts;result.lastTransfer=requestLastTransfer;
    }
    for(unsigned n=0;n<3;++n){result.applied[n]=holdApplied[n].load();result.restored[n]=holdRestored[n].load();}
    result.patchFailures=holdPatchFailures.load();result.restoreFailures=holdRestoreFailures.load();
    result.activeCallbacks=active.load()-1;result.revision=callbackRevision.load();return result;
}
static ReloadReserveObservation ReadReserveObservedFor(bool diagnostic)noexcept {
    ReserveReadEvidence readEvidence;readEvidence.observedNs=Now();
    const auto reject=[&](unsigned stage,bool deferred=false,bool cohort=false)noexcept->ReloadReserveObservation{
        const auto result=cohort?ReloadObservationResult::CohortGap:deferred?ReloadObservationResult::Deferred:ReloadObservationResult::Rejected;
        ++reserveReadStages[stage];readEvidence.stage=stage;readEvidence.result=unsigned(result);
        if(!readEvidence.validationNs)readEvidence.validationNs=Now();reserveReadEvidence.Observe(readEvidence);return {result,{},stage};};
    if(!requestMode||!enabled.load(std::memory_order_acquire))return reject(0);
    Active activeScope;const auto revision=callbackRevision.load(std::memory_order_acquire);
    if(!activeScope.requestEntryAllowed||active.load(std::memory_order_acquire)!=1)return reject(1,true);
    std::optional<Lease> lease;
    {OwnerCopyGate lock;if(!lock.held)return reject(2,true);if(IsCurrent(published,Now())&&published.server)lease=published;}
    if(!lease)return reject(3);
    const auto configAccepted=[&]()noexcept{return diagnostic?DiagnosticFireReserveConfig(memory,lease->snapshot.config):RequestConfig(lease->snapshot.config);};
    if(!configAccepted())return reject(4);
    const auto identity=RoundIdentity(*lease);
    readEvidence.owner={identity.owner.player,identity.owner.soldier,identity.owner.weak,identity.owner.weapon};
    readEvidence.sampledRevision=revision;readEvidence.targetKnown=true;
    readEvidence.targetMatched=RequestTarget(identity.firing[0])||RequestTarget(identity.firing[1])||RequestTarget(identity.firing[2]);
    const auto validateFinal=[&](std::int64_t sourceDeadline,unsigned check)noexcept {
        // Reprove structural ownership only for an actual callback overlap.
        // Quiet success already double-read the exact boundaries/config.
        const bool overlap=active.load(std::memory_order_acquire)!=1||callbackRevision.load(std::memory_order_acquire)!=revision;
        bool configOkay=!overlap||configAccepted();
        bool ownerOkay=!overlap||RequestOwnerEvidence(*lease,Now());
        // Explicitly ordered AFTER any structural reader; no argument-order race.
        auto validationNow=Now();auto currentActive=active.load(std::memory_order_acquire);
        auto currentRevision=callbackRevision.load(std::memory_order_acquire);
        if(!overlap&&(currentActive!=1||currentRevision!=revision)){
            configOkay=configAccepted();ownerOkay=RequestOwnerEvidence(*lease,Now());
            validationNow=Now();currentActive=active.load(std::memory_order_acquire);
            currentRevision=callbackRevision.load(std::memory_order_acquire);
        }
        const bool current=IsCurrent(*lease,validationNow);
        readEvidence.check=check;readEvidence.validationNs=validationNow;readEvidence.deadlineNs=sourceDeadline;
        readEvidence.active=currentActive;readEvidence.currentRevision=currentRevision;
        readEvidence.ownerKnown=readEvidence.configKnown=true;
        readEvidence.ownerCurrent=current&&ownerOkay;readEvidence.configCurrent=configOkay;
        return ValidateReserveFinal(current&&ownerOkay&&configOkay,validationNow<sourceDeadline,currentActive,revision,currentRevision);
    };
    std::array<ReloadFlowBoundary,3> first{},second{};std::array<int,3> capacities{},again{};
    const auto countObserved=Now(); // Before the first actual count read, not owner snapshot time.
    if(!IsCurrent(*lease,countObserved))return reject(7);
    for(unsigned pass=0;pass<2;++pass)for(unsigned n=0;n<3;++n){
        ReloadServerBoundaryDiagnostic serverDiagnostic;ReloadFlowBoundaryDiagnostic clientDiagnostic;
        const auto b=BoundaryFor(*lease,identity.firing[n],Now(),&serverDiagnostic,&clientDiagnostic);
        const auto capacity=Capacity(identity.firing[n],lease->snapshot.config.baseCapacity);
        if(!capacity)return reject(5);
        if(!b){
            const bool changed=n==2?serverDiagnostic.failure==ReloadServerBoundaryFailure::State&&serverDiagnostic.stateFailure==3:
                clientDiagnostic.failure==ReloadFlowBoundaryFailure::ChangedState;
            const auto changedOffset=n==2?serverDiagnostic.changedOffset:clientDiagnostic.changedOffset;
            const bool mutableState=(changedOffset>=0x3c&&changedOffset<0x48)||(changedOffset>=0x50&&changedOffset<0x58)||
                (changedOffset>=0x7c&&changedOffset<0x84)||changedOffset==0xa8;
            const bool retained=changed&&mutableState&&IsCurrent(*lease,Now())&&configAccepted()&&RequestOwnerEvidence(*lease,Now());
            return reject(5,false,retained);
        }
        if(!pass){first[n]=*b;capacities[n]=*capacity;}
        else {second[n]=*b;again[n]=*capacity;}
    }
    if(!ReloadReserveCopiesAgree(identity,first,second,capacities,again)){
        bool sane=true;
        for(unsigned n=0;n<3;++n)for(const auto* sample:{&first[n],&second[n]})
            sane=sane&&sample->owner==identity.owner&&sample->firing==identity.firing[n]&&sample->branch==n&&
                sample->wrapperOffset==(n==0?0x3cu:n==1?0x40u:0x10u)&&SameIdentity(first[n],second[n])&&
                (n!=2||(sample->serverPlayer==identity.serverPlayer&&sample->serverSoldier==identity.serverSoldier&&sample->serverItem==identity.serverItem))&&
                sample->current<=15&&sample->previous<=15&&sample->next<=15&&std::isfinite(sample->timer)&&
                sample->loaded>=0&&sample->loaded<=1000000&&sample->reserve>=0&&sample->reserve<=1000000&&
                !(sample->flagsA8&(8|16))&&capacities[n]>0&&capacities[n]<=1000000&&
                capacities[n]==again[n]&&capacities[n]==capacities[0]&&sample->loaded<=capacities[n];
        const bool retained=sane&&IsCurrent(*lease,Now())&&configAccepted()&&RequestOwnerEvidence(*lease,Now());
        return reject(6,false,retained);
    }
    const auto now=Now();const auto deadline=ReloadReserveReadDeadline(lease->snapshot.observedNs,lease->deadline,countObserved,now);
    if(!deadline)return reject(7);
    const auto firstVerdict=validateFinal(*deadline,1);
    if(firstVerdict!=ReserveFinalVerdict::Available)return reject(7,false,firstVerdict==ReserveFinalVerdict::CohortGap);
    const auto sequence=reserveSequence.Next();if(!sequence)return reject(8);
    ++reserveReadStages[9];
    Bc2AmmoReserveLease result{identity,sequence,countObserved,*deadline,first[0].loaded,first[0].reserve,capacities[0],true,
        ReloadReserveInputReady(identity,first,second,capacities,again),ReloadReserveIdle(identity,first,second,capacities,again)};
    if(!diagnostic&&combinedFamilies&&result.loaded==0&&result.reloadInputReady){
        bool deferred=false;const bool checked=WithRequest([&]{if(requestCycle.IsMagazine())
            if(const auto end=emptyControlReceipts.EmptyDeadline(identity,requestCycle.MagazineProfileId(),lease->revision,result.reserve,Now())){
                result.deadlineNs=std::min(result.deadlineNs,*end);result.emptyReloadControlled=true;
            }},false,&deferred);
        if(deferred)return reject(2,true);
        if(!checked)result.emptyReloadControlled=false;
    }
    // WithRequest may have waited while an original callback changed counts.
    // The original cohort revision must still be quiet after that wait too.
    const auto finalVerdict=validateFinal(result.deadlineNs,2);
    if(finalVerdict!=ReserveFinalVerdict::Available)return reject(7,false,finalVerdict==ReserveFinalVerdict::CohortGap);
    return {ReloadObservationResult::Available,result,9};
}
// Most gather-time reads deferred (stage 1, entry: another reload hook was in
// flight on a game sim thread; live runs 75-85% of gathers). Those callbacks
// last microseconds, so wait for a quiet moment (no hook in flight), bounded
// to 1 ms in total, and read again. Never waits inside a hook: the read is
// the gather's own, and the same quiet-entry rule still decides.
ReloadReserveObservation ReadReserveObserved()noexcept {
    auto r=ReadReserveObservedFor(false);
    if(r.result!=ReloadObservationResult::Deferred||r.stage!=1||currentFrame)return r;
    const auto start=Now();
    for(unsigned attempt=0;attempt<4&&r.result==ReloadObservationResult::Deferred&&r.stage==1;++attempt){
        ++reserveQuietRetries;
        while(active.load(std::memory_order_acquire)!=0){if(Now()-start>1000000){++reserveQuietTimeouts;return r;}YieldProcessor();}
        r=ReadReserveObservedFor(false);
    }
    if(r.result==ReloadObservationResult::Available)++reserveQuietWins;
    return r;
}
std::optional<Bc2AmmoReserveLease> ReadReserve()noexcept {return ReadReserveObserved().lease;}
std::optional<Bc2AmmoReserveLease> ReadDiagnosticFireReserve()noexcept {return ReadReserveObservedFor(true).lease;}

bool StartRequestCycle(const ReloadCycleControl& control)noexcept {
    const auto identity=RequestIdentity();if(!identity||*identity!=control.identity)return false;
#ifdef FVR_BC2_PREHOLD_CLEANUP
    const auto preholdPreread=ReadPreholdStartPreread(control);if(!preholdPreread)return false;
#endif
    Active activeScope;bool accepted=false;
    ReloadInvocationExclusion entry(requestEntryGate,active,callbackRevision);
    if(!entry.held||!activeScope.requestEntryAllowed)return false;
    const bool locked=WithRequest([&]{
        if(!entry.Quiet())return;
        // New arrivals observe requestEntryGate, cancel this attempt and cannot
        // acquire a policy decision. No callback gate spans an original.
        requestCycle.DrainCancelledInvocations(true);

#ifdef FVR_BC2_PREHOLD_CLEANUP
        if(!PreholdAllowsNewOperation()||!PreholdStartPublishedCurrent(*preholdPreread)||!IsCurrent(preholdPreread->lease,Now())||
           !RequestConfig(preholdPreread->lease.snapshot.config)||
           !PreholdStartPrereadMayCommit(preholdPreread->key,PreholdStartKey(preholdPreread->lease),
               preholdPreread->evidence,control,preholdPreread->lease.snapshot.config,Now()))return;
#endif
#ifdef FVR_BC2_PREHOLD_CLEANUP
        const auto priorPrehold=SnapshotPreholdOperation();
#endif
        accepted=requestCycle.Start(control,Now());
#ifdef FVR_BC2_PREHOLD_CLEANUP
        if(accepted&&priorPrehold)preholdOperationJournal.Archive(*priorPrehold,Now(),entry.Quiet());
#endif
        if(accepted){retiredFamily.reset();emptyControlReceipts.Clear();abortCleanup.Abandon(ReloadAbortFailure::NewCycle,Now());}
        if(!entry.Quiet()){requestCycle.Cancel();accepted=false;}
#ifdef FVR_BC2_PREHOLD_CLEANUP
        // Commit only the already-read immutable idle cohort after registration.
        // No native memory I/O occurs inside this callback exclusion.
        if(accepted&&entry.Quiet()&&!PreparePreholdMonitor(preholdPreread->evidence)){requestCycle.Cancel();accepted=false;}
#endif
        if(accepted){for(unsigned n=0;n<3;++n)requestTargets[n].store(control.identity.firing[n],std::memory_order_release);orphanStop.Publish({});}},true,nullptr,10);
    return locked&&accepted;
}
ReloadKeepAliveResult KeepAliveRequestCycleObserved(const ReloadCycleControl& control)noexcept {
    using Result=ReloadKeepAliveResult;
    if(!requestMode||!enabled.load(std::memory_order_acquire))return Result::Rejected;
    Active activeScope;if(!activeScope.requestEntryAllowed)return Result::Deferred;
    std::optional<Lease> lease;
    {OwnerCopyGate lock;if(!lock.held){++ownerDrops;return Result::Deferred;}
        if(IsCurrent(published,Now())&&published.server)lease=published;}
    if(!lease||RoundIdentity(*lease)!=control.identity||!RequestConfig(lease->snapshot.config)||
       !RequestTiming(lease->snapshot.config)||!RequestOwnerEvidence(*lease,Now())){
        RequestCancelAsync(4);return Result::Rejected;}
    bool accepted=false,deferred=false;
    const bool locked=WithRequest([&]{
        if(!IsCurrent(*lease,Now())||RoundIdentity(*lease)!=control.identity)return;
        accepted=requestCycle.KeepAlive(control,Now());},false,&deferred);
    return deferred?Result::Deferred:locked&&accepted?Result::Accepted:Result::Rejected;
}
bool KeepAliveRequestCycle(const ReloadCycleControl& control)noexcept {
    return KeepAliveRequestCycleObserved(control)==ReloadKeepAliveResult::Accepted;
}

std::optional<ReloadRoundLease> RequestLease(const ReloadHoldIdentity& wanted,std::uint64_t cycle)noexcept {
    const auto identity=RequestIdentityForObservation();if(!identity||*identity!=wanted)return {};
    Active activeScope;std::optional<ReloadRoundLease> lease;
    return WithRequest([&]{lease=requestCycle.Lease(wanted,cycle,Now());},false)?lease:std::nullopt;
}
bool SubmitRequest(const Bc2ReloadNativeRequest& request)noexcept {
    const auto identity=RequestIdentity();if(!identity||*identity!=request.heldLease.identity)return false;
    // Three-shell row on cf21d15: the seat's Submit may have landed while a copy's Update
    // hook was between its decision and its finish (lease unavailable,
    // spas_submit_failure 1) and the reload was cancelled before any credit.
    // That window and lock contention are transient: nothing was changed, so
    // retry for up to 3 ms with the lock released between attempts.
    Active activeScope;const auto begin=Now();
    for(unsigned attempt=0;;++attempt){bool accepted=false,deferred=false;
        const bool locked=WithRequest([&]{accepted=requestCycle.Submit(request,Now());},false,&deferred,5);
        if(locked&&accepted){if(attempt)++submitRetried;return true;}
        const bool transient=deferred||(locked&&requestCycle.SpasSubmitFailure()==9);
        if(!transient||Now()-begin>=3000000){if(transient)++submitRetryExpired;if(deferred)RequestCancelAsync(2);return false;}
        ++submitRetries;SwitchToThread();}
}
std::optional<Bc2ReloadAckEvidence> TakeRequestAcknowledgement(const ReloadHoldIdentity& wanted,std::uint64_t cycle)noexcept {
    const auto identity=RequestIdentityForObservation();if(!identity||*identity!=wanted)return {};
    Active activeScope;std::optional<Bc2ReloadAckEvidence> ack;
    const bool delivered=WithRequest([&]{ack=requestCycle.TakeAcknowledgement(wanted,cycle,Now());},false);
    if(!delivered&&ack){++requestUndeliveredAcknowledgements;requestLastUndelivered.store(ack->acknowledgement.semantic.request,std::memory_order_release);RequestCancelAsync();}
    return delivered?ack:std::nullopt;
}
MagazineCycleStartResult InspectMagazineRequestCycleStart(const ReloadHoldIdentity& wanted,std::uint64_t cycle,std::optional<ReloadMagazineStartupPulse> pulse)noexcept {
    using Result=MagazineCycleStartResult;
    if(!requestMode||!enabled.load(std::memory_order_acquire)||!cycle)return Result::Unknown;
    Active activeScope;if(!activeScope.requestEntryAllowed)return Result::Unknown;
    ReloadInvocationExclusion entry(requestEntryGate,active,callbackRevision);
    if(!entry.held||!entry.Quiet())return Result::Unknown;
    Result result=Result::Unknown;
    const bool locked=WithRequest([&]{result=InspectMagazineStartRegistration(entry.Quiet(),requestCycle.IsMagazine(),
        requestCycle.Identity(),requestCycle.Cycle(),wanted,cycle);
        if((result==Result::Started||result==Result::RegisteredCancelled)&&requestCycle.MagazineStartupPulse()!=pulse)result=Result::Unknown;},false,nullptr,5);
    return locked&&entry.Quiet()?result:Result::Unknown;
}
MagazineCycleStartResult StartMagazineRequestCycleObserved(const ReloadCycleControl& control,const interaction::ManualReloadRequest& unseat,std::optional<ReloadMagazineStartupPulse> pulse)noexcept {
    using Result=MagazineCycleStartResult;
    if(!requestCycle.IsMagazine())return Result::Unknown;
    if(pulse&&(!pulse->ValidFor(control)||pulse->endNs<=Now()))
        return InspectMagazineRequestCycleStart(control.identity,control.cycle,pulse);
    const auto identity=RequestIdentity();
    if(!identity||*identity!=control.identity)return InspectMagazineRequestCycleStart(control.identity,control.cycle,pulse);
#ifdef FVR_BC2_PREHOLD_CLEANUP
    const auto preholdPreread=ReadPreholdStartPreread(control);if(!preholdPreread)return Result::Unknown;
#endif
    Active activeScope;bool accepted=false,registered=false;Result rejected=Result::Unknown;
    ReloadInvocationExclusion entry(requestEntryGate,active,callbackRevision);
    if(!entry.held||!activeScope.requestEntryAllowed)return Result::Unknown;
    const bool locked=WithRequest([&]{
        if(!entry.Quiet())return;
        requestCycle.DrainCancelledInvocations(true);

#ifdef FVR_BC2_PREHOLD_CLEANUP
        if(!PreholdAllowsNewOperation()||!PreholdStartPublishedCurrent(*preholdPreread)||!IsCurrent(preholdPreread->lease,Now())||
           !RequestConfig(preholdPreread->lease.snapshot.config)||
           !PreholdStartPrereadMayCommit(preholdPreread->key,PreholdStartKey(preholdPreread->lease),
               preholdPreread->evidence,control,preholdPreread->lease.snapshot.config,Now()))return;
#endif
#ifdef FVR_BC2_PREHOLD_CLEANUP
        const auto priorPrehold=SnapshotPreholdOperation();
#endif
        registered=accepted=requestCycle.StartMagazine(control,unseat,Now(),pulse);
#ifdef FVR_BC2_PREHOLD_CLEANUP
        if(accepted&&priorPrehold)preholdOperationJournal.Archive(*priorPrehold,Now(),entry.Quiet());
#endif
        if(accepted)emptyControlReceipts.Clear();
        if(accepted){retiredFamily.reset();abortCleanup.Abandon(ReloadAbortFailure::NewCycle,Now());}
        else rejected=InspectMagazineStartRegistration(entry.Quiet(),requestCycle.IsMagazine(),
            requestCycle.Identity(),requestCycle.Cycle(),control.identity,control.cycle);
        if(!accepted&&(rejected==Result::Started||rejected==Result::RegisteredCancelled)&&requestCycle.MagazineStartupPulse()!=pulse)rejected=Result::Unknown;
        if(!entry.Quiet()){requestCycle.Cancel();accepted=false;}
#ifdef FVR_BC2_PREHOLD_CLEANUP
        // Commit only the already-read immutable idle cohort after registration.
        // No native memory I/O occurs inside this callback exclusion.
        if(accepted&&entry.Quiet()&&!PreparePreholdMonitor(preholdPreread->evidence)){requestCycle.Cancel();accepted=false;}
#endif
        if(accepted){for(unsigned n=0;n<3;++n)requestTargets[n].store(control.identity.firing[n],std::memory_order_release);orphanStop.Publish({});}
    },true,nullptr,11);
    if(registered)return locked&&accepted&&entry.Quiet()?Result::Started:Result::RegisteredCancelled;
    return locked&&entry.Quiet()?rejected:Result::Unknown;
}
bool StartMagazineRequestCycle(const ReloadCycleControl& control,const interaction::ManualReloadRequest& unseat)noexcept {
    return StartMagazineRequestCycleObserved(control,unseat)==MagazineCycleStartResult::Started;
}
ReloadMagazineLeaseObservation ObserveMagazineLease(const ReloadHoldIdentity& wanted,std::uint64_t cycle)noexcept {
    if(!requestMode||!enabled.load(std::memory_order_acquire))return {};
    const auto epoch=requestCancelEpoch.load(std::memory_order_acquire);
    bool deferred=false;const auto identity=RequestIdentityForObservation(&deferred);
    if(epoch!=requestCancelEpoch.load(std::memory_order_acquire))return {};
    if(!identity)return {deferred?ReloadMagazineObservationResult::Deferred:ReloadMagazineObservationResult::Rejected,{}};
    if(*identity!=wanted)return {};
    Active activeScope;if(!activeScope.requestEntryAllowed)return {};
    ReloadMagazineLeaseObservation observation;deferred=false;
    const bool delivered=WithRequest([&]{observation=requestCycle.ObserveMagazineLease(wanted,cycle,Now());},false,&deferred);
    if(epoch!=requestCancelEpoch.load(std::memory_order_acquire))return {};
    if(!delivered)return {deferred?ReloadMagazineObservationResult::Deferred:ReloadMagazineObservationResult::Rejected,{}};
    return observation;
}
std::optional<ReloadMagazineLease> RequestMagazineLease(const ReloadHoldIdentity& wanted,std::uint64_t cycle)noexcept {
    if(!requestCycle.IsMagazine())return {};
    const auto identity=RequestIdentityForObservation();if(!identity||*identity!=wanted)return {};
    Active activeScope;std::optional<ReloadMagazineLease> lease;
    return WithRequest([&]{lease=requestCycle.MagazineLease(wanted,cycle,Now());},false)?lease:std::nullopt;
}
std::optional<ReloadMagazineGateAcknowledgement> TakeMagazineUnseatAcknowledgement(const ReloadHoldIdentity& wanted,std::uint64_t cycle)noexcept {
    if(!requestCycle.IsMagazine())return {};
    const auto identity=RequestIdentityForObservation();if(!identity||*identity!=wanted)return {};
    Active activeScope;std::optional<ReloadMagazineGateAcknowledgement> ack;
    const bool delivered=WithRequest([&]{ack=requestCycle.TakeUnseatAcknowledgement(wanted,cycle,Now());},false);
    if(!delivered&&ack){++requestUndeliveredAcknowledgements;requestLastUndelivered.store(ack->semantic.request,std::memory_order_release);RequestCancelAsync();}
    return delivered?ack:std::nullopt;
}
bool SubmitMagazineRequest(const ReloadMagazineNativeRequest& request)noexcept {
    if(!requestCycle.IsMagazine())return false;
    const auto identity=RequestIdentity();if(!identity||*identity!=request.heldLease.identity)return false;
    Active activeScope;bool accepted=false;
    const bool locked=WithRequest([&]{accepted=requestCycle.SubmitMagazine(request,Now());},true);return locked&&accepted;
}
std::optional<ReloadMagazineAckEvidence> TakeMagazineAcknowledgement(const ReloadHoldIdentity& wanted,std::uint64_t cycle)noexcept {
    if(!requestCycle.IsMagazine())return {};
    const auto identity=RequestIdentityForObservation();if(!identity||*identity!=wanted)return {};
    Active activeScope;std::optional<ReloadMagazineAckEvidence> ack;
    const bool delivered=WithRequest([&]{ack=requestCycle.TakeMagazineAcknowledgement(wanted,cycle,Now());},false);
    if(!delivered&&ack){++requestUndeliveredAcknowledgements;requestLastUndelivered.store(ack->acknowledgement.semantic.request,std::memory_order_release);RequestCancelAsync();}
    return delivered?ack:std::nullopt;
}
void CancelRequestCycle()noexcept {
    if(!requestMode)return;Active activeScope;
    std::optional<Lease> owner;
    {OwnerCopyGate lock;if(lock.held&&IsCurrent(published,Now())&&published.server)owner=published;}
    WithRequest([&]{
        // One stop per cycle: ClearOwner calls this on every gather while the
        // owner is lost (run 175312: 48 restamps, 46 failed re-arms).
        if(requestCycle.Cycle()&&requestCycle.Cycle()==stopHandledCycle){++stopRepeats;requestCycle.Cancel(ReloadRequestCycleFailure::Stopped);return;}
        std::optional<ReloadRoundLease> held;
        if(activeScope.requestEntryAllowed&&owner&&IsCurrent(*owner,Now())&&
           requestCycle.Phase()==ReloadRequestCyclePhase::Holding&&!requestCycle.PendingRequest()&&!requestCycle.UnresolvedRequest()&&
           requestCycle.Identity()==RoundIdentity(*owner))held=requestCycle.AbortLease(requestCycle.Identity(),requestCycle.Cycle(),Now());
        const bool wasHolding=requestCycle.Phase()==ReloadRequestCyclePhase::Holding||requestCycle.Phase()==ReloadRequestCyclePhase::Arming;
        if(held)held->deadlineNs=std::min(held->deadlineNs,std::min(owner->deadline,owner->snapshot.observedNs+200000000ll));
        // Fail-closed stop: with no fresh all-held lease at this moment (stale
        // input, entry gate, owner gap), arm the abort from this cycle's last
        // all-held publication. TryAbort re-reads and verifies every native
        // field and the owner before the native abort, so only provenance is
        // re-stamped; releasing the hold instead refills the gun for free.
        if(!held)if(auto stop=requestCycle.StopLease(requestCycle.Identity(),requestCycle.Cycle())){
            const auto now=Now();stop->observedNs=now-1;stop->deadlineNs=now+150000000ll;held=stop;++abortRestamped;} // inside ArmWithCeiling's 200 ms span (runs 173124/173258: +200 ms+1 ns was rejected)
        std::optional<ReloadRoundLease> afterTransfer;
        if(!held)if(auto a=requestCycle.AfterTransferStopLease(requestCycle.Identity(),requestCycle.Cycle())){
            const auto now=Now();a->observedNs=now-1;a->deadlineNs=now+1400000000ll;afterTransfer=a;}
        if(held||afterTransfer||wasHolding||requestCycle.Phase()==ReloadRequestCyclePhase::Advancing)stopHandledCycle=requestCycle.Cycle();
        requestCycle.Cancel(ReloadRequestCycleFailure::Stopped);
        if(afterTransfer){const auto revision=owner?owner->revision:ownerRevision.load(std::memory_order_acquire);
            if(abortCleanup.ArmAfterTransfer(*afterTransfer,revision,Now(),combinedFamilies)){++abortArmed;++abortAfterTransfer;stopArmedAtNs.store(Now(),std::memory_order_release);}else ++abortArmFailures;}
        if(held){OrphanStop stop;const auto& id=requestCycle.Identity();stop.cycle=requestCycle.Cycle();stop.firing={id.firing[0],id.firing[1],id.firing[2]};
            if(const auto closed=ReadClosedIdentity()){stop.firingData=closed->firingData;stop.ammoAddress=closed->ammoAddress;}
            if(owner){stop.firingData=owner->snapshot.config.firingData;stop.ammoAddress=owner->snapshot.config.ammoAddress;}
            stop.loaded=held->loaded;stop.reserve=held->reserve;stop.untilNs=Now()+3000000000ll;
            if(stop.firingData&&stop.ammoAddress)orphanStop.Publish(stop);}
        if(held){const auto revision=owner?owner->revision:ownerRevision.load(std::memory_order_acquire);
            if(requestCycle.IsMagazine()?abortCleanup.ArmMagazine(*held,revision,Now(),true,requestCycle.MagazineProfile(),combinedFamilies):
                abortCleanup.Arm(*held,revision,Now(),true,ReloadAbortTiming::Spas,combinedFamilies)){++abortArmed;stopArmedAtNs.store(Now(),std::memory_order_release);}else ++abortArmFailures;}
        else if(wasHolding)++abortReleases;
    },true);
    RequestCancelAsync(6);WithRequest([]{},true);
}
std::optional<ReloadCycleRetirement> RetireRequestCycle(const ReloadHoldIdentity& wanted,std::uint64_t cycle)noexcept {
    ++retireAttempts;if(!requestMode||!enabled.load(std::memory_order_acquire)||!cycle)return {};
    Active activeScope;
    ReloadInvocationExclusion entry(requestEntryGate,active,callbackRevision);
    if(!entry.held||!activeScope.requestEntryAllowed){++retireBusy;return {};}
    std::optional<ReloadCycleRetirement> receipt;
    const auto quiet=[&]{return entry.Quiet()&&enabled.load(std::memory_order_acquire);};
    const bool locked=WithRequest([&]{
        if(requestCycle.Identity()!=wanted||requestCycle.Cycle()!=cycle){++retireWrongCycle;return;}
        // Exact old identity is sufficient. Resolving the current actor here
        // would make death/equip recovery impossible. Cancellation can stop an
        // outstanding decision, but retirement waits for proven callback drain.
        requestCycle.Cancel(ReloadRequestCycleFailure::Stopped);
        if(!quiet()){++retireUnquiet;return;}
        if(!CancelAndDrainReloadCycle(requestCycle,wanted,cycle,true)||!quiet()){++retireUnquiet;return;}
        receipt=retirementReceipts.Observe(requestCycle,wanted,cycle,Now(),true);
        if(receipt&&quiet())retiredFamily=RetiredFamily{requestCycle.Family(),wanted,cycle,familyEpoch,receipt->event};
    },true);
    // New arrivals failed the same entry gate and cannot acquire an old held
    // decision. Any arrival/revision/cancel race still withholds this receipt.
    if(!locked||!receipt||!quiet()){++retireDropped;return {};}
    retireLastEvent.store(receipt->event,std::memory_order_release);retireLastCycle.store(receipt->cycle,std::memory_order_release);
    retireLastObserved.store(receipt->observedNs,std::memory_order_release);retireLastDeadline.store(receipt->deadlineNs,std::memory_order_release);
    ++retireSuccesses;return receipt;
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t image,const ReloadStateMemory& reader,bool enableDiagnosticHold,bool enableDiagnosticRound,bool enableRequestCycle,bool enableMagazineRequests){
    if(installed||started||image>UINT32_MAX||!reader.read||!reader.type||unsigned(enableDiagnosticHold)+unsigned(enableDiagnosticRound)+unsigned(enableRequestCycle)+unsigned(enableMagazineRequests)>1)return false;
    const auto found=DiscoverReloadFlow(bytes,pe);if(!found||!ValidateReloadFlowLive(reader,*found,unsigned(image)))return false;
    LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0||f.QuadPart>1000000000)return false;
    binding=*found;memory=reader;base=unsigned(image);frequency=f.QuadPart;
    serverBinding=DiscoverReloadServer(bytes,pe);if(serverBinding&&!ValidateReloadServerLive(reader,*serverBinding,base))serverBinding.reset();
    if(enableDiagnosticHold||enableDiagnosticRound||enableRequestCycle||enableMagazineRequests){const auto proof=DiscoverReloadHoldCode(bytes,pe,binding);
        if(!serverBinding||!proof||!ValidateReloadHoldCodeLive(reader,*proof,base,binding.state.imageSize))return false;
        holdCodeVerified=true;diagnosticHold=enableDiagnosticHold;diagnosticRound=enableDiagnosticRound;requestMode=enableRequestCycle||enableMagazineRequests;
        if(enableMagazineRequests&&!requestCycle.ConfigureMagazine())return false;
        // Step 2 (server authority): FVR_BC2_COPY_AUTHORITY=1 keeps client1
        // (+0x40) held but non-critical; =2 makes it observe-only. Any request
        // mode: the physical stack runs magazines on enableRequestCycle.
        if(requestMode)if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_RELOAD_MAILBOX",value,sizeof(value))&&value[0]=='1')mailboxMode=true;
        if(requestMode)if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_COPY_AUTHORITY",value,sizeof(value))&&(value[0]=='1'||value[0]=='2'))
            requestCycle.SetMagazineCopyAuthority(5u,value[0]=='2');
        if(requestMode){abortCode=*proof;nativeAbort=reinterpret_cast<AbortFn>(base+(*proof)[1].rva);}
        // Fail-closed empty gate is the default in request mode; FVR_BC2_EMPTY_GATE_FAILCLOSED=0 restores the lease-only gate.
        if(requestMode){closedEmptyGate=true;if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_EMPTY_GATE_FAILCLOSED",value,sizeof(value))&&value[0]=='0')closedEmptyGate=false;}
    }
    const std::array<unsigned,5> rvas{binding.code[0].rva,binding.state.code[3].rva,binding.state.code[4].rva,binding.code[6].rva,binding.state.code[1].rva};
    const std::array<void*,5> callbacks{reinterpret_cast<void*>(Update),reinterpret_cast<void*>(Commit),reinterpret_cast<void*>(Transfer),reinterpret_cast<void*>(Restore),reinterpret_cast<void*>(Step)};
    const std::array<void**,5> originals{reinterpret_cast<void**>(&originalUpdate),reinterpret_cast<void**>(&originalCommit),reinterpret_cast<void**>(&originalTransfer),reinterpret_cast<void**>(&originalRestore),reinterpret_cast<void**>(&originalStep)};
    for(unsigned n=0;n<hooks.size();++n){void* at=reinterpret_cast<void*>(base+rvas[n]);
        if(MH_CreateHook(at,callbacks[n],originals[n])!=MH_OK){for(auto& hook:hooks)if(hook){MH_RemoveHook(hook);hook=nullptr;}return false;}hooks[n]=at;}
    installed=true;return true;
}
std::optional<ReloadPreholdEntryObservation> ReadPreholdEntry(const ReloadHoldIdentity& identity,std::uint64_t cycle,std::int64_t now)noexcept {
#ifdef FVR_BC2_PREHOLD_CLEANUP
    std::optional<ReloadPreholdEntryObservation> result;
    if(!enabled.load(std::memory_order_acquire))return {};
    WithRequest([&]{
        if(!preholdEntryObservation||requestCycle.Identity()!=identity||requestCycle.Cycle()!=cycle||
           !FreshPreholdEntry(*preholdEntryObservation,identity,cycle,now))return;
        result=preholdEntryObservation;result->phase=requestCycle.Phase();
    },false,nullptr,1);return result;
#else
    (void)identity;(void)cycle;(void)now;return {};
#endif
}
OwnerPublicationResult PublishOwnerObserved(const ReloadStateSnapshot& snapshot,std::int64_t deadline)noexcept{
    using Status=OwnerPublicationStatus;using Reason=OwnerPublicationReason;
    const auto now=Now();if(!enabled.load(std::memory_order_acquire)||now<startNs||(!requestMode&&now-startNs>=20000000000ll)||
       !OwnerValid(snapshot.owner)||!snapshot.sequence||snapshot.observedNs<=0||now<snapshot.observedNs||now>=deadline||
       deadline<=snapshot.observedNs||deadline-snapshot.observedNs>250000000){if(requestMode)RequestCancelAsync(5);return {Status::Rejected,Reason::DisabledOrInvalidSource};}
    std::optional<ReloadServerSnapshot> server;
    if(serverBinding){++serverPublishAttempts;server=ReadReloadServerState(memory,*serverBinding,binding.state,base,snapshot);if(!server)++serverPublishMisses;}
    auto revision=ownerRevision.load(std::memory_order_acquire);
    OwnerCopyGate gate;const auto acquiredNow=Now();
    const bool knownServerFailure=requestMode&&!server;
    const auto copy=AdmitOwnerPublicationCopy(gate.held,snapshot.observedNs,deadline,acquiredNow,knownServerFailure);
    if(copy!=OwnerCopyAdmission::Acquired){
        if(!gate.held)++ownerDrops;
        if(requestMode&&OwnerCopyUnavailableCancels(copy))RequestCancelAsync(5);
        return copy==OwnerCopyAdmission::Deferred?OwnerPublicationResult{Status::Deferred,Reason::OwnerGateBusy}:
            OwnerPublicationResult{Status::Rejected,knownServerFailure?Reason::ServerReadFailure:Reason::SourceExpired};
    }
    if(snapshot.observedNs<published.snapshot.observedNs)return {Status::Superseded,Reason::OlderSnapshot};
    if(revision!=ownerRevision.load(std::memory_order_acquire))return {Status::Superseded,Reason::RevisionChanged};
    if(combinedFamilies&&!FindMagazineNativeProfile(snapshot.config))
        magazineProfileRejection=MagazineProfileRejection{snapshot,bool(server),requestMode};
    Reason reason=Reason::Published;
    if(published.snapshot.owner!=snapshot.owner||published.snapshot.branches[0].address!=snapshot.branches[0].address||
       published.snapshot.branches[1].address!=snapshot.branches[1].address){
        if(requestMode)RequestCancelAsync(5);reason=Reason::NativeOwnerChanged;
        const auto next=revision+1;
        if(!ownerRevision.compare_exchange_strong(revision,next,std::memory_order_acq_rel))return {Status::Superseded,Reason::RevisionChanged};
        revision=next;
    }
    // Server optional failure remains conservatively invalidating: this reader
    // has no typed mutable-cohort-vs-structural-loss proof. Never borrow old server.
    if(requestMode&&!server){RequestCancelAsync(5);reason=Reason::ServerReadFailure;}
    else if(requestMode&&published.server&&published.server->links.firing!=server->links.firing){RequestCancelAsync(5);reason=Reason::ServerOwnerChanged;}
    // ClearOwner retains its independent barrier even after this assignment.
    published={snapshot,deadline,revision,std::move(server)};
    if(published.server){localServerInventory.store(published.server->links.inventory,std::memory_order_release);localServerFiring.store(published.server->links.firing,std::memory_order_release);localServerInventoryTick.store(GetTickCount64(),std::memory_order_release);}
    if(closedEmptyGate){auto next=closedIdentity;
        // A failed server read keeps the same weapon's last server copy.
        if(next.copies[0]!=snapshot.branches[0].address||next.copies[1]!=snapshot.branches[1].address)next.copies[2]=0;
        next.copies[0]=snapshot.branches[0].address;next.copies[1]=snapshot.branches[1].address;
        if(published.server)next.copies[2]=published.server->links.firing;next.observedNs=snapshot.observedNs;
        next.firingData=snapshot.config.firingData;next.ammoAddress=snapshot.config.ammoAddress;
        // F2: support of the gun in hand, independent of cycle selection.
        const auto* registered=FindMagazineNativeProfile(snapshot.config);
        next.supported=ManualEmptyFamilyConfig(ReloadNativeFamily::SpasTube,NativeMagazineProfileId::ScopedXm8,snapshot.config)||
            (registered&&registered->enabled&&ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,registered->id,snapshot.config));
        const auto s=closedIdentitySeq.load(std::memory_order_relaxed);closedIdentitySeq.store(s+1,std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);closedIdentity=next;
        closedIdentitySeq.store(s+2,std::memory_order_release);}
    if(revision!=ownerRevision.load(std::memory_order_acquire))return {Status::Superseded,Reason::ClearBarrier};
    return {Status::Published,reason};
}
bool PublishOwner(const ReloadStateSnapshot& snapshot,std::int64_t deadline)noexcept {
    return PublishOwnerObserved(snapshot,deadline).status==OwnerPublicationStatus::Published;
}
unsigned AbortArmedCount()noexcept{return abortArmed.load(std::memory_order_acquire);}
std::uint32_t LocalServerFiring()noexcept{const auto at=localServerInventoryTick.load(std::memory_order_acquire);
    return at&&GetTickCount64()-at<2000?localServerFiring.load(std::memory_order_acquire):0u;}
std::uint32_t LocalServerInventory()noexcept{const auto at=localServerInventoryTick.load(std::memory_order_acquire);
    return at&&GetTickCount64()-at<2000?localServerInventory.load(std::memory_order_acquire):0u;}
void ClearOwner()noexcept{ownerRevision.fetch_add(1,std::memory_order_acq_rel);pumpGate.OwnerCleared();holdProbe.Abort(ReloadHoldReason::OwnerOrRead);
    if(requestMode)WithRequest([]{emptyControlReceipts.Clear();abortCleanup.Abandon(ReloadAbortFailure::Owner,Now());},true);
    if(requestMode)CancelRequestCycle();
    if(diagnosticRound){Gate lock(roundGateLock);
        if(!lock.held){roundCancelled.store(true,std::memory_order_release);return;}
        const auto phase=roundGate.Phase();
        if(phase==ReloadRoundGatePhase::FirstHold||phase==ReloadRoundGatePhase::Advancing||phase==ReloadRoundGatePhase::SecondHold){
            roundGate.Cancel(ReloadRoundGateFailure::OwnerOrRead);roundCancelled.store(true,std::memory_order_release);}
    }
}
// SPAS physical pump (Bc2PumpHold.h). Request mode only; off by default.
bool EnablePumpHold(bool on)noexcept{if(!installed||!requestMode||pumpDiagnostic)return false;pumpGate.Enable(on);return true;}
void PumpReloadMailbox()noexcept {
    if(!installed||!mailboxMode||!requestMode||!requestCycle.IsMagazine())return;
    std::vector<MailboxReceipt> receipts;receipts.reserve(48);
    for(unsigned n=0;n<3;++n)mailboxRings[n].Drain(mailboxCursor[n],[&](const MailboxReceipt& r){receipts.push_back(r);},&mailboxLost);
    std::stable_sort(receipts.begin(),receipts.end(),[](const auto& a,const auto& b){return a.nowNs<b.nowNs;});
    ReloadCommandBoardState board;
    const bool locked=WithRequest([&]{
        requestCycle.BeginMagazineReplay();
        for(const auto& r:receipts){const auto b=r.input.branch;if(b>=3)continue;
            mailboxTimer[b]=r.input.branches[b].phaseTimer;mailboxWaiting[b]=r.input.branches[b].currentState==11;
            if(r.finished){mailboxTimer[b]=r.after.phaseTimer;mailboxWaiting[b]=r.after.currentState==11;}
            if(Now()-r.nowNs>2*ReloadHoldProbe::ContextFreshNs){++mailboxStale;continue;}
            ++mailboxReplays;
            const auto d=requestCycle.Evaluate(r.input,r.timing,r.coherent,r.update,r.nowNs);
            if(!d.tracked)continue;
            if(r.transfer&&requestCycle.Transfer(*r.transfer,r.transferParent)){++requestTransferCounts[b];requestLastTransfer[b]=r.transfer->invocation;}
            const auto wanted=d.hold?ReloadCommandKind::Hold:d.advanceSeconds>0?ReloadCommandKind::AdvanceOnce:ReloadCommandKind::None;
            if(r.applied!=ReloadCommandKind::None&&r.applied!=wanted){requestCycle.ReleaseMagazine(d);++mailboxReleases;continue;}
            if(wanted==ReloadCommandKind::Hold&&r.applied==ReloadCommandKind::None){requestCycle.ReleaseMagazine(d);++mailboxReleases;continue;}
            if(!r.finished){requestCycle.Finish(d,{},r.nowNs,false,r.delta);continue;}
            requestCycle.Finish(d,r.after,r.afterNs,r.retained,r.delta,r.afterNs);
        }
        requestCycle.EndMagazineReplay();
        const auto phase=requestCycle.Phase();const auto& id=requestCycle.Identity();const auto cycle=requestCycle.Cycle();const auto now=Now();
        if(cycle&&(phase==ReloadRequestCyclePhase::Holding||phase==ReloadRequestCyclePhase::Advancing)){
            board.owner.key=cycle;for(unsigned n=0;n<3;++n)board.owner.firing[n]=id.firing[n];
            for(unsigned n=0;n<3;++n){auto& c=board.copies[n];c.cycle=cycle;c.expiresNs=now+80000000ll;
                if(phase==ReloadRequestCyclePhase::Holding)c.kind=ReloadCommandKind::Hold;
                else if(!requestCycle.MagazineAdvanced(n)&&mailboxWaiting[n]&&mailboxTimer[n]>0){
                    c.kind=ReloadCommandKind::AdvanceOnce;c.serial=cycle*4+n+1;c.advanceSeconds=mailboxTimer[n]+.001f;}}}
    },true);
    if(!locked){++mailboxPumpBusy;return;}
    mailboxBoard.Publish(board);++mailboxPublishes;
}
bool StopPending(std::int64_t windowNs)noexcept{const auto at=stopArmedAtNs.load(std::memory_order_acquire);const auto now=Now();return at>0&&now>=at&&now-at<windowNs;}
void GrantStockReload(std::int64_t windowNs)noexcept{if(!installed||!requestMode||windowNs<=0||windowNs>1000000000)return;
    stockTokenUntilNs.store(Now()+windowNs,std::memory_order_release);++stockTokenGrants;orphanStop.Publish({});}
bool PumpOwed()noexcept{return pumpGate.Owed();}
bool PumpExpected()noexcept{return pumpGate.Expected();}
bool RackPump()noexcept{return pumpGate.Rack();}
bool EnablePumpHoldDiagnostic()noexcept {
    if(!installed||started||enabled.load()||!diagnosticHold||diagnosticRound||requestMode||!holdCodeVerified)return false;
    pumpDiagnostic=true;return true;
}
void Start()noexcept{if(!installed||started)return;startNs=Now();if(startNs<=0)return;started=true;if(diagnosticHold)holdProbe.Enable(pumpDiagnostic?ReloadHoldTarget::SpasPump:ReloadHoldTarget::Reload);if(diagnosticRound)roundGate.Enable();enabled.store(true,std::memory_order_release);}
bool Stop()noexcept{
    enabled.store(false,std::memory_order_release);holdProbe.Stop();ClearOwner();bool okay=true;
    for(auto hook:hooks)if(hook){const auto status=MH_DisableHook(hook);okay&=status==MH_OK||status==MH_ERROR_DISABLED;}
    const auto until=GetTickCount64()+2000;while(active.load(std::memory_order_acquire)&&GetTickCount64()<until)Sleep(1);
    drained=okay&&!active.load(std::memory_order_acquire);return drained;
}
void Report(std::ostream& out){
    // A drained report owns the same gate for both completion flush and rows.
    // A competing report cannot mutate records beneath an existing reader.
    std::optional<Gate> reportRecordsGate;
    if(drained){reportRecordsGate.emplace(recordGate);if(!reportRecordsGate->held){
        out<<"{\"drained\":true,\"records_unavailable_until_drain\":true,\"completion_report_lock_busy\":true}";return;}
        recordCompletions.Drain(records);
    }
    std::optional<MagazineProfileRejection> descriptorRejection;
    {OwnerCopyGate gate;if(gate.held)descriptorRejection=magazineProfileRejection;}
    out<<"{\"magazine_profile_rejection\":";
    if(!descriptorRejection)out<<"null";
    else {
        const auto& r=*descriptorRejection;const auto& s=r.snapshot;const auto& c=s.config;
        const auto text=[&](const auto& chars){
            constexpr char hex[]="0123456789abcdef";out<<'"';
            for(unsigned char ch:chars){if(!ch)break;
                if(ch=='"'||ch=='\\')out<<'\\'<<char(ch);
                else if(ch<32)out<<"\\u00"<<hex[ch>>4]<<hex[ch&15];else out<<char(ch);
            }out<<'"';
        };
        out<<"{\"reason\":\"no_exact_descriptor\",\"source\":\"coherent_owner_publication\",\"sequence\":"<<s.sequence
           <<",\"observed_ns\":"<<s.observedNs<<",\"server_known\":"<<(r.serverKnown?"true":"false")
           <<",\"request_mode\":"<<(r.requestEnabled?"true":"false")<<",\"asset\":";text(c.assetName);
        out<<",\"asset_path\":";text(c.assetPath);
        out<<",\"weapon_data\":"<<c.weaponData<<",\"firing_data\":"<<c.firingData<<",\"primary_fire\":"<<c.primaryFire
           <<",\"ammo_address\":"<<c.ammoAddress<<",\"fire_logic\":"<<c.fireLogicType<<",\"reload_type\":"<<c.reloadType
           <<",\"fire_action\":"<<c.fireInputAction<<",\"reload_action\":"<<c.reloadInputAction
           <<",\"base_capacity\":"<<c.baseCapacity<<",\"number_of_magazines\":"<<c.numberOfMagazines
           <<",\"reload_delay\":"<<c.reloadDelay<<",\"reload_time\":"<<c.reloadTime<<",\"reload_threshold\":"<<c.reloadThreshold
           <<",\"post_reload_time\":"<<c.postReloadTime<<",\"bolt_delay\":"<<c.boltDelay<<",\"bolt_time\":"<<c.boltTime
           <<",\"timing_bits\":["<<std::bit_cast<std::uint32_t>(c.reloadDelay)<<','<<std::bit_cast<std::uint32_t>(c.reloadTime)
           <<','<<std::bit_cast<std::uint32_t>(c.reloadThreshold)<<','<<std::bit_cast<std::uint32_t>(c.postReloadTime)
           <<','<<std::bit_cast<std::uint32_t>(c.boltDelay)<<','<<std::bit_cast<std::uint32_t>(c.boltTime)<<']'
           <<",\"hold_bolt_fire\":"<<(c.holdBoltUntilFireRelease?"true":"false")
           <<",\"hold_bolt_zoom\":"<<(c.holdBoltUntilZoomRelease?"true":"false")
           <<",\"authored_state_count\":"<<unsigned(c.authoredStateCount)<<",\"authored_pump_handling\":[";
        for(std::size_t n=0;n<c.authoredPumpHandling.size();++n){if(n)out<<',';out<<(c.authoredPumpHandling[n]?"true":"false");}
        out<<"]}";
    }
    out<<",\"reserve_read_evidence\":";reserveReadEvidence.Report(out,drained);
    out<<",\"installed\":"<<(installed?"true":"false")<<",\"started\":"<<(started?"true":"false")<<",\"drained\":"<<(drained?"true":"false")
       <<",\"observation_only\":"<<(!(diagnosticHold||diagnosticRound||requestMode)?"true":"false")<<",\"native_state_writes\":"<<((holdApplied[0].load()+holdApplied[1].load()+holdApplied[2].load()+abortCalls.load()+emptyApplied[0].load()+emptyApplied[1].load()+emptyApplied[2].load())?"true":"false")
       <<",\"combined_reload_families\":"<<(combinedFamilies?"true":"false")<<",\"family_epoch\":"<<familyEpoch.load()
       <<",\"family_select_attempts\":"<<familySelectAttempts.load()<<",\"family_select_changes\":"<<familySelectChanges.load()<<",\"family_select_rejected\":"<<familySelectRejected.load()
       <<",\"family_retirement_reuses\":"<<familyRetirementReuses.load()
       <<",\"family_select_failures\":{\"disabled\":"<<familySelectFailures[0].load()<<",\"entry\":"<<familySelectFailures[1].load()
       <<",\"owner_lock\":"<<familySelectFailures[2].load()<<",\"owner_missing\":"<<familySelectFailures[3].load()
       <<",\"config\":"<<familySelectFailures[4].load()<<",\"timing\":"<<familySelectFailures[5].load()<<",\"owner_evidence\":"<<familySelectFailures[6].load()
       <<",\"exclusion\":"<<familySelectFailures[7].load()<<",\"policy_or_lock\":"<<familySelectFailures[8].load()<<",\"revision\":"<<familySelectFailures[9].load()<<'}'
       <<",\"reserve_read_stages\":{\"disabled\":"<<reserveReadStages[0].load()<<",\"entry\":"<<reserveReadStages[1].load()
       <<",\"owner_lock\":"<<reserveReadStages[2].load()<<",\"owner_missing\":"<<reserveReadStages[3].load()<<",\"config\":"<<reserveReadStages[4].load()
       <<",\"boundary_or_capacity\":"<<reserveReadStages[5].load()<<",\"cohort\":"<<reserveReadStages[6].load()<<",\"expired_or_changed\":"<<reserveReadStages[7].load()
       <<",\"sequence\":"<<reserveReadStages[8].load()<<",\"accepted\":"<<reserveReadStages[9].load()<<'}'
       <<",\"reserve_quiet\":{\"retries\":"<<reserveQuietRetries.load()<<",\"wins\":"<<reserveQuietWins.load()<<",\"timeouts\":"<<reserveQuietTimeouts.load()<<'}'
       <<",\"magazine_request_profile\":"<<std::uint64_t(requestCycle.MagazineProfileId())
       <<",\"magazine_request_mode\":"<<(requestCycle.IsMagazine()?"true":"false")
       <<",\"native_gate_enabled\":"<<(requestMode?"true":"false")<<",\"authority_proven\":false,\"headset_verified\":false"
       <<",\"server_binding_verified\":"<<(serverBinding?"true":"false")<<",\"server_publish_attempts\":"<<serverPublishAttempts.load()
       <<",\"server_publish_misses\":"<<serverPublishMisses.load()<<",\"server_read_misses\":"<<serverReadMisses.load()
       <<",\"server_matched\":["<<serverMatches[0].load()<<','<<serverMatches[1].load()<<','<<serverMatches[2].load()<<','<<serverMatches[3].load()<<']'
       <<",\"capacity\":"<<ReloadFlowRecords::Capacity<<",\"window_seconds\":20,\"start_ns\":"<<startNs<<",\"in_flight\":"<<active.load()
       <<",\"owner_lock_drops\":"<<ownerDrops.load()<<",\"record_lock_drops\":"<<recordBusy.load()
       <<",\"record_begin_lock_drops\":"<<recordBeginBusy.load()<<",\"record_end_lock_drops\":"<<recordEndBusy.load()
       <<",\"completion_journal\":{\"capacity\":"<<ReloadDeferredCompletions::Capacity<<",\"pending\":"<<recordCompletions.Pending()
       <<",\"recovered\":"<<recordCompletions.Recovered()<<",\"overflow\":"<<recordCompletions.Overflow()<<",\"rejected_drain\":"<<recordCompletions.Rejected()<<'}'
       <<",\"owner_misses\":"<<ownerMisses.load()
       <<",\"read_misses\":"<<readMisses.load()<<",\"context_misses\":"<<contextMisses.load()<<",\"nesting_misses\":"<<nestingMisses.load()
       <<",\"window_expired_calls\":"<<windowExpired.load()<<",\"calls\":["<<calls[0].load()<<','<<calls[1].load()<<','<<calls[2].load()<<','<<calls[3].load()
       <<"],\"matched\":["<<matches[0].load()<<','<<matches[1].load()<<','<<matches[2].load()<<','<<matches[3].load()<<']';
    const auto empty=ReadMagazineEmptyControlCounters();
    out<<",\"entry_gate\":{\"waits\":"<<entryWaits.load()<<",\"wait_wins\":"<<entryWaitWins.load()<<",\"denied\":"<<entryDenied.load()<<'}';
    out<<",\"closed_empty_gate\":{\"enabled\":"<<(closedEmptyGate?"true":"false")<<",\"applied\":["<<closedApplied[0].load()<<','<<closedApplied[1].load()<<','<<closedApplied[2].load()
       <<"],\"restore_failures\":"<<closedRestoreFailures.load()<<",\"token_passes\":"<<closedTokenPasses.load()<<",\"unsupported_passes\":"<<closedUnsupportedPasses.load()
       <<",\"identity_misses\":"<<closedIdentityMisses.load()<<",\"context_misses\":"<<closedContextMisses.load()
       <<",\"state_misses\":"<<closedStateMisses.load()<<",\"patch_failures\":"<<closedPatchFailures.load()<<",\"stock_fallbacks\":"<<stockTokenGrants.load()<<'}';
    {const auto a=requestCycle.MagazineCopyAuthority();
     out<<",\"mailbox\":{\"enabled\":"<<(mailboxMode?"true":"false")<<",\"replays\":"<<mailboxReplays<<",\"releases\":"<<mailboxReleases
        <<",\"stale\":"<<mailboxStale<<",\"lost\":"<<mailboxLost<<",\"publishes\":"<<mailboxPublishes<<",\"pump_busy\":"<<mailboxPumpBusy
        <<",\"holds\":["<<mailboxHolds[0].load()<<','<<mailboxHolds[1].load()<<','<<mailboxHolds[2].load()
        <<"],\"advances\":["<<mailboxAdvances[0].load()<<','<<mailboxAdvances[1].load()<<','<<mailboxAdvances[2].load()<<"]}";
     out<<",\"cancel_stop\":{\"restamped\":"<<abortRestamped<<",\"arm_failures\":"<<abortArmFailures<<",\"releases\":"<<abortReleases<<",\"after_transfer\":"<<abortAfterTransfer<<",\"repeats\":"<<stopRepeats
        <<",\"orphan_calls\":"<<orphanCalls.load()<<",\"orphan_exact\":"<<orphanExact.load()<<",\"orphan_rejects\":"<<orphanRejects.load()<<",\"orphan_deferred\":"<<orphanDeferred.load()<<'}';
     out<<",\"copy_authority\":{\"critical\":"<<a.critical<<",\"observe_only\":"<<(a.observeOnly?"true":"false")<<",\"tolerated\":"<<a.tolerated
        <<",\"native_resyncs\":"<<a.resyncs<<",\"observed_transfers\":"<<a.observedTransfers<<'}';}
    {const unsigned total=tokenPassCount.load();out<<",\"token_passes\":{\"total\":"<<total<<",\"rows\":[";
        for(unsigned n=0;n<std::min<unsigned>(total,unsigned(tokenPasses.size()));++n){const auto& r=tokenPasses[n];if(n)out<<',';
            out<<"{\"now_ns\":"<<r.nowNs<<",\"copy\":"<<r.branch<<",\"stage\":"<<r.stage<<",\"token_age_ns\":"<<r.ageNs<<",\"input\":"<<r.input<<",\"gate\":\""<<(r.gate==1?"closed":r.gate==2?"unsupported":r.gate==3?"stock":"lease")<<"\"}";}
        out<<"]}";}
    {const unsigned total=emptyLeakCount.load();out<<",\"empty_gate_leaks\":{\"total\":"<<total<<",\"rows\":[";
        const unsigned begin=total>emptyLeaks.size()?total-unsigned(emptyLeaks.size()):0;
        for(unsigned n=begin;n<total;++n){const auto& e=emptyLeaks[n%emptyLeaks.size()];if(n!=begin)out<<',';
            out<<"{\"now_ns\":"<<e.nowNs<<",\"branch\":"<<e.branch<<",\"stage\":"<<e.stage<<",\"mask\":"<<e.mask<<",\"phase\":"<<e.phase<<",\"input\":"<<e.input<<",\"flags_a8\":"<<e.flags<<'}';}
        out<<"]}";}
    out<<",\"empty_magazine_control\":{\"requested\":["<<empty.requested[0]<<','<<empty.requested[1]<<','<<empty.requested[2]
       <<"],\"applied\":["<<empty.applied[0]<<','<<empty.applied[1]<<','<<empty.applied[2]
       <<"],\"restored\":["<<empty.restored[0]<<','<<empty.restored[1]<<','<<empty.restored[2]
       <<"],\"patch_failures\":"<<empty.patchFailures<<",\"restore_failures\":"<<empty.restoreFailures
       <<",\"receipt_failures\":"<<empty.receiptFailures<<'}';
    out<<",\"empty_step_diagnostic\":{\"schema\":1,\"capacity\":"<<MagazineEmptyDiagnosticJournal::Capacity
       <<",\"diagnostic_only\":true,\"drained\":"<<(drained?"true":"false")
       <<",\"lock_drops\":"<<emptyDiagnosticLockDrops.load()<<",\"publisher_lock_drops\":"<<interactionDiagnosticLockDrops.load()
       <<",\"stages\":[\"boundary\",\"owner_or_context\",\"owner_evidence\",\"firing_read\",\"policy\",\"eligibility\",\"profile_or_timing\",\"policy_lock\",\"adjacent_recheck\",\"applied\",\"patch_failed\"],\"counts\":[";
    if(drained){for(unsigned n=0;n<unsigned(MagazineEmptyDiagnosticStage::Count);++n){if(n)out<<',';out<<emptyDiagnosticJournal.Counts()[n];}}
    out<<"],\"observed\":"<<(drained?emptyDiagnosticJournal.Observed():0)<<",\"overwritten\":"<<(drained?emptyDiagnosticJournal.Overwritten():0)<<",\"unowned_skipped\":"<<(drained?emptyDiagnosticJournal.UnownedSkipped():0)<<",\"last_known_attributed\":"<<(drained?emptyDiagnosticJournal.LastKnownAttributed():0)<<",\"salient_retained\":"<<(drained?emptyDiagnosticJournal.SalientSize():0)<<",\"salient_dropped\":"<<(drained?emptyDiagnosticJournal.SalientDropped():0)<<",\"salient_boundary_dropped\":"<<(drained?emptyDiagnosticJournal.BoundaryDropped():0)<<",\"salient_transition_dropped\":"<<(drained?emptyDiagnosticJournal.TransitionDropped():0)<<",\"rows\":[";
    const auto diagnosticState=[&](const ReloadFiringObservation& b){out<<"{\"firing\":"<<b.address
       <<",\"current\":"<<b.currentState<<",\"next\":"<<b.nextState<<",\"loaded\":"<<b.loaded
       <<",\"reserve\":"<<b.reserve<<",\"timer\":"<<b.phaseTimer<<",\"flags_a8\":"<<unsigned(b.flagsA8)<<'}';};
    if(drained)for(std::size_t n=0;n<emptyDiagnosticJournal.Size();++n){if(n)out<<',';const auto& r=emptyDiagnosticJournal.Row(n);const auto& s=r.sample;const auto& o=s.identity.owner;
       out<<"{\"stage\":"<<unsigned(r.stage)<<",\"now_ns\":"<<r.nowNs<<",\"input_sequence\":"<<s.sourceSequence
          <<",\"owner_revision\":"<<s.ownerRevision<<",\"observed_ns\":"<<s.observedNs<<",\"deadline_ns\":"<<s.deadlineNs
          <<",\"branch\":"<<s.branch<<",\"profile\":"<<std::uint64_t(s.profile)<<",\"family_known\":"<<(r.familyKnown?"true":"false")<<",\"family\":"<<r.family<<",\"phase\":"<<r.phase

          <<",\"boundary_reason_known\":"<<(r.boundaryReasonKnown?"true":"false")<<",\"boundary_reason\":"<<r.boundaryReason
          <<",\"step_delta_bits\":"<<r.stepDeltaBits<<",\"raw_context_known\":"<<(r.rawContextKnown?"true":"false")
          <<",\"context_delta_bits\":"<<r.contextDeltaBits<<",\"context_multiplier_bits\":"<<r.contextMultiplierBits<<",\"context_input_flags\":"<<r.contextInputFlags
          <<",\"context_flag_bytes\":[";for(unsigned k=0;k<5;++k){if(k)out<<',';out<<r.rawContextFlags[k];}out<<']'
          <<",\"owner\":["<<o.player<<','<<o.soldier<<','<<o.weak<<','<<o.weapon<<','<<o.actorGeneration<<','<<o.equipGeneration<<','<<o.space<<']'
          <<",\"requested\":"<<(r.requested?"true":"false")<<",\"applied\":"<<(r.applied?"true":"false")<<",\"restored\":"<<(r.restored?"true":"false")
          <<",\"lease_invalid_last_known\":"<<(r.leaseInvalidLastKnown?"true":"false")<<",\"owner_retained\":"<<(r.ownerRetained?"true":"false")<<",\"before\":";
       if(r.hasBefore)diagnosticState(s.before);else out<<"null";
       out<<",\"after\":";if(r.hasAfter)diagnosticState(r.after);else out<<"null";
       out<<",\"context\":";if(r.hasContext){out<<"{\"delta\":"<<r.context.deltaSeconds<<",\"reload_multiplier\":"<<r.context.reloadTimeMultiplier
          <<",\"input_flags\":"<<r.context.inputFlags<<",\"flags_24_28\":[";
          for(unsigned k=0;k<5;++k){if(k)out<<',';out<<(r.context.flags24Through28[k]?1:0);}out<<"]}";}else out<<"null";
       out<<",\"interaction\":";if(r.hasInteraction){const auto& i=r.interaction;out<<"{\"input\":"<<i.sequence
          <<",\"observed_ns\":"<<i.observedNs<<",\"deadline_ns\":"<<i.deadlineNs<<",\"support_holding\":"<<(i.supportHolding?"true":"false")
          <<",\"owns_left_hand\":"<<(i.ownsLeftHand?"true":"false")<<",\"blocks_weapon_actions\":"<<(i.blocksWeaponActions?"true":"false")
          <<",\"magazine_phase\":"<<i.magazinePhase<<'}';}else out<<"null";out<<'}';
    }out<<"]}";
    out<<",\"phase_retry\":{\"max_extra_reads\":1,\"paths\":[\"client_boundary\",\"server_boundary\",\"client_owner\",\"server_owner\"],\"attempts\":[";
    for(unsigned n=0;n<4;++n){if(n)out<<',';out<<phaseRetryAttempts[n].load();}out<<"],\"recovered\":[";
    for(unsigned n=0;n<4;++n){if(n)out<<',';out<<phaseRetryRecovered[n].load();}out<<"],\"failed\":[";
    for(unsigned n=0;n<4;++n){if(n)out<<',';out<<phaseRetryFailed[n].load();}out<<"],\"first_attempts\":[";
    if(drained)for(unsigned n=0;n<std::min<unsigned>(phaseRetrySampleCount.load(),unsigned(phaseRetrySamples.size()));++n){
        if(n)out<<',';const auto& s=phaseRetrySamples[n];const auto& a=s.attempt;
        out<<"{\"path\":"<<unsigned(s.path)<<",\"firing\":"<<s.firing<<",\"sequence\":"<<s.sequence<<",\"deadline_ns\":"<<s.deadline
           <<",\"first_ns\":"<<a.firstNs<<",\"retry_ns\":"<<a.retryNs<<",\"recovered\":"<<(a.recovered?"true":"false")
           <<",\"clock_rejected\":"<<(a.clockRejected?"true":"false")
           <<",\"first\":["<<a.first.failure<<','<<unsigned(a.first.beforeFlags)<<','<<unsigned(a.first.afterFlags)
           <<"],\"second\":["<<a.second.failure<<','<<unsigned(a.second.beforeFlags)<<','<<unsigned(a.second.afterFlags)<<"]}";
    }out<<"]}";
    out<<",\"cohort_retry\":{\"max_full_attempts\":2,\"attempts\":["<<cohortRetryAttempts[0].load()<<','<<cohortRetryAttempts[1].load()<<','<<cohortRetryAttempts[2].load()
       <<"],\"recovered\":["<<cohortRetryRecovered[0].load()<<','<<cohortRetryRecovered[1].load()<<','<<cohortRetryRecovered[2].load()
       <<"],\"failed\":["<<cohortRetryFailed[0].load()<<','<<cohortRetryFailed[1].load()<<','<<cohortRetryFailed[2].load()<<"],\"first_attempts\":[";
    const auto cohortFailure=[&](const ReloadCohortRejection& d){out<<"{\"stage\":"<<d.stage<<",\"caller_branch\":"<<d.callerBranch<<",\"read_branch\":"<<d.readBranch
        <<",\"boundary_okay\":"<<(d.boundaryOkay?"true":"false")<<",\"capacity_okay\":"<<(d.capacityOkay?"true":"false")
        <<",\"client_failure\":"<<unsigned(d.client.failure)<<",\"client_changed_offset\":"<<d.client.changedOffset
        <<",\"client_changed_before\":"<<unsigned(d.client.changedBefore)<<",\"client_changed_after\":"<<unsigned(d.client.changedAfter)
        <<",\"server_failure\":"<<unsigned(d.server.failure)<<",\"server_state_failure\":"<<d.server.stateFailure
        <<",\"server_changed_offset\":"<<d.server.changedOffset<<",\"server_changed_before\":"<<unsigned(d.server.changedBefore)<<",\"server_changed_after\":"<<unsigned(d.server.changedAfter)<<'}';};
    if(drained)for(unsigned n=0;n<std::min(unsigned(cohortRetrySamples.size()),cohortRetrySampleCount.load());++n){const auto& s=cohortRetrySamples[n];const auto& a=s.attempt;if(n)out<<',';
        out<<"{\"firing\":"<<s.firing<<",\"snapshot_sequence\":"<<s.sequence<<",\"original_deadline_ns\":"<<s.deadline
           <<",\"first_ns\":"<<a.firstNs<<",\"retry_ns\":"<<a.retryNs<<",\"recovered\":"<<(a.recovered?"true":"false")
           <<",\"clock_rejected\":"<<(a.clockRejected?"true":"false")<<",\"first\":";cohortFailure(a.first);out<<",\"second\":";cohortFailure(a.second);out<<'}';}out<<"]}";
    out<<",\"native_abort_cleanup\":{\"armed\":"<<abortArmed.load()<<",\"calls\":"<<abortCalls.load()<<",\"completed_branches\":"<<abortCompleted.load()
       <<",\"context_writes\":false,\"ammo_writes\":false,\"records_total\":"<<abortRecordCount.load();
#ifdef FVR_BC2_PREHOLD_CLEANUP
    out<<",\"prehold_monitor\":{\"started\":"<<(preholdMonitorStarted?"true":"false")
       <<",\"cancelled\":"<<(preholdMonitorCancelled?"true":"false")<<",\"called_mask\":"<<preholdMonitorCalled.load()
       <<",\"first_server_denied_ns\":"<<preholdFirstServerDeniedNs.load()<<",\"first_server_denied_reason\":"<<preholdFirstServerDeniedReason.load()
       <<",\"first_failure_ns\":"<<preholdFirstFailureNs.load()<<",\"first_failure_reason\":"<<preholdFirstFailureReason.load()
       <<",\"authoritative_server_idle_ns\":"<<preholdAuthoritativeIdleNs.load()<<",\"first_failure_branch\":"<<preholdFirstFailureBranch.load()<<",\"failed\":"<<(preholdMonitorFailed?"true":"false")<<",\"helper_exact_mask\":"<<preholdMonitorExact.load()<<",\"update_idle_mask\":"<<preholdMonitorIdle.load();
    if(drained){const auto& origin=preholdMonitor.Original();
       out<<",\"operation\":"<<origin.operation<<",\"revision\":"<<origin.revision<<",\"source_sequence\":"<<origin.sequence
          <<",\"source_observed_ns\":"<<origin.observedNs<<",\"source_deadline_ns\":"<<origin.deadlineNs
          <<",\"phase\":"<<unsigned(preholdMonitor.Phase())<<",\"entered_mask\":"<<preholdMonitor.EnteredMask()
          <<",\"loaded\":"<<origin.source.branches[0].loaded<<",\"reserve\":"<<origin.source.branches[0].reserve
          <<",\"capacity\":"<<origin.source.capacities[0]<<",\"branches\":[";
       const auto state=[&](const ReloadFiringObservation& b){out<<"{\"address\":"<<b.address<<",\"current\":"<<b.currentState
          <<",\"next\":"<<b.nextState<<",\"timer\":"<<b.phaseTimer<<",\"loaded\":"<<b.loaded<<",\"reserve\":"<<b.reserve<<'}';};
       for(unsigned n=0;n<3;++n){if(n)out<<',';const auto& r=preholdMonitorBranches[n];
          out<<"{\"branch\":"<<n<<",\"reason\":"<<r.reason<<",\"invocation\":"<<r.invocation
             <<",\"called\":"<<(r.called?"true":"false")<<",\"helper_exact\":"<<(r.helperExact?"true":"false")
             <<",\"update_known\":"<<(r.updateKnown?"true":"false")<<",\"update_idle\":"<<(r.updateIdle?"true":"false")<<",\"before\":";state(r.before);
          out<<",\"helper_after\":";state(r.helperAfter);out<<",\"update_after\":";state(r.updateAfter);out<<'}';}
       out<<']';
    }out<<'}';
    out<<",\"prehold_operations\":{\"capacity\":"<<PreholdOperationJournal::Capacity
       <<",\"drained\":"<<(drained?"true":"false")<<",\"dropped_known\":"<<(drained?"true":"false")<<",\"dropped\":"<<(drained?preholdOperationJournal.Dropped():0)<<",\"archived\":[";
    const auto operationState=[&](const ReloadFiringObservation& b){out<<"{\"address\":"<<b.address<<",\"current\":"<<b.currentState
        <<",\"next\":"<<b.nextState<<",\"loaded\":"<<b.loaded<<",\"reserve\":"<<b.reserve<<'}';};
    const auto operationIdentity=[&](const ReloadHoldIdentity& id){const auto& o=id.owner;
        out<<"{\"owner\":["<<o.player<<','<<o.soldier<<','<<o.weak<<','<<o.weapon<<','<<o.actorGeneration<<','<<o.equipGeneration<<','<<o.space
           <<"],\"firing\":["<<id.firing[0]<<','<<id.firing[1]<<','<<id.firing[2]<<"],\"server\":["<<id.serverPlayer<<','<<id.serverSoldier<<','<<id.serverItem<<"]}";};
    const auto operationEvidence=[&](const std::optional<PreholdCleanupEvidence>& e){if(!e){out<<"null";return;}
        out<<"{\"operation\":"<<e->operation<<",\"revision\":"<<e->revision<<",\"sequence\":"<<e->sequence
           <<",\"coherent\":"<<(e->coherent?"true":"false")<<",\"config_verified\":"<<(e->configVerified?"true":"false")<<",\"source_verified\":"<<(e->source.verified?"true":"false")<<",\"observed_ns\":"<<e->observedNs<<",\"deadline_ns\":"<<e->deadlineNs<<",\"identity\":";operationIdentity(e->source.identity);
        out<<",\"primary_fire\":"<<e->source.config.primaryFire<<",\"firing_data\":"<<e->source.config.firingData
           <<",\"fire_logic\":"<<e->source.config.fireLogicType<<",\"reload_type\":"<<e->source.config.reloadType<<",\"capacities\":["<<e->source.capacities[0]<<','<<e->source.capacities[1]<<','<<e->source.capacities[2]<<"],\"branches\":[";
        for(unsigned n=0;n<3;++n){if(n)out<<',';operationState(e->source.branches[n]);}out<<"]}";};
    const auto operationRow=[&](const PreholdOperationRecord& r){out<<"{\"epoch\":"<<r.epoch<<",\"operation\":"<<r.operation
        <<",\"family\":"<<unsigned(r.family)<<",\"profile\":"<<unsigned(r.profile)<<",\"policy_phase\":"<<unsigned(r.policyPhase)
        <<",\"policy_failure_at_snapshot\":"<<r.policyFailure<<",\"archived_ns\":"<<r.archivedNs<<",\"cleanup_deadline_ns\":"<<r.cleanupDeadlineNs<<",\"ledger_phase\":"<<r.ledgerPhase
        <<",\"entered_mask\":"<<r.entered<<",\"called_mask\":"<<r.called<<",\"helper_exact_mask\":"<<r.exact<<",\"update_idle_mask\":"<<r.idle
        <<",\"failed\":"<<(r.failed?"true":"false")<<",\"first_failure_ns\":"<<r.firstFailureNs<<",\"first_failure_reason\":"<<r.firstFailureReason
        <<",\"authoritative_server_idle_ns\":"<<r.authoritativeIdleNs<<",\"cancellation_origin\":";
        if(r.cancellation){const auto& c=*r.cancellation;out<<"{\"cycle\":"<<c.cycle<<",\"family\":"<<unsigned(c.family)
            <<",\"profile\":"<<unsigned(c.profile)<<",\"prior_phase\":"<<unsigned(c.prior)<<",\"identity\":";operationIdentity(c.identity);out<<'}';}else out<<"null";
        out<<",\"original\":";operationEvidence(r.original);out<<",\"last_coherent\":";operationEvidence(r.lastCoherent);out<<",\"helper_branches\":[";
        for(unsigned n=0;n<3;++n){if(n)out<<',';const auto& b=r.branches[n];out<<"{\"branch\":"<<n<<",\"invocation\":"<<b.invocation
            <<",\"called\":"<<(b.called?"true":"false")<<",\"helper_exact\":"<<(b.helperExact?"true":"false")
            <<",\"update_known\":"<<(b.updateKnown?"true":"false")<<",\"update_idle\":"<<(b.updateIdle?"true":"false")<<",\"before\":";operationState(b.before);
            out<<",\"helper_after\":";operationState(b.helperAfter);out<<",\"update_after\":";operationState(b.updateAfter);out<<'}';}out<<"]}";};
    if(drained)for(unsigned n=0;n<preholdOperationJournal.Count();++n){if(n)out<<',';operationRow(preholdOperationJournal.Row(n));}
    out<<"],\"current\":";
    if(drained){std::optional<PreholdOperationRecord> current;WithRequest([&]{current=SnapshotPreholdOperation();},false,nullptr,7);if(current)operationRow(*current);else out<<"null";}else out<<"null";
    out<<'}';
#endif
    if(drained){out<<",\"cycle\":"<<abortCleanup.Cycle()<<",\"owner_revision\":"<<abortCleanup.Revision()<<",\"deadline_ns\":"<<abortCleanup.Deadline()
       <<",\"claimed_mask\":"<<abortCleanup.Claimed()<<",\"completed_mask\":"<<abortCleanup.Completed()<<",\"failure\":"<<unsigned(abortCleanup.Failure())
       <<",\"stall_rebases\":"<<abortCleanup.StallRebases()
       <<",\"active\":"<<(abortCleanup.Active()?"true":"false")<<",\"records\":[";
       for(unsigned n=0;n<std::min<unsigned>(abortRecordCount.load(),unsigned(abortRecords.size()));++n){const auto& r=abortRecords[n];if(n)out<<',';
           out<<"{\"cycle\":"<<r.cycle<<",\"dispatch_epoch\":"<<r.dispatchEpoch<<",\"invocation\":"<<r.invocation<<",\"branch\":"<<r.branch<<",\"owner_revision\":"<<r.revision
              <<",\"begin_ns\":"<<r.begin<<",\"end_ns\":"<<r.end<<",\"deadline_ns\":"<<r.deadline<<",\"called\":"<<(r.called?"true":"false")
              <<",\"helper_exact\":"<<(r.helperExact?"true":"false")<<",\"context_unchanged\":"<<(r.contextUnchanged?"true":"false")
              <<",\"owner_retained\":"<<(r.ownerRetained?"true":"false")<<",\"completed\":"<<(r.completed?"true":"false")
              <<",\"before\":{\"current\":"<<r.before.currentState<<",\"next\":"<<r.before.nextState<<",\"timer\":"<<r.before.phaseTimer<<",\"loaded\":"<<r.before.loaded<<",\"reserve\":"<<r.before.reserve
              <<"},\"after\":{\"current\":"<<r.after.currentState<<",\"next\":"<<r.after.nextState<<",\"timer\":"<<r.after.phaseTimer<<",\"loaded\":"<<r.after.loaded<<",\"reserve\":"<<r.after.reserve<<"}}";
       }out<<']';
       // The main flow recorder can expire before a late cancellation. Keep
       // bounded per-cycle admission history independently of that window.
       const auto admissionSample=[&](const ReloadAbortAdmissionSample& s){
           unsigned flags=0;for(unsigned n=0;n<5;++n)if(s.context.flags24Through28[n])flags|=1u<<n;
           out<<"{\"observed_ns\":"<<s.observedNs<<",\"invocation\":"<<s.invocation<<",\"branch\":"<<s.branch
              <<",\"input_flags\":"<<s.context.inputFlags<<",\"flags24_28_mask\":"<<flags
              <<",\"fire\":"<<(s.context.fireRequested?"true":"false")<<",\"order\":"<<(s.context.orderRequested?"true":"false")
              <<",\"reload\":"<<(s.context.reloadRequested?"true":"false")<<",\"delta_seconds\":";
           if(std::isfinite(s.context.deltaSeconds))out<<s.context.deltaSeconds;else out<<"null";
           out<<",\"reload_multiplier\":";if(std::isfinite(s.context.reloadTimeMultiplier))out<<s.context.reloadTimeMultiplier;else out<<"null";
           out<<",\"before\":{\"address\":"<<s.before.address<<",\"wrapper_offset\":"<<s.before.wrapperOffset
              <<",\"current\":"<<s.before.currentState<<",\"next\":"<<s.before.nextState<<",\"loaded\":"<<s.before.loaded
              <<",\"reserve\":"<<s.before.reserve<<",\"flags_a8\":"<<unsigned(s.before.flagsA8)<<",\"timer\":";
           if(std::isfinite(s.before.phaseTimer))out<<s.before.phaseTimer;else out<<"null";out<<"}}";
       };
       out<<",\"cycles_total\":"<<abortCleanup.HistoryTotal()<<",\"cycle_history_capacity\":"<<ReloadAbortCleanup::HistoryCapacity
          <<",\"cycles_overwritten\":"<<(abortCleanup.HistoryTotal()-abortCleanup.HistoryCount())<<",\"cycle_history\":[";
       for(std::uint64_t n=0;n<abortCleanup.HistoryCount();++n){const auto& r=abortCleanup.History(n);if(n)out<<',';
           out<<"{\"cycle\":"<<r.cycle<<",\"dispatch_epoch\":"<<r.dispatchEpoch<<",\"owner_revision\":"<<r.revision<<",\"armed_ns\":"<<r.armedNs
              <<",\"deadline_ns\":"<<r.deadlineNs<<",\"terminal_ns\":"<<r.terminalNs<<",\"initial_loaded\":"<<r.loaded
              <<",\"initial_reserve\":"<<r.reserve<<",\"claims\":"<<r.claims<<",\"fire_deferred\":"<<r.fireDeferred
              <<",\"deferred_by_branch\":["<<r.deferredByBranch[0]<<','<<r.deferredByBranch[1]<<','<<r.deferredByBranch[2]
              <<"],\"claimed_mask\":"<<r.claimed<<",\"completed_mask\":"<<r.completed<<",\"failure\":"<<unsigned(r.failure)
              <<",\"last_admission\":"<<unsigned(r.lastAdmission)<<",\"active\":"<<(r.active?"true":"false")<<",\"first\":";
           admissionSample(r.first);out<<",\"first_deferred\":";admissionSample(r.firstDeferred);
           out<<",\"last\":";admissionSample(r.last);out<<'}';
       }out<<']';}out<<'}';
    out<<",\"diagnostic_hold\":{\"enabled\":"<<(diagnosticHold?"true":"false")<<",\"code_verified\":"<<(holdCodeVerified?"true":"false")
       <<",\"target\":"<<unsigned(holdProbe.Target())<<",\"native_pump_accepted\":false"
       <<",\"phase\":"<<unsigned(holdProbe.Phase())<<",\"reason\":"<<unsigned(holdProbe.Reason())<<",\"begin_ns\":"<<holdProbe.BeginNs()<<",\"deadline_ns\":"<<holdProbe.DeadlineNs()
       <<",\"duration_ns\":"<<ReloadHoldProbe::DurationNs<<",\"contention\":"<<holdProbe.Contention()<<",\"original_calls\":"<<holdOriginalCalls.load()
       <<",\"patch_failures\":"<<holdPatchFailures.load()<<",\"restore_failures\":"<<holdRestoreFailures.load()<<",\"sample_failures\":"<<holdSampleFailures.load()
       <<",\"prepare_rejections\":["<<holdPrepareRejected[0].load()<<','<<holdPrepareRejected[1].load()<<','<<holdPrepareRejected[2].load()<<','
       <<holdPrepareRejected[3].load()<<','<<holdPrepareRejected[4].load()<<','<<holdPrepareRejected[5].load()<<']'
       <<",\"applied\":["<<holdApplied[0].load()<<','<<holdApplied[1].load()<<','<<holdApplied[2].load()<<"],\"restored\":["<<holdRestored[0].load()<<','<<holdRestored[1].load()<<','<<holdRestored[2].load()<<']'
       <<",\"seat_advances\":["<<seatAdvances[0].load()<<','<<seatAdvances[1].load()<<','<<seatAdvances[2].load()<<']'
       <<",\"pump_hold\":{\"enabled\":"<<(pumpGate.Enabled()?"true":"false")<<",\"applied\":["<<pumpHoldApplied[0].load()<<','<<pumpHoldApplied[1].load()<<','<<pumpHoldApplied[2].load()
       <<"],\"failures\":"<<pumpHoldFailures.load()<<",\"owed\":"<<pumpGate.Stats().owed<<",\"held_updates\":"<<pumpGate.Stats().held<<",\"racked\":"<<pumpGate.Stats().racked<<",\"advances\":"<<pumpGate.Stats().advances
       <<",\"timeouts\":"<<pumpGate.Stats().timeouts<<",\"reload_releases\":"<<pumpGate.Stats().reloadReleases<<",\"state_releases\":"<<pumpGate.Stats().stateReleases<<",\"owner_resets\":"<<pumpGate.Stats().ownerResets<<",\"early\":"<<pumpGate.Stats().early<<",\"order_waits\":"<<pumpGate.Stats().orderWaits
       <<",\"left_seven_unadvanced\":["<<pumpGate.Stats().leftSevenUnadvanced[0]<<','<<pumpGate.Stats().leftSevenUnadvanced[1]<<','<<pumpGate.Stats().leftSevenUnadvanced[2]<<']'
       <<",\"native_resyncs\":"<<(pumpGate.Stats().leftSevenUnadvanced[0]+pumpGate.Stats().leftSevenUnadvanced[1]+pumpGate.Stats().leftSevenUnadvanced[2])
       <<",\"settled_copies\":"<<(pumpGate.Stats().advances+pumpGate.Stats().leftSevenUnadvanced[0]+pumpGate.Stats().leftSevenUnadvanced[1]+pumpGate.Stats().leftSevenUnadvanced[2])
       <<",\"advance_unready\":["<<pumpAdvanceUnready[0].load()<<','<<pumpAdvanceUnready[1].load()<<','<<pumpAdvanceUnready[2].load()<<']'<<'}';
    out<<",\"read_rejected_by_caller_read_kind\":[";
    for(unsigned c=0;c<3;++c){if(c)out<<',';out<<'[';for(unsigned r=0;r<3;++r){if(r)out<<',';out<<'['<<holdReadRejected[c][r][0].load()<<','<<holdReadRejected[c][r][1].load()<<']';}out<<']';}out<<']';
    out<<",\"read_rejection_samples\":[";
    if(drained)for(unsigned n=0;n<std::min<unsigned>(holdReadRejectionCount.load(),unsigned(holdReadRejectionSamples.size()));++n){
        const auto& r=holdReadRejectionSamples[n];if(n)out<<',';
        out<<"{\"now_ns\":"<<r.now<<",\"caller_branch\":"<<r.callerBranch<<",\"read_branch\":"<<r.readBranch<<",\"firing\":"<<r.firing
           <<",\"boundary_okay\":"<<(r.boundaryOkay?"true":"false")<<",\"capacity_okay\":"<<(r.capacityOkay?"true":"false")<<",\"capacity\":"<<r.capacity
           <<",\"server_failure\":"<<unsigned(r.server.failure)<<",\"expected_soldier_flags\":"<<unsigned(r.server.expectedSoldierFlags)
           <<",\"observed_soldier_flags\":"<<unsigned(r.server.observedSoldierFlags)<<",\"only_soldier_flags_differ\":"<<(r.server.differsOnlySoldierFlags?"true":"false")
           <<",\"client_failure\":"<<unsigned(r.client.failure)<<",\"client_changed_offset\":"<<r.client.changedOffset
           <<",\"client_changed_before\":"<<unsigned(r.client.changedBefore)<<",\"client_changed_after\":"<<unsigned(r.client.changedAfter)
           <<",\"server_state_failure\":"<<r.server.stateFailure<<",\"server_changed_offset\":"<<r.server.changedOffset
           <<",\"server_changed_before\":"<<unsigned(r.server.changedBefore)<<",\"server_changed_after\":"<<unsigned(r.server.changedAfter)<<'}';}out<<']';
    if(drained&&holdProbe.BeginNs()>0){const auto& id=holdProbe.Identity();out<<",\"client_weapon\":"<<id.owner.weapon<<",\"server_player\":"<<id.serverPlayer
        <<",\"server_soldier\":"<<id.serverSoldier<<",\"server_item\":"<<id.serverItem<<",\"firing\":["<<id.firing[0]<<','<<id.firing[1]<<','<<id.firing[2]
        <<"],\"loaded\":"<<holdProbe.Loaded()<<",\"reserve\":"<<holdProbe.Reserve();}out<<'}';
    out<<",\"diagnostic_round\":{\"enabled\":"<<(diagnosticRound?"true":"false")<<",\"code_verified\":"<<(holdCodeVerified?"true":"false")<<",\"contention\":"<<roundContention.load();
    if(drained){if(roundCancelled.load())roundGate.Cancel(ReloadRoundGateFailure::OwnerOrRead);
        out<<",\"phase\":"<<unsigned(roundGate.Phase())<<",\"failure\":"<<unsigned(roundGate.Failure())
           <<",\"first_begin_ns\":"<<roundGate.FirstBegin()<<",\"first_deadline_ns\":"<<roundGate.FirstDeadline()
           <<",\"request_ns\":"<<roundGate.RequestNs()<<",\"advance_deadline_ns\":"<<roundGate.AdvanceDeadline()
           <<",\"second_begin_ns\":"<<roundGate.SecondBegin()<<",\"second_deadline_ns\":"<<roundGate.SecondDeadline()
           <<",\"transfer_ids\":["<<roundGate.TransferIds()[0]<<','<<roundGate.TransferIds()[1]<<','<<roundGate.TransferIds()[2]
           <<"],\"first_rehold_ids\":["<<roundGate.FirstReholds()[0]<<','<<roundGate.FirstReholds()[1]<<','<<roundGate.FirstReholds()[2]<<']';
        const auto& ack=roundGate.Acknowledgement();out<<",\"acknowledgement\":";
        if(ack)out<<"{\"request\":"<<ack->semantic.request<<",\"cycle\":"<<ack->cycle<<",\"sample_sequence\":"<<ack->sampleSequence<<",\"server_invocation\":"<<ack->serverInvocation
            <<",\"actor\":"<<ack->semantic.owner.actor<<",\"actor_generation\":"<<ack->semantic.owner.actorGeneration<<",\"weapon\":"<<ack->semantic.owner.weapon
            <<",\"equip_generation\":"<<ack->semantic.owner.equipGeneration<<",\"space\":"<<ack->semantic.owner.space<<",\"diagnostic_only\":true}";
        else out<<"null";
    }out<<'}';
    out<<",\"reload_retirement\":{\"attempts\":"<<retireAttempts.load()<<",\"successes\":"<<retireSuccesses.load()
       <<",\"busy\":"<<retireBusy.load()<<",\"wrong_cycle\":"<<retireWrongCycle.load()<<",\"unquiet\":"<<retireUnquiet.load()
       <<",\"undelivered\":"<<retireDropped.load()<<",\"last_event\":"<<retireLastEvent.load()<<",\"last_cycle\":"<<retireLastCycle.load()
       <<",\"last_observed_ns\":"<<retireLastObserved.load()<<",\"last_deadline_ns\":"<<retireLastDeadline.load()<<",\"ammo_rollback\":false}";
    out<<",\"request_owner_lock\":{\"wait_budget_ns\":"<<ReloadPolicyLock::WaitNs<<",\"max_attempts\":"<<ReloadPolicyLock::MaxAttempts
       <<",\"waited\":"<<requestOwnerLockWaits.load()<<",\"recovered\":"<<requestOwnerLockRecovered.load()<<",\"failed\":"<<requestOwnerLockFailed.load()
       <<",\"max_recovered_wait_ns\":"<<(drained?requestOwnerLockMaxRecoveredWaitNs:0)<<'}';
    out<<",\"request_policy_body\":{\"total\":"<<requestBodyTelemetry.Total()<<",\"slow_threshold_ns\":100000,\"slow\":"<<requestBodyTelemetry.Slow()
       <<",\"invalid_clock\":"<<requestBodyTelemetry.Invalid()<<",\"max_ns\":"<<requestBodyTelemetry.Maximum()
       <<",\"drained\":"<<(drained?"true":"false")<<",\"dropped\":"<<(requestBodyTelemetry.Slow()>32?requestBodyTelemetry.Slow()-32:0)<<",\"rows\":[";
    if(drained)for(unsigned n=0;n<requestBodyTelemetry.CountAfterDrain();++n){if(n)out<<',';const auto& r=requestBodyTelemetry.RowsAfterDrain()[n];
        out<<"{\"site\":"<<r.site<<",\"thread\":"<<r.thread<<",\"firing\":"<<r.firing<<",\"begin_ns\":"<<r.beginNs<<",\"end_ns\":"<<r.endNs<<'}';}
    out<<"]}";
    out<<",\"request_policy_lock\":{\"wait_budget_ns\":"<<ReloadPolicyLock::WaitNs<<",\"max_attempts\":"<<ReloadPolicyLock::MaxAttempts
       <<",\"waited\":"<<requestLockWaits.load()<<",\"recovered\":"<<requestLockRecovered.load()<<",\"failed\":"<<requestContention.load()
       <<",\"max_recovered_wait_ns\":"<<(drained?requestLockMaxRecoveredWaitNs:0)<<",\"first_waits\":[";
    if(drained)for(unsigned n=0;n<std::min<unsigned>(requestLockSampleCount.load(),unsigned(requestLockSamples.size()));++n){
        if(n)out<<',';const auto& s=requestLockSamples[n];const auto& e=s.evidence;
        out<<"{\"thread\":"<<s.thread<<",\"firing\":"<<s.firing<<",\"site\":"<<s.site<<",\"begin_ns\":"<<e.beginNs<<",\"end_ns\":"<<e.endNs
           <<",\"attempts\":"<<e.attempts<<",\"acquired\":"<<(e.held?"true":"false")<<",\"failure\":"<<unsigned(e.failure)<<'}';
    }out<<"]}";
    out<<",\"request_cycle\":{\"enabled\":"<<(requestMode?"true":"false")<<",\"contention\":"<<requestContention.load()<<",\"read_failures\":"<<requestReadFailures.load()
       <<",\"undelivered_acknowledgements\":"<<requestUndeliveredAcknowledgements.load()<<",\"last_undelivered_request\":"<<requestLastUndelivered.load();
    out<<",\"deferred_prehold_misses\":"<<requestDeferredMisses.load();
    if(drained){out<<",\"read_miss_evidence\":[";
        for(unsigned n=0;n<std::min(unsigned(requestReadMisses.size()),requestReadMissCount.load());++n){const auto& e=requestReadMisses[n];if(n)out<<',';
            out<<"{\"now_ns\":"<<e.now<<",\"firing\":"<<e.firing<<",\"stage\":"<<e.stage<<",\"kind\":"<<e.kind<<",\"phase\":"<<e.phase<<",\"deferred\":"<<(e.deferred?"true":"false");
            const auto& d=e.boundary;out<<",\"boundary\":{\"stage\":"<<d.stage<<",\"read_branch\":"<<d.readBranch
                <<",\"client_failure\":"<<unsigned(d.client.failure)<<",\"client_flags_before\":"<<unsigned(d.client.beforeSoldierFlags)<<",\"client_flags_after\":"<<unsigned(d.client.afterSoldierFlags)
                <<",\"client_only_flags\":"<<(d.client.differsOnlySoldierFlags?"true":"false")<<",\"client_changed_offset\":"<<d.client.changedOffset
                <<",\"client_changed_before\":"<<unsigned(d.client.changedBefore)<<",\"client_changed_after\":"<<unsigned(d.client.changedAfter)
                <<",\"server_failure\":"<<unsigned(d.server.failure)<<",\"server_flags_published\":"<<unsigned(d.server.expectedSoldierFlags)
                <<",\"server_flags_before\":"<<unsigned(d.server.observedSoldierFlags)<<",\"server_flags_after\":"<<unsigned(d.server.afterSoldierFlags)
                <<",\"server_only_flags\":"<<(d.server.changedOnlySoldierFlags?"true":"false")<<",\"server_state_failure\":"<<d.server.stateFailure
                <<",\"server_changed_offset\":"<<d.server.changedOffset<<",\"server_changed_before\":"<<unsigned(d.server.changedBefore)<<",\"server_changed_after\":"<<unsigned(d.server.changedAfter)<<"}}";}
        out<<"],\"cancellations\":[";
        for(unsigned n=0;n<std::min(unsigned(requestCancellations.size()),requestCancellationCount.load());++n){const auto& e=requestCancellations[n];if(n)out<<',';out<<"{\"now_ns\":"<<e.now<<",\"reason\":"<<e.reason<<",\"epoch\":"<<e.epoch<<'}';}
        out<<']';}
    if(drained&&requestCycle.IsMagazine()){
        const auto& pulse=requestCycle.MagazineStartupPulse();out<<",\"startup_pulse\":{\"present\":"<<(pulse?"true":"false")
           <<",\"input_sequence\":"<<(pulse?pulse->control.sequence:0)<<",\"input_observed_ns\":"<<(pulse?pulse->control.observedNs:0)
           <<",\"input_deadline_ns\":"<<(pulse?pulse->control.deadlineNs:0)<<",\"end_ns\":"<<(pulse?pulse->endNs:0)
           <<",\"first_holding_ns\":"<<requestCycle.MagazineFirstHoldingNs()<<",\"arming_context_observed_ns\":[";
        const auto& contexts=requestCycle.MagazineArmingContexts();for(unsigned n=0;n<3;++n){if(n)out<<',';out<<contexts[n];}out<<"]}";
    }
    if(drained){if(const auto& clock=requestCycle.ClockFailure();clock){out<<",\"clock_failure\":{\"now_ns\":"<<clock->nowNs
        <<",\"last_now_ns\":"<<clock->lastNowNs<<",\"source_ns\":"<<clock->sourceNs<<",\"deadline_ns\":"<<clock->deadlineNs
        <<",\"sequence\":"<<clock->sequence<<",\"regression\":"<<(clock->regression?"true":"false")<<'}';}}
    if(drained){if(requestAppliedCancel!=requestCancelEpoch.load())requestCycle.Cancel(ReloadRequestCycleFailure::Owner);
        out<<",\"phase\":"<<unsigned(requestCycle.Phase())<<",\"failure\":"<<unsigned(requestCycle.Failure())<<",\"cycle\":"<<requestCycle.Cycle()
           <<",\"pending_request\":"<<requestCycle.PendingRequest()<<",\"unresolved\":"<<(requestCycle.UnresolvedRequest()?"true":"false")
           <<",\"spas_submit_failure\":"<<requestCycle.SpasSubmitFailure()<<",\"evaluate_contention_skips\":"<<evaluateContentionSkips.load()
           <<",\"submit_retries\":"<<submitRetries.load()<<",\"submit_retried\":"<<submitRetried.load()<<",\"submit_retry_expired\":"<<submitRetryExpired.load();}out<<'}';
    if(!drained){out<<",\"records_unavailable_until_drain\":true}";return;}
    out<<",\"dropped\":"<<records.Dropped()<<",\"rejected\":"<<records.Rejected()<<",\"records\":[";bool first=true;
    for(const auto& record:records.Records()){if(!first)out<<',';first=false;const auto& e=record.entry;
        out<<"{\"id\":"<<record.id<<",\"kind\":"<<unsigned(e.kind)<<",\"parent\":"<<e.parent<<",\"update\":"<<e.update
           <<",\"native_invocation\":"<<e.nativeInvocation<<",\"native_parent\":"<<e.nativeParent<<",\"native_update\":"<<e.nativeUpdate
           <<",\"thread\":"<<e.thread<<",\"depth\":"<<e.depth<<",\"caller\":"<<e.caller<<",\"context\":"<<e.context<<",\"argument\":"<<e.argument
           <<",\"begin_ns\":"<<e.nowNs<<",\"begin_tick_ms\":"<<e.tickMs<<",\"transfer_path\":";
        if(e.transferPath)out<<unsigned(*e.transferPath);else out<<"null";out<<",\"before\":";Boundary(out,e.boundary);
        out<<",\"context_before\":";Copied(out,e.copiedContext,e.contextCopied);
        out<<",\"finished\":"<<(record.finished?"true":"false")<<",\"identity_retained\":"<<(record.identityRetained?"true":"false")
           <<",\"end_ns\":"<<record.exit.nowNs<<",\"end_tick_ms\":"<<record.exit.tickMs<<",\"after\":";
        if(record.exit.boundary)Boundary(out,*record.exit.boundary);else out<<"null";
        out<<",\"context_after\":";Copied(out,record.exit.copiedContext,record.exit.contextCopied);
        out<<",\"snapshot_before\":";Snapshot(out,e.copiedSnapshot,e.snapshotCopied);
        out<<",\"snapshot_after\":";Snapshot(out,record.exit.copiedSnapshot,record.exit.snapshotCopied);
        out<<",\"restore_fields_matched\":"<<(ReloadRestoreMatched(record)?"true":"false")
           <<",\"hold_requested\":"<<(record.exit.holdRequested?"true":"false")<<",\"hold_applied\":"<<(record.exit.hold.applied?"true":"false")
           <<",\"hold_restored\":"<<(record.exit.hold.restored?"true":"false")<<",\"hold_original_delta_bits\":"<<record.exit.hold.original
           <<",\"hold_before_restore_bits\":"<<record.exit.hold.beforeRestore<<",\"hold_unexpected_native_write\":"<<(record.exit.hold.unexpectedNativeWrite?"true":"false");out<<'}';
    }out<<"]}";
}
}
#else
namespace fvr::bc2::reloadFlowRuntime {
bool EnablePumpHoldDiagnostic()noexcept{return false;}
bool EnablePumpHold(bool)noexcept{return false;}
void PumpReloadMailbox()noexcept{}
void GrantStockReload(std::int64_t)noexcept{}
bool StopPending(std::int64_t)noexcept{return false;}
bool PumpOwed()noexcept{return false;}
bool PumpExpected()noexcept{return false;}
bool RackPump()noexcept{return false;}
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,const ReloadStateMemory&,bool,bool,bool,bool){return false;}
MagazineEmptyControlCounters ReadMagazineEmptyControlCounters()noexcept{return {};}
void PublishMagazineInteractionDiagnostic(const MagazineInteractionDiagnostic&)noexcept{}
bool EnableMagazineRequestCycles()noexcept{return false;}
bool SelectRequestFamily(ReloadNativeFamily,const ReloadStateOwner&)noexcept{return false;}
bool SelectMagazineRequestProfile(NativeMagazineProfileId,const ReloadStateOwner&)noexcept{return false;}
std::optional<NativeMagazineProfileId> ReadMagazineRequestProfile(const ReloadStateOwner&)noexcept{return {};}
std::optional<ReloadHoldIdentity> RequestIdentity()noexcept{return {};}
std::optional<RequestProbeSnapshot> ReadRequestProbeSnapshot()noexcept{return {};}
std::optional<Bc2AmmoReserveLease> ReadReserve()noexcept{return {};}
std::optional<Bc2AmmoReserveLease> ReadDiagnosticFireReserve()noexcept{return {};}
ReloadReserveObservation ReadReserveObserved()noexcept{return {};}
std::optional<ReloadCycleRetirement> RetireRequestCycle(const ReloadHoldIdentity&,std::uint64_t)noexcept{return {};}
bool StartRequestCycle(const ReloadCycleControl&)noexcept{return false;}
bool KeepAliveRequestCycle(const ReloadCycleControl&)noexcept{return false;}
ReloadKeepAliveResult KeepAliveRequestCycleObserved(const ReloadCycleControl&)noexcept{return ReloadKeepAliveResult::Rejected;}
std::optional<ReloadRoundLease> RequestLease(const ReloadHoldIdentity&,std::uint64_t)noexcept{return {};}
bool SubmitRequest(const Bc2ReloadNativeRequest&)noexcept{return false;}
std::optional<Bc2ReloadAckEvidence> TakeRequestAcknowledgement(const ReloadHoldIdentity&,std::uint64_t)noexcept{return {};}
MagazineCycleStartResult StartMagazineRequestCycleObserved(const ReloadCycleControl&,const interaction::ManualReloadRequest&,std::optional<ReloadMagazineStartupPulse>)noexcept{return MagazineCycleStartResult::Unknown;}
MagazineCycleStartResult InspectMagazineRequestCycleStart(const ReloadHoldIdentity&,std::uint64_t,std::optional<ReloadMagazineStartupPulse>)noexcept{return MagazineCycleStartResult::Unknown;}
bool StartMagazineRequestCycle(const ReloadCycleControl&,const interaction::ManualReloadRequest&)noexcept{return false;}
ReloadMagazineLeaseObservation ObserveMagazineLease(const ReloadHoldIdentity&,std::uint64_t)noexcept{return {};}
std::optional<ReloadMagazineLease> RequestMagazineLease(const ReloadHoldIdentity&,std::uint64_t)noexcept{return {};}
std::optional<ReloadMagazineGateAcknowledgement> TakeMagazineUnseatAcknowledgement(const ReloadHoldIdentity&,std::uint64_t)noexcept{return {};}
bool SubmitMagazineRequest(const ReloadMagazineNativeRequest&)noexcept{return false;}
std::optional<ReloadMagazineAckEvidence> TakeMagazineAcknowledgement(const ReloadHoldIdentity&,std::uint64_t)noexcept{return {};}
void CancelRequestCycle()noexcept{}
std::optional<ReloadPreholdEntryObservation> ReadPreholdEntry(const ReloadHoldIdentity&,std::uint64_t,std::int64_t)noexcept{return {};}
OwnerPublicationResult PublishOwnerObserved(const ReloadStateSnapshot&,std::int64_t)noexcept{return {};}
bool PublishOwner(const ReloadStateSnapshot&,std::int64_t)noexcept{return false;}
void ClearOwner()noexcept{}std::uint32_t LocalServerInventory()noexcept{return 0;}unsigned AbortArmedCount()noexcept{return 0;}std::uint32_t LocalServerFiring()noexcept{return 0;}void Start()noexcept{}bool Stop()noexcept{return true;}
void Report(std::ostream& out){out<<"{\"installed\":false,\"unsupported_architecture\":true,\"observation_only\":true}";}
}
#endif





