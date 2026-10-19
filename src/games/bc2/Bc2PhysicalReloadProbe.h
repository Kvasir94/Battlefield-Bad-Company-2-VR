#pragma once
#include "Bc2PhysicalReload.h"
namespace fvr::bc2 {
// Diagnostic-only inverse of the existing TrackedRig mapping. The coherent raw
// wrist/body sample and its original controller input are both required.
std::optional<math::Pose> PhysicalReloadProbeController(const ReloadRawContact&,const interaction::InputFrame& original,
    const math::Matrix4& desiredRawWristWorldMeters)noexcept;
class Bc2PhysicalReloadProbe {
public:
    // Two rounds is a separate bounded fixture; the verified one-round default
    // keeps its original motion and completion behavior.
    explicit Bc2PhysicalReloadProbe(bool enabled=false,unsigned rounds=1)noexcept:enabled_(enabled),rounds_(rounds){}
    // Edits only a private synthetic-fixture input copy before hand ownership.
    // Never publishes geometry or invokes native reload dispatch/ack functions.
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view asset,const ReloadRawContact&,
        const PhysicalReloadProbeState&,std::int64_t observedNs,std::int64_t deadlineNs,std::int64_t nowNs)noexcept;
    void Observe(const PhysicalReloadProbeState&,std::int64_t nowNs)noexcept;
    bool CancelConsumer()const noexcept{return phase_==Phase::Done||phase_==Phase::Failed||phase_==Phase::ForcedCancel;}
    // Regression legs (FVR_BC2_PROBE_CANCEL / FVR_BC2_PROBE_SPAS_FIRE_AFTER): 1 cancel between shells
    // (after the first credit, cycle still held), 2 cancel at begin before the first hold,
    // 3 cancel right after a seat is submitted, before its credit. FireAfter(n): press the
    // right trigger for 100 ms once n shells are credited (fire interrupts the reload).
    void ForceCancel(unsigned where)noexcept{forcedCancel_=where<=3?where:0;}
    void FireAfter(unsigned shells)noexcept{fireAfter_=shells;}
    void PumpAfter()noexcept{pumpAfter_=true;}
    // The caller applies the press to the action input (actions are derived
    // before Prepare runs, so editing the fixture input is too late).
    bool FireTrigger(std::int64_t now)const noexcept{return fireUntil_&&now>=fireFrom_&&now<fireUntil_;}
    // Abandon leg: after n shells stop all input; 4 s later press fire once (the gun must
    // have left the reload on its own and fire normally).
    void AbandonAfter(unsigned shells)noexcept{abandonAfter_=shells;}
    // Grab N ms after the last prep shot, even while the gun still pumps (a player
    // grabbing right after a shot); otherwise the fixture waits for readiness.
    void GrabAfterShot(unsigned ms)noexcept{grabInPump_=true;grabAfterShotMs_=ms;}
    void Report(std::ostream&)const;
private:
    enum class Phase:unsigned {Warmup,Grab,WaitHold,Approach,Enter,Stroke,WaitAck,Done,Failed,Rearm,ForcedCancel,Fired,PumpApproach,PumpGrip,PumpRear,PumpForward,PumpFire};
    void PhaseTo(Phase,std::int64_t)noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    struct Input {interaction::InputFrame frame{};std::int64_t observed=0,deadline=0;ReloadStateOwner owner{};};
    std::array<std::optional<Input>,32> history_{};unsigned next_=0;
    bool enabled_=false;Phase phase_=Phase::Warmup;unsigned failure_=0;
    std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0;
    ReloadStateOwner owner_{};std::optional<interaction::InputFrame> last_;
    math::Pose command_{};int loadedBefore_=-1,reserveBefore_=-1;
    unsigned completed_=0,submitted_=0,rounds_=1;
    bool preparationReserveFresh_=false,preparationRaw_=false;
    bool pumpAfter_=false,pumpVerified_=false;
    int preparationLoaded_=-1,preparationReserve_=-1,preparationCapacity_=-1;
    std::uint64_t lastRaw_=0,neutralFirst_=0,neutralLast_=0;
    struct Round {unsigned number=0,acquired=0;std::int64_t completedNs=0;
        std::uint64_t firstNeutral=0,lastNeutral=0,grabInput=0;int loaded=-1,reserve=-1;};
    std::array<Round,4> roundsEvidence_{};
    bool grabInPump_=false;unsigned grabAfterShotMs_=0;int lastPrepLoaded_=-1;std::int64_t lastPrepShotAt_=0;unsigned forcedCancel_=0,fireAfter_=0,abandonAfter_=0;std::int64_t forcedAt_=0,fireUntil_=0,fireFrom_=0;
    struct Row {unsigned phase=0,reason=0;std::int64_t now=0;std::uint64_t input=0,raw=0;
        float positionError=0,angleError=0,rail=0;int loaded=-1,reserve=-1;};
    std::array<Row,192> rows_{};unsigned rowsCount_=0;std::int64_t rowAt_=0;
};
}
