#include <Windows.h>
#include <intrin.h>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <MinHook.h>
#include "Bc2GunEventLog.h"
#include "Bc2Gameplay.h"
#include "Bc2TestRangeHandoff.h"
#include "Bc2BodyAmmoRenderer.h"
#include "fvr/interaction/DroppedProps.h"
#include "fvr/interaction/ReloadHints.h"
#include "fvr/interaction/ReloadGrip.h"
#include "Bc2EmptyFireProbe.h"
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
#include "Bc2ArmingEmptyProbe.h"
#endif
#include "Bc2InputBinding.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2EquipmentCycle.h"
#include "Bc2VehicleRoutes.h"
#include "Bc2BoatProfile.h"
#include "Bc2BoatAim.h"
#include "Bc2VehicleCommit.h"
#include "Bc2ContextInteractEvidence.h"
#include "fvr/interaction/VehicleTracking.h"
#include "Bc2Rig.h"
#include "Bc2PlayerPair.h"
#include "Bc2RigPublication.h"
#include "Bc2GripAim.h"
#include "Bc2WeaponEffects.h"
#include "Bc2WeaponProfiles.h"
#include "Bc2WeaponMode.h"
#include "Bc2BodyInventory.h"
#include "Bc2BodyHolster.h"
#include "Bc2BodyHolsterLifecycle.h"
#include "Bc2ReloadState.h"
#include "Bc2NativeRecoilTransfer.h"
#include "Bc2SelectedMeshesObservation.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2PhysicalWeaponRuntime.h"
#include "Bc2ReloadButtonGesture.h"
#include "Bc2ArsenalChord.h"
#include "Bc2ReloadRequestProbe.h"
#include "Bc2MagazineReloadProbe.h"
#include "Bc2MagazineReloadSession.h"
#include "Bc2PhysicalReload.h"
#include "Bc2PhysicalTubeInteraction.h"
#include "Bc2Xm8BoltRuntime.h"
#include "Bc2PumpHold.h"
#include "Bc2PumpPart.h"
#include "Bc2GroundHeight.h"
#include "Bc2DroppedFloor.h"
#include "Bc2DroppedMagazineSpawn.h"
#include "Bc2InteractionZones.h"
#include "Bc2FloatingHands.h"
#include "Bc2ReloadSoundGate.h"
#include "Bc2ReloadAudioCue.h"
#include "Bc2OwnedReloadAudio.h"
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2PhysicalWeaponInteraction.h"
#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2MagazineDetached.h"
#include "Bc2MagazineDetachedProbe.h"
#include "Bc2PhysicalReloadProbe.h"
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
#include "Bc2PhysicalReloadLifecycleProbe.h"
static_assert(FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO>=1&&FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO<=4);
#endif
#include "fvr/interaction/SightFlip.h"
#include "fvr/interaction/SightFlipPackets.h"
#include "fvr/interaction/SightGrasp.h"
#include "fvr/interaction/SightVisualHandoff.h"
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/SightOwnershipRecovery.h"
#include "fvr/interaction/SupportOwnershipRecovery.h"
#include "fvr/interaction/ArmIk.h"
#include "fvr/interaction/AimFrame.h"
#include "fvr/interaction/RoomscaleFollow.h"
#include "fvr/interaction/ComfortCamera.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cmath>
#include <vector>
#include <mutex>
#include "fvr/interaction/FiringPoseHistory.h"
namespace fvr::bc2::gameplay {
namespace {
using UpdateFn=void(__thiscall*)(void*,float);using GatherFn=void(__thiscall*)(void*,void*);
UpdateFn updateOriginal=nullptr;GatherFn gatherOriginal=nullptr;
using FireOriginFn=std::uintptr_t(__thiscall*)(void*,void*);
FireOriginFn fireOriginOriginal=nullptr;FireOriginCandidates fireProfile{};
struct FireRecord {unsigned caller=0,effects=0,player=0,serverSoldier=0,clientSoldier=0,weapon=0,data=0,thread=0;ULONGLONG ms=0;std::array<float,16> matrix{},clientEffects{},serverEffects{};unsigned playerId=0,weaponKind=0,shotToken=0;bool tokenValid=false;bool clientPath=false,originWritten=false;unsigned phase=0;std::array<float,16> after{};std::optional<rigPublication::WeaponShotFrame> shot;std::optional<math::Matrix4> mappedShot;std::optional<interaction::FiringPoseSample> eventPose;};
std::array<FireRecord,32> fireRecords{};unsigned fireRecordCount=0;
std::atomic_flag fireRecordGate=ATOMIC_FLAG_INIT;
std::atomic<unsigned> fireOriginCalls=0,fireOriginMatches=0,fireOriginRejected=0;
using ClientShootFn=std::uintptr_t(__thiscall*)(void*,void*,float,unsigned,void*);
using MatrixCopyFn=std::uintptr_t(__thiscall*)(void*,const void*);
ClientShootFn clientShootOriginal=nullptr,serverShootOriginal=nullptr;MatrixCopyFn matrixCopyOriginal=nullptr;
using ComposeFn=std::uintptr_t(__thiscall*)(void*,void*,const void*);ComposeFn composeOriginal=nullptr;
std::mutex shotPoseMutex;interaction::FiringPoseHistory shotHistory;
std::atomic<unsigned> clientOriginWrites=0,serverOriginWrites=0,originWriteFailures=0,shotSourceChanges=0,originFallbacks=0,originWritesAfterLogFull=0;
std::atomic<unsigned> clientShotCalls=0,clientShotCopies=0,clientShotRejected=0;
AimCandidates aimingProfile{};InputBindingCandidates profile{};std::uintptr_t image=0;unsigned imageSize=0;
InputReader readInput=nullptr;std::array<void*,23> hooks{};
// 1P weapon FireEffects anchoring (Bc2WeaponEffects.h). hooks[17] is the
// verified spawn function; the setter is the engine's own handle mover.
using EffectSpawnFn=unsigned(__thiscall*)(void*,void*,const void*);EffectSpawnFn effectSpawnOriginal=nullptr;
using EffectSetFn=void(__thiscall*)(void*,unsigned,const void*);EffectSetFn effectSetTransform=nullptr;
using EffectComposeFn=void(__thiscall*)(void*,const void*,const void*,const void*,const void*,bool,void*);
EffectComposeFn effectComposeOriginal=nullptr;
WeaponEffectCandidates effectProfile{};EffectAnchorConfig effectAnchorConfig{};bool effectAnchorInstalled=false;
std::mutex effectAnchorMutex;WeaponEffectAnchor effectAnchor;
InputObserver observeInput=nullptr;
FeedbackWriter writeFeedback=nullptr;unsigned feedbackSent=0,feedbackDropped=0;
ReloadAudioWriter writeReloadAudio=nullptr;ReloadAudioOwnerWriter writeReloadAudioOwner=nullptr;
ReloadAudioReadyReader readReloadAudioReady=nullptr;
struct ReloadAudioSuppression {OwnedReloadAudioProof proof;std::uint64_t space=0;std::int64_t observedNs=0,deadlineNs=0;};
std::atomic<std::shared_ptr<const ReloadAudioSuppression>> reloadAudioSuppression;
std::optional<OwnedReloadAudioProof> reloadAudioBinding;
std::int64_t reloadAudioReadyUntil=0;
std::uint64_t reloadAudioOwnerSerial=0,reloadAudioEventSerial=0,reloadAudioWeapon=0,reloadAudioEquip=0,reloadAudioActor=0,reloadAudioSpace=0;
std::atomic<unsigned> reloadAudioSent=0,reloadAudioDropped=0;
std::atomic<unsigned> reloadAudioOwnerPublished=0,reloadAudioOwnerRejected=0,reloadAudioReadySamples=0,reloadAudioNotReady=0;
std::atomic<unsigned> reloadAudioProofAttempts=0,reloadAudioProofAccepted=0,reloadAudioProofReads=0,reloadAudioProofFailure=0;
std::atomic<bool> reloadAudioProofUnreadable=false;
std::atomic<unsigned> reloadAudioLaneSamples=0,reloadAudioSuppressed=0;
std::array<std::atomic<unsigned>,unsigned(ipc::ReloadAudioRole::Count)> reloadAudioRoleSent{};
// Live gun-event fvr rows (docs/GUN-EVENT-LOG.md). Game thread only: the
// gather and the consumer API callbacks it drives. Inert unless the log is open.
GunFvrTracker gunFvr;GunWeapon gunWeapon{};
std::string_view GunFeedbackName(interaction::FeedbackKind kind)noexcept{return GunFeedbackKindName(kind);}
std::atomic<bool> enabled=false;std::atomic<unsigned> active=0,inputThread=0;
std::atomic<unsigned> updates=0,localUpdates=0,gathers=0,onFoot=0,vehicle=0,stale=0,applied=0,committed=0,nativeInactive=0,mismatches=0,turnUnavailable=0,turns=0,moving=0,buttons=0;
bool motionAim=false,bodyFollow=false,observePoses=false,handPoses=false,deathProbe=false,equipProbe=false,equipHeld=true,muzzleFire=false,twoHandGrip=false;
bool weaponModeBindingVerified=false,weaponModeHeld=true,sightFlip=false;
std::optional<interaction::SightFlip> sightPolicy;
interaction::SightFlipPhase sightPhase=interaction::SightFlipPhase::Idle;
std::uint64_t sightAcknowledged=0;
std::optional<interaction::SightGraspBinding> sightGrasp;
std::optional<interaction::SightVisualHandoff> sightVisual;
std::uint64_t sightGraspGeneration=0;
const SightAdapterProfile* sightAdapterAtGrab=nullptr;
math::Matrix4 sightFrontAtGrab{};math::Vec3 sightPalmAtGrab{};interaction::SightMode sightGraspMode=interaction::SightMode::Primary;
std::uint64_t sightGraspToken=0;
std::atomic<unsigned> sightRequests=0,sightCommits=0,sightCancellations=0;
std::optional<ReloadStateBinding> reloadStateBinding;
SelectedMeshesObservation selectedMeshes;
void CancelPhysicalReload(PhysicalReloadResult* unpublished=nullptr,unsigned sourceFlags=0)noexcept;
ReloadButtonGesture reloadButtonGesture;
// Test-range arsenal: left trigger + right-stick flick signals the injected test-range
// DLL's events (never created here). FVR_BC2_ARSENAL_INPUT=0 disables.
ArsenalChord arsenalChord;bool arsenalInputOff=false;HANDLE arsenalEvents[2]{};ULONGLONG arsenalEventRetryAt=0;
bool ArsenalEventsReady()noexcept{
    if(arsenalEvents[0]&&arsenalEvents[1])return true;
    const auto now=GetTickCount64();if(now<arsenalEventRetryAt)return false;
    arsenalEventRetryAt=now+2000;
    if(!arsenalEvents[0])arsenalEvents[0]=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\BC2TestRange.ArsenalNext");
    if(!arsenalEvents[1])arsenalEvents[1]=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\BC2TestRange.ArsenalPrev");
    return arsenalEvents[0]&&arsenalEvents[1];
}
ReloadInputRouting reloadInputRouting;
interaction::WeaponReadinessTransitions weaponReadyTransitions;
struct AutomaticReloadRequest {
    interaction::PhysicalWeaponIdentity weapon{};
    interaction::HandInteractionOwner owner{};
    std::uint64_t token=0;
    std::int64_t waitingSinceNs=0;
    bool requested=false;
};
AutomaticReloadRequest automaticReload;
std::uint64_t automaticReloadSerial=0;
std::atomic<unsigned> automaticReloadHolds=0,automaticReloadStarts=0,automaticReloadCancels=0,shortReloadReleases=0;
void CancelAutomaticReload()noexcept {
    reloadButtonGesture.Reset();
    if(automaticReload.token){
        if(automaticReload.requested)physicalWeaponRuntime::CancelNativeReload(automaticReload.weapon,automaticReload.token);
        automaticReload={};++automaticReloadCancels;
    }
}
// Which caller cancelled an ACTIVE magazine reload from outside the consumer
// (cause 1, run 170455): 0 other, 1 reload owner cleared, 2 hand ownership
// reset (last reason kept), 3 input apply rejected.
unsigned physicalCancelSite=0;std::array<unsigned,4> physicalCancelSites{};char physicalCancelReason[24]{};
void CancelWeaponVisibilityProbe()noexcept;
std::optional<Bc2EmptyFireProbe> emptyFireFixture;
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
std::optional<Bc2ArmingEmptyProbe> armingEmptyFixture;
#endif
// A short input gap (stale gathers) keeps the native owner published so the
// empty-magazine gate still blocks the native auto-reload; run 114713 (F2000)
// lost 155 gathers right after a dry pull and the empty gun reloaded natively.
// Bounded to 2 s and only with no physical reload in progress.
ULONGLONG lastFreshTick=0;std::uint64_t lastFreshSpace=0,lastFreshGeneration=0;unsigned staleOwnerHolds=0;
void StopHaptics()noexcept;
void ResetPumpPresentation()noexcept;
void ClearReloadOwner()noexcept {
reloadAudioSuppression.store({},std::memory_order_release);
reloadAudioBinding.reset();
if(writeReloadAudioOwner)writeReloadAudioOwner(0,0,0);reloadAudioWeapon=0;
StopHaptics();CancelAutomaticReload();ResetPumpPresentation();
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
if(armingEmptyFixture)armingEmptyFixture->Cancel();
#endif
if(emptyFireFixture)emptyFireFixture->Cancel();CancelWeaponVisibilityProbe();physicalCancelSite=1;CancelPhysicalReload();selectedMeshes.Clear();reloadFlowRuntime::ClearOwner();}
std::array<ReloadStateSnapshot,128> reloadSnapshots{};
std::shared_ptr<const ReloadStateSnapshot> nativeActionFiring;
bool NativeActionCaptureRequested()noexcept {
    static const bool enabled=[]{char value[16]{};GetEnvironmentVariableA("FVR_BC2_NATIVE_ACTION_CAPTURE",value,sizeof(value));return std::strcmp(value,"1")==0;}();return enabled;
}
unsigned reloadSnapshotCount=0;std::array<unsigned,7> reloadStatuses{};
std::array<unsigned,4> reloadPublicationStatuses{};std::array<unsigned,10> reloadPublicationReasons{};
ULONGLONG reloadObservedAt=0,reloadObservedStart=0;
std::int64_t reloadObservedSourceNs=0;
bool reloadRequestMode=false;std::optional<Bc2ReloadRequestProbe> reloadRequestFixture;std::optional<Bc2MagazineReloadProbe> magazineReloadFixture;
bool physicalReloadMode=false;std::optional<Bc2PhysicalReload> physicalReload;
std::optional<Bc2PhysicalTubeInteraction> physicalTube;
bool UsePhysicalTube()noexcept{return physicalWeaponRuntime::Enabled()&&physicalTube.has_value();}
PhysicalReloadProbeState TubeProbeState(std::int64_t now)noexcept {
    return UsePhysicalTube()?physicalTube->ProbeState(now):physicalReload?physicalReload->ProbeState(now):PhysicalReloadProbeState{};
}
bool TubeLatchesLoadingHand()noexcept{return UsePhysicalTube()?physicalTube->LatchesLoadingHand():physicalReload&&physicalReload->LatchesLoadingHand();}
bool TubeBlocksEquipment()noexcept{return UsePhysicalTube()?physicalTube->BlocksEquipment():physicalReload&&physicalReload->BlocksEquipment();}
PhysicalReloadApi physicalReloadApi{};
std::optional<Bc2MagazineFrontend> magazinePhysical;MagazinePhysicalApi magazinePhysicalApi{};
std::optional<Bc2MagazinePhysicalProbe> magazinePhysicalFixture;
// SPAS physical pump: support-hand stroke along the barrel releases the native hold.
std::array<char,64> magazineSupportAsset{};MagazineSupportStatus magazineSupportStatus=MagazineSupportStatus::NoNativeProfile;unsigned magazineSupportChanges=0;
bool pumpSoundExpected=false;std::uint64_t pumpVisualShot=0;std::optional<rigPublication::Tracking::PumpVisual> pumpVisual;PumpStrokeConfig SpasPumpInputConfig()noexcept {
    PumpStrokeConfig config{SpasObservedForeEndStroke};config.travelScale=1.35f;
    char value[32]{};const auto n=GetEnvironmentVariableA("FVR_BC2_PUMP_TRAVEL_SCALE",value,sizeof(value));
    if(n&&n<sizeof(value)){char* end=nullptr;const auto scale=std::strtof(value,&end);
        if(end!=value&&!*end&&std::isfinite(scale)&&scale>=.5f&&scale<=3.f)config.travelScale=scale;}
    return config;
}
PumpPresentationState pumpPresentation{SpasPumpInputConfig()};unsigned pumpRacks=0,pumpRearStops=0,pumpFrames=0,pumpGripFrames=0;float pumpMaxTravel=0,pumpMinAlong=1e9f,pumpMaxAlong=-1e9f;bool pumpEnabled=false;unsigned pumpZoneRejects=0;float pumpLateralMin=1e9f,pumpLateralMax=-1e9f;
PumpCommandQueue pumpCommands;
// Post-rack settle (docs/SPAS-PUMP-BOLT-GRASPS-20261011.md): the physical stroke
// already showed the pump, so for the length of the native pump the fore-end
// stays on the original closed BoltAction sample instead of
// the native replay. FVR_BC2_PUMP_SETTLE_MS (default 1900, 0 = off).
unsigned pumpSettles=0;
void ResetPumpPresentation()noexcept {
    pumpPresentation.Reset();pumpVisual.reset();pumpSoundExpected=false;
    // Submitted commands remain owned by their original physical weapon until
    // their terminal receipts are consumed; cancellation never invents a rack.
}
std::int64_t PumpSettleNs()noexcept {
    static const std::int64_t ns=[]()->std::int64_t{char value[16]{};
        if(!GetEnvironmentVariableA("FVR_BC2_PUMP_SETTLE_MS",value,sizeof(value)))return 1900000000ll;
        const long ms=std::strtol(value,nullptr,10);return ms>0&&ms<=10000?std::int64_t(ms)*1000000:0;}();
    return ns;
}
unsigned policyInactiveGathers=0;
// Preparation-burst evidence (proto/reload): where a held probe trigger stops.
// Which term set the magazine sample's cancel: any, body draw, aim invalid, policy inactive, fixture, physical reload.
std::array<unsigned,6> magCancelTerms{};
struct PrepBurstCounters {unsigned window=0,noLease=0,notFull=0,injected=0,policyInactive=0,fireDerived=0,fireMasked=0,fireSent=0,blocked=0;};
PrepBurstCounters prepBurst;bool prepBurstFrame=false;
std::optional<Bc2MagazineDetached> magazineDetached;
std::optional<Bc2MagazineDetachedProbe> magazineDetachedFixture;
std::atomic<unsigned> magazineRecoverySuppressionCommits=0,magazineRecoverySuppressionFailures=0;
std::optional<Bc2BodyInventory> bodyInventory;
std::optional<Bc2BodyHolster> bodyHolster;
std::atomic<std::shared_ptr<const BodyHolsterProbeSample>> bodyHolsterSample;
std::optional<Bc2BodyHolsterProbe> bodyHolsterFixture;
std::atomic<std::shared_ptr<const BodyHolsterFixtureSample>> bodyHolsterFixtureSample;
std::optional<WeaponModeCommand> holsterQueuedDraw;
std::uint64_t holsterNativeTick=0;
std::atomic<unsigned> holsterSuppressionCommits=0,holsterSuppressionFailures=0,holsterFreePublications=0;
interaction::BodyAnchorConfig bodyAnchors{};
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
std::optional<Bc2PhysicalReloadLifecycleProbe> physicalReloadFixture;
#else
std::optional<Bc2PhysicalReloadProbe> physicalReloadFixture;
#endif
std::optional<Bc2WeaponVisibilityProbe> weaponVisibilityProbe;
std::atomic<std::shared_ptr<const WeaponVisibilityProbeSample>> weaponVisibilitySample;
std::atomic<unsigned> weaponVisibilityActionCommits=0,weaponVisibilityActionFailures=0;

struct SightRecord {
    ULONGLONG ms=0;std::uint64_t generation=0,contactGeneration=0,coherentGeneration=0,request=0;
    unsigned weapon=0,phase=0,cancel=0,packetReason=0;
    float distance=0,angle=0,squeeze=0,pairedSqueeze=0,supportDistance=0;
    bool contact=false,grabbed=false,committed=false,preferred=false,tracked=false,familyValid=false,paired=false,advanced=false;
};
std::array<SightRecord,192> sightRecords{};unsigned sightRecordCount=0,sightRecordNext=0;ULONGLONG sightRecordTime=0;
std::array<SightRecord,128> sightEvents{};unsigned sightEventCount=0,sightEventNext=0;
std::uint64_t sightRecordsTotal=0,sightEventsTotal=0;
interaction::SightFlipPackets sightPackets;
// Gesture summaries survive the periodic pose ring rolling over. Keep bounded
// native-relative geometry and extrema, not an unbounded tracking history.
struct SightGestureRecord {
    std::uint64_t id=0,startGeneration=0,endGeneration=0,request=0;
    ULONGLONG startMs=0,endMs=0;
    unsigned weapon=0,startMode=0,reason=0,freshSamples=0;
    math::Vec3 pivot{},axis{},nativePivot{},startHand{},endHand{};
    float minAngle=0,maxAngle=0,peakProgress=0,maxContactDistance=0;
    std::int64_t longestDetentNs=0;
    bool committed=false,finished=false;
};
std::array<SightGestureRecord,64> sightGestures{};
unsigned sightGestureCount=0,sightGestureNext=0;std::uint64_t sightGesturesTotal=0;
std::optional<unsigned> activeSightGesture;
void FinishSightGesture(const interaction::SightFlipResult& result){
    if(!activeSightGesture)return;
    auto& r=sightGestures[*activeSightGesture];r.endMs=GetTickCount64();
    r.reason=unsigned(result.reason);r.finished=true;activeSightGesture.reset();
}
void RecordSightGesture(const interaction::SightFlipSample& sample,const interaction::SightFlipResult& result,
    const rigPublication::WeaponSightContact& contact,unsigned weapon,bool fresh){
    if(result.grabbed&&sightPolicy){
        activeSightGesture=sightGestureNext;sightGestureNext=(sightGestureNext+1)%sightGestures.size();
        sightGestureCount=std::min(unsigned(sightGestures.size()),sightGestureCount+1);
        auto& r=sightGestures[*activeSightGesture];r={};r.id=++sightGesturesTotal;
        r.startMs=GetTickCount64();r.startGeneration=sample.sequence;r.weapon=weapon;r.startMode=unsigned(sample.nativeMode);
        r.pivot=sightPolicy->Config().pivotMeters;r.axis=sightPolicy->Config().axis;
        r.nativePivot=contact.pivotMeters;r.startHand=sample.handLocalMeters;r.endHand=r.startHand;
    }
    if(!activeSightGesture)return;
    auto& r=sightGestures[*activeSightGesture];r.endMs=GetTickCount64();r.endGeneration=sample.sequence;
    r.minAngle=std::min(r.minAngle,result.signedRadians);r.maxAngle=std::max(r.maxAngle,result.signedRadians);
    r.peakProgress=std::max(r.peakProgress,r.startMode==unsigned(interaction::SightMode::Primary)?result.signedRadians:-result.signedRadians);
    if(!r.request)r.longestDetentNs=std::max(r.longestDetentNs,result.detentDwellNs); // Stop at dispatch, not post-ack hold.
    if(fresh&&sample.contactValid&&std::isfinite(sample.handLocalMeters.x)&&std::isfinite(sample.handLocalMeters.y)&&std::isfinite(sample.handLocalMeters.z)){
        ++r.freshSamples;r.endHand=sample.handLocalMeters;
        if(std::isfinite(sample.contactDistanceMeters))r.maxContactDistance=std::max(r.maxContactDistance,sample.contactDistanceMeters);
    }
    if(result.request)r.request=result.request->id;
    if(result.committedMode)r.committed=true;
    if(result.released||result.cancelled)FinishSightGesture(result);
}
void RecordSight(const SightRecord& record,bool event){
    if(event||record.ms-sightRecordTime>=100){
        sightRecords[sightRecordNext]=record;sightRecordNext=(sightRecordNext+1)%sightRecords.size();
        sightRecordCount=std::min(unsigned(sightRecords.size()),sightRecordCount+1);sightRecordTime=record.ms;++sightRecordsTotal;
    }
    if(event){
        sightEvents[sightEventNext]=record;sightEventNext=(sightEventNext+1)%sightEvents.size();
        sightEventCount=std::min(unsigned(sightEvents.size()),sightEventCount+1);++sightEventsTotal;
    }
}
void ReleaseSightOwnership()noexcept;
void CancelPendingSight(std::uint64_t)noexcept;
void CancelSight(){sightAdapterAtGrab=nullptr;ReleaseSightOwnership();sightGrasp.reset();sightVisual.reset();sightPackets.Reset();if(sightPolicy){const auto result=sightPolicy->Update({});if(result.cancelled)++sightCancellations;CancelPendingSight(result.cancelledRequest);FinishSightGesture(result);}sightPhase=interaction::SightFlipPhase::Idle;sightAcknowledged=0;}

struct ModeRecord {ULONGLONG ms=0,ackMs=0;unsigned actor=0,weak=0,from=0,target=0,action=0;std::uint64_t owner=0,space=0;bool cancelled=false;std::uint64_t gesture=0;};
std::array<ModeRecord,32> modeRecords{};unsigned modeRecordCount=0,modeRecordNext=0;
std::optional<unsigned> pendingMode;
std::atomic<unsigned> modeRequests=0,modeRejections=0,modeAcknowledgements=0;
// Static-initialization env read: each value is consumed before the next read.
std::string_view ProcessEnvText(std::string_view name)noexcept {
    static char value[32]{};const std::string key(name);
    const auto n=GetEnvironmentVariableA(key.c_str(),value,sizeof(value));return n&&n<sizeof(value)?std::string_view(value,n):std::string_view{};
}
// One interaction zone table for every grab range (Bc2InteractionZones.h).
const bc2::InteractionZoneSettings zoneSettings=bc2::ParseInteractionZoneSettings(ProcessEnvText);
const interaction::InteractionZoneTable interactionZones=bc2::ResolveInteractionZones(zoneSettings);
// FVR_BC2_SHORT_B_RACK_AFTER_INSERT=0: short B ejects any seated, ready magazine again (Bc2ShortReloadAction.h).
const bool shortRackAfterInsert=bc2::ParseShortReloadRackAfterInsert(ProcessEnvText);
interaction::SupportGrip supportGrip{bc2::SupportGripConfigFor(interactionZones[interaction::InteractionZoneKind::Support])};
std::uint64_t supportFlickKept=0;
// FVR_HANDS_MODE=floating|ik (default ik; Bc2FloatingHands.h).
const bc2::HandsModeSettings handsModeSettings=bc2::ParseHandsModeSettings(ProcessEnvText);
std::atomic<unsigned> supportSamples=0,supportGrabs=0,supportReleases=0,supportPreservationFailures=0,supportFireSamples=0,supportAimSamples=0;
float supportAimResidual=0;
struct SupportRecord {ULONGLONG ms=0;std::uint64_t generation=0;float distance=0,squeeze=0,angle=0;bool contact=false,holding=false,engaged=false,released=false;math::Quaternion rawAim{},aim{};unsigned weaponKind=0;unsigned reason=0,actions=0;std::uint64_t token=0;};
std::array<SupportRecord,192> supportRecords{};unsigned supportRecordCount=0,supportRecordNext=0;ULONGLONG supportRecordTime=0;
// Keep the earliest transition evidence separately from rolling pose samples.
// A long idle/headset-off tail must not overwrite the first unexpected drop.
struct SupportEvent {
    SupportRecord sample{};ULONGLONG eventMs=0;std::uint64_t previousToken=0,rigEpoch=0,space=0;
    unsigned actor=0,weak=0,weapon=0,cancelFlags=0;
    bool focused=false,headTracked=false,leftGripTracked=false,rightGripTracked=false,rightAimTracked=false;
    math::Vec3 leftGrip{},rightGrip{};const char* resetReason=nullptr;
};
std::array<SupportEvent,512> supportEvents{};unsigned supportEventCount=0,supportEventTotal=0,supportEventDropped=0,supportForcedResets=0;
SupportEvent supportAttemptRecord{};bool supportAttemptActive=false;
std::array<std::atomic<unsigned>,9> supportReleaseCounts{};
SupportEvent lastSupportSample{};
void RetainSupportEvent(const SupportEvent& event)noexcept {
    ++supportEventTotal;
    if(supportEventCount<supportEvents.size())supportEvents[supportEventCount++]=event;
    else ++supportEventDropped;
}
void ResetSupportGrip(const char* reason)noexcept {
    if(lastSupportSample.sample.holding){
        auto event=lastSupportSample;event.eventMs=GetTickCount64();event.previousToken=event.sample.token;
        event.resetReason=reason;RetainSupportEvent(event);++supportForcedResets;
    }
    supportGrip.Reset();lastSupportSample.sample.holding=false;lastSupportSample.sample.token=0;
}

std::atomic<unsigned> equipCommands=0,vehicleInteractions=0,nextWeaponCommands=0,previousWeaponCommands=0;
std::atomic<unsigned> grenadeSamples=0,leftOnlyActions=0,rightOnlyActions=0,leftLossFire=0,untrackedFire=0,recoveryFireSuppressed=0;
BodyPositionCandidates bodyProfile{};interaction::RoomscaleFollow roomscale;
struct FollowRecord {ULONGLONG ms=0;math::Vec3 head{},body{},consumed{};float forward=0,strafe=0;};
std::array<FollowRecord,96> followRecords{};unsigned followRecordCount=0;ULONGLONG followRecordTime=0;
math::Vec3 consumedOffset{};std::atomic<unsigned> followSamples=0,bodyReadFailures=0;
FirstPersonPoseCandidates poseProfile{};RigCandidates rigProfile{};
using AnimationUpdateFn=std::uintptr_t(__thiscall*)(void*,float,float,bool);
using EvaluateFn=std::uintptr_t(__thiscall*)(void*,float,bool);
using PostEvaluateFn=std::uintptr_t(__thiscall*)(void*,float);
AnimationUpdateFn animationOriginal=nullptr;EvaluateFn evaluateOriginal=nullptr;PostEvaluateFn postOriginal=nullptr;
using WeaponWorldFn=std::uintptr_t(__thiscall*)(void*,void*);WeaponWorldFn weaponWorldOriginal=nullptr;
struct PhaseRecord {unsigned sequence=0,phase=0,thread=0,animation=0,world=0,skin=0,overrideSkin=0;bool overrideActive=false;std::uint64_t worldHash=0,skinHash=0,overrideHash=0;};
std::array<PhaseRecord,480> phaseRecords{};unsigned phaseRecordCount=0;
std::atomic_flag phaseRecordGate=ATOMIC_FLAG_INIT;
std::atomic<unsigned> animationCalls=0,animationInFlight=0,animationOverlap=0,phaseFailures=0;
std::atomic<ULONGLONG> phaseLastTime=0;
// effectRoot: the caller-owned weapon-getter output of THIS animation update
// (update stack), valid until the update returns; the 1P FireEffects Update at
// update +0x140 reads the same buffer.
struct AnimationScope {unsigned animation=0,sequence=0;bool sample=false;EffectAnchorOwner effectOwner{};unsigned effectRoot=0;bool effectRootWritten=false;};thread_local AnimationScope* animationScope=nullptr;

std::optional<RigSnapshot> observedRig;unsigned rigAttempts=0;
unsigned ikPreviewSolved=0,ikPreviewFailed=0,ikPreviewEdits=0,ikPreviewClamps=0;
float ikPreviewLengthError=0,ikPreviewTargetError=0;
void PreviewArmIk(const RigSnapshot& rig){
    const auto point=[](const math::Matrix4& m){return math::Vec3{m.values[3][0],m.values[3][1],m.values[3][2]};};
    const auto diff=[](math::Vec3 a,math::Vec3 b){return math::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto distance=[&](const math::Matrix4& a,const math::Matrix4& b){auto d=diff(point(a),point(b));return std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);};
    const auto& native=rig.evaluatedWorld;
    for(unsigned sample=0;sample<12;++sample){
        auto left=native[rig.left.wrist],right=native[rig.right.wrist];
        // A bounded copied-pose exercise, not controller calibration or a write.
        const float angle=(float(sample)-5.5f)*.04f;
        math::Matrix4 roll{};for(unsigned n=0;n<4;++n)roll.values[n][n]=1;
        roll.values[0][0]=roll.values[1][1]=std::cos(angle);roll.values[0][1]=std::sin(angle);roll.values[1][0]=-std::sin(angle);
        left=interaction::Multiply(roll,left);right=interaction::Multiply(roll,right);
        left.values[3][0]+=.08f*std::sin(float(sample));right.values[3][2]+=.08f*std::cos(float(sample));
        const auto solved=interaction::SolveTrackedArms(rig.parents,native,rig.left,rig.right,
            {left,diff(point(native[rig.left.elbow]),point(native[rig.left.shoulder]))},
            {right,diff(point(native[rig.right.elbow]),point(native[rig.right.shoulder]))});
        if(!solved){++ikPreviewFailed;continue;}const auto plan=BuildRigPosePlan(rig,solved->writes);
        if(!plan){++ikPreviewFailed;continue;}++ikPreviewSolved;ikPreviewEdits+=unsigned(plan->edits.size());
        auto posed=native;for(const auto& write:solved->writes)posed[write.index]=write.transform;
        for(auto arm:{rig.left,rig.right})for(auto pair:{std::pair{arm.shoulder,arm.elbow},std::pair{arm.elbow,arm.wrist}})
            ikPreviewLengthError=std::max(ikPreviewLengthError,std::abs(distance(native[pair.first],native[pair.second])-distance(posed[pair.first],posed[pair.second])));
        for(unsigned side=0;side<2;++side){ikPreviewClamps+=solved->reachClamped[side]?1u:0u;ikPreviewTargetError=std::max(ikPreviewTargetError,solved->targetError[side]);}
    }
}

using PoseFn=std::uintptr_t(__thiscall*)(void*,void*);
PoseFn worldPoseOriginal=nullptr,rootPoseOriginal=nullptr;
struct PoseRecord {unsigned kind=0,thread=0,caller=0,owner=0,object=0;ULONGLONG ms=0;std::array<float,16> matrix{};};
std::array<PoseRecord,64> poseRecords{};unsigned poseRecordCount=0;
std::atomic_flag poseRecordGate=ATOMIC_FLAG_INIT;std::array<ULONGLONG,2> poseRecordTime{};
std::atomic<unsigned> poseCalls=0,poseRejected=0;

struct Anchor {std::atomic<unsigned> sequence=0,soldier=0,weak=0;std::atomic<std::uint64_t> space=0;std::atomic<float> yaw=0,consumedX=0,consumedZ=0,units=1;};Anchor anchor;
unsigned anchorSoldier=0,anchorWeak=0;std::uint64_t anchorSpace=0;float baseYaw=0,lastHandYaw=0,lastHeadYaw=0;unsigned sprintHeadings=0;
std::atomic<unsigned> aimSamples=0,baseViews=0;
struct BaseRecord {float nativeYaw=0,nativePitch=0,sourceYaw=0,viewYaw=0,bodyYaw=0;bool sharedEye=false;math::Vec3 sourcePosition{},eyePosition{};unsigned weapon=0;ULONGLONG ms=0;std::uint64_t space=0;};
std::array<BaseRecord,192> baseRecords{};std::atomic<unsigned> baseRecordCount=0;ULONGLONG baseRecordTime=0;
void PublishAnchor(unsigned soldier,unsigned weak,std::uint64_t space,float yaw,float units)noexcept {
    anchor.sequence.fetch_add(1,std::memory_order_acq_rel);
    anchor.soldier.store(soldier);anchor.weak.store(weak);anchor.space.store(space);anchor.yaw.store(yaw);anchor.consumedX.store(consumedOffset.x);anchor.consumedZ.store(consumedOffset.z);anchor.units.store(units);
    anchor.sequence.fetch_add(1,std::memory_order_release);
}
interaction::ControllerActions actions;
interaction::TurnOptions turnOptions;
interaction::VehicleControllerActions vehicleActions;
ContextInteractEvidence contextInteractEvidence;
std::atomic<unsigned> vehicleRouteRejects=0,vehicleUnsupported=0,vehicleDriveCommits=0,vehicleCameraViews=0;
std::atomic<unsigned> vehicleCameraWaits=0,vehicleCameraUnsupportedFrames=0;
std::atomic<unsigned> vehicleSeatEntity=0,vehicleSeatEntry=0;
std::atomic<std::uint64_t> vehicleRouteFingerprint=0;
std::atomic<float> vehicleThrottle=0,vehicleSteer=0;
std::atomic<unsigned> vehicleThrottleNonzero=0,vehicleSteerNonzero=0,vehicleReadbackFailures=0;
std::atomic<float> vehicleThrottleMaximum=0,vehicleSteerMaximum=0;
struct VehicleGatherRecord {
    unsigned entity=0,entry=0,soldier=0,weak=0,mask=0,permissions=0;
    std::uint64_t actorGeneration=0,seatGeneration=0,space=0,inputSequence=0;
    ULONGLONG ms=0;std::array<float,2> before{},written{},observed{};bool verified=false;
};
std::array<VehicleGatherRecord,16> vehicleGatherRecords{};std::atomic<unsigned> vehicleGatherCount=0;
VehicleGatherRecord previousVehicleGather{};bool havePreviousVehicleGather=false;ULONGLONG vehicleGatherRecordedAt=0;
std::mutex vehicleViewMutex;
using VehicleViewState=interaction::VehicleCameraSample;
VehicleViewState vehicleView{};interaction::VehicleCameraAnchor vehicleCameraAnchor;
bool boatHeadAim=false,boatHeadFire=false,boatAimReady=false;
BoatAimBinding boatAimBinding{};VehicleRouteSnapshot boatAimSeat{};
interaction::VehicleHeadAim boatHeadPolicy;math::Pose boatAimReference{};
std::atomic<unsigned> boatAimSamples=0,boatAimRejects=0,boatAimCommits=0,boatAimFireCommits=0,boatAimCameraViews=0;
struct BoatAimRecord {ULONGLONG ms=0;std::uint64_t sequence=0;float yaw=0,pitch=0,yawError=0,pitchError=0,jointYaw=0,jointPitch=0,fire=0;};
std::array<BoatAimRecord,64> boatAimRecords{};unsigned boatAimRecordCount=0;ULONGLONG boatAimRecordedAt=0;
struct VehicleCameraRecord {
    interaction::VehicleTrackingOwner owner{};unsigned frame=0,appliedMask=0;ULONGLONG ms=0;
    runtime::TrackingFrame tracking{};math::Matrix4 native{},adjusted{};
    std::array<math::Matrix4,2> planned{},applied{};
};
std::array<VehicleCameraRecord,16> vehicleCameraRecords{};unsigned vehicleCameraRecordCount=0;
bool CameraEvidenceMatrix(const RenderViewCopy& copy,math::Matrix4& matrix)noexcept {
    engine::FrostbiteCameraInput source{};std::memcpy(&source.transform,copy.bytes.data()+0x50,64);
    std::memcpy(&source.nearPlane,copy.bytes.data()+0x1c,4);std::memcpy(&source.farPlane,copy.bytes.data()+0x20,4);source.worldUnitsPerMeter=1;
    const auto result=engine::CanonicalCamera(source);if(!result)return false;matrix=result->camera;return true;
}
void PublishVehicleView(const VehicleViewState& next)noexcept {
    std::lock_guard lock(vehicleViewMutex);
    if(interaction::PublishVehicleCameraSample(vehicleView,next))vehicleCameraAnchor.Reset();
}
std::optional<VehicleSeatProfile> cachedVehicleProfile;
VehicleSeatIdentity cachedVehicleIdentity{};std::uint64_t cachedVehicleFingerprint=0;
unsigned cachedVehicleData=0,cachedEntryData=0;
interaction::InputFrame cachedInput{};std::int64_t inputDeadline=0;
bool Read(std::uintptr_t at,void* out,std::size_t n)noexcept {SIZE_T actual=0;return at>=0x10000&&at<=UINT32_MAX-n&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&actual)&&actual==n;}
unsigned U32(std::uintptr_t at)noexcept {unsigned out=0;Read(at,&out,4);return out;}
bool InImage(unsigned at,unsigned n)noexcept{return at>=image&&std::uint64_t(at)+n<=image+imageSize;}
struct Owner {unsigned manager=0,player=0,weak=0,soldier=0,controlled=0,entry=0,router=0,cache=0,weapon=0,aim=0;WeaponEquipmentIdentity equipment{};bool foot=false;bool operator==(const Owner&)const=default;};
bool Type(unsigned object,const char* expected)noexcept {
    // Inspect the virtual reflection getter without invoking native code.
    const auto vt=U32(object);if(!InImage(vt,12))return false;const auto getter=U32(vt+8);
    std::array<unsigned char,6> code{};if(!InImage(getter,6)||!Read(getter,code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return false;
    unsigned info=0;std::memcpy(&info,code.data()+1,4);if(!InImage(info,8))return false;
    const auto metadata=U32(info+4),name=U32(metadata);char text[64]{};const auto n=std::strlen(expected)+1;
    return n<=sizeof(text)&&InImage(metadata,4)&&InImage(name,unsigned(n))&&Read(name,text,n)&&!std::memcmp(text,expected,n);
}
bool Resolve(Owner& o)noexcept {
    o.manager=U32(image+profile.gameplay.contextObject+8);if(U32(o.manager)!=image+profile.gameplay.managerVtable)return false;
    o.player=U32(o.manager+profile.gameplay.localPlayerOffset);if(!o.player)return false;
    const auto vt=U32(o.player);if(!InImage(vt,0x3c)||U32(vt+0x38)!=image+profile.controlledGetter)return false;
    unsigned char flags=0;if(!Read(o.player+0xccd,&flags,1)||!(flags&8))return false;
    o.weak=U32(o.player+profile.gameplay.soldierWeakOffset);const auto target=U32(o.weak);if(target<4)return false;o.soldier=target-4;
    if(!Type(o.soldier,"ClientSoldierEntity")||U32(o.soldier+0x220)!=o.player)return false;
    const auto controlled=U32(o.player+0xc68),attached=U32(o.player+0xc60);
    const bool useAttached=controlled==o.soldier&&attached;
    o.controlled=useAttached?attached:controlled;const auto slot=U32(o.player+(useAttached?0xc64:0xc6c));
    const auto begin=U32(o.controlled+0x7c),end=U32(o.controlled+0x80);
    if(!begin||end<begin||end-begin>256||(end-begin)%4||slot>=(end-begin)/4)return false;
    o.entry=U32(begin+slot*4);o.router=U32(o.entry+0x198);o.cache=U32(o.player+profile.gameplay.inputCacheOffset);
    if(U32(o.router)!=image+profile.gameplay.inputRouterVtable||U32(o.cache)!=image+profile.cacheVtable)return false;
    o.foot=o.controlled==o.soldier;
    if(o.foot){
        unsigned char soldierFlags=0;Read(o.soldier+0x114,&soldierFlags,1);
        const auto inventory=U32(o.soldier+((soldierFlags&1)?0x24c:0x248)),index=U32(inventory+0x14c);
        const auto first=U32(o.soldier+0x260),last=U32(o.soldier+0x264);
        if(first&&last>=first&&last-first<=256&&!((last-first)%4)&&index<(last-first)/4){
            const auto weapon=U32(first+4*index),data=U32(weapon+4),aim=U32(weapon+0x2c);
            if(Type(data,"SoldierWeaponData")&&aim&&U32(aim)==U32(data+0x9c)&&Type(U32(aim),"SoldierAimingSimulationData")){
                const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
                    [](void*,unsigned at,const char* name){return Type(at,name);}};
                const auto equipment=ReadWeaponEquipmentIdentity(memory,weapon);
                if(!equipment||equipment->data!=data)return false;
                o.weapon=weapon;o.aim=aim;o.equipment=*equipment;
            }
        }
    }
    return U32(o.manager+profile.gameplay.localPlayerOffset)==o.player&&U32(o.weak)==target;
}
bool BodyPosition(const Owner& owner,math::Vec3& out)noexcept {
    unsigned char flags=0;if(!Read(owner.soldier+0x114,&flags,1)||!(flags&1))return false;
    const auto authority=U32(owner.soldier+0x230),character=U32(authority+4);
    if(!character||character!=U32(owner.soldier+0x238)||!Type(character,"CharacterEntity")||U32(U32(character)+0x8c)!=image+bodyProfile.getter)return false;
    const auto proxy=U32(character+0xbc),body=U32(character+0x40);
    const auto position=proxy?proxy+0x110:U32(U32(body+0x30)+0x18)+0x30;
    if(!Read(position,&out,12)||!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z))return false;
    out.z=-out.z;return U32(authority+4)==character&&U32(owner.soldier+0x238)==character;
}
bool Writable(unsigned at,unsigned n)noexcept {
    MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery(reinterpret_cast<void*>(at),&mbi,sizeof(mbi))||mbi.State!=MEM_COMMIT||(mbi.Protect&PAGE_GUARD))return false;
    const auto p=mbi.Protect&0xff;return (p==PAGE_READWRITE||p==PAGE_EXECUTE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_WRITECOPY)&&
        std::uintptr_t(mbi.BaseAddress)<=at&&std::uint64_t(at)+n<=std::uintptr_t(mbi.BaseAddress)+mbi.RegionSize;
}
struct Callback {Callback(){++active;}~Callback(){--active;}};
// Reload sounds (docs/re BC2_DISASM_WEAPON_SOUNDS.md section 4): the soldier's
// AnimSound listener OnAnimStart 0x881650 plays the level AnimationSoundMap
// sound mapped to each starting animation clip, if listener+0x50 matches
// soldier+0x475 bit0. Observation only: the original runs exactly once and
// every start is logged as an anim_sound gun-event row (GUID, views, played).
using AnimSoundFn=void(__thiscall*)(void*,const void*,int);AnimSoundFn animSoundStartOriginal=nullptr,animSoundStopOriginal=nullptr;
std::atomic<unsigned> animSoundStarts=0,animSoundPlayed=0;
// F3: the physical reload owns its clip sound (Bc2ReloadSoundGate.h).
ReloadSoundGate reloadSoundGate;bool reloadSoundEnabled=false;
// SPAS pump clip on the physical stroke (Bc2PumpHold.h): learned 100-900 ms
// after the shot, replayed at the rack (or when the hold releases natively).
ReloadSoundGate pumpSoundGate{ReloadSoundTiming{100000000,900000000,9000000000ll,4000000000ll}};
std::array<ReloadSoundGate*,2> soundGates{&reloadSoundGate,&pumpSoundGate};
ReloadAudioCue rackAudioAudition;
std::atomic<unsigned> rackAudioAttempts=0,rackAudioRejected=0,rackAudioPlayed=0,rackAudioStopped=0;
std::atomic<unsigned> rackRejectProof=0,rackRejectResolve=0,rackRejectChanged=0,rackRejectRequest=0,rackRejectCallback=0,rackRejectDispatchOwner=0;
std::atomic<unsigned> rackProofFailure=0,rackProofReads=0,rackListenerMisses=0;std::atomic<bool> rackProofUnreadable=false;
const std::optional<ReloadAudioProfiles>& RackAudioProfile()noexcept{
    static const auto profile=[]()->std::optional<ReloadAudioProfiles>{
        char genericEnabled[4]{},profiles[ReloadAudioProfiles::Capacity*165]{};
        const auto enabledSize=GetEnvironmentVariableA("FVR_BC2_RACK_AUDIO_AUDITION",genericEnabled,sizeof(genericEnabled));
        const auto profilesSize=GetEnvironmentVariableA("FVR_BC2_RACK_AUDIO_PROFILES",profiles,sizeof(profiles));
        // An explicit generic setting wins, including disabled/malformed input;
        // never fall back to a stale legacy M9 profile in that case.
        if(enabledSize||profilesSize){
            if(enabledSize!=1||genericEnabled[0]!='1'||!profilesSize||profilesSize>=sizeof(profiles))return {};
            return ParseReloadAudioProfiles(std::string_view(profiles,profilesSize));}
        char enabled[4]{},value[192]{};
        if(GetEnvironmentVariableA("FVR_BC2_M9_RACK_AUDIO_AUDITION",enabled,sizeof(enabled))!=1||enabled[0]!='1')return {};
        const auto n=GetEnvironmentVariableA("FVR_BC2_M9_RACK_AUDIO_PROFILE",value,sizeof(value));
        return n&&n<sizeof(value)?ParseReloadAudioProfiles(std::string_view(value,n)):std::nullopt;
    }();return profile;
}
ReloadAudioOwner AudioOwner(const Owner& o)noexcept{return {o.player,o.soldier,o.weak,o.weapon,o.equipment.data};}
std::optional<ReloadAudioProof> ReadRackAudioProof(const Owner& owner)noexcept{
    using Failure=ReloadAudioResolveFailure;
    const auto reject=[](Failure failure){rackProofFailure=unsigned(failure);rackProofReads=0;rackProofUnreadable=false;return std::optional<ReloadAudioProof>{};};
    const auto& expected=RackAudioProfile();if(!expected)return reject(Failure::Disabled);if(!owner.foot)return reject(Failure::Owner);
    // The verified singleton getter ends in MOV EAX,object; RET. Read its return immediate,
    // do not invoke a new native API or assume the object is at an env address.
    constexpr unsigned getterRva=0x7ff0d3-0x400000;std::array<unsigned char,6> getter{};unsigned object=0;
    if(!InImage(image+getterRva,6)||!Read(image+getterRva,getter.data(),getter.size())||getter[0]!=0xb8||getter[5]!=0xc3)return reject(Failure::Getter);
    std::memcpy(&object,getter.data()+1,4);if(!InImage(object,0x4c))return reject(Failure::Getter);
    const ReloadAudioMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
        [](void*,unsigned at,const char* type){return Type(at,type);}};
    ReloadAudioResolution diagnostic;const auto proof=ResolveReloadAudioCue(memory,AudioOwner(owner),*expected,object+0x48,&diagnostic);
    if(!proof){rackProofReads=diagnostic.reads;rackProofUnreadable=diagnostic.unreadable;rackProofFailure=unsigned(diagnostic.failure);}return proof;
}
// Private, opt-in identity evidence for joining the existing hand/placement
// observations. No cue admission, transforms, native calls or game-state writes.
void ObserveAudioIdentity(const Owner& owner,const interaction::InputFrame& input,
    std::uint64_t actorGeneration,std::uint64_t equipmentGeneration,
    std::uint64_t renderGeneration,std::uint64_t handGeneration,
    std::int64_t deadlineQpc,std::int64_t frequency)noexcept {
    struct Key {Owner owner;ReloadAudioGuid guid{};std::uint64_t actor=0,equipment=0,render=0,hand=0,space=0;bool operator==(const Key&)const=default;};
    struct Sink {
        std::array<wchar_t,1024> path{};std::mutex mutex;std::array<Key,16> seen{};
        unsigned count=0;HANDLE file=INVALID_HANDLE_VALUE;bool enabled=false,failed=false;
        Sink()noexcept {
            const auto n=GetEnvironmentVariableW(L"FVR_BC2_AUDIO_IDENTITY_PATH",path.data(),DWORD(path.size()));
            enabled=n>3&&n<path.size()&&((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&
                path[1]==L':'&&(path[2]==L'\\'||path[2]==L'/');
        }
        ~Sink(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    };
    static Sink sink;
    if(!sink.enabled||!owner.foot||!owner.weapon||!input.generation||!input.focused||!input.headValid||
       !interaction::ValidInput(input)||!actorGeneration||!equipmentGeneration||!input.spaceGeneration||frequency<=0)return;
    std::unique_lock lock(sink.mutex,std::try_to_lock);if(!lock.owns_lock()||sink.failed||sink.count==sink.seen.size())return;
    Owner before{},after{};if(!Resolve(before)||before!=owner)return;
    const ReloadAudioMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
        [](void*,unsigned at,const char* type){return Type(at,type);}};
    reload_audio_detail::Reader reader{memory};ReloadAudioGuid guid{},again{};unsigned ref=0,refAfter=0;
    if(!reader.Type(owner.equipment.data,"SoldierWeaponData")||
       !reader.Get(std::uint64_t(owner.equipment.data)+4,ref)||!reader.Read(ref,guid.data(),guid.size())||
       std::all_of(guid.begin(),guid.end(),[](auto b){return b==0;}))return;
    const Key key{owner,guid,actorGeneration,equipmentGeneration,renderGeneration,handGeneration,input.spaceGeneration};
    for(unsigned n=0;n<sink.count;++n)if(sink.seen[n]==key)return;
    if(!Resolve(after)||after!=before||!reader.Get(std::uint64_t(after.equipment.data)+4,refAfter)||refAfter!=ref||
       !reader.Read(refAfter,again.data(),again.size())||again!=guid)return;
    LARGE_INTEGER stamp{};if(!QueryPerformanceCounter(&stamp)||stamp.QuadPart<=0||stamp.QuadPart>=deadlineQpc)return;
    // EquipmentIdentity rejects unterminated/overlong names and double-samples
    // the typed data, Name(+0xc), persistence and ClientWeapon association.
    const auto name=owner.equipment.Asset();if(name.empty())return;
    std::array<char,128> escaped{};std::size_t at=0;
    for(char c:name){if(c=='"'||c=='\\')escaped[at++]='\\';escaped[at++]=c;}
    constexpr char hex[]="0123456789abcdef";std::array<char,33> encoded{};
    for(unsigned n=0;n<guid.size();++n){encoded[2*n]=hex[guid[n]>>4];encoded[2*n+1]=hex[guid[n]&15];}
    std::array<char,2048> line{};
    const int size=std::snprintf(line.data(),line.size(),
        "{\"schema\":1,\"generation\":%llu,\"owner_generation\":%llu,\"equipment_generation\":%llu,"
        "\"render_equipment_generation\":%llu,\"hand_equipment_generation\":%llu,\"space\":%llu,"
        "\"player\":%u,\"soldier\":%u,\"weak\":%u,\"weapon\":%u,\"weapon_data\":%u,\"persistence\":%u,"
        "\"name\":\"%s\",\"data_guid_memory_hex\":\"%s\",\"qpc\":%lld,\"qpc_frequency\":%lld}\n",
        static_cast<unsigned long long>(input.generation),static_cast<unsigned long long>(actorGeneration),
        static_cast<unsigned long long>(equipmentGeneration),static_cast<unsigned long long>(renderGeneration),
        static_cast<unsigned long long>(handGeneration),static_cast<unsigned long long>(input.spaceGeneration),
        owner.player,owner.soldier,owner.weak,owner.weapon,owner.equipment.data,owner.equipment.persistence,
        escaped.data(),encoded.data(),static_cast<long long>(stamp.QuadPart),static_cast<long long>(frequency));
    if(size<=0||std::size_t(size)>=line.size())return;
    if(sink.file==INVALID_HANDLE_VALUE){
        sink.file=CreateFileW(sink.path.data(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(sink.file==INVALID_HANDLE_VALUE){sink.failed=true;return;}
    }
    DWORD written=0;
    if(!WriteFile(sink.file,line.data(),DWORD(size),&written,nullptr)||written!=DWORD(size)||!FlushFileBuffers(sink.file)){
        sink.failed=true;return;
    }
    sink.seen[sink.count++]=key;
}
bool RackAudioListener(std::uintptr_t listener,unsigned soldier)noexcept{
    const auto table=U32(listener);std::uint8_t view=0,flags=0;
    return listener&&InImage(table,12)&&U32(table+4)==reinterpret_cast<std::uintptr_t>(hooks[13])&&
        U32(table+8)==reinterpret_cast<std::uintptr_t>(hooks[14])&&U32(listener+4)==soldier&&
        Read(listener+0x50,&view,1)&&Read(soldier+0x475,&flags,1)&&view==(flags&1);
}
// The replay/stop must run where the game calls the listener. Waiting for its
// next clip callback made the replay land at the pouch grab, not the pull (run
// 083229); the gather and the soldier animation update are other threads
// (runs 085753/085925: listener thread 34288, gather/update 39140).
std::atomic<std::uint32_t> animThreadId=0;std::atomic<unsigned> animDispatchWork=0,animDispatchOwners=0,animSoundCalls=0,animSoundInSoldierUpdate=0;std::atomic<std::uint32_t> animationHookThread=0,animDispatchThread=0;
std::int64_t AnimNowNs()noexcept{LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
    return std::int64_t(q.QuadPart/f.QuadPart)*1000000000+std::int64_t(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
void AnimSoundRow(std::uint32_t soldier,std::uint8_t listenerView,std::uint8_t soldierFlags,const AnimGuid& id,bool played,int callback,int action)noexcept{
    if(!GunEvents().Enabled())return;
    char hex[33]{};for(unsigned n=0;n<16;++n){static constexpr char d[]="0123456789abcdef";hex[2*n]=d[id[n]>>4];hex[2*n+1]=d[id[n]&15];}
    GunEvent e;e.phase=GunEventPhase::AnimSound;e.asset=gunWeapon.asset;e.weaponId=gunWeapon.id;SetGunText(e.detail,std::string_view(hex,32));
    // action: 0 native start, 1 withheld for the physical reload, 2 replayed on the pull, 3 replay stopped
    SetGunText(e.reason,action==1?"withheld":action==2?"replayed":action==3?"stopped":"native");
    e.v[0]=listenerView;e.v[1]=soldierFlags&1;e.v[2]=played;e.v[3]=callback;e.wide=soldier;e.flags|=gun_flag::Wide;GunEvents().Push(e);
}
void RackAudioWork(void* listener,unsigned soldier,std::int64_t now)noexcept{
    if(!rackAudioAudition.Due(now)||rackAudioAudition.Owner().soldier!=soldier)return;
    Owner before{},after{};const bool owned=Resolve(before)&&before.foot&&before.soldier==soldier;
    const auto proof=owned?ReadRackAudioProof(before):std::nullopt;
    const bool coherent=owned&&Resolve(after)&&before==after;
    if(!coherent||!proof){++rackAudioRejected;++rackRejectCallback;}
    const auto w=rackAudioAudition.Poll(coherent?AudioOwner(after):ReloadAudioOwner{},coherent?proof:std::nullopt,
        reinterpret_cast<std::uintptr_t>(listener),coherent&&RackAudioListener(reinterpret_cast<std::uintptr_t>(listener),soldier),AnimNowNs());
    if(w.stop){animSoundStopOriginal(listener,w.guid.data(),0);++rackAudioStopped;}
    if(w.play){animSoundStartOriginal(listener,w.guid.data(),0);++rackAudioPlayed;}
}
// Runs due replay/stop work on the animation thread, inside a native callback.
void AnimSoundWork(void* listener,std::uint32_t soldier)noexcept{
    if(!reloadSoundEnabled)return;const auto now=AnimNowNs();
    for(auto* gate:soundGates){const auto w=gate->Poll(now,listener,soldier);
    if(w.stop&&w.stopCall.listener&&animSoundStopOriginal){animSoundStopOriginal(w.stopCall.listener,w.stopCall.guid.data(),w.stopCall.callback);AnimSoundRow(0,0,0,w.stopCall.guid,true,w.stopCall.callback,3);}
    if(w.replay&&w.call.listener){animSoundStartOriginal(w.call.listener,w.call.guid.data(),w.call.callback);AnimSoundRow(0,0,0,w.call.guid,true,w.call.callback,2);}}
    RackAudioWork(listener,soldier,now);
}
void __fastcall AnimSoundStartHook(void* self,void*,const void* guid,int callback){Callback c;
    animThreadId.store(GetCurrentThreadId(),std::memory_order_relaxed);++animSoundCalls;if(animationScope)++animSoundInSoldierUpdate;++animSoundStarts;
    std::uint32_t soldier=0;std::uint8_t listenerView=0,soldierFlags=0;AnimGuid id{};
    const auto at=unsigned(reinterpret_cast<std::uintptr_t>(self));
    if(!Read(at+4,&soldier,4)||!Read(at+0x50,&listenerView,1)||!soldier||!Read(soldier+0x475,&soldierFlags,1)||
       !guid||!Read(unsigned(reinterpret_cast<std::uintptr_t>(guid)),id.data(),id.size())){animSoundStartOriginal(self,guid,callback);return;}
    AnimSoundWork(self,soldier);
    const bool played=(soldierFlags&1)==listenerView;if(played)++animSoundPlayed;
    const auto physicalAudio=reloadAudioSuppression.load(std::memory_order_acquire);
    const auto physicalAudioNow=AnimNowNs();
    if(enabled.load(std::memory_order_acquire)&&physicalAudio&&played&&physicalAudioNow>=physicalAudio->observedNs&&physicalAudioNow<physicalAudio->deadlineNs&&
       physicalAudio->proof.identity.owner.soldier==soldier&&physicalAudio->proof.Covers(id)&&RackAudioListener(at,soldier)){
        ++reloadAudioSuppressed;AnimSoundRow(soldier,listenerView,soldierFlags,id,played,callback,1);return;}
    if(reloadSoundEnabled){const auto now=AnimNowNs();
        for(auto* gate:soundGates)if(!gate->OnStart({self,id,callback,soldier},played,now)){AnimSoundRow(soldier,listenerView,soldierFlags,id,played,callback,1);return;}}
    animSoundStartOriginal(self,guid,callback);AnimSoundRow(soldier,listenerView,soldierFlags,id,played,callback,0);
}
void __fastcall AnimSoundStopHook(void* self,void*,const void* guid,int callback){Callback c;
    animThreadId.store(GetCurrentThreadId(),std::memory_order_relaxed);++animSoundCalls;if(animationScope)++animSoundInSoldierUpdate;
    std::uint32_t soldier=0;if(Read(unsigned(reinterpret_cast<std::uintptr_t>(self))+4,&soldier,4)&&soldier)AnimSoundWork(self,soldier);
    animSoundStopOriginal(self,guid,callback);
}
// The Ant event dispatch 0xf4fb50, thiscall(ant,events,float dt) ret 8, is the
// only caller of the listeners (ant+0xc0..0xc4, registered by 0xf677b0 from
// the soldier's 1P/3P listener setup at 0x8ba3ae/0x8ba55b). It calls them
// (vtable+4 start, +8 stop) inside a scoped lock: EnterCriticalSection on
// 0x18a4bc0 and a TLS chain node {cs,previous} at TLS index [0x1502e04]
// (0x50ece0 get, 0x50ecc0 set), released in reverse. A due replay/stop runs
// right after that dispatch returns, on the same job, only for the ant that
// owns the learned listener, under the same lock taken the same way.
using AnimDispatchFn=void(__thiscall*)(void*,void*,float);AnimDispatchFn animDispatchOriginal=nullptr;
CRITICAL_SECTION* animDispatchLock=nullptr;DWORD animDispatchTls=TLS_OUT_OF_INDEXES;
std::atomic<std::uintptr_t> animDispatchOwner=0,animDispatchListener=0;
bool AntOwnsListener(std::uintptr_t ant,std::uintptr_t listener)noexcept{
    std::uint32_t first=0,last=0;if(!Read(ant+0xc0,&first,4)||!Read(ant+0xc4,&last,4)||last<first||last-first>64*4||(last-first)%4)return false;
    std::array<std::uint32_t,64> items{};const auto n=(last-first)/4;if(!n||!Read(first,items.data(),n*4))return false;
    for(unsigned i=0;i<n;++i)if(items[i]==listener)return true;return false;
}
std::uintptr_t RackAudioDispatchListener(std::uintptr_t ant)noexcept{
    if(!rackAudioAudition.Due(AnimNowNs()))return 0;const auto owner=rackAudioAudition.Owner();
    Owner current{};if(!Resolve(current)||!current.foot||AudioOwner(current)!=owner){
        rackAudioAudition.Poll({},std::nullopt,0,false,AnimNowNs());++rackAudioRejected;++rackRejectDispatchOwner;return 0;}
    std::uint32_t first=0,last=0;if(!Read(ant+0xc0,&first,4)||!Read(ant+0xc4,&last,4)||last<first||last-first>64*4||(last-first)%4)return 0;
    std::array<std::uint32_t,64> items{};const auto n=(last-first)/4;if(!n||!Read(first,items.data(),n*4))return 0;
    std::uintptr_t found=0;for(unsigned i=0;i<n;++i)if(RackAudioListener(items[i],owner.soldier)){if(found){++rackListenerMisses;return 0;}found=items[i];}
    if(!found)++rackListenerMisses;
    return found;
}
void __fastcall AnimDispatchHook(void* self,void*,void* events,float dt){Callback c;
    animDispatchOriginal(self,events,dt);
    if(!reloadSoundEnabled)return;auto listener=reinterpret_cast<std::uintptr_t>(reloadSoundGate.WorkListener());
    if(!listener)listener=reinterpret_cast<std::uintptr_t>(pumpSoundGate.WorkListener());
    const auto ant=reinterpret_cast<std::uintptr_t>(self);
    // Due audition work must not be starved by a retained reload listener from
    // another owner. Both paths still drain once under the existing lock.
    if(const auto audition=RackAudioDispatchListener(ant))listener=audition;if(!listener)return;
    if(animDispatchListener.load(std::memory_order_relaxed)!=listener){
        if(!AntOwnsListener(ant,listener))return;
        animDispatchOwner.store(ant,std::memory_order_relaxed);animDispatchListener.store(listener,std::memory_order_relaxed);++animDispatchOwners;}
    else if(animDispatchOwner.load(std::memory_order_relaxed)!=ant||!AntOwnsListener(ant,listener))return;
    struct Held {CRITICAL_SECTION* cs;void* previous;} held{animDispatchLock,TlsGetValue(animDispatchTls)};
    EnterCriticalSection(held.cs);TlsSetValue(animDispatchTls,&held);
    std::uint32_t soldier=0;
    if(AntOwnsListener(ant,listener)&&Read(unsigned(listener)+4,&soldier,4)&&soldier){
        animDispatchThread.store(GetCurrentThreadId(),std::memory_order_relaxed);++animDispatchWork;AnimSoundWork(reinterpret_cast<void*>(listener),soldier);}
    TlsSetValue(animDispatchTls,held.previous);LeaveCriticalSection(held.cs);
}
// M6 dropped magazine (DroppedProps.h, docs/BC2_RELOAD_PARITY.md): the old
// magazine leaves the hand at RemovedHeld -> WellEmpty with the wrist's
// velocity and falls to the depth-derived ground (Bc2DroppedFloor.h) or, without
// a fresh sample, the play-space floor (the hand's tracked height above a
// floor-relative space; 1 m below the hand otherwise) corrected by the last
// observed ground offset.
// Presentation only and not drawn yet: rows (prop drop/land/expire) are the
// evidence until the renderer instance exists.
// Settings and spawner: Bc2DroppedMagazineSpawn.h explains why released
// magazines used to vanish (committed authority drops never spawned a prop).
bc2::DroppedMagazineSettings droppedSettings=bc2::ParseDroppedMagazineSettings(ProcessEnvText);
interaction::DroppedProps droppedMagazines{bc2::DroppedMagazineConfig(droppedSettings)};bc2::DroppedFloor droppedFloor;
bc2::DroppedMagazineSpawner droppedSpawner;
std::atomic<std::shared_ptr<const ReloadHintSnapshot>> reloadHintPublication;
interaction::ReloadHints reloadHints;interaction::ReloadHintOutput hintOutput{};std::array<unsigned,5> hintLit{};unsigned hintGhostFrames=0,hintGlowFrames=0;
const bool reloadGuidanceEnabled=[](){return ProcessEnvText("FVR_BC2_RELOAD_GUIDANCE")!="0";}();
unsigned fireHeldForStop=0,fireReplays=0;std::uint32_t fireLatched=0,fireReplayMask=0;std::int64_t fireReplayUntilNs=0;std::int64_t stockPressNs=0,stockPulseUntilNs=0;std::uint64_t stockPressMarker=0;unsigned stockFallbacks=0,stockFallbackSkips=0;
interaction::DetachableMagazinePhase dropPhase=interaction::DetachableMagazinePhase::Attached;
std::uint64_t droppedDrawn=0,droppedPublished=0,droppedAppended=0,droppedCulled=0,droppedDisabled=0;
// Each prop keeps the magazine part geometry key of the weapon it left
// (the same key the body magazine display uses), by prop id.
std::array<std::pair<std::uint64_t,graphics::RigidPropGeometryKey>,interaction::DroppedProps::Slots> droppedGeometry{};
struct DroppedPropPublication {std::array<graphics::BodyPropInstance,interaction::DroppedProps::Slots> items{};unsigned count=0;};
std::atomic<std::shared_ptr<const DroppedPropPublication>> droppedPublication;
interaction::HandInteractionOwner droppedOwner{};std::array<std::pair<std::uint64_t,bool>,interaction::DroppedProps::Slots> droppedVisible{};
// Both eyes of a pair read the same snapshot (taken at the pair's first eye).
struct DroppedPairSnapshot {std::uint64_t frame=0;std::shared_ptr<const DroppedPropPublication> publication;};
std::mutex droppedPairMutex;DroppedPairSnapshot droppedPair{};std::uint64_t droppedPairReuses=0,droppedPairSplits=0;
void DroppedMagazineStep(const ReloadRawContact& raw,const interaction::InputFrame& input,interaction::DetachableMagazinePhase phase,
    std::int64_t now,MagazineTracking& tracking,const interaction::HandInteractionOwner& owner,bool committedDrop=false,bool dropFromWell=false)noexcept{
    if(raw.valid)droppedSpawner.ObserveWrist(raw.rawLeftWristWorldMeters,now);
    // Depth ground (native units, Z negated into the canonical prop space) and
    // the play-space floor this step; a fresh pair refreshes the offset.
    std::optional<bc2::DroppedFloorGround> ground;
    if(const float units=input.worldUnitsPerMeter;std::isfinite(units)&&units>0)
        if(const auto g=bc2::ground::Latest(now,250000000))ground=bc2::DroppedFloorGround{g->y/units,g->x/units,-g->z/units};
    std::optional<float> playFloor;
    if(raw.valid&&input.floorRelative&&input.hands[0].gripTracked&&std::isfinite(input.hands[0].grip.position.y)&&input.hands[0].grip.position.y>0)
        playFloor=raw.rawLeftWristWorldMeters.values[3][1]-input.hands[0].grip.position.y;
    droppedFloor.Observe(playFloor,ground);
    // The carried magazine's own world pose, as the presentation draws it
    // (retained 500 ms so a release frame without raw contact still has it).
    if(tracking.target&&(tracking.target->role==interaction::MagazinePropRole::Removed||
        (tracking.physicalAuthority&&tracking.target->role==interaction::MagazinePropRole::Replacement))&&raw.valid)
        droppedSpawner.ObserveCarried(interaction::Multiply(tracking.target->weaponFromItemMeters,tracking.removalFrame?tracking.removalFrame->weaponWorldMeters:raw.weaponWorldMeters),now);
    // M1 button eject (the Reload press, DetachableMagazine eject mode): the
    // magazine leaves the well without a hand pull, from its seated pose.
    const auto* geometry=tracking.family.binding.profile?tracking.family.binding.profile->geometry:nullptr;
    std::optional<math::Matrix4> wellPose;
    if(geometry&&raw.valid)wellPose=interaction::Multiply(geometry->attachedItem,raw.weaponWorldMeters);
    // Physical authority: the applied DropMagazine receipt alone commits the
    // drop (the ledger already holds the magazine as Dropped). Legacy consumer:
    // the RemovedHeld/eject -> WellEmpty transition, as before.
    const bool legacyPulled=!tracking.physicalAuthority&&phase==interaction::DetachableMagazinePhase::WellEmpty&&dropPhase==interaction::DetachableMagazinePhase::RemovedHeld;
    const bool legacyEject=!tracking.physicalAuthority&&phase==interaction::DetachableMagazinePhase::WellEmpty&&
        (dropPhase==interaction::DetachableMagazinePhase::PreparingRemoval||dropPhase==interaction::DetachableMagazinePhase::Pulling);
    if(tracking.physicalAuthority?committedDrop:(legacyPulled||legacyEject)){
        if(droppedSettings.enabled)droppedSpawner.Commit(tracking.physicalAuthority?dropFromWell:legacyEject,now);else ++droppedDisabled;}
    if(const auto launch=droppedSpawner.Resolve(now,raw.valid?std::optional(raw.rawLeftWristWorldMeters):std::nullopt,wellPose)){
        const auto& t=launch->worldFromItem.values[3];const bool fromWell=launch->fromWell;
        const auto floor=droppedFloor.Choose(t[0],t[2],playFloor,t[1]-1.f,ground);const float floorY=floor.y;
        if(const auto id=droppedMagazines.Drop(gunWeapon.id,launch->worldFromItem,launch->velocity,launch->angularVelocity,floorY,now)){
            gunFvr.Prop(GunEvents(),gunWeapon,bc2::DroppedFloorLabel(fromWell,floor.source),*id,{t[0],t[1],t[2]},floorY);
            graphics::RigidPropGeometryKey key{};
            if(geometry)key=MakeBodyAmmoGeometryKey(geometry->asset,geometry->mesh,geometry->bones.magazine,geometry->rigFingerprint);
            auto* slot=&droppedGeometry[0];for(auto& e:droppedGeometry)if(e.first<slot->first)slot=&e;*slot={*id,key};}
        if(const auto old=droppedMagazines.TakeRecycled())gunFvr.Prop(GunEvents(),gunWeapon,"recycle",*old,{t[0],t[1],t[2]},floorY);
    }
    dropPhase=phase;
    const auto step=droppedMagazines.Update(now);
    // The weapon has one magazine bone: the newest dropped magazine is drawn
    // through it while the well is empty; the replacement takes it back.
    if(phase==interaction::DetachableMagazinePhase::WellEmpty&&tracking.target&&tracking.target->role==interaction::MagazinePropRole::Hidden){
        const interaction::DroppedProp* newest=nullptr;for(const auto& p:droppedMagazines.Props())if(p&&(!newest||p->id>newest->id))newest=&*p;
        if(newest&&now-newest->droppedNs<droppedMagazines.Config().lifetimeNs){tracking.droppedWorldMeters=newest->worldFromProp;++droppedDrawn;}}
    // Host composition instances for the full lifetime: magazine part in
    // native world units (geometry is in metres, so the basis scales too).
    auto publication=std::make_shared<DroppedPropPublication>();const float units=input.worldUnitsPerMeter;
    // The host draws props over the scene (no scene depth yet), so a prop the
    // player has walked away from (beyond 3 m of the hand, horizontally) or
    // left on another level (floor estimate now 0.5 m off) would show through
    // walls or floors: it is not published.
    // A gather without a hand measurement (raw contact invalid) or owner keeps
    // the last decision and identity: only a positive measurement culls (runs
    // 104343/105118: empty publications between the two eyes of a pair).
    std::optional<std::array<float,2>> hand;std::optional<float> currentFloor;
    if(raw.valid){const auto& w=raw.rawLeftWristWorldMeters.values[3];hand=std::array<float,2>{w[0],w[2]};
        if(input.floorRelative&&input.hands[0].gripTracked&&std::isfinite(input.hands[0].grip.position.y))currentFloor=droppedFloor.Corrected(w[1]-input.hands[0].grip.position.y);}
    if(owner.actorGeneration&&owner.equipGeneration&&owner.space)droppedOwner=owner;
    const auto& who=droppedOwner;
    if(std::isfinite(units)&&units>0&&who.actorGeneration&&who.equipGeneration&&who.space)
        for(const auto& p:droppedMagazines.Props()){if(!p)continue;
            bool* visible=nullptr;for(auto& v:droppedVisible)if(v.first==p->id)visible=&v.second;
            if(!visible){auto* slot=&droppedVisible[0];for(auto& v:droppedVisible)if(v.first<slot->first)slot=&v;*slot={p->id,true};visible=&slot->second;}
            if(hand){const auto& at=p->worldFromProp.values[3];const float dx=at[0]-(*hand)[0],dz=at[2]-(*hand)[1];
                *visible=!(dx*dx+dz*dz>droppedSettings.visibleMeters*droppedSettings.visibleMeters||(currentFloor&&std::fabs(*currentFloor-p->floorY)>droppedSettings.floorToleranceMeters));}
            if(!*visible){++droppedCulled;continue;}
            graphics::RigidPropGeometryKey key{};for(const auto& e:droppedGeometry)if(e.first==p->id)key=e.second;
            if(!graphics::ValidRigidPropKey(key))continue;
            graphics::BodyPropInstance i{};i.geometry=key;i.world=p->worldFromProp;
            for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)i.world.values[r][c]*=units;
            i.observedNs=now;i.deadlineNs=now+100000000;i.actorGeneration=who.actorGeneration;i.spaceGeneration=who.space;
            i.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Ammo,who.equipGeneration,1+publication->count);
            if(graphics::BodyPropFresh(i,now))publication->items[publication->count++]=i;}
    // F5 magazine/magwell tips: an amber translucent magazine at the seated
    // pose, its opacity following the hint pulse. Over the native magazine
    // (pull it) or as a ghost in the empty well (seat the held one). Host
    // props draw over the eye image, so the overlay never depth-fights.
    using HintPart=interaction::ReloadHintPart;
    const float magazineGlow=hintOutput.alpha[std::size_t(HintPart::Magazine)],wellGlow=hintOutput.alpha[std::size_t(HintPart::Magwell)];
    const float glow=std::max(magazineGlow,wellGlow);
    const auto glowOpacity=unsigned(std::clamp(glow/interaction::ReloadHints::PeakAlpha*150.f+.5f,0.f,255.f));
    // Glow geometry has its own current configured-mesh/owner lease. A shot
    // may invalidate physical contact without moving or changing this prop.
    const auto glowHint=reloadHintPublication.load(std::memory_order_acquire);
    if(glowOpacity>0&&glowHint&&glowHint->magazineWorld&&glowHint->owner==who&&
       glowHint->observedNs<=now&&glowHint->deadlineNs>now&&
       publication->count<interaction::DroppedProps::Slots){
        graphics::BodyPropInstance i{};i.geometry=glowHint->magazineGeometry;i.world=*glowHint->magazineWorld;
        if(graphics::ValidRigidPropKey(i.geometry)){
            i.observedNs=glowHint->observedNs;i.deadlineNs=glowHint->deadlineNs;
            i.actorGeneration=who.actorGeneration;i.spaceGeneration=who.space;
            i.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Ammo,who.equipGeneration,7);
            i.tintRgba=255u|166u<<8|26u<<16|255u<<24;i.opacity=glowOpacity;
            if(graphics::BodyPropFresh(i,now)){publication->items[publication->count++]=i;if(wellGlow>=magazineGlow)++hintGhostFrames;else ++hintGlowFrames;}}}
    if(publication->count)++droppedPublished;
    droppedPublication.store(std::move(publication),std::memory_order_release);
    for(unsigned n=0;n<step.count;++n){const auto& e=step.notices[n];
        float floor=0;for(const auto& p:droppedMagazines.Props())if(p&&p->id==e.id)floor=p->floorY;
        gunFvr.Prop(GunEvents(),gunWeapon,e.event==interaction::DroppedPropEvent::Landed?"land":"expire",e.id,{e.position.x,e.position.y,e.position.z},
            e.event==interaction::DroppedPropEvent::Landed?floor:e.position.y);}
}
struct Scope {
    Owner owner{};InputOverride patch;bool used=false;std::uint64_t nativeTick=0;
    std::optional<math::Vec3> testPositionBefore;std::int64_t testBeforeQpc=0;
    std::optional<unsigned> testInputSlot;
};thread_local Scope* scope=nullptr;
Owner lastOwner{};std::uint64_t epoch=0,rigEpoch=0,renderEquipmentEpoch=0,physicalActorGeneration=0;ULONGLONG lastTick=0;
// Opt-in total-angle ordering evidence at already verified hook/setter seams.
// These are NOT recoil-only samples: native recovery/provenance is unbound.
// No native call, extra impulse, angle compensation or gameplay write lives here.
bool aimAngleTraceRequested=false;
enum class AimAngleStage:unsigned {BeforeUpdate,AfterGatherOriginal,BeforeVrBaseline,AfterVrBaseline,AfterUpdate,Count};
struct AimAngleSnapshot {
    std::int64_t qpc=0,deadline=0;std::uint64_t input=0,space=0;
    unsigned aimer=0;bool valid=false,inputFresh=false;
    const char* reason="read_failed";
    std::array<float,2> aimerAngles{},simulationAngles{};
};
struct AimAngleTransaction {
    Owner owner{};std::uint64_t update=0,actorGeneration=0,equipmentGeneration=0;
    float dt=0;bool baselineWritten=false;
    std::optional<std::array<float,2>> requested;
    std::array<std::optional<AimAngleSnapshot>,unsigned(AimAngleStage::Count)> stages{};
};
constexpr unsigned AimAngleCapacity=4096;
std::vector<AimAngleTransaction> aimAngleRecords; // Allocated only by opt-in Install.
unsigned aimAngleCount=0,aimAngleNext=0;std::uint64_t aimAngleOverwrites=0;
std::mutex aimAngleMutex;
thread_local AimAngleTransaction* aimAngleScope=nullptr;
// Nested/pass-through updates cannot append phases to their parent's record.
// Restore that parent even if native callbacks reenter either existing hook.
struct AimAngleScopeRestore {
    AimAngleTransaction* previous=nullptr;bool active=false;
    explicit AimAngleScopeRestore(bool enabled)noexcept:active(enabled){
        if(active){previous=aimAngleScope;aimAngleScope=nullptr;}
    }
    ~AimAngleScopeRestore(){if(active)aimAngleScope=previous;}
};
void SampleAimAngles(AimAngleStage stage,const interaction::InputFrame* source=nullptr)noexcept {
    if(!aimAngleScope)return;
    auto& transaction=*aimAngleScope;auto& slot=transaction.stages[unsigned(stage)];
    if(slot)return; // A transaction samples each boundary at most once.
    AimAngleSnapshot sample;LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);sample.qpc=now.QuadPart;sample.deadline=inputDeadline;
    const auto& input=source?*source:cachedInput;
    sample.input=input.generation;sample.space=input.spaceGeneration;
    sample.inputFresh=now.QuadPart>0&&now.QuadPart<inputDeadline&&input.generation!=0;
    const auto current=[&]{Owner check{};return rigEpoch==transaction.actorGeneration&&
        renderEquipmentEpoch==transaction.equipmentGeneration&&Resolve(check)&&check==transaction.owner&&check.foot&&check.weapon;};
    if(!current())sample.reason="owner_changed";
    else if(source&&(source->generation!=cachedInput.generation||source->spaceGeneration!=cachedInput.spaceGeneration))sample.reason="input_changed";
    else{
        const auto aim=transaction.owner.aim;
        if(aim&&U32(U32(aim)+0x28)==0){
            sample.aimer=U32(aim+8);
            if(sample.aimer&&Type(U32(sample.aimer),"HaloAimerData")&&
               Read(sample.aimer+12,sample.aimerAngles.data(),8)&&Read(aim+12,sample.simulationAngles.data(),8)){
                bool finite=true;for(float value:sample.aimerAngles)finite&=std::isfinite(value)&&std::abs(value)<100;
                for(float value:sample.simulationAngles)finite&=std::isfinite(value)&&std::abs(value)<100;
                if(!finite)sample.reason="invalid_angles";
                else if(U32(aim+8)!=sample.aimer||!current())sample.reason="owner_changed";
                else{sample.valid=true;sample.reason="valid";}
            }
        }
    }
    slot=sample;
}
void CompleteAimAngles(const AimAngleTransaction& transaction){
    std::lock_guard lock(aimAngleMutex);
    aimAngleRecords[aimAngleNext]=transaction;aimAngleNext=(aimAngleNext+1)%unsigned(aimAngleRecords.size());
    if(aimAngleCount<aimAngleRecords.size())++aimAngleCount;else ++aimAngleOverwrites;
}
void ReportAimAngles(std::ostream& out,std::int64_t frequency){
    std::vector<AimAngleTransaction> records;std::uint64_t overwrites=0;
    if(aimAngleTraceRequested){
        std::lock_guard lock(aimAngleMutex);records.reserve(aimAngleCount);overwrites=aimAngleOverwrites;
        for(unsigned n=0;n<aimAngleCount;++n)records.push_back(aimAngleRecords[(aimAngleNext+aimAngleRecords.size()-aimAngleCount+n)%aimAngleRecords.size()]);
    }
    out<<",\"total_angles_radians\":{\"requested\":"<<(aimAngleTraceRequested?"true":"false")
       <<",\"recoil_attribution\":false,\"native_recovery_boundary_verified\":false,\"capacity\":"<<AimAngleCapacity
       <<",\"angle_order\":[\"yaw\",\"pitch\"],\"qpc_frequency\":"<<frequency
       <<",\"overwrites\":"<<overwrites<<",\"transactions\":[";
    constexpr const char* names[]={"before_update","after_gather_original","before_vr_baseline","after_vr_baseline","after_update"};
    for(unsigned n=0;n<records.size();++n){const auto& r=records[n];if(n)out<<',';
        out<<"{\"update\":"<<r.update<<",\"player\":"<<r.owner.player<<",\"soldier\":"<<r.owner.soldier<<",\"weak\":"<<r.owner.weak
           <<",\"weapon\":"<<r.owner.weapon<<",\"aim\":"<<r.owner.aim<<",\"weapon_data\":"<<r.owner.equipment.data
           <<",\"persistence\":"<<r.owner.equipment.persistence<<",\"asset\":\"";
        for(char c:r.owner.equipment.Asset()){if(c=='\\'||c=='\"')out<<'\\';out<<c;}
        out<<"\",\"actor_generation\":"<<r.actorGeneration
           <<",\"equipment_generation\":"<<r.equipmentGeneration<<",\"baseline_written\":"<<(r.baselineWritten?"true":"false");
        if(std::isfinite(r.dt))out<<",\"dt\":"<<r.dt;
        if(r.requested)out<<",\"requested_angles_radians\":["<<(*r.requested)[0]<<','<<(*r.requested)[1]<<']';
        out<<",\"stages\":{";bool comma=false;
        for(unsigned i=0;i<r.stages.size();++i)if(r.stages[i]){const auto& s=*r.stages[i];if(comma)out<<',';comma=true;
            out<<'\"'<<names[i]<<"\":{\"qpc\":"<<s.qpc<<",\"input_deadline_qpc\":"<<s.deadline<<",\"input\":"<<s.input<<",\"space\":"<<s.space
               <<",\"input_fresh\":"<<(s.inputFresh?"true":"false")<<",\"valid\":"<<(s.valid?"true":"false")<<",\"reason\":\""<<s.reason<<'\"';
            if(s.valid)out<<",\"aimer_object\":"<<s.aimer<<",\"aimer\":["<<s.aimerAngles[0]<<','<<s.aimerAngles[1]
                          <<"],\"simulation\":["<<s.simulationAngles[0]<<','<<s.simulationAngles[1]<<']';
            out<<'}';
        }
        out<<"}}";
    }
    out<<"]}";
}
// Ownership consumes current safety and separately identified original contact
// evidence. It never retimestamps the renderer's N-1 geometry as raw packet N.
interaction::HandInteraction handOwnership;
interaction::HandInteractionSample handSample{};
interaction::HandInteractionKey handItem{};
struct HandEvidence {interaction::HandInteractionSample sample{};unsigned weapon=0;};
std::array<HandEvidence,32> handHistory{};unsigned handHistoryNext=0;
std::uint64_t handEquip=0,handIntent=0,handPhysical=0,handActor=0,handRig=0;
WeaponEquipmentIdentity handNativeEquipment{};
unsigned handInventory=0,handWeapon=0;std::int64_t handFrequency=0;
// FVR_BC2_NATIVE_RECOIL_OBSERVE=1: bounded, local-only native ordering evidence.
// The published input context is an observation label, NEVER an assertion that
// the native tick consumed that input. No renderer or shot consumer uses it.
bool nativeRecoilObserveRequested=false,nativeRecoilObserveInstalled=false;
bool nativeRecoilTransferEnabled=false;
bool nativeRecoilStrengthVerified=false;
NativeRecoilKickEnvelope nativeRecoilKick;
std::uint64_t nativeRecoilKickSeenShot=0;
std::atomic<unsigned> nativeRecoilKickShots=0,nativeRecoilKickMissingStrength=0;
// Feedback-channel writes only, not OpenXR/controller delivery acknowledgements.
std::atomic<unsigned> nativeRecoilFeedbackRightSent=0,nativeRecoilFeedbackRightDropped=0;
std::atomic<unsigned> nativeRecoilFeedbackSupportSent=0,nativeRecoilFeedbackSupportDropped=0;
std::atomic<float> nativeRecoilLastMeanDegrees=0;
bool SendFeedback(interaction::FeedbackEvent e)noexcept;
bool GunHapticsEnabled()noexcept;
std::int64_t HandNanos(std::int64_t ticks)noexcept;
std::atomic<std::shared_ptr<const NativeRecoilPoseSample>> nativeRecoilPose;
std::atomic<std::uint64_t> nativeRecoilPoseSerial=0,nativeRecoilPosePublished=0,nativeRecoilPoseRejected=0;
NativeSwayTickFn nativeSwayTickOriginal=nullptr;
NativeSwayCameraFn nativeSwayCameraOriginal=nullptr;
std::atomic<std::shared_ptr<const NativeRecoilContext>> nativeRecoilContext;
std::mutex nativeRecoilMutex;
std::unique_ptr<NativeRecoilObservationHistory<>> nativeRecoilHistory;
std::atomic<unsigned> nativeRecoilUnlabelled=0,nativeRecoilOtherCopies=0;
bool NativeRecoilOwnerCurrent(void*,const NativeRecoilContext& expected)noexcept {
    const auto lease=nativeRecoilContext.load(std::memory_order_acquire);
    Owner current{};const auto& o=expected.owner;
    return lease&&*lease==expected&&Resolve(current)&&current.foot&&current.player==o.player&&
        current.soldier==o.soldier&&current.weak==o.weak&&current.weapon==o.weapon&&current.equipment==expected.equipment;
}
void ObserveNativeRecoil(NativeRecoilSeam seam,unsigned sway,float dt=0,const void* cameraOutput=nullptr,
    const NativeRecoilBoundary* boundary=nullptr,const NativeRecoilContext* boundaryContext=nullptr)noexcept {
    if(!nativeRecoilObserveRequested||!nativeRecoilObserveInstalled||!enabled.load(std::memory_order_acquire))return;
    const auto lease=nativeRecoilContext.load(std::memory_order_acquire);
    if(boundaryContext&&(!lease||*lease!=*boundaryContext))return;
    LARGE_INTEGER stamp{};
    if(!lease||!QueryPerformanceCounter(&stamp)||stamp.QuadPart<=0||stamp.QuadPart>lease->deadlineQpc){++nativeRecoilUnlabelled;return;}
    if(!reloadStateBinding||!nativeRecoilHistory)return;
    const bool swayCallback=seam==NativeRecoilSeam::PostTick||seam==NativeRecoilSeam::CameraRead;
    if(swayCallback){
        // Cheap rejection of remote/unselected native ticks. These unchecked
        // candidate reads confer no ownership; the complete admission below
        // still verifies the exact local actor, equipment, branch and vtable.
        std::uint8_t flags=0;unsigned firing=0,candidate=0;
        const auto weapon=std::uint64_t(lease->owner.weapon);
        if(!Read(lease->owner.soldier+0x114,&flags,1)||!(flags&1)||weapon+0x44>UINT32_MAX||
           !Read(unsigned(weapon+((flags&0x10)?0x40:0x3c)),&firing,4)||
           firing>UINT32_MAX-0x18||!Read(firing+0x14,&candidate,4)||candidate!=sway){++nativeRecoilOtherCopies;return;}
    }
    const NativeRecoilMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},NativeRecoilOwnerCurrent};
    const auto sample=ReadNativeRecoilObservation(memory,*reloadStateBinding,unsigned(image),*lease);
    // Hook sees every native sway; admit only the camera-selected local copy.
    // Unrelated calls must never reset a local tick or manufacture a serial.
    if(swayCallback&&(!sample.sample||sample.copy.sway!=sway)){++nativeRecoilOtherCopies;return;}
    NativeRecoilObservation row;row.seam=seam;row.status=sample.status;row.context=*lease;
    row.copy=sample.copy;row.qpc=stamp.QuadPart;row.thread=GetCurrentThreadId();row.dtValid=seam==NativeRecoilSeam::PostTick&&std::isfinite(dt);row.dt=row.dtValid?dt:0;
    row.sample=sample.sample;
    if(boundary)row.boundary=*boundary;
    if(cameraOutput&&row.sample){
        // The actual native camera output, not a second call to its getter.
        std::array<float,16> output{};
        if(!Read(reinterpret_cast<unsigned>(cameraOutput),output.data(),sizeof(output)))return;
        for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component)
            if(!std::isfinite(output[4*axis+component]))return;
        row.sample->camera=output;
    }
    if(!NativeRecoilOwnerCurrent(nullptr,*lease))return;
    std::lock_guard lock(nativeRecoilMutex);
    if(nativeRecoilContext.load(std::memory_order_acquire)!=lease)return;
    nativeRecoilHistory->Record(row,handFrequency/10);
}
void ObserveRecoilVrBaseline(NativeRecoilSeam seam,const Owner& owner,unsigned aimer,
    const interaction::InputFrame& input,std::int64_t deadline,float yaw,float pitch)noexcept {
    if(!nativeRecoilObserveRequested||!nativeRecoilObserveInstalled)return;
    const auto lease=nativeRecoilContext.load(std::memory_order_acquire);
    if(!lease||lease->owner.soldier!=owner.soldier||lease->owner.weak!=owner.weak||
       lease->owner.weapon!=owner.weapon||lease->equipment!=owner.equipment||lease->owner.space!=input.spaceGeneration)return;
    NativeRecoilBoundary boundary;boundary.input=input.generation;boundary.inputDeadlineQpc=deadline;
    boundary.requestedAim={yaw,pitch};
    boundary.anglesValid=Read(aimer+12,boundary.aimerAim.data(),sizeof(boundary.aimerAim))&&
        Read(owner.aim+12,boundary.simulationAim.data(),sizeof(boundary.simulationAim));
    for(const auto angles:{boundary.requestedAim,boundary.aimerAim,boundary.simulationAim})
        for(float value:angles)boundary.anglesValid=boundary.anglesValid&&std::isfinite(value);
    ObserveNativeRecoil(seam,0,0,nullptr,&boundary,lease.get());
}
void ObserveRecoilShotBase(NativeRecoilSeam seam,const Owner& owner,const NativeRecoilBoundary& boundary)noexcept {
    const auto lease=nativeRecoilContext.load(std::memory_order_acquire);
    if(!lease||lease->owner.soldier!=owner.soldier||lease->owner.weak!=owner.weak||
       lease->owner.weapon!=owner.weapon||lease->equipment!=owner.equipment)return;
    ObserveNativeRecoil(seam,0,0,nullptr,&boundary,lease.get());
}
std::optional<float> ReadNativeRecoilStrength(const NativeRecoilSample& sample)noexcept {
    if(!nativeRecoilStrengthVerified)return {};
    const auto read=[](std::uint64_t at,auto& value){return at>=0x10000&&at+sizeof(value)<=UINT32_MAX&&Read(unsigned(at),&value,sizeof(value));};
    unsigned selector=0;std::array<unsigned,2> data{};float amplitude=0;
    if(!read(std::uint64_t(sample.copy.sway)+0xe0,selector)||!read(selector,data)||!data[0]||!data[1])return {};
    const auto offset=std::uint64_t(data[0])-data[1];
    if(offset!=0x138&&offset!=0x1c4&&offset!=0x23c&&offset!=0x28c&&offset!=0x6c&&offset!=0xbc)return {};
    if(!read(std::uint64_t(data[0])+0x10,amplitude)||!std::isfinite(amplitude)||amplitude<0||amplitude>360)return {};
    unsigned again=0;std::array<unsigned,2> dataAgain{};float amplitudeAgain=0;
    if(!read(std::uint64_t(sample.copy.sway)+0xe0,again)||again!=selector||!read(selector,dataAgain)||dataAgain!=data||
       !read(std::uint64_t(data[0])+0x10,amplitudeAgain)||amplitudeAgain!=amplitude||!NativeRecoilOwnerCurrent(nullptr,sample.context))return {};
    const NativeRecoilMemory access{nullptr,[](void*,unsigned at,void* out,std::size_t bytes){return Read(at,out,bytes);},NativeRecoilOwnerCurrent};
    const auto current=ReadNativeRecoilObservation(access,*reloadStateBinding,unsigned(image),sample.context);
    if(!current.sample||current.copy!=sample.copy)return {};
    // Original configured amplitude is degrees; native random amplitude is
    // uniformly half..full. Native runtime modifiers remain in angular recoil.
    return .75f*amplitude;
}
void __fastcall NativeSwayTickHook(void* self,void*,float dt,void* context){
    Callback callback;
    // ABI: ECX=this, dt/context on stack, native ret 8 (0x6ec6bc).
    nativeSwayTickOriginal(self,dt,context);
    if(nativeRecoilTransferEnabled&&enabled.load(std::memory_order_acquire)&&reloadStateBinding&&handFrequency>0){
        const auto lease=nativeRecoilContext.load(std::memory_order_acquire);
        LARGE_INTEGER stamp{};
        if(lease&&QueryPerformanceCounter(&stamp)&&stamp.QuadPart>0&&stamp.QuadPart<lease->deadlineQpc){
            // Reject unrelated native ticks before the full owned-copy read.
            std::uint8_t flags=0;unsigned firing=0,sway=0;
            if(Read(lease->owner.soldier+0x114,&flags,1)&&(flags&1)&&
               Read(lease->owner.weapon+((flags&0x10)?0x40:0x3c),&firing,4)&&firing&&
               Read(firing+0x14,&sway,4)&&sway==reinterpret_cast<unsigned>(self)){
                const NativeRecoilMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t bytes){return Read(at,out,bytes);},NativeRecoilOwnerCurrent};
                const auto result=ReadNativeRecoilObservation(memory,*reloadStateBinding,unsigned(image),*lease);
                if(result.sample&&result.copy.sway==sway&&result.copy.cameraEnabled&&nativeRecoilContext.load(std::memory_order_acquire)==lease){
                    try {
                        auto sample=std::make_shared<NativeRecoilPoseSample>();sample->native=*result.sample;
                        sample->native.authoredMeanDegrees=ReadNativeRecoilStrength(sample->native);
                        sample->serial=++nativeRecoilPoseSerial;sample->tickQpc=stamp.QuadPart;
                        sample->deadlineQpc=(std::min)(lease->deadlineQpc,stamp.QuadPart+handFrequency/10);
                        std::shared_ptr<const NativeRecoilPoseSample> next=sample;
                        auto previous=nativeRecoilPose.load(std::memory_order_acquire);
                        while(!previous||previous->tickQpc<sample->tickQpc){
                            if(nativeRecoilPose.compare_exchange_weak(previous,next,std::memory_order_release,std::memory_order_acquire))break;
                        }
                    }catch(...){nativeRecoilPose.store({},std::memory_order_release);}
                }else{nativeRecoilPose.store({},std::memory_order_release);++nativeRecoilPoseRejected;}
            }
        }
    }
    ObserveNativeRecoil(NativeRecoilSeam::PostTick,reinterpret_cast<unsigned>(self),dt);
}
void __fastcall NativeSwayCameraHook(void* self,void*,void* output){
    Callback callback;
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    // ABI: ECX=this, output on stack, native ret 4 (0x6d60d4).
    nativeSwayCameraOriginal(self,output);
    if(caller==image+(0x89c649-0x400000))
        ObserveNativeRecoil(NativeRecoilSeam::CameraRead,reinterpret_cast<unsigned>(self),0,output);
}
void PublishRecoilObservedTracking(const rigPublication::Tracking& tracking)noexcept {
    auto published=tracking;published.recoil.reset();published.kick.reset();
    if(nativeRecoilObserveInstalled){
        const auto& input=tracking.input;
        Owner current{};
        if(tracking.deadline>0&&input.generation&&input.spaceGeneration&&input.focused&&input.headValid&&
           interaction::ValidInput(input)&&Resolve(current)&&current.foot&&current.soldier==tracking.soldier&&
           current.weak==tracking.weak&&current.weapon==tracking.weapon&&current.equipment==tracking.nativeEquipment){
            NativeRecoilContext next;
            next.owner={current.player,current.soldier,current.weak,current.weapon,tracking.ownerGeneration,renderEquipmentEpoch,input.spaceGeneration};
            next.equipment=tracking.nativeEquipment;next.input=input.generation;
            next.rigEquipmentGeneration=tracking.equipmentGeneration;next.deadlineQpc=tracking.deadline;
            const auto previous=nativeRecoilContext.load(std::memory_order_acquire);
            const auto shot=physicalWeaponRuntime::ReadAcceptedShotObservation();
            const bool sameOwner=previous&&previous->owner==next.owner&&previous->equipment==next.equipment&&
                previous->rigEquipmentGeneration==next.rigEquipmentGeneration;
            if(!sameOwner){nativeRecoilKick.Reset();if(shot)nativeRecoilKickSeenShot=(std::max)(nativeRecoilKickSeenShot,shot->accepted.sequence);}
            try{nativeRecoilContext.store(std::make_shared<const NativeRecoilContext>(next),std::memory_order_release);}
            catch(...){nativeRecoilContext.store({},std::memory_order_release);nativeRecoilPose.store({},std::memory_order_release);}
            if(nativeRecoilTransferEnabled){
                const auto sample=nativeRecoilPose.load(std::memory_order_acquire);
                LARGE_INTEGER now{};QueryPerformanceCounter(&now);
                if(sample&&sample->native.context.owner==next.owner&&sample->native.context.equipment==next.equipment&&
                   sample->native.context.rigEquipmentGeneration==next.rigEquipmentGeneration&&sample->tickQpc<=now.QuadPart&&sample->deadlineQpc>now.QuadPart){
                    const NativeRecoilMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t bytes){return Read(at,out,bytes);},NativeRecoilOwnerCurrent};
                    const auto currentSample=ReadNativeRecoilObservation(memory,*reloadStateBinding,unsigned(image),next);
                    if(currentSample.sample&&currentSample.copy==sample->native.copy){published.recoil=*sample;++nativeRecoilPosePublished;}
                }
                if(sample&&!published.recoil){auto rejected=sample;nativeRecoilPose.compare_exchange_strong(rejected,{},std::memory_order_release,std::memory_order_acquire);}
                if(shot&&shot->accepted.sequence>nativeRecoilKickSeenShot){
                    // Consume each authority receipt once, even if stale or
                    // strength is unavailable. Selection/focus cannot replay it.
                    nativeRecoilKickSeenShot=shot->accepted.sequence;
                    const bool owned=shot->clientWeapon==current.weapon&&shot->actor.player==current.player&&
                        shot->actor.soldier==current.soldier&&shot->actor.weak==current.weak;
                    const auto time=shot->accepted.qpc;
                    if(sameOwner&&owned&&time>0&&now.QuadPart>=time&&now.QuadPart-time<=handFrequency/10&&published.recoil){
                        const auto strength=published.recoil->native.authoredMeanDegrees;
                        const auto weight=strength?NativeRecoilStrengthWeight(*strength):std::nullopt;
                        if(strength&&weight){
                            const bool braced=tracking.supportHolding&&tracking.supportToken;
                            if(nativeRecoilKick.Shot(next,shot->accepted.sequence,time,*strength,braced,handFrequency)){
                                ++nativeRecoilKickShots;nativeRecoilLastMeanDegrees=*strength;
                                const auto observed=HandNanos(time),deadline=observed+100000000;
                                interaction::FeedbackEvent event{1,input.generation,input.spaceGeneration,observed,deadline,interaction::FeedbackKind::GunFire,1};
                                event.recoilWeight=*weight;
                                if(!GunHapticsEnabled()){}else if(SendFeedback(event))++nativeRecoilFeedbackRightSent;else ++nativeRecoilFeedbackRightDropped;
                                if(braced){event.hand=0;event.recoilSupport=true;
                                    if(!GunHapticsEnabled()){}else if(SendFeedback(event))++nativeRecoilFeedbackSupportSent;else ++nativeRecoilFeedbackSupportDropped;}
                            }
                        }else ++nativeRecoilKickMissingStrength;
                    }
                }
                published.kick=nativeRecoilKick.Sample();
                if(published.kick&&now.QuadPart>=NativeRecoilKickDeadline(*published.kick,handFrequency)){
                    nativeRecoilKick.Reset();published.kick.reset();
                }
            }
            ObserveNativeRecoil(NativeRecoilSeam::BeforePublication,0);
        }else{
            nativeRecoilContext.store({},std::memory_order_release);
            nativeRecoilPose.store({},std::memory_order_release);
            nativeRecoilKick.Reset();
            std::lock_guard lock(nativeRecoilMutex);if(nativeRecoilHistory)nativeRecoilHistory->Invalidate();
        }
    }
    rigPublication::PublishTracking(published);
}
void ReportNativeRecoilObservation(std::ostream& out){
    // Called with hooks disabled into the existing local final native report.
    // No stdout, IPC payload, telemetry stream or derived recoil claim.
    out<<",\"native_recoil_observation\":{\"requested\":"<<(nativeRecoilObserveRequested?"true":"false")
       <<",\"installed\":"<<(nativeRecoilObserveInstalled?"true":"false")
       <<",\"transfer_implemented\":true,\"transfer_enabled\":"<<(nativeRecoilTransferEnabled?"true":"false")
       <<",\"pose_samples\":"<<nativeRecoilPoseSerial.load()<<",\"pose_publications\":"<<nativeRecoilPosePublished.load()<<",\"pose_rejections\":"<<nativeRecoilPoseRejected.load()
       <<",\"kick_strength_binding\":"<<(nativeRecoilStrengthVerified?"true":"false")<<",\"kick_shots\":"<<nativeRecoilKickShots.load()<<",\"kick_missing_strength\":"<<nativeRecoilKickMissingStrength.load()<<",\"last_authored_mean_degrees\":"<<nativeRecoilLastMeanDegrees.load()
       <<",\"fire_feedback\":{\"channel_writes_only\":true,\"right_sent\":"<<nativeRecoilFeedbackRightSent.load()
       <<",\"right_dropped\":"<<nativeRecoilFeedbackRightDropped.load()<<",\"support_sent\":"<<nativeRecoilFeedbackSupportSent.load()
       <<",\"support_dropped\":"<<nativeRecoilFeedbackSupportDropped.load()<<'}'
       <<",\"same_frame_authority\":false,\"zoom_context_observed\":false"
       <<",\"alternate_branch_supported\":false,\"capacity\":4096,\"qpc_frequency\":"<<handFrequency
       <<",\"unlabelled_callbacks\":"<<nativeRecoilUnlabelled.load()<<",\"other_or_rejected_copies\":"<<nativeRecoilOtherCopies.load();
    std::lock_guard lock(nativeRecoilMutex);
    out<<",\"total\":"<<(nativeRecoilHistory?nativeRecoilHistory->Total():0)
       <<",\"overwritten\":"<<(nativeRecoilHistory?nativeRecoilHistory->Overwritten():0)
       <<",\"selected_ticks\":"<<(nativeRecoilHistory?nativeRecoilHistory->Ticks():0)<<",\"records\":[";
    unsigned n=0;if(nativeRecoilHistory)nativeRecoilHistory->ForEach([&](const NativeRecoilObservation& r){
        if(n++)out<<',';const auto& o=r.context.owner;
        out<<"{\"seam\":"<<unsigned(r.seam)<<",\"status\":"<<unsigned(r.status)<<",\"qpc\":"<<r.qpc
           <<",\"tick_qpc\":"<<r.tickQpc<<",\"thread\":"<<r.thread<<",\"dt\":"<<r.dt<<",\"dt_valid\":"<<(r.dtValid?"true":"false")
           <<",\"serial\":"<<r.serial<<",\"tick_input_context\":"<<r.tickInputContext
           <<",\"input\":"<<r.context.input<<",\"input_deadline_qpc\":"<<r.context.deadlineQpc
           <<",\"actor\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
           <<",\"actor_generation\":"<<o.actorGeneration<<",\"equipment_generation\":"<<o.equipGeneration
           <<",\"rig_equipment_generation\":"<<r.context.rigEquipmentGeneration<<",\"space\":"<<o.space
           <<",\"weapon_data\":"<<r.context.equipment.data<<",\"persistence\":"<<r.context.equipment.persistence
           <<",\"branch\":"<<unsigned(r.copy.branch)<<",\"firing\":"<<r.copy.firing<<",\"sway\":"<<r.copy.sway
           <<",\"camera_enabled\":"<<(r.copy.cameraEnabled?"true":"false")
           <<",\"matched_tick\":"<<(r.matchedTick?"true":"false")
           <<",\"camera_matrix_matches_tick\":"<<(r.cameraMatrixMatchesTick?"true":"false");
        const auto& b=r.boundary;
        if(r.seam==NativeRecoilSeam::BeforeVrBaseline||r.seam==NativeRecoilSeam::AfterVrBaseline){
            out<<",\"boundary_input\":"<<b.input<<",\"boundary_input_deadline_qpc\":"<<b.inputDeadlineQpc
               <<",\"angles_valid\":"<<(b.anglesValid?"true":"false");
            if(b.anglesValid)out<<",\"requested_aim_radians\":["<<b.requestedAim[0]<<','<<b.requestedAim[1]
                <<"],\"aimer_aim_radians\":["<<b.aimerAim[0]<<','<<b.aimerAim[1]
                <<"],\"simulation_aim_radians\":["<<b.simulationAim[0]<<','<<b.simulationAim[1]<<']';
        }
        if(r.seam==NativeRecoilSeam::ClientShotBase||r.seam==NativeRecoilSeam::ServerShotBase){
            out<<",\"shot_token_valid\":"<<(b.shotTokenValid?"true":"false")<<",\"shot_token\":"<<b.shotToken
               <<",\"shot_sway_valid\":"<<(b.shotSwayValid?"true":"false")<<",\"shot_sway\":"<<b.shotSway
               <<",\"shot_uses_selected_sway\":"<<(r.shotUsesSelectedSway?"true":"false")
               <<",\"rig_generation\":"<<b.rigGeneration<<",\"rig_deadline_qpc\":"<<b.rigDeadlineQpc
               <<",\"final_projectile_basis\":false";
            if(b.shotBaseValid){out<<",\"native_shot_base_columns\":[";
                for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component){if(axis||component)out<<',';out<<b.nativeShotBase[4*axis+component];}out<<']';}
            if(b.trackedWeaponValid){out<<",\"tracked_weapon_rows\":[";
                for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component){if(axis||component)out<<',';out<<b.trackedWeapon[4*axis+component];}out<<']';}
        }
        if(r.sample){out<<",\"raw_radians\":["<<r.sample->rawRadians[0]<<','<<r.sample->rawRadians[1]<<"],\"native_camera_columns\":[";
            for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component){if(axis||component)out<<',';out<<r.sample->camera[4*axis+component];}out<<']';}
        out<<'}';
    });out<<"]}";
}
struct HandOwnershipRecord {
    std::uint64_t serial=0,rawGeneration=0;std::int64_t nowNs=0;
    interaction::HandInteractionOwner owner{};interaction::HandInteractionKey item{};
    unsigned weapon=0,sightPhase=0;
    std::optional<interaction::HandClaim> left{},right{};
    bool supportHolding=false,sightCommit=false;std::uint64_t supportToken=0,sightRequest=0,sightAck=0;
    const char* reason="transition";
};
std::array<HandOwnershipRecord,512> handEvents{};unsigned handEventCount=0;
std::uint64_t handChecks=0,handEventsTotal=0,handEventsDropped=0,handOverlap=0,handMissingSupport=0,handMissingSight=0,handLeaseFailures=0,handClaimRejected=0;
HandOwnershipRecord handPrevious{};
std::uint64_t gatherSightRequest=0,gatherSightAck=0;bool gatherSightCommit=false,handDirty=false;
const char* handReason="transition";const char* handLastReset=nullptr;
bool sightLeaseContinuation=false;
std::optional<interaction::HandClaim> handPreviousLeft;
interaction::HandInteractionResult handLifecycle{};
std::int64_t HandNanos(std::int64_t ticks)noexcept {
    if(ticks<=0||handFrequency<=0)return 0;
    const auto seconds=ticks/handFrequency;
    if(seconds>INT64_MAX/1000000000)return 0;
    return seconds*1000000000+std::int64_t((static_cast<long double>(ticks%handFrequency)*1000000000)/handFrequency);
}
std::int64_t HandTicks(std::int64_t nanos)noexcept {
    if(nanos<=0||handFrequency<=0)return 0;
    const auto seconds=nanos/1000000000;
    if(seconds>INT64_MAX/handFrequency)return 0;
    const auto whole=seconds*handFrequency;
    const auto part=std::int64_t((static_cast<long double>(nanos%1000000000)*handFrequency)/1000000000);
    return whole>INT64_MAX-part?0:whole+part;
}
void HandChanged(const char* reason)noexcept {handDirty=true;handReason=reason;}
void CancelPendingSight(std::uint64_t request)noexcept {
    if(request&&pendingMode&&modeRecords[*pendingMode].gesture==request){modeRecords[*pendingMode].cancelled=true;pendingMode.reset();}
}
void ReleaseLeftOwnership(interaction::HandClaimKind kind)noexcept {
    const auto claim=handOwnership.Current(interaction::InteractionHand::Left);
    if(claim&&claim->token.kind==kind){handOwnership.Release(handSample,claim->token);HandChanged("release");}
}
void ReleaseSightOwnership()noexcept {ReleaseLeftOwnership(interaction::HandClaimKind::Sight);}
void CancelWeaponVisibilityProbe()noexcept {
    if(!weaponVisibilityProbe)return;
    if(weaponVisibilityProbe->RequestId()){LARGE_INTEGER now{};QueryPerformanceCounter(&now);weaponVisibilityProbe->Cancel(8,HandNanos(now.QuadPart));}
    rigPublication::PublishWeaponVisibility({});weaponVisibilitySample.store({},std::memory_order_release);
}
void SuppressMagazineRecovery()noexcept;
// Design step 4: the charging-handle rack is on by default; profiles without
// handle data (AEK) never owe one. FVR_BC2_CHAMBER_RACK=0 opts out.
// Native-lane reload fires on the press edge; FVR_BC2_NATIVE_RELOAD_HOLD=1 restores the 600 ms hold.
bool NativeReloadHoldRestored()noexcept{static const bool v=[]{char value[4]{};return GetEnvironmentVariableA("FVR_BC2_NATIVE_RELOAD_HOLD",value,sizeof(value))&&value[0]=='1';}();return v;}
bool ChamberRackDefault()noexcept{char value[4]{};return !(GetEnvironmentVariableA("FVR_BC2_CHAMBER_RACK",value,sizeof(value))&&value[0]=='0');}
void CancelPhysicalReload(PhysicalReloadResult* unpublished,unsigned sourceFlags)noexcept {
    // A fixture may finish AFTER Tick produced output in this same gather.
    // Revoke that unpublished capability/pulse before native cancellation;
    // otherwise the later PublishTracking would revive the old shell control.
    if(unpublished)*unpublished={};
    if(magazineDetached){magazineDetached->Invalidate();SuppressMagazineRecovery();}
    if(magazinePhysical){auto safety=handSample;LARGE_INTEGER now{};
        if(QueryPerformanceCounter(&now))safety.nowNs=HandNanos(now.QuadPart);
        if(magazinePhysical->ProbeState(safety.nowNs).active)++physicalCancelSites[physicalCancelSite<4?physicalCancelSite:0];
        magazinePhysical->Cancel(safety,handOwnership);}
    physicalCancelSite=0;
    if(physicalReload){auto safety=handSample;LARGE_INTEGER now{};if(QueryPerformanceCounter(&now))safety.nowNs=HandNanos(now.QuadPart);
        if(UsePhysicalTube())physicalTube->Cancel(safety,handOwnership,sourceFlags?PhysicalReloadCancelReason::RequestedInput:PhysicalReloadCancelReason::External,sourceFlags);
        else physicalReload->Cancel(safety,handOwnership,sourceFlags?PhysicalReloadCancelReason::RequestedInput:PhysicalReloadCancelReason::External,sourceFlags);}
}
bool HolsterCacheCurrent(void*,const HolsterSuppressionRequest& r)noexcept {
    if(bodyHolster&&bodyHolster->DiagnosticDeadline()){
        LARGE_INTEGER clock{};if(!QueryPerformanceCounter(&clock))return false;const auto now=HandNanos(clock.QuadPart);
        if(now<bodyHolster->DiagnosticStart()||now>=bodyHolster->DiagnosticDeadline())return false;
    }
    Owner check{};return enabled.load(std::memory_order_acquire)&&scope&&scope->used&&scope->nativeTick==r.nativeTick&&
        Resolve(check)&&check==scope->owner&&check.foot&&check.cache==r.cache&&Writable(check.cache,InputBytes)&&
        r.owner==ReloadStateOwner{check.player,check.soldier,check.weak,check.weapon,rigEpoch,epoch,r.input.owner.space}&&
        r.input.owner.actor==((std::uint64_t(check.weak)<<32)|check.soldier)&&r.input.owner.actorGeneration==rigEpoch;
}
void SuppressMagazineRecovery()noexcept {
    if(!magazineDetached||!magazineDetached->BlocksActions()||!scope||!scope->used||!scope->owner.foot)return;
    const ReloadStateOwner owner{scope->owner.player,scope->owner.soldier,scope->owner.weak,scope->owner.weapon,rigEpoch,epoch,handSample.owner.space};
    auto safety=handSample;LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return;safety.nowNs=HandNanos(now.QuadPart);
    if(const auto request=magazineDetached->RecoveryDemand(owner,safety,scope->nativeTick,scope->owner.cache)){HolsterInputOverride patch;
        if(patch.Apply({reinterpret_cast<std::byte*>(scope->owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent})&&patch.Commit())++magazineRecoverySuppressionCommits;
        else ++magazineRecoverySuppressionFailures;
    }
}
BodyHolsterLifecycleLog bodyHolsterLifecycle;
void InvalidateBodyHolsterState(BodyHolsterLifecycleReason reason,bool preserve=false)noexcept {
    if(!bodyHolster)return;auto source=handSample;LARGE_INTEGER now{};
    if(QueryPerformanceCounter(&now))source.nowNs=HandNanos(now.QuadPart);
    bodyHolsterLifecycle.Invalidate(*bodyHolster,reason,preserve,source,epoch);
    rigPublication::PublishOrdinaryEquipment({});
}
void InvalidateBodyHolster(bool trackingGap=false,BodyHolsterLifecycleReason reason=BodyHolsterLifecycleReason::External)noexcept {
    bodyHolsterSample.store({},std::memory_order_release);
    if(!bodyHolster)return;InvalidateBodyHolsterState(reason,trackingGap);holsterQueuedDraw.reset();rigPublication::PublishWeaponVisibility({});
    // Keep a current on-foot gather suppressed during tracking-loss recovery.
    // The old XR packet is NOT renewed and cannot grant free-hand pose authority.
    if(bodyHolster->BlocksActions()&&scope&&scope->used&&scope->owner.foot){
        BodyHolsterSample s;s.nativeOwner={scope->owner.player,scope->owner.soldier,scope->owner.weak,scope->owner.weapon,rigEpoch,epoch,handSample.owner.space};
        s.hand=handSample;LARGE_INTEGER now{};QueryPerformanceCounter(&now);s.hand.nowNs=HandNanos(now.QuadPart);
        s.nativeTick=scope->nativeTick;s.cache=scope->owner.cache;
        if(const auto request=bodyHolster->Demand(s)){HolsterInputOverride patch;
            if(patch.Apply({reinterpret_cast<std::byte*>(s.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent})&&patch.Commit())++holsterSuppressionCommits;
            else ++holsterSuppressionFailures;}
    }
}
void ResetHandOwnership(const char* reason)noexcept {
    ResetPumpPresentation();
    CancelAutomaticReload();
    physicalCancelSite=2;if(magazinePhysical&&magazinePhysical->ProbeState(0).active)strncpy_s(physicalCancelReason,reason?reason:"",_TRUNCATE);
    CancelPhysicalReload();
    if(bodyInventory&&std::strcmp(reason,"identity")){
        const auto cause=!std::strcmp(reason,"input_unavailable")?BodyHolsterLifecycleReason::InputUnavailable:
            !std::strcmp(reason,"owner_unavailable")?BodyHolsterLifecycleReason::OwnerUnavailable:
            !std::strcmp(reason,"not_on_foot")?BodyHolsterLifecycleReason::NotOnFoot:
            !std::strcmp(reason,"session_stop")?BodyHolsterLifecycleReason::SessionStop:BodyHolsterLifecycleReason::External;
        InvalidateBodyHolster(cause==BodyHolsterLifecycleReason::InputUnavailable,cause);
        if(cause==BodyHolsterLifecycleReason::InputUnavailable)bodyInventory->SuspendInteraction();
        else bodyInventory->Cancel();}
    const bool occupied=handOwnership.Current(interaction::InteractionHand::Left).has_value()||
        handOwnership.Current(interaction::InteractionHand::Right).has_value();
    handOwnership.Reset();handHistory={};handHistoryNext=0;
    if(occupied||!handLastReset||std::strcmp(handLastReset,reason))HandChanged(reason);
    handLastReset=reason;
}
void RecordHandOwnership()noexcept {
    if(!twoHandGrip)return;
    LARGE_INTEGER eventClock{};QueryPerformanceCounter(&eventClock);
    HandOwnershipRecord row;row.serial=++handChecks;row.rawGeneration=handSample.sequence;row.nowNs=HandNanos(eventClock.QuadPart);
    row.owner=handSample.owner;row.item=handItem;row.weapon=handWeapon;
    row.left=handOwnership.Current(interaction::InteractionHand::Left);row.right=handOwnership.Current(interaction::InteractionHand::Right);
    row.supportHolding=lastSupportSample.sample.holding;row.supportToken=lastSupportSample.sample.token;
    row.sightPhase=unsigned(sightPhase);row.sightRequest=gatherSightRequest;row.sightAck=gatherSightAck;row.sightCommit=gatherSightCommit;row.reason=handReason;
    const bool supportClaim=row.left&&row.left->token.kind==interaction::HandClaimKind::WeaponSupport;
    const bool sightClaim=row.left&&row.left->token.kind==interaction::HandClaimKind::Sight;
    const bool sightActive=sightPhase!=interaction::SightFlipPhase::Idle;
    if(row.supportHolding&&sightActive)++handOverlap;
    if(row.supportHolding!=supportClaim)++handMissingSupport;
    if(sightActive!=sightClaim)++handMissingSight;
    for(const auto* claim:{&row.left,&row.right})if(*claim&&(*claim)->deadlineNs<=row.nowNs)++handLeaseFailures;
    const auto id=[](const std::optional<interaction::HandClaim>& claim){return claim?claim->token.id:0;};
    const bool changed=id(row.left)!=id(handPrevious.left)||id(row.right)!=id(handPrevious.right)||
        row.supportHolding!=handPrevious.supportHolding||row.supportToken!=handPrevious.supportToken||row.sightPhase!=handPrevious.sightPhase;
    if(changed||handDirty||gatherSightRequest||gatherSightAck||gatherSightCommit){
        ++handEventsTotal;if(handEventCount<handEvents.size())handEvents[handEventCount++]=row;else ++handEventsDropped;
    }
    handPrevious=row;handDirty=false;handReason="transition";
}
const HandEvidence* FindHandEvidence(std::uint64_t generation,unsigned weapon)noexcept {
    for(const auto& value:handHistory)if(value.sample.sequence==generation&&value.sample.owner==handSample.owner&&value.weapon==weapon)return &value;
    return nullptr;
}
bool OwnLeft(interaction::HandClaimKind kind,bool acquire,std::uint64_t generation,std::int64_t deadline,unsigned weapon,
    const interaction::SupportGripResult* supportContinuation=nullptr,bool cancelling=false)noexcept {
    auto source=FindHandEvidence(generation,weapon);
    const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
    auto proofDeadline=source?std::min(source->sample.deadlineNs,HandNanos(deadline)):0;
    // Support ownership: the renderer's contact packet trails the live input by
    // several packets, so on a fresh squeeze it predates the grip press and
    // the arbiter's release barrier rejected every first attempt (live run
    // 005105: 70 claim rejections at 1-15 cm, one grab). The contact geometry
    // stays the renderer's own unexpired publication for this exact owner and
    // space; the hand evidence is the CURRENT tracked packet that carries the
    // squeeze. FVR_BC2_SUPPORT_CURRENT_EVIDENCE=0 restores renderer evidence.
    if(kind==interaction::HandClaimKind::WeaponSupport&&
       ((!acquire&&supportContinuation&&supportContinuation->holding)||
        (zoneSettings.supportCurrentEvidence&&HandNanos(deadline)>handSample.nowNs)))
        if(const auto current=FindHandEvidence(handSample.sequence,weapon)){source=current;generation=current->sample.sequence;proofDeadline=current->sample.deadlineNs;}
    if(!source||!gun){if(acquire){++handClaimRejected;HandChanged("claim_rejected");}return false;}
    const interaction::HandContactProof proof{
        {kind==interaction::HandClaimKind::Sight?3ull:2ull,kind==interaction::HandClaimKind::Sight?handEquip:std::uint64_t(weapon)},
        generation,proofDeadline,true};
    const auto left=handOwnership.Current(interaction::InteractionHand::Left);
    // Ammo pickup and original pending-item reservations never transfer to a support/sight claim.
    if(left&&left->token.kind==interaction::HandClaimKind::AmmoObject)return false;
    const bool continuing=!acquire&&supportContinuation&&kind==interaction::HandClaimKind::WeaponSupport&&
        interaction::SupportLeaseContinuationEligible(handPreviousLeft,handLifecycle,handSample,handItem,
            lastSupportSample.sample.token,*supportContinuation,left,gun,source->sample,proof,cancelling);
    acquire=acquire||continuing;
    interaction::HandInteractionResult result;
    if(left&&left->token.kind==kind)
        result=handOwnership.RenewFrom(handSample,source->sample,left->token,proof);
    else if(acquire){
        interaction::HandClaimRequest request{handSample.owner,interaction::InteractionHand::Left,kind,handItem,proof,++handIntent,gun->token.id};
        result=left?handOwnership.TransferFrom(handSample,source->sample,left->token,request):handOwnership.AcquireFrom(handSample,source->sample,request);
    }else return false;
    if(!result.accepted){++handClaimRejected;HandChanged("claim_rejected");}
    if(result.accepted&&(!left||left->token.kind!=kind))HandChanged(continuing?"support_lease_reservation":"acquire");
    return result.accepted;
}
bool ReserveAcknowledgedSight()noexcept {
    const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
    if(!gun||handOwnership.Current(interaction::InteractionHand::Left))return false;
    // An exact native acknowledgement continues an already dispatched gesture.
    // This is a new current-input reservation, never resurrected old geometry.
    const interaction::HandContactProof proof{{3,handEquip},handSample.sequence,handSample.deadlineNs,true};
    const interaction::HandClaimRequest request{handSample.owner,interaction::InteractionHand::Left,
        interaction::HandClaimKind::Sight,handItem,proof,++handIntent,gun->token.id};
    const bool accepted=handOwnership.Acquire(handSample,request).accepted;
    if(accepted)HandChanged("native_ack_reservation");
    else{++handClaimRejected;HandChanged("claim_rejected");}
    return accepted;
}
void ObserveHandOwnership(const Owner& owner,const interaction::InputFrame& input,std::int64_t nowQpc,bool aimValid,bool cancelling,const std::optional<WeaponModeCommand>& family)noexcept {
    sightLeaseContinuation=false;handPreviousLeft.reset();handLifecycle={};
    handLastReset=nullptr;
    const auto actor=(std::uint64_t(owner.weak)<<32)|owner.soldier;
    const auto physical=family?std::uint64_t(family->persistent):std::uint64_t(owner.weapon);
    const auto inventory=family?family->inventory:0;
    if(handActor!=actor||handRig!=rigEpoch||handPhysical!=physical||handInventory!=inventory||
       (!family&&handNativeEquipment!=owner.equipment)){
        handActor=actor;handRig=rigEpoch;handPhysical=physical;handInventory=inventory;++handEquip;
        ResetHandOwnership("identity");
    }
    handNativeEquipment=owner.equipment;handWeapon=owner.weapon;handItem={physical,handEquip};
    const interaction::HandInteractionOwner identity{actor,rigEpoch,handEquip,input.spaceGeneration};
    const auto nowNs=HandNanos(nowQpc);
    // Slider release-to-finish: the magazine consumer still holds the loading
    // hand, so a grip release is not a HandInteraction release until the
    // settle seats or drops. The consumer still sees the physical grip.
    // The SPAS shell consumer latches the same way for AS2 release-on-insert.
    const bool loadingHandLatched=(magazinePhysical&&magazinePhysical->LatchesLoadingHand())||
        TubeLatchesLoadingHand();
    if(handSample.owner!=identity||handSample.sequence!=input.generation){
        handSample={identity,input.generation,(physicalReloadMode||bodyInventory||weaponVisibilityProbe)?HandNanos(inputDeadline-handFrequency/10):nowNs,HandNanos(inputDeadline),nowNs,
            input.focused&&input.headValid,{input.hands[0].gripTracked&&bool(input.hands[0].active&interaction::Squeeze),
            input.hands[1].gripTracked&&input.hands[1].aimTracked&&aimValid}};
        handSample.released[0]=input.hands[0].squeeze<=.35f;
        LatchLoadingHand(handSample,loadingHandLatched);
        handHistory[handHistoryNext]={handSample,owner.weapon};handHistoryNext=(handHistoryNext+1)%handHistory.size();
    }else{
        handSample.nowNs=nowNs;handSample.focused=handSample.focused&&input.focused&&input.headValid;
        handSample.tracked[0]=handSample.tracked[0]&&input.hands[0].gripTracked&&(!physicalReloadMode||bool(input.hands[0].active&interaction::Squeeze));
        handSample.tracked[1]=handSample.tracked[1]&&input.hands[1].gripTracked&&input.hands[1].aimTracked&&aimValid;
        handSample.released[0]=handSample.released[0]||input.hands[0].squeeze<=.35f;
        LatchLoadingHand(handSample,loadingHandLatched);
    }
    handPreviousLeft=handOwnership.Current(interaction::InteractionHand::Left);
    handLifecycle=handOwnership.Update(handSample);
    if(!handLifecycle.inputValid)return;
    sightLeaseContinuation=interaction::SightLeaseContinuationEligible(
        handPreviousLeft,handLifecycle,handSample,handItem,cancelling,input.hands[0].squeeze);
    // BC2 still owns an equipped gun; right grip is not a physical draw gesture.
    // This claim only supplies ownership for the dependent off-hand interaction.
    if(handSample.focused&&handSample.tracked[1]){
        const interaction::HandContactProof grip{{1,handEquip},handSample.sequence,handSample.deadlineNs,true};
        const auto right=handOwnership.Current(interaction::InteractionHand::Right);
        if(right)handOwnership.Renew(handSample,right->token,grip);
        else if(!bodyHolster||!bodyHolster->BlocksActions()){
            const interaction::HandClaimRequest request{handSample.owner,interaction::InteractionHand::Right,interaction::HandClaimKind::GunHold,handItem,grip,++handIntent,0};
            if(handOwnership.Acquire(handSample,request).accepted)HandChanged("gun_hold");
        }
    }
    // A held support grip is an input-owned action. Refresh its real claim
    // before the pump/reload consumers run, without requiring another rendered
    // proximity sample for a grip that the player has not released.
    if(twoHandGrip&&!cancelling&&supportGrip.Holding()&&lastSupportSample.sample.holding){
        auto trial=supportGrip;
        const interaction::SupportGripOwner supportOwner{actor,rigEpoch,owner.weapon};
        const auto held=trial.Update(supportOwner,input,{});
        if(held.holding&&!held.engaged)OwnLeft(interaction::HandClaimKind::WeaponSupport,false,
            handSample.sequence,inputDeadline,owner.weapon,&held,false);
    }
}

// Observe native output on the original calling thread. No game/animation
// memory is modified; forwarding EAX also preserves callers that inspect it.
void ObservePose(unsigned kind,unsigned object,unsigned matrix,unsigned caller)noexcept {
    if(!observePoses||!enabled.load(std::memory_order_acquire))return;
    ++poseCalls;if(poseRecordGate.test_and_set(std::memory_order_acquire))return;
    struct Gate {~Gate(){poseRecordGate.clear(std::memory_order_release);}} gate;
    const auto now=GetTickCount64();if(poseRecordCount>=poseRecords.size()||now-poseRecordTime[kind]<100)return;
    Owner owner{};if(!Resolve(owner)||!owner.foot)return;
    if(kind==0){if(object!=owner.soldier)return;}
    else if(U32(object+4)!=owner.soldier||(U32(owner.soldier+0x3b8)!=object&&U32(owner.soldier+0x3d0)!=object))return;
    if(kind==1&&!observedRig&&rigAttempts<3){++rigAttempts;
        const RigMemory memory{nullptr,[](void*,std::uint32_t at,void* out,std::size_t n){return Read(at,out,n);}};
        observedRig=ReadFirstPersonRig(memory,owner.soldier,owner.weak);if(observedRig)PreviewArmIk(*observedRig);
    }
    PoseRecord record{kind,GetCurrentThreadId(),caller,owner.soldier,object,now,{}};
    if(!Read(matrix,record.matrix.data(),64)){++poseRejected;return;}
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)if(!std::isfinite(record.matrix[row*4+col])){++poseRejected;return;}
    poseRecords[poseRecordCount++]=record;poseRecordTime[kind]=now;
}
std::uintptr_t __fastcall WorldPoseHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=worldPoseOriginal(self,matrix);
    if(handPoses&&enabled.load(std::memory_order_acquire)){
        Owner owner{};math::Matrix4 camera{};
        if(Resolve(owner)&&owner.foot&&owner.soldier==reinterpret_cast<unsigned>(self)&&Read(reinterpret_cast<unsigned>(matrix),&camera,64)){
            for(unsigned row=0;row<4;++row)camera.values[row][3]=row==3?1.f:0.f;
            for(unsigned n=0;n<4;++n){camera.values[2][n]=-camera.values[2][n];camera.values[n][2]=-camera.values[n][2];}
            rigPublication::PublishNativeEye(owner.soldier,owner.weak,camera);
        }
    }
    ObservePose(0,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(matrix),caller);return result;
}
std::uintptr_t __fastcall RootPoseHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=rootPoseOriginal(self,matrix);
    ObservePose(1,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(self)+16,caller);return result;
}
// Diagnostic only. Original animation runs once on its native thread. Cross-thread
// overlap is measured, never hidden by serializing or replaying native jobs.
void ObservePhase(unsigned phase)noexcept {
    if(!animationScope||!animationScope->sample||phaseRecordGate.test_and_set(std::memory_order_acquire))return;
    struct Release {~Release(){phaseRecordGate.clear(std::memory_order_release);}} release;
    if(phaseRecordCount>=phaseRecords.size())return;
    const auto anim=animationScope->animation,pose=U32(anim+0x54),sk=U32(anim+0x50),header=U32(pose+0x18),count=U32(sk+0x10);
    if(count<6||count>1024||U32(header)!=count||U32(pose+0x28)!=sk){++phaseFailures;return;}
    PhaseRecord r{};r.sequence=animationScope->sequence;r.phase=phase;r.thread=GetCurrentThreadId();r.animation=anim;
    r.world=U32(header+4);r.skin=U32(header+8);r.overrideSkin=U32(anim+0x31c);unsigned char flag=0;Read(anim+0x99c,&flag,1);r.overrideActive=flag!=0;
    const auto hash=[&](unsigned at)->std::uint64_t {
        std::array<std::byte,64*1024> buffer{};if(!Read(at,buffer.data(),count*64)){++phaseFailures;return 0;}
        std::uint64_t result=14695981039346656037ull;
        // Skip undefined SIMD padding; compare only the twelve affine scalars.
        for(unsigned i=0;i<count*64;++i)if((i%16)<12){result^=std::to_integer<unsigned char>(buffer[i]);result*=1099511628211ull;}return result;
    };
    r.worldHash=hash(r.world);r.skinHash=hash(r.skin);if(r.overrideSkin)r.overrideHash=hash(r.overrideSkin);
    phaseRecords[phaseRecordCount++]=r;
}
std::uintptr_t __fastcall AnimationHook(void* self,void*,float dt,float alpha,bool full){
    Callback callback;Owner owner{};
    if(!observePoses||!enabled.load(std::memory_order_acquire)||animationScope||!Resolve(owner)||!owner.foot||reinterpret_cast<unsigned>(self)!=owner.soldier+0x214||U32(U32(owner.soldier)+0x160)!=image+rigProfile.animationGetter)
        return animationOriginal(self,dt,alpha,full);
    unsigned char flags=0;Read(owner.soldier+0x475,&flags,1);if(!(flags&1))return animationOriginal(self,dt,alpha,full);
    AnimationScope local{U32(owner.soldier+0x3b4),++animationCalls,false};local.effectOwner={owner.soldier,owner.weak,owner.weapon};
    const auto now=GetTickCount64();auto last=phaseLastTime.load();
    local.sample=now-last>=100&&phaseLastTime.compare_exchange_strong(last,now);
    if(animationInFlight.fetch_add(1)!=0)++animationOverlap;
    struct Release {~Release(){animationScope=nullptr;--animationInFlight;}} release;animationScope=&local;
    ObservePhase(0);const auto result=animationOriginal(self,dt,alpha,full);ObservePhase(4);
    animationHookThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    return result;
}
std::uintptr_t __fastcall EvaluateHook(void* self,void*,float dt,bool full){
    Callback callback;const auto result=evaluateOriginal(self,dt,full);
    if(animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self))ObservePhase(1);return result;
}
std::uintptr_t __fastcall PostEvaluateHook(void* self,void*,float alpha){
    Callback callback;const auto result=postOriginal(self,alpha);
    if(animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self))ObservePhase(2);return result;
}
std::string_view ReadWeaponAsset(unsigned weapon,std::array<char,64>& name)noexcept;
// Every launcher row is kept; automatic rifles are sampled (two rows per kind
// per second) so sustained fire cannot evict launcher/selection evidence.
std::array<std::atomic<ULONGLONG>,4> effectRowSampled{};
bool KeepEffectRow(WeaponEffectRecordKind kind,std::string_view asset)noexcept {
    if(asset=="40mmgl")return true;
    auto& last=effectRowSampled[unsigned(kind)&3];const auto now=GetTickCount64();auto previous=last.load(std::memory_order_relaxed);
    return now-previous>=500&&last.compare_exchange_strong(previous,now,std::memory_order_relaxed);
}
std::int64_t EffectNanos()noexcept {LARGE_INTEGER now{};QueryPerformanceCounter(&now);return HandNanos(now.QuadPart);}
// Animation thread only: the last weapon root the VR rig wrote. The render
// side keeps drawing that pose for up to 25 ms (rigPublication PoseHoldNs)
// when a frame publishes nothing, so an effect spawned on such a frame is
// placed on the drawn gun rather than on the native camera-relative root.
struct DrawnRoot {EffectAnchorOwner owner{};math::Matrix4 root{};std::int64_t ns=0,deadlineNs=0;bool valid=false;};
DrawnRoot lastDrawnRoot;constexpr std::int64_t DrawnRootHoldNs=25000000;
void ApplyEffectTransform(std::uint32_t manager,std::uint32_t handle,const math::Matrix4& world)noexcept {
    // Native LinearTransform layout; the setter copies the twelve affine scalars.
    alignas(16) std::array<float,16> raw{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)raw[r*4+c]=world.values[r][c];
    effectSetTransform(reinterpret_cast<void*>(std::uintptr_t(manager)),handle,raw.data());
}
// Same animation update, after RetargetWeapon: move this owner's recent 1P
// FireEffects instances to the root the visible gun uses this frame. Only
// a root actually written by the VR rig is used; on a native-output frame the
// effects keep their last VR-anchored transform instead of snapping back.
void ReanchorWeaponEffects(const AnimationScope& frame,const float* root,bool written)noexcept {
    if(!effectAnchorInstalled||!effectAnchorConfig.enabled||!effectSetTransform)return;
    std::unique_lock lock(effectAnchorMutex,std::try_to_lock);if(!lock.owns_lock())return;
    if(!effectAnchor.Live())return;
    auto& counters=WeaponEffectEvidence().Count();
    if(!written){counters.reanchorSkippedUnwritten.fetch_add(1,std::memory_order_relaxed);return;}
    const auto moved=effectAnchor.Reanchor(frame.effectOwner,NativeAffine(root),EffectNanos(),effectAnchorConfig.windowNs,ApplyEffectTransform);
    if(moved){counters.reanchorCalls.fetch_add(moved,std::memory_order_relaxed);counters.reanchorFrames.fetch_add(1,std::memory_order_relaxed);}
}
std::uintptr_t __fastcall WeaponWorldHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=weaponWorldOriginal(self,matrix);
    if(handPoses&&animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self)&&caller==image+rigProfile.animationUpdate+0x85){
        // The output is the verified caller-owned stack buffer of this update.
        alignas(16) std::array<float,16> before{},after{};std::memcpy(before.data(),matrix,64);
        rigPublication::RetargetWeapon(reinterpret_cast<unsigned>(self),matrix);
        std::memcpy(after.data(),matrix,64);
        const bool written=std::memcmp(before.data(),after.data(),64)!=0;
        animationScope->effectRoot=reinterpret_cast<unsigned>(matrix);animationScope->effectRootWritten=written;
        // The getter remains the native ballistic baseline. Effects instead
        // follow the current private mesh root, including visual recoil/kick.
        auto effectRoot=after;bool effectRootCurrent=false;
        if(written&&effectAnchorInstalled){
            const auto& effectOwner=animationScope->effectOwner;
            if(const auto pose=rigPublication::ReadWeaponShotFrame(effectOwner.soldier,effectOwner.weak,effectOwner.weapon)){
                auto visual=pose->trackedWeapon;
                for(unsigned n=0;n<4;++n){visual.values[2][n]=-visual.values[2][n];visual.values[n][2]=-visual.values[n][2];}
                std::memcpy(effectRoot.data(),&visual,64);
                const auto now=EffectNanos(),deadline=HandNanos(pose->deadline);
                effectRootCurrent=now<deadline;
                if(effectRootCurrent){std::unique_lock lock(effectAnchorMutex,std::try_to_lock);
                    if(lock.owns_lock())lastDrawnRoot={effectOwner,NativeAffine(effectRoot.data()),now,deadline,true};}
            }
            if(!effectRootCurrent&&effectAnchorConfig.writtenRoot){
                // No current shot frame (e.g. no jntWpn_Flash child): the getter
                // root written by the VR rig THIS update is still the drawn gun's
                // root (without private visual recoil). Never a native root.
                effectRoot=after;effectRootCurrent=true;
                WeaponEffectEvidence().Count().reanchorWrittenRoot.fetch_add(1,std::memory_order_relaxed);
            }
        }
        (written?WeaponEffectEvidence().Count().weaponOutputsWritten:WeaponEffectEvidence().Count().weaponOutputsNative).fetch_add(1,std::memory_order_relaxed);
        ReanchorWeaponEffects(*animationScope,effectRoot.data(),effectRootCurrent);
    }
    if(animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self))ObservePhase(3);return result;
}
// 0x7f90b0 thiscall, ret 0x18. Keep native per-entry normal/zoom offset,
// rotation and particle basis. Only bypass the flat-screen FOV warp for the
// exact current owner's FireEffects call with a still-leased VR weapon pose.
void __fastcall EffectComposeHook(void* effects,void*,const void* root,const void* camera,
                                 const void* offset,const void* rotation,bool firstPerson,void* output){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    if(firstPerson&&enabled.load(std::memory_order_acquire)&&effectAnchorInstalled&&effectAnchorConfig.enabled&&
       caller==image+effectProfile.composeReturn&&animationScope&&animationScope->effectRoot==reinterpret_cast<unsigned>(root)){
        const auto& frame=*animationScope;Owner owner{};
        if(Resolve(owner)&&owner.foot&&owner.soldier==frame.effectOwner.soldier&&owner.weak==frame.effectOwner.weak&&
           owner.weapon==frame.effectOwner.weapon&&U32(owner.weapon+0x38)==reinterpret_cast<unsigned>(effects)){
            const auto pose=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
            const bool leased=pose&&EffectNanos()<HandNanos(pose->deadline);
            // The exact root buffer was overwritten by RetargetWeapon earlier in
            // this same update: it is the VR gun, so the flat-screen FOV warp
            // must not displace its offsets either.
            const bool writtenRoot=!leased&&effectAnchorConfig.writtenRoot&&frame.effectRootWritten;
            if(leased||writtenRoot){
                firstPerson=false;
                WeaponEffectEvidence().Count().fovCompensationBypassed.fetch_add(1,std::memory_order_relaxed);
                if(writtenRoot)WeaponEffectEvidence().Count().fovBypassWrittenRoot.fetch_add(1,std::memory_order_relaxed);
            }
        }
    }
    effectComposeOriginal(effects,root,camera,offset,rotation,firstPerson,output);
}
// Verified 1P FireEffects spawn (see Bc2Profile.h WeaponEffectCandidates):
// thiscall(manager,asset,const LinearTransform*) ret 8 -> handle. Only the call
// from ClientWeaponFiringEffects::Update inside THIS owned soldier's animation
// update is observed; every other effect in the game passes straight through.
unsigned __fastcall EffectSpawnHook(void* manager,void*,void* asset,const void* transform){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    const auto handle=effectSpawnOriginal(manager,asset,transform);
    if(!enabled.load(std::memory_order_acquire)||caller!=image+effectProfile.spawnReturn||!animationScope||!animationScope->effectRoot||!transform)return handle;
    auto& evidence=WeaponEffectEvidence();auto& counters=evidence.Count();counters.spawnsSeen.fetch_add(1,std::memory_order_relaxed);
    const auto& frame=*animationScope;
    const auto root=NativeAffine(reinterpret_cast<const float*>(std::uintptr_t(frame.effectRoot)));
    const auto spawn=NativeAffine(static_cast<const float*>(transform));
    if(!frame.effectRootWritten)counters.spawnsNativeRoot.fetch_add(1,std::memory_order_relaxed);
    bool tracked=false;
    if(effectAnchorInstalled&&effectAnchorConfig.enabled&&handle){
        std::unique_lock lock(effectAnchorMutex,std::try_to_lock);
        const auto now=EffectNanos();
        if(lock.owns_lock())tracked=effectAnchor.Add(frame.effectOwner,unsigned(reinterpret_cast<std::uintptr_t>(manager)),handle,root,spawn,now)==WeaponEffectAnchor::Track::Stored;
        (tracked?counters.spawnsTracked:counters.spawnsRejected).fetch_add(1,std::memory_order_relaxed);
        // Native spawn already includes each effect's configured offset. Move
        // that exact local relation once onto the visual root, even on a written
        // getter frame (the getter deliberately excludes private visual recoil).
        if(tracked&&lastDrawnRoot.valid&&lastDrawnRoot.owner==frame.effectOwner&&
           now>=lastDrawnRoot.ns&&now-lastDrawnRoot.ns<=DrawnRootHoldNs&&now<lastDrawnRoot.deadlineNs&&effectSetTransform){
            if(effectAnchor.Reanchor(frame.effectOwner,lastDrawnRoot.root,now,effectAnchorConfig.windowNs,ApplyEffectTransform)&&!frame.effectRootWritten)
                counters.spawnsNativeRootCorrected.fetch_add(1,std::memory_order_relaxed);
        }
    }
    WeaponEffectRecord row;row.kind=WeaponEffectRecordKind::Spawn;row.tick=gathers.load(std::memory_order_relaxed);row.qpcNs=EffectNanos();
    row.weapon=frame.effectOwner.weapon;row.a=unsigned(reinterpret_cast<std::uintptr_t>(manager));row.b=handle;
    row.flags=(frame.effectRootWritten?weapon_effect_flag::RootWritten:0u)|(effectAnchorConfig.enabled?weapon_effect_flag::AnchorEnabled:0u)|(tracked?weapon_effect_flag::Tracked:0u);
    std::array<char,64> name{};const auto assetName=ReadWeaponAsset(frame.effectOwner.weapon,name);
    if(KeepEffectRow(row.kind,assetName)){SetRecordAsset(row,assetName);row.m[0]=root;row.m[1]=spawn;row.matrices=3;evidence.Push(row);}
    return handle;
}
// Observe only caller-owned results produced during the server shot path.
// Matching data/actor links identify candidates, not permission to alter fire.
std::uintptr_t __fastcall FireOriginHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    const auto result=fireOriginOriginal(self,matrix);
    if(!enabled.load(std::memory_order_acquire)||(caller!=image+fireProfile.callerA&&caller!=image+fireProfile.callerB))return result;
    ++fireOriginCalls;if(fireRecordGate.test_and_set(std::memory_order_acquire))return result;
    struct Release {~Release(){fireRecordGate.clear(std::memory_order_release);}} release;
    Owner owner{};const auto effects=reinterpret_cast<unsigned>(self);
    if(!Resolve(owner)||!owner.foot||!owner.weapon||!Type(effects,"ServerWeaponFiringEffects"))return result;
    const auto client=U32(owner.weapon+0x38),data=U32(effects+0x10),player=U32(effects+0x128),vt=U32(player);
    if(!Type(client,"ClientWeaponFiringEffects")||!Type(data,"WeaponFiringData")||data!=U32(client+0x10)||!InImage(vt,0x34)||U32(vt+0x30)!=image+fireProfile.serverControlledGetter)return result;
    const auto soldier=U32(player+0xc3c),callbackObject=U32(effects+0xd4);
    if(!Type(soldier,"ServerSoldierEntity")||U32(soldier+0xc)!=U32(owner.soldier+0xc)||U32(callbackObject+4)!=data)return result;
    const PlayerPairMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto id=MatchServerPlayer(memory,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player,player);
    if(!id||U32(player+0xc6c)!=soldier)return result;
    ++fireOriginMatches;if(fireRecordCount>=fireRecords.size())return result;
    FireRecord row{caller,effects,player,soldier,owner.soldier,owner.weapon,data,GetCurrentThreadId(),GetTickCount64()};
    if(result!=reinterpret_cast<unsigned>(matrix)||!Read(result,row.matrix.data(),64)||!Read(client+0x30,row.clientEffects.data(),64)||!Read(effects+0x30,row.serverEffects.data(),64)){++fireOriginRejected;return result;}
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)if(!std::isfinite(row.matrix[r*4+c])||!std::isfinite(row.clientEffects[r*4+c])||!std::isfinite(row.serverEffects[r*4+c])){++fireOriginRejected;return result;}
    row.playerId=*id;row.shot=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
    if(row.shot){const auto canonical=[](const std::array<float,16>& values){math::Matrix4 m{};std::memcpy(&m,values.data(),64);
        for(unsigned r=0;r<4;++r)m.values[r][3]=r==3?1.f:0.f;
        for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}return m;};
        row.mappedShot=interaction::PlaceFireAtMuzzle(canonical(row.matrix),row.shot->trackedFlash,2*row.shot->unitsPerMetre);
    }
    row.after=row.matrix;fireRecords[fireRecordCount++]=row;return result;
}
bool SendFeedback(interaction::FeedbackEvent e)noexcept;
bool GunHapticsEnabled()noexcept;
void StopHaptics()noexcept;
bool GunHapticsEnabled()noexcept{static const bool v=[]{char value[4]{};return !(GetEnvironmentVariableA("FVR_BC2_GUN_HAPTICS",value,sizeof(value))&&value[0]=='0');}();return v;} // kill switch
struct ShotScope {Owner owner;unsigned effects=0,serverPlayer=0,serverSoldier=0;bool supported=false;std::optional<rigPublication::WeaponShotFrame> shot;unsigned shotToken=0;bool tokenValid=false;unsigned shotSway=0;bool swayValid=false;};
thread_local ShotScope* clientShotScope=nullptr;thread_local ShotScope* serverShotScope=nullptr;
std::string_view ReadWeaponAsset(unsigned weapon,std::array<char,64>& name)noexcept {
    if(!weapon||!Read(U32(U32(weapon+4)+0xc),name.data(),name.size())){name={};return {};}
    const auto end=static_cast<const char*>(std::memchr(name.data(),0,name.size()));
    if(!end){name={};return {};}
    for(auto at=name.data();at!=end;++at)if(static_cast<unsigned char>(*at)<32||static_cast<unsigned char>(*at)>126){name={};return {};}
    return {name.data(),std::size_t(end-name.data())};
}
const Bc2WeaponProfile* CurrentWeaponProfile(unsigned weapon)noexcept {
    std::array<char,64> name{};return FindWeaponProfile(ReadWeaponAsset(weapon,name));
}
unsigned MuzzleWeaponKind(unsigned weapon)noexcept {
    const auto item=CurrentWeaponProfile(weapon);return item?item->telemetryKind:0;
}
bool SupportedMuzzleShot(const Owner& owner,unsigned effects,unsigned config,const std::optional<rigPublication::WeaponShotFrame>& shot)noexcept {
    unsigned char special=1;const auto data=U32(effects+0x10),function=U32(data+0x40);
    const auto item=CurrentWeaponProfile(owner.weapon);
    return (interaction::WeaponFeatureEnabled(item?&item->core:nullptr,interaction::WeaponFeature::TranslatedMuzzle)||
        (shot&&shot->configuredMuzzleReference))&&
        function>=0x10000&&function<UINT32_MAX-0xf0&&config==function+0xa0&&Read(config+0x4a,&special,1)&&!special&&!U32(config+0x40);
}
std::uintptr_t __fastcall ServerShootHook(void* self,void*,void* config,float dt,unsigned primary,void* context){
    Callback callback;Owner owner{};const auto effects=reinterpret_cast<unsigned>(self);
    const auto passthrough=[&]{return serverShootOriginal(self,config,dt,primary,context);};
    if(!enabled.load(std::memory_order_acquire)||!Resolve(owner)||!owner.foot||!owner.weapon||!Type(effects,"ServerWeaponFiringEffects"))return passthrough();
    const auto client=U32(owner.weapon+0x38),data=U32(effects+0x10),player=U32(effects+0x128),vt=U32(player);
    if(!Type(client,"ClientWeaponFiringEffects")||!Type(data,"WeaponFiringData")||data!=U32(client+0x10)||!InImage(vt,0x34)||U32(vt+0x30)!=image+fireProfile.serverControlledGetter)return passthrough();
    const auto soldier=U32(player+0xc3c),callbackObject=U32(effects+0xd4);
    if(!Type(soldier,"ServerSoldierEntity")||U32(soldier+0xc)!=U32(owner.soldier+0xc)||U32(callbackObject+4)!=data||U32(player+0xc6c)!=soldier)return passthrough();
    const PlayerPairMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    if(!MatchServerPlayer(memory,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player,player))return passthrough();
    ShotScope local{owner,effects,player,soldier,false,rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon)};
    local.supported=SupportedMuzzleShot(owner,effects,reinterpret_cast<unsigned>(config),local.shot);
    local.tokenValid=Read(reinterpret_cast<unsigned>(context)+0x1c,&local.shotToken,4);
    if(nativeRecoilObserveRequested&&nativeRecoilObserveInstalled)local.swayValid=Read(reinterpret_cast<unsigned>(context)+0x14,&local.shotSway,4);
    auto* previous=serverShotScope;serverShotScope=&local;struct Restore {ShotScope* previous;~Restore(){serverShotScope=previous;}} restore{previous};
    return passthrough();
}
std::uintptr_t __fastcall ClientShootHook(void* self,void*,void* config,float dt,unsigned primary,void* context){
    Callback callback;Owner owner{};const auto effects=reinterpret_cast<unsigned>(self);
    if(!enabled.load(std::memory_order_acquire)||!Resolve(owner)||!owner.foot||!owner.weapon||
       U32(owner.weapon+0x38)!=effects||!Type(effects,"ClientWeaponFiringEffects")||U32(U32(effects)+0x4c)!=image+fireProfile.clientShoot)
        return clientShootOriginal(self,config,dt,primary,context);
    const PlayerPairMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto paired=FindServerPlayer(memory,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player);
    const auto player=paired.value_or(0),soldier=player?U32(player+0xc3c):0,vt=player?U32(player):0;
    const bool campaign=player&&InImage(vt,0x34)&&U32(vt+0x30)==image+fireProfile.serverControlledGetter&&
        Type(soldier,"ServerSoldierEntity")&&U32(soldier+0xc)==U32(owner.soldier+0xc)&&U32(player+0xc6c)==soldier;
    ++clientShotCalls;ShotScope local{owner,effects,player,soldier,false,rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon)};
    local.supported=campaign&&SupportedMuzzleShot(owner,effects,reinterpret_cast<unsigned>(config),local.shot);local.tokenValid=Read(reinterpret_cast<unsigned>(context)+0x1c,&local.shotToken,4);auto* previous=clientShotScope;clientShotScope=&local;
    if(nativeRecoilObserveRequested&&nativeRecoilObserveInstalled)local.swayValid=Read(reinterpret_cast<unsigned>(context)+0x14,&local.shotSway,4);
    struct Restore {ShotScope* previous;~Restore(){clientShotScope=previous;}} restore{previous};
    return clientShootOriginal(self,config,dt,primary,context);
}
std::uintptr_t __fastcall MatrixCopyHook(void* self,void*,const void* source){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    const auto result=matrixCopyOriginal(self,source);
    if(!enabled.load(std::memory_order_acquire)||!clientShotScope||(caller!=image+fireProfile.clientCopyA&&caller!=image+fireProfile.clientCopyB))return result;
    ++clientShotCopies;if(fireRecordGate.test_and_set(std::memory_order_acquire))return result;
    struct Release {~Release(){fireRecordGate.clear(std::memory_order_release);}} release;
    if(fireRecordCount>=fireRecords.size())return result;Owner now{};const auto& shotScope=*clientShotScope;
    if(!Resolve(now)||now!=shotScope.owner||U32(now.weapon+0x38)!=shotScope.effects){++clientShotRejected;return result;}
    FireRecord row{};row.phase=1;row.clientPath=true;row.caller=caller;row.effects=shotScope.effects;row.clientSoldier=now.soldier;row.weapon=now.weapon;row.playerId=U32(now.player+0x154);row.data=U32(shotScope.effects+0x10);row.thread=GetCurrentThreadId();row.ms=GetTickCount64();
    if(result!=reinterpret_cast<unsigned>(self)||!Read(result,row.matrix.data(),64)||!Read(shotScope.effects+0x30,row.clientEffects.data(),64)){++clientShotRejected;return result;}
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)if(!std::isfinite(row.matrix[r*4+c])||!std::isfinite(row.clientEffects[r*4+c])){++clientShotRejected;return result;}
    row.shot=rigPublication::ReadWeaponShotFrame(now.soldier,now.weak,now.weapon);
    if(row.shot){const auto canonical=[](const std::array<float,16>& values){math::Matrix4 m{};std::memcpy(&m,values.data(),64);
        for(unsigned r=0;r<4;++r)m.values[r][3]=r==3?1.f:0.f;
        for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}return m;};
        row.mappedShot=interaction::PlaceFireAtMuzzle(canonical(row.matrix),row.shot->trackedFlash,2*row.shot->unitsPerMetre);
    }
    row.after=row.matrix;fireRecords[fireRecordCount++]=row;return result;
}
std::uintptr_t __fastcall ComposeHook(void* self,void*,void* output,const void* rhs){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=composeOriginal(self,output,rhs);
    if(!enabled.load(std::memory_order_acquire))return result;
    const bool client=caller==image+fireProfile.clientCompose;auto* current=client?clientShotScope:(caller==image+fireProfile.serverCompose?serverShotScope:nullptr);
    if(!current)return result;
    const bool recording=!fireRecordGate.test_and_set(std::memory_order_acquire);
    struct Release {bool held;~Release(){if(held)fireRecordGate.clear(std::memory_order_release);}} release{recording};
    Owner owner{};
    if(!Resolve(owner)||owner!=current->owner)return result;
    FireRecord row{};row.phase=2;row.clientPath=client;row.caller=caller;row.effects=current->effects;row.data=U32(current->effects+0x10);row.player=current->serverPlayer;row.serverSoldier=current->serverSoldier;row.clientSoldier=owner.soldier;row.weapon=owner.weapon;row.playerId=U32(owner.player+0x154);row.thread=GetCurrentThreadId();row.ms=GetTickCount64();row.shot=current->shot;row.weaponKind=MuzzleWeaponKind(owner.weapon);row.shotToken=current->shotToken;row.tokenValid=current->tokenValid;
    const auto at=reinterpret_cast<unsigned>(output);if(result!=at||!Read(at,row.matrix.data(),64)){++originWriteFailures;return result;}
    const auto canonical=[](const std::array<float,16>& values){math::Matrix4 m{};std::memcpy(&m,values.data(),64);for(unsigned r=0;r<4;++r)m.values[r][3]=r==3?1.f:0.f;
        for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}return m;};
    row.after=row.matrix;
    if(nativeRecoilObserveInstalled){
        // This is the caller-owned shot base before our translation override.
        // Native spread is consumed later in each shoot function; never label
        // this as a final projectile direction or use it to subtract recoil.
        NativeRecoilBoundary boundary;boundary.shotToken=row.shotToken;boundary.shotTokenValid=row.tokenValid;
        boundary.shotSway=current->shotSway;boundary.shotSwayValid=current->swayValid;
        boundary.nativeShotBase=row.matrix;boundary.shotBaseValid=true;
        for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component)
            if(!std::isfinite(boundary.nativeShotBase[4*axis+component]))boundary.shotBaseValid=false;
        if(row.shot){
            boundary.rigGeneration=row.shot->generation;boundary.rigDeadlineQpc=row.shot->deadline;
            std::memcpy(boundary.trackedWeapon.data(),&row.shot->trackedWeapon,sizeof(row.shot->trackedWeapon));
            boundary.trackedWeaponValid=true;
            for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component)
                if(!std::isfinite(boundary.trackedWeapon[4*axis+component]))boundary.trackedWeaponValid=false;
        }
        ObserveRecoilShotBase(client?NativeRecoilSeam::ClientShotBase:NativeRecoilSeam::ServerShotBase,owner,boundary);
    }
    LARGE_INTEGER q{};QueryPerformanceCounter(&q);
    const auto live=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
    if(current->supported&&row.tokenValid&&row.shot&&live&&live->space==row.shot->space&&live->ownerGeneration==row.shot->ownerGeneration){
        std::lock_guard lock(shotPoseMutex);
        row.eventPose=shotHistory.Resolve({(std::uint64_t(owner.weak)<<32)|owner.soldier,row.shot->ownerGeneration,owner.weapon,row.shot->space,row.shotToken},
            {row.shot->trackedFlash,row.shot->generation,row.shot->deadline},q.QuadPart);
        if(row.eventPose)row.mappedShot=interaction::PlaceFireAtMuzzle(canonical(row.matrix),row.eventPose->muzzle,2*row.shot->unitsPerMetre);
    }
    if(muzzleFire&&row.mappedShot&&q.QuadPart<row.eventPose->deadline){
        const auto left=reinterpret_cast<unsigned>(self),right=reinterpret_cast<unsigned>(rhs);
        const auto separate=[&](unsigned p){return std::uint64_t(at)+64<=p||std::uint64_t(p)+64<=at;};
        std::array<std::byte,64> before{},leftBefore{},rightBefore{},after{},leftAfter{},rightAfter{};
        if(separate(left)&&separate(right)&&Writable(at,64)&&Read(at,before.data(),64)&&!std::memcmp(before.data(),row.matrix.data(),64)&&Read(left,leftBefore.data(),64)&&Read(right,rightBefore.data(),64)){
            const auto& m=*row.mappedShot;const std::array<float,3> position{m.values[3][0],m.values[3][1],-m.values[3][2]};
            // Caller-owned composition result only: preserve all orientation and
            // padding bytes, and leave both native input matrices untouched.
            std::memcpy(reinterpret_cast<void*>(at+48),position.data(),12);
            const bool verified=Read(at,after.data(),64)&&!std::memcmp(before.data(),after.data(),48)&&!std::memcmp(before.data()+60,after.data()+60,4)&&!std::memcmp(after.data()+48,position.data(),12);
            if(!verified)++originWriteFailures;
            if(!Read(left,leftAfter.data(),64)||leftAfter!=leftBefore||!Read(right,rightAfter.data(),64)||rightAfter!=rightBefore)++shotSourceChanges;
            if(verified){row.originWritten=true;std::memcpy(row.after.data(),after.data(),64);if(client)++clientOriginWrites;else ++serverOriginWrites;}
        }else ++originWriteFailures;
    }
    if(muzzleFire&&current->supported&&!row.originWritten)++originFallbacks;
    std::array<char,64> shotName{};WeaponEffectEvidence().Count().shots.fetch_add(1,std::memory_order_relaxed);
    if(client&&row.shot&&(!physicalWeaponRuntime::Enabled()||
        physicalWeaponRuntime::ReadActionAdmission(owner.weapon).weapon==physicalWeaponRuntime::WeaponAdmission::Unmanaged)){
        const auto nowNs=HandNanos(q.QuadPart);
        const auto observedNs=nowNs>10000000?nowNs-10000000:nowNs;
        const auto deadlineNs=nowNs+90000000;
        // Recoil belongs to the hand holding the gun (right, index 1). The branch's braced
        // left-hand pulse (PublishRecoilObservedTracking) is gated on a held support grip.
        interaction::FeedbackEvent shotEvent{1,row.shot->generation,row.shot->space,observedNs,deadlineNs,interaction::FeedbackKind::GunFire,1};
        if(GunHapticsEnabled())SendFeedback(shotEvent);
    }
    if(const auto shotAsset=ReadWeaponAsset(owner.weapon,shotName);KeepEffectRow(WeaponEffectRecordKind::Shot,shotAsset)){
        // Projectile launch basis for owned shots (every launcher shot), independent of the 32-row log.
        auto& evidence=WeaponEffectEvidence();
        WeaponEffectRecord shot;shot.kind=WeaponEffectRecordKind::Shot;shot.tick=gathers.load(std::memory_order_relaxed);shot.qpcNs=HandNanos(q.QuadPart);
        shot.weapon=owner.weapon;shot.a=row.tokenValid?row.shotToken:0;
        shot.flags=(client?weapon_effect_flag::ClientPath:0u)|(row.originWritten?weapon_effect_flag::OriginWritten:0u)|
            (row.shot?weapon_effect_flag::ShotFrame:0u)|(row.eventPose?weapon_effect_flag::EventMuzzle:0u)|(current->supported?weapon_effect_flag::SupportedMuzzle:0u);
        SetRecordAsset(shot,shotAsset);
        shot.m[0]=NativeAffine(row.matrix.data());shot.m[1]=NativeAffine(row.after.data());shot.matrices=3;
        if(row.shot){shot.m[2]=row.shot->trackedWeapon;shot.matrices|=4;shot.generation=row.shot->generation;
            shot.source=WeaponOrientationSource(row.shot->orientationSource);
            // Canonical shot basis (same z flip as the rig) versus the visible barrel:
            // both use the model's -Z forward / +Y up.
            const auto shotCanonical=canonical(row.matrix);
            shot.delta=MeasureBasisDelta(row.shot->trackedWeapon,{0,0,-1},{0,1,0},shotCanonical,{0,0,-1},{0,1,0});}
        if(row.eventPose){shot.m[3]=row.eventPose->muzzle;shot.matrices|=8;}
        evidence.Push(shot);
    }
    if(row.originWritten&&recording&&fireRecordCount>=fireRecords.size())++originWritesAfterLogFull;
    if(recording&&fireRecordCount<fireRecords.size())fireRecords[fireRecordCount++]=row;return result;
}
// Test-only frame hitch (FVR_BC2_TEST_HITCH_MS=N[,AFTER_MS]): stall the gather thread once
// for N ms, AFTER_MS (default 150) after the first last-round slide lock, so the next
// Step sees a long delta on demand (F2000 runs 145846/150154: a 15 fps hitch, 0.0667 s).
// N,AFTER,stop: instead AFTER_MS after the first reload stop is armed (SPAS abandon,
// magazine cancel), so the abort's claim lands on the long frame (run 195831).
unsigned testHitchMs=0,testHitchAfterMs=150,testHitches=0;
// Test-only HUD-copy check (FVR_BC2_TEST_SERVER_LOADED=N,AFTER_MS): AFTER_MS after the first
// last-round slide lock, set the local server weapon's loaded count (firing+0x7c, the
// field the pickup setter 0x6cd520 writes) to N once; the client copies and the HUD are
// then observed, not touched.
int testServerLoaded=-1;unsigned testServerLoadedAfterMs=0;int testServerLoadedBefore=-1;std::uint32_t testServerLoadedFiring=0;std::int64_t testServerLoadedNs=0;bool testServerLoadedDone=false;
// N,AFTER,hold: AFTER_MS after the server copy is first seen holding at state 11 (a held
// physical reload), so the stall lands mid-hold (583b08c, no 50 ms delta cap).
bool testHitchOnStop=false,testHitchOnHold=false;std::int64_t testHitchStopSeenNs=0,testHitchHoldSeenNs=0;
// QPC nanoseconds, the reload runtime's clock, so the stall can be placed against abort claims.
std::int64_t testHitchBeginQpcNs=0,testHitchEndQpcNs=0;
std::int64_t QpcNs()noexcept{LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
    return (q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
// Stick weapon switch (weapon-switch-re.md, static RE 2026-10-08). BC2 selects on a
// rising edge on the SERVER inventory update 0x6f1980, called from the server soldier
// at 0x68dc79 (return 0x68dc7e) with the native allowSwitch gate; our client input
// never reaches that pair. A flick latches here (gather thread) and the detour, after
// the original, selects the switch-map target for EiaSwitchPrimaryWeapon (reversed for
// -1) through the native setter 0x6d7c80 on the local player's server inventory.
// The physical runtime supplies independently verified owned slots; for the gun
// slots VR permits empty weapons and waives native reactivation cooldown. The
// caller's snapshot compare then replicates the selection (dirty 0x20).
// October 9: the stick click carries an ordered candidate list over every carried
// slot (Bc2EquipmentCycle.h) instead of one target; see docs/WEAPON-SWITCH-20261009.md.
using InventoryUpdateFn=void(__thiscall*)(void*,void*,std::uint32_t);InventoryUpdateFn inventoryUpdateOriginal=nullptr;
using SelectSlotFn=void(__thiscall*)(void*,int,std::uint32_t);SelectSlotFn selectSlotNative=nullptr;
using SlotValidFn=bool(__thiscall*)(void*,int);
std::uintptr_t inventoryUpdateServerReturn=0;bool weaponSwitchHooked=false;
// Death/checkpoint null-weapon guard (docs/DEATH-RESPAWN-RELOAD-20261011.md,
// docs/re/BC2_NULL_CLIENT_WEAPON_FOV.md). Build 795745 ClientSoldier getters
// 0x89bc40/0x89bcb0 (thiscall, float in ST0, only caller the ClientPlayer view
// update 0x805e20) index the client weapon vector soldier+0x260 with the client
// inventory's +0x14c slot and dereference wrapper+0x2c (0x7efca0) with no null
// check. During the checkpoint reload after a death that slot held NULL and
// BFBC2Game.exe faulted at 0x7efca0 (playtest b74560c, 2026-10-11 21:11:56).
// When the slot's wrapper or its aiming object is missing we return the
// soldier's own fallback (+0x400/+0x404): exactly the value the native getter
// returns when its 0x89bbd0 eligibility check fails. Reads only; no writes.
// Kill switch: FVR_BC2_NULL_WEAPON_GUARD=0.
using SoldierFloatFn=float(__thiscall*)(void*);
SoldierFloatFn weaponViewFovOriginal=nullptr,weaponViewFov2Original=nullptr;
std::atomic<unsigned> nullWeaponGuardHits{0},nullWeaponGuardCalls{0};bool nullWeaponGuardInstalled=false;
// Runs twice per local view update, so it reads directly under SEH instead of
// ReadProcessMemory. Mirrors the native path exactly: inventory by soldier
// +0x114 bit 0 (+0x24c/+0x248), slot = inventory+0x14c (0x6ce960), wrapper =
// vector[slot] (bounded by +0x264-+0x260), aim = wrapper+0x2c (0x7efca0).
template<class T>T SehLoad(unsigned at)noexcept{return *reinterpret_cast<const volatile T*>(static_cast<std::uintptr_t>(at));}
bool CurrentClientWeaponAimReadable(unsigned soldier,unsigned field)noexcept {
    __try{
        const auto flags=SehLoad<unsigned char>(soldier+0x114);
        const auto inventory=SehLoad<unsigned>(soldier+((flags&1)?0x24c:0x248));if(inventory<0x10000)return false;
        const auto index=SehLoad<unsigned>(inventory+0x14c),first=SehLoad<unsigned>(soldier+0x260),last=SehLoad<unsigned>(soldier+0x264);
        if(first<0x10000||last<first||last-first>0x1000||((last-first)&3)||index>=(last-first)/4)return false;
        const auto weapon=SehLoad<unsigned>(first+4*index);if(weapon<0x10000)return false;
        const auto aim=SehLoad<unsigned>(weapon+0x2c);if(aim<0x10000)return false;
        (void)SehLoad<float>(aim+field);return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool SehReadFloat(unsigned at,float& value)noexcept {
    __try{value=SehLoad<float>(at);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
float GuardedWeaponViewFloat(void* self,SoldierFloatFn original,unsigned field,unsigned fallback)noexcept {
    nullWeaponGuardCalls.fetch_add(1,std::memory_order_relaxed);
    const auto soldier=unsigned(reinterpret_cast<std::uintptr_t>(self));float value=0;
    if(soldier>=0x10000&&!CurrentClientWeaponAimReadable(soldier,field)&&SehReadFloat(soldier+fallback,value)){
        nullWeaponGuardHits.fetch_add(1,std::memory_order_relaxed);return value;}
    return original(self);
}
float __fastcall WeaponViewFovHook(void* self,void*){return GuardedWeaponViewFloat(self,weaponViewFovOriginal,0xc,0x400);}
float __fastcall WeaponViewFov2Hook(void* self,void*){return GuardedWeaponViewFloat(self,weaponViewFov2Original,0x10,0x404);}
// Local actor loss (death, checkpoint reload): a pawn rebuilt at recycled
// addresses must still be a new physical-weapon authority. Set on the input
// thread when the local update cannot resolve AND the soldier is structurally
// absent; any such loss advances the actor generations on the next resolve.
// Losses shorter than ActorLossRebindMs are still rebound (absence is
// structural, not timing) but counted separately as "short" for telemetry.
// FVR_BC2_ACTOR_LOSS_REBIND=0 restores pointer-only generation.
constexpr ULONGLONG ActorLossRebindMs=500;
ULONGLONG localActorLostAt=0;bool actorLossRebind=true;
std::atomic<unsigned> actorLossRebinds{0},actorLossTransient{0},actorLossObserved{0},lastActorLossMs{0};
// Structural absence only: the local ClientPlayer or its live ClientSoldier is
// gone (death, ragdoll handoff, level/checkpoint teardown). A Resolve failure
// for any other reason (weapon data mid-switch, vehicle input router) is not a
// loss and must not retire the reload ledger or its held magazine metadata.
bool LocalSoldierAbsent()noexcept {
    const auto manager=U32(image+profile.gameplay.contextObject+8);
    if(U32(manager)!=image+profile.gameplay.managerVtable)return true;
    const auto player=U32(manager+profile.gameplay.localPlayerOffset);if(!player)return true;
    const auto target=U32(U32(player+profile.gameplay.soldierWeakOffset));if(target<4)return true;
    const auto soldier=target-4;
    return !Type(soldier,"ClientSoldierEntity")||U32(soldier+0x220)!=player;
}
void ObserveLocalActorLoss()noexcept {
    if(!actorLossRebind||!LocalSoldierAbsent())return;
    if(!localActorLostAt){localActorLostAt=GetTickCount64();actorLossObserved.fetch_add(1,std::memory_order_relaxed);}
}
std::atomic<int> flickDirection{0};std::atomic<std::uint32_t> flickSerial{0};std::atomic<ULONGLONG> flickAt{0};std::atomic<std::uint32_t> flickConsumed{0};
std::atomic<unsigned> flickLatched{0},flickBlocked{0},flickSelected{0},flickNoTarget{0},flickGated{0},flickExpired{0},flickNoInventory{0};std::atomic<int> flickLastTarget{-1},flickLastFrom{-1};
// Right stick click cycles everything carried (Bc2EquipmentCycle.h). Settings are
// read once in Start: FVR_BC2_STICK_CYCLE=0 restores the October 8 equipment-only
// click and drops the long-click backward cycle; FVR_BC2_STICK_CYCLE_MELEE_GRENADE=0
// keeps the knife and hand grenade out of the cycle.
EquipmentCycleSettings cycleSettings{};
// Server-callback outcome of each latched request: serial<<8 | SwitchOutcome.
enum class SwitchOutcome : std::uint8_t {None,Selected,NoTarget,Unchanged,Expired};
std::atomic<std::uint64_t> switchOutcome{0};
std::atomic<int> switchLastSelected{-1};std::atomic<ULONGLONG> switchLastSelectedAt{0};
std::atomic<unsigned> switchSkippedIdentity{0},switchSkippedInvalid{0},switchSelectRejected{0},switchCoalesced{0},switchUnchanged{0};
std::array<std::atomic<unsigned>,EquipmentSlotCount> switchSelectedSlots{};
void PublishSwitchOutcome(std::uint32_t serial,SwitchOutcome outcome)noexcept{switchOutcome.store((std::uint64_t(serial)<<8)|std::uint64_t(outcome),std::memory_order_release);}
// Inventory's own slot validity callback ([inv+0x148] vtable+4, thiscall (slot)),
// the one the October 8 switch path already uses on this server thread.
bool NativeSlotValid(std::uintptr_t inventory,int slot)noexcept {
    std::uint32_t valid=0,table=0,fn=0;
    if(!Read(inventory+0x148,&valid,4)||!Read(valid,&table,4)||!Read(std::uintptr_t(table)+4,&fn,4)||fn<0x400000)return false;
    return reinterpret_cast<bool(__thiscall*)(void*,int)>(std::uintptr_t(fn))(reinterpret_cast<void*>(std::uintptr_t(valid)),slot);
}
// The reload runtime publishes the local server inventory only while a supported gun
// is held. After our own select onto equipment, that inventory stays ours while it
// still holds the slot we selected and no fresh publication names another one.
std::uintptr_t equipmentInventory=0;int equipmentSlot=-1;std::atomic<unsigned> flickEquipmentOwned{0};
// An ordered fallback list captured from one coherent inventory snapshot. The
// server callback revalidates each item's identity and native validity.
struct PhysicalSwitchRequest {
    std::array<PlayerInventoryCandidate,EquipmentSlotCount> targets{};
    unsigned count=0;
    std::uint32_t serial=0;
};
std::mutex physicalSwitchMutex;
std::optional<PhysicalSwitchRequest> physicalSwitchRequest;
enum class SwitchLatch {Latched,NoInventory,NoTarget};
SwitchLatch LatchWeaponSwitch(StickSwitch request,const PlayerInventoryAnchor& actor)noexcept {
    std::lock_guard lock(physicalSwitchMutex);
    const auto now=GetTickCount64();
    // A request still waiting for its server update is replaced, chaining from
    // the slot it was heading to: two quick clicks move two items, not one.
    const bool pending=flickSerial.load(std::memory_order_acquire)!=flickConsumed.load(std::memory_order_acquire)&&
        now-flickAt.load(std::memory_order_relaxed)<=300;
    SwitchChainState chain{};
    if(pending&&physicalSwitchRequest&&physicalSwitchRequest->count)chain.pendingTarget=int(physicalSwitchRequest->targets[0].item.slot);
    chain.selectedTarget=switchLastSelected.load(std::memory_order_acquire);chain.selectedAtMs=switchLastSelectedAt.load(std::memory_order_acquire);
    physicalSwitchRequest.reset();
    if(physicalWeaponRuntime::Enabled()){
        const auto inventory=physicalWeaponRuntime::Inventory();
        if(!inventory||inventory->anchor!=actor){++flickNoInventory;return SwitchLatch::NoInventory;}
        std::uint32_t occupied=0;
        for(std::uint32_t slot=0;slot<EquipmentSlotCount;++slot)if(Candidate(*inventory,slot))occupied|=1u<<slot;
        const auto from=SwitchChainBase(chain,inventory->selectedSlot,now);
        const auto plan=PlanStickSwitch(request,occupied,from,inventory->lastPrimary,cycleSettings);
        PhysicalSwitchRequest next{};
        for(unsigned n=0;n<plan.count;++n)if(const auto candidate=Candidate(*inventory,plan.slots[n]))next.targets[next.count++]=*candidate;
        if(!next.count){++flickNoTarget;return SwitchLatch::NoTarget;}
        if(chain.pendingTarget>=0)++switchCoalesced;
        physicalSwitchRequest=next;
    }
    flickDirection.store(int(request),std::memory_order_relaxed);flickAt.store(now,std::memory_order_relaxed);
    const auto serial=flickSerial.fetch_add(1,std::memory_order_release)+1;
    if(physicalSwitchRequest)physicalSwitchRequest->serial=serial;
    ++flickLatched;return SwitchLatch::Latched;
}
// Which slots a flick may land on: the two primaries (BC2 slots 0/1; SP has no other
// gun slot). Everything else is on the right stick click cycle (PlanStickSwitch).
bool FlickSlotAllowed(int slot)noexcept{return slot==0||slot==1;} // guns only (owner decision, 2026-10-08)
int SwitchTarget(std::uintptr_t inventory,int direction)noexcept {
    std::uint32_t current=0,data=0,begin=0,end=0,valid=0,validTable=0,validFn=0;
    if(!Read(inventory+0x14c,&current,4)||!Read(inventory+4,&data,4)||!Read(std::uintptr_t(data)+0x18,&begin,4)||!Read(std::uintptr_t(data)+0x1c,&end,4)||
       end<=begin||end-begin>128*24||(end-begin)%24||!Read(inventory+0x148,&valid,4)||!Read(valid,&validTable,4)||!Read(std::uintptr_t(validTable)+4,&validFn,4)||validFn<0x400000)return -1;
    flickLastFrom.store(int(current),std::memory_order_relaxed);
    for(std::uintptr_t at=begin;at<end;at+=24){std::uint32_t from=0,action=0,tb=0,te=0;
        if(!Read(at+20,&from,4)||!Read(at+16,&action,4)||from!=current||action!=7)continue;
        if(!Read(at+8,&tb,4)||!Read(at+12,&te,4)||te<=tb||te-tb>44||(te-tb)%4)return -1;
        std::array<int,11> targets{};unsigned n=0;for(std::uintptr_t t=tb;t<te&&n<targets.size();t+=4){std::uint32_t v=0;if(!Read(t,&v,4))return -1;targets[n++]=int(v);}
        if(direction<0)std::reverse(targets.begin(),targets.begin()+n);
        for(unsigned i=0;i<n;++i){const int slot=targets[i];if(!FlickSlotAllowed(slot)||slot==int(current))continue; // 9/10 (last/other primary) not used
            if(reinterpret_cast<SlotValidFn>(std::uintptr_t(validFn))(reinterpret_cast<void*>(std::uintptr_t(valid)),slot))return slot;}
        break;}
    // Holding equipment whose switch-map row has no gun: back to the last primary.
    if(current>=2&&current<=8){std::uint32_t last=0;const auto ok=[&](int slot){return reinterpret_cast<SlotValidFn>(std::uintptr_t(validFn))(reinterpret_cast<void*>(std::uintptr_t(valid)),slot);};
        if(Read(inventory+0x154,&last,4)&&last<=1&&ok(int(last)))return int(last);if(ok(0))return 0;if(ok(1))return 1;}
    return -1;
}
void __fastcall InventoryUpdateHook(void* inventory,void*,void* pair,std::uint32_t allowSwitch){
    const bool localServerCaller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())==inventoryUpdateServerReturn;
    const auto self=reinterpret_cast<std::uintptr_t>(inventory);
    // Body/equipment input is consumed inside the original update, before our
    // explicit stick selector below. Preserve its update/receipt work while
    // withholding only the existing low-byte switch permission for a positively
    // owned managed source with an outstanding obligation. Unknown ownership,
    // native equipment/death paths and other soldiers retain native behavior.
    if(localServerCaller&&physicalWeaponRuntime::Enabled()){
        const auto owned=physicalWeaponRuntime::Inventory();
        std::uint32_t selected=0;
        if(owned&&owned->inventory==self&&Read(self+0x14c,&selected,4)&&selected<owned->count){
            const auto& source=owned->slots[selected];
            const auto admission=physicalWeaponRuntime::ReadActionAdmission(source.clientWeapon,source.data);
            if(admission.weapon==physicalWeaponRuntime::WeaponAdmission::Managed&&
                (admission.pendingCommand||admission.nativeBusy))allowSwitch&=~0xffu;
        }
    }
    inventoryUpdateOriginal(inventory,pair,allowSwitch);
    if(!localServerCaller)return;
    if(physicalWeaponRuntime::Enabled()){
        // Runs on the native server inventory thread, inside its before/after
        // snapshot window. Ownership publication is independent of reloads.
        physicalWeaponRuntime::InventoryUpdate(unsigned(self));
        PhysicalSwitchRequest request{};
        {
            std::lock_guard lock(physicalSwitchMutex);
            const auto serial=flickSerial.load(std::memory_order_acquire);
            if(serial==flickConsumed.load(std::memory_order_acquire))return;
            if(GetTickCount64()-flickAt.load(std::memory_order_relaxed)>300){flickConsumed.store(serial,std::memory_order_release);++flickExpired;
                physicalSwitchRequest.reset();PublishSwitchOutcome(serial,SwitchOutcome::Expired);return;}
            if(!physicalSwitchRequest||physicalSwitchRequest->serial!=serial||!physicalSwitchRequest->count)return;
            request=*physicalSwitchRequest;
        }
        if(request.targets[0].inventory!=self)return; // Another soldier's update.
        const auto current=physicalWeaponRuntime::Inventory();
        if(!current||current->inventory!=self||current->anchor!=request.targets[0].anchor||
            current->serverSoldier!=request.targets[0].serverSoldier||current->serverPlayer!=request.targets[0].serverPlayer)return;
        if(!(allowSwitch&0xffu)){++flickGated;return;} // Retry this exact request while its lease remains live.
        std::uint32_t before=0;
        if(!Read(self+0x14c,&before,4))return;
        // Refresh the source's authority after the inventory callback drained
        // pending receipts. Never keep its mutex across native selection.
        if(before>=current->count||physicalWeaponRuntime::ReadActionAdmission(
            current->slots[before].clientWeapon,current->slots[before].data).BlocksSelection()){
            ++flickGated;return;
        }
        // Claim this serial under the producer lock, then release it before the
        // native callback chain. A newer flick remains a separate pending action.
        {
            std::lock_guard lock(physicalSwitchMutex);
            if(flickSerial.load(std::memory_order_acquire)!=request.serial)return;
            flickConsumed.store(request.serial,std::memory_order_release);physicalSwitchRequest.reset();
            if(GetTickCount64()-flickAt.load(std::memory_order_relaxed)>300){++flickExpired;PublishSwitchOutcome(request.serial,SwitchOutcome::Expired);return;}
        }
        flickLastFrom.store(int(before),std::memory_order_relaxed);
        // First candidate that is still the same item and natively selectable
        // wins. Guns keep the empty-weapon/cooldown waiver (validate 0); other
        // slots must pass the inventory's own validity callback, and the knife
        // and grenade are selected with native validation.
        for(unsigned n=0;n<request.count;++n){
            const auto& wanted=request.targets[n];const auto slot=wanted.item.slot;
            const auto target=Candidate(*current,slot);
            if(!target||target->item.item!=wanted.item.item||target->item.data!=wanted.item.data||target->item.serial!=wanted.item.serial){++switchSkippedIdentity;continue;}
            if(before==slot){++switchUnchanged;PublishSwitchOutcome(request.serial,SwitchOutcome::Unchanged);return;}
            if(SlotNeedsNativeValidity(slot)&&!NativeSlotValid(self,int(slot))){++switchSkippedInvalid;continue;}
            selectSlotNative(inventory,int(slot),SlotSelectValidates(slot)?1:0);
            std::uint32_t after=0;
            if(Read(self+0x14c,&after,4)&&after==slot){
                flickLastTarget.store(int(after),std::memory_order_relaxed);++flickSelected;
                if(slot<switchSelectedSlots.size())++switchSelectedSlots[slot];
                switchLastSelectedAt.store(GetTickCount64(),std::memory_order_release);switchLastSelected.store(int(slot),std::memory_order_release);
                PublishSwitchOutcome(request.serial,SwitchOutcome::Selected);return;
            }
            ++switchSelectRejected;
            if(after!=before)break; // The native setter moved somewhere else: stop, report.
        }
        ++flickNoTarget;PublishSwitchOutcome(request.serial,SwitchOutcome::NoTarget);
        return;
    }
    const auto serial=flickSerial.load(std::memory_order_acquire);if(serial==flickConsumed.load(std::memory_order_acquire))return;
    const auto local=reloadFlowRuntime::LocalServerInventory();
    if(local&&local!=equipmentInventory)equipmentInventory=0;
    std::uint32_t held=0;
    const bool owned=!local&&equipmentInventory&&self==equipmentInventory&&Read(self+0x14c,&held,4)&&int(held)==equipmentSlot;
    if(!local&&!equipmentInventory){if(GetTickCount64()-flickAt.load(std::memory_order_acquire)>300){flickConsumed.store(serial);++flickNoInventory;PublishSwitchOutcome(serial,SwitchOutcome::Expired);}return;}
    if(local?self!=local:!owned){ // another soldier: wait for ours
        if(!local&&self==equipmentInventory&&GetTickCount64()-flickAt.load(std::memory_order_acquire)>300){flickConsumed.store(serial);++flickNoInventory;PublishSwitchOutcome(serial,SwitchOutcome::Expired);}return;}
    if(owned)++flickEquipmentOwned;
    if(GetTickCount64()-flickAt.load(std::memory_order_acquire)>300){flickConsumed.store(serial);++flickExpired;PublishSwitchOutcome(serial,SwitchOutcome::Expired);return;}
    if(!(allowSwitch&0xffu)){++flickGated;return;}
    flickConsumed.store(serial);
    const auto direction=StickSwitch(flickDirection.load(std::memory_order_acquire));
    int target=-1;
    if(direction==StickSwitch::NextGun||direction==StickSwitch::PreviousGun)target=SwitchTarget(self,int(direction));
    else{
        // Without the physical authority there is no published snapshot: the
        // native validity callback alone says which slots can be selected.
        std::uint32_t current=0,last=0,occupied=0;
        if(Read(self+0x14c,&current,4)){Read(self+0x154,&last,4);flickLastFrom.store(int(current),std::memory_order_relaxed);
            for(std::uint32_t slot=0;slot<EquipmentSlotCount;++slot)if(slot!=current&&NativeSlotValid(self,int(slot)))occupied|=1u<<slot;
            target=PlanStickSwitch(direction,occupied,current,last,cycleSettings).First();}
    }
    if(target<0){++flickNoTarget;PublishSwitchOutcome(serial,SwitchOutcome::NoTarget);return;}
    selectSlotNative(inventory,target,1);std::uint32_t selected=0;
    if(!Read(self+0x14c,&selected,4)||int(selected)!=target){++flickNoTarget;PublishSwitchOutcome(serial,SwitchOutcome::NoTarget);return;}
    flickLastTarget.store(target,std::memory_order_relaxed);++flickSelected;
    if(unsigned(target)<switchSelectedSlots.size())++switchSelectedSlots[target];
    switchLastSelectedAt.store(GetTickCount64(),std::memory_order_release);switchLastSelected.store(target,std::memory_order_release);
    PublishSwitchOutcome(serial,SwitchOutcome::Selected);
    if(target>=2){equipmentInventory=self;equipmentSlot=target;}else equipmentInventory=0;
}
// One writer numbers every producer's feedback: the channel and the host accept
// only ids above the last one they saw, whichever consumer produced it.
std::uint64_t feedbackWireId=0;std::atomic<ULONGLONG> lastPulseTick{0};
bool SendFeedback(interaction::FeedbackEvent e)noexcept {
    if(!writeFeedback||feedbackWireId==UINT64_MAX)return false;
    e.id=feedbackWireId+1;if(!writeFeedback(e))return false;
    feedbackWireId=e.id;
    if(e.kind!=interaction::FeedbackKind::StopAll)lastPulseTick.store(GetTickCount64(),std::memory_order_relaxed);
    return true;
}
bool PhysicalEventAudioEnabled()noexcept {
    static const bool on=[]{char v[8]{};return GetEnvironmentVariableA("FVR_BC2_RELOAD_AUDIO",v,sizeof(v))!=1||v[0]!='0';}();return on;
}
bool PublishReloadAudioOwner(const Owner& owner,const interaction::InputFrame& input,std::string_view asset)noexcept {
    reloadAudioSuppression.store({},std::memory_order_release);reloadAudioReadyUntil=0;
    if(!PhysicalEventAudioEnabled()||!writeReloadAudioOwner||!writeReloadAudio||!readReloadAudioReady)return false;
    if((asset!=Xm8MagazineAsset&&asset!=SpasReloadAsset)||!input.focused||!input.headValid||
       !input.hands[0].gripTracked||!input.hands[1].gripTracked){writeReloadAudioOwner(0,0,0);reloadAudioWeapon=0;reloadAudioBinding.reset();return false;}
    const auto actor=(std::uint64_t(owner.weak)<<32)|owner.soldier;
    if(reloadAudioWeapon!=owner.weapon||reloadAudioActor!=actor||reloadAudioEquip!=epoch||reloadAudioSpace!=input.spaceGeneration){
        if(reloadAudioOwnerSerial==UINT64_MAX)return false;
        ++reloadAudioOwnerSerial;reloadAudioWeapon=owner.weapon;reloadAudioActor=actor;reloadAudioEquip=epoch;reloadAudioSpace=input.spaceGeneration;
    }
    if(!writeReloadAudioOwner(reloadAudioOwnerSerial,input.spaceGeneration,handSample.observedNs)){++reloadAudioOwnerRejected;return false;}
    ++reloadAudioOwnerPublished;
    reloadAudioReadyUntil=readReloadAudioReady(reloadAudioOwnerSerial,input.spaceGeneration);
    const auto now=AnimNowNs();if(reloadAudioReadyUntil<=now){++reloadAudioNotReady;return false;}++reloadAudioReadySamples;
    // Cache only during fresh owned Gather. Failed bindings retry at 1 Hz;
    // successful native object proofs are rechecked every 250 ms.
    static Owner cachedOwner{};static std::uint64_t cachedEpoch=0,cachedSpace=0;static std::int64_t retryAt=0;
    if(cachedOwner!=owner||cachedEpoch!=epoch||cachedSpace!=input.spaceGeneration){
        cachedOwner=owner;cachedEpoch=epoch;cachedSpace=input.spaceGeneration;retryAt=0;reloadAudioBinding.reset();}
    if(now>=retryAt){
        reloadAudioBinding.reset();retryAt=now+1000000000;
        const ReloadAudioMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
            [](void*,unsigned at,const char* name){return Type(at,name);}};
        const OwnedReloadAudioIdentity identity{AudioOwner(owner),epoch};OwnedReloadAudioResolution diagnostic;++reloadAudioProofAttempts;
        reloadAudioBinding=ResolveOwnedReloadAnimation(memory,identity,[&]()->std::optional<OwnedReloadAudioIdentity>{
            Owner current{};if(!Resolve(current)||!current.foot||current!=owner||epoch!=identity.equipmentGeneration)return {};
            return OwnedReloadAudioIdentity{AudioOwner(current),epoch};},&diagnostic);
        reloadAudioProofReads=diagnostic.reads;reloadAudioProofFailure=unsigned(diagnostic.failure);reloadAudioProofUnreadable=diagnostic.unreadable;
        if(reloadAudioBinding){retryAt=now+250000000;++reloadAudioProofAccepted;}
    }
    return reloadAudioBinding.has_value();
}
void PublishReloadAudioSuppression(const interaction::InputFrame& input,bool active)noexcept {
    if(!active||!reloadAudioBinding){reloadAudioSuppression.store({},std::memory_order_release);return;}
    const auto deadline=(std::min)(handSample.deadlineNs,reloadAudioReadyUntil);
    if(deadline<=AnimNowNs()){reloadAudioSuppression.store({},std::memory_order_release);return;}
    ++reloadAudioLaneSamples;
    try{reloadAudioSuppression.store(std::make_shared<const ReloadAudioSuppression>(ReloadAudioSuppression{
        *reloadAudioBinding,input.spaceGeneration,handSample.observedNs,deadline}),std::memory_order_release);
    }catch(...){reloadAudioSuppression.store({},std::memory_order_release);}
    // A previously queued private audition must not replay the whole clip.
    rackAudioAudition.Cancel();reloadSoundGate.Cancel();pumpSoundGate.Cancel();
}
void SendReloadAudio(ipc::ReloadAudioRole role,const interaction::InputFrame& input)noexcept {
    if(!reloadAudioWeapon||!reloadAudioOwnerSerial||!writeReloadAudio||reloadAudioEventSerial==UINT64_MAX)return;
    ipc::ReloadAudioEvent e;e.id=++reloadAudioEventSerial;e.inputSequence=input.generation;e.ownerRevision=reloadAudioOwnerSerial;
    e.space=input.spaceGeneration;e.observedNs=handSample.observedNs;e.deadlineNs=handSample.deadlineNs;e.role=role;
    const auto p=input.hands[1].grip.position;e.emitterX=p.x;e.emitterY=p.y;e.emitterZ=p.z;
    if(writeReloadAudio(e)){++reloadAudioSent;++reloadAudioRoleSent[unsigned(role)];}else ++reloadAudioDropped;
}
// Cancel every running or pending haptic pulse (weapon switch, owner loss/death, pause).
// No-op unless a pulse was sent in the last second, so the per-gather callers are cheap.
void StopHaptics()noexcept {
    if(!lastPulseTick.load(std::memory_order_relaxed)||GetTickCount64()-lastPulseTick.load(std::memory_order_relaxed)>1000)return;
    LARGE_INTEGER q{};if(!lastFreshGeneration||!lastFreshSpace||!QueryPerformanceCounter(&q))return;
    const auto nowNs=HandNanos(q.QuadPart);
    SendFeedback({1,lastFreshGeneration,lastFreshSpace,nowNs>1000000?nowNs-1000000:nowNs,nowNs+50000000,interaction::FeedbackKind::StopAll,1});
    lastPulseTick.store(0,std::memory_order_relaxed);
}
// Stick switch requests resolved at the start of the gather, before any hand,
// reload, magazine or sight consumer sees the flick (docs/WEAPON-SWITCH-20261009.md).
struct DeferredSwitch {StickSwitch request{};ULONGLONG since=0;PlayerInventoryAnchor actor{};};
std::optional<DeferredSwitch> deferredSwitch;
bool flickKeepsHands=false;std::uint32_t switchOutcomeSeen=0;
std::array<std::atomic<unsigned>,4> switchRequests{}; // flick next, flick previous, cycle forward, cycle backward
std::atomic<unsigned> switchReloadBlocked{0},switchReloadCancelled{0},switchDeferred{0},switchDeferredExpired{0},switchBodyBlocked{0},switchModeBlocked{0};
std::atomic<unsigned> switchFlickHandsKept{0},switchConflicts{0},switchFeedbackSent{0},switchFeedbackDropped{0},switchSelectedSeen{0},switchFailedSeen{0};
void SwitchFeedback(interaction::FeedbackKind kind,const interaction::InputFrame& input,std::int64_t deadlineQpc,std::int64_t nowQpc)noexcept {
    const auto observed=HandNanos(deadlineQpc-handFrequency/10),deadline=HandNanos(deadlineQpc),now=HandNanos(nowQpc);
    const interaction::FeedbackEvent event{1,input.generation,input.spaceGeneration,observed,(std::min)(deadline,observed+100000000),kind,1};
    const bool sent=interaction::ValidFeedback(event,now)&&SendFeedback(event);
    if(sent)++switchFeedbackSent;else ++switchFeedbackDropped;
    gunFvr.Haptic(GunEvents(),gunWeapon,GunFeedbackName(kind),1,sent);
}
enum class SwitchReloadHold {Free,Settling,Running};
// The physical authority's accounting rules: a Queued/Ready native reload has
// entered nothing and may be cancelled (its obligations settle at native idle,
// as for any cancellation); a Running one is never stowed mid-transaction.
SwitchReloadHold ReadSwitchReloadHold(const Owner& owner)noexcept {
    if(!physicalWeaponRuntime::Enabled())return SwitchReloadHold::Free;
    const auto a=physicalWeaponRuntime::ReadActionAdmission(owner.weapon,owner.equipment.data);
    if(!a.BlocksSelection())return SwitchReloadHold::Free;
    if(a.pendingCommand||(a.nativeBusy&&a.nativePhase!=physicalWeaponRuntime::NativeReloadPhase::Running))
        return SwitchReloadHold::Settling;
    return SwitchReloadHold::Running; // Unavailable/init also fail closed.
}
void CancelReloadForSwitch(const Owner& owner)noexcept {
    if(automaticReload.token){CancelAutomaticReload();++switchReloadCancelled;return;}
    if(!physicalWeaponRuntime::Enabled())return;
    const auto managed=physicalWeaponRuntime::Read(owner.weapon);if(!managed)return;
    const auto native=physicalWeaponRuntime::ReadNativeReload(managed->identity);
    if(native&&native->busy&&native->phase!=physicalWeaponRuntime::NativeReloadPhase::Running&&
       physicalWeaponRuntime::CancelNativeReload(managed->identity,native->token))++switchReloadCancelled;
}
void ResolveStickSwitch(interaction::ActionOutput& out,const Owner& owner,const interaction::InputFrame& input,std::int64_t deadlineQpc,std::int64_t nowQpc)noexcept {
    constexpr auto flickBits=interaction::NextWeapon|interaction::PreviousWeapon;
    constexpr auto cycleBits=interaction::Equipment|interaction::EquipmentBack;
    const auto edge=out.pressed&(flickBits|cycleBits);
    // Consumers never use the click bits; the native cache never receives them.
    out.held&=~cycleBits;out.pressed&=~cycleBits;out.released&=~cycleBits;
    if(!weaponSwitchHooked)return; // Legacy client-cache flick path, unchanged.
    const auto reject=[&]{SwitchFeedback(interaction::FeedbackKind::SwitchRejected,input,deadlineQpc,nowQpc);};
    // The server callback's verdict on the last latched request.
    const auto outcome=switchOutcome.load(std::memory_order_acquire);const auto outcomeSerial=std::uint32_t(outcome>>8);
    if(outcomeSerial&&outcomeSerial!=switchOutcomeSeen){
        switchOutcomeSeen=outcomeSerial;const auto result=SwitchOutcome(outcome&0xffu);
        if(result==SwitchOutcome::Selected){
            // The held item is changing: drop the support hand now rather than
            // waiting for the new native identity to reach the hand observer.
            ++switchSelectedSeen;ResetSupportGrip("weapon_switch");
            SwitchFeedback(interaction::FeedbackKind::SwitchSelected,input,deadlineQpc,nowQpc);
        }else if(result==SwitchOutcome::NoTarget||result==SwitchOutcome::Expired){++switchFailedSeen;reject();}
    }
    const PlayerInventoryAnchor actor{owner.player,owner.soldier,owner.weak,physicalActorGeneration};
    const auto latch=[&](StickSwitch request){
        if(LatchWeaponSwitch(request,actor)==SwitchLatch::Latched)return true;
        reject();return false;
    };
    if(deferredSwitch){
        if(deferredSwitch->actor!=actor||GetTickCount64()-deferredSwitch->since>1500){deferredSwitch.reset();++switchDeferredExpired;reject();}
        else if(ReadSwitchReloadHold(owner)==SwitchReloadHold::Free){const auto request=deferredSwitch->request;deferredSwitch.reset();latch(request);}
    }
    if(edge){
        const bool flick=(edge&flickBits)!=0;
        int index=-1;StickSwitch request{};
        if(edge==interaction::NextWeapon){index=0;request=StickSwitch::NextGun;}
        else if(edge==interaction::PreviousWeapon){index=1;request=StickSwitch::PreviousGun;}
        else if(edge==interaction::Equipment){index=2;request=StickSwitch::CycleForward;}
        else if(edge==interaction::EquipmentBack){index=3;request=StickSwitch::CycleBackward;}
        bool keeps=false;
        if(index<0)++switchConflicts; // Two different requests on one sample: neither.
        else{
            ++switchRequests[unsigned(index)];
            if(!physicalWeaponRuntime::Enabled()&&((magazinePhysical&&magazinePhysical->BlocksEquipment())||(physicalReload&&physicalReload->BlocksEquipment())||(magazineDetached&&magazineDetached->BlocksActions()))){++flickBlocked;reject();}
            else if((bodyHolster&&bodyHolster->BlocksActions())||(bodyInventory&&bodyInventory->Pending())){++switchBodyBlocked;reject();}
            else if(pendingMode){++switchModeBlocked;reject();}
            else switch(ReadSwitchReloadHold(owner)){
            case SwitchReloadHold::Running:++switchReloadBlocked;reject();break;
            case SwitchReloadHold::Settling:
                CancelReloadForSwitch(owner);deferredSwitch=DeferredSwitch{request,GetTickCount64(),actor};++switchDeferred;keeps=true;break;
            case SwitchReloadHold::Free:
                // A hold-B request that has not reached the authority owns nothing native.
                if(automaticReload.token&&!automaticReload.requested){CancelAutomaticReload();++switchReloadCancelled;}
                deferredSwitch.reset();keeps=latch(request);break;
            }
        }
        if(flick){flickKeepsHands=keeps;if(keeps)++switchFlickHandsKept;}
    }
    // Only a flick that actually requested a switch cancels the support grip,
    // hand claims, magazine and sight consumers (the October 8 behavior). An
    // unsuccessful flick is invisible to them for its whole deflection.
    if(!(out.held&flickBits))flickKeepsHands=false;
    if(!flickKeepsHands){out.held&=~flickBits;out.pressed&=~flickBits;}
}
// Test-only input log (FVR_BC2_TEST_INPUT_LOG=1): native-gathered and committed
// EiaSwitchPrimaryWeapon float (+0x24) and boolean words (+0x98/+0x9c), plus both
// inventory current-state candidates, on each change (weapon-switch investigation).
// Also retains HMD/body movement intent and aggregates unchanged commands' own
// update position/QPC interval. Collision motion is not sprint-gate attribution.
bool testInputLog=false;struct TestInputRow{
    std::uint64_t tick=0;std::uint32_t action=0,native[3]{},committed[3]{},state[2]{};
    std::array<float,2> desiredBodyAxes{},submittedNativeAxes{},movementYaw{};
    bool movementBasisValid=false,rightAimTracked=false,submittedAxesValid=false,committedButtonsValid=false;
    unsigned player=0,soldier=0,weak=0,weapon=0;
    std::uint64_t space=0,actorGeneration=0,equipmentGeneration=0;
    float worldUnitsPerMeter=0;
    // Observation interval is excluded from the unchanged-command key.
    std::uint64_t firstInput=0,lastInput=0,completedUpdates=0;
    std::int64_t beforeQpc=0,afterQpc=0;
    std::optional<math::Vec3> positionBefore,positionAfter;
    bool positionOwnerCurrent=false;
    bool afterUpdateSprintValid=false,afterUpdateSprint=false;
    bool SameCommand(const TestInputRow& r)const noexcept {
        return action==r.action&&std::equal(std::begin(native),std::end(native),std::begin(r.native))&&
            std::equal(std::begin(committed),std::end(committed),std::begin(r.committed))&&
            std::equal(std::begin(state),std::end(state),std::begin(r.state))&&
            desiredBodyAxes==r.desiredBodyAxes&&submittedNativeAxes==r.submittedNativeAxes&&movementYaw==r.movementYaw&&
            movementBasisValid==r.movementBasisValid&&rightAimTracked==r.rightAimTracked&&submittedAxesValid==r.submittedAxesValid&&
            committedButtonsValid==r.committedButtonsValid&&
            player==r.player&&soldier==r.soldier&&weak==r.weak&&weapon==r.weapon&&space==r.space&&
            actorGeneration==r.actorGeneration&&equipmentGeneration==r.equipmentGeneration&&worldUnitsPerMeter==r.worldUnitsPerMeter;
    }
};
std::array<TestInputRow,256> testInputRows{};unsigned testInputCount=0;TestInputRow testInputLast{};
void __fastcall GatherHook(void* self,void*,void* cache){
    Callback callback;
    if(aimAngleTraceRequested){AimAngleScopeRestore nested{true};gatherOriginal(self,cache);}
    else gatherOriginal(self,cache);
    if(aimAngleTraceRequested&&enabled.load(std::memory_order_acquire)&&scope&&!scope->used&&
       reinterpret_cast<unsigned>(self)==scope->owner.router&&reinterpret_cast<unsigned>(cache)==scope->owner.cache&&
       aimAngleScope&&aimAngleScope->owner==scope->owner)
        SampleAimAngles(AimAngleStage::AfterGatherOriginal);
    if(testHitchMs&&!testHitches&&testHitchOnStop&&reloadFlowRuntime::AbortArmedCount()){
        const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(!testHitchStopSeenNs)testHitchStopSeenNs=now;
        if(now-testHitchStopSeenNs>=std::int64_t(testHitchAfterMs)*1000000){++testHitches;testHitchBeginQpcNs=QpcNs();Sleep(testHitchMs);testHitchEndQpcNs=QpcNs();}}
    if(testServerLoaded>=0&&!testServerLoadedDone)if(const auto lock=GunNativeTracker().LastRoundLockNs()){
        const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(now-lock>=std::int64_t(testServerLoadedAfterMs)*1000000)if(const auto firing=reloadFlowRuntime::LocalServerFiring()){
            testServerLoadedDone=true;std::int32_t value=-1;
            if(Read(firing+0x7c,&value,4)&&value>=0&&value<=testServerLoaded){testServerLoadedBefore=value;testServerLoadedFiring=firing;
                *reinterpret_cast<volatile std::int32_t*>(std::uintptr_t(firing)+0x7c)=testServerLoaded;testServerLoadedNs=QpcNs();}}}
    if(testHitchMs&&!testHitches&&testHitchOnHold){GunCopyState server{};
        const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(!testHitchHoldSeenNs&&GunNativeTracker().Last(GunCopy::Server,server)&&server.current==11)testHitchHoldSeenNs=now;
        if(testHitchHoldSeenNs&&now-testHitchHoldSeenNs>=std::int64_t(testHitchAfterMs)*1000000){++testHitches;testHitchBeginQpcNs=QpcNs();Sleep(testHitchMs);testHitchEndQpcNs=QpcNs();}}
    if(testHitchMs&&!testHitches&&!testHitchOnStop&&!testHitchOnHold)if(const auto lock=GunNativeTracker().LastRoundLockNs()){
        const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(now-lock>=std::int64_t(testHitchAfterMs)*1000000){++testHitches;Sleep(testHitchMs);}}
    if(!enabled.load(std::memory_order_acquire)||!scope||scope->used)return;
    if(reinterpret_cast<unsigned>(self)!=scope->owner.router||reinterpret_cast<unsigned>(cache)!=scope->owner.cache){++mismatches;return;}
    static const bool nativeHandCapture=[]{char value[4]{};return GetEnvironmentVariableA("FVR_NATIVE_HAND_CAPTURE",value,sizeof(value))==1&&value[0]=='1';}();
    static const bool blockPhysicalInput=[]{char value[4]{};return GetEnvironmentVariableA("FVR_BC2_BLOCK_PHYSICAL_INPUT",value,sizeof(value))==1&&value[0]=='1';}();
    const bool blockNativeInput=nativeHandCapture||blockPhysicalInput;
    unsigned nativeCaptureAllowed=0;
    if(blockNativeInput){
        // Original Gather ran exactly once. This mode owns its local cache
        // even when synthetic input is stale/Busy/Closed; no physical fallback
        // on those paths. Preserve native eligibility for the pose pipeline.
        // Native Update consumes/submits it AFTER this hook returns. Other
        // native global input callbacks, frontend and OS routes are outside
        // this verified local-cache diagnostic; this is not total isolation.
        Owner current{};
        if(!Resolve(current)||current!=scope->owner||!Writable(current.cache,InputBytes)){scope->used=true;++mismatches;return;}
        nativeCaptureAllowed=U32(current.cache+0x98);
        SuppressNativePhysicalInput({reinterpret_cast<std::byte*>(current.cache),InputBytes},true);
    }
    struct CaptureInputScope {
        bool active,strong,syntheticCommitted=false;std::int64_t syntheticDeadline=0;Owner owner;
        void CommitSynthetic(std::int64_t deadline)noexcept{syntheticCommitted=true;syntheticDeadline=deadline;}
        ~CaptureInputScope(){if(active){Owner current{};if(Resolve(current)&&current==owner&&Writable(current.cache,InputBytes))
            {LARGE_INTEGER now{};const bool fresh=QueryPerformanceCounter(&now)&&now.QuadPart>0&&now.QuadPart<syntheticDeadline;
                CompleteNativePhysicalInput({reinterpret_cast<std::byte*>(current.cache),InputBytes},true,!strong&&syntheticCommitted,fresh);}}}
    } captureInputScope{blockNativeInput,nativeHandCapture,false,0,scope->owner};
    scope->used=true;const auto gatherTick=++gathers;
    // The schema's tick: one per GatherHook update, the same count the input tape uses.
    if(GunEvents().Enabled()){GunEvents().SetTick(gatherTick);if(gatherTick%30==0)GunNativeTracker().Sample(GunEvents(),gunFvr.MagPhase(),250000);}
    if((bodyHolster||magazineDetached)&&holsterNativeTick<UINT64_MAX)scope->nativeTick=++holsterNativeTick;
    std::optional<rigPublication::Tracking> holsterTracking;
    bool detachedUsed=false;std::optional<Bc2AmmoReserveLease> detachedReserve;
    interaction::InputFrame input{};
    std::int64_t deadline=0;const auto result=readInput?readInput(input,deadline):ipc::ChannelResult::Closed;
    if(result==ipc::ChannelResult::Ok){cachedInput=input;inputDeadline=deadline;}
    else if(result!=ipc::ChannelResult::Busy){cachedInput={};inputDeadline=0;}
    LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);
    const bool fresh=clock.QuadPart>0&&clock.QuadPart<inputDeadline;
    input=fresh?cachedInput:interaction::InputFrame{};if(!fresh)++stale;
    SuppressNativeHandCaptureControls(input,nativeHandCapture);
    auto recordedInput=input;
    struct InputRecordScope {
        interaction::InputFrame& frame;std::int64_t deadline,consumedQpc;
        ~InputRecordScope(){if(observeInput)observeInput(frame,deadline,consumedQpc);}
    } inputRecordScope{recordedInput,inputDeadline,clock.QuadPart};
    if(fresh){lastFreshTick=GetTickCount64();lastFreshSpace=input.spaceGeneration;lastFreshGeneration=input.generation;}
    const auto& owner=scope->owner;
    ContextInteractRecord interact;interact.input=input.generation;interact.space=input.spaceGeneration;
    interact.actorGeneration=rigEpoch;interact.equipGeneration=epoch;interact.observedNs=HandNanos(inputDeadline-handFrequency/10);
    interact.deadlineNs=HandNanos(inputDeadline);interact.nowNs=HandNanos(clock.QuadPart);
    interact.player=owner.player;interact.soldier=owner.soldier;interact.weak=owner.weak;interact.controlled=owner.controlled;
    interact.entry=owner.entry;interact.cache=owner.cache;interact.onFoot=owner.foot;interact.action=owner.foot?27u:16u;interact.actionMask=1u<<interact.action;
    interact.sourceFresh=fresh;interact.controllerHeld=(input.hands[0].held&interaction::Primary)!=0;
    struct InteractReportScope {
        ContextInteractRecord& record;const Owner& owner;bool readback;
        ~InteractReportScope(){
            if(record.committed&&readback){Owner current{};record.ownerCurrent=Resolve(current)&&current==owner;
                unsigned word=0;record.readback=record.ownerCurrent&&Read(owner.cache+0x98,&word,4);
                record.nativeHeld=record.readback&&record.actionMask&&(word&record.actionMask)==record.actionMask;}
            LARGE_INTEGER now{};if(QueryPerformanceCounter(&now))record.nowNs=HandNanos(now.QuadPart);
            contextInteractEvidence.Observe(record);
        }
    } interactScope{interact,owner,contextInteractEvidence.NeedsReadback(interact.controllerHeld)};
    gatherSightRequest=gatherSightAck=0;gatherSightCommit=false;
    struct OwnershipReportScope {~OwnershipReportScope(){RecordHandOwnership();}} ownershipReportScope;
    if(pendingMode){
        auto& request=modeRecords[*pendingMode];
        if(request.actor!=owner.soldier||request.weak!=owner.weak||request.owner!=rigEpoch||request.space!=input.spaceGeneration||!fresh||!input.focused||!input.headValid||!input.hands[0].gripTracked||(owner.weapon!=request.from&&owner.weapon!=request.target)||GetTickCount64()-request.ms>2000){
            request.cancelled=true;pendingMode.reset();
        }else if(owner.weapon==request.target){request.ackMs=GetTickCount64();if(request.gesture)sightAcknowledged=request.gesture;++modeAcknowledgements;pendingMode.reset();}
    }

    if(!input.hands[1].gripTracked||!input.hands[1].aimTracked){std::lock_guard lock(shotPoseMutex);shotHistory.Reset();}
    // FrameChannel owns freshness against QPC; XR times are a separate clock.
    // Duplicate samples preserve holds, never duplicate action/snap edges.
    // Native gather encodes allowed float actions in low bits of +98.
    // Require movement and fire eligibility; menus/cutscenes must keep control.
    const auto allowed=blockNativeInput?nativeCaptureAllowed:U32(owner.cache+0x98);
    const bool vehicleInteraction=!owner.foot&&Type(owner.controlled,"ClientVehicleEntity")&&(allowed&0x11u)==0x11u;
    const bool nativePlaying=owner.foot?(allowed&0x103u)==0x103u:vehicleInteraction;
    interact.playing=nativePlaying;
    if(!nativePlaying)++nativeInactive;
    if(!owner.foot){
        // Vehicle controls have their own neutral recovery, continuous axes and
        // ownership. Infantry snap/cycling/aim/roomscale never execute here.
        actions={};ClearReloadOwner();
        CancelSight();weaponModeHeld=true;ResetSupportGrip("not_on_foot");ResetHandOwnership("not_on_foot");PublishRecoilObservedTracking({});roomscale.Suspend();
        const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        const VehicleRouteBinding binding{unsigned(image+profile.gameplay.managerVtable),unsigned(image+profile.gameplay.inputRouterVtable),unsigned(image+profile.cacheVtable),true};
        const auto routes=ReadVehicleRoutes(memory,binding,{owner.manager,owner.player,owner.soldier,owner.weak,rigEpoch,epoch});
        if(routes.status!=VehicleRouteStatus::Okay){++vehicleRouteRejects;vehicleActions.Reset();PublishVehicleView({});return;}
        const auto& seat=routes.snapshot;const auto data=U32(owner.controlled+12),entryData=U32(owner.entry+12);
        if(seat.identity!=cachedVehicleIdentity||seat.fingerprint!=cachedVehicleFingerprint||data!=cachedVehicleData||entryData!=cachedEntryData){
            cachedVehicleProfile=ReadPblDriverProfile(memory,seat);cachedVehicleIdentity=seat.identity;cachedVehicleFingerprint=seat.fingerprint;cachedVehicleData=data;cachedEntryData=entryData;
        }
        vehicleSeatEntity.store(owner.controlled);vehicleSeatEntry.store(owner.entry);vehicleRouteFingerprint.store(seat.fingerprint);
        const interaction::VehicleTrackingOwner seatOwner{seat.identity.actor,rigEpoch,owner.controlled,owner.entry};
        const auto controls=vehicleActions.Update(input,{seatOwner,input.spaceGeneration},nativePlaying,fresh);
        interact.semanticHeld=controls.axes.exit;
        const bool viewValid=cachedVehicleProfile&&fresh&&nativePlaying&&interaction::ValidInput(input)&&input.focused&&input.headValid;
        interaction::VehicleAimOutput headAim;std::optional<BoatAimSnapshot> nativeBoatAim;
        if(boatHeadAim&&cachedVehicleProfile&&viewValid)nativeBoatAim=ReadPblDriverAim(memory,boatAimBinding,seat);
        {
            std::lock_guard lock(vehicleViewMutex);boatAimReady=false;
            if(nativeBoatAim){
                ++boatAimSamples;const interaction::VehicleAimObservation observation{seatOwner,input.spaceGeneration,nativeBoatAim->hull,nativeBoatAim->cameraWorld,nativeBoatAim->neutralCameraLocal,
                    HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),true};
                headAim=boatHeadPolicy.Update(observation,input,HandNanos(clock.QuadPart),PblDriverAimLimits());
                if(headAim.ready){boatAimReady=true;boatAimSeat=seat;boatAimReference=boatHeadPolicy.Reference();}
            }else if(boatHeadAim&&cachedVehicleProfile&&viewValid)++boatAimRejects;
        }
        PublishVehicleView({seatOwner,input.spaceGeneration,bool(cachedVehicleProfile),viewValid,
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline)});
        // The native component/config checks must not extend the original XR lease.
        QueryPerformanceCounter(&clock);
        Owner check{};if(!fresh||clock.QuadPart>=inputDeadline||!controls.active||!Resolve(check)||check!=owner||!Writable(owner.cache,InputBytes))return;
        if(!cachedVehicleProfile){
            ++vehicleUnsupported;
            // Preserve the previously supported exit, only when this actual
            // native router independently maps ChangeVehicle16 to concept36.
            const auto exit=std::find(seat.routes.begin(),seat.routes.begin()+seat.count,VehicleRoute{16,36});
            interaction::ActionOutput out{};out.active=true;out.owner=owner.soldier;if(controls.axes.exit)out.held|=interaction::Use;
            if(vehicleInteraction&&exit!=seat.routes.begin()+seat.count&&scope->patch.ApplyVehicleExit({reinterpret_cast<std::byte*>(owner.cache),InputBytes},out)){scope->patch.Commit();captureInputScope.CommitSynthetic(inputDeadline);interact.committed=true;if(controls.axes.exit)++vehicleInteractions;}
            return;
        }
        VehicleInputCommand command;command.identity=seat.identity;command.active=true;
        command.axes[unsigned(VehicleAxis::Throttle)]=controls.axes.throttle;
        command.axes[unsigned(VehicleAxis::Steer)]=controls.axes.steer;command.exitHeld=controls.axes.exit;
        auto inputProfile=*cachedVehicleProfile;
        if(headAim.ready&&nativeBoatAim){
            inputProfile.role=VehicleSeatRole::DriverGunner;
            inputProfile.axes[unsigned(VehicleAxis::LookYaw)]=static_cast<EntryAction>(6); // Verified EiaRoll, not hull-steering EiaYaw.
            inputProfile.axes[unsigned(VehicleAxis::LookPitch)]=EntryAction::Pitch;
            inputProfile.axes[unsigned(VehicleAxis::Fire)]=EntryAction::Fire;
            command.axes[unsigned(VehicleAxis::LookYaw)]=headAim.yaw;command.axes[unsigned(VehicleAxis::LookPitch)]=headAim.pitch;
            command.axes[unsigned(VehicleAxis::Fire)]=boatHeadFire&&controls.armedRight?controls.axes.fire:0;
        }
        const auto plan=BuildVehicleInputPlan(inputProfile,{seat.identity,seat.fingerprint,true,nativePlaying},command,{reinterpret_cast<const std::byte*>(owner.cache),InputBytes});
        VehicleInputOverride patch;
        if(!patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},plan)){++vehicleRouteRejects;return;}
        patch.Commit();captureInputScope.CommitSynthetic(inputDeadline);interact.committed=true;
        if(headAim.ready&&nativeBoatAim){
            ++boatAimCommits;if(command.axes[unsigned(VehicleAxis::Fire)]>0&&!(plan.deniedAxes&(1u<<unsigned(VehicleAxis::Fire))))++boatAimFireCommits;
            std::lock_guard lock(vehicleViewMutex);const auto now=GetTickCount64();
            if(boatAimRecordCount<boatAimRecords.size()&&(!boatAimRecordCount||now-boatAimRecordedAt>=100)){
                boatAimRecords[boatAimRecordCount++]={now,input.generation,headAim.yaw,headAim.pitch,headAim.yawError,headAim.pitchError,nativeBoatAim->jointYaw,nativeBoatAim->jointPitch,command.axes[unsigned(VehicleAxis::Fire)]};boatAimRecordedAt=now;
            }
        }
        ++vehicleDriveCommits;vehicleThrottle.store(controls.axes.throttle);vehicleSteer.store(controls.axes.steer);if(controls.axes.exit)++vehicleInteractions;
        // Capture actual native gather-buffer words after scoped commit, not
        // just the requested axes or final neutral sample in the stop report.
        VehicleGatherRecord record;record.entity=owner.controlled;record.entry=owner.entry;record.soldier=owner.soldier;record.weak=owner.weak;
        record.actorGeneration=rigEpoch;record.seatGeneration=epoch;record.space=input.spaceGeneration;record.inputSequence=input.generation;record.ms=GetTickCount64();record.permissions=allowed;
        bool readback=true;
        for(unsigned n=0;n<plan.count;++n){const auto& edit=plan.edits[n];if(edit.offset!=8&&edit.offset!=24)continue;
            const unsigned axis=edit.offset==8?0u:1u;record.mask|=1u<<axis;
            std::memcpy(&record.before[axis],&edit.before,4);std::memcpy(&record.written[axis],&edit.after,4);
            const bool read=Read(owner.cache+unsigned(edit.offset),&record.observed[axis],4);
            readback&=read&&!std::memcmp(&record.observed[axis],&edit.after,4);
        }
        record.verified=record.mask&&readback;
        if(!record.verified)++vehicleReadbackFailures;
        else {
            if((record.mask&1)&&std::abs(record.observed[0])>0){++vehicleThrottleNonzero;vehicleThrottleMaximum.store(std::max(vehicleThrottleMaximum.load(),std::abs(record.observed[0])));}
            if((record.mask&2)&&std::abs(record.observed[1])>0){++vehicleSteerNonzero;vehicleSteerMaximum.store(std::max(vehicleSteerMaximum.load(),std::abs(record.observed[1])));}
        }
        const bool changed=!havePreviousVehicleGather||record.entity!=previousVehicleGather.entity||record.entry!=previousVehicleGather.entry||record.actorGeneration!=previousVehicleGather.actorGeneration||record.seatGeneration!=previousVehicleGather.seatGeneration||record.space!=previousVehicleGather.space||record.mask!=previousVehicleGather.mask||record.written!=previousVehicleGather.written||record.verified!=previousVehicleGather.verified;
        const bool held=(record.written[0]!=0||record.written[1]!=0)&&record.ms-vehicleGatherRecordedAt>=100;
        const auto count=vehicleGatherCount.load();
        if(count<vehicleGatherRecords.size()&&(changed||held)){vehicleGatherRecords[count]=record;vehicleGatherRecordedAt=record.ms;vehicleGatherCount.store(count+1,std::memory_order_release);}
        previousVehicleGather=record;havePreviousVehicleGather=true;
        return;
    }
    vehicleActions.Reset();PublishVehicleView({});
    {std::lock_guard lock(vehicleViewMutex);boatAimReady=false;boatHeadPolicy.Reset();}
    auto actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
    if(magazinePhysicalFixture){ // proto/reload preparation burst: fire a full magazine down to partial
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const auto now=HandNanos(stamp.QuadPart);
        prepBurstFrame=false;
        if(magazinePhysicalFixture->PrepareWindow(now)){++prepBurst.window;
            const auto physical=physicalWeaponRuntime::Enabled()?physicalWeaponRuntime::Read(owner.weapon):std::nullopt;
            const auto lease=physicalWeaponRuntime::Enabled()?std::optional<Bc2AmmoReserveLease>{}:reloadFlowRuntime::ReadReserve();
            const int loaded=physical?int(physical->state.loaded):lease?lease->loaded:-1;
            const int capacity=physical?int(physical->state.config.feedCapacity):lease?lease->capacity:-1;
            const bool full=loaded>0&&capacity>0&&loaded>=capacity;
            if(loaded<0)++prepBurst.noLease;else if(!full)++prepBurst.notFull;
            const bool press=magazinePhysicalFixture->DryFireComplete()?magazinePhysicalFixture->PrepareTrigger(now,full):
                magazinePhysicalFixture->DryFireTrigger(now,loaded); // fire to empty, then dry pulls
            if(press){ // pulsed: press, neutral, press...
                // Trigger is analog: active plus value. `held` carries button bits only (ValidInput).
                auto& r=actionInput.hands[1];r.active|=interaction::Trigger;r.trigger=1;
                ++prepBurst.injected;prepBurstFrame=true;}}
        // Eject leg: the probe's single Reload press (right Primary/A maps to Reload).
        if(magazinePhysicalFixture->RackTrigger(now)){auto& r=actionInput.hands[1];r.active|=interaction::Trigger;r.trigger=1;}
        if(magazinePhysicalFixture->FlickPress(now)){auto& r=actionInput.hands[1];r.active|=interaction::Stick;r.stickY=-1;}
        if(magazinePhysicalFixture->EjectPress(now)){auto& r=actionInput.hands[1];r.active|=interaction::Primary;r.held|=interaction::Primary;}}
#ifndef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
    // SPAS probe fire leg: pressed on the action input, before actions are derived.
    if(physicalReloadFixture){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        if(physicalReloadFixture->FireTrigger(HandNanos(stamp.QuadPart))){auto& r=actionInput.hands[1];r.active|=interaction::Trigger;r.trigger=1;}}
#endif
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(armingEmptyFixture){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto reserve=reloadFlowRuntime::ReadReserve();const auto family=reloadFlowRuntime::ReadRequestProbeSnapshot();bool neutral=true;
        for(const auto& h:input.hands)neutral=neutral&&!h.held&&h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        const auto trigger=armingEmptyFixture->Fire(current,physicalReload.has_value()&&family&&family->identity.owner==current&&family->family==unsigned(ReloadNativeFamily::SpasTube),
            fresh&&nativePlaying&&interaction::ValidInput(input)&&input.focused&&input.headValid&&neutral,reserve,HandNanos(stamp.QuadPart));
        for(auto& h:actionInput.hands){h.held=0;h.squeeze=h.stickX=h.stickY=0;h.trigger=0;}actionInput.hands[1].trigger=trigger;
    }
#endif
    if(emptyFireFixture){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto profileId=reloadFlowRuntime::ReadMagazineRequestProfile(current);
        const auto fixtureProfile=profileId?FindMagazineEquipment(*profileId):nullptr;
        const bool supported=(fixtureProfile&&fixtureProfile->Ready())||owner.equipment.Asset()==SpasReloadAsset;
        bool neutral=true;for(const auto& h:input.hands)neutral=neutral&&!h.held&&h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;
        const bool safe=fresh&&nativePlaying&&interaction::ValidInput(input)&&input.focused&&input.headValid&&neutral;
        const auto reserve=reloadFlowRuntime::ReadReserve();
        // Reserve stamps its actual read. Validate after that read completes,
        // rather than treating every fresh observation as future evidence.
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        const float trigger=emptyFireFixture->Tick(current,supported,safe,reserve,HandNanos(stamp.QuadPart));
        for(auto& h:actionInput.hands){h.held=0;h.squeeze=h.stickX=h.stickY=0;h.trigger=0;}
        actionInput.hands[1].trigger=trigger;
    }
    // Arsenal reads the original LH trigger; reloadRequestMode masks only the
    // semantic-action copy above. Capture the stick through neutral on release
    // so the catalog flick cannot also turn, jump or select carried equipment.
    if(!arsenalInputOff){
        const bool equipmentFree=input.hands[0].trigger<ArsenalChord::TriggerThreshold||
            (!pendingMode&&(!bodyHolster||!bodyHolster->BlocksActions())&&(!bodyInventory||!bodyInventory->Pending())&&
             ReadSwitchReloadHold(owner)==SwitchReloadHold::Free);
        const auto chord=arsenalChord.Route(input,actionInput,fresh&&nativePlaying&&equipmentFree&&ArsenalEventsReady(),rigEpoch);
        if(chord.step!=ArsenalStep::None)SetEvent(arsenalEvents[chord.step==ArsenalStep::Next?0:1]);
    }
    // Only the explicit firing fixture changes trigger input. Evaluate the
    // ordinary action policy once, after that private input is prepared.
    const bool holsterFireFixture=bodyHolsterFixture&&bodyHolsterFixture->FireRequested();
    auto out=holsterFireFixture?interaction::ActionOutput{}:actions.Update(actionInput,{owner.soldier,epoch,nativePlaying,true},input.predictedNs,turnOptions);
    if(nativeHandCapture){out={};out.owner=owner.soldier;} // Pose publication continues; no action/roomscale admission.
    // A single stale or late gather leaves the action policy inactive for a
    // frame. Reload consumers cancel only on a sustained inactive policy
    // (more than 2 gathers); live run 073547 cancelled on a 1-frame blip.
    policyInactiveGathers=out.active?0u:policyInactiveGathers+1;const bool policyInactive=policyInactiveGathers>2;
    if(prepBurstFrame){if(!out.active)++prepBurst.policyInactive;if(out.held&interaction::Fire)++prepBurst.fireDerived;}
    if(!fresh&&nativePlaying&&lastFreshSpace&&GetTickCount64()-lastFreshTick<2000&&
       (physicalWeaponRuntime::Enabled()||(!(magazinePhysical&&magazinePhysical->BlocksEquipment())&&!(physicalReload&&physicalReload->BlocksEquipment())))&&reloadStateBinding){
        CancelSight();weaponModeHeld=true;ResetSupportGrip("stale_input");ResetHandOwnership("input_unavailable");
        {std::lock_guard lock(shotPoseMutex);shotHistory.Reset();}PublishRecoilObservedTracking({});roomscale.Suspend();
        Owner check{};if(!Resolve(check)||check!=owner){ClearReloadOwner();return;}
        const auto nowNs=HandNanos(clock.QuadPart);
        if(SelectedMeshesObservation::SourceObservationDue(reloadObservedSourceNs,nowNs)){reloadObservedSourceNs=nowNs;
            const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
            const auto observed=ReadReloadState(memory,*reloadStateBinding,unsigned(image),
                {owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,lastFreshSpace},lastFreshGeneration,nowNs);
            if(observed.snapshot){reloadFlowRuntime::PublishOwnerObserved(*observed.snapshot,observed.snapshot->observedNs+200000000);++staleOwnerHolds;}
            else ClearReloadOwner();}
        return;}
    if(!fresh||!nativePlaying||!interaction::ValidInput(input)||!input.focused||!input.headValid){ClearReloadOwner();CancelSight();weaponModeHeld=true;ResetSupportGrip(!fresh?"stale_input":!nativePlaying?"native_inactive":!interaction::ValidInput(input)?"invalid_input":!input.focused?"focus_loss":"head_untracked");ResetHandOwnership("input_unavailable"); {std::lock_guard lock(shotPoseMutex);shotHistory.Reset();} PublishRecoilObservedTracking({});roomscale.Suspend();return;}
    if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
    Owner check{};if(!Resolve(check)||check!=owner||!Writable(owner.cache,InputBytes)){ClearReloadOwner();CancelSight();ResetSupportGrip("owner_unavailable");ResetHandOwnership("owner_unavailable");PublishRecoilObservedTracking({});return;}
    const auto aimer=U32(owner.aim+8);
    float nativeYaw=0,simulationYaw=0;const bool aimValid=owner.aim&&U32(U32(owner.aim)+0x28)==0&&Type(U32(aimer),"HaloAimerData")&&
        Read(aimer+12,&nativeYaw,4)&&std::isfinite(nativeYaw)&&std::abs(nativeYaw)<100&&
        Read(owner.aim+12,&simulationYaw,4)&&std::isfinite(simulationYaw)&&std::abs(simulationYaw)<100&&
        Writable(aimer+12,8)&&Writable(owner.aim+12,8);
    if(handPoses&&!aimValid)PublishRecoilObservedTracking({});
    // Stick weapon selection is decided here, once, before any consumer reads
    // the flick: failed flicks must not cancel support/hand/reload state.
    ResolveStickSwitch(out,owner,input,inputDeadline,clock.QuadPart);

    // On-demand bounded observer. No native calls/writes and no reload policy
    // authority: both native branches remain visible with their actual states.
    const auto reloadSourceNowNs=HandNanos(clock.QuadPart);
    if(reloadStateBinding&&(NativeActionCaptureRequested()||reloadRequestMode||!reloadObservedStart||GetTickCount64()-reloadObservedStart<20000)&&
        SelectedMeshesObservation::SourceObservationDue(reloadObservedSourceNs,reloadSourceNowNs)){
        reloadObservedAt=GetTickCount64();if(!reloadObservedStart)reloadObservedStart=reloadObservedAt;
        reloadObservedSourceNs=reloadSourceNowNs;
        const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        const auto observed=ReadReloadState(memory,*reloadStateBinding,unsigned(image),
            {owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration},input.generation,reloadSourceNowNs);
        ++reloadStatuses[std::size_t(observed.status)];
        if(observed.snapshot){
            const auto publication=reloadFlowRuntime::PublishOwnerObserved(*observed.snapshot,observed.snapshot->observedNs+200000000);
            ++reloadPublicationStatuses[unsigned(publication.status)];++reloadPublicationReasons[unsigned(publication.reason)];
            // Deferred serialization supplies no new native lease. Do not turn it
            // into ClearOwner; original deadlines and native structural reads remain gates.
            LARGE_INTEGER selectedNow{};if(QueryPerformanceCounter(&selectedNow))
                selectedMeshes.Observe(*observed.snapshot,HandNanos(selectedNow.QuadPart));
            if(reloadSnapshotCount<reloadSnapshots.size())reloadSnapshots[reloadSnapshotCount++]=*observed.snapshot;
            if(NativeActionCaptureRequested())nativeActionFiring=std::make_shared<const ReloadStateSnapshot>(*observed.snapshot);
        }else ClearReloadOwner();
    }
    const auto weaponAsset=owner.equipment.asset;const auto asset=owner.equipment.Asset();
    {static unsigned hapticWeapon=0;if(hapticWeapon&&hapticWeapon!=owner.weapon)StopHaptics();hapticWeapon=owner.weapon;}
    if(GunEvents().Enabled()){const auto current=MakeGunWeapon(asset,owner.weapon);
        if(current!=gunWeapon){PushWeaponSwitch(GunEvents(),gunWeapon,current);gunWeapon=current;}}
    const auto itemProfileCandidate=FindWeaponProfile(asset);
    // 40mmgl is a generic name reused by other rifles. The measured profile is
    // only the exact shared scoped-XM8 mesh/configuration/persistence family.
    static unsigned familySoldier=0,familyWeak=0,familyWeapon=0,familyData=0;static WeaponEquipmentIdentity familyEquipment{};static std::uint64_t familyEpoch=0;
    static std::optional<WeaponModeCommand> familyBinding;static ULONGLONG familyChecked=0;
    const auto nowFamily=GetTickCount64();const auto data=U32(owner.weapon+4);
    if(familySoldier!=owner.soldier||familyWeak!=owner.weak||familyWeapon!=owner.weapon||familyData!=data||familyEquipment!=owner.equipment||familyEpoch!=rigEpoch||nowFamily-familyChecked>=500){
        familySoldier=owner.soldier;familyWeak=owner.weak;familyWeapon=owner.weapon;familyData=data;familyEpoch=rigEpoch;familyChecked=nowFamily;familyEquipment=owner.equipment;
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},[](void*,unsigned at,const char* expected){return Type(at,expected);}};
        familyBinding=ResolveWeaponMode(memory,owner.soldier,owner.weapon);
    }
    const ReloadStateOwner visualOwner{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
    LARGE_INTEGER visualStamp{};QueryPerformanceCounter(&visualStamp);const auto visualNow=HandNanos(visualStamp.QuadPart);
    const auto visualMeshes=asset=="40mmgl"?selectedMeshes.Read(visualOwner,visualNow):nullptr;
    const WeaponModeMemory visualMemory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},
        [](void*,unsigned at,const char* expected){return Type(at,expected);}};
    const auto visualFamily=visualMeshes?ResolveWeaponVisualFamily(visualMemory,*visualMeshes,visualOwner,visualNow):std::nullopt;
    const auto itemProfile=(asset!="40mmgl"||visualFamily)?itemProfileCandidate:nullptr;
    {   // Launcher visual admission evidence: each decision change (rate-limited
        // to 10/s unless the carried item changes) plus per-frame counters.
        const bool launcher=asset=="40mmgl";auto& evidence=WeaponEffectEvidence();
        static unsigned flipWeapon=0;static bool flipAdmitted=false;
        if(launcher){
            (itemProfile?evidence.Count().launcherAdmittedFrames:evidence.Count().launcherRejectedFrames).fetch_add(1,std::memory_order_relaxed);
            if(owner.weapon==flipWeapon&&bool(itemProfile)!=flipAdmitted)evidence.Count().launcherAdmissionChanges.fetch_add(1,std::memory_order_relaxed);
            flipWeapon=owner.weapon;flipAdmitted=bool(itemProfile);
        }else flipWeapon=0;
        namespace f=weapon_effect_flag;
        const std::uint32_t flags=(itemProfileCandidate?f::CandidateProfile:0u)|(familyBinding?f::NativeModeMap:0u)|(visualMeshes?f::VisualMeshes:0u)|
            (visualFamily?f::VisualFamily:0u)|(itemProfile?f::Admitted:0u)|(visualFamily&&visualFamily->selectedSecondary?f::SelectedSecondary:0u);
        static unsigned admissionWeapon=0;static std::uint32_t admissionFlags=~0u;static ULONGLONG admissionMs=0;
        const auto nowAdmission=GetTickCount64();
        if(owner.weapon!=admissionWeapon||(flags!=admissionFlags&&nowAdmission-admissionMs>=100)){
            admissionWeapon=owner.weapon;admissionFlags=flags;admissionMs=nowAdmission;
            WeaponEffectRecord row;row.kind=WeaponEffectRecordKind::Admission;row.tick=gatherTick;row.qpcNs=visualNow;row.weapon=owner.weapon;
            row.flags=flags;row.generation=rigEpoch;SetRecordAsset(row,asset);evidence.Push(row);
        }
    }
    const auto coreProfile=itemProfile?&itemProfile->core:nullptr;
    if(magazineDetachedFixture&&magazineDetached){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto contact=rigPublication::ReadMagazineContact(current);
        const auto reserve=reloadFlowRuntime::ReadReserve();
        // These reads stamp evidence after Gather began. Compare against a
        // processing clock sampled after them, retaining the input deadline.
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        magazineDetachedFixture->Prepare(input,current,asset,contact,reserve,
            magazineDetached->Phase(),HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
    if(magazinePhysicalFixture&&magazinePhysical){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto now=HandNanos(clock.QuadPart);
        magazinePhysicalFixture->Prepare(input,current,asset,rigPublication::ReadMagazineContact(current),magazinePhysical->ProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(armingEmptyFixture&&physicalReload){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto now=HandNanos(clock.QuadPart);
        armingEmptyFixture->Prepare(input,current,asset,rigPublication::ReadReloadContact(current),physicalReload->ProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
#endif
    if(physicalReloadFixture&&physicalReload){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto now=HandNanos(clock.QuadPart);
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
        {const auto state=physicalReload->ProbeState(now);
         physicalReloadFixture->Entry(reloadFlowRuntime::ReadPreholdEntry(state.identity,state.cycle,now));}
#endif
        physicalReloadFixture->Prepare(input,current,asset,rigPublication::ReadReloadContact(current),TubeProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
    if(bodyHolsterFixture){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto diagnosticAmmo=bodyHolsterFixture->FireRequested()?reloadFlowRuntime::ReadDiagnosticFireReserve():std::optional<Bc2AmmoReserveLease>{};
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);const auto ns=HandNanos(now.QuadPart);
        bodyHolsterFixture->Prepare(input,current,asset,ReadBodyHolsterProbe(ns),HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),ns,diagnosticAmmo);
        if(bodyHolsterFixture->Failed())InvalidateBodyHolster(false,BodyHolsterLifecycleReason::DiagnosticFailed);
        if(holsterFireFixture){actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
            out=actions.Update(actionInput,{owner.soldier,epoch,nativePlaying&&!bodyHolsterFixture->Failed(),true},input.predictedNs,turnOptions);
            if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
            if(bodyHolsterFixture->NeedsNeutralFire()){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}}
    }
    // The input reader runs before fixtures. Record their resolved private
    // poses and action fields, so a replay does not silently become neutral.
    if(observeInput){
        recordedInput=input;
        for(unsigned h=0;h<2;++h){
            recordedInput.hands[h].trigger=actionInput.hands[h].trigger;
            recordedInput.hands[h].held=actionInput.hands[h].held;
            recordedInput.hands[h].stickX=actionInput.hands[h].stickX;
            recordedInput.hands[h].stickY=actionInput.hands[h].stickY;
            recordedInput.hands[h].active|=actionInput.hands[h].active;
        }
    }
    if(twoHandGrip){
        ObserveHandOwnership(owner,input,clock.QuadPart,aimValid,
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon))!=0,familyBinding);
        const auto left=handOwnership.Current(interaction::InteractionHand::Left);
        if(sightPhase!=interaction::SightFlipPhase::Idle&&(!left||left->token.kind!=interaction::HandClaimKind::Sight)&&
            !(sightLeaseContinuation&&sightPhase==interaction::SightFlipPhase::AwaitingAcknowledgement&&sightAcknowledged))CancelSight();
    }
    bool reloadEventAudio=PublishReloadAudioOwner(owner,input,asset);
    if(reloadRequestFixture){
        // FrameChannel publishes deadline = source QPC + frequency/10. Preserve
        // that exact source epoch across duplicate/Busy input reads.
        reloadRequestFixture->Tick(input,handSample.owner,HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),HandNanos(clock.QuadPart));
    }
    if(magazineReloadFixture)magazineReloadFixture->Tick(input,handSample.owner,HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),HandNanos(clock.QuadPart));
    if(weaponVisibilityProbe){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        // Count/mesh observation above may have completed after Gather began.
        // Refresh processing time only; original IPC evidence is immutable.
        auto processing=handSample;LARGE_INTEGER visibilityNow{};
        if(QueryPerformanceCounter(&visibilityNow))processing.nowNs=HandNanos(visibilityNow.QuadPart);
        auto sample=weaponVisibilityProbe->Tick(current,processing,selectedMeshes.Read(current,processing.nowNs),asset,
            rigPublication::ReadWeaponVisibilityReceipt(current,weaponVisibilityProbe->RequestId(),processing.nowNs));
        rigPublication::PublishWeaponVisibility(sample.intent);
        try{weaponVisibilitySample.store(std::make_shared<const WeaponVisibilityProbeSample>(std::move(sample)),std::memory_order_release);}
        catch(...){CancelWeaponVisibilityProbe();}
    }
    PhysicalReloadResult physicalResult;MagazinePhysicalResult magazineResult;
    Xm8BoltCycleResult xm8Bolt{}; // XM8 charging-handle cycle (presentation + gating), ticked with the magazine lane.
    std::optional<MagazineFamilyEvidence> physicalReplacementFamily;
    const ReloadStateOwner magazineOwner{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
    const auto magazineProfileId=magazinePhysical?reloadFlowRuntime::ReadMagazineRequestProfile(magazineOwner):std::nullopt;
    // Several native firing paths share one asset name. The current coherent
    // owner/configuration lease selects the exact registered descriptor.
    const auto magazineProfile=magazineProfileId?FindMagazineEquipment(*magazineProfileId):nullptr;
    // Per-weapon support, reported by asset: an unsupported weapon keeps the
    // stock reload with its reason (never a session failure).
    const auto magazineSupport=magazineProfile?MagazineSupport{
        !magazineProfile->native->Reviewed()?MagazineSupportStatus::NativeNotReviewed:
        !magazineProfile->geometry?MagazineSupportStatus::NoGeometry:
        magazineProfile->Ready()?MagazineSupportStatus::Supported:MagazineSupportStatus::NotReady,magazineProfile}:MagazineSupportFor(asset);
    if(magazinePhysical&&!asset.empty()&&(asset!=std::string_view(magazineSupportAsset.data())||magazineSupportStatus!=magazineSupport.status)){
        magazineSupportStatus=magazineSupport.status;
        std::fill(magazineSupportAsset.begin(),magazineSupportAsset.end(),'\0');
        std::copy_n(asset.begin(),std::min(asset.size(),magazineSupportAsset.size()-1),magazineSupportAsset.begin());++magazineSupportChanges;}
    if(!physicalWeaponRuntime::Enabled()&&magazinePhysical&&!(physicalReload&&physicalReload->BlocksEquipment())&&!magazinePhysical->BlocksEquipment()&&!(magazineDetached&&magazineDetached->BlocksActions())){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        if(magazineProfile&&magazineProfile->Ready())reloadFlowRuntime::SelectMagazineRequestProfile(magazineProfile->nativeId,current);
        else if(asset==SpasReloadAsset)reloadFlowRuntime::SelectRequestFamily(ReloadNativeFamily::SpasTube,current);
    }
    const auto actionAdmission=physicalWeaponRuntime::ReadActionAdmission(owner.weapon,owner.equipment.data);
    // Enabled consumers reserve their exact profiles before the first ledger
    // initialization. A disabled consumer does not claim a merely known asset.
    const bool physicalLaneExpected=(magazinePhysical&&magazineProfile&&magazineProfile->Ready())||
        (physicalReload&&asset==SpasReloadAsset);
    BodyDrawResult bodyDraw;std::optional<BodyDrawSample> holsterBodySample;
    if(bodyInventory){
        BodyDrawSample sample;sample.owner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        sample.input=input;sample.hand=handSample;sample.gun=handItem;sample.verifiedFamily=familyBinding;
        // Interaction eligibility cannot retire physical inventory identity.
        // The body reader independently proves current native metadata while
        // these actions suspend gestures, presentation and pending requests.
        sample.interaction=(!aimValid||!out.active||sightPhase!=interaction::SightFlipPhase::Idle||
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon|interaction::Fire)))?
            BodyInteractionState::Suspended:BodyInteractionState::Available;
        sample.reloadBusy=physicalWeaponRuntime::Enabled()?
            (actionAdmission.BlocksSelection()||automaticReload.token!=0):
            ((physicalReload&&physicalReload->BlocksEquipment())||(magazinePhysical&&magazinePhysical->BlocksEquipment())||(magazineDetached&&magazineDetached->BlocksActions()));
        const auto pose=bodyHolster?rigPublication::ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon):rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
        if(pose&&pose->ownerGeneration==rigEpoch&&pose->space==input.spaceGeneration){
            if(const auto original=FindHandEvidence(pose->generation,owner.weapon))
                sample.visible=BodyVisibleRig{sample.owner,pose->generation,original->sample.observedNs,
                    std::min(original->sample.deadlineNs,HandNanos(pose->deadline))};
        }
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},
            [](void*,unsigned at,const char* expected){return Type(at,expected);}};
        if(bodyHolster){holsterBodySample=sample;bodyDraw.pending=bodyHolster->BlocksActions()||bodyInventory->Pending();bodyDraw.blockFire=bodyDraw.pending;
            if(bodyDraw.pending)ResetSupportGrip("body_holster");}
        else bodyDraw=bodyInventory->Tick(memory,sample,handOwnership,handIntent);
        if(bodyDraw.blockFire){out.held&=~(interaction::Fire|interaction::AlternateFire|interaction::Reload);out.pressed&=~(interaction::Fire|interaction::AlternateFire|interaction::Reload);}
    }
    bool automaticReloadBusy=false;
    if(physicalWeaponRuntime::Enabled()){
        const auto lane=!out.active||!aimValid||!handSample.focused?ReloadInputLane::Unavailable:
            actionAdmission.weapon==physicalWeaponRuntime::WeaponAdmission::Managed?
            ReloadInputLane::Physical:
            actionAdmission.weapon==physicalWeaponRuntime::WeaponAdmission::Unmanaged&&
                actionAdmission.nativeReloadInput&&!physicalLaneExpected?ReloadInputLane::Native:ReloadInputLane::Unavailable;
        if(reloadInputRouting.Update(handSample.owner,actionAdmission.incarnation,lane,(out.held&interaction::Reload)!=0))
            CancelAutomaticReload();
        const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
        const auto managed=physicalWeaponRuntime::Read(owner.weapon);
        const bool eligible=out.active&&aimValid&&!bodyDraw.pending&&managed&&!managed->suspended&&gun&&
            gun->token.kind==interaction::HandClaimKind::GunHold&&gun->token.owner==handSample.owner&&gun->token.item==handItem&&
            gun->inputSequence==handSample.sequence&&gun->deadlineNs>handSample.nowNs&&handSample.focused&&handSample.tracked[1];
        if(automaticReload.token&&(!eligible||automaticReload.owner!=handSample.owner||!managed||managed->identity!=automaticReload.weapon))CancelAutomaticReload();
        const bool nativeEligible=reloadInputRouting.NativeAllowed()&&!bodyDraw.pending&&gun&&
            gun->token.kind==interaction::HandClaimKind::GunHold&&gun->token.owner==handSample.owner&&gun->token.item==handItem&&
            gun->inputSequence==handSample.sequence&&gun->deadlineNs>handSample.nowNs&&handSample.tracked[1];
        const auto button=reloadButtonGesture.Update(handSample.owner,input.generation,input.predictedNs,eligible||nativeEligible,(out.held&interaction::Reload)!=0);
        auto dispatch=DispatchReloadButton(lane,button);
        // Physical guns keep the 600 ms hold (automatic reload). Unrigged guns
        // receive one ordinary native input pulse on the press edge, preserving
        // their own reload transfer and authored animation; a held button cannot
        // repeat it. The env kill switch restores the old native-lane hold.
        if(!NativeReloadHoldRestored())dispatch.nativePulse=reloadInputRouting.NativePressEdge();
        out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;out.released&=~interaction::Reload;
        if(dispatch.nativePulse&&nativeEligible){out.held|=interaction::Reload;out.pressed|=interaction::Reload;++automaticReloadHolds;}
        if(dispatch.physicalShort&&!automaticReload.token){out.held|=interaction::Reload;out.pressed|=interaction::Reload;++shortReloadReleases;}
        if(dispatch.physicalAutomatic&&eligible&&!automaticReload.token&&automaticReloadSerial!=UINT64_MAX){
            automaticReload={managed->identity,handSample.owner,++automaticReloadSerial,handSample.nowNs,false};++automaticReloadHolds;
        }
        const auto native=managed?physicalWeaponRuntime::ReadNativeReload(managed->identity):std::nullopt;
        automaticReloadBusy=automaticReload.token||(native&&native->busy);
        if(automaticReloadBusy){
            CancelPhysicalReload();out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;
        }
    }
    const bool reloadPressedRaw=(out.pressed&interaction::Reload)!=0;
    reloadEventAudio=reloadEventAudio&&!automaticReloadBusy&&out.active&&aimValid&&
        ((asset==Xm8MagazineAsset&&magazinePhysical&&magazineProfile&&magazineProfile->Ready())||
         (asset==SpasReloadAsset&&physicalReload&&pumpEnabled));
    PublishReloadAudioSuppression(input,reloadEventAudio);
    if(magazinePhysical){
        if(!physicalWeaponRuntime::Enabled())reloadFlowRuntime::PumpReloadMailbox();
        MagazinePhysicalSample sample;
        sample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        sample.input=handSample;sample.weapon=handItem;sample.geometrySequence=input.generation;sample.trackingEpoch=input.spaceGeneration;
        sample.actionFlagsKnown=true;sample.actionHeld=out.held;sample.actionPressed=out.pressed;
        const WeaponModeMemory familyMemory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},
            [](void*,unsigned at,const char* expected){return Type(at,expected);}};
        if(magazineProfile&&magazineProfile->Ready())
            sample.family=ResolveMagazineFamily(familyMemory,sample.nativeOwner,handSample,handItem,*magazineProfile).value_or(MagazineFamilyEvidence{});
        // Exact current native-to-physical mapping remains available even when
        // an old SPAS cycle temporarily blocks native reload-family selection.
        if(sample.family.verified)physicalReplacementFamily=sample.family;
        sample.gripPressed=interaction::ReloadGripActive(input.hands[0].squeeze,handSample,handOwnership.Current(interaction::InteractionHand::Left));
        sample.ejectPressed=(out.pressed&interaction::Reload)!=0;
        sample.cancel=automaticReloadBusy||bodyDraw.pending||!aimValid||policyInactive||(magazinePhysicalFixture&&magazinePhysicalFixture->CancelConsumer())||
            TubeBlocksEquipment();
        if(sample.cancel){++magCancelTerms[0];if(bodyDraw.pending)++magCancelTerms[1];if(!aimValid)++magCancelTerms[2];if(policyInactive)++magCancelTerms[3];
            if(magazinePhysicalFixture&&magazinePhysicalFixture->CancelConsumer())++magCancelTerms[4];if(physicalReload&&physicalReload->BlocksEquipment())++magCancelTerms[5];}
        sample.asset=asset;sample.meshes=selectedMeshes.Read(sample.nativeOwner,handSample.nowNs);
        sample.raw=rigPublication::ReadMagazineContact(sample.nativeOwner);
        if(const auto original=FindHandEvidence(sample.raw.inputEvidence.sequence,owner.weapon))sample.originalHandEvidence=original->sample;
        const auto pouch=bodyInventory?interaction::BodyAnchorHandPose(input,interaction::InteractionHand::Left):PhysicalReloadPouchPose(input);
        if(pouch)sample.bodyFromHand=*pouch;else sample.cancel=true;
        if(magazineDetached&&!physicalWeaponRuntime::Enabled()){detachedReserve=reloadFlowRuntime::ReadReserve();
            detachedUsed=magazineDetached->Routes(detachedReserve,magazinePhysical->BlocksEquipment());}
        if(detachedUsed){if(magazineDetachedFixture&&magazineDetachedFixture->CancelConsumer())sample.cancel=true;
            // Only processing time advances after the fresh native count read;
            // never renew the controller packet or renderer contact lifetime.
            LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);sample.input.nowNs=HandNanos(processed.QuadPart);
            magazineResult=magazineDetached->Prepare(sample,detachedReserve,scope->nativeTick,owner.cache,handOwnership);}
        else magazineResult=magazinePhysical->Tick(sample,handOwnership,handIntent);
        if(reloadEventAudio&&sample.asset==Xm8MagazineAsset){
            if(magazineResult.physicalUnseat)SendReloadAudio(ipc::ReloadAudioRole::MagazineOut,input);
            if(magazineResult.physicalSeat)SendReloadAudio(ipc::ReloadAudioRole::MagazineIn,input);
            if(magazineResult.nativeRackRearStop)SendReloadAudio(ipc::ReloadAudioRole::HandleBack,input);
            if(magazineResult.nativeRackForwardStarted||(magazineResult.racked&&magazineResult.boltReleased))
                SendReloadAudio(ipc::ReloadAudioRole::HandleForward,input);
        }
        if(reloadSoundEnabled){static unsigned soundStarted=0;static auto soundPhase=interaction::DetachableMagazinePhase::Attached;
            const auto now=AnimNowNs();const auto st=magazinePhysical->ProbeState(now);reloadSoundGate.Weapon(owner.weapon,owner.soldier);
            if(st.started!=soundStarted){soundStarted=st.started;reloadSoundGate.Begin(now);}
            // The pull (RemovedHeld), or a button eject straight to an empty well.
            if(st.phase!=soundPhase){if(st.phase==interaction::DetachableMagazinePhase::RemovedHeld||
                (st.phase==interaction::DetachableMagazinePhase::WellEmpty&&soundPhase!=interaction::DetachableMagazinePhase::RemovedHeld))if(!reloadEventAudio)reloadSoundGate.Trigger(now);
                if(st.phase==interaction::DetachableMagazinePhase::Cancelled){reloadSoundGate.Cancel();rackAudioAudition.Cancel();}soundPhase=st.phase;}
            if(!reloadEventAudio&&magazineResult.racked&&!magazineResult.boltReleased&&RackAudioProfile()){
                ++rackAudioAttempts;const auto proof=ReadRackAudioProof(owner);Owner after{};
                if(!proof){++rackAudioRejected;++rackRejectProof;}
                else if(!Resolve(after)){++rackAudioRejected;++rackRejectResolve;}
                else if(owner!=after){++rackAudioRejected;++rackRejectChanged;}
                else if(!rackAudioAudition.Request(true,true,false,AudioOwner(owner),*proof,
                    sample.input.sequence,AnimNowNs(),std::min<std::int64_t>(sample.input.deadlineNs,now+250000000))){++rackAudioRejected;++rackRejectRequest;}}}
        // F5 reload tips: which step is pending; each part glows after 0.75 s.
        {const auto st=magazinePhysical->ProbeState(sample.input.nowNs);using Phase=interaction::DetachableMagazinePhase;
            const bool supported=magazineProfile&&magazineProfile->Ready();const auto& ch=magazineResult.chamber;
            interaction::ReloadHintInput hint;hint.nowNs=sample.input.nowNs;
            hint.pullMagazine=supported&&st.phase==Phase::Attached&&!st.active&&ch.known&&ch.loaded==0&&!st.ownsHand;
            hint.takeFromPouch=supported&&st.phase==Phase::WellEmpty&&!st.ownsHand;
            // Seat tip only while the held magazine is outside the well; it
            // clears as the guided insertion starts (0dbdcf1 reference run).
            hint.seatMagazine=supported&&st.phase==Phase::ReplacementHeld;
            hint.rackHandle=magazineResult.rackTravel>=0&&ch.needsRack;
            // SPAS: a pump owed (or about to be) that the hand is not working yet.
            hint.workPump=pumpVisual&&pumpVisual->fraction<=0;
            if(physicalWeaponRuntime::Enabled()){
                const auto owned=physicalWeaponRuntime::Read(owner.weapon);
                const auto ready=owned?owned->readiness:interaction::WeaponReadiness{};
                using Cue=interaction::WeaponReadyCue;
                if(ready.cue==Cue::Unavailable||ready.projectionPending)hint={};
                else {
                    hint.rackHandle=magazineResult.rackTravel>=0&&ready.cue==Cue::Rack;
                    hint.workPump=ready.cue==Cue::Pump&&pumpVisual&&pumpVisual->fraction<=0;
                }
                hint.nowNs=sample.input.nowNs;
            }
            if(!reloadGuidanceEnabled)hint={};
            hintOutput=reloadHints.Update(hint);
            // Tint plumbing: publish the per-part alphas, with the magazine part's
            // geometry key and seated world pose (native units) for a tinted
            // host instance once the compositor takes a per-instance RGBA.
            {auto snapshot=std::make_shared<ReloadHintSnapshot>();snapshot->alpha=hintOutput.alpha;snapshot->observedNs=sample.input.nowNs;
                snapshot->space=handSample.owner.space;snapshot->owner=handSample.owner;snapshot->inputSequence=sample.input.sequence;
                snapshot->deadlineNs=std::min(sample.input.deadlineNs,sample.input.nowNs+100000000);
                const float units=input.worldUnitsPerMeter;
                const auto glowPose=rigPublication::ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon);
                if(supported&&magazineProfile->geometry&&glowPose&&
                   glowPose->ownerGeneration==handSample.owner.actorGeneration&&glowPose->space==handSample.owner.space&&
                   glowPose->generation<=sample.input.sequence&&
                   glowPose->bodyRigFingerprint==magazineProfile->geometry->rigFingerprint&&
                   std::isfinite(units)&&units>0){const auto& g=*magazineProfile->geometry;
                    snapshot->magazineGeometry=MakeBodyAmmoGeometryKey(g.asset,g.mesh,g.bones.magazine,g.rigFingerprint);
                    auto world=interaction::Multiply(interaction::ScaleTranslation(g.attachedItem,units),glowPose->trackedWeapon);
                    // Convert the prop's metre-space basis as well as its position.
                    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)world.values[r][c]*=units;
                    snapshot->deadlineNs=std::min(snapshot->deadlineNs,HandNanos(glowPose->deadline));
                    snapshot->magazineWorld=world;}
                reloadHintPublication.store(std::move(snapshot),std::memory_order_release);}
            for(unsigned n=0;n<unsigned(interaction::ReloadHintPart::Count);++n)if(hintOutput.changed&(1u<<n)){
                const bool on=hintOutput.lit&(1u<<n);if(on)++hintLit[n];
                if(GunEvents().Enabled()){static constexpr const char* names[]{"magazine","pouch","magwell","handle","pump"};
                    GunEvent e;e.phase=GunEventPhase::Hint;e.asset=gunWeapon.asset;e.weaponId=gunWeapon.id;SetGunText(e.detail,names[n]);e.v[0]=on;GunEvents().Push(e);}}}
        if(!detachedUsed)DroppedMagazineStep(sample.raw,input,magazinePhysical->ProbeState(sample.input.nowNs).phase,sample.input.nowNs,magazineResult.tracking,handSample.owner,
            magazineResult.physicalDrop,magazineResult.physicalDropFromWell);
        if(magazinePhysicalFixture){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const auto now=HandNanos(stamp.QuadPart);
            magazinePhysicalFixture->Observe(magazinePhysical->ProbeState(now),rigPublication::ReadMagazinePackCounters(),now);
            if(magazinePhysicalFixture->CancelConsumer()){auto safety=handSample;safety.nowNs=now;
                magazinePhysical->Cancel(safety,handOwnership);magazineResult.tracking.target.reset();magazineResult.reloadHeld=false;
                magazineResult.blocksWeaponActions=magazinePhysical->BlocksEquipment();}}
        const bool burstFire=prepBurstFrame&&(out.held&interaction::Fire);
        ApplyMagazinePhysicalActions(out,magazineProfile,magazineResult);
        if(burstFire){if(magazineResult.blocksWeaponActions)++prepBurst.blocked;if(!(out.held&interaction::Fire))++prepBurst.fireMasked;else ++prepBurst.fireSent;}
        // XM8 VR bolt: the left hand racks the charging handle through the shared
        // arbiter (Mechanism after the right GunHold), never while the magazine
        // lane holds the hand or owes its own rack. Presentation plus gating only:
        // no native completion hook is identified, nothing native is written.
        if(xm8BoltRuntime::Active()){
            Xm8BoltCycleSample bolt;bolt.input=sample.input;bolt.evidence=sample.originalHandEvidence;bolt.weapon=handItem;
            bolt.xm8=asset==Xm8BoltWeaponAsset&&sample.raw.valid&&sample.raw.rigFingerprint==Xm8BoltRigFingerprint;
            bolt.gripPressed=sample.gripPressed;bolt.cancel=sample.cancel;
            bolt.magazineOwnsLeft=magazineResult.ownsLeftHand||magazineResult.rackOwnsLeft||magazineResult.rackTravel>=0||magazineResult.blocksWeaponActions;
            const auto palm=magazineProfile&&magazineProfile->geometry?interaction::InverseRigid(magazineProfile->geometry->interaction.insertion.itemFromHand):std::nullopt;
            if(bolt.xm8)bolt.weaponFromHand=Xm8BoltWeaponFromHand(sample.raw.rawLeftWristWorldMeters,sample.raw.weaponWorldMeters,palm?&*palm:nullptr);
            xm8Bolt=xm8BoltRuntime::TickCycle(bolt,handOwnership,handIntent);
            if(xm8Bolt.blocksFire){out.held&=~(interaction::Fire|interaction::AlternateFire);out.pressed&=~(interaction::Fire|interaction::AlternateFire);}
        }
        // Magazine seat click (ReloadInsertionHaptic::Seated), same writer as the SPAS path.
        for(unsigned n=0;n<magazineResult.feedbackCount;++n){
            const bool sent=SendFeedback(magazineResult.feedback[n]);if(sent)++feedbackSent;else ++feedbackDropped;
            gunFvr.Haptic(GunEvents(),gunWeapon,GunFeedbackName(magazineResult.feedback[n].kind),magazineResult.feedback[n].hand,sent);}
    }
    if(physicalReload){
        // Shared right GunHold has just been renewed. RAW controller/pouch and
        // renderer contact stay in their respective original input epochs.
        PhysicalReloadSample sample;sample.replacementFamily=physicalReplacementFamily;
        sample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        sample.input=handSample;sample.weapon=handItem;sample.geometrySequence=input.generation;sample.trackingEpoch=input.spaceGeneration;
        sample.actionFlagsKnown=true;sample.actionHeld=out.held;sample.actionPressed=out.pressed;
        sample.gripPressed=interaction::ReloadGripActive(input.hands[0].squeeze,handSample,handOwnership.Current(interaction::InteractionHand::Left));
        sample.cancel=automaticReloadBusy||bodyDraw.pending||magazineResult.blocksWeaponActions||(physicalReloadFixture&&physicalReloadFixture->CancelConsumer())||!aimValid||policyInactive||((out.pressed|out.held)&(interaction::Fire|interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        sample.asset=asset;sample.meshes=selectedMeshes.Read(sample.nativeOwner,handSample.nowNs);
        sample.raw=rigPublication::ReadReloadContact(sample.nativeOwner);
        if(const auto original=FindHandEvidence(sample.raw.inputEvidence.sequence,owner.weapon))sample.originalHandEvidence=original->sample;
        const auto pouch=bodyInventory?interaction::BodyAnchorHandPose(input,interaction::InteractionHand::Left):PhysicalReloadPouchPose(input);
        if(pouch)sample.bodyFromHand=*pouch;else sample.cancel=true;
        // Record the same pure input predicates separately from the existing
        // cancellation decision. These labels never drive native policy.
        const auto reloadActions=out.pressed|out.held;
        sample.cancelFlags=(bodyDraw.pending?ReloadCancelBodyDraw:0u)|
            (magazineResult.blocksWeaponActions?ReloadCancelMagazineBusy:0u)|
            ((physicalReloadFixture&&physicalReloadFixture->CancelConsumer())?ReloadCancelFixture:0u)|
            (!aimValid?ReloadCancelAimInvalid:0u)|(!out.active?ReloadCancelInactive:0u)|
            ((reloadActions&interaction::Fire)?ReloadCancelFire:0u)|((reloadActions&interaction::Use)?ReloadCancelUse:0u)|
            ((reloadActions&interaction::NextWeapon)?ReloadCancelNextWeapon:0u)|((reloadActions&interaction::PreviousWeapon)?ReloadCancelPreviousWeapon:0u)|
            (!pouch?ReloadCancelNoPouch:0u);
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
        if(armingEmptyFixture&&armingEmptyFixture->CancelConsumer()){sample.cancel=true;sample.cancelFlags|=ReloadCancelFixture;}
#endif
        physicalResult=UsePhysicalTube()?physicalTube->Tick(sample,handOwnership,handIntent):physicalReload->Tick(sample,handOwnership,handIntent);
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
        if(armingEmptyFixture){const auto policy=reloadFlowRuntime::ReadRequestProbeSnapshot();LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
            const auto now=HandNanos(stamp.QuadPart);physicalResult.reloadHeld=armingEmptyFixture->After(physicalReload->ProbeState(now),
                physicalResult.reloadHeld,policy?policy->phase:~0u,policy?policy->cycle:0,input.generation,now);
            if(armingEmptyFixture->CancelConsumer())CancelPhysicalReload(&physicalResult,ReloadCancelFixture);}
#endif
        if(physicalReloadFixture){LARGE_INTEGER observed{};QueryPerformanceCounter(&observed);const auto now=HandNanos(observed.QuadPart);
            physicalReloadFixture->Observe(TubeProbeState(now),now);if(physicalReloadFixture->CancelConsumer())CancelPhysicalReload(&physicalResult,ReloadCancelFixture);}
        // Owed pump (or the shot that will owe it, still recovering): the left
        // hand strokes rear toward the shooter and back along the gun hand's
        // aim axis (PumpAlong). The weapon-local -|z| it replaces read a left
        // hand behind the model origin backwards (run 091024 tape).
        const auto tubeView=UsePhysicalTube()?physicalWeaponRuntime::Read(owner.weapon):std::nullopt;
        const PumpPresentationOwner pumpOwner{tubeView?tubeView->identity:interaction::PhysicalWeaponIdentity{},sample.input.owner,owner.equipment};
        if(pumpPresentation.Observe(pumpOwner)){pumpVisual.reset();pumpSoundExpected=false;}
        bool pumpReturning=pumpPresentation.stroke.Returning();
        const bool tubePump=tubeView&&!tubeView->suspended&&tubeView->state.config.family==interaction::PhysicalWeaponFamily::TubePump;
        const bool expectedPump=UsePhysicalTube()?(tubeView&&PhysicalPumpExpected(tubeView->state,tubeView->suspended)):
            (pumpEnabled&&reloadFlowRuntime::PumpExpected());
        for(const auto& command:pumpCommands.Entries())if(command.id){const auto receipt=physicalWeaponRuntime::Result(command.id);
            if(receipt&&receipt->status!=physicalWeaponRuntime::CommandStatus::Pending){
                if(receipt->status==physicalWeaponRuntime::CommandStatus::Applied&&tubePump&&command.owner==pumpOwner){
                    ++pumpRacks;gunFvr.Rack(GunEvents(),gunWeapon,true);physicalTube->PumpRackFeedback(physicalResult,sample.input);
                    if(PumpSettleNs()){pumpPresentation.settleUntilNs=sample.input.nowNs+PumpSettleNs();++pumpSettles;}
                    if(reloadSoundEnabled&&!reloadEventAudio)pumpSoundGate.Trigger(AnimNowNs());pumpSoundExpected=false;}
                pumpCommands.Consumed(command.id);}}
        const bool pumpPending=tubeView&&pumpCommands.PendingFor(tubeView->identity);
        pumpReturning=pumpReturning||pumpPending;
        // Fire input is gated by authoritative readiness below; a held trigger
        // must not prevent the support hand from working its owed pump.
        const bool pumpCancelled=automaticReloadBusy||bodyDraw.pending||magazineResult.blocksWeaponActions||!aimValid||policyInactive||
            (physicalReloadFixture&&physicalReloadFixture->CancelConsumer())||
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        if(!pumpCancelled&&expectedPump&&!pumpPending&&!physicalResult.ammoOwnsHand&&sample.asset==SpasReloadAsset){
            const bool tracked=input.hands[0].gripTracked&&input.hands[1].aimTracked;
            const auto pumpPosition=tracked?PumpPosition(input.hands[0].grip.position,input.hands[1].aim):std::nullopt;
            const float along=pumpPosition?pumpPosition->z:0.f;
            // KF2's pump belongs to the actual fore-end support grip. A broad
            // barrel zone must not turn a pouch squeeze or free hand into a
            // pump, or snap a hand that never acquired the fore-end onto it.
            const auto support=handOwnership.Current(interaction::InteractionHand::Left);
            const bool inZone=pumpPosition&&input.hands[1].gripTracked&&support&&
                support->token.kind==interaction::HandClaimKind::WeaponSupport&&
                support->token.owner==sample.input.owner&&support->token.item==sample.weapon&&
                support->deadlineNs>sample.input.nowNs;
            if(sample.gripPressed&&tracked&&!pumpPresentation.zoneHeld&&!inZone)++pumpZoneRejects;
            pumpPresentation.zoneHeld=sample.gripPressed&&inZone;
            if(sample.gripPressed&&tracked){const float lateral=PumpLateral(input.hands[0].grip.position,math::Pose{input.hands[1].grip.position,input.hands[1].aim.orientation});pumpLateralMin=std::min(pumpLateralMin,lateral);pumpLateralMax=std::max(pumpLateralMax,lateral);}
            const auto stroke=pumpPosition?pumpPresentation.stroke.Update(pumpPresentation.zoneHeld,*pumpPosition,sample.input.observedNs):
                pumpPresentation.stroke.Update(false,0.f,sample.input.observedNs);
            pumpReturning=stroke.returning;
            ++pumpFrames;if(sample.gripPressed&&tracked){++pumpGripFrames;pumpMinAlong=std::min(pumpMinAlong,along);pumpMaxAlong=std::max(pumpMaxAlong,along);}
            pumpMaxTravel=std::max(pumpMaxTravel,stroke.travel);pumpPresentation.travel=stroke.travel;if(stroke.rearStop){++pumpRearStops;
                if(UsePhysicalTube())physicalTube->PumpRearFeedback(physicalResult,sample.input);else physicalReload->PumpRearFeedback(physicalResult,sample.input);}
            if(stroke.completed){pumpPresentation.stroke.Reset();
                if(UsePhysicalTube()){
                    if(pumpCommands.CanSubmit(tubeView->identity)){
                        const auto command=physicalWeaponRuntime::Submit(tubeView->identity,interaction::PhysicalWeaponCommandKind::CycleAction);
                        if(command)pumpCommands.Remember(command,pumpOwner);}
                }else if(reloadFlowRuntime::RackPump()){++pumpRacks;gunFvr.Rack(GunEvents(),gunWeapon,true);physicalReload->PumpRackFeedback(physicalResult,sample.input);
                    if(PumpSettleNs()){pumpPresentation.settleUntilNs=sample.input.nowNs+PumpSettleNs();++pumpSettles;}
                    if(reloadSoundEnabled&&!reloadEventAudio)pumpSoundGate.Trigger(AnimNowNs());pumpSoundExpected=false;}}
        }else{pumpPresentation.stroke.Reset();pumpPresentation.zoneHeld=false;pumpPresentation.travel=0;}
        // Pump clip: armed by the shot that will owe the pump; a hold released
        // without a rack (timeout, reload, state change) lets it play then.
        if((UsePhysicalTube()||pumpEnabled)&&reloadSoundEnabled&&sample.asset==SpasReloadAsset){const bool expected=expectedPump;
            pumpSoundGate.Weapon(owner.weapon,owner.soldier);
            if(expected&&!pumpSoundExpected)pumpSoundGate.Begin(AnimNowNs());
            if(!UsePhysicalTube()&&!expected&&pumpSoundExpected)pumpSoundGate.Trigger(AnimNowNs());
            pumpSoundExpected=expected;}
        // Fore-end visual: one closed-pose capture per owed pump (shot); the
        // stroke travel while the hand works it, closed otherwise.
        // After a rack the closed sample is held through the native pump replay
        // (settle); a new owed pump takes over, a weapon change or the deadline ends it.
        {const bool expected=sample.asset==SpasReloadAsset&&expectedPump;
            const bool settling=!expected&&pumpPresentation.settleUntilNs&&sample.asset==SpasReloadAsset&&
                sample.input.nowNs<pumpPresentation.settleUntilNs;
            // Read(view) can precede the terminal receipt publication in this
            // gather. Preserve the newly armed closed tail until debt clears.
            if(!settling&&!expected)pumpPresentation.settleUntilNs=0;
            if((expected||settling)&&!pumpVisual)pumpVisual=rigPublication::Tracking::PumpVisual{++pumpVisualShot,0.f};
            if(!expected&&!settling){pumpVisual.reset();pumpPresentation.handLatched=false;}
            else{
                if(expected){pumpVisual->fraction=pumpPresentation.stroke.Phase()==PumpStrokePhase::Idle?0.f:pumpPresentation.stroke.PoseFraction(pumpPresentation.travel);
                    pumpVisual->returning=pumpReturning||pumpPending;}
                else{pumpVisual->fraction=0.f;pumpVisual->returning=true;}
                // A static closed sample suppresses native replay after a rack;
                // it never acquires a free hand or starts another physical pump.
                pumpPresentation.handLatched=expected&&pumpPresentation.zoneHeld&&sample.gripPressed&&input.hands[0].gripTracked&&
                    !pumpCancelled&&!physicalResult.ammoOwnsHand;
                pumpVisual->handOwned=pumpPresentation.handLatched;}}
        for(unsigned n=0;n<physicalResult.feedbackCount;++n){
            if(reloadEventAudio&&sample.asset==SpasReloadAsset){
                const auto kind=physicalResult.feedback[n].kind;
                if(kind==interaction::FeedbackKind::ReloadSeated)SendReloadAudio(ipc::ReloadAudioRole::ShellIn,input);
                if(kind==interaction::FeedbackKind::PumpRear)SendReloadAudio(ipc::ReloadAudioRole::PumpBack,input);
                if(kind==interaction::FeedbackKind::PumpForward&&physicalResult.feedback[n].hand==0)SendReloadAudio(ipc::ReloadAudioRole::PumpForward,input);
            }
            const bool sent=SendFeedback(physicalResult.feedback[n]);if(sent)++feedbackSent;else ++feedbackDropped;
            gunFvr.Haptic(GunEvents(),gunWeapon,GunFeedbackName(physicalResult.feedback[n].kind),physicalResult.feedback[n].hand,sent);}
        // Native Reload is only the explicit 100ms cycle-start pulse in this mode.
        // Fire and equipment actions remain ordinary native actions after cancellation.
        if(asset==SpasReloadAsset){out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;}
        if(physicalResult.reloadHeld)out.held|=interaction::Reload;
    }
    // A trigger pull mid-reload cancels it; keep Fire off the native for up to
    // 150 ms while its abort lands (11 -> 1 -> 2, loaded kept), then fire.
    // A press during the hold-off is latched and replayed as a 60 ms tap as
    // soon as the abort lands (run 192404: a 100 ms tap was swallowed).
    {constexpr auto shots=interaction::Fire|interaction::AlternateFire;LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);const auto now=HandNanos(clock.QuadPart);
        if(reloadFlowRuntime::StopPending(150000000)){
            if((out.held|out.pressed)&shots){++fireHeldForStop;fireLatched|=(out.held|out.pressed)&shots;}
            out.held&=~shots;out.pressed&=~shots;}
        else if(fireLatched){out.pressed|=fireLatched;out.held|=fireLatched;fireReplayMask=fireLatched;fireReplayUntilNs=now+60000000;fireLatched=0;++fireReplays;}
        else if(fireReplayUntilNs&&now<fireReplayUntilNs)out.held|=fireReplayMask;
        else fireReplayUntilNs=0;}
    if(physicalWeaponRuntime::Enabled()){
        const bool attemptedFire=(out.pressed&interaction::Fire)!=0;
        const auto view=physicalWeaponRuntime::Read(owner.weapon);
        const auto admission=view?physicalWeaponRuntime::ActionAdmission{}:physicalWeaponRuntime::ReadActionAdmission(owner.weapon);
        if(attemptedFire&&view&&!view->suspended&&!view->state.canFire){
            const interaction::FeedbackEvent dryEvent{1,handSample.sequence,input.spaceGeneration,handSample.observedNs,handSample.deadlineNs,interaction::FeedbackKind::DryFire,1};
            SendFeedback(dryEvent);
        }
        // Native fire-state/audio activation precedes the projectile hook.
        // Use the same owned readiness before submitting trigger input; retain
        // the native Shot hook as the barrier for previously scheduled shots.
        if((view&&!interaction::WeaponReadinessAllowsFireInput(view->readiness))||
           admission.weapon==physicalWeaponRuntime::WeaponAdmission::Initializing||
           (admission.weapon==physicalWeaponRuntime::WeaponAdmission::Managed&&admission.suspended)){
            constexpr auto shots=interaction::Fire|interaction::AlternateFire;
            out.held&=~shots;out.pressed&=~shots;
        }
    }
    // Fail-stuck guard (KF2: the stock reload plays when the VR path cannot).
    // A Reload press on an empty gun that no physical reload picks up within
    // 400 ms (hand busy or lost, no geometry, consumer cancelled) becomes a
    // plain native reload: a 300 ms stock window through the empty gates and
    // a 150 ms Reload pulse.
    if(!physicalWeaponRuntime::Enabled()&&(magazinePhysical||physicalReload)){LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);const auto now=HandNanos(clock.QuadPart);
        const auto mst=magazinePhysical?std::optional(magazinePhysical->ProbeState(now)):std::nullopt;
        const auto pst=physicalReload?std::optional(TubeProbeState(now)):std::nullopt;
        const std::uint64_t marker=(mst?std::uint64_t(mst->started)<<32:0)|(pst?pst->acquired:0u);
        const bool busy=(mst&&(mst->active||mst->pending))||(pst&&(pst->active||pst->pending));
        if(reloadPressedRaw&&!stockPressNs&&!busy){stockPressNs=now;stockPressMarker=marker;}
        if(stockPressNs&&(busy||marker!=stockPressMarker||now<stockPressNs))stockPressNs=0;
        else if(stockPressNs&&now-stockPressNs>=400000000){stockPressNs=0;
            const auto reserve=reloadFlowRuntime::ReadReserve();
            if(reserve&&reserve->loaded==0&&reserve->reserve>0){reloadFlowRuntime::GrantStockReload(300000000);stockPulseUntilNs=now+150000000;++stockFallbacks;}
            else ++stockFallbackSkips;}
        if(stockPulseUntilNs&&now<stockPulseUntilNs)out.held|=interaction::Reload;else stockPulseUntilNs=0;}
    if(physicalWeaponRuntime::Enabled()&&automaticReloadBusy){
        // Cancelled frontends still tick above to drain exact cleanup receipts.
        // Only then can the authority enter its native reload transaction.
        out.held&=~(interaction::Reload|interaction::Fire|interaction::AlternateFire);
        out.pressed&=~(interaction::Reload|interaction::Fire|interaction::AlternateFire);
        if(automaticReload.token){
            const bool drained=(!magazinePhysical||!magazinePhysical->BlocksEquipment())&&!TubeBlocksEquipment()&&!pumpCommands.PendingFor(automaticReload.weapon)&&
                !(physicalReload&&TubeProbeState(handSample.nowNs).pending);
            if(!automaticReload.requested&&drained){
                automaticReload.requested=physicalWeaponRuntime::RequestNativeReload(automaticReload.weapon,automaticReload.token);
                if(automaticReload.requested)++automaticReloadStarts;
            }
            if(automaticReload.requested){
                const auto status=physicalWeaponRuntime::ReadNativeReload(automaticReload.weapon,automaticReload.token);
                if(status&&status->busy){if(status->inputNeeded)out.held|=interaction::Reload;}
                else automaticReload={};
            }else if(handSample.nowNs-automaticReload.waitingSinceNs>2000000000)CancelAutomaticReload();
        }
    }
    if(physicalWeaponRuntime::Enabled()){
        const auto owned=physicalWeaponRuntime::Read(owner.weapon);
        const auto ready=owned?owned->readiness:interaction::WeaponReadiness{};
        if(weaponReadyTransitions.Update(owned?owned->identity:interaction::PhysicalWeaponIdentity{},ready.cue)&&GunEvents().Enabled()){
            GunEvent event;event.phase=GunEventPhase::Hint;event.asset=gunWeapon.asset;event.weaponId=gunWeapon.id;
            SetGunText(event.detail,interaction::WeaponReadyCueName(ready.cue));
            event.v[0]=ready.physicalCanFire;event.v[1]=ready.projectionPending;GunEvents().Push(event);
        }
    }
    // fvr phase rows from the consumers' own probe state (no native read).
    if(GunEvents().Enabled()){LARGE_INTEGER probed{};QueryPerformanceCounter(&probed);const auto probeNs=HandNanos(probed.QuadPart);
        if(magazinePhysical){const auto st=magazinePhysical->ProbeState(probeNs);gunFvr.Magazine(GunEvents(),gunWeapon,{st.phase,st.reason,st.cycle,st.originalReturns});
            if(magazineResult.settleStarted)gunFvr.Settle(GunEvents(),gunWeapon);
            if(magazineResult.racked)gunFvr.Rack(GunEvents(),gunWeapon,!magazineResult.boltReleased);
            if(magazineResult.rackTrace&&GunEvents().Enabled()){const auto& t=*magazineResult.rackTrace;
                GunEvent e;e.phase=GunEventPhase::RackTrace;e.asset=gunWeapon.asset;e.weaponId=gunWeapon.id;
                const auto mm=[](float m){return std::isfinite(m)?int(std::lround(std::clamp(m,-1000.f,1000.f)*1000)):0;};
                e.v={int(t.phase),int(t.reason),mm(t.distance),mm(t.along),mm(t.lateral),int(std::lround(t.travel*1000))};
                if(t.grip)e.flags|=gun_flag::Value0;
                char text[48]{};std::snprintf(text,sizeof(text),"%d,%d,%d;%d,%d,%d",mm(t.handleWorld[0]),mm(t.handleWorld[1]),mm(t.handleWorld[2]),
                    mm(t.palmWorld[0]),mm(t.palmWorld[1]),mm(t.palmWorld[2]));
                SetGunText(e.detail,text);GunEvents().Push(e);}
            const auto c=magazinePhysical->Chamber();
            gunFvr.Chamber(GunEvents(),gunWeapon,{c.known,c.magazineOut,c.chamberLoaded,c.needsRack,c.loaded,c.displayAmmo});}
        if(physicalReload){const auto st=TubeProbeState(probeNs);gunFvr.Shell(GunEvents(),gunWeapon,{st.active,st.held,st.submitted,st.completed,st.cycle});
            if(physicalResult.settleStarted)gunFvr.Settle(GunEvents(),gunWeapon,"port");}
        const auto left=handOwnership.Current(interaction::InteractionHand::Left),right=handOwnership.Current(interaction::InteractionHand::Right);
        gunFvr.Claims(GunEvents(),gunWeapon,{left?std::uint8_t(left->token.kind):std::uint8_t(0),right?std::uint8_t(right->token.kind):std::uint8_t(0)},
            {left?left->token.id:0,right?right->token.id:0});
        if(gatherTick%30==0)gunFvr.Sample(GunEvents(),gunWeapon);
    }
    // The renderer's authored reference is bound to the original exact
    // equipment/configuration/rig lease; ReadWeaponShotFrame revalidates it.
    // It admits this contact to the SAME support/claim policy, without enabling
    // any native muzzle, aim-axis, sight, reload or holster feature.
    auto supportEvidence=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
    // Every gun offers the support grip (KF2 standard: the contact is the
    // original 1P left-wrist pose relative to the weapon). The profile gate
    // only admitted SPAS/XM8; FVR_BC2_SUPPORT_ALL_WEAPONS=0 restores it.
    if(!zoneSettings.supportAllWeapons&&!interaction::WeaponFeatureEnabled(coreProfile,interaction::WeaponFeature::SupportGrip)&&
       (!supportEvidence||!supportEvidence->authoredSupportReference))supportEvidence.reset();
    const auto supportContact=supportEvidence&&supportEvidence->ownerGeneration==rigEpoch&&supportEvidence->space==input.spaceGeneration?
        supportEvidence->support:interaction::SupportGripContact{};
    std::optional<WeaponModeCommand> physicalMode;std::uint64_t physicalRequest=0;
    bool sightOwnsHand=false;rigPublication::SightPreview sightPreview;
    if(sightFlip&&!physicalResult.ammoOwnsHand&&!magazineResult.ownsLeftHand&&!magazineResult.blocksWeaponActions&&!bodyDraw.pending){
        // Sight preference and support distance come from one immutable pose.
        // Current and original-source tracking are independently checked before
        // ownership; no left-hand eligibility is inferred from the gun alone.
        const auto* sightAdapter=familyBinding?SightAdapterForFamily(familyBinding->family):nullptr;
        const auto contact=sightAdapter&&sightAdapter->nativeSightAccepted&&supportEvidence&&supportEvidence->ownerGeneration==rigEpoch&&supportEvidence->space==input.spaceGeneration?
            supportEvidence->sight:rigPublication::WeaponSightContact{};
        if(!sightPolicy&&contact.valid){
            interaction::SightFlipConfig config;config.pivotMeters=contact.pivotMeters;config.axis=contact.axis;
            config.grabRadiusMeters=.08f;config.holdRadiusMeters=.20f;config.minLeverMeters=.015f;
            config.thresholdRadians=.40f;config.hysteresisRadians=.08f;config.maxStepRadians=.70f;
            config.detentHoldNs=70000000;config.gestureTimeoutNs=2500000000;config.ackTimeoutNs=1500000000;config.maxSampleGapNs=250000000;
            sightPolicy.emplace(config);
        }
        const bool occupied=sightPhase!=interaction::SightFlipPhase::Idle;
        const bool preferred=occupied||(contact.valid&&contact.contactDistanceMeters<=.08f&&(!supportContact.valid||contact.contactDistanceMeters<supportContact.distanceMeters));
        const bool cancelling=((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon))!=0;
        interaction::SightFlipSample live;
        live.owner={(std::uint64_t(owner.weak)<<32)|owner.soldier,rigEpoch,familyBinding?familyBinding->persistent:0,input.spaceGeneration};
        live.sequence=input.generation;live.nowNs=std::int64_t(GetTickCount64())*1000000;
        live.focused=input.focused&&!cancelling;live.tracked=input.headValid&&input.hands[0].gripTracked&&input.hands[1].gripTracked&&input.hands[1].aimTracked&&(input.hands[0].active&interaction::Squeeze);
        live.squeeze=input.hands[0].squeeze;live.nativeModeValid=bool(familyBinding);
        live.nativeMode=familyBinding&&familyBinding->selectedSecondary?interaction::SightMode::Secondary:interaction::SightMode::Primary;
        live.acknowledgedRequest=sightAcknowledged;
        // Render publication usually trails the current XR packet. Its local
        // hand point must consume that same packet's squeeze, never a newer one.
        const auto packet=sightPackets.Update(
            {live,owner.weapon,inputDeadline,out.active||occupied,sightPhase==interaction::SightFlipPhase::AwaitingAcknowledgement,sightPhase==interaction::SightFlipPhase::Latched},
            {contact.valid,preferred,contact.generation,contact.deadline,contact.handLocalMeters,contact.contactDistanceMeters},
            clock.QuadPart);
        interaction::SightFlipResult sightResult;
        if(sightPolicy){
            // Bind the hinge from the same verified publication as the press,
            // immediately before a new gesture. Never cache an equip-transition
            // pivot for the lifetime of the injected session, nor move a hinge
            // while a gesture/request is in flight.
            if(packet.advanced&&packet.sample.contactValid&&packet.geometrySequence==contact.generation)
                sightPolicy->SetIdleGeometry(contact.pivotMeters,contact.axis);
            // Do not commit a policy edge, preview or native mode request until
            // its hand ownership succeeds. The original contact retains its
            // publication generation and deadline even when raw input is newer.
            auto proposed=*sightPolicy;
            sightResult=proposed.Update(packet.sample);
            gatherSightAck=live.acknowledgedRequest;
            if(sightResult.phase!=interaction::SightFlipPhase::Idle){
                const bool geometry=packet.paired&&contact.valid&&packet.geometrySequence==contact.generation;
                // A matched native acknowledgement needs no old-mode geometry.
                // Ownership here is a held interaction reservation, not a pose.
                const bool acknowledged=live.acknowledgedRequest&&
                    (sightPhase==interaction::SightFlipPhase::AwaitingAcknowledgement||sightResult.committedMode);
                const auto source=geometry?packet.geometrySequence:acknowledged?input.generation:0;
                const auto expiry=geometry?contact.deadline:inputDeadline;
                const bool latched=sightPhase==interaction::SightFlipPhase::Latched&&sightResult.phase==interaction::SightFlipPhase::Latched;
                bool owned=latched?interaction::RenewLatchedSightReservation(handOwnership,handSample,handItem,sightPhase,sightResult):
                    source&&OwnLeft(interaction::HandClaimKind::Sight,sightResult.grabbed,source,expiry,owner.weapon);
                // The policy must first confirm the exact pending request, native
                // target and bounded sample gap. Recovery cannot dispatch again.
                if(!owned&&sightLeaseContinuation&&sightResult.committedMode)
                    owned=ReserveAcknowledgedSight();
                if(!owned&&!sightResult.grabbed){
                    const auto existing=handOwnership.Current(interaction::InteractionHand::Left);
                    // Expected mode switch may occur on a duplicate raw packet;
                    // keep the original unexpired reservation without new pose.
                    owned=acknowledged&&existing&&existing->token.kind==interaction::HandClaimKind::Sight;
                }
                if(!owned){
                    // Cancel the original pending interaction, not the trial
                    // state that may already have consumed its acknowledgement.
                    proposed=*sightPolicy;sightResult=proposed.Update({});
                    sightAcknowledged=0;HandChanged("sight_denied");ReleaseSightOwnership();
                }
            }else ReleaseSightOwnership();
            *sightPolicy=proposed;sightPhase=sightResult.phase;
            gatherSightRequest=sightResult.request?sightResult.request->id:0;gatherSightCommit=bool(sightResult.committedMode);
            RecordSightGesture(packet.sample,sightResult,contact,owner.weapon,packet.advanced);
            if(sightResult.grabbed&&packet.paired&&packet.geometrySequence==contact.generation&&contact.previewValid){
                sightGrasp=interaction::SightGraspBinding::Begin(contact.pivotMeters,contact.axis,contact.sightLocalMeters,
                    contact.handLocalFrameMeters,contact.graspPointMeters,contact.palmPointWristMeters,sightAdapter?sightAdapter->travelRadians:1.570796327f);
                sightAdapterAtGrab=sightAdapter;sightFrontAtGrab=contact.frontLocalMeters;sightPalmAtGrab=contact.palmPointWristMeters;sightGraspMode=packet.sample.nativeMode;++sightGraspToken;
                sightGraspGeneration=packet.geometrySequence;
                const auto raw=interaction::SightVisualHandCapture{contact.rawHandLocalFrameMeters,contact.rawPalmPointWristMeters,packet.geometrySequence};
                sightVisual=sightAdapter&&sightAdapter->assembly==SightAssemblyKind::Subtree?
                    interaction::SightVisualHandoff::BeginWithGeometry(contact.sightLocalMeters,sightGraspMode,contact.pivotMeters,contact.axis,sightAdapter->travelRadians,raw):
                    interaction::SightVisualHandoff::Begin(contact.sightLocalMeters,sightGraspMode,raw);
            }
            // The visual attachment remains captured while native mode selection
            // and its later animation finish. This is a current held reservation,
            // never replacement contact evidence or an additive native write.
            if(sightGrasp&&sightVisual&&sightPhase!=interaction::SightFlipPhase::Idle){
                std::optional<math::Matrix4> nativeRear;std::optional<interaction::SightVisualRawHand> rawHand;
                if(familyBinding&&supportEvidence&&supportEvidence->ownerGeneration==rigEpoch&&supportEvidence->space==input.spaceGeneration&&
                   supportEvidence->generation>=sightGraspGeneration&&supportEvidence->generation<=input.generation&&
                   supportEvidence->deadline>clock.QuadPart&&supportEvidence->nativeSight.valid&&
                   supportEvidence->nativeSight.physicalItem==familyBinding->persistent){
                    nativeRear=supportEvidence->nativeSight.rear;
                    if(supportEvidence->nativeSight.rawHandValid)
                        rawHand=interaction::SightVisualRawHand::Fresh(supportEvidence->nativeSight.rawHand,
                            supportEvidence->generation,input.generation,clock.QuadPart,supportEvidence->deadline);
                }
                const auto progress=sightVisual->Update(sightPhase,sightResult.signedRadians,input.predictedNs,nativeRear,rawHand);
                const auto preview=progress?sightGrasp->Evaluate(progress->appliedRadians,sightGraspMode):std::nullopt;
                const math::Vec3 pivot{sightFrontAtGrab.values[3][0],sightFrontAtGrab.values[3][1],sightFrontAtGrab.values[3][2]};
                const math::Vec3 axis{sightFrontAtGrab.values[0][0],sightFrontAtGrab.values[0][1],sightFrontAtGrab.values[0][2]};
                const bool subtree=sightAdapterAtGrab&&sightAdapterAtGrab->assembly==SightAssemblyKind::Subtree;
                const auto front=preview&&!subtree?interaction::RotateAboutHinge(sightFrontAtGrab,pivot,axis,-preview->appliedRadians):std::nullopt;
                const auto reservation=handOwnership.Current(interaction::InteractionHand::Left);
                const auto visualDeadline=reservation?std::min(inputDeadline,HandTicks(reservation->deadlineNs)):0;
                if(preview&&(subtree||front)&&reservation&&reservation->token.kind==interaction::HandClaimKind::Sight&&visualDeadline>clock.QuadPart){
                    sightPreview.valid=true;sightPreview.token=sightGraspToken;sightPreview.weapon=owner.weapon;
                    sightPreview.generation=input.generation;sightPreview.deadline=visualDeadline;
                    sightPreview.physicalItem=familyBinding?familyBinding->persistent:0;
                    sightPreview.grabGeneration=sightGraspGeneration;sightPreview.phase=sightPhase;
                    sightPreview.adapter=sightAdapterAtGrab;sightPreview.rear=preview->sight;if(front)sightPreview.front=*front;sightPreview.wrist=preview->wrist;
                    sightPreview.palmPointWrist=sightPalmAtGrab;sightPreview.graspPoint=preview->graspPoint;
                    sightPreview.angle=preview->appliedRadians;sightPreview.gestureAngle=sightResult.signedRadians;
                    sightPreview.nativeObserved=progress->nativeObserved;sightPreview.nativeProgress=progress->nativeProgressRadians;
                    sightPreview.rawObserved=progress->rawObserved;sightPreview.rawProgress=progress->rawProgressRadians;sightPreview.rawGeneration=progress->rawGeneration;
                }
            }else if(sightPhase==interaction::SightFlipPhase::Idle){sightGrasp.reset();sightVisual.reset();}
            if(sightResult.cancelled){
                ++sightCancellations;sightAcknowledged=0;
                if(pendingMode&&modeRecords[*pendingMode].gesture==sightResult.cancelledRequest){modeRecords[*pendingMode].cancelled=true;pendingMode.reset();}
            }
            if(sightResult.committedMode){++sightCommits;sightAcknowledged=0;}
            if(sightResult.request){
                const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},[](void*,unsigned at,const char* expected){return Type(at,expected);}};
                const auto mode=weaponModeBindingVerified?ResolveWeaponMode(memory,owner.soldier,owner.weapon):std::nullopt;
                const auto expected=sightResult.request->target==interaction::SightMode::Secondary?33u:36u;
                if(mode&&mode->action==expected&&!pendingMode){
                    physicalMode=mode;physicalRequest=sightResult.request->id;++sightRequests;
                }else{++modeRejections;CancelSight();}
            }
            const auto claim=handOwnership.Current(interaction::InteractionHand::Left);
            sightOwnsHand=sightPhase!=interaction::SightFlipPhase::Idle&&claim&&claim->token.kind==interaction::HandClaimKind::Sight;
            if(!sightOwnsHand)sightPreview={}; // Dispatch rejection/cancellation invalidates the same publication.
        }
        SightRecord record;
        record.ms=GetTickCount64();record.generation=input.generation;record.contactGeneration=contact.generation;
        record.coherentGeneration=packet.sample.sequence;record.request=sightResult.request?sightResult.request->id:0;
        record.weapon=owner.weapon;record.phase=unsigned(sightResult.phase);record.cancel=unsigned(sightResult.reason);
        record.packetReason=unsigned(packet.reason);record.distance=contact.contactDistanceMeters;record.angle=sightResult.signedRadians;
        record.squeeze=live.squeeze;record.pairedSqueeze=packet.sample.squeeze;record.supportDistance=supportContact.distanceMeters;
        record.contact=contact.valid;record.grabbed=sightResult.grabbed;record.committed=bool(sightResult.committedMode);
        record.preferred=preferred;record.tracked=live.tracked;record.familyValid=bool(familyBinding);
        record.paired=packet.paired;record.advanced=packet.advanced;
        RecordSight(record,sightResult.grabbed||sightResult.request||sightResult.cancelled||sightResult.committedMode||sightResult.released);
    }
    if(sightOwnsHand){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
    bool supportHeld=false;std::uint64_t supportToken=0;
    if(twoHandGrip){
        const auto contact=supportContact;
        LARGE_INTEGER supportClock{};QueryPerformanceCounter(&supportClock);
        const bool handBusy=physicalResult.ammoOwnsHand||MagazineBlocksSupport(magazineResult,HandNanos(supportClock.QuadPart))||magazineResult.rackOwnsLeft||xm8Bolt.ownsLeft;
        // Weapon flicks (Next/PreviousWeapon) no longer cancel support: an
        // unsuccessful flick kept dropping the support hand. A real switch
        // changes owner.weapon and SupportGrip releases on that identity change.
        // FVR_BC2_SUPPORT_KEEP_ON_FLICK=0 restores the old cancel.
        const auto flick=(out.pressed|out.held)&(interaction::NextWeapon|interaction::PreviousWeapon);
        const bool cancel=bodyDraw.pending||sightOwnsHand||!aimValid||!out.active||
            ((out.pressed|out.held)&interaction::Use)||(flick&&!zoneSettings.supportKeepOnFlick);
        if(flick&&zoneSettings.supportKeepOnFlick&&lastSupportSample.sample.holding)++supportFlickKept;
        auto proposed=supportGrip;
        const interaction::SupportGripOwner supportOwner{(std::uint64_t(owner.weak)<<32)|owner.soldier,rigEpoch,owner.weapon};
        auto supported=proposed.Update(supportOwner,input,contact,cancel,handBusy);
        if(supported.holding){
            const bool owned=OwnLeft(interaction::HandClaimKind::WeaponSupport,supported.engaged,
                supportEvidence?supportEvidence->generation:handSample.sequence,
                supportEvidence?supportEvidence->deadline:inputDeadline,owner.weapon,&supported,cancel);
            if(!owned){
                // Re-evaluate the original state: a rejected trial grab is
                // not logged as an accepted grab/release pair.
                // A new trial can precede its renderer-source squeeze proof.
                // Keep its genuine neutral arm while that claim is unavailable;
                // established holds still cancel when their proof is lost.
                supported=supportGrip.Update(supportOwner,input,contact,!supported.engaged,true,false);
                proposed=supportGrip;ReleaseLeftOwnership(interaction::HandClaimKind::WeaponSupport);
            }
        }else ReleaseLeftOwnership(interaction::HandClaimKind::WeaponSupport);
        supportGrip=proposed;
        if(supported.holding)++supportSamples;if(supported.engaged)++supportGrabs;if(supported.released)++supportReleases;
        auto restored=supported.input;restored.hands[1].grip.orientation=input.hands[1].grip.orientation;restored.hands[1].aim.orientation=input.hands[1].aim.orientation;
        if(std::memcmp(&restored,&input,sizeof(input)))++supportPreservationFailures;
        const auto now=GetTickCount64();
        SupportEvent evidence;
        evidence.sample={now,input.generation,contact.distanceMeters,input.hands[0].squeeze,supported.correctionRadians,contact.valid,supported.holding,supported.engaged,supported.released,input.hands[1].aim.orientation,supported.input.hands[1].aim.orientation,itemProfileCandidate?itemProfileCandidate->telemetryKind:0,unsigned(supported.reason),out.pressed|out.held,supported.token};
        evidence.eventMs=now;evidence.previousToken=lastSupportSample.sample.holding?lastSupportSample.sample.token:0;
        evidence.rigEpoch=rigEpoch;evidence.space=input.spaceGeneration;evidence.actor=owner.soldier;evidence.weak=owner.weak;evidence.weapon=owner.weapon;
        evidence.cancelFlags=(sightOwnsHand?1u:0u)|(!aimValid?2u:0u)|(!out.active?4u:0u)|(((out.pressed|out.held)&interaction::Use)?8u:0u)|(((out.pressed|out.held)&interaction::NextWeapon)?16u:0u)|(((out.pressed|out.held)&interaction::PreviousWeapon)?32u:0u)|(physicalResult.ammoOwnsHand?64u:0u)|(MagazineBlocksSupport(magazineResult,HandNanos(supportClock.QuadPart))?128u:0u);
        evidence.focused=input.focused;evidence.headTracked=input.headValid;evidence.leftGripTracked=input.hands[0].gripTracked;evidence.rightGripTracked=input.hands[1].gripTracked;evidence.rightAimTracked=input.hands[1].aimTracked;
        evidence.leftGrip=input.hands[0].grip.position;evidence.rightGrip=input.hands[1].grip.position;
        // Retain first/changed failed squeeze, never a per-frame retry stream.
        // Ordinary records roll over before users finish describing a delay.
        const bool failedAttempt=input.hands[0].squeeze>=.7f&&!supported.holding;
        const bool attemptIdentityChanged=supportAttemptRecord.weapon!=evidence.weapon||supportAttemptRecord.rigEpoch!=evidence.rigEpoch||supportAttemptRecord.space!=evidence.space;
        const bool changedAttempt=attemptIdentityChanged||supportAttemptRecord.cancelFlags!=evidence.cancelFlags||supportAttemptRecord.sample.reason!=evidence.sample.reason||
            supportAttemptRecord.sample.contact!=evidence.sample.contact||
            (supportAttemptRecord.sample.distance<=.16f)!=(evidence.sample.distance<=.16f);
        const bool retainedAttempt=failedAttempt&&(!supportAttemptActive||attemptIdentityChanged||
            (changedAttempt&&evidence.eventMs-supportAttemptRecord.eventMs>=100));
        if(supported.engaged||supported.released||retainedAttempt)RetainSupportEvent(evidence);
        if(retainedAttempt){supportAttemptRecord=evidence;supportAttemptActive=true;}
        if(!failedAttempt)supportAttemptActive=false;
        if(supported.released&&unsigned(supported.reason)<supportReleaseCounts.size())++supportReleaseCounts[unsigned(supported.reason)];
        lastSupportSample=evidence;
        if(supported.engaged||supported.released||now-supportRecordTime>=100){
            supportRecords[supportRecordNext]=evidence.sample;supportRecordNext=(supportRecordNext+1)%supportRecords.size();supportRecordCount=std::min(unsigned(supportRecords.size()),supportRecordCount+1);supportRecordTime=now;
        }
        supportHeld=supported.holding;supportToken=supported.token;if(supportHeld&&(out.held&interaction::Fire))++supportFireSamples;
        input=supported.input; // Private copy only; cached raw XR input is unchanged.
    }
    if(reloadRequestMode)reloadFlowRuntime::PublishMagazineInteractionDiagnostic({magazineOwner,
        input.generation,handSample.observedNs,handSample.deadlineNs,supportHeld,
        magazineResult.ownsLeftHand,magazineResult.blocksWeaponActions,unsigned(magazineResult.interaction.phase)});
    constexpr float tau=6.283185307179586f,rad=.017453292519943295f;
    const auto wrap=[](float value){float out=std::fmod(value,tau);return out<0?out+tau:out;};
    using SetAngle=void(__thiscall*)(void*,float);
    const auto setYaw=[&](float yaw){
        reinterpret_cast<SetAngle>(image+aimingProfile.aimerYawSetter)(reinterpret_cast<void*>(aimer),yaw);
        std::memcpy(reinterpret_cast<void*>(owner.aim+12),&yaw,4);
    };
    auto desiredBodyAxes=std::array{out.forward,out.strafe};
    std::optional<std::array<float,2>> movementYaw;
    if(motionAim&&aimValid){
        // DiscoverAiming verifies yawGetter reads this simulation-aim yaw.
        float movementNativeYaw=simulationYaw;
        if(anchorSoldier!=owner.soldier||anchorWeak!=owner.weak){
            anchorSoldier=owner.soldier;anchorWeak=owner.weak;anchorSpace=input.spaceGeneration;baseYaw=nativeYaw;lastHandYaw=lastHeadYaw=0;
        }else if(anchorSpace!=input.spaceGeneration){
            // Explicit XR recenter makes the old HMD direction the new forward.
            baseYaw=wrap(baseYaw+lastHeadYaw);anchorSpace=input.spaceGeneration;lastHandYaw=lastHeadYaw=0;
        }
        if(out.turnDegrees){baseYaw=wrap(baseYaw+out.turnDegrees*rad);++turns;}
        if(bodyFollow&&out.active){
            math::Vec3 body{};const auto localHead=math::MakeRelativePose(input.referenceHead,input.head);
            if(localHead&&BodyPosition(owner,body)){
                const float units=input.worldUnitsPerMeter;body.x/=units;body.y/=units;body.z/=units;
                const auto state=roomscale.Update({(std::uint64_t(owner.weak)<<32)|owner.soldier,input.spaceGeneration,GetTickCount64(),localHead->position,body,baseYaw-3.141592653589793f,std::hypot(out.forward,out.strafe)>.001f});
                consumedOffset=state.consumedLocalMeters;
                // BC2 adapter calibration: about 6 metres/second at full native
                // on-foot input. The feedback loop uses actual collision motion.
                if(state.valid&&state.driving){out.forward=RoomscaleNativeAxis(state.forwardMetersPerSecond);out.strafe=RoomscaleNativeAxis(state.strafeMetersPerSecond);++followSamples;}
                const auto now=GetTickCount64();if(followRecordCount<followRecords.size()&&now-followRecordTime>=100){
                    followRecords[followRecordCount++]={now,localHead->position,body,consumedOffset,out.forward,out.strafe};followRecordTime=now;
                }
            }else{roomscale.Suspend();++bodyReadFailures;}
        }
        auto hand=input.hands[1].aimTracked?interaction::ControllerAim(input.referenceHead,input.hands[1].aim,lastHandYaw):std::nullopt;
        if(input.hands[1].gripTracked){
            if(const auto relation=rigPublication::ReadWeaponBoreInGrip(owner.soldier,owner.weak,owner.weapon,rigEpoch,
                twoHandGrip?handEquip:renderEquipmentEpoch,input.spaceGeneration,owner.equipment)){
                if(const auto bore=CurrentGripBore(input.referenceHead,input.hands[1].grip,*relation))
                    if(const auto physical=GripBoreAngles(*bore,lastHandYaw))hand=physical;
            }
        }
        if(hand){
            ObserveRecoilVrBaseline(NativeRecoilSeam::BeforeVrBaseline,owner,aimer,input,inputDeadline,wrap(baseYaw+hand->yaw),hand->pitch);
            if(aimAngleTraceRequested&&aimAngleScope){
                aimAngleScope->requested=std::array<float,2>{wrap(baseYaw+hand->yaw),hand->pitch};
                SampleAimAngles(AimAngleStage::BeforeVrBaseline,&input);
            }
            lastHandYaw=hand->yaw;movementNativeYaw=wrap(baseYaw+hand->yaw);setYaw(movementNativeYaw);
            // Normal mouse-down lowers native pitch; positive native pitch aims up.
            const float pitch=hand->pitch;
            reinterpret_cast<SetAngle>(image+aimingProfile.aimerPitchSetter)(reinterpret_cast<void*>(aimer),pitch);
            float clamped=0;Read(aimer+16,&clamped,4);std::memcpy(reinterpret_cast<void*>(owner.aim+16),&clamped,4);
            ObserveRecoilVrBaseline(NativeRecoilSeam::AfterVrBaseline,owner,aimer,input,inputDeadline,wrap(baseYaw+hand->yaw),hand->pitch);
            if(aimAngleTraceRequested&&aimAngleScope){
                aimAngleScope->baselineWritten=true;
                SampleAimAngles(AimAngleStage::AfterVrBaseline,&input);
            }
            if(supportHeld){
                float actualYaw=0;Read(aimer+12,&actualYaw,4);
                const float wanted=wrap(baseYaw+hand->yaw),difference=std::remainder(actualYaw-wanted,tau);
                supportAimResidual=std::max(supportAimResidual,std::max(std::abs(difference),std::abs(clamped-pitch)));++supportAimSamples;
            }
            // Native movement follows the gun. Cancel that relative angle so
            // the shared HMD-relative movement remains in the body reference.
            ++aimSamples;
        }
        // Tracking loss leaves native aiming in charge. Its current yaw may
        // differ from the last tracked hand, especially after an item/turn.
        desiredBodyAxes={out.forward,out.strafe};
        const auto head=interaction::ControllerAim(input.referenceHead,input.head,lastHeadYaw);
        // Native sprint only accepts roughly-forward input and then runs along
        // its own aim yaw, which is the gun hand here. Sprint therefore needs
        // the HMD to be the native heading: the stick stays HMD-relative and
        // forward-only like stock, whatever the gun hand or room is doing.
        // Firing/zoom cancel native sprint, so those frames keep the gun yaw.
        if(head&&interaction::SprintOwnsHeading(out.held)){movementNativeYaw=wrap(interaction::SprintNativeYaw(baseYaw,head->yaw));setYaw(movementNativeYaw);++sprintHeadings;}
        movementYaw=std::array{baseYaw,movementNativeYaw};
        interaction::MovementToNativeHeading(baseYaw,movementNativeYaw,out.forward,out.strafe);
        if(head)lastHeadYaw=head->yaw;
        PublishAnchor(owner.soldier,owner.weak,input.spaceGeneration,baseYaw,input.worldUnitsPerMeter);
        if(handPoses){math::Vec3 actorPosition{};
            if(BodyPosition(owner,actorPosition)){
                rigPublication::Tracking sample{input,owner.soldier,owner.weak,owner.weapon,rigEpoch,inputDeadline,baseYaw,consumedOffset,actorPosition};
                if(NativeActionCaptureRequested()&&nativeActionFiring&&
                   nativeActionFiring->owner==ReloadStateOwner{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration}&&
                   nativeActionFiring->config.weaponData==owner.equipment.data&&nativeActionFiring->sequence<=input.generation&&
                   nativeActionFiring->observedNs<=reloadSourceNowNs&&reloadSourceNowNs-nativeActionFiring->observedNs<200000000)
                    sample.nativeActionFiring=nativeActionFiring;
                if(fresh&&nativePlaying)ObserveAudioIdentity(owner,input,rigEpoch,twoHandGrip?handEquip:renderEquipmentEpoch,
                    renderEquipmentEpoch,handEquip,inputDeadline,handFrequency);
                sample.equipmentGeneration=twoHandGrip?handEquip:renderEquipmentEpoch;sample.nativeEquipment=owner.equipment;sample.sightPhysicalItem=familyBinding?familyBinding->persistent:0;sample.sightAdapter=familyBinding?SightAdapterForFamily(familyBinding->family):nullptr;sample.sightSecondary=familyBinding&&familyBinding->selectedSecondary;sample.weaponProfile=itemProfile;sample.assetName=weaponAsset;sample.supportHolding=supportHeld;sample.supportToken=supportToken;sample.sightPreview=sightPreview;sample.reload=physicalResult.tracking;sample.pump=pumpVisual;sample.handleTravel=magazineResult.rackTravel;sample.handleOwned=magazineResult.rackPoseOwned;sample.nativeRack=magazineResult.nativeRack;sample.handleHandOwned=magazineResult.rackOwnsLeft||xm8Bolt.ownsLeft;if(xm8Bolt.ownsLeft)if(const auto* parts=FindChargingHandleParts(asset))sample.handleTravel=(std::max)(sample.handleTravel,xm8BoltRuntime::HandleFraction(xm8Bolt.travel,parts->strokeMeters));sample.magazine=magazineResult.tracking;sample.bodyMagazine=magazineResult.bodyAmmo;if(magazinePhysical){sample.pouchContacts=magazinePhysical->PouchContacts();sample.pouchFrame=magazinePhysical->PouchFrame();}sample.weaponActionsBlocked=magazineResult.blocksWeaponActions;if(bodyHolster||magazineDetached)holsterTracking=sample;else PublishRecoilObservedTracking(sample);
            }
            else{++bodyReadFailures;PublishRecoilObservedTracking({});}
        }
    }else if(out.turnDegrees){
        if(aimValid){setYaw(wrap(nativeYaw+out.turnDegrees*rad));++turns;}else ++turnUnavailable;
    }
    if(out.active){
        if(input.hands[0].gripTracked&&!input.hands[1].aimTracked)++leftOnlyActions;
        if(!input.hands[0].gripTracked&&input.hands[1].aimTracked)++rightOnlyActions;
        if(out.held&interaction::Fire){if(!input.hands[1].aimTracked)++untrackedFire;else if(!input.hands[0].gripTracked)++leftLossFire;}
        else if(input.hands[1].aimTracked&&input.hands[1].trigger>=.75f)++recoveryFireSuppressed;
    }
    // The automatic equip/shot diagnostic is limited to these inspected
    // conventional firearms. Normal user-driven controls retain native weapons.
    if(equipProbe&&(out.held&interaction::Fire)){
        const bool firearm=interaction::WeaponFeatureEnabled(coreProfile,interaction::WeaponFeature::TranslatedMuzzle);
        if(!firearm){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
    }
    // This isolated render test never fires, reloads or throws a grenade. All
    // cache writes use the already verified InputOverride path, including an
    // explicit false grenade override so native keyboard input cannot leak.
    if(weaponVisibilityProbe){
        constexpr auto blocked=interaction::Fire|interaction::AlternateFire|interaction::Reload|interaction::NextWeapon|interaction::PreviousWeapon;
        out.held&=~blocked;out.pressed&=~blocked;
    }
    const auto grenade=(bodyDraw.blockFire||weaponVisibilityProbe||magazineResult.blocksWeaponActions)?std::optional<bool>{false}:deathProbe?std::optional<bool>{input.hands[1].squeeze>=.75f}:std::nullopt;
    if(grenade&&*grenade)++grenadeSamples;
    std::optional<bool> cycle;
    if(equipProbe){const bool held=input.hands[1].aimTracked&&(input.hands[1].held&interaction::MenuClick);
        cycle=held&&!equipHeld&&out.active;equipHeld=held;if(*cycle)++equipCommands;}
    const auto queueMode=[&](const WeaponModeCommand& mode,std::uint64_t gesture){
        const auto index=modeRecordNext;modeRecordNext=(modeRecordNext+1)%unsigned(modeRecords.size());
        modeRecordCount=(std::min)(modeRecordCount+1,unsigned(modeRecords.size()));
        modeRecords[index]={GetTickCount64(),0,owner.soldier,owner.weak,owner.weapon,mode.targetWeapon,mode.action,rigEpoch,input.spaceGeneration,false,gesture};
        pendingMode=index;++modeRequests;
    };
    std::optional<EntryAction> weaponMode;
    if(physicalMode){
        weaponMode=static_cast<EntryAction>(physicalMode->action);
        queueMode(*physicalMode,physicalRequest);
    }
    if(bodyHolster&&holsterQueuedDraw){bodyDraw.command=holsterQueuedDraw;holsterQueuedDraw.reset();}
    std::optional<EntryAction> holsterEquipment;
    if(bodyDraw.command){
        // Repeat owner/route immediately before staging the one native edge;
        // an item replacement cannot inherit a queued shoulder contact.
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},
            [](void*,unsigned at,const char* expected){return Type(at,expected);}};
        Owner nowOwner{};const ReloadStateOwner bodyOwner{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto inventory=ReadBodyInventory(memory,bodyOwner,true);
        const auto command=inventory.snapshot?ResolveBodyDraw(*inventory.snapshot,bodyDraw.command->targetWeapon):std::nullopt;
        const bool authorityAllows=!physicalWeaponRuntime::Enabled()||
            !physicalWeaponRuntime::ReadActionAdmission(owner.weapon,owner.equipment.data).BlocksSelection();
        if(authorityAllows&&!weaponMode&&!pendingMode&&Resolve(nowOwner)&&nowOwner==owner&&command&&
            command->action==bodyDraw.command->action&&command->targetSlot==bodyDraw.command->targetSlot&&command->inventory==bodyDraw.command->inventory){
            if(bodyHolster&&bodyHolster->BlocksActions())holsterEquipment=static_cast<EntryAction>(command->action);
            else if(command->action==7)cycle=true;else weaponMode=static_cast<EntryAction>(command->action);
        }else {InvalidateBodyHolster(false,BodyHolsterLifecycleReason::SelectionRejected);
            // Rejected selection retires the request, not native item identity.
            // The next independent native observation proves replacement/loss.
            bodyInventory->SuspendInteraction();}
    }
    // Bounded diagnostic only. Production mode gestures will feed this same
    // adapter-resolved action after their own physical contact/ack policy.
    if(equipProbe){
        if(!out.active||!input.hands[0].gripTracked||!(input.hands[0].active&interaction::MenuClick))weaponModeHeld=true;
        else{
            const bool held=(input.hands[0].held&interaction::MenuClick)!=0;
            if(held&&!weaponModeHeld&&!pendingMode){
                const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},[](void*,unsigned at,const char* expected){return Type(at,expected);}};
                const auto mode=weaponModeBindingVerified?ResolveWeaponMode(memory,owner.soldier,owner.weapon):std::nullopt;
                if(mode){
                    weaponMode=static_cast<EntryAction>(mode->action);
                    queueMode(*mode,0);
                }else ++modeRejections;
            }
            weaponModeHeld=held;
        }
    }
    // The magazine can start after the early inventory sample. Do not allow
    // a same-tick shoulder draw to revoke its newly acquired gun prerequisite.
    if(holsterBodySample)holsterBodySample->reloadBusy|=magazineResult.blocksWeaponActions||
        (physicalWeaponRuntime::Enabled()&&(automaticReload.token||
            physicalWeaponRuntime::ReadActionAdmission(owner.weapon,owner.equipment.data).BlocksSelection()));
    interact.semanticHeld=(out.held&interaction::Use)!=0;interact.reloadBlocked=magazineResult.blocksWeaponActions;
    // Reproduce the actual soldier router's shared native context concept.
    // No alias is inferred from the X button, a nearby object or a past owner.
    bool contextUseVehicleAlias=false;
    if(out.active&&(out.held&interaction::Use)){
        const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        const VehicleRouteBinding binding{unsigned(image+profile.gameplay.managerVtable),unsigned(image+profile.gameplay.inputRouterVtable),unsigned(image+profile.cacheVtable),true};
        const auto routes=ReadOnFootRoutes(memory,binding,{owner.manager,owner.player,owner.soldier,owner.weak,rigEpoch,epoch});
        if(routes.status==VehicleRouteStatus::Okay&&HasOnFootContextUseVehicleAlias(routes.snapshot)&&
           routes.snapshot.identity.entry==owner.entry&&routes.snapshot.identity.router==owner.router&&routes.snapshot.identity.cache==owner.cache){
            contextUseVehicleAlias=true;interact.contextAliasVerified=true;interact.routeFingerprint=routes.snapshot.fingerprint;interact.actionMask|=1u<<16;
        }
        // Reading the router must not extend the input deadline or carry a
        // command across pickup/seat/actor replacement while resolving it.
        Owner current{};if(!QueryPerformanceCounter(&clock)||clock.QuadPart>=inputDeadline||!Resolve(current)||current!=owner||!Writable(owner.cache,InputBytes))return;
    }
    TestInputRow inputRow{};
    if(testInputLog){inputRow.tick=gatherTick;inputRow.action=out.held;
        inputRow.player=owner.player;inputRow.soldier=owner.soldier;inputRow.weak=owner.weak;inputRow.weapon=owner.weapon;
        inputRow.space=input.spaceGeneration;inputRow.actorGeneration=rigEpoch;inputRow.equipmentGeneration=renderEquipmentEpoch;
        inputRow.worldUnitsPerMeter=input.worldUnitsPerMeter;
        inputRow.desiredBodyAxes=desiredBodyAxes;inputRow.rightAimTracked=input.hands[1].aimTracked;
        if(movementYaw){inputRow.movementBasisValid=true;inputRow.movementYaw=*movementYaw;}
        for(unsigned n=0;n<3;++n)Read(unsigned(owner.cache)+(n==0?0x24u:n==1?0x98u:0x9cu),&inputRow.native[n],4);
        for(unsigned n=0;n<2;++n){std::uint32_t inventory=0;if(Read(unsigned(owner.soldier)+(n?0x24cu:0x248u),&inventory,4)&&inventory>=0x10000)Read(inventory+0x14cu,&inputRow.state[n],4);}}
    // With the server-side switch hooked, the stick no longer drives EiaSwitchPrimaryWeapon
    // (the client pulse never reaches the authoritative selector); ResolveStickSwitch
    // latched the request at the start of this gather.
    auto appliedOut=out;if(weaponSwitchHooked){appliedOut.pressed&=~(interaction::NextWeapon|interaction::PreviousWeapon);appliedOut.held&=~(interaction::NextWeapon|interaction::PreviousWeapon);}
    if(scope->patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},appliedOut,grenade,cycle,weaponMode,contextUseVehicleAlias)){++applied;if(out.pressed&interaction::NextWeapon)++nextWeaponCommands;if(out.pressed&interaction::PreviousWeapon)++previousWeaponCommands;if(out.forward||out.strafe)++moving;if(out.held)++buttons;
        // Do not restore at the end of player update: BC2 also consumes this
        // buffer afterward. The next original gather replaces every action.
        std::optional<HolsterSuppressionReceipt> holsterReceipt;
        BodyHolsterSample holsterSample;
        if(bodyHolster){
            holsterSample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
            holsterSample.hand=handSample;LARGE_INTEGER holsterNow{};
            if(QueryPerformanceCounter(&holsterNow))holsterSample.hand.nowNs=HandNanos(holsterNow.QuadPart);
            else holsterSample.cancel=true;
            holsterSample.nativeTick=scope->nativeTick;holsterSample.cache=owner.cache;
            holsterSample.equipment=owner.equipment;
            holsterSample.ordinaryPair=rigPublication::ReadOrdinaryEquipmentPair(holsterSample.nativeOwner,bodyHolster->RequestId(),holsterSample.hand.nowNs);
            holsterSample.selected=selectedMeshes.Read(holsterSample.nativeOwner,holsterSample.hand.nowNs);
            holsterSample.visibility=rigPublication::ReadWeaponVisibilityReceipt(holsterSample.nativeOwner,bodyHolster->RequestId(),holsterSample.hand.nowNs);
            if(const auto request=bodyHolster->Demand(holsterSample)){HolsterInputOverride patch;
                BodyHolsterCacheChallenge challenge;
                if(bodyHolsterFixture&&!holsterEquipment){
                    LARGE_INTEGER now{};QueryPerformanceCounter(&now);const auto ns=HandNanos(now.QuadPart);
                    const auto previous=ReadBodyHolsterProbe(ns);
                    if(previous&&bodyHolsterFixture->ChallengeWanted(*previous,ns)&&previous->outcome.freeRight)
                        challenge.Stage({reinterpret_cast<std::byte*>(owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent},
                            *previous->outcome.freeRight,handOwnership,previous->trialDeadlineNs);
                }
                if(patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent},holsterEquipment))holsterReceipt=patch.Commit();
                if(challenge.Evidence().staged){challenge.Complete(holsterReceipt);LARGE_INTEGER now{};QueryPerformanceCounter(&now);
                    bodyHolsterFixture->RecordChallenge(challenge.Evidence(),HandNanos(now.QuadPart));
                    if(!challenge.Evidence().committed)holsterSample.cancel=true;}
                if(holsterReceipt)++holsterSuppressionCommits;else {++holsterSuppressionFailures;holsterSample.cancel=true;}}
        }
        std::optional<HolsterSuppressionReceipt> detachedSuppression;std::int64_t detachedCommittedNs=0;bool detachedDemanded=false;
        if(detachedUsed){if(const auto request=magazineDetached->Demand()){detachedDemanded=true;HolsterInputOverride patch;
            if(patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent}))detachedSuppression=patch.Commit();
            LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);detachedCommittedNs=HandNanos(stamp.QuadPart);
        }}
        scope->patch.Commit();captureInputScope.CommitSynthetic(inputDeadline);++committed;interact.committed=true;
        if(testInputLog){for(unsigned n=0;n<3;++n){const bool read=Read(unsigned(owner.cache)+(n==0?0x24u:n==1?0x98u:0x9cu),&inputRow.committed[n],4);
                if(n==1)inputRow.committedButtonsValid=read;}
            inputRow.submittedAxesValid=Read(unsigned(owner.cache)+8,inputRow.submittedNativeAxes.data(),8)&&
                std::isfinite(inputRow.submittedNativeAxes[0])&&std::isfinite(inputRow.submittedNativeAxes[1]);
            unsigned slot=0;
            if(!testInputCount||!inputRow.SameCommand(testInputLast)){
                slot=testInputCount++%testInputRows.size();testInputRows[slot]=inputRow;testInputLast=inputRow;
            }else slot=(testInputCount-1)%testInputRows.size();
            auto& row=testInputRows[slot];
            if(!row.firstInput){row.firstInput=input.generation;row.beforeQpc=scope->testBeforeQpc;row.positionBefore=scope->testPositionBefore;}
            row.lastInput=input.generation;scope->testInputSlot=slot;
        }
        if(detachedUsed){
            const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
            LARGE_INTEGER before{};QueryPerformanceCounter(&before);
            const auto pair=rigPublication::ReadMagazineDetachPair(current,HandNanos(before.QuadPart));
            const auto postReserve=reloadFlowRuntime::ReadReserve();
            LARGE_INTEGER after{};QueryPerformanceCounter(&after);const auto now=HandNanos(after.QuadPart);
            magazineResult=magazineDetached->Commit(detachedSuppression,pair,postReserve,handOwnership,handIntent,detachedCommittedNs,now);
            if(holsterTracking){holsterTracking->magazine=magazineResult.tracking;holsterTracking->weaponActionsBlocked=magazineResult.blocksWeaponActions;}
            if(magazineDetachedFixture)magazineDetachedFixture->Observe(*magazineDetached,magazineResult,detachedSuppression,pair,
                postReserve,rigPublication::ReadMagazinePackCounters(),detachedDemanded,now);
        }
        if(magazineDetached&&!bodyHolster){if(holsterTracking)PublishRecoilObservedTracking(*holsterTracking);else PublishRecoilObservedTracking({});}
        // All inventory/claim transitions occur AFTER the actual current native
        // cache commit. Native consumes the equipment pulse later on this tick.
        if(bodyHolster&&bodyInventory&&holsterBodySample){
            holsterSample.suppression=holsterReceipt;
            // Refresh processing time only, preserving original input identity,
            // observation/deadline and all controller poses consumed by contact.
            LARGE_INTEGER holsterNow{};if(QueryPerformanceCounter(&holsterNow))holsterBodySample->hand.nowNs=HandNanos(holsterNow.QuadPart);
            else holsterSample.cancel=true;
            const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
            auto holsterOutcome=bodyInventory->TickHolster(memory,*holsterBodySample,holsterSample,*bodyHolster,handOwnership,handIntent);
            if(holsterOutcome.blockWeaponActions){std::lock_guard lock(shotPoseMutex);shotHistory.Reset();}
            if(holsterOutcome.select){
                // A holstered item remains native-selected. Drawing that exact
                // item needs fresh show proof, not a redundant equip command.
                if(holsterOutcome.select->id!=holsterSample.nativeOwner.weapon){
                    const auto inventory=ReadBodyInventory(memory,holsterSample.nativeOwner,true);
                    if(inventory.snapshot)holsterQueuedDraw=ResolveBodyDraw(*inventory.snapshot,unsigned(holsterOutcome.select->id));
                    if(!holsterQueuedDraw)InvalidateBodyHolsterState(BodyHolsterLifecycleReason::SelectionRejected);}
            }
            else if(holsterOutcome.ordinaryDraw.command)holsterQueuedDraw=holsterOutcome.ordinaryDraw.command;
            rigPublication::PublishWeaponVisibility(holsterOutcome.visibility);
            rigPublication::PublishOrdinaryEquipment(holsterOutcome.ordinaryRecovery);
            if(holsterTracking){holsterTracking->weaponActionsBlocked=holsterOutcome.blockWeaponActions||magazineResult.blocksWeaponActions;
                if(holsterOutcome.freeRight){try{holsterTracking->freeRight=std::make_shared<const BodyFreeRightEvidence>(*holsterOutcome.freeRight);
                    holsterTracking->bodyHolsterSlot=bodyInventory->AssignedSlot(holsterSample.nativeOwner.weapon);
                    holsterTracking->bodyAnchors=bodyAnchors;++holsterFreePublications;
                }catch(...){InvalidateBodyHolsterState(BodyHolsterLifecycleReason::FreePoseAllocationFailed);}}
                holsterTracking->bodyAnchors=bodyAnchors;
                if(const auto display=bodyInventory->Display(holsterBodySample->hand.nowNs)){
                    try {holsterTracking->bodyInventoryDisplay=std::make_shared<const BodyInventoryDisplay>(*display);
                        for(unsigned n=0;n<display->count;++n){const auto& slot=display->slots[n];
                            if(slot.assignment.item==display->physicalSelected||slot.native.weapon==display->selectedOwner.weapon)continue;
                            const auto equipment=ReadWeaponEquipmentIdentity(memory,slot.native.weapon);
                            if(!equipment||equipment->data!=slot.native.data||equipment->persistence!=slot.native.persistence)continue;
                            // Only existing installed render-only geometry profiles. No
                            // category/name grants native reload, hide or shot capability.
                            if(std::none_of(BodyEquipmentProfiles.begin(),BodyEquipmentProfiles.end(),[&](const auto& p){return equipment->Asset()==p.asset;}))continue;
                            holsterTracking->bodyCarriedMeshes[n]=selectedMeshes.ReadCarried(display->selectedOwner,display->carried.inventory,
                                slot.native.slot,slot.native.weapon,slot.native.data,slot.native.persistence,display->sequence,
                                display->observedNs,holsterBodySample->hand.nowNs);
                        }
                    }catch(...){holsterTracking->bodyInventoryDisplay.reset();holsterTracking->bodyCarriedMeshes={};}
                }
                PublishRecoilObservedTracking(*holsterTracking);}
            else {InvalidateBodyHolsterState(BodyHolsterLifecycleReason::TrackingPublicationUnavailable);PublishRecoilObservedTracking({});}
            if(bodyHolster->DiagnosticDeadline()){
                try{auto sample=std::make_shared<BodyHolsterProbeSample>();LARGE_INTEGER now{};QueryPerformanceCounter(&now);
                    sample->sampledNs=HandNanos(now.QuadPart);sample->trialStartNs=bodyHolster->DiagnosticStart();sample->trialDeadlineNs=bodyHolster->DiagnosticDeadline();
                    sample->nativeOwner=holsterSample.nativeOwner;sample->input=input;sample->hand=holsterBodySample->hand;
                    sample->physicalGun=holsterBodySample->gun;
                    if(holsterFireFixture){sample->fireTickMs=GetTickCount64();sample->fireRequested=(out.held&interaction::Fire)!=0;
                        sample->fireCacheRead=Read(owner.cache+8+4*unsigned(EntryAction::Fire),&sample->fireCache,4);}
                    sample->nativeTick=scope->nativeTick;sample->request=bodyHolster->RequestId();sample->phase=bodyHolster->Phase();
                    sample->selectedSlot=bodyInventory->AssignedSlot(holsterSample.nativeOwner.weapon);sample->anchors=bodyAnchors;
                    sample->left=handOwnership.Current(interaction::InteractionHand::Left);sample->right=handOwnership.Current(interaction::InteractionHand::Right);
                    sample->outcome=holsterOutcome;sample->visibility=holsterSample.visibility;sample->suppression=holsterReceipt;
                    sample->queuedTarget=holsterQueuedDraw?holsterQueuedDraw->targetWeapon:0;sample->pack=rigPublication::ReadBodyHolsterPackCounters();
                    bodyHolsterSample.store(std::move(sample),std::memory_order_release);
                    if(bodyHolsterFixture){LARGE_INTEGER after{};QueryPerformanceCounter(&after);const auto ns=HandNanos(after.QuadPart);
                        bodyHolsterFixture->Observe(ReadBodyHolsterProbe(ns),ns);
                        bodyHolsterFixtureSample.store(std::make_shared<const BodyHolsterFixtureSample>(bodyHolsterFixture->Sample()),std::memory_order_release);}
                }catch(...){bodyHolsterSample.store({},std::memory_order_release);InvalidateBodyHolsterState(BodyHolsterLifecycleReason::ProbePublicationFailed);}
            }
        }
        if(weaponVisibilityProbe){
            float fire=-1;std::array<unsigned,2> bits{};
            bool quiet=Read(owner.cache+8+4*unsigned(EntryAction::Fire),&fire,4)&&fire==0&&Read(owner.cache+0x98,bits.data(),8);
            for(const auto action:{EntryAction::Zoom,EntryAction::Reload,EntryAction::ThrowGrenade}){
                const auto bit=unsigned(action);quiet&=(bits[bit/32]&(1u<<(bit%32)))==0;
            }
            if(quiet)++weaponVisibilityActionCommits;else ++weaponVisibilityActionFailures;
        }
    }
    else{
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        // Preserve only a registered exact same-owner timeout transition while
        // ControllerActions is crossing its neutral rearm barrier. No native
        // commit, pose or old receipt is accepted on this failed input Apply.
        const bool rearm=bodyHolster&&BodyHolsterInputRearm(*bodyHolster,current,out,
            grenade.value_or(false)||cycle.value_or(false)||bool(weaponMode)||bool(pendingMode)||bool(bodyDraw.command));
        physicalCancelSite=3;CancelPhysicalReload();InvalidateBodyHolster(rearm,rearm?BodyHolsterLifecycleReason::InputRearm:BodyHolsterLifecycleReason::InputApplyRejected);
        if(bodyInventory)bodyInventory->SuspendInteraction();if(weaponMode&&pendingMode){modeRecords[*pendingMode].cancelled=true;pendingMode.reset();--modeRequests;++modeRejections;}
    }
}
void __fastcall UpdateHook(void* self,void*,float dt){
    Callback callback;++updates;
    AimAngleScopeRestore traceRestore{aimAngleTraceRequested};
    Scope local{};
    if(!enabled.load(std::memory_order_acquire)||scope){updateOriginal(self,dt);return;}
    if(!Resolve(local.owner)){
        // Only the input thread may own the loss marker it later consumes.
        if(inputThread.load()==GetCurrentThreadId())ObserveLocalActorLoss();
        updateOriginal(self,dt);return;
    }
    if(local.owner.player!=reinterpret_cast<unsigned>(self)){updateOriginal(self,dt);return;}
    const auto thread=GetCurrentThreadId();unsigned expected=0;inputThread.compare_exchange_strong(expected,thread);
    if(inputThread.load()!=thread){++mismatches;updateOriginal(self,dt);return;}
    ++localUpdates;if(local.owner.foot)++onFoot;else ++vehicle;
    const auto now=GetTickCount64();
    // Death and checkpoint reload can rebuild the pawn (and its server
    // inventory/items) at recycled addresses, so pointer comparison alone keeps
    // the previous life's reload ledger, reload owner and hand state. A
    // structurally observed loss of the local soldier always starts a new
    // actor generation on the next successful resolve.
    const bool lossRebind=localActorLostAt!=0;
    if(lossRebind){
        lastActorLossMs.store(unsigned((std::min<ULONGLONG>)(now-localActorLostAt,0xffffffffull)),std::memory_order_relaxed);
        if(now-localActorLostAt<ActorLossRebindMs)actorLossTransient.fetch_add(1,std::memory_order_relaxed);
        actorLossRebinds.fetch_add(1,std::memory_order_relaxed);localActorLostAt=0;
    }
    const bool actorChanged=lossRebind||local.owner.manager!=lastOwner.manager||local.owner.player!=lastOwner.player||local.owner.soldier!=lastOwner.soldier||local.owner.weak!=lastOwner.weak||local.owner.controlled!=lastOwner.controlled||local.owner.entry!=lastOwner.entry;
    if(actorChanged)++rigEpoch;
    if(lossRebind||local.owner.player!=lastOwner.player||local.owner.soldier!=lastOwner.soldier||local.owner.weak!=lastOwner.weak)
        ++physicalActorGeneration;
    physicalWeaponRuntime::PublishActor({local.owner.player,local.owner.soldier,local.owner.weak,physicalActorGeneration});
    if(actorChanged||local.owner.equipment!=lastOwner.equipment)++renderEquipmentEpoch;
    // Resolve succeeded on the verified input owner. A requested button,
    // input epoch change or missing read is not an observed vehicle exit.
    // A recycled pawn must re-enter the automatic stow/draw path as a new actor.
    if(lossRebind&&bodyInventory)bodyInventory->ObserveNativeActor(local.owner.manager,local.owner.player,local.owner.soldier,local.owner.weak,false);
    if(bodyInventory)bodyInventory->ObserveNativeActor(local.owner.manager,local.owner.player,local.owner.soldier,local.owner.weak,local.owner.foot);
    // Item changes still re-arm semantic actions, but cannot recalibrate anatomy.
    // A pause timeout advances only the input observation epoch when the full
    // resolved native owner is unchanged. It is not a physical draw request.
    if(bodyHolster&&local.owner==lastOwner&&local.owner.foot&&now-lastTick>150&&epoch&&epoch<UINT64_MAX){
        const ReloadStateOwner before{local.owner.player,local.owner.soldier,local.owner.weak,local.owner.weapon,rigEpoch,epoch,handSample.owner.space};
        auto after=before;++after.equipGeneration;const auto previous=bodyHolster->Phase();
        const bool accepted=bodyHolster->ObserveNativeInputGap(before,after);auto source=handSample;
        LARGE_INTEGER stamp{};if(QueryPerformanceCounter(&stamp))source.nowNs=HandNanos(stamp.QuadPart);
        bodyHolsterLifecycle.Observe(accepted?BodyHolsterLifecycleReason::NativeInputGapAccepted:BodyHolsterLifecycleReason::NativeInputGapRejected,
            accepted,previous,bodyHolster->Phase(),source,after.equipGeneration);
    }
    if(lossRebind||local.owner!=lastOwner||now-lastTick>150){++epoch;lastOwner=local.owner;}lastTick=now;
    struct Current {Scope*& slot;Scope* previous;~Current(){slot=previous;}} current{scope,scope};scope=&local;
    std::optional<AimAngleTransaction> traceRecord;
    if(aimAngleTraceRequested&&local.owner.foot&&local.owner.weapon){
        auto& record=traceRecord.emplace();record.owner=local.owner;record.update=localUpdates.load();
        record.actorGeneration=rigEpoch;record.equipmentGeneration=renderEquipmentEpoch;record.dt=dt;
        aimAngleScope=&record;SampleAimAngles(AimAngleStage::BeforeUpdate);
    }
    if(testInputLog&&local.owner.foot){
        LARGE_INTEGER stamp{};if(QueryPerformanceCounter(&stamp))local.testBeforeQpc=stamp.QuadPart;
        Owner current{};math::Vec3 position{};
        if(bodyFollow&&Resolve(current)&&current==local.owner&&BodyPosition(current,position))local.testPositionBefore=position;
    }
    updateOriginal(self,dt);
    if(traceRecord){SampleAimAngles(AimAngleStage::AfterUpdate);CompleteAimAngles(*traceRecord);}
    if(testInputLog&&local.testInputSlot){
        auto& row=testInputRows[*local.testInputSlot];LARGE_INTEGER stamp{};
        row.afterQpc=QueryPerformanceCounter(&stamp)?stamp.QuadPart:0;++row.completedUpdates;
        Owner current{};math::Vec3 position{};row.positionAfter.reset();
        row.positionOwnerCurrent=rigEpoch==row.actorGeneration&&renderEquipmentEpoch==row.equipmentGeneration&&
            Resolve(current)&&current==local.owner&&current.foot;
        if(bodyFollow&&row.positionOwnerCurrent&&BodyPosition(current,position))row.positionAfter=position;
        unsigned flags=0;row.afterUpdateSprintValid=row.positionOwnerCurrent&&Read(current.cache+0x98,&flags,4);
        row.afterUpdateSprint=row.afterUpdateSprintValid&&(flags&(1u<<31));
    }
}
std::optional<std::size_t> Offset(const engine::PeImage& pe,unsigned rva,unsigned n){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva))return std::size_t(s.rawOffset)+rva-s.rva;return {};}
}
bool EnableSelectedMeshesObservation(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,bool persistent)noexcept {
    if(enabled.load()||!reloadStateBinding||base!=image||base>UINT32_MAX)return false;
    const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},nullptr};
    return selectedMeshes.Install(bytes,pe,unsigned(base),memory,true,persistent);
}
bool EnablePhysicalReloadProbeRepeat()noexcept {
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
    return false; // Separate bounded lifecycle fixture; no two-shell mode combination.
#endif
    if(enabled.load()||bodyInventory||!physicalReloadMode||!physicalReload||!physicalReloadFixture)return false;
    physicalReloadFixture.emplace(true,2);return true;
}
bool EnableEmptyFireProbe()noexcept {
    if(enabled.load()||emptyFireFixture||!hooks[0]||!physicalReloadMode||!magazinePhysical||
       physicalReloadFixture||magazinePhysicalFixture||magazineDetachedFixture||reloadRequestFixture||
       bodyInventory||weaponVisibilityProbe||deathProbe||equipProbe||sightFlip)return false;
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(physicalReloadFixture||armingEmptyFixture)return false;
    armingEmptyFixture.emplace();
#else
    emptyFireFixture.emplace();
#endif
    return true;
}
bool EnableWeaponVisibilityProbe()noexcept {
    if(enabled.load()||weaponVisibilityProbe||!hooks[0]||!twoHandGrip||!handPoses||!bodyFollow||!motionAim||
        bodyInventory||physicalReloadMode||physicalReloadFixture||reloadRequestFixture||deathProbe||equipProbe||sightFlip)return false;
    weaponVisibilityProbe.emplace(true);return true;
}
std::shared_ptr<const WeaponVisibilityProbeSample> ReadWeaponVisibilityProbe(std::int64_t now)noexcept {
    const auto sample=weaponVisibilitySample.load(std::memory_order_acquire);
    if(!enabled.load()||!sample||sample->sampledNs<=0||sample->sampledNs>now||now-sample->sampledNs>150000000)return {};
    if(sample->intent.enabled&&sample->intent.input.deadlineNs<=now)return {};
    return sample;
}
namespace {
bool VerifyHolsterActions()noexcept {
    // Existing DiscoverInputBinding proved gather/setters/table; verify every
    // additional live enum row before admitting the broader suppression mask.
    for(const auto [id,name]:{std::pair{7u,"EiaSwitchPrimaryWeapon"},std::pair{8u,"EiaFire"},std::pair{12u,"EiaAltFire"},std::pair{14u,"EiaZoom"},
        std::pair{29u,"EiaReload"},std::pair{33u,"EiaGrenadeLauncher"},std::pair{36u,"EiaDynamicGadget2"},std::pair{37u,"EiaMeleeAttack"},std::pair{38u,"EiaThrowGrenade"}}){
        const auto row=unsigned(image+profile.entryActions+id*24);std::array<char,32> value{};
        if(U32(row+16)!=id||std::strlen(name)+1>value.size()||!Read(U32(row),value.data(),std::strlen(name)+1)||std::strcmp(value.data(),name))return false;
    }
    return true;
}
bool HolsterSetupAvailable()noexcept {
    return !enabled.load()&&!bodyHolster&&bodyInventory&&!weaponVisibilityProbe&&!physicalReloadFixture&&!reloadRequestFixture&&!deathProbe&&!equipProbe;
}
}
bool EnableBodyHolsters(BodyHolsterCapabilities acceptance)noexcept {
    if(!HolsterSetupAvailable()||!acceptance.nativeVisibilityAccepted||!acceptance.nativeInputSuppressionAccepted||
        !acceptance.acceptedProfileMask||(acceptance.acceptedProfileMask&~3u)||!VerifyHolsterActions())return false;
    bodyHolster.emplace(acceptance);bodyInventory->EnableAutomaticStow();return true;
}
bool EnableBodyHolsterFixture(BodyHolsterDiagnosticProfile diagnosticProfile)noexcept {
    if(bodyHolsterFixture||!EnableBodyHolsterDiagnostic(15000,diagnosticProfile))return false;
    bodyHolsterFixture.emplace(true,diagnosticProfile);bodyHolsterFixtureSample.store({},std::memory_order_release);return true;
}
std::shared_ptr<const BodyHolsterFixtureSample> ReadBodyHolsterFixture(std::int64_t now)noexcept {
    const auto s=bodyHolsterFixtureSample.load(std::memory_order_acquire);
    if(!enabled.load()||!s||!s->actual||s->sampledNs<=0||s->sampledNs>now||now-s->sampledNs>150000000||
        s->actual->hand.observedNs<=0||s->actual->hand.observedNs>now||s->actual->hand.deadlineNs<=now)return {};
    return s;
}
bool EnableBodyHolsterDiagnostic(std::uint32_t lifetimeMs,BodyHolsterDiagnosticProfile diagnosticProfile)noexcept {
    if(!HolsterSetupAvailable()||!lifetimeMs||lifetimeMs>15000||!VerifyHolsterActions())return false;
    LARGE_INTEGER clock{};if(!QueryPerformanceCounter(&clock))return false;
    const auto now=HandNanos(clock.QuadPart),duration=std::int64_t(lifetimeMs)*1000000;
    if(now<=0||now>INT64_MAX-duration)return false;
    Bc2BodyHolster candidate; // Production acceptance bits remain false/profile0.
    if(!candidate.AdmitDiagnostic(now,now+duration,diagnosticProfile))return false;
    bodyHolster.emplace(std::move(candidate));bodyHolsterSample.store({},std::memory_order_release);return true;
}
std::shared_ptr<const BodyHolsterProbeSample> ReadBodyHolsterProbe(std::int64_t now)noexcept {
    const auto sample=bodyHolsterSample.load(std::memory_order_acquire);
    if(!enabled.load()||!sample||sample->sampledNs<=0||sample->sampledNs>now||now-sample->sampledNs>150000000||
        sample->hand.observedNs<=0||sample->hand.observedNs>now||sample->hand.deadlineNs<=now)return {};
    return sample; // Original source/trial bounds remain immutable, including expiry.
}
bool EnableBoatHeadAim(bool enableFire)noexcept {
    if(enabled.load(std::memory_order_acquire)||!hooks[0]||!hooks[1]||boatHeadAim)return false;
    const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
    const auto verified=VerifyBoatAimCode(memory,unsigned(image));if(!verified.verified)return false;
    boatAimBinding=verified;boatHeadFire=enableFire;boatHeadAim=true;return true;
}
bool EnableBodyInventory(interaction::BodyAnchorConfig config)noexcept {
    if(enabled.load()||bodyInventory||!hooks[0]||!twoHandGrip||!bodyFollow||!motionAim||!handPoses||!weaponModeBindingVerified||
        deathProbe||equipProbe||physicalReloadFixture||reloadRequestFixture||!physicalReloadMode||!physicalReload||!interaction::ValidBodyAnchors(config))return false;
    config=bc2::ZonedBodyAnchors(config,interactionZones[interaction::InteractionZoneKind::Holster]);
    if(!interaction::ValidBodyAnchors(config))return false;
    bodyAnchors=config;bodyInventory.emplace(true,config);
    // Before Start there are no items/reservations/cycles to discard. Existing
    // fixture/legacy sessions retain DefaultPouch and its original transform.
    if(physicalReload){physicalReload.emplace(true,physicalReloadApi,interaction::ChestAmmoSupply(config));physicalReload->EnableBeltAmmo(true,interaction::SupplyAnchorFrame::RecenteredBody);
        physicalTube.emplace(true,interaction::ChestAmmoSupply(config));}
    if(magazinePhysical){auto pouch=interaction::ChestAmmoSupply(config);
        const auto magazinePouch=Bc2MagazinePhysicalReload::DefaultPouch();pouch.itemNamespace=magazinePouch.itemNamespace;pouch.pouch=magazinePouch.pouch;
        magazinePhysical.emplace(true,magazinePhysicalApi,pouch);magazinePhysical->EnableBodyAmmo(reloadGuidanceEnabled,interaction::SupplyAnchorFrame::RecenteredBody);
        magazinePhysical->ConfigureZones(interactionZones);magazinePhysical->EnableShortRackAfterInsert(shortRackAfterInsert);
        magazinePhysical->EnableGrabRemoval(zoneSettings.magazineGrabRemove);
        // C2 rack: after an empty reload, fire waits for the handle stroke or
        // the bolt-release press (on by default; =0 opts out).
        magazinePhysical->EnableChamberRack(ChamberRackDefault());}
    return true;
}
bool BodyDisplayNativeCurrent(const BodyInventoryDisplay& d)noexcept {
    const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
    const auto current=ReadBodyInventory(memory,d.selectedOwner,true);
    if(!current.snapshot)return false;const auto& n=*current.snapshot;
    return n.owner==d.selectedOwner&&BodyCarriedIdentity{n.inventory,n.switching,n.count,n.items}==d.carried;
}
std::shared_ptr<const SelectedMeshesSnapshot> ReadSelectedMeshes(const ReloadStateOwner& owner,std::int64_t now)noexcept {
    return selectedMeshes.Read(owner,now);
}
std::shared_ptr<const SelectedMeshesSnapshot> ReadCurrentSelectedMeshes(std::int64_t now)noexcept {
    return selectedMeshes.ReadCurrent(now);
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,InputReader reader,bool enableMotionAim,bool enableBodyFollow,bool enablePoseObservation,bool enableRigPulse,bool enableHandPoses,bool enableDeathProbe,bool enableEquipProbe,bool enableMuzzleFire,bool enableTwoHandGrip,bool enableSightFlip,bool reloadHoldProbe,bool reloadRoundProbe,bool reloadRequestProbe,bool enablePhysicalReload,bool enablePhysicalReloadProbe,unsigned magazineReloadSession){
    if(hooks[0]||hooks[1]||!reader)return false;const auto found=DiscoverInputBinding(bytes,pe);if(!found)return false;
    if(!testRangeHandoff::Prepare())return false;
    aimAngleTraceRequested=ProcessEnvText("FVR_BC2_AIM_ANGLE_TRACE")=="1";
    if(aimAngleTraceRequested)aimAngleRecords.resize(AimAngleCapacity);
    sightFlip=enableSightFlip;if(sightFlip&&!enableTwoHandGrip)return false;
    LARGE_INTEGER ownershipClock{};if(!QueryPerformanceFrequency(&ownershipClock)||ownershipClock.QuadPart<=0)return false;handFrequency=ownershipClock.QuadPart;
    twoHandGrip=enableTwoHandGrip;if(twoHandGrip&&(!enableMuzzleFire||!enableHandPoses))return false;
    equipProbe=enableEquipProbe;muzzleFire=enableMuzzleFire;if((equipProbe||muzzleFire)&&!enableMotionAim)return false;if(muzzleFire&&!enableHandPoses)return false;
    deathProbe=enableDeathProbe;if(deathProbe&&!enableMotionAim)return false;
    motionAim=enableMotionAim;bodyFollow=enableBodyFollow;observePoses=enablePoseObservation;handPoses=enableHandPoses;if((bodyFollow||observePoses)&&!motionAim)return false;profile=*found;const auto aim=DiscoverAiming(bytes,pe);if(!aim)return false;aimingProfile=*aim;image=base;imageSize=pe.imageSize;readInput=reader;
    if(unsigned(reloadHoldProbe)+unsigned(reloadRoundProbe)+unsigned(reloadRequestProbe)>1||((reloadHoldProbe||reloadRoundProbe||reloadRequestProbe)&&!handPoses)||
        (reloadRequestProbe&&!twoHandGrip))return false;
    if(enablePhysicalReload&&(reloadHoldProbe||reloadRoundProbe||reloadRequestProbe||enableRigPulse||enableDeathProbe||enableEquipProbe||!twoHandGrip||!handPoses))return false;
    if(enablePhysicalReloadProbe&&!enablePhysicalReload)return false;
    if(enablePhysicalReloadProbe){
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
        physicalReloadFixture.emplace(true,1,static_cast<PhysicalReloadLifecycleScenario>(FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO));
#else
        // FVR_BC2_PROBE_SPAS_ROUNDS=1..4 shells; FVR_BC2_PROBE_CANCEL=between|arming|after-seat;
        // FVR_BC2_PROBE_SPAS_FIRE_AFTER=n (regression legs, test only).
        unsigned rounds=1;if(char value[8]{};GetEnvironmentVariableA("FVR_BC2_PROBE_SPAS_ROUNDS",value,sizeof(value))&&value[0]>='1'&&value[0]<='4')rounds=unsigned(value[0]-'0');
        physicalReloadFixture.emplace(true,rounds);
        if(char value[24]{};GetEnvironmentVariableA("FVR_BC2_PROBE_CANCEL",value,sizeof(value))){const std::string_view v=value;
            physicalReloadFixture->ForceCancel(v=="between"?1u:v=="arming"?2u:v=="after-seat"?3u:0u);}
        if(char value[8]{};GetEnvironmentVariableA("FVR_BC2_PROBE_SPAS_FIRE_AFTER",value,sizeof(value))&&value[0]>='1'&&value[0]<='3')physicalReloadFixture->FireAfter(unsigned(value[0]-'0'));
        if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_PROBE_SPAS_PUMP_AFTER",value,sizeof(value))&&value[0]=='1')physicalReloadFixture->PumpAfter();
        if(char value[8]{};GetEnvironmentVariableA("FVR_BC2_PROBE_SPAS_ABANDON_AFTER",value,sizeof(value))&&value[0]>='1'&&value[0]<='3')physicalReloadFixture->AbandonAfter(unsigned(value[0]-'0'));
        // FVR_BC2_PROBE_SPAS_GRAB_AFTER_SHOT_MS=n: grab n ms after the last prep shot, inside the pump.
        if(char value[8]{};GetEnvironmentVariableA("FVR_BC2_PROBE_SPAS_GRAB_AFTER_SHOT_MS",value,sizeof(value))){unsigned ms=0;if(std::from_chars(value,value+std::strlen(value),ms).ec==std::errc{}&&ms<=3000)physicalReloadFixture->GrabAfterShot(ms);}
#endif
    }
    const bool magazinePhysicalProbe=magazineReloadSession==4||magazineReloadSession==5||magazineReloadSession==7||magazineReloadSession==8;
    const bool magazineNormal=magazineReloadSession==3||magazinePhysicalProbe||magazineReloadSession==6;
    const bool magazineProbe=magazineReloadSession==1||magazineReloadSession==2;
    if(magazineReloadSession>8||((magazinePhysicalProbe||magazineReloadSession==6)&&(enableSightFlip||reloadHoldProbe||reloadRoundProbe||reloadRequestProbe||enableRigPulse||enableDeathProbe||enableEquipProbe))||
       (magazineNormal&&(!enablePhysicalReload||enablePhysicalReloadProbe))||
       (magazineProbe&&(reloadHoldProbe||reloadRoundProbe||reloadRequestProbe||enablePhysicalReload||enablePhysicalReloadProbe||enableSightFlip||enableRigPulse||enableDeathProbe||enableEquipProbe||!handPoses||!twoHandGrip)))return false;
    physicalReloadMode=enablePhysicalReload;reloadRequestMode=reloadRequestProbe||physicalReloadMode||magazineReloadSession!=0;
    if(handPoses||observePoses){
        reloadStateBinding=DiscoverReloadState(bytes,pe);
        const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        if(reloadStateBinding&&!ValidateReloadStateLive(memory,*reloadStateBinding,unsigned(base)))reloadStateBinding.reset();
        if(physicalReloadMode&&!physicalWeaponRuntime::Install(bytes,pe,unsigned(base),memory))return false;
        const bool flowInstalled=reloadStateBinding&&reloadFlowRuntime::Install(bytes,pe,base,memory,reloadHoldProbe,reloadRoundProbe,reloadRequestMode&&!magazineProbe,magazineProbe);
        if((reloadHoldProbe||reloadRoundProbe||reloadRequestMode)&&!flowInstalled)return false;
        if(magazineNormal&&!reloadFlowRuntime::EnableMagazineRequestCycles())return false;
        if(physicalReloadMode){
            PhysicalReloadApi api;
            api.reserve=[](void*)noexcept{return reloadFlowRuntime::ReadReserve();};
            api.identity=[](void*)noexcept{return reloadFlowRuntime::RequestIdentity();};
            api.start=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::StartRequestCycle(c);};
            api.keep=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycle(c);};
            api.keepObserved=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycleObserved(c);};
            api.reserveObserved=[](void*)noexcept{return reloadFlowRuntime::ReadReserveObserved();};
            api.lease=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RequestLease(id,cycle);};
            api.submit=[](void*,const Bc2ReloadNativeRequest& request)noexcept{const bool submitted=reloadFlowRuntime::SubmitRequest(request);
                if(submitted)gunFvr.Seat(GunEvents(),gunWeapon,request.heldLease.cycle,1);return submitted;};
            api.ack=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{auto evidence=reloadFlowRuntime::TakeRequestAcknowledgement(id,cycle);
                // The shell receipt carries no counts: report the server copy's last observed values.
                GunCopyState server;if(evidence&&GunEvents().Enabled()&&GunNativeTracker().Last(GunCopy::Server,server))gunFvr.Credit(GunEvents(),gunWeapon,cycle,server.loaded,server.reserve);
                return evidence;};
            api.cancel=[](void*)noexcept{reloadFlowRuntime::CancelRequestCycle();};
            api.retire=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RetireRequestCycle(id,cycle);};
            api.clock=[](void*)noexcept{LARGE_INTEGER now{};QueryPerformanceCounter(&now);return HandNanos(now.QuadPart);};
            physicalReloadApi=api;physicalReload.emplace(true,api);physicalTube.emplace(true);if(!enablePhysicalReloadProbe)physicalReload->EnableBeltAmmo();
            pumpEnabled=!enablePhysicalReloadProbe&&reloadFlowRuntime::EnablePumpHold(true);
        }
        if(magazineNormal){
            MagazinePhysicalApi api;
            api.reserve=[](void*)noexcept{return reloadFlowRuntime::ReadReserve();};
            api.identity=[](void*)noexcept{return reloadFlowRuntime::RequestIdentity();};
            api.start=[](void*,const ReloadCycleControl& c,const interaction::ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{return reloadFlowRuntime::StartMagazineRequestCycleObserved(c,r,pulse);};
            api.inspectStart=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle,const std::optional<ReloadMagazineStartupPulse>& pulse)noexcept{return reloadFlowRuntime::InspectMagazineRequestCycleStart(id,cycle,pulse);};
            api.keep=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycle(c);};
            api.keepObserved=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycleObserved(c);};
            api.reserveObserved=[](void*)noexcept{return reloadFlowRuntime::ReadReserveObserved();};
            api.lease=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RequestMagazineLease(id,cycle);};
            api.leaseObserved=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::ObserveMagazineLease(id,cycle);};
            api.gate=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::TakeMagazineUnseatAcknowledgement(id,cycle);};
            api.submit=[](void*,const ReloadMagazineNativeRequest& r)noexcept{const bool submitted=reloadFlowRuntime::SubmitMagazineRequest(r);
                if(submitted)gunFvr.Seat(GunEvents(),gunWeapon,r.heldLease.cycle,int(r.reservedUnits));return submitted;};
            api.ack=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{auto evidence=reloadFlowRuntime::TakeMagazineAcknowledgement(id,cycle);
                if(evidence)gunFvr.Credit(GunEvents(),gunWeapon,cycle,evidence->acknowledgement.loadedAfter,evidence->acknowledgement.reserveAfter);return evidence;};
            api.cancel=[](void*)noexcept{reloadFlowRuntime::CancelRequestCycle();};
            api.retire=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RetireRequestCycle(id,cycle);};
            api.clock=[](void*)noexcept{LARGE_INTEGER now{};QueryPerformanceCounter(&now);return HandNanos(now.QuadPart);};
            magazinePhysicalApi=api;magazinePhysical.emplace(true,api);magazinePhysical->ConfigureZones(interactionZones);
            magazinePhysical->EnableBodyAmmo(reloadGuidanceEnabled);
            magazinePhysical->EnableGrabRemoval(zoneSettings.magazineGrabRemove);
            magazinePhysical->EnableShortRackAfterInsert(shortRackAfterInsert);
            magazinePhysical->EnableChamberRack(ChamberRackDefault());
    if(magazinePhysicalProbe){
        // Mode 7: one fresh replacement (no original return or carry), released
        // mid-stroke so the slider's release-to-finish seats it in the game.
        const bool slider=magazineReloadSession==7,eject=magazineReloadSession==8;
        magazinePhysicalFixture.emplace(true,magazineReloadSession==5,magazineReloadSession!=5&&!slider&&!eject,magazineReloadSession==4);
        if(slider){magazinePhysicalFixture->ReleaseMidStroke();magazinePhysicalFixture->PrepareDryFire(3);} // fire to empty, 3 dry pulls, reload
        if(magazineReloadSession==8)magazinePhysicalFixture->Eject(); // partial magazine, Reload press ejects, pouch, seat
        // With the opt-in chamber rack, mode 7 also racks the handle after the reload.
        if(slider&&ChamberRackDefault())magazinePhysicalFixture->RackLeg();
        // FVR_BC2_PROBE_BOLT_RELEASE=1: the rack leg presses Reload (bolt release, C3) instead of racking.
        if(slider)if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_PROBE_BOLT_RELEASE",value,sizeof(value))&&value[0]=='1')magazinePhysicalFixture->BoltReleaseLeg();
        if(slider)if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_PROBE_FLICK_AT_HOLD",value,sizeof(value))&&value[0]=='1')magazinePhysicalFixture->FlickAtHold();
        // FVR_BC2_PROBE_CANCEL=hold|arming|untracked: force a consumer cancel (regression legs).
        if(slider)if(char value[24]{};GetEnvironmentVariableA("FVR_BC2_PROBE_CANCEL",value,sizeof(value))){const std::string_view v=value;
            magazinePhysicalFixture->ForceCancel(v=="hold"?1u:v=="arming"?2u:v=="untracked"?3u:v=="arming-untracked"?4u:v=="head-lost"?5u:0u);}
        magazinePhysicalFixture->PrepareShots(); // proto/reload: start from a fresh full-magazine spawn
    }
            if(MagazineDetachedSessionEnabled(magazineReloadSession))magazineDetached.emplace(true);
            if(magazineReloadSession==6)magazineDetachedFixture.emplace(true);
        }
        if(magazineProbe)magazineReloadFixture.emplace(true,MagazineReloadProbeApi{reloadFlowRuntime::RequestIdentity,reloadFlowRuntime::StartMagazineRequestCycle,reloadFlowRuntime::KeepAliveRequestCycle,reloadFlowRuntime::RequestMagazineLease,reloadFlowRuntime::SubmitMagazineRequest,reloadFlowRuntime::TakeMagazineAcknowledgement,reloadFlowRuntime::CancelRequestCycle,reloadFlowRuntime::ReadRequestProbeSnapshot,[]()noexcept{LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);return HandNanos(clock.QuadPart);},reloadFlowRuntime::RetireRequestCycle,reloadFlowRuntime::ReadReserve,reloadFlowRuntime::TakeMagazineUnseatAcknowledgement},magazineReloadSession==2);
        if(reloadRequestProbe)reloadRequestFixture.emplace(true,ReloadRequestProbeApi{reloadFlowRuntime::RequestIdentity,reloadFlowRuntime::StartRequestCycle,reloadFlowRuntime::KeepAliveRequestCycle,reloadFlowRuntime::RequestLease,reloadFlowRuntime::SubmitRequest,reloadFlowRuntime::TakeRequestAcknowledgement,reloadFlowRuntime::CancelRequestCycle,reloadFlowRuntime::ReadRequestProbeSnapshot,[]()noexcept{LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);return HandNanos(clock.QuadPart);},reloadFlowRuntime::RetireRequestCycle,reloadFlowRuntime::ReadReserve});
    }
    for(const auto [rva,n]:{std::pair{profile.gameplay.playerInputUpdate,0x1ebu},std::pair{profile.gameplay.inputGather,0x232u},std::pair{profile.gameplay.contextGetter,0x79u},std::pair{profile.gameplay.soldierGetter,23u},std::pair{profile.gameplay.inputRouterVtable,20u},std::pair{profile.cacheConstructor,0x41u},std::pair{profile.cacheVtable,4u},std::pair{profile.buttonSetter,0x8cu},std::pair{profile.floatSetter,0x108u},std::pair{profile.controlledGetter,7u},std::pair{profile.attachedPredicate,0x46u},std::pair{aimingProfile.weaponGetter,0x41u},std::pair{aimingProfile.indexGetter,7u},std::pair{aimingProfile.aimGetter,4u},std::pair{aimingProfile.yawGetter,0xa6u},std::pair{aimingProfile.inputPrepare,0x1a3u},std::pair{aimingProfile.absoluteYawSetter,17u},std::pair{aimingProfile.angleCopy,0x5du},std::pair{aimingProfile.aimerYawSetter,0x38u},std::pair{aimingProfile.aimerPitchSetter,0x5fu}}){
        const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);
        if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
    }
    if(equipProbe||sightFlip||physicalReloadMode){
        const auto mode=DiscoverWeaponMode(bytes,pe);
        weaponModeBindingVerified=bool(mode);
        if(mode)for(const auto [rva,n]:{std::pair{mode->selector,0x1b1u},std::pair{mode->update,0x86u},std::pair{mode->edgeReader,0x37u},std::pair{mode->booleanReader,0x88u},std::pair{mode->scalarReader,0x7cu}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);
            if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))weaponModeBindingVerified=false;
        }
    }
    if(equipProbe||muzzleFire){const auto fire=DiscoverFireOrigin(bytes,pe);if(!fire)return false;fireProfile=*fire;
        for(const auto [rva,n]:{std::pair{fireProfile.builder,0x158u},std::pair{fireProfile.serverShoot,0xc90u},std::pair{fireProfile.serverControlledGetter,0x21u},std::pair{fireProfile.serverContextGetter,0x9du},std::pair{fireProfile.serverManagerConstructor,0x39u},std::pair{fireProfile.serverPlayerCreate,0x71u},std::pair{fireProfile.serverPlayerConstructor,0x1fu},std::pair{fireProfile.playerConstructor,0x5au},std::pair{fireProfile.clientShoot,0xb9eu},std::pair{fireProfile.matrixCopy,0x57u},std::pair{fireProfile.compose,0xb0u},std::pair{fireProfile.spreadSeed,8u},std::pair{fireProfile.randomSeed,0x15u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
        const auto address=reinterpret_cast<void*>(base+fireProfile.builder);
        if(MH_CreateHook(address,FireOriginHook,reinterpret_cast<void**>(&fireOriginOriginal))!=MH_OK)return false;hooks[8]=address;
        const auto client=reinterpret_cast<void*>(base+fireProfile.clientShoot),copy=reinterpret_cast<void*>(base+fireProfile.matrixCopy);
        if(MH_CreateHook(client,ClientShootHook,reinterpret_cast<void**>(&clientShootOriginal))!=MH_OK)return false;hooks[9]=client;
        if(MH_CreateHook(copy,MatrixCopyHook,reinterpret_cast<void**>(&matrixCopyOriginal))!=MH_OK)return false;hooks[10]=copy;
        const auto server=reinterpret_cast<void*>(base+fireProfile.serverShoot),compose=reinterpret_cast<void*>(base+fireProfile.compose);
        if(MH_CreateHook(server,ServerShootHook,reinterpret_cast<void**>(&serverShootOriginal))!=MH_OK)return false;hooks[11]=server;
        if(MH_CreateHook(compose,ComposeHook,reinterpret_cast<void**>(&composeOriginal))!=MH_OK)return false;hooks[12]=compose;
    }
    if(observePoses){const auto poses=DiscoverFirstPersonPose(bytes,pe);if(!poses)return false;poseProfile=*poses;
        for(const auto [rva,n]:{std::pair{poseProfile.animationUpdate,0x190u},std::pair{poseProfile.worldBuilder,0x204u},std::pair{poseProfile.rootSetter,0x56u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
        const auto rig=DiscoverRig(bytes,pe);if(!rig)return false;rigProfile=*rig;
        for(const auto [rva,n]:{std::pair{rigProfile.animationGetter,0x2fu},std::pair{rigProfile.animationUpdate,0x2f3u},std::pair{rigProfile.evaluate,0xdau},std::pair{rigProfile.postEvaluate,0xe9u},std::pair{rigProfile.weaponWorld,0x14u},std::pair{rigProfile.boneWorld,0x2bu},std::pair{rigProfile.worldThunk,0x15u},std::pair{rigProfile.worldIndex,0x17u},std::pair{rigProfile.skinSelect,0x15u},std::pair{rigProfile.skinGetter,8u},std::pair{rigProfile.paletteGetter,10u},std::pair{rigProfile.skinThunk,16u},std::pair{rigProfile.skinData,8u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
        const auto add=[&](unsigned slot,unsigned rva,void* replacement,void** original){const auto address=reinterpret_cast<void*>(base+rva);if(MH_CreateHook(address,replacement,original)!=MH_OK)return false;hooks[slot]=address;return true;};
        if(!add(4,rigProfile.animationUpdate,reinterpret_cast<void*>(AnimationHook),reinterpret_cast<void**>(&animationOriginal))||
           !add(5,rigProfile.evaluate,reinterpret_cast<void*>(EvaluateHook),reinterpret_cast<void**>(&evaluateOriginal))||
           !add(6,rigProfile.postEvaluate,reinterpret_cast<void*>(PostEvaluateHook),reinterpret_cast<void**>(&postOriginal))||
           !add(7,rigProfile.weaponWorld,reinterpret_cast<void*>(WeaponWorldHook),reinterpret_cast<void**>(&weaponWorldOriginal)))return false;
        if(handPoses){
            // Optional: a discovery or live-byte mismatch leaves native effects untouched.
            char enabledValue[8]{},windowValue[16]{};
            const auto enabledLength=GetEnvironmentVariableA("FVR_BC2_EFFECT_ANCHOR",enabledValue,sizeof(enabledValue));
            const auto windowLength=GetEnvironmentVariableA("FVR_BC2_EFFECT_ANCHOR_MS",windowValue,sizeof(windowValue));
            effectAnchorConfig=ParseEffectAnchorConfig(enabledLength&&enabledLength<sizeof(enabledValue)?enabledValue:nullptr,
                windowLength&&windowLength<sizeof(windowValue)?windowValue:nullptr);
            {char writtenValue[4]{};const auto writtenLength=GetEnvironmentVariableA("FVR_BC2_EFFECT_WRITTEN_ROOT",writtenValue,sizeof(writtenValue));
                effectAnchorConfig.writtenRoot=!(writtenLength==1&&writtenValue[0]=='0');}
            if(const auto effects=DiscoverWeaponEffects(bytes,pe)){
                bool live=true;
                for(const auto [rva,n]:{std::pair{effects->effectsUpdate,0x1deu},std::pair{effects->spawn,0x63u},std::pair{effects->create,0x46u},
                        std::pair{effects->setTransform,0x56u},std::pair{effects->overheat,0x156u},std::pair{effects->matrixCopy,0x10u},
                        std::pair{effects->lookup,0x16u},std::pair{effects->firingEffectsGetter,4u},std::pair{effects->compose,0x4b4u}}){
                    const auto offset=Offset(pe,rva,n);std::vector<std::byte> current(n);
                    if(!offset||!Read(base+rva,current.data(),n)||std::memcmp(current.data(),bytes.data()+*offset,n)){live=false;break;}
                }
                const auto spawnAddress=reinterpret_cast<void*>(base+effects->spawn);
                if(live&&MH_CreateHook(spawnAddress,reinterpret_cast<void*>(EffectSpawnHook),reinterpret_cast<void**>(&effectSpawnOriginal))==MH_OK){
                    const auto composeAddress=reinterpret_cast<void*>(base+effects->compose);
                    if(MH_CreateHook(composeAddress,reinterpret_cast<void*>(EffectComposeHook),reinterpret_cast<void**>(&effectComposeOriginal))==MH_OK){
                        hooks[17]=spawnAddress;hooks[20]=composeAddress;effectProfile=*effects;
                        effectSetTransform=reinterpret_cast<EffectSetFn>(base+effects->setTransform);effectAnchorInstalled=true;
                    }else{MH_RemoveHook(spawnAddress);effectSpawnOriginal=nullptr;}
                }
            }
        }
        const auto world=reinterpret_cast<void*>(base+poseProfile.worldBuilder),root=reinterpret_cast<void*>(base+poseProfile.rootSetter);
        if(MH_CreateHook(world,WorldPoseHook,reinterpret_cast<void**>(&worldPoseOriginal))!=MH_OK)return false;hooks[2]=world;
        if(MH_CreateHook(root,RootPoseHook,reinterpret_cast<void**>(&rootPoseOriginal))!=MH_OK)return false;hooks[3]=root;
    }
    if(observePoses||enableRigPulse){
        if(!rigPublication::Install(bytes,pe,base,[](unsigned soldier,unsigned& weak)noexcept{Owner owner{};if(!Resolve(owner)||!owner.foot||owner.soldier!=soldier)return false;weak=owner.weak;return true;},enableRigPulse,handPoses))return false;
    }
    if(bodyFollow){const auto position=DiscoverBodyPosition(bytes,pe);if(!position)return false;bodyProfile=*position;
        for(const auto [rva,n]:{std::pair{bodyProfile.getter,0x18u},std::pair{bodyProfile.fallback,10u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
    }
    if(physicalReloadMode){
        // Unique, byte-checked in the file image and live; skipped (not fatal) otherwise.
        constexpr std::array<std::uint8_t,20> sig{0x56,0x8B,0xF1,0x8B,0x46,0x04,0x8A,0x88,0x75,0x04,0x00,0x00,0x80,0xE1,0x01,0x38,0x4E,0x50,0x75,0x1A};
        constexpr unsigned rva=0x881650-0x400000;unsigned matches=0;
        for(std::size_t n=0;n+sig.size()<=bytes.size();++n)if(std::memcmp(bytes.data()+n,sig.data(),sig.size())==0)++matches;
        const auto offset=Offset(pe,rva,unsigned(sig.size()));std::array<std::uint8_t,20> live{};
        if(matches==1&&offset&&std::memcmp(bytes.data()+*offset,sig.data(),sig.size())==0&&Read(base+rva,live.data(),live.size())&&live==sig){
            const auto address=reinterpret_cast<void*>(base+rva);
            if(MH_CreateHook(address,reinterpret_cast<void*>(AnimSoundStartHook),reinterpret_cast<void**>(&animSoundStartOriginal))==MH_OK)hooks[13]=address;}
        // OnAnimStop 0x881690: same body with the stop call (75 15); gives the
        // reload sound replay/stop more animation-thread callbacks.
        constexpr std::array<std::uint8_t,20> stopSig{0x56,0x8B,0xF1,0x8B,0x46,0x04,0x8A,0x88,0x75,0x04,0x00,0x00,0x80,0xE1,0x01,0x38,0x4E,0x50,0x75,0x15};
        constexpr unsigned stopRva=0x881690-0x400000;unsigned stopMatches=0;
        for(std::size_t n=0;n+stopSig.size()<=bytes.size();++n)if(std::memcmp(bytes.data()+n,stopSig.data(),stopSig.size())==0)++stopMatches;
        const auto stopOffset=Offset(pe,stopRva,unsigned(stopSig.size()));std::array<std::uint8_t,20> stopLive{};
        if(hooks[13]&&stopMatches==1&&stopOffset&&std::memcmp(bytes.data()+*stopOffset,stopSig.data(),stopSig.size())==0&&
           Read(base+stopRva,stopLive.data(),stopLive.size())&&stopLive==stopSig){
            const auto address=reinterpret_cast<void*>(base+stopRva);
            if(MH_CreateHook(address,reinterpret_cast<void*>(AnimSoundStopHook),reinterpret_cast<void**>(&animSoundStopOriginal))==MH_OK)hooks[14]=address;}
        // Listener dispatch 0xf4fb50 through its lock setup (CS 0x18a4bc0 is
        // inside the bytes), and the TLS get/set helpers that name the index.
        // The replay is enabled only with all three hooks; otherwise the
        // native clip sound plays on the stock clock as before.
        constexpr std::array<std::uint8_t,54> dispatchSig{0x83,0xEC,0x0C,0x56,0x57,0x8B,0x7C,0x24,0x18,0x8B,0x07,0x3B,0x47,0x04,0x8B,0xF1,0x75,0x1A,0x8B,0x4F,0x54,0x3B,0x4F,0x58,
            0x75,0x12,0x8B,0x97,0xD0,0x00,0x00,0x00,0x3B,0x97,0xD4,0x00,0x00,0x00,0x0F,0x84,0x6A,0x02,0x00,0x00,0x53,0x55,0xC7,0x44,0x24,0x14,0xC0,0x4B,0x8A,0x01};
        constexpr std::array<std::uint8_t,13> tlsGetSig{0xA1,0x04,0x2E,0x50,0x01,0x50,0xFF,0x15,0x54,0x53,0x40,0x01,0xC3};
        constexpr std::array<std::uint8_t,19> tlsSetSig{0x8B,0x44,0x24,0x04,0x8B,0x0D,0x04,0x2E,0x50,0x01,0x50,0x51,0xFF,0x15,0x58,0x53,0x40,0x01,0xC3};
        constexpr unsigned dispatchRva=0xf4fb50-0x400000,tlsGetRva=0x50ece0-0x400000,tlsSetRva=0x50ecc0-0x400000,lockRva=0x18a4bc0-0x400000,tlsSlotRva=0x1502e04-0x400000;
        const auto exact=[&](unsigned rva,const auto& sig){unsigned matches=0;
            for(std::size_t n=0;n+sig.size()<=bytes.size();++n)if(std::memcmp(bytes.data()+n,sig.data(),sig.size())==0)++matches;
            const auto offset=Offset(pe,rva,unsigned(sig.size()));std::remove_cvref_t<decltype(sig)> live{};
            return matches==1&&offset&&std::memcmp(bytes.data()+*offset,sig.data(),sig.size())==0&&Read(base+rva,live.data(),live.size())&&live==sig;};
        DWORD tlsIndex=TLS_OUT_OF_INDEXES;
        if(hooks[14]&&exact(dispatchRva,dispatchSig)&&exact(tlsGetRva,tlsGetSig)&&exact(tlsSetRva,tlsSetSig)&&
           Read(base+tlsSlotRva,&tlsIndex,4)&&tlsIndex<1088){
            animDispatchLock=reinterpret_cast<CRITICAL_SECTION*>(base+lockRva);animDispatchTls=tlsIndex;
            const auto address=reinterpret_cast<void*>(base+dispatchRva);
            if(MH_CreateHook(address,reinterpret_cast<void*>(AnimDispatchHook),reinterpret_cast<void**>(&animDispatchOriginal))==MH_OK){hooks[15]=address;reloadSoundEnabled=true;}}
    }
    nativeRecoilObserveRequested=ProcessEnvText("FVR_BC2_NATIVE_RECOIL_OBSERVE")=="1";
    nativeRecoilTransferEnabled=ProcessEnvText("FVR_BC2_NATIVE_RECOIL_TRANSFER")!="0";
    // Pin the inspected selector and amplitude read before following its data.
    const auto recoilBytes=[&](unsigned rva,std::initializer_list<unsigned char> signature){
        const auto offset=Offset(pe,rva,unsigned(signature.size()));std::vector<unsigned char> live(signature.size());
        return offset&&*offset<=bytes.size()&&signature.size()<=bytes.size()-*offset&&
            !std::memcmp(bytes.data()+*offset,signature.begin(),signature.size())&&
            Read(base+rva,live.data(),live.size())&&!std::memcmp(live.data(),signature.begin(),live.size());
    };
    nativeRecoilStrengthVerified=recoilBytes(0x6d60e0-0x400000,
        {0x83,0xec,0x10,0x53,0x8b,0x5c,0x24,0x1c,0x56,0x8b,0xf1,0x57,0x8b,0xbe,0xe0,0,0,0,
         0xc7,0x07,0,0,0,0,0x8b,0x03,0x8b,0x50,0x14,0x57,0x8b,0xcb,0xff,0xd2,0x8b,0xcb,
         0x50,0x8b,0x03,0x8b,0x10,0xff,0xd2,0x50,0xe8,0x6f,0xac,0xff,0xff,0x8b,0x07})&&
        recoilBytes(0x6d611e-0x400000,
        {0x80,0xbe,0xc4,0,0,0,0,0xd9,0x40,0x0c,0xdd,0x05,0x40,0x96,0x4d,0x01,0xf3,0x0f,0x10,0x40,0x04,
         0xdc,0xc9,0xf3,0x0f,0x11,0x44,0x24,0x24,0xdd,0x05,0xb0,0xa0,0x40,0x01,0xdc,0xfa,0xd9,0xca,
         0xd9,0x5c,0x24,0x0c,0x0f,0x84,0x36,0x01,0,0,0xd9,0x40,0x08,0x55,0xd8,0xc9,0x83,0xec,0x08,
         0x8d,0xbe,0xb4,0,0,0,0x8b,0xcf,0xd8,0xf2,0xd9,0x5c,0x24,0x20,0xd9,0x44,0x24,0x20,
         0xdd,0x05,0x88,0x94,0x4d,0x01,0xdc,0xc9,0xd9,0xc9,0xd9,0x5c,0x24,0x20,0xd9,0,0xd8,0xca,
         0xd8,0xf3,0xd9,0x5c,0x24,0x1c,0xd8,0x4c,0x24,0x1c,0xd9,0x5c,0x24,0x1c,0xd8,0x48,0x10,
         0xde,0xf1,0xd9,0x5c,0x24,0x24});
    if((nativeRecoilObserveRequested||nativeRecoilTransferEnabled)&&handPoses&&reloadStateBinding&&DiscoverNativeRecoilObservation(bytes,pe)){
        // Complete inspected functions, native cleanup, vtable links and the
        // camera return site are checked against file and live executable.
        const auto exactLive=[&](unsigned rva,unsigned size){
            const auto offset=Offset(pe,rva,size);std::vector<std::byte> current(size);
            return offset&&*offset<=bytes.size()&&size<=bytes.size()-*offset&&
                Read(base+rva,current.data(),size)&&!std::memcmp(current.data(),bytes.data()+*offset,size);
        };
        std::array<unsigned char,3> tickReturn{},cameraReturn{};
        std::array<unsigned char,2> cameraCall{};
        if(exactLive(NativeRecoilBinding::tickRva,0x15f)&&exactLive(NativeRecoilBinding::cameraRva,0x147)&&
           exactLive(NativeRecoilBinding::resolverRva,0x74)&&exactLive(0x89c638-0x400000,0x11)&&
           Read(base+(0x6ec6bc-0x400000),tickReturn.data(),3)&&tickReturn==std::array<unsigned char,3>{0xc2,8,0}&&
           Read(base+(0x6d60d4-0x400000),cameraReturn.data(),3)&&cameraReturn==std::array<unsigned char,3>{0xc2,4,0}&&
           Read(base+(0x89c647-0x400000),cameraCall.data(),2)&&cameraCall==std::array<unsigned char,2>{0xff,0xd2}&&
           U32(unsigned(base)+NativeRecoilBinding::swayVtableRva+0x5c)==base+NativeRecoilBinding::tickRva&&
           U32(unsigned(base)+NativeRecoilBinding::swayVtableRva+0x68)==base+NativeRecoilBinding::cameraRva){
            const auto tick=reinterpret_cast<void*>(base+NativeRecoilBinding::tickRva),camera=reinterpret_cast<void*>(base+NativeRecoilBinding::cameraRva);
            if(nativeRecoilObserveRequested)try{nativeRecoilHistory=std::make_unique<NativeRecoilObservationHistory<>>();}catch(...){nativeRecoilHistory.reset();}
            if(MH_CreateHook(tick,reinterpret_cast<void*>(NativeSwayTickHook),reinterpret_cast<void**>(&nativeSwayTickOriginal))==MH_OK){
                if(MH_CreateHook(camera,reinterpret_cast<void*>(NativeSwayCameraHook),reinterpret_cast<void**>(&nativeSwayCameraOriginal))==MH_OK){
                    hooks[18]=tick;hooks[19]=camera;nativeRecoilObserveInstalled=true;
                }else{MH_RemoveHook(tick);nativeSwayTickOriginal=nullptr;nativeRecoilHistory.reset();}
            }
        }
    }
    const auto update=reinterpret_cast<void*>(base+profile.gameplay.playerInputUpdate),gather=reinterpret_cast<void*>(base+profile.gameplay.inputGather);
    {// Stick weapon switch: byte-checked server inventory update, setter and call site.
        static constexpr unsigned char updateSig[]={0x83,0xEC,0x08,0x56,0x8B,0xF1,0xD9,0x86,0x58,0x01,0x00,0x00};
        static constexpr unsigned char setterSig[]={0x80,0x7C,0x24,0x08,0x00,0x56,0x57,0x8B,0x7C,0x24,0x0C,0x8B,0xF1,0x74,0x18,0x8B,0x8E,0x48,0x01,0x00,0x00};
        unsigned char liveUpdate[sizeof updateSig]{},liveSetter[sizeof setterSig]{},call[5]{},updateReturn[3]{},setterReturn[3]{};
        const auto u=base+0x2f1980,s=base+0x2d7c80,site=base+0x28dc79;std::int32_t rel=0;
        if(Read(u,liveUpdate,sizeof liveUpdate)&&!std::memcmp(liveUpdate,updateSig,sizeof updateSig)&&Read(s,liveSetter,sizeof liveSetter)&&!std::memcmp(liveSetter,setterSig,sizeof setterSig)&&
           Read(u+0x83,updateReturn,3)&&updateReturn[0]==0xc2&&updateReturn[1]==8&&updateReturn[2]==0&&
           Read(s+0x55,setterReturn,3)&&setterReturn[0]==0xc2&&setterReturn[1]==8&&setterReturn[2]==0&&
           Read(site,call,5)&&call[0]==0xE8&&(std::memcpy(&rel,call+1,4),site+5+std::uintptr_t(std::intptr_t(rel))==u)){
            selectSlotNative=reinterpret_cast<SelectSlotFn>(s);inventoryUpdateServerReturn=site+5;
            if(MH_CreateHook(reinterpret_cast<void*>(u),reinterpret_cast<void*>(InventoryUpdateHook),reinterpret_cast<void**>(&inventoryUpdateOriginal))==MH_OK){hooks[16]=reinterpret_cast<void*>(u);weaponSwitchHooked=true;}}}
    actorLossRebind=ProcessEnvText("FVR_BC2_ACTOR_LOSS_REBIND")!="0";
    if(ProcessEnvText("FVR_BC2_NULL_WEAPON_GUARD")!="0"){
        // ClientSoldier weapon-view getters 0x89bc40 (+0xc, fallback +0x400) and
        // 0x89bcb0 (+0x10, fallback +0x404), build 795745. Both are 0x66-byte
        // thiscall functions (no stack arguments, plain RET) returning a float
        // in ST0. Exact full-body bytes (including the rel32 calls to 0x89bbd0,
        // 0x6ce960 and 0x7efca0) must match the file image and the live image;
        // any mismatch leaves both getters native. Optional, never fatal.
        static constexpr unsigned char fovSig[0x66]={
            0x56,0x8b,0xf1,0xe8,0x88,0xff,0xff,0xff,0x84,0xc0,0x74,0x52,0xf6,0x86,0x14,0x01,0x00,0x00,0x01,0x74,
            0x08,0x8b,0x8e,0x4c,0x02,0x00,0x00,0xeb,0x06,0x8b,0x8e,0x48,0x02,0x00,0x00,0xe8,0xf8,0x2c,0xe3,0xff,
            0x8b,0x8e,0x64,0x02,0x00,0x00,0x2b,0x8e,0x60,0x02,0x00,0x00,0xc1,0xf9,0x02,0x3b,0xc1,0x72,0x0e,0x33,
            0xc0,0x8b,0xc8,0xe8,0x1c,0x40,0xf5,0xff,0xd9,0x40,0x0c,0x5e,0xc3,0x8b,0x96,0x60,0x02,0x00,0x00,0x8b,
            0x04,0x82,0x8b,0xc8,0xe8,0x07,0x40,0xf5,0xff,0xd9,0x40,0x0c,0x5e,0xc3,0xd9,0x86,0x00,0x04,0x00,0x00,
            0x5e,0xc3};
        static constexpr unsigned char fov2Sig[0x66]={
            0x56,0x8b,0xf1,0xe8,0x18,0xff,0xff,0xff,0x84,0xc0,0x74,0x52,0xf6,0x86,0x14,0x01,0x00,0x00,0x01,0x74,
            0x08,0x8b,0x8e,0x4c,0x02,0x00,0x00,0xeb,0x06,0x8b,0x8e,0x48,0x02,0x00,0x00,0xe8,0x88,0x2c,0xe3,0xff,
            0x8b,0x8e,0x64,0x02,0x00,0x00,0x2b,0x8e,0x60,0x02,0x00,0x00,0xc1,0xf9,0x02,0x3b,0xc1,0x72,0x0e,0x33,
            0xc0,0x8b,0xc8,0xe8,0xac,0x3f,0xf5,0xff,0xd9,0x40,0x10,0x5e,0xc3,0x8b,0x96,0x60,0x02,0x00,0x00,0x8b,
            0x04,0x82,0x8b,0xc8,0xe8,0x97,0x3f,0xf5,0xff,0xd9,0x40,0x10,0x5e,0xc3,0xd9,0x86,0x04,0x04,0x00,0x00,
            0x5e,0xc3};
        constexpr unsigned fovRva=0x89bc40-0x400000,fov2Rva=0x89bcb0-0x400000;
        const auto exactBody=[&](unsigned rva,const unsigned char(&sig)[0x66]){
            const auto offset=Offset(pe,rva,unsigned(sizeof sig));unsigned char live[sizeof sig]{};
            return offset&&*offset<=bytes.size()&&sizeof sig<=bytes.size()-*offset&&!std::memcmp(bytes.data()+*offset,sig,sizeof sig)&&
                Read(base+rva,live,sizeof live)&&!std::memcmp(live,sig,sizeof sig);};
        if(exactBody(fovRva,fovSig)&&exactBody(fov2Rva,fov2Sig)){
            const auto fov=reinterpret_cast<void*>(base+fovRva),fov2=reinterpret_cast<void*>(base+fov2Rva);
            if(MH_CreateHook(fov,reinterpret_cast<void*>(WeaponViewFovHook),reinterpret_cast<void**>(&weaponViewFovOriginal))==MH_OK){
                if(MH_CreateHook(fov2,reinterpret_cast<void*>(WeaponViewFov2Hook),reinterpret_cast<void**>(&weaponViewFov2Original))==MH_OK){
                    hooks[21]=fov;hooks[22]=fov2;nullWeaponGuardInstalled=true;
                }else{MH_RemoveHook(fov);weaponViewFovOriginal=nullptr;}
            }
        }
    }
    if(MH_CreateHook(update,UpdateHook,reinterpret_cast<void**>(&updateOriginal))!=MH_OK)return false;hooks[0]=update;
    if(MH_CreateHook(gather,GatherHook,reinterpret_cast<void**>(&gatherOriginal))!=MH_OK)return false;hooks[1]=gather;return true;
}
bool AdjustViewBase(std::array<RenderViewCopy,2>& copies,const runtime::TrackingFrame& tracking)noexcept {
    if(!enabled.load(std::memory_order_acquire))return true;
    const auto space=tracking.spaceGeneration;
    Owner viewOwner{};
    if(Resolve(viewOwner)&&!viewOwner.foot){
        std::lock_guard lock(vehicleViewMutex);
        const auto actor=(std::uint64_t(viewOwner.weak)<<32)|viewOwner.soldier;
        LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return false;
        const auto admission=interaction::AdmitVehicleCamera(vehicleView,actor,viewOwner.controlled,viewOwner.entry,space,HandNanos(now.QuadPart));
        if(admission==interaction::VehicleCameraAdmission::Unsupported){++vehicleCameraUnsupportedFrames;return true;}
        // A known seat must wait for current input instead of composing its
        // camera with the old on-foot reference for a single bad frame.
        if(admission!=interaction::VehicleCameraAdmission::Ready||!tracking.focused||!tracking.headValid){++vehicleCameraWaits;return false;}
        std::optional<BoatAimSnapshot> renderAim;
        if(boatHeadAim){
            const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
            if(!boatAimReady||boatAimSeat.identity.actor!=actor||boatAimSeat.identity.actorGeneration!=vehicleView.owner.generation||boatAimSeat.identity.controlled!=viewOwner.controlled||boatAimSeat.identity.entry!=viewOwner.entry)return false;
            renderAim=ReadPblDriverAim(memory,boatAimBinding,boatAimSeat);if(!renderAim)return false;
        }
        auto changed=copies;
        for(auto& copy:changed){engine::FrostbiteCameraInput source{};
            std::memcpy(&source.transform,copy.bytes.data()+0x50,64);std::memcpy(&source.nearPlane,copy.bytes.data()+0x1c,4);std::memcpy(&source.farPlane,copy.bytes.data()+0x20,4);source.worldUnitsPerMeter=1;
            const auto native=engine::CanonicalCamera(source);if(!native)return false;
            std::optional<math::Matrix4> seatBase;
            if(renderAim){
                auto stable=interaction::Multiply(renderAim->neutralCameraLocal,renderAim->hull);stable.values[3]=native->camera.values[3];
                // One exact reference drives desired aim and both eyes.
                seatBase=interaction::VehicleAimViewBase(stable,boatAimReference,tracking.referenceHead,tracking.worldUnitsPerMeter);
            }else seatBase=vehicleCameraAnchor.Base(vehicleView.owner,native->camera,tracking.referenceHead,tracking.head,space,tracking.worldUnitsPerMeter);
            if(!seatBase)return false;
            const auto built=BuildTransformCopy(copy,*seatBase);if(!built)return false;copy=*built;
        }
        // The same anchored prototypes feed BuildTrackedViews' two eyes and
        // culling. Original XR poses and current native boat motion survive.
        if(renderAim){QueryPerformanceCounter(&now);if(HandNanos(now.QuadPart)>=vehicleView.deadlineNs){++vehicleCameraWaits;return false;}}
        copies=changed;++vehicleCameraViews;if(renderAim)++boatAimCameraViews;return true;
    }
    if(!motionAim)return true;
    unsigned soldier=0,weak=0;std::uint64_t sourceSpace=0;float yaw=0,consumedX=0,consumedZ=0,units=1;bool captured=false;
    for(unsigned n=0;n<3;++n){const auto seq=anchor.sequence.load(std::memory_order_acquire);if(seq&1)continue;
        soldier=anchor.soldier.load();weak=anchor.weak.load();sourceSpace=anchor.space.load();yaw=anchor.yaw.load();consumedX=anchor.consumedX.load();consumedZ=anchor.consumedZ.load();units=anchor.units.load();
        if(seq==anchor.sequence.load(std::memory_order_acquire)){captured=true;break;}}
    if(!captured)return false;if(!soldier)return true;
    Owner owner{};if(!Resolve(owner)||!owner.foot||owner.soldier!=soldier||owner.weak!=weak)return true;
    if(sourceSpace!=space)return false;
    BaseRecord record{};record.weapon=owner.weapon;record.ms=GetTickCount64();record.space=space;Read(owner.aim+12,&record.nativeYaw,4);Read(owner.aim+16,&record.nativePitch,4);record.bodyYaw=yaw;
    bool primary=true;for(auto& copy:copies){engine::FrostbiteCameraInput input{};
        std::memcpy(&input.transform,copy.bytes.data()+0x50,64);std::memcpy(&input.nearPlane,copy.bytes.data()+0x1c,4);std::memcpy(&input.farPlane,copy.bytes.data()+0x20,4);input.worldUnitsPerMeter=1;
        const auto canonical=engine::CanonicalCamera(input);if(!canonical)return false;
        // BC2 aim heading and the native RenderView forward differ by pi.
        // Live source yaw = native yaw - pi (independently sampled Sept 30).
        auto base=interaction::MakeComfortCamera(canonical->camera,(yaw-3.141592653589793f)*57.29577951308232f);if(!base)return false;
        if(primary){record.sourceYaw=std::atan2(canonical->camera.values[2][0],canonical->camera.values[2][2]);record.viewYaw=std::atan2(base->values[2][0],base->values[2][2]);primary=false;}
        const auto sharedEye=handPoses?rigPublication::ReadEyeBase(owner.soldier,owner.weak,space):std::nullopt;
        if(sharedEye)base=*sharedEye;
        record.sharedEye=bool(sharedEye);record.sourcePosition={canonical->camera.values[3][0],canonical->camera.values[3][1],canonical->camera.values[3][2]};record.eyePosition={base->values[3][0],base->values[3][1],base->values[3][2]};
        if(bodyFollow&&!sharedEye){
            // Counter only observed roomscale body displacement. Joystick travel
            // stays in the native camera; HMD translation is added downstream.
            for(unsigned axis=0;axis<3;++axis)base->values[3][axis]-=units*(consumedX*base->values[0][axis]-consumedZ*base->values[2][axis]);
        }
        const auto changed=BuildTransformCopy(copy,*base);if(!changed)return false;copy=*changed;
    }
    const auto now=GetTickCount64();const auto count=baseRecordCount.load();
    if(count<baseRecords.size()&&now-baseRecordTime>=100){baseRecords[count]=record;baseRecordTime=now;baseRecordCount.store(count+1,std::memory_order_release);}
    ++baseViews;return true;
}
bool ObserveVehicleCameraPlan(unsigned frame,const runtime::TrackingFrame& tracking,const RenderViewCopy& native,const RenderViewCopy& adjusted,const std::array<RenderViewCopy,2>& planned)noexcept {
    std::lock_guard lock(vehicleViewMutex);
    if(!vehicleView.live||vehicleView.space!=tracking.spaceGeneration||!frame||vehicleCameraRecordCount>=vehicleCameraRecords.size())return false;
    const auto now=GetTickCount64();
    if(vehicleCameraRecordCount){const auto& previous=vehicleCameraRecords[vehicleCameraRecordCount-1];
        const bool changed=previous.owner!=vehicleView.owner||previous.tracking.spaceGeneration!=tracking.spaceGeneration||std::memcmp(&previous.tracking.head,&tracking.head,sizeof(tracking.head));
        if(!changed&&now-previous.ms<500)return false;
    }
    VehicleCameraRecord record;record.owner=vehicleView.owner;record.frame=frame;record.ms=now;record.tracking=tracking;
    if(!CameraEvidenceMatrix(native,record.native)||!CameraEvidenceMatrix(adjusted,record.adjusted)||!CameraEvidenceMatrix(planned[0],record.planned[0])||!CameraEvidenceMatrix(planned[1],record.planned[1]))return false;
    vehicleCameraRecords[vehicleCameraRecordCount++]=record;return true;
}
void ObserveVehicleCameraApplied(unsigned frame,unsigned eye,const RenderViewCopy& actual)noexcept {
    if(eye>1)return;std::lock_guard lock(vehicleViewMutex);
    if(!vehicleCameraRecordCount)return;auto& record=vehicleCameraRecords[vehicleCameraRecordCount-1];
    if(record.frame!=frame||record.appliedMask&(1u<<eye))return;
    if(CameraEvidenceMatrix(actual,record.applied[eye]))record.appliedMask|=1u<<eye;
}
void ReportBoatAimEvidence(std::ostream& out){
    std::lock_guard lock(vehicleViewMutex);out<<",\"boat_head_aim\":"<<(boatHeadAim?"true":"false")<<",\"boat_head_fire\":"<<(boatHeadFire?"true":"false")
        <<",\"boat_aim_samples\":"<<boatAimSamples.load()<<",\"boat_aim_rejects\":"<<boatAimRejects.load()<<",\"boat_aim_commits\":"<<boatAimCommits.load()<<",\"boat_aim_fire_commits\":"<<boatAimFireCommits.load()<<",\"boat_aim_camera_views\":"<<boatAimCameraViews.load()<<",\"boat_aim_records\":[";
    for(unsigned n=0;n<boatAimRecordCount;++n){const auto&r=boatAimRecords[n];if(n)out<<',';
        out<<"{\"ms\":"<<r.ms<<",\"sequence\":"<<r.sequence<<",\"yaw_command\":"<<r.yaw<<",\"pitch_command\":"<<r.pitch<<",\"yaw_error\":"<<r.yawError<<",\"pitch_error\":"<<r.pitchError<<",\"native_yaw\":"<<r.jointYaw<<",\"native_pitch\":"<<r.jointPitch<<",\"fire\":"<<r.fire<<'}';}
    out<<']';
}
void ReportVehicleCameraEvidence(std::ostream& out){
    std::lock_guard lock(vehicleViewMutex);const auto precision=out.precision();out.precision(9);out<<",\"vehicle_camera_evidence\":[";
    const auto matrix=[&](const math::Matrix4& m){out<<'[';for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){if(r||c)out<<',';out<<m.values[r][c];}out<<']';};
    const auto pose=[&](const math::Pose& p){out<<'['<<p.position.x<<','<<p.position.y<<','<<p.position.z<<','<<p.orientation.x<<','<<p.orientation.y<<','<<p.orientation.z<<','<<p.orientation.w<<']';};
    for(unsigned n=0;n<vehicleCameraRecordCount;++n){const auto& r=vehicleCameraRecords[n];if(n)out<<',';
        out<<"{\"native_frame\":"<<r.frame<<",\"ms\":"<<r.ms<<",\"actor\":"<<r.owner.actor<<",\"actor_generation\":"<<r.owner.generation<<",\"vehicle\":"<<r.owner.vehicle<<",\"entry\":"<<r.owner.seat<<",\"tracking_sequence\":"<<r.tracking.generation<<",\"space\":"<<r.tracking.spaceGeneration<<",\"predicted_ns\":"<<r.tracking.predictedNs<<",\"units_per_metre\":"<<r.tracking.worldUnitsPerMeter<<",\"applied_mask\":"<<r.appliedMask<<",\"reference_pose\":";pose(r.tracking.referenceHead);
        out<<",\"head_pose\":";pose(r.tracking.head);out<<",\"eye_poses\":[";pose(r.tracking.eyes[0]);out<<',';pose(r.tracking.eyes[1]);out<<"],\"native_camera\":";matrix(r.native);out<<",\"adjusted_camera\":";matrix(r.adjusted);
        out<<",\"planned_eyes\":[";matrix(r.planned[0]);out<<',';matrix(r.planned[1]);out<<"],\"applied_eyes\":[";matrix(r.applied[0]);out<<',';matrix(r.applied[1]);out<<"]}";
    }out<<']';out.precision(precision);
}
bool SetFeedbackWriter(FeedbackWriter writer)noexcept {
    if(!hooks[0]||enabled.load(std::memory_order_acquire)||writeFeedback||!writer)return false;writeFeedback=writer;return true;
}
bool SetReloadAudioWriters(ReloadAudioWriter writer,ReloadAudioOwnerWriter ownerWriter,ReloadAudioReadyReader readyReader)noexcept {
    if(!hooks[0]||enabled.load(std::memory_order_acquire)||writeReloadAudio||writeReloadAudioOwner||!writer||!ownerWriter||!readyReader)return false;
    writeReloadAudio=writer;writeReloadAudioOwner=ownerWriter;readReloadAudioReady=readyReader;return true;
}
bool SetInputObserver(InputObserver observer)noexcept {
    if(!hooks[0]||enabled.load(std::memory_order_acquire)||observeInput||!observer)return false;
    observeInput=observer;return true;
}
void Start()noexcept {
    turnOptions={};
    {
        char value[16]{};const auto n=GetEnvironmentVariableA("FVR_TURN_MODE",value,sizeof(value));
        if(n&&n<sizeof(value)){
            if(std::strcmp(value,"smooth")==0)turnOptions.smooth=true;
            else if(std::strcmp(value,"snap")==0)turnOptions.smooth=false;
        }
    }
    {
        char value[32]{};const auto n=GetEnvironmentVariableA("FVR_SMOOTH_TURN_DEG_PER_SEC",value,sizeof(value));
        if(n&&n<sizeof(value)){
            float speed=0;char trailing=0;
            if(sscanf_s(value,"%f%c",&speed,&trailing,1u)==1&&std::isfinite(speed)&&speed>=30.f&&speed<=360.f)
                turnOptions.degreesPerSecond=speed;
        }
    }
    // Stick weapon cycle: enabled by default; each switch is a kill switch.
    cycleSettings={};
    if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_ARSENAL_INPUT",value,sizeof(value))&&value[0]=='0')arsenalInputOff=true;
    if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_STICK_CYCLE",value,sizeof(value))&&value[0]=='0')cycleSettings.enabled=false;
    if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_STICK_CYCLE_MELEE_GRENADE",value,sizeof(value))&&value[0]=='0')cycleSettings.meleeAndGrenade=false;
    {interaction::StickCycleOptions stick{};stick.backward=cycleSettings.enabled;
        char value[16]{};const auto n=GetEnvironmentVariableA("FVR_BC2_STICK_CYCLE_BACK_MS",value,sizeof(value));
        unsigned ms=0;if(n&&n<sizeof(value)&&std::from_chars(value,value+n,ms).ec==std::errc{}&&ms>=250&&ms<=1500)stick.backwardHoldNs=std::int64_t(ms)*1000000;
        actions.SetStickCycle(stick);}
    if(char value[4]{};GetEnvironmentVariableA("FVR_BC2_TEST_INPUT_LOG",value,sizeof(value))&&value[0]=='1')testInputLog=true;
    if(char value[24]{};GetEnvironmentVariableA("FVR_BC2_TEST_SERVER_LOADED",value,sizeof(value))){int n=-1;unsigned after=0;
        if(sscanf_s(value,"%d,%u",&n,&after)>=1&&n>=0&&n<=200&&after<=60000){testServerLoaded=n;testServerLoadedAfterMs=after;}}
    if(char value[24]{};GetEnvironmentVariableA("FVR_BC2_TEST_HITCH_MS",value,sizeof(value))){unsigned ms=0,after=150;
        if(sscanf_s(value,"%u,%u",&ms,&after)>=1&&ms<=500&&after<=10000){testHitchMs=ms;testHitchAfterMs=after;testHitchOnStop=std::strstr(value,",stop")!=nullptr;testHitchOnHold=std::strstr(value,",hold")!=nullptr;}}
    reloadFlowRuntime::Start();physicalWeaponRuntime::Start();rigPublication::Start();enabled.store(true,std::memory_order_release);}
bool Stop()noexcept {
    enabled.store(false,std::memory_order_release);
    reloadAudioSuppression.store({},std::memory_order_release);
    if(writeReloadAudioOwner)writeReloadAudioOwner(0,0,0);reloadAudioWeapon=0;
    nativeRecoilContext.store({},std::memory_order_release);
    nativeRecoilPose.store({},std::memory_order_release);
    rigPublication::PublishWeaponVisibility({});weaponVisibilitySample.store({},std::memory_order_release);
    bodyHolsterSample.store({},std::memory_order_release);
    bool okay=rigPublication::Stop();
    const bool reloadOkay=reloadFlowRuntime::Stop();okay&=reloadOkay;
    okay&=physicalWeaponRuntime::Stop();
    for(auto entry:hooks)if(entry){const auto r=MH_DisableHook(entry);okay&=r==MH_OK||r==MH_ERROR_DISABLED;}
    const auto until=GetTickCount64()+2000;while(active.load()&&GetTickCount64()<until)Sleep(1);
    if(!active.load()&&magazineReloadFixture){LARGE_INTEGER now{};QueryPerformanceCounter(&now);magazineReloadFixture->Stop(HandNanos(now.QuadPart));}
    if(!active.load()&&reloadRequestFixture){LARGE_INTEGER now{};QueryPerformanceCounter(&now);reloadRequestFixture->Stop(HandNanos(now.QuadPart));}
    if(!active.load()&&twoHandGrip){
        CancelSight();ResetSupportGrip("session_stop");ResetHandOwnership("session_stop");
        gatherSightRequest=gatherSightAck=0;gatherSightCommit=false;RecordHandOwnership();
    }
    selectedMeshes.Clear();if(bodyInventory&&!active.load())bodyInventory->Cancel();
    if(!active.load()&&weaponVisibilityProbe){LARGE_INTEGER now{};QueryPerformanceCounter(&now);weaponVisibilityProbe->Cancel(9,HandNanos(now.QuadPart));
        rigPublication::PublishWeaponVisibility({});weaponVisibilitySample.store({},std::memory_order_release);}
    return okay&&!active.load();
}
std::optional<ReloadHintSnapshot> ReadReloadHints(std::int64_t now)noexcept{
    const auto p=reloadHintPublication.load(std::memory_order_acquire);
    if(!p||p->observedNs<=0||now<p->observedNs||p->deadlineNs<=now||now-p->observedNs>100000000)return {};return *p;
}
bool ReloadGuidanceEnabled()noexcept{return reloadGuidanceEnabled;}
void AppendReloadGuidanceProps(std::int64_t now,std::uint64_t space,std::uint64_t frame,unsigned eye,
    bool pouchGeometryAvailable,graphics::BodyPropEye& props)noexcept {
    if(!reloadGuidanceEnabled)return;
    static std::mutex mutex;static std::uint64_t pairFrame=0;static graphics::BodyPropEye pair;
    std::lock_guard lock(mutex);
    if(eye==0||pairFrame!=frame||!frame){pairFrame=frame;pair=rigPublication::ReadReloadGuidanceProps(now,pouchGeometryAvailable);}
    for(unsigned n=0;n<pair.count&&props.count<graphics::MaxBodyProps;++n){const auto& p=pair.instances[n];
        if(p.spaceGeneration==space&&graphics::BodyPropFresh(p,now))props.instances[props.count++]=p;}
}
const interaction::InteractionZoneTable& InteractionZoneTable()noexcept{return interactionZones;}
HandsPresentationMode HandsMode()noexcept{return handsModeSettings.mode;}
void AppendDroppedMagazineProps(std::int64_t now,std::uint64_t space,std::uint64_t frame,unsigned eye,graphics::BodyPropEye& props)noexcept{
    std::shared_ptr<const DroppedPropPublication> p;
    {std::lock_guard lock(droppedPairMutex);
        if(eye==0||droppedPair.frame!=frame||!frame){if(eye!=0)++droppedPairSplits;droppedPair={frame,droppedPublication.load(std::memory_order_acquire)};}
        else ++droppedPairReuses;
        p=droppedPair.publication;}
    if(!p)return;bool any=false;
    for(unsigned n=0;n<p->count&&props.count<graphics::MaxBodyProps;++n){const auto& i=p->items[n];
        if(i.spaceGeneration==space&&graphics::BodyPropFresh(i,now)){props.instances[props.count++]=i;any=true;}}
    if(any)++droppedAppended;
}
// Atomic counters and Start-time settings only: safe for the periodic writer.
void WriteSwitchTelemetry(std::ostream& out){
    out<<"{\"hooked\":"<<(weaponSwitchHooked?"true":"false")<<",\"stick_cycle\":"<<(cycleSettings.enabled?"true":"false")
       <<",\"melee_grenade\":"<<(cycleSettings.meleeAndGrenade?"true":"false")
       <<",\"requests\":{\"flick_next\":"<<switchRequests[0].load()<<",\"flick_previous\":"<<switchRequests[1].load()
       <<",\"cycle_forward\":"<<switchRequests[2].load()<<",\"cycle_backward\":"<<switchRequests[3].load()<<'}'
       <<",\"latched\":"<<flickLatched.load()<<",\"blocked\":"<<flickBlocked.load()<<",\"selected\":"<<flickSelected.load()<<",\"no_target\":"<<flickNoTarget.load()
       <<",\"gated\":"<<flickGated.load()<<",\"expired\":"<<flickExpired.load()<<",\"no_inventory\":"<<flickNoInventory.load()
       <<",\"last_from\":"<<flickLastFrom.load()<<",\"last_target\":"<<flickLastTarget.load()<<",\"equipment_owned\":"<<flickEquipmentOwned.load()
       <<",\"coalesced\":"<<switchCoalesced.load()<<",\"unchanged\":"<<switchUnchanged.load()<<",\"skipped_identity\":"<<switchSkippedIdentity.load()
       <<",\"skipped_invalid\":"<<switchSkippedInvalid.load()<<",\"select_rejected\":"<<switchSelectRejected.load()
       <<",\"reload_blocked\":"<<switchReloadBlocked.load()<<",\"reload_cancelled\":"<<switchReloadCancelled.load()
       <<",\"deferred\":"<<switchDeferred.load()<<",\"deferred_expired\":"<<switchDeferredExpired.load()
       <<",\"body_blocked\":"<<switchBodyBlocked.load()<<",\"mode_blocked\":"<<switchModeBlocked.load()<<",\"conflicts\":"<<switchConflicts.load()
       <<",\"flick_switches\":"<<switchFlickHandsKept.load()<<",\"outcome_selected\":"<<switchSelectedSeen.load()<<",\"outcome_failed\":"<<switchFailedSeen.load()
       <<",\"feedback_sent\":"<<switchFeedbackSent.load()<<",\"feedback_dropped\":"<<switchFeedbackDropped.load()<<",\"selected_slots\":[";
    for(unsigned n=0;n<switchSelectedSlots.size();++n){if(n)out<<',';out<<switchSelectedSlots[n].load();}
    out<<"]}";
}
void ReportWeaponEffects(std::ostream& out){WeaponEffectEvidence().Write(out,effectAnchorConfig,effectAnchorInstalled);}
std::uint64_t CurrentGatherTick()noexcept {return gathers.load(std::memory_order_relaxed);}
void ReportControls(std::ostream& out){
    out<<"{\"turn_mode\":\""<<(turnOptions.smooth?"smooth":"snap")<<"\",\"smooth_turn_degrees_per_second\":"<<turnOptions.degreesPerSecond
       <<",\"turn_deadzone\":"<<turnOptions.deadzone<<",\"turn_updates\":"<<turns.load()
       <<",\"reload_hold_ms\":600,\"reload_short_releases\":"<<shortReloadReleases.load()
       <<",\"reload_long_holds\":"<<automaticReloadHolds.load()<<",\"reload_native_requests\":"<<automaticReloadStarts.load()
       <<",\"reload_cancellations\":"<<automaticReloadCancels.load()
       <<",\"death_recovery\":{\"null_weapon_guard_installed\":"<<(nullWeaponGuardInstalled?"true":"false")
       <<",\"null_weapon_guard_calls\":"<<nullWeaponGuardCalls.load(std::memory_order_relaxed)
       <<",\"null_weapon_guard_hits\":"<<nullWeaponGuardHits.load(std::memory_order_relaxed)
       <<",\"actor_loss_rebind\":"<<(actorLossRebind?"true":"false")<<",\"actor_losses\":"<<actorLossObserved.load(std::memory_order_relaxed)
       <<",\"actor_loss_rebinds\":"<<actorLossRebinds.load(std::memory_order_relaxed)<<",\"actor_loss_short\":"<<actorLossTransient.load(std::memory_order_relaxed)
       <<",\"last_actor_loss_ms\":"<<lastActorLossMs.load(std::memory_order_relaxed)<<'}'
       <<",\"weapon_switch\":";WriteSwitchTelemetry(out);
    out<<",\"support\":{\"grabs\":"<<supportGrabs.load()<<",\"releases\":"<<supportReleases.load()<<",\"held_samples\":"<<supportSamples.load()<<",\"release_reasons\":[";
    for(unsigned i=0;i<supportReleaseCounts.size();++i){if(i)out<<',';out<<supportReleaseCounts[i].load();}
    out<<"]},\"reload_audio\":{\"enabled\":"<<(PhysicalEventAudioEnabled()?"true":"false")
       <<",\"writers_ready\":"<<(writeReloadAudio&&writeReloadAudioOwner&&readReloadAudioReady?"true":"false")
       <<",\"sent\":"<<reloadAudioSent.load()<<",\"dropped\":"<<reloadAudioDropped.load()
       <<",\"owner_published\":"<<reloadAudioOwnerPublished.load()<<",\"owner_rejected\":"<<reloadAudioOwnerRejected.load()
       <<",\"ready_samples\":"<<reloadAudioReadySamples.load()<<",\"not_ready\":"<<reloadAudioNotReady.load()
       <<",\"proof_attempts\":"<<reloadAudioProofAttempts.load()<<",\"proof_accepted\":"<<reloadAudioProofAccepted.load()
       <<",\"proof_failure\":\""<<OwnedReloadAudioFailureName(OwnedReloadAudioFailure(reloadAudioProofFailure.load()))
       <<"\",\"proof_reads\":"<<reloadAudioProofReads.load()<<",\"proof_unreadable\":"<<(reloadAudioProofUnreadable.load()?"true":"false")
       <<",\"lane_samples\":"<<reloadAudioLaneSamples.load()<<",\"native_suppressed\":"<<reloadAudioSuppressed.load()<<",\"role_sent\":[";
    for(unsigned i=0;i<reloadAudioRoleSent.size();++i){if(i)out<<',';out<<reloadAudioRoleSent[i].load();}out<<"]}}";
}
void Report(std::ostream& out){out<<"{\"installed\":"<<(hooks[0]&&hooks[1]?"true":"false")
    <<",\"thread\":"<<inputThread.load()<<",\"updates\":"<<updates.load()<<",\"local_updates\":"<<localUpdates.load()
    <<",\"gathers\":"<<gathers.load()<<",\"on_foot\":"<<onFoot.load()<<",\"vehicle_gated\":"<<vehicle.load()
    <<",\"stale_or_missing\":"<<stale.load()<<",\"applied\":"<<applied.load()<<",\"committed\":"<<committed.load()
    <<",\"movement_samples\":"<<moving.load()<<",\"button_samples\":"<<buttons.load()
    <<",\"death_probe_requested\":"<<(deathProbe?"true":"false")<<",\"grenade_samples\":"<<grenadeSamples.load()
    <<",\"left_only_action_samples\":"<<leftOnlyActions.load()<<",\"right_only_action_samples\":"<<rightOnlyActions.load()
    <<",\"fire_with_left_tracking_missing\":"<<leftLossFire.load()<<",\"untracked_fire_samples\":"<<untrackedFire.load()<<",\"recovery_fire_suppressed\":"<<recoveryFireSuppressed.load()
    <<",\"equip_probe_requested\":"<<(equipProbe?"true":"false")<<",\"equip_commands\":"<<equipCommands.load()
    <<",\"weapon_mode_binding_verified\":"<<(weaponModeBindingVerified?"true":"false")
    <<",\"weapon_mode_requests\":"<<modeRequests.load()<<",\"weapon_mode_rejections\":"<<modeRejections.load()<<",\"weapon_mode_acknowledgements\":"<<modeAcknowledgements.load()
    <<",\"boat_driver_commits\":"<<vehicleDriveCommits.load()<<",\"boat_camera_views\":"<<vehicleCameraViews.load()
    <<",\"boat_camera_stale_waits\":"<<vehicleCameraWaits.load()<<",\"boat_camera_unsupported_frames\":"<<vehicleCameraUnsupportedFrames.load()
    <<",\"vehicle_route_rejects\":"<<vehicleRouteRejects.load()<<",\"vehicle_unsupported_samples\":"<<vehicleUnsupported.load()
    <<",\"vehicle_entity\":"<<vehicleSeatEntity.load()<<",\"vehicle_entry\":"<<vehicleSeatEntry.load()<<",\"vehicle_route_fingerprint\":"<<vehicleRouteFingerprint.load()
    <<",\"vehicle_last_throttle\":"<<vehicleThrottle.load()<<",\"vehicle_last_steer\":"<<vehicleSteer.load()
    <<",\"vehicle_throttle_nonzero_commits\":"<<vehicleThrottleNonzero.load()<<",\"vehicle_steer_nonzero_commits\":"<<vehicleSteerNonzero.load()
    <<",\"vehicle_throttle_max_abs\":"<<vehicleThrottleMaximum.load()<<",\"vehicle_steer_max_abs\":"<<vehicleSteerMaximum.load()<<",\"vehicle_cache_readback_failures\":"<<vehicleReadbackFailures.load()
    <<",\"vehicle_driver_native_look\":false,\"vehicle_driver_native_fire\":false,\"vehicle_gather_evidence\":[";
    for(unsigned i=0;i<vehicleGatherCount.load(std::memory_order_acquire);++i){const auto& row=vehicleGatherRecords[i];if(i)out<<',';
        out<<"{\"entity\":"<<row.entity<<",\"entry\":"<<row.entry<<",\"soldier\":"<<row.soldier<<",\"weak\":"<<row.weak<<",\"actor_generation\":"<<row.actorGeneration<<",\"seat_generation\":"<<row.seatGeneration<<",\"space\":"<<row.space<<",\"input_sequence\":"<<row.inputSequence<<",\"ms\":"<<row.ms<<",\"axis_mask\":"<<row.mask<<",\"permissions\":"<<row.permissions<<",\"before\":["<<row.before[0]<<','<<row.before[1]<<"],\"written\":["<<row.written[0]<<','<<row.written[1]<<"],\"observed\":["<<row.observed[0]<<','<<row.observed[1]<<"],\"readback_verified\":"<<(row.verified?"true":"false")<<'}';
    }
    out<<']';ReportVehicleCameraEvidence(out);ReportBoatAimEvidence(out);
    out
    <<",\"vehicle_interact_samples\":"<<vehicleInteractions.load()<<",\"next_weapon_commands\":"<<nextWeaponCommands.load()<<",\"previous_weapon_commands\":"<<previousWeaponCommands.load()
    <<",\"native_input_inactive\":"<<nativeInactive.load()<<",\"mismatches\":"<<mismatches.load()
    <<",\"motion_aim_requested\":"<<(motionAim?"true":"false")<<",\"motion_aim_samples\":"<<aimSamples.load()<<",\"sprint_heading_samples\":"<<sprintHeadings<<",\"independent_view_bases\":"<<baseViews.load()
    <<",\"turn_mode\":\""<<(turnOptions.smooth?"smooth":"snap")<<"\",\"smooth_turn_degrees_per_second\":"<<turnOptions.degreesPerSecond
    <<",\"turn_deadzone\":"<<turnOptions.deadzone<<",\"smooth_turn_max_step_ms\":50"
    <<",\"snap_turns\":"<<(turnOptions.smooth?0:turns.load())<<",\"turn_updates\":"<<turns.load()<<",\"unbound_turn_requests\":"<<turnUnavailable.load()<<",\"independent_weapon_aim\":false,\"body_collision_roomscale\":false,\"view_basis_evidence\":[";
    for(unsigned i=0;i<baseRecordCount.load(std::memory_order_acquire);++i){const auto& r=baseRecords[i];if(i)out<<',';
        out<<"{\"weapon\":"<<r.weapon<<",\"ms\":"<<r.ms<<",\"space\":"<<r.space<<",\"native_yaw\":"<<r.nativeYaw<<",\"native_pitch\":"<<r.nativePitch<<",\"source_yaw\":"<<r.sourceYaw<<",\"view_yaw\":"<<r.viewYaw<<",\"body_yaw\":"<<r.bodyYaw<<",\"shared_eye\":"<<(r.sharedEye?"true":"false")<<",\"source_position\":["<<r.sourcePosition.x<<','<<r.sourcePosition.y<<','<<r.sourcePosition.z<<"],\"eye_position\":["<<r.eyePosition.x<<','<<r.eyePosition.y<<','<<r.eyePosition.z<<"]}";}

    out<<']';ReportAimAngles(out,handFrequency);ReportNativeRecoilObservation(out);
    out<<",\"weapon_mode_records\":[";
    for(unsigned n=0;n<modeRecordCount;++n){const auto& r=modeRecords[(modeRecordNext+modeRecords.size()-modeRecordCount+n)%modeRecords.size()];if(n)out<<',';
        out<<"{\"ms\":"<<r.ms<<",\"ack_ms\":"<<r.ackMs<<",\"actor\":"<<r.actor<<",\"weak\":"<<r.weak<<",\"owner\":"<<r.owner<<",\"space\":"<<r.space<<",\"gesture\":"<<r.gesture<<",\"from\":"<<r.from<<",\"target\":"<<r.target<<",\"action\":"<<r.action<<",\"cancelled\":"<<(r.cancelled?"true":"false")<<'}';
    }
    out<<"],\"hand_ownership\":{\"schema\":1,\"event_capacity\":"<<handEvents.size()
       <<",\"checks\":"<<handChecks<<",\"overlap_failures\":"<<handOverlap
       <<",\"support_without_claim\":"<<handMissingSupport<<",\"sight_without_claim\":"<<handMissingSight
       <<",\"lease_failures\":"<<handLeaseFailures<<",\"claim_rejections\":"<<handClaimRejected
       <<",\"events_total\":"<<handEventsTotal<<",\"events_dropped\":"<<handEventsDropped<<",\"events\":[";
    const auto claimJson=[&](const std::optional<interaction::HandClaim>& claim){
        if(!claim){out<<"null";return;}const auto& t=claim->token;
        out<<"{\"id\":"<<t.id<<",\"kind\":"<<unsigned(t.kind)<<",\"item\":"<<t.item.id<<",\"generation\":"<<t.item.generation
           <<",\"contact\":"<<t.contact.id<<",\"contact_generation\":"<<t.contact.generation<<",\"prerequisite\":"<<t.prerequisiteClaim
           <<",\"input_generation\":"<<claim->inputSequence<<",\"deadline_ns\":"<<claim->deadlineNs<<'}';
    };
    for(unsigned n=0;n<handEventCount;++n){
        if(n)out<<',';const auto& r=handEvents[n];
        out<<"{\"serial\":"<<r.serial<<",\"now_ns\":"<<r.nowNs<<",\"raw_generation\":"<<r.rawGeneration
           <<",\"actor\":"<<unsigned(r.owner.actor)<<",\"weak\":"<<unsigned(r.owner.actor>>32)
           <<",\"rig_epoch\":"<<r.owner.actorGeneration<<",\"equip_generation\":"<<r.owner.equipGeneration<<",\"space\":"<<r.owner.space
           <<",\"weapon\":"<<r.weapon<<",\"physical_item\":"<<r.item.id<<",\"reason\":\""<<r.reason<<"\",\"left\":";claimJson(r.left);
        out<<",\"right\":";claimJson(r.right);
        out<<",\"support_holding\":"<<(r.supportHolding?"true":"false")<<",\"support_token\":"<<r.supportToken
           <<",\"sight_phase\":"<<r.sightPhase<<",\"sight_request\":"<<r.sightRequest<<",\"sight_ack\":"<<r.sightAck
           <<",\"sight_commit\":"<<(r.sightCommit?"true":"false")<<'}';
    }
    out<<"]}";
    out<<",\"selected_meshes_observer\":";selectedMeshes.Report(out);
    out<<",\"reload_flow\":";reloadFlowRuntime::Report(out);
    out<<",\"physical_weapon_authority\":";physicalWeaponRuntime::Report(out);
    if(bodyHolsterFixture){out<<",\"body_holster_probe\":";bodyHolsterFixture->Report(out);}
    if(weaponVisibilityProbe){out<<",\"weapon_visibility_probe\":";weaponVisibilityProbe->Report(out);
        out<<",\"weapon_visibility_action_suppression\":{\"verified_commits\":"<<weaponVisibilityActionCommits.load()
           <<",\"failures\":"<<weaponVisibilityActionFailures.load()<<'}';}
    if(emptyFireFixture){out<<",\"empty_fire_probe\":";emptyFireFixture->Report(out);}
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(armingEmptyFixture){out<<",\"arming_empty_probe\":";armingEmptyFixture->Report(out);}
#endif
    if(physicalReloadFixture){out<<",\"physical_reload_probe\":";physicalReloadFixture->Report(out);}
    if(physicalReload){out<<",\"physical_reload\":";if(physicalWeaponRuntime::Installed()&&physicalTube)physicalTube->Report(out);else physicalReload->Report(out);
        {const auto r=reloadSoundGate.Stats();out<<",\"anim_sound\":{\"hooked\":"<<(hooks[13]?"true":"false")<<",\"stop_hooked\":"<<(hooks[14]?"true":"false")
            <<",\"starts\":"<<animSoundStarts.load()<<",\"played\":"<<animSoundPlayed.load()
            <<",\"rack_audition\":{\"profile_valid\":"<<(RackAudioProfile()?"true":"false")<<",\"attempts\":"<<rackAudioAttempts.load()
            <<",\"rejected\":"<<rackAudioRejected.load()<<",\"played\":"<<rackAudioPlayed.load()<<",\"stopped\":"<<rackAudioStopped.load()
            <<",\"reject_proof\":"<<rackRejectProof.load()<<",\"reject_resolve\":"<<rackRejectResolve.load()<<",\"reject_owner_changed\":"<<rackRejectChanged.load()
            <<",\"reject_request\":"<<rackRejectRequest.load()<<",\"request_reason\":"<<unsigned(rackAudioAudition.LastRequestReject())
            <<",\"proof_failure\":\""<<ReloadAudioResolveFailureName(ReloadAudioResolveFailure(rackProofFailure.load()))<<"\",\"proof_reads\":"<<rackProofReads.load()
            <<",\"proof_unreadable\":"<<(rackProofUnreadable.load()?"true":"false")<<",\"dispatch_reason\":"<<unsigned(rackAudioAudition.LastDispatchReject())
            <<",\"listener_misses\":"<<rackListenerMisses.load()
            <<",\"reject_callback\":"<<rackRejectCallback.load()<<",\"reject_dispatch_owner\":"<<rackRejectDispatchOwner.load()<<'}'
            <<",\"reload_sound\":{\"enabled\":"<<(reloadSoundEnabled?"true":"false")
            <<",\"pump\":{\"learned\":"<<pumpSoundGate.Stats().learned<<",\"withheld\":"<<pumpSoundGate.Stats().withheld<<",\"replayed\":"<<pumpSoundGate.Stats().replayed<<",\"passed_early\":"<<pumpSoundGate.Stats().passedEarly<<",\"stopped\":"<<pumpSoundGate.Stats().stopped<<",\"expired\":"<<pumpSoundGate.Stats().expired<<"}"
            <<",\"learned\":"<<r.learned<<",\"withheld\":"<<r.withheld<<",\"replayed\":"<<r.replayed<<",\"stopped\":"<<r.stopped<<",\"dropped\":"<<r.dropped<<",\"anim_thread\":"<<animThreadId.load()<<",\"gather_thread\":"<<inputThread.load()<<",\"dispatch_hooked\":"<<(hooks[15]?"true":"false")<<",\"dispatch_work\":"<<animDispatchWork.load()<<",\"dispatch_owners\":"<<animDispatchOwners.load()<<",\"dispatch_thread\":"<<animDispatchThread.load()<<",\"animation_hook_thread\":"<<animationHookThread.load()<<",\"listener_calls\":"<<animSoundCalls.load()<<",\"anim_sound_in_soldier_update\":"<<animSoundInSoldierUpdate.load()<<",\"expired\":"<<r.expired<<"}}";}
        out<<",\"weapon_switch\":";WriteSwitchTelemetry(out);
        out<<",\"stale_owner_holds\":"<<staleOwnerHolds<<",\"test_hitch\":{\"ms\":"<<testHitchMs<<",\"after_ms\":"<<testHitchAfterMs<<",\"on_stop\":"<<(testHitchOnStop?"true":"false")<<",\"fired\":"<<testHitches<<",\"begin_qpc_ns\":"<<testHitchBeginQpcNs<<",\"end_qpc_ns\":"<<testHitchEndQpcNs<<'}'
            <<",\"test_server_loaded\":{\"n\":"<<testServerLoaded<<",\"after_ms\":"<<testServerLoadedAfterMs<<",\"done\":"<<(testServerLoadedDone?"true":"false")
            <<",\"before\":"<<testServerLoadedBefore<<",\"firing\":"<<testServerLoadedFiring<<",\"qpc_ns\":"<<testServerLoadedNs<<'}';
        if(testInputLog){out<<",\"test_input_log\":{\"total\":"<<testInputCount<<",\"capacity\":"<<testInputRows.size()
            <<",\"qpc_frequency\":"<<handFrequency<<",\"body_position_admitted\":"<<(bodyFollow?"true":"false")
            <<",\"native_sprint_gate_verified\":false,\"body_position_coordinate_order\":[\"x\",\"y\",\"negated_native_z\"],\"rows\":[";const unsigned n=std::min<unsigned>(testInputCount,unsigned(testInputRows.size()));
            for(unsigned i=0;i<n;++i){const auto& r=testInputRows[(testInputCount-n+i)%testInputRows.size()];if(i)out<<',';
                out<<"{\"tick\":"<<r.tick<<",\"held\":"<<r.action<<",\"native\":["<<r.native[0]<<','<<r.native[1]<<','<<r.native[2]<<"],\"committed\":["<<r.committed[0]<<','<<r.committed[1]<<','<<r.committed[2]<<"],\"state\":["<<r.state[0]<<','<<r.state[1]<<']'
                   <<",\"sprint_requested\":"<<((r.action&interaction::Sprint)?"true":"false")
                   <<",\"sprint_submitted_valid\":"<<(r.committedButtonsValid?"true":"false")
                   <<",\"right_aim_tracked\":"<<(r.rightAimTracked?"true":"false")
                   <<",\"desired_body_axes\":["<<r.desiredBodyAxes[0]<<','<<r.desiredBodyAxes[1]<<']'
                   <<",\"movement_basis_valid\":"<<(r.movementBasisValid?"true":"false")
                   <<",\"submitted_axes_valid\":"<<(r.submittedAxesValid?"true":"false")
                   <<",\"player\":"<<r.player<<",\"soldier\":"<<r.soldier<<",\"weak\":"<<r.weak<<",\"weapon\":"<<r.weapon
                   <<",\"space\":"<<r.space<<",\"actor_generation\":"<<r.actorGeneration<<",\"equipment_generation\":"<<r.equipmentGeneration
                   <<",\"world_units_per_meter\":"<<r.worldUnitsPerMeter<<",\"first_input\":"<<r.firstInput<<",\"last_input\":"<<r.lastInput
                   <<",\"completed_updates\":"<<r.completedUpdates<<",\"before_qpc\":"<<r.beforeQpc<<",\"after_qpc\":"<<r.afterQpc
                   <<",\"position_owner_current\":"<<(r.positionOwnerCurrent?"true":"false")
                   <<",\"after_update_sprint_valid\":"<<(r.afterUpdateSprintValid?"true":"false")
                   <<",\"position_before_valid\":"<<(r.positionBefore?"true":"false")<<",\"position_after_valid\":"<<(r.positionAfter?"true":"false");
                if(r.afterUpdateSprintValid)out<<",\"after_update_sprint\":"<<(r.afterUpdateSprint?"true":"false");
                if(r.committedButtonsValid)out<<",\"sprint_submitted\":"<<((r.committed[1]&(1u<<31))?"true":"false");
                if(r.movementBasisValid)out<<",\"body_native_yaw_radians\":["<<r.movementYaw[0]<<','<<r.movementYaw[1]<<']';
                if(r.submittedAxesValid)out<<",\"submitted_native_axes\":["<<r.submittedNativeAxes[0]<<','<<r.submittedNativeAxes[1]<<']';
                const auto position=[&](const char* name,const std::optional<math::Vec3>& value){if(value)out<<",\""<<name<<"\":["<<value->x<<','<<value->y<<','<<value->z<<']';};
                position("position_before",r.positionBefore);position("position_after",r.positionAfter);
                out<<'}';}
            out<<"]}";}
        out<<",\"reload_hints\":{\"lit\":["<<hintLit[0]<<','<<hintLit[1]<<','<<hintLit[2]<<','<<hintLit[3]<<','<<hintLit[4]<<"],\"magwell_ghost_frames\":"<<hintGhostFrames<<",\"magazine_glow_frames\":"<<hintGlowFrames<<'}';
        out<<",\"stock_fallback\":{\"grants\":"<<stockFallbacks<<",\"skips\":"<<stockFallbackSkips<<",\"fire_held_for_stop\":"<<fireHeldForStop<<",\"fire_replays\":"<<fireReplays<<'}';
        out<<",\"reload_button\":{\"hold_ms\":600,\"short_releases\":"<<shortReloadReleases<<",\"long_holds\":"<<automaticReloadHolds
           <<",\"native_starts\":"<<automaticReloadStarts<<",\"cancelled\":"<<automaticReloadCancels<<'}';
        out<<",\"physical_cancel_sites\":{\"other\":"<<physicalCancelSites[0]<<",\"owner_cleared\":"<<physicalCancelSites[1]<<",\"hand_reset\":"<<physicalCancelSites[2]
           <<",\"input_rejected\":"<<physicalCancelSites[3]<<",\"hand_reset_reason\":\""<<physicalCancelReason<<"\"}";
        out<<",\"magazine_support\":{\"asset\":\""<<magazineSupportAsset.data()<<"\",\"status\":\""<<MagazineSupportName(magazineSupportStatus)<<"\",\"changes\":"<<magazineSupportChanges<<'}';
        {const auto& d=droppedMagazines.Stats();out<<",\"dropped_magazines\":{\"dropped\":"<<d.dropped<<",\"landed\":"<<d.landed<<",\"expired\":"<<d.expired<<",\"recycled\":"<<d.recycled<<",\"rejected\":"<<d.rejected<<",\"drawn_frames\":"<<droppedDrawn<<",\"published_frames\":"<<droppedPublished<<",\"appended_eyes\":"<<droppedAppended<<",\"culled_frames\":"<<droppedCulled<<",\"pair_reuses\":"<<droppedPairReuses<<",\"pair_splits\":"<<droppedPairSplits
            <<",\"enabled\":"<<(droppedSettings.enabled?"true":"false")<<",\"lifetime_s\":"<<droppedSettings.lifetimeSeconds<<",\"max_live\":"<<droppedSettings.maxLive
            <<",\"visible_m\":"<<droppedSettings.visibleMeters<<",\"disabled_drops\":"<<droppedDisabled<<",\"bounces\":"<<d.bounces<<",\"forced_settles\":"<<d.forcedSettles
            <<",\"spawn\":{\"committed\":"<<droppedSpawner.Counts().committed<<",\"spawned\":"<<droppedSpawner.Counts().spawned
            <<",\"lost\":"<<droppedSpawner.Counts().lost<<",\"deferred_gathers\":"<<droppedSpawner.Counts().deferredGathers<<'}';
        out<<",\"interaction_zones\":{\"zones\":"<<(zoneSettings.zones?"true":"false")<<",\"magnet\":"<<(zoneSettings.magnet?"true":"false")
            <<",\"hover_cues\":"<<(zoneSettings.hoverCues?"true":"false")<<",\"scale\":"<<zoneSettings.scale
            <<",\"support_keep_on_flick\":"<<(zoneSettings.supportKeepOnFlick?"true":"false")<<",\"support_flick_kept\":"<<supportFlickKept
            <<",\"hands_mode\":\""<<bc2::HandsModeName(handsModeSettings.mode)<<"\",\"hands_mode_invalid\":"<<(handsModeSettings.invalid?"true":"false")
            <<",\"support_capture_m\":"<<supportGrip.Config().captureMeters<<",\"support_release_m\":"<<supportGrip.Config().releaseMeters;
        if(magazinePhysical){const auto h=magazinePhysical->HoverCues();out<<",\"hover\":{\"magazine\":"<<h.magazineHover<<",\"handle\":"<<h.handleHover<<",\"pouch\":"<<h.pouchHover<<'}';}
        out<<'}';
         const auto fc=droppedFloor.Counts();out<<",\"floor\":{\"ground\":"<<fc.ground<<",\"offset\":"<<fc.offset<<",\"play_space\":"<<fc.playSpace<<",\"fixed\":"<<fc.fixed
            <<",\"offset_observed\":"<<fc.observed<<",\"offset_rejected\":"<<fc.rejected<<",\"offset_m\":";
         if(const auto o=droppedFloor.Offset())out<<*o;else out<<"null";out<<"}}";}
        out<<",\"spas_pump\":{\"enabled\":"<<(pumpEnabled?"true":"false")<<",\"rear_stops\":"<<pumpRearStops<<",\"racks\":"<<pumpRacks<<",\"frames\":"<<pumpFrames<<",\"grip_frames\":"<<pumpGripFrames<<",\"max_travel\":"<<pumpMaxTravel<<",\"along_min\":"<<pumpMinAlong<<",\"along_max\":"<<pumpMaxAlong<<",\"zone_rejects\":"<<pumpZoneRejects<<",\"lateral_min\":"<<pumpLateralMin<<",\"lateral_max\":"<<pumpLateralMax<<",\"settles\":"<<pumpSettles<<",\"settle_ms\":"<<PumpSettleNs()/1000000<<"}";}
    out<<",\"context_interact\":";contextInteractEvidence.Report(out);
    if(magazinePhysical){out<<',';magazinePhysical->Report(out);}
    if(magazinePhysicalFixture){out<<",\"magazine_physical_probe\":";magazinePhysicalFixture->Report(out);
        const auto& b=prepBurst;out<<",\"prep_burst\":{\"window_frames\":"<<b.window<<",\"no_lease\":"<<b.noLease<<",\"not_full\":"<<b.notFull
            <<",\"injected\":"<<b.injected<<",\"policy_inactive\":"<<b.policyInactive<<",\"fire_derived\":"<<b.fireDerived
            <<",\"fire_masked\":"<<b.fireMasked<<",\"magazine_blocked\":"<<b.blocked<<",\"fire_sent\":"<<b.fireSent<<"}";
        out<<",\"magazine_cancel_terms\":{\"any\":"<<magCancelTerms[0]<<",\"body_draw\":"<<magCancelTerms[1]<<",\"aim_invalid\":"<<magCancelTerms[2]
            <<",\"policy_inactive\":"<<magCancelTerms[3]<<",\"fixture\":"<<magCancelTerms[4]<<",\"physical_reload\":"<<magCancelTerms[5]<<'}';}
    if(magazineDetached){out<<',';magazineDetached->Report(out);}
    if(magazineDetached)out<<",\"magazine_recovery_suppression\":{\"commits\":"<<magazineRecoverySuppressionCommits<<",\"failures\":"<<magazineRecoverySuppressionFailures<<'}';
    if(magazineDetachedFixture){out<<",\"magazine_detached_probe\":";magazineDetachedFixture->Report(out);}
    out<<",\"reload_feedback\":{\"writer_installed\":"<<(writeFeedback?"true":"false")<<",\"sent\":"<<feedbackSent<<",\"dropped\":"<<feedbackDropped<<'}';
    if(bodyInventory)bodyInventory->Report(out);
    if(bodyHolster){out<<",\"body_holster_transitions\":";bodyHolster->Report(out);out<<",\"body_holster_lifecycle\":";bodyHolsterLifecycle.Report(out);}
    out<<",\"body_holster\":{\"enabled\":"<<(bodyHolster?"true":"false")<<",\"suppression_commits\":"<<holsterSuppressionCommits
       <<",\"suppression_failures\":"<<holsterSuppressionFailures<<",\"free_publications\":"<<holsterFreePublications
       <<",\"diagnostic_profile\":"<<(bodyHolster?unsigned(bodyHolster->DiagnosticProfile()):0)<<",\"diagnostic_start_ns\":"<<(bodyHolster?bodyHolster->DiagnosticStart():0)<<",\"diagnostic_deadline_ns\":"<<(bodyHolster?bodyHolster->DiagnosticDeadline():0)
       <<",\"diagnostic_grants_production_acceptance\":false}";
    if(magazineReloadFixture){out<<",\"magazine_reload_probe\":";magazineReloadFixture->Report(out);}
    if(reloadRequestFixture){out<<",\"reload_request_probe\":";reloadRequestFixture->Report(out);}
    out<<",\"reload_observer\":{\"binding_verified\":"<<(reloadStateBinding?"true":"false")
       <<",\"read_only\":true,\"manual_dispatch\":false,\"manual_cycle_gate\":false,\"chamber_known\":false,\"authority_known\":false,\"atomic_snapshot\":false,\"status_counts\":[";
    for(unsigned n=0;n<reloadStatuses.size();++n){if(n)out<<',';out<<reloadStatuses[n];}out<<"],\"publication_statuses\":[";
    for(unsigned n=0;n<reloadPublicationStatuses.size();++n){if(n)out<<',';out<<reloadPublicationStatuses[n];}out<<"],\"publication_reasons\":[";
    for(unsigned n=0;n<reloadPublicationReasons.size();++n){if(n)out<<',';out<<reloadPublicationReasons[n];}out<<"],\"samples\":[";
    for(unsigned n=0;n<reloadSnapshotCount;++n){const auto& r=reloadSnapshots[n];if(n)out<<',';
        out<<"{\"sequence\":"<<r.sequence<<",\"observed_ns\":"<<r.observedNs<<",\"actor\":"<<r.owner.soldier<<",\"weapon\":"<<r.owner.weapon
           <<",\"actor_generation\":"<<r.owner.actorGeneration<<",\"equip_generation\":"<<r.owner.equipGeneration<<",\"space\":"<<r.owner.space
           <<",\"eligibility_branch\":"<<unsigned(r.eligibilityBranch)<<",\"fire_logic\":"<<r.config.fireLogicType<<",\"reload_type\":"<<r.config.reloadType<<",\"base_capacity\":"<<r.config.baseCapacity
           <<",\"branches_agree_ammo\":"<<(r.branchesAgreeOnAmmo?"true":"false")<<",\"branches\":[";
        for(unsigned b=0;b<2;++b){const auto& f=r.branches[b];if(b)out<<',';
            out<<"{\"address\":"<<f.address<<",\"current\":"<<f.currentState<<",\"previous\":"<<f.previousState<<",\"next\":"<<f.nextState<<",\"phase\":"<<unsigned(f.phase)
               <<",\"loaded\":"<<f.loaded<<",\"reserve\":"<<f.reserve<<",\"effective_capacity\":";if(f.effectiveCapacity)out<<*f.effectiveCapacity;else out<<"null";
            out<<",\"reload_eligibility\":"<<unsigned(f.reloadAmmo)<<'}';
        }out<<"]}";
    }out<<"]}";
    out<<",\"sight_flip\":{\"requested\":"<<(sightFlip?"true":"false")<<",\"requests\":"<<sightRequests.load()<<",\"commits\":"<<sightCommits.load()<<",\"cancellations\":"<<sightCancellations.load()<<",\"headset_verified\":false,\"records_total\":"<<sightRecordsTotal<<",\"events_total\":"<<sightEventsTotal<<",\"records\":[";
    const auto writeSight=[&](const SightRecord& r){
        out<<"{\"ms\":"<<r.ms<<",\"generation\":"<<r.generation<<",\"contact_generation\":"<<r.contactGeneration<<",\"coherent_generation\":"<<r.coherentGeneration<<",\"weapon\":"<<r.weapon<<",\"request\":"<<r.request<<",\"phase\":"<<r.phase<<",\"cancel_reason\":"<<r.cancel<<",\"packet_reason\":"<<r.packetReason
           <<",\"distance_m\":"<<r.distance<<",\"support_distance_m\":"<<r.supportDistance<<",\"signed_radians\":"<<r.angle<<",\"squeeze\":"<<r.squeeze<<",\"paired_squeeze\":"<<r.pairedSqueeze
           <<",\"contact\":"<<(r.contact?"true":"false")<<",\"preferred\":"<<(r.preferred?"true":"false")<<",\"tracked\":"<<(r.tracked?"true":"false")<<",\"family_valid\":"<<(r.familyValid?"true":"false")<<",\"paired\":"<<(r.paired?"true":"false")<<",\"advanced\":"<<(r.advanced?"true":"false")
           <<",\"grabbed\":"<<(r.grabbed?"true":"false")<<",\"committed\":"<<(r.committed?"true":"false")<<'}';
    };
    for(unsigned n=0;n<sightRecordCount;++n){if(n)out<<',';writeSight(sightRecords[(sightRecordNext+sightRecords.size()-sightRecordCount+n)%sightRecords.size()]);}
    out<<"],\"events\":[";
    for(unsigned n=0;n<sightEventCount;++n){if(n)out<<',';writeSight(sightEvents[(sightEventNext+sightEvents.size()-sightEventCount+n)%sightEvents.size()]);}
    out<<"],\"gestures_total\":"<<sightGesturesTotal<<",\"gestures\":[";
    const auto sightVector=[&](math::Vec3 p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};
    for(unsigned n=0;n<sightGestureCount;++n){
        if(n)out<<',';const auto& r=sightGestures[(sightGestureNext+sightGestures.size()-sightGestureCount+n)%sightGestures.size()];
        out<<"{\"id\":"<<r.id<<",\"start_ms\":"<<r.startMs<<",\"end_ms\":"<<r.endMs
           <<",\"start_generation\":"<<r.startGeneration<<",\"end_generation\":"<<r.endGeneration<<",\"weapon\":"<<r.weapon
           <<",\"start_mode\":"<<r.startMode<<",\"request\":"<<r.request<<",\"cancel_reason\":"<<r.reason
           <<",\"fresh_samples\":"<<r.freshSamples<<",\"min_signed_radians\":"<<r.minAngle<<",\"max_signed_radians\":"<<r.maxAngle
           <<",\"peak_progress_radians\":"<<r.peakProgress<<",\"longest_detent_ns\":"<<r.longestDetentNs
           <<",\"max_contact_distance_m\":"<<r.maxContactDistance<<",\"committed\":"<<(r.committed?"true":"false")
           <<",\"finished\":"<<(r.finished?"true":"false")<<",\"configured_pivot_m\":";sightVector(r.pivot);
        out<<",\"configured_axis\":";sightVector(r.axis);out<<",\"native_pivot_at_grab_m\":";sightVector(r.nativePivot);
        out<<",\"start_hand_local_m\":";sightVector(r.startHand);out<<",\"end_hand_local_m\":";sightVector(r.endHand);out<<'}';
    }
    out<<"]}";
    const auto writeSupportRecord=[&](const SupportRecord& r){
        const auto q=[&](math::Quaternion p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<','<<p.w<<']';};
        out<<"{\"ms\":"<<r.ms<<",\"generation\":"<<r.generation<<",\"weapon_kind\":"<<r.weaponKind<<",\"release_reason\":"<<r.reason<<",\"action_bits\":"<<r.actions<<",\"grasp_token\":"<<r.token<<",\"contact\":"<<(r.contact?"true":"false")<<",\"distance_m\":"<<r.distance<<",\"squeeze\":"<<r.squeeze<<",\"holding\":"<<(r.holding?"true":"false")<<",\"engaged\":"<<(r.engaged?"true":"false")<<",\"released\":"<<(r.released?"true":"false")<<",\"correction_radians\":"<<r.angle<<",\"raw_aim\":";q(r.rawAim);out<<",\"aim\":";q(r.aim);out<<'}';
    };
    out<<",\"two_hand_support\":{\"requested\":"<<(twoHandGrip?"true":"false")<<",\"held_samples\":"<<supportSamples.load()<<",\"grabs\":"<<supportGrabs.load()<<",\"releases\":"<<supportReleases.load()<<",\"preservation_failures\":"<<supportPreservationFailures.load()<<",\"fire_samples\":"<<supportFireSamples.load()<<",\"aim_samples\":"<<supportAimSamples.load()<<",\"max_aim_residual_radians\":"<<supportAimResidual<<",\"records\":[";
    for(unsigned n=0;n<supportRecordCount;++n){if(n)out<<',';writeSupportRecord(supportRecords[(supportRecordNext+supportRecords.size()-supportRecordCount+n)%supportRecords.size()]);}
    out<<"],\"event_schema\":1,\"event_capacity\":"<<supportEvents.size()<<",\"events_total\":"<<supportEventTotal<<",\"events_dropped\":"<<supportEventDropped<<",\"forced_resets\":"<<supportForcedResets<<",\"policy_release_counts\":[";
    for(unsigned n=0;n<supportReleaseCounts.size();++n){if(n)out<<',';out<<supportReleaseCounts[n];}
    out<<"],\"events\":[";
    for(unsigned n=0;n<supportEventCount;++n){
        if(n)out<<',';const auto& e=supportEvents[n];
        const auto point=[&](math::Vec3 v){out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';};
        out<<"{\"event_ms\":"<<e.eventMs<<",\"previous_grasp_token\":"<<e.previousToken<<",\"actor\":"<<e.actor<<",\"weak\":"<<e.weak<<",\"weapon\":"<<e.weapon<<",\"rig_epoch\":"<<e.rigEpoch<<",\"space\":"<<e.space<<",\"cancel_flags\":"<<e.cancelFlags;
        out<<",\"focused\":"<<(e.focused?"true":"false")<<",\"head_tracked\":"<<(e.headTracked?"true":"false")<<",\"left_grip_tracked\":"<<(e.leftGripTracked?"true":"false")<<",\"right_grip_tracked\":"<<(e.rightGripTracked?"true":"false")<<",\"right_aim_tracked\":"<<(e.rightAimTracked?"true":"false")<<",\"reset_reason\":";
        if(e.resetReason)out<<'"'<<e.resetReason<<'"';else out<<"null";
        out<<",\"left_grip\":";point(e.leftGrip);out<<",\"right_grip\":";point(e.rightGrip);out<<",\"sample\":";writeSupportRecord(e.sample);out<<'}';
    }
    out<<"]},\"pose_observer_calls\":"<<poseCalls.load()<<",\"pose_observer_rejected\":"<<poseRejected.load()<<",\"pose_writes_enabled\":false,\"pose_evidence\":[";
    for(unsigned i=0;i<poseRecordCount;++i){const auto& r=poseRecords[i];if(i)out<<',';
        out<<"{\"kind\":"<<r.kind<<",\"thread\":"<<r.thread<<",\"caller\":"<<r.caller<<",\"owner\":"<<r.owner<<",\"object\":"<<r.object<<",\"ms\":"<<r.ms<<",\"matrix\":[";
        for(unsigned n=0;n<16;++n){if(n)out<<',';out<<(n%4==3?0:r.matrix[n]);}out<<"]}";
    }out<<"],\"body_follow_requested\":"<<(bodyFollow?"true":"false")<<",\"body_follow_samples\":"<<followSamples.load()<<",\"body_read_failures\":"<<bodyReadFailures.load()<<",\"body_follow_evidence\":[";
    for(unsigned i=0;i<followRecordCount;++i){const auto& r=followRecords[i];if(i)out<<',';out<<"{\"ms\":"<<r.ms<<",\"head\":["<<r.head.x<<','<<r.head.y<<','<<r.head.z<<"],\"body\":["<<r.body.x<<','<<r.body.y<<','<<r.body.z<<"],\"consumed\":["<<r.consumed.x<<','<<r.consumed.y<<','<<r.consumed.z<<"],\"forward\":"<<r.forward<<",\"strafe\":"<<r.strafe<<'}';}
    out<<"],\"rig_attempts\":"<<rigAttempts<<",\"rig_writes_enabled\":false,\"rig\":";
    if(!observedRig)out<<"null";else{const auto& r=*observedRig;
        out<<"{\"animation\":"<<r.identity.animation<<",\"skeleton\":"<<r.identity.skeleton<<",\"pose\":"<<r.identity.pose<<",\"world_matrices\":"<<r.identity.worldMatrices<<",\"weapon_bone\":"<<r.weaponBone<<",\"evaluated_matrices\":"<<r.identity.evaluatedMatrices<<",\"native_ik\":"<<(r.identity.nativeIk?"true":"false")<<",\"skin_consistency_error\":"<<r.skinConsistencyError<<",\"bones\":[";
        for(unsigned n=0;n<r.names.size();++n){if(n)out<<',';out<<"{\"name\":\"";
            for(char c:r.names[n]){if(c=='"'||c=='\\')out<<'\\';out<<c;}out<<"\",\"parent\":"<<r.parents[n]<<",\"world\":[";
            for(unsigned j=0;j<16;++j){if(j)out<<',';out<<r.world[n].values[j/4][j%4];}out<<"]}";
        }out<<"]}";
    }out<<",\"ik_preview\":{\"solved\":"<<ikPreviewSolved<<",\"failed\":"<<ikPreviewFailed<<",\"candidate_bone_edits\":"<<ikPreviewEdits<<",\"reach_clamps\":"<<ikPreviewClamps<<",\"max_segment_length_error\":"<<ikPreviewLengthError<<",\"max_target_error\":"<<ikPreviewTargetError<<",\"writes_applied\":false},\"animation_calls\":"<<animationCalls.load()<<",\"animation_overlaps\":"<<animationOverlap.load()<<",\"phase_failures\":"<<phaseFailures.load()<<",\"animation_phases\":[";
    for(unsigned n=0;n<phaseRecordCount;++n){const auto& r=phaseRecords[n];if(n)out<<',';
        out<<"{\"sequence\":"<<r.sequence<<",\"phase\":"<<r.phase<<",\"thread\":"<<r.thread<<",\"animation\":"<<r.animation<<",\"world\":"<<r.world<<",\"skin\":"<<r.skin<<",\"override\":"<<r.overrideSkin<<",\"override_active\":"<<(r.overrideActive?"true":"false")<<",\"world_hash\":"<<r.worldHash<<",\"skin_hash\":"<<r.skinHash<<",\"override_hash\":"<<r.overrideHash<<'}';
    }out<<"],\"fire_origin_observation\":{\"requested\":"<<((equipProbe||muzzleFire)?"true":"false")<<",\"calls\":"<<fireOriginCalls.load()<<",\"matching_candidates\":"<<fireOriginMatches.load()<<",\"rejected_outputs\":"<<fireOriginRejected.load()<<",\"client_calls\":"<<clientShotCalls.load()<<",\"client_copies\":"<<clientShotCopies.load()<<",\"client_rejected\":"<<clientShotRejected.load()<<",\"fire_writes_enabled\":"<<(muzzleFire?"true":"false")<<",\"client_origin_writes\":"<<clientOriginWrites.load()<<",\"server_origin_writes\":"<<serverOriginWrites.load()<<",\"origin_write_failures\":"<<originWriteFailures.load()<<",\"shot_source_changes\":"<<shotSourceChanges.load()<<",\"origin_fallbacks\":"<<originFallbacks.load()<<",\"origin_writes_after_log_full\":"<<originWritesAfterLogFull.load()<<",\"records\":[";
    for(unsigned i=0;i<fireRecordCount;++i){const auto& r=fireRecords[i];const auto item=FindWeaponProfileByTelemetryKind(r.weaponKind);if(i)out<<',';
        out<<"{\"shot_token\":"<<r.shotToken<<",\"token_valid\":"<<(r.tokenValid?"true":"false")<<",\"weapon_name\":\""<<(item?item->assetName:std::string_view{"unclassified"})<<"\",\"phase\":"<<r.phase<<",\"origin_written\":"<<(r.originWritten?"true":"false")<<",\"client_path\":"<<(r.clientPath?"true":"false")<<",\"caller\":"<<r.caller<<",\"effects\":"<<r.effects<<",\"server_player\":"<<r.player<<",\"server_soldier\":"<<r.serverSoldier<<",\"client_soldier\":"<<r.clientSoldier<<",\"weapon\":"<<r.weapon<<",\"firing_data\":"<<r.data<<",\"thread\":"<<r.thread<<",\"ms\":"<<r.ms<<",\"native_shot_matrix\":[";
        for(unsigned j=0;j<16;++j){if(j)out<<',';out<<(j%4==3?0:r.matrix[j]);}out<<"],\"client_effects_matrix\":[";
        for(unsigned j=0;j<16;++j){if(j)out<<',';out<<(j%4==3?0:r.clientEffects[j]);}out<<"],\"player_index\":"<<r.playerId<<",\"tracked_shot_candidate\":";
        if(!r.shot)out<<"null";else {out<<"{\"generation\":"<<r.shot->generation<<",\"flash_bone\":"<<r.shot->flashBone;
            const auto matrix=[&](const char* name,const math::Matrix4& m){out<<",\""<<name<<"\":[";for(unsigned n=0;n<16;++n){if(n)out<<',';out<<m.values[n/4][n%4];}out<<']';};
            matrix("native_weapon",r.shot->nativeWeapon);matrix("tracked_weapon",r.shot->trackedWeapon);matrix("native_flash",r.shot->nativeFlash);matrix("tracked_flash",r.shot->trackedFlash);
            if(r.eventPose){matrix("event_muzzle",r.eventPose->muzzle);out<<",\"event_generation\":"<<r.eventPose->generation;}
            if(r.mappedShot)matrix("mapped_shot",*r.mappedShot);out<<'}';
        }out<<",\"output_matrix\":[";for(unsigned j=0;j<16;++j){if(j)out<<',';out<<(j%4==3?0:r.after[j]);}out<<"]}";
    }out<<"]},\"weapon_effects\":";WeaponEffectEvidence().Write(out,effectAnchorConfig,effectAnchorInstalled,WeaponEffectTelemetry::Capacity);
    out<<",\"rig_publication\":";rigPublication::Report(out);out<<'}';}
}
