#pragma once
#include "Bc2MagazinePhysicalReload.h"
#include "fvr/interaction/ControllerInput.h"
#include "Bc2MagazineEquipmentProfile.h"
#include <string>
namespace fvr::bc2 {
struct MagazinePackCounters {
 unsigned copies=0,pairs=0,fallbacks=0;
 std::array<unsigned,4> roleCopies{},rolePairs{};
};
// Bounded, explicit diagnostic. Modifies only the private synthetic controller
// input BEFORE shared hand ownership. Native Start/Seat/ack, supply claims,
// geometry evidence and packed-copy receipts are never manufactured here.
class Bc2MagazinePhysicalProbe {
public:
 explicit Bc2MagazinePhysicalProbe(bool enabled=false,bool originalReturn=false,bool carryChallenge=false,bool returnThenReplace=false)noexcept:enabled_(enabled),originalReturn_(originalReturn||returnThenReplace),carryChallenge_(carryChallenge&&!originalReturn&&!returnThenReplace),returnThenReplace_(returnThenReplace){}
 // Slider release-to-finish: open the grip part-way through the replacement
 // stroke (past the 25% release threshold) and keep the hand still; the
 // consumer's settle must seat and submit on its own.
 void ReleaseMidStroke(bool enabled=true)noexcept{releaseMidStroke_=enabled&&!originalReturn_&&!carryChallenge_;}
 // Eject leg: instead of pulling the magazine by hand, press Reload (right
 // Secondary) once for 150 ms and keep the hand still until the well is
 // empty, then the usual pouch grab and full stroke. The caller applies the
 // press to the action input while EjectPress() is true.
 void Eject(bool enabled=true)noexcept{eject_=enabled&&!originalReturn_&&!carryChallenge_;if(eject_)releaseMidStroke_=false;}
 // Rack leg (opt-in charging-handle rack): after the reload completes, press
 // fire once (no shot expected), rack the handle with the left hand (palm
 // through the magazine grasp, along the profile's pull direction), release,
 // then press fire again (a shot expected). Events are checked offline.
 void RackLeg(bool enabled=true)noexcept{rackLeg_=enabled&&!originalReturn_&&!carryChallenge_;}
 // Bolt-release variant of the rack leg (C3): after the first fire press,
 // press Reload (right Secondary) once instead of racking the handle, then
 // fire again. The hand stays where the seat left it.
 void BoltReleaseLeg(bool enabled=true)noexcept{boltRelease_=enabled&&rackLeg_;}
 // Forced-cancel legs (regression for held-reload aborts): 1 cancel once the
 // native reload is held (before the pull), 2 cancel at begin before the first
 // hold (Arming), 3 left hand untracked for 300 ms once held, 4 untracked for
 // 300 ms at begin before the first hold, 5 head untracked for 1 s once held
 // (owner clear mid-hold). Then stop.
 void ForceCancel(unsigned where)noexcept{forcedCancel_=where<=5?where:0;}
 // Legacy expects a blocked flick during its native hold. Physical authority
 // instead tests switching away with the magazine out and returning two seconds
 // later; that round trip must preserve the original weapon's mechanics.
 void FlickAtHold(bool enabled=true)noexcept{flickLeg_=enabled;}
 bool FlickPress(std::int64_t now)const noexcept{const auto dt=now-flickAt_;return flickAt_&&dt>=0&&
  (dt<300000000ll||(physicalAuthority_&&dt>=2000000000ll&&dt<2300000000ll));}
 bool RackTrigger(std::int64_t now)noexcept{const bool press=(phase_==Phase::RackFire1||phase_==Phase::RackFire2)&&now-phaseAt_<100000000ll;if(press)++rackPressFrames_;return press;}
 bool EjectPress(std::int64_t now)noexcept{const bool press=(phase_==Phase::Eject||phase_==Phase::BoltRelease)&&now-phaseAt_<150000000ll;if(press)++ejectPressFrames_;return press;}
 // Prototype: fire the real right trigger from a full magazine until it is
 // partial, so the fixture can start from a fresh spawn without a headset.
 void PrepareShots(bool enabled=true)noexcept{prepareShots_=enabled;}
 // Window in which the caller may hold the trigger (Fire is derived before
 // Prepare runs). The caller checks the live counts and reports each held frame.
 bool PrepareWindow(std::int64_t now)const noexcept{
  return prepareShots_&&phase_==Phase::Warmup&&first_&&now-first_>=6000000000ll&&now-first_<12000000000ll;}
 void NoteShot(std::int64_t now)noexcept{if(!lastShotAt_)++preparedBursts_;lastShotAt_=now;}
 // One call per gather in the window: pulses the trigger (press, neutral,
 // press...) so a waiting consumer still sees neutral packets. Counts for the report.
 bool PrepareTrigger(std::int64_t now,bool leaseFull)noexcept{
  if(!PrepareWindow(now))return false;++prepWindow_;if(!leaseFull)return false;++prepFull_;
  pulse_=!pulse_;if(!pulse_)return false;++prepPressed_;NoteShot(now);return true;}
 // Prototype dry-fire leg: empty the magazine (pulsed trigger while loaded>0),
 // then `pulls` separate pulls on the empty gun (100 ms press, 300 ms release),
 // before the reload. loaded<0 means no fresh read (hold the current stage).
 void PrepareDryFire(unsigned pulls)noexcept{dryPulls_=pulls;}
 bool DryFireTrigger(std::int64_t now,int loaded)noexcept{
  if(!dryPulls_||!PrepareWindow(now)||dryDone_)return false;++prepWindow_;
  if(!dryEmpty_){if(loaded==0){dryEmpty_=true;dryAt_=now;return false;}
   if(loaded<0)return false;pulse_=!pulse_;if(!pulse_)return false;++prepPressed_;NoteShot(now);return true;}
  // Wait 300 ms after the empty read: a pull at that instant lands while the
  // gun is still leaving the automatic fire state and makes no dry row (run 090321).
  if(now-dryAt_<300000000ll)return false;
  const auto t=now-dryAt_-300000000ll;const unsigned pull=unsigned(t/400000000ll);
  if(pull>=dryPulls_){dryDone_=true;return false;}
  const bool press=t%400000000ll<100000000ll;if(press){++dryPressed_;NoteShot(now);}return press;}
 bool DryFireComplete()const noexcept{return !dryPulls_||dryDone_;}
 void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view asset,const MagazineRawContact&,
     const MagazinePhysicalProbeState&,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
 void Observe(const MagazinePhysicalProbeState&,const MagazinePackCounters&,std::int64_t now)noexcept;
 bool CancelConsumer()const noexcept{return phase_==Phase::Done||phase_==Phase::Failed||phase_==Phase::Skipped||phase_==Phase::ForcedCancel;}
 // The held weapon has no magazine support: nothing was driven (not a failure).
 bool Skipped()const noexcept{return phase_==Phase::Skipped;}
 bool Completed()const noexcept{return phase_==Phase::Done;}
 void Report(std::ostream&)const;
private:
 enum class Phase:unsigned {Warmup,ApproachMagazine,Grip,Pull,Release,Pouch,GrabReplacement,ApproachRail,Enter,Stroke,WaitAck,WaitBaseline,Done,Failed,Carry,SettleRelease,Eject,Skipped,RackFire1,RackApproach,RackGrip,RackPull,RackRelease,RackFire2,BoltRelease,ForcedCancel};
 void PhaseTo(Phase,std::int64_t)noexcept;
 void Fail(unsigned,std::int64_t)noexcept;
 struct Input {interaction::InputFrame frame{};ReloadStateOwner owner{};std::int64_t observed=0,deadline=0;};
 std::array<std::optional<Input>,32> history_{};unsigned historyNext_=0;
 bool enabled_=false,originalReturn_=false,carryChallenge_=false;
 bool returnThenReplace_=false,secondCycle_=false;std::string firstCycleReport_;
 bool releaseMidStroke_=false,releasedMidStroke_=false;float releaseAlong_=0;
 bool eject_=false;unsigned ejectPressFrames_=0;
 std::int64_t cancelSeenAt_=0;unsigned transientCancels_=0;
 bool boltRelease_=false;unsigned forcedCancel_=0;bool forcedDone_=false;std::int64_t untrackUntil_=0,headLostUntil_=0,forcedAt_=0,flickAt_=0;bool flickLeg_=false;bool rackLeg_=false;unsigned rackPressFrames_=0;const interaction::ChargingHandleProfile* handle_=nullptr;
 // Per-weapon geometry from MagazineSupportFor(asset), fixed at the first frame.
 math::Pose head_{};std::string asset_;MagazineSupportStatus support_=MagazineSupportStatus::NoNativeProfile;const MagazineGeometryProfile* geometry_=nullptr;
 std::int64_t rearmWaitAt_=0;unsigned rearmWaits_=0,regripFrames_=0,invalidFrames_=0,invalidRun_=0;
 unsigned dryPulls_=0,dryPressed_=0;bool dryEmpty_=false,dryDone_=false;std::int64_t dryAt_=0;
 bool prepareShots_=false,pulse_=false;std::int64_t lastShotAt_=0;unsigned preparedBursts_=0,prepWindow_=0,prepFull_=0,prepPressed_=0;
 bool carryReturning_=false,carryExercised_=false;std::optional<math::Matrix4> carryStart_;
 std::optional<math::Matrix4> carryPrevious_;
 std::int64_t carryWaitAt_=0,carryFailureAt_=0;unsigned carryWaits_=0,carryFailureFlags_=0;
 float carryStepTranslation_=0,carryStepAngle_=0;
 float carryDisplacement_=0,carryAngle_=0;unsigned carrySamples_=0;Phase phase_=Phase::Warmup;unsigned failure_=0;
 std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0,rowAt_=0;
 ReloadStateOwner owner_{};std::optional<interaction::InputFrame> lastInput_;
 math::Pose command_{};std::uint64_t lastRaw_=0;
 int loadedBefore_=-1,reserveBefore_=-1,capacityBefore_=-1,expectedUnits_=0;
 bool physicalAuthority_=false;interaction::PhysicalWeaponIdentity physicalIdentity_{};
 int chamberBefore_=0;std::uint64_t admittedBefore_=0,spentBefore_=0,discardedBefore_=0;
 unsigned completed_=0,submitted_=0,acquired_=0,originalReturns_=0;
 std::optional<interaction::OriginalMagazine> original_;
 std::optional<interaction::OriginalMagazineReturnReceipt> originalReceipt_;
 std::uint64_t originalCycle_=0;
 MagazinePackCounters packs_{},baseline_{};
 bool removedPair_=false,hiddenPair_=false,replacementPair_=false,receiptVerified_=false;
 std::array<std::optional<unsigned>,4> phasePairs_{};
 struct Row {unsigned phase=0,reason=0;std::int64_t now=0;std::uint64_t input=0,raw=0,cycle=0;
  float positionError=0,angleError=0,rail=0;int loaded=-1,reserve=-1;unsigned pairs=0;};
 std::array<Row,192> rows_{};unsigned rowCount_=0,dropped_=0;
};
} // namespace fvr::bc2
