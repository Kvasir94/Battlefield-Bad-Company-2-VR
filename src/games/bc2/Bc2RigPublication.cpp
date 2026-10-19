#include <Windows.h>
#include <intrin.h>
#include <MinHook.h>
#include "Bc2RigPublication.h"
#include "Bc2PumpPart.h"
#include "Bc2Xm8BoltRuntime.h"
#include "Bc2GunEventLog.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2MagazineFallbackObservation.h"
#include "Bc2AuthoredSupport.h"
#ifdef FVR_BC2_AUTHORED_SUPPORT_HEADER
#include FVR_BC2_AUTHORED_SUPPORT_HEADER
#endif
#include "Bc2AuthoredGrip.h"
#include "Bc2GripAim.h"
#include "Bc2NativeActionPose.h"
#include "Bc2NativeRackPose.h"
#ifdef FVR_BC2_NATIVE_ACTION_POSE_HEADER
#include FVR_BC2_NATIVE_ACTION_POSE_HEADER
#endif
#include "Bc2Gameplay.h"
#include "Bc2AuthoredContacts.h"
#include "Bc2FloatingHands.h"
#include "Bc2NativeHandDrawCapture.h"
#include "Bc2NativeHandMesh.h"
#include "Bc2NativeHandWorkerSample.h"
#include "Bc2BodyAmmo.h"
#include "Bc2PhysicalWeaponRuntime.h"
#include "Bc2BodyAmmoHost.h"
#include "Bc2ReloadGuidance.h"
#include "Bc2ReticleDrawObservation.h"
#ifdef FVR_BC2_AUTHORED_GRIP_HEADER
#include FVR_BC2_AUTHORED_GRIP_HEADER
#endif
#include "Bc2Rig.h"
#include "Bc2HandPose.h"
#include "fvr/interaction/GripAttachment.h"
#include "fvr/interaction/HandTouch.h"
#include "Bc2WeaponCapture.h"
#include "Bc2WeaponEffects.h"
#include <sstream>
#include <bit>
#include "fvr/interaction/ArmIk.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ComfortCamera.h"
#include <mutex>
#include <memory>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <cmath>
#include <algorithm>
namespace fvr::bc2::rigPublication {
namespace {
using Getter=unsigned(__thiscall*)(void*);
using Pack=std::uintptr_t(__cdecl*)(const void*,void*,unsigned);
Getter getterA=nullptr,getterB=nullptr;Pack packOriginal=nullptr;
RigConsumerCandidates profile{};std::uintptr_t base=0;ResolveOwner resolveOwner=nullptr;
std::array<void*,3> hooks{};std::atomic<bool> enabled=false;std::atomic<unsigned> inFlight=0;
bool pulse=false,hands=false;ULONGLONG started=0;
std::mutex frameMutex;Tracking tracking;interaction::TrackedRig calibratedRig;WeaponCapture weaponCapture;ULONGLONG captureTime=0;
struct PublishedPose {std::optional<NativeHandPoseLayout> handLayout;std::array<bool,2> handTracked{};std::int64_t handObservedNs=0;std::optional<AuthoredGripBinding> authoredGrip;std::optional<OrdinaryEquipmentRequest> ordinaryRecovery;WeaponEquipmentIdentity equipment{};std::shared_ptr<const BeltAmmoPalette> belt;ReloadPackedSource drawSource{};RigIdentity identity;unsigned weapon=0;std::uint64_t generation=0,space=0,ownerGeneration=0,equipmentGeneration=0;std::int64_t deadline=0,reloadDeadlineNs=0,recoilDeadline=0;std::vector<std::array<std::byte,64>> before,posed,previewBase,reloadBase;std::uint64_t previewToken=0;std::shared_ptr<const MagazineTracking> magazine;std::vector<std::array<std::byte,64>> magazineBase;std::shared_ptr<const ReloadPreview> reloadPreview;std::shared_ptr<const NativeRackPresentation> nativeRack;std::vector<std::array<std::byte,64>> nativeRackBase;std::shared_ptr<const ReloadShellHidePlan> shellHide;std::shared_ptr<const WeaponVisibilityPlan> visibility;std::shared_ptr<const BodyFreeRightEvidence> freeRight;std::optional<WeaponShotFrame> shot;};
std::shared_ptr<const PublishedPose> published;
std::atomic<bool> nativeHandPoseConsumer=false;
std::atomic<unsigned> nativeRecoilPoses=0,nativeRecoilNonidentityPoses=0,nativeRecoilRejected=0,nativeRecoilRectified=0,nativeRecoilMuzzleBore=0,nativeRecoilBoreMismatch=0;
std::atomic<float> nativeRecoilMaximumRadians=0;
std::atomic<unsigned> nativeRecoilKickPoses=0,nativeRecoilKickRejected=0;
std::atomic<float> nativeRecoilMaximumKickMeters=0;
struct NativeHandPosePublication {NativeHandPose pose;WeaponEquipmentIdentity equipment;};
std::atomic<std::shared_ptr<const NativeHandPosePublication>> nativeHandPose;
std::mutex nativeHandPoseMutex;std::uint64_t nativeHandPoseSerial=0;
void StoreNativeHandPose(std::uint64_t serial,std::shared_ptr<const NativeHandPosePublication> next){
    std::lock_guard lock(nativeHandPoseMutex);
    if(nativeHandPoseConsumer.load(std::memory_order_acquire)&&serial>nativeHandPoseSerial){
        nativeHandPoseSerial=serial;nativeHandPose.store(std::move(next),std::memory_order_release);
    }
}
struct ShotGuard {unsigned soldier=0,weak=0,weapon=0;std::uint64_t owner=0,space=0,generation=0;std::int64_t deadline=0;bool valid=false,leftTracked=false;std::uint64_t previewToken=0;bool weaponActionsBlocked=false;std::uint64_t equipmentGeneration=0;WeaponEquipmentIdentity equipment{};};
struct ShotPublication {WeaponShotFrame frame;ShotGuard identity;std::optional<AuthoredGripBinding> authoredGrip;};
std::atomic<std::shared_ptr<const ShotGuard>> shotGuard;
std::atomic<std::shared_ptr<const ShotPublication>> shotPublication;
struct GripBorePublication {math::Matrix4 relation;ShotGuard identity;std::optional<AuthoredGripBinding> authoredGrip;};
std::atomic<std::shared_ptr<const GripBorePublication>> gripBorePublication;
std::atomic<std::shared_ptr<const ReloadTracking>> reloadGuard;
std::atomic<std::shared_ptr<const MagazineTracking>> magazineGuard;
std::atomic<std::shared_ptr<const NativeRackPresentation>> nativeRackGuard;
std::atomic<unsigned> nativeRackPoses=0,nativeRackRejects=0,nativeRackCopies=0,nativeRackFallbacks=0;
std::atomic<std::shared_ptr<const MagazineDetachPairReceipt>> magazineDetachPair;
std::atomic<unsigned> magazinePoses=0,magazineFallbacks=0,magazineCopies=0,magazinePairs=0,magazineContacts=0;
MagazineFallbackJournal<> magazineFallbackJournal;
std::array<std::atomic<unsigned>,4> magazineRoleCopies{},magazineRolePairs{};
// Render-thread only (PackHook): the last pose that went in, for the hold.
std::shared_ptr<const PublishedPose> lastGoodPose;std::int64_t lastGoodPoseNs=0;constexpr std::int64_t PoseHoldNs=25000000;
std::array<float,3> pouchWorld{},easeFrom{};std::int64_t pouchWorldNs=0,easeStartNs=0,easeLengthNs=0;interaction::HandInteractionKey easeItem{};std::atomic<unsigned> pickupEases=0;
std::optional<interaction::AmmoSupplyVisualSample> lastPouch;std::int64_t lastPouchNs=0;std::array<std::atomic<unsigned>,4> pickupEaseMisses{};
std::atomic<unsigned> poseRebases=0,poseHolds=0,handlePoses=0,handleRejects=0;float handleShown=0;std::int64_t handleShownNs=0;
std::atomic<unsigned> pumpPartPoses=0,pumpPartRejects=0,pumpPartCaptures=0,pumpOpenLearned=0;
// S3 pump mechanism: the fore-end (jntWpn_4) and the parts the native pump
// moves with it (jntWpn_3 ~69 mm, jntWpn_5 ~83 mm with ~6 deg; RELOAD-
// GEOMETRY-EVIDENCE-20261001). Moving only the fore-end left the parts that
// travel with it closed and pushed its geometry through the receiver (side
// eye images, run on d4e82d5). Closed: each part relative to the native
// weapon on the first frame of the owed pump. Open: the same parts on the
// frame where the native pump animation (which plays on its own ~0.35 s after
// every shot) has the fore-end furthest from closed. The drawn pose is the
// closed->open blend at the stroke fraction, so full rear is the game's own
// full-rear pose.
struct PumpMechanismPart {unsigned bone=0;math::Matrix4 closed{},open{},learnedOpen{};};
std::array<PumpMechanismPart,3> pumpMechanism{};unsigned pumpMechanismCount=0;
std::uint64_t pumpClosedShot=0;float pumpOpenTravel=0,pumpLearnedTravel=0;
math::Matrix4 BlendRigid(const math::Matrix4& a,const math::Matrix4& b,float t)noexcept{
    math::Matrix4 m{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m.values[r][c]=a.values[r][c]+(b.values[r][c]-a.values[r][c])*t;
    // Re-orthonormalize the basis (rows 0-2) after the linear blend, keeping row lengths.
    const auto len=[](const std::array<float,4>& v){return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);};
    const float s0=len(a.values[0]),s1=len(a.values[1]),s2=len(a.values[2]);
    auto& x=m.values[0];auto& y=m.values[1];auto& z=m.values[2];
    auto norm=[&](std::array<float,4>& v,float s){const float l=len(v);if(l>0)for(unsigned k=0;k<3;++k)v[k]*=s/l;};
    norm(x,s0);const float d=(y[0]*x[0]+y[1]*x[1]+y[2]*x[2])/(s0*s0);for(unsigned k=0;k<3;++k)y[k]-=d*x[k];norm(y,s1);
    z={x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0],z[3]};norm(z,s2);
    return m;
}
std::atomic<unsigned> magazinePlanAttempts=0,magazineBindingRejects=0,magazinePlanRejects=0,magazineReachRejects=0,magazinePoseRejects=0,magazineHiddenRejects=0,magazineFreshRejects=0;
struct MagazineReachRecord {std::uint64_t sequence=0;unsigned role=0;float error=0;bool solved=false,clamped=false;};
std::array<MagazineReachRecord,24> magazineReachRecords{};unsigned magazineReachCount=0;
std::atomic<std::shared_ptr<const WeaponVisibilityRequest>> visibilityGuard;
std::atomic<std::shared_ptr<const WeaponVisibilityReceipt>> visibilityReceipt;
std::atomic<std::shared_ptr<const OrdinaryEquipmentRequest>> ordinaryEquipmentGuard;
std::atomic<std::shared_ptr<const OrdinaryEquipmentPair>> ordinaryEquipmentPair;
std::atomic<unsigned> ordinaryEquipmentCopies=0,ordinaryEquipmentPairs=0;
std::atomic<std::shared_ptr<const BodyFreeRightEvidence>> freeRightGuard;
std::atomic<unsigned> freeRightPoses=0,freeRightFallbacks=0;
std::atomic<unsigned> freeRightPackedCopies=0,freeRightPairedCopies=0;
std::atomic<unsigned> visibilityPlans=0,visibilityRejected=0,visibilityHiddenCopies=0,visibilityShownCopies=0,visibilityFallbacks=0,visibilityPairedReceipts=0;
std::atomic<unsigned> beltPlans=0,beltCopies=0,beltPairs=0,beltFallbacks=0,beltSourceRejected=0;
std::atomic<unsigned> reloadPreviewPoses=0,reloadPreviewRejected=0,reloadPreviewFallbacks=0,reloadRawContacts=0;
std::atomic<unsigned> reloadNativeVisibleContacts=0,reloadNativeHiddenContacts=0;
std::atomic<unsigned> reloadOwnedShellVisibilityPoses=0;
std::atomic<unsigned> reloadShellHidePlans=0,reloadShellHideCopies=0,reloadShellHidePairs=0,reloadShellHideFallbacks=0,reloadShellHideSourceRejects=0;
constexpr unsigned ReloadPresentationPhaseCount=5;
constexpr int ReloadPackFallback=int(ReloadPresentationPhaseCount);
std::array<std::atomic<unsigned>,ReloadPresentationPhaseCount> reloadPhasePoses{};
std::array<std::atomic<unsigned>,ReloadPresentationPhaseCount> reloadVerifiedPacks{},reloadVerifiedPairedPacks{};
std::atomic<unsigned> reloadVerifiedFallbackPacks=0,reloadVerifiedPairedFallbackPacks=0;
std::array<std::atomic<unsigned>,8> reloadPresentationRejections{};
struct NativeEye {unsigned soldier=0,weak=0;math::Matrix4 camera{};ULONGLONG ms=0;};
std::atomic<std::shared_ptr<const NativeEye>> nativeEye;
std::atomic<std::shared_ptr<const Tracking>> eyeTracking;
std::atomic<unsigned> shotFrameRaces=0,shotFrameExpired=0,shotFrameUnavailable=0;

bool NativeActionCaptureRequested()noexcept {
    static const bool enabled=[]{char value[16]{};GetEnvironmentVariableA("FVR_BC2_NATIVE_ACTION_CAPTURE",value,sizeof(value));return std::strcmp(value,"1")==0;}();
    return enabled;
}
struct NativeActionCapturePart {std::array<char,64> bone{};math::Matrix4 partInWeapon{};};
struct NativeActionCaptureRecord {
    std::array<char,64> asset{};std::array<char,256> configuredMesh{};
    std::uint64_t rigFingerprint=0,equipmentGeneration=0,capturedMs=0;unsigned animation=0,pose=0,partCount=0;
    std::shared_ptr<const ReloadStateSnapshot> firing;
    math::Matrix4 nativeWeapon{},nativeLeftWrist{},wristInWeapon{};std::array<math::Matrix4,15> fingersInWrist{};
    std::array<NativeActionCapturePart,32> parts{};
};
struct HandRecord {std::optional<NativeActionCaptureRecord> nativeAction;math::Matrix4 rawLeftTarget{},interactionWeapon{},leftGripWorld{};math::Pose leftController{},referenceHead{};float unitsPerMeter=1;bool rawAnchorValid=false;std::uint64_t generation=0;math::Matrix4 nativeWeapon{},placedWeapon{},eyeBase{},resolvedRight{},nativeRight{},nativeLeft{};bool attachmentPending=false,authoredGrip=false,supportAttached=false,sightAttached=false;std::array<math::Vec3,2> localGrips{};math::Vec3 grip{},actorPosition{},nativeRoot{},bodyRoot{},anatomyRoot{};std::array<math::Vec3,2> grips{},shoulders{},anchors{},targets{},resolved{};std::array<float,2> errors{};std::array<bool,2> tracked{};unsigned weapon=0;std::uint64_t ownerGeneration=0,space=0;};
std::array<HandRecord,512> handRecords{};unsigned handRecordCount=0,handRecordNext=0;ULONGLONG handRecordTime=0;
// At most 512 opt-in frames, 32 parts and 15 fingers each. Fixed storage is
// under 4 MiB; bounded names/finite matrices serialize below 16 MiB.
static_assert(sizeof(handRecords)+512*sizeof(ReloadStateSnapshot)<4*1024*1024);
std::atomic<unsigned> torsoPoses=0,hiddenLeafPoses=0,supportAttachedPoses=0,supportMagnetBlendPoses=0,supportMagnetReturnPoses=0;
// Soft support magnetism (InteractionZones.h): the visible left hand eases
// onto the authored support contact and back. Contact/steering still use the
// raw tracked wrist, so the blend can never sustain a grab by itself.
interaction::ContactMagnet supportMagnet;
NativeActionHandTransition nativeActionHandTransition;
std::atomic<unsigned> authoredSupportWrists=0,authoredSupportFingers=0,authoredFingerFailures=0,floatingPoses=0,floatingRejects=0;
struct SightPreviewRecord {
    SightPreview preview{};std::uint64_t inputGeneration=0,owner=0,space=0;
    math::Matrix4 nativeRear{},nativeFront{},rawHand{},resolvedHand{};
    float palmError=0;bool clamped=false;
};
std::array<SightPreviewRecord,128> sightPreviewRecords{};unsigned sightPreviewCount=0,sightPreviewNext=0;ULONGLONG sightPreviewRecordTime=0;
std::atomic<unsigned> sightPreviewPoses=0,sightPreviewBaseFallbacks=0,sightPreviewRejected=0;
interaction::GripAttachment supportAttachment;
std::optional<Bc2HandBinding> handBinding,rightHandBinding;
std::uint64_t handBindingFingerprint=0;float handBindingUnits=0;
std::array<std::atomic<unsigned>,3> handRolePoses{};std::atomic<unsigned> handPoseRejected=0;
struct FingerEvidence {unsigned index=0,parent=0;math::Matrix4 reference{},native{},posed{};};
struct HandPoseEvidence {
    std::uint64_t generation=0,space=0,owner=0;unsigned weapon=0;interaction::HandPoseRole role{};
    float squeeze=0,trigger=0;unsigned touchActive=0,touched=0;math::Matrix4 wrist{},grip{};math::Vec3 mechanismPoint{};
    std::array<FingerEvidence,15> fingers{};
};
std::array<HandPoseEvidence,256> handPoseRecords{};unsigned handPoseCount=0,handPoseNext=0;
ULONGLONG handPoseRecordTime=0;
struct TriggerEvidence {std::uint64_t generation=0;unsigned weapon=0;float trigger=0;bool wristPreserved=false,otherBranchesPreserved=false;std::array<unsigned,3> indices{};std::array<math::Matrix4,3> before{},after{};};
std::array<TriggerEvidence,128> triggerRecords{};unsigned triggerCount=0,triggerNext=0;ULONGLONG triggerRecordTime=0;
std::atomic<unsigned> triggerPoses=0,triggerFailures=0;


std::array<std::atomic<unsigned>,unsigned(AuthoredGripStatus::Count)> authoredGripStatus{};
std::atomic<unsigned> authoredGripExpired=0,authoredGripCopies=0;
std::span<const NativeActionPoseProfile> NativeActionPoseProfiles()noexcept {
    static const bool requested=[]{char value[16]{};GetEnvironmentVariableA("FVR_BC2_NATIVE_ACTION_POSES",value,sizeof(value));return std::strcmp(value,"0")!=0;}();
    if(!requested)return {};
#ifdef FVR_BC2_NATIVE_ACTION_POSE_HEADER
    return generated::NativeActionPoses;
#else
    return {};
#endif
}
// Default-on presentation switches (docs/SPAS-PUMP-BOLT-GRASPS-20261011.md).
bool EnvNotZero(const char* name)noexcept {char value[8]{};return !(GetEnvironmentVariableA(name,value,sizeof(value))==1&&value[0]=='0');}
bool RackFamilyGraspEnabled()noexcept {static const bool on=EnvNotZero("FVR_BC2_RACK_FAMILY_GRASP");return on;}
bool PumpNativeHandEnabled()noexcept {static const bool on=EnvNotZero("FVR_BC2_PUMP_NATIVE_HAND");return on;}
std::atomic<unsigned> pumpHandPoses=0,pumpHandRejects=0,rackFamilyGraspPoses=0,rackFamilyGraspRejects=0,rackFamilyGraspBindings=0;
std::span<const AuthoredGripProfile> AuthoredGripProfiles()noexcept {
#ifdef FVR_BC2_AUTHORED_GRIP_HEADER
    return generated::AuthoredGrips;
#else
    return {};
#endif
}
std::atomic<unsigned> trackedPoses=0,trackingRejected=0,trackingUnavailable=0,weaponOutputs=0,calibrations=0,poseMisses=0;
constexpr const char* rejectionNames[]={"owner","rig_snapshot","weapon_output","eye_base","physical_torso","head_bone","bind_landmarks","bind_frame","inverse_bind_frame","bind_arms","bind_torso","tracked_targets","torso_subtree","arm_solve","weapon_subtree","palette_plan","output_deadline"};
std::array<std::atomic<unsigned>,17> rejectionCounts{};
void RejectTracking(unsigned reason){++trackingRejected;++rejectionCounts[reason];}
std::array<std::atomic<unsigned>,2> handPoses{},handCalibrations{};std::atomic<unsigned> partialPoses=0,fallbackFailures=0;
bool Fresh(std::int64_t deadline)noexcept {LARGE_INTEGER q{};return QueryPerformanceCounter(&q)&&q.QuadPart>0&&q.QuadPart<deadline;}
std::int64_t ReloadNanos(std::int64_t ticks=0)noexcept {
    LARGE_INTEGER frequency{},now{};
    if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return 0;
    if(!ticks){if(!QueryPerformanceCounter(&now))return 0;ticks=now.QuadPart;}
    if(ticks<=0||ticks/frequency.QuadPart>INT64_MAX/1000000000)return 0;
    return (ticks/frequency.QuadPart)*1000000000+std::int64_t(static_cast<long double>(ticks%frequency.QuadPart)*1000000000/frequency.QuadPart);
}

bool AuthoredCurrent(const std::optional<AuthoredGripBinding>& binding)noexcept {
    if(!binding)return true;
    const auto current=shotGuard.load(std::memory_order_acquire);const auto ns=ReloadNanos();
    if(!current||!current->valid||!Fresh(current->deadline))return false;
    const auto selected=gameplay::ReadSelectedMeshes(binding->selected->owner,ns);
    return AuthoredGripCurrent(*binding,selected.get(),current->equipment,
        {current->soldier,current->weak,current->owner,current->equipmentGeneration,current->space,
         current->generation,ReloadNanos(current->deadline)},ns);
}
bool FreeRightCurrent(const BodyFreeRightEvidence& source)noexcept {
    const auto current=freeRightGuard.load(std::memory_order_acquire);const auto ns=ReloadNanos();
    const auto visibility=visibilityGuard.load(std::memory_order_acquire);
    return BodyFreeRightRenderCurrent(source,current.get(),visibility.get(),ns);
}
std::atomic<unsigned> owners=0,copies=0,changedCopies=0,rejected=0,sourceChanges=0,packingFailures=0,pairedCopies=0;
std::atomic<std::uint64_t> drawPackSerial=0;
struct Scope {std::shared_ptr<const PublishedPose> handPose;std::optional<AuthoredGripBinding> authoredGrip;std::optional<OrdinaryEquipmentRequest> ordinaryRecovery;RigIdentity ordinaryRig{};unsigned ordinaryMask=0;std::int64_t ordinaryDeadlineNs=0,recoilDeadline=0;std::shared_ptr<const BeltAmmoPalette> belt;unsigned beltMask=0;bool beltVisible=false;ReloadPackedSource drawSource{};std::uint64_t drawSerial=0;unsigned soldier=0,weak=0,sourceA=0,sourceB=0,count=0;int reloadPack=-1;std::shared_ptr<const ReloadPreview> reloadPreview;std::vector<std::array<std::byte,64>> reloadBase;std::int64_t reloadDeadlineNs=0;std::uint64_t reloadPoseGeneration=0;std::shared_ptr<const NativeRackPresentation> nativeRack;std::vector<std::array<std::byte,64>> nativeRackBase;bool nativeRackFallback=false;unsigned visibilityMask=0,freeRightMask=0,shellHideMask=0,magazineMask=0;std::shared_ptr<const MagazineTracking> magazine;std::vector<std::array<std::byte,64>> magazineBase;bool shellHidden=false;std::shared_ptr<const ReloadShellHidePlan> shellHide;std::shared_ptr<const WeaponVisibilityPlan> visibility;std::shared_ptr<const BodyFreeRightEvidence> freeRight;bool changed=false;std::uint64_t equipmentGeneration=0;WeaponEquipmentIdentity equipment{};std::vector<std::array<std::byte,64>> before,posed;};
thread_local Scope scope;
struct Callback {Callback(){++inFlight;}~Callback(){--inFlight;}};
bool Read(unsigned at,void* out,std::size_t n)noexcept {SIZE_T got=0;return at>=0x10000&&n&&n<=65536&&std::uint64_t(at)+n<=UINT32_MAX&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&got)&&got==n;}
unsigned U32(unsigned at)noexcept {unsigned v=0;Read(at,&v,4);return v;}
bool OrdinaryGuardCurrent(const OrdinaryEquipmentRequest& source)noexcept {
    const auto current=ordinaryEquipmentGuard.load(std::memory_order_acquire);
    const auto shot=shotGuard.load(std::memory_order_acquire);
    const auto reload=reloadGuard.load(std::memory_order_acquire);
    const auto magazine=magazineGuard.load(std::memory_order_acquire);
    return current&&OrdinaryEquipmentCurrent(source,*current,ReloadNanos())&&shot&&shot->valid&&shot->leftTracked&&
        shot->soldier==source.owner.soldier&&shot->weak==source.owner.weak&&shot->weapon==source.owner.weapon&&
        shot->owner==source.owner.actorGeneration&&shot->space==source.owner.space&&
        shot->equipmentGeneration==source.input.owner.equipGeneration&&shot->equipment==source.equipment&&
        shot->generation>=source.input.sequence&&!shot->previewToken&&Fresh(shot->deadline)&&
        !visibilityGuard.load(std::memory_order_acquire)&&!freeRightGuard.load(std::memory_order_acquire)&&
        (!reload||(!reload->preview&&!reload->shellControl&&!reload->belt))&&(!magazine||!magazine->target);
}
bool EquipmentStillCurrent(const WeaponEquipmentIdentity& expected)noexcept {
    // The exact data pointer was reflected by Gameplay. Native addresses and
    // contents are re-read coherently at consumption; no native call occurs.
    const WeaponModeMemory memory{const_cast<WeaponEquipmentIdentity*>(&expected),
        [](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);},
        [](void* p,unsigned at,const char*){return static_cast<const WeaponEquipmentIdentity*>(p)->data==at;}};
    return WeaponEquipmentStillCurrent(memory,expected);
}
std::optional<std::size_t> Offset(const engine::PeImage& pe,unsigned rva,unsigned n){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva))return std::size_t(s.rawOffset)+rva-s.rva;return {};}
unsigned __fastcall GetA(void* self,void*){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=getterA(self);
    if(!enabled.load(std::memory_order_acquire)||caller!=base+profile.getCallerA)return result;
    scope={};unsigned weak=0;if(resolveOwner&&resolveOwner(reinterpret_cast<unsigned>(self),weak)){
        scope.soldier=reinterpret_cast<unsigned>(self);scope.weak=weak;scope.sourceA=result;scope.drawSerial=++drawPackSerial;++owners;
    }return result;
}
unsigned __fastcall GetB(void* self,void*){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=getterB(self);
    if(enabled.load(std::memory_order_acquire)&&caller==base+profile.getCallerB&&scope.soldier==reinterpret_cast<unsigned>(self))scope.sourceB=result;
    return result;
}
bool MakePulse(unsigned count){
    const RigMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto rig=ReadFirstPersonRig(memory,scope.soldier,scope.weak);
    if(!rig||rig->identity.count!=count||rig->identity.evaluatedMatrices!=scope.sourceA||scope.sourceB!=scope.sourceA)return false;
    const auto& native=rig->evaluatedWorld;
    const auto pole=[&](interaction::ArmJoints a){const auto& e=native[a.elbow].values[3];const auto& s=native[a.shoulder].values[3];return math::Vec3{e[0]-s[0],e[1]-s[1],e[2]-s[2]};};
    auto left=native[rig->left.wrist],right=native[rig->right.wrist];left.values[3][1]+=.08f;right.values[3][1]+=.08f;
    const auto solved=interaction::SolveTrackedArms(rig->parents,native,rig->left,rig->right,{left,pole(rig->left)},{right,pole(rig->right)});
    if(!solved)return false;const auto plan=BuildRigPosePlan(*rig,solved->writes);if(!plan)return false;
    scope.before=rig->nativeEvaluated;scope.posed=scope.before;scope.count=count;
    for(const auto& edit:plan->edits)scope.posed[edit.index]=edit.after;
    // The snapshot is still the exact source about to be copied. No writes to it.
    std::vector<std::array<std::byte,64>> current(count);if(!Read(scope.sourceA,current.data(),count*64)||current!=scope.before)return false;
    scope.changed=true;return true;
}
std::uintptr_t __cdecl PackHook(const void* source,void* destination,unsigned count){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const bool first=caller==base+profile.packCallerA,second=caller==base+profile.packCallerB;
    if(auto* sample=Bc2NativeHandWorkerSample::Scope::Current())sample->PackCalled(count);
    if(!enabled.load(std::memory_order_acquire)||(!first&&!second)||!scope.soldier||count<6||count>1024||reinterpret_cast<unsigned>(source)!=(first?scope.sourceA:scope.sourceB))return packOriginal(source,destination,count);
    unsigned weak=0;if(!resolveOwner||!resolveOwner(scope.soldier,weak)||weak!=scope.weak){++rejected;scope={};return packOriginal(source,destination,count);}
    ++copies;const auto elapsed=GetTickCount64()-started;
    // Pack audit (flicker): why a first pack did not use the posed rig.
    // 1 pose lock busy, 2 tracking stale, 3 nothing published, 4 published for
    // another weapon/space/owner/equipment, 5 shot guard/pose deadline/identity,
    // 6 native source unreadable, 7 native palette moved since the pose.
    unsigned packMiss=0;bool packMagazineFallback=false;
    if(first&&hands){
        std::shared_ptr<const PublishedPose> pose;bool previewAllowed=false;
        {std::unique_lock lock(frameMutex,std::try_to_lock);
        if(!lock.owns_lock())packMiss=1;else if(!Fresh(tracking.deadline))packMiss=2;else if(!published)packMiss=3;
        else if(!(published->weapon==tracking.weapon&&published->space==tracking.input.spaceGeneration&&published->ownerGeneration==tracking.ownerGeneration&&published->equipmentGeneration==tracking.equipmentGeneration))packMiss=4;
        if(lock.owns_lock()&&Fresh(tracking.deadline)&&published&&published->weapon==tracking.weapon&&published->space==tracking.input.spaceGeneration&&published->ownerGeneration==tracking.ownerGeneration&&published->equipmentGeneration==tracking.equipmentGeneration){
            pose=published;previewAllowed=tracking.sightPreview.valid&&tracking.sightPreview.token==pose->previewToken&&
                Fresh(tracking.sightPreview.deadline)&&tracking.input.focused&&tracking.input.headValid&&
                tracking.input.hands[0].gripTracked&&tracking.input.hands[1].gripTracked&&tracking.input.hands[0].squeeze>.35f;
        }}
        // PublishTracking always updates this guard, even if frameMutex was
        // busy. A release/recenter/item change must invalidate a latched preview
        // without waiting for another successful pose publication.
        // Hold: a busy pose lock or a just-stale tracking packet reuses the last
        // posed rig for up to two frames instead of snapping to the native pose.
        bool held=false;
        if(!pose&&(packMiss==1||packMiss==2)&&lastGoodPose&&ReloadNanos()-lastGoodPoseNs<=PoseHoldNs){pose=lastGoodPose;held=true;++poseHolds;}
        const auto currentGuard=shotGuard.load(std::memory_order_acquire);
        previewAllowed=previewAllowed&&currentGuard&&currentGuard->valid&&currentGuard->leftTracked&&pose&&
            currentGuard->previewToken==pose->previewToken&&currentGuard->weapon==pose->weapon&&
            currentGuard->soldier==scope.soldier&&currentGuard->weak==scope.weak&&
            currentGuard->owner==pose->ownerGeneration&&currentGuard->space==pose->space&&currentGuard->equipmentGeneration==pose->equipmentGeneration&&Fresh(currentGuard->deadline);
        if(pose&&currentGuard&&currentGuard->valid&&currentGuard->weapon==pose->weapon&&currentGuard->owner==pose->ownerGeneration&&currentGuard->space==pose->space&&currentGuard->equipmentGeneration==pose->equipmentGeneration&&currentGuard->equipment==pose->equipment&&EquipmentStillCurrent(pose->equipment)&&pose->identity.soldier==scope.soldier&&pose->identity.weak==scope.weak&&pose->identity.evaluatedMatrices==scope.sourceA&&scope.sourceA==scope.sourceB&&pose->identity.count==count&&(Fresh(pose->deadline)||held)&&(!pose->recoilDeadline||Fresh(pose->recoilDeadline))&&AuthoredCurrent(pose->authoredGrip)&&(!pose->freeRight||FreeRightCurrent(*pose->freeRight))){
            std::vector<std::array<std::byte,64>> current(count);
            const bool readable=Read(scope.sourceA,current.data(),count*64);if(!readable)packMiss=6;else if(current!=pose->before)packMiss=7;
            // Rebase: during a reload animation the native palette changes every
            // frame, and a pack after a newer native evaluation used to drop the
            // posed rig for that frame (miss 7). Our edits replace their bones;
            // every other bone takes the current native value.
            const bool moved=readable&&current!=pose->before;
            const auto rebase=[&](const std::vector<std::array<std::byte,64>>& palette){
                if(!moved||palette.size()!=count)return palette;auto out=current;
                for(unsigned n=0;n<count;++n)if(palette[n]!=pose->before[n])out[n]=palette[n];return out;};
            if(readable){if(moved)++poseRebases;
                scope.handPose=held?nullptr:pose;scope.recoilDeadline=pose->recoilDeadline;scope.before=moved?current:pose->before;scope.authoredGrip=pose->authoredGrip;scope.drawSource=pose->drawSource;scope.freeRight=pose->freeRight;scope.equipment=currentGuard->equipment;scope.equipmentGeneration=currentGuard->equipmentGeneration;
                const bool fallback=pose->previewToken&&!previewAllowed&&pose->previewBase.size()==count;
                const auto reload=reloadGuard.load(std::memory_order_acquire);
                const auto reloadNow=ReloadNanos();
                scope.posed=rebase(fallback?pose->previewBase:pose->posed);
                // Retain ORIGINAL reload authority and base for both native
                // copies. Current guards are rechecked immediately before each
                // Pack, so a release, receipt rejection or tail expiry between
                // A and B restores this same base without borrowing a new lease.
                scope.reloadPreview=pose->reloadPreview;scope.reloadBase=pose->reloadPreview?rebase(pose->reloadBase):std::vector<std::array<std::byte,64>>{};
                scope.reloadDeadlineNs=pose->reloadDeadlineNs;scope.reloadPoseGeneration=pose->generation;scope.reloadPack=-1;
                scope.nativeRack=pose->nativeRack;scope.nativeRackBase=pose->nativeRack?rebase(pose->nativeRackBase):std::vector<std::array<std::byte,64>>{};
                scope.nativeRackFallback=false;
                // Overlays built on the old native palette (belt, shell hide,
                // visibility, ordinary recovery) are only used unrebased.
                if(!moved&&pose->belt&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility)scope.belt=pose->belt;
                if(pose->magazine){scope.magazine=pose->magazine;scope.magazineBase=rebase(pose->magazineBase);}
                if(!moved&&pose->shellHide&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility)scope.shellHide=pose->shellHide;
                if(!moved&&pose->visibility&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&scope.posed==pose->visibility->ordinary){
                    const auto requested=visibilityGuard.load(std::memory_order_acquire);
                    if(requested&&WeaponVisibilityCurrent(*pose->visibility,*requested,reloadNow)&&(!reload||!reload->preview)&&currentGuard&&currentGuard->valid&&
                        !currentGuard->previewToken&&
                        currentGuard->leftTracked&&currentGuard->generation>=pose->generation&&Fresh(currentGuard->deadline)){
                        scope.visibility=pose->visibility;scope.posed=scope.visibility->privatePalette;
                    }else ++visibilityFallbacks;
                }
                if(fallback)++sightPreviewBaseFallbacks;
                if(!moved&&pose->ordinaryRecovery&&!scope.visibility&&!scope.freeRight&&!scope.magazine&&!scope.shellHide&&!scope.belt&&
                   !pose->reloadPreview&&!pose->previewToken&&OrdinaryGuardCurrent(*pose->ordinaryRecovery)){
                    scope.ordinaryRecovery=pose->ordinaryRecovery;scope.ordinaryRig=pose->identity;
                    scope.ordinaryDeadlineNs=std::min(ReloadNanos(pose->deadline),pose->ordinaryRecovery->input.deadlineNs);
                }
                scope.count=count;scope.changed=true;
            }
        }
        else if(pose&&!packMiss)packMiss=5;
        if(scope.changed&&pose&&!held){lastGoodPose=pose;lastGoodPoseNs=ReloadNanos();}
        if(held&&scope.changed)packMiss=packMiss==1?8:9; // 8/9: held through a busy lock / stale tracking
        if(!scope.changed)++poseMisses;
    }
    if(first&&pulse&&elapsed>=600&&elapsed<1800&&!MakePulse(count)){++rejected;scope.changed=false;}
    // A same-address replacement between the native pack callbacks cannot use
    // any old private pose, claim or paired receipt, even before the next Gather.
    if(scope.changed&&hands){const auto current=shotGuard.load(std::memory_order_acquire);
        if((scope.recoilDeadline&&!Fresh(scope.recoilDeadline))||!current||!current->valid||current->equipmentGeneration!=scope.equipmentGeneration||
           current->equipment!=scope.equipment||!EquipmentStillCurrent(scope.equipment)||!AuthoredCurrent(scope.authoredGrip)){
            scope.changed=false;scope.authoredGrip.reset();scope.visibility.reset();scope.freeRight.reset();scope.shellHide.reset();scope.magazine.reset();scope.reloadPreview.reset();scope.nativeRack.reset();
            scope.belt.reset();scope.beltMask=0;scope.beltVisible=false;
            scope.visibilityMask=scope.freeRightMask=scope.shellHideMask=scope.magazineMask=0;scope.reloadPack=-1;
        }}
    // A release/equip/recenter between native Pack callbacks must restore the
    // exact ordinary palette immediately, and can never mint a paired receipt.
    if(scope.visibility){
        const auto requested=visibilityGuard.load(std::memory_order_acquire);
        const auto current=shotGuard.load(std::memory_order_acquire);
        const auto reload=reloadGuard.load(std::memory_order_acquire);
        if(!requested||!WeaponVisibilityCurrent(*scope.visibility,*requested,ReloadNanos())||!current||!current->valid||!current->leftTracked||
            current->previewToken||(reload&&reload->preview)||
            current->soldier!=scope.soldier||current->weak!=scope.weak||current->weapon!=scope.visibility->nativeOwner.weapon||
            current->owner!=scope.visibility->nativeOwner.actorGeneration||current->space!=scope.visibility->nativeOwner.space||
            current->generation<scope.visibility->inputSequence||!Fresh(current->deadline)){
            scope.posed=scope.visibility->ordinary;scope.visibility.reset();scope.visibilityMask=0;++visibilityFallbacks;
        }
    }
    if(scope.changed&&scope.reloadPreview){
        const auto current=reloadGuard.load(std::memory_order_acquire);
        const auto shot=shotGuard.load(std::memory_order_acquire);
        const auto& old=*scope.reloadPreview;
        const auto* owner=old.selectedMeshes?&old.selectedMeshes->owner:nullptr;
        const bool coherent=owner&&shot&&shot->valid&&shot->leftTracked&&current&&
            shot->soldier==scope.soldier&&shot->weak==scope.weak&&shot->weapon==owner->weapon&&
            shot->owner==owner->actorGeneration&&shot->space==owner->space&&shot->equipmentGeneration==scope.equipmentGeneration&&
            shot->generation>=scope.reloadPoseGeneration&&shot->generation==current->inputEvidence.sequence&&Fresh(shot->deadline);
        std::vector<std::array<std::byte,64>> actual(count);
        if(!Read(reinterpret_cast<unsigned>(source),actual.data(),count*64)||actual!=scope.before){
            scope.changed=false;scope.reloadPreview.reset();scope.reloadPack=-1;
        }else{
            const auto choice=scope.reloadPack==ReloadPackFallback?ReloadPackedPaletteChoice{scope.reloadBase,true}:
                SelectReloadPackedPalette(old,current.get(),ReloadNanos(),scope.reloadDeadlineNs,scope.posed,scope.reloadBase,coherent);
            if(choice.bytes.empty()){scope.changed=false;scope.reloadPack=-1;}
            else {
                if(choice.bytes.data()!=scope.posed.data())scope.posed.assign(choice.bytes.begin(),choice.bytes.end());
                scope.reloadPack=choice.fallback?ReloadPackFallback:int(old.phase);
            }
            if(choice.fallback)++reloadPreviewFallbacks;
        }
    }
    // Shell suppression has its own current cycle authority, not an AmmoObject
    // claim. Revalidate before BOTH native Pack callbacks; cancellation between
    // eyes restores the exact ordinary palette and cannot mint a paired receipt.
    if(scope.shellHide){
        const auto current=shotGuard.load(std::memory_order_acquire);
        const auto reload=reloadGuard.load(std::memory_order_acquire);
        const auto& sourceControl=scope.shellHide->source;
        const bool coherent=current&&current->valid&&current->leftTracked&&reload&&
            current->soldier==scope.soldier&&current->weak==scope.weak&&current->weapon==sourceControl.owner.weapon&&
            current->owner==sourceControl.owner.actorGeneration&&current->space==sourceControl.owner.space&&
            current->generation>=sourceControl.inputEvidence.sequence&&reload->inputEvidence.sequence==current->generation&&
            !current->previewToken&&Fresh(current->deadline);
        std::vector<std::array<std::byte,64>> currentSource(count);
        if(!Read(reinterpret_cast<unsigned>(source),currentSource.data(),count*64)||currentSource!=scope.before){
            scope.changed=false;scope.shellHide.reset();scope.shellHideMask=0;scope.shellHidden=false;++reloadShellHideSourceRejects;
        }else{
            const auto chosen=SelectReloadShellPackedPalette(*scope.shellHide,reload.get(),ReloadNanos(),coherent);
            scope.shellHidden=!chosen.fallback;
            if(chosen.bytes.empty()){scope.changed=false;scope.shellHide.reset();scope.shellHideMask=0;}
            else scope.posed.assign(chosen.bytes.begin(),chosen.bytes.end());
            if(chosen.fallback){scope.shellHideMask=0;++reloadShellHideFallbacks;}
        }
    }
    // The single idle shell is only a pool handle. Check current supply, exact
    // native source and original pose deadlines before EACH native Pack.
    if(scope.belt){
        const auto shot=shotGuard.load(std::memory_order_acquire);
        const auto reload=reloadGuard.load(std::memory_order_acquire);
        const auto& old=scope.belt->source;
        const bool coherent=shot&&shot->valid&&shot->leftTracked&&!shot->weaponActionsBlocked&&reload&&reload->belt&&
            ReloadBeltCompatible(*reload,ReloadNanos())&&!shot->previewToken&&
            shot->soldier==scope.soldier&&shot->weak==scope.weak&&shot->weapon==old.owner.weapon&&
            shot->owner==old.owner.actorGeneration&&shot->space==old.owner.space&&
            shot->equipmentGeneration==old.visual.input.owner.equipGeneration&&EquipmentStillCurrent(shot->equipment)&&
            shot->generation>=old.visual.input.sequence&&shot->generation==reload->inputEvidence.sequence&&Fresh(shot->deadline);
        std::vector<std::array<std::byte,64>> actual(count);
        if(!Read(reinterpret_cast<unsigned>(source),actual.data(),count*64)||actual!=scope.before){
            scope.changed=false;scope.belt.reset();scope.beltMask=0;++beltSourceRejected;
        }else{
            const bool cycleHidden=scope.shellHide&&scope.shellHidden&&scope.belt->source.heldCycle&&
                scope.shellHide->source.shellControl&&scope.shellHide->source.shellControl->cycle==scope.belt->source.heldCycle->cycle;
            const auto fallback=scope.shellHide?std::span<const std::array<std::byte,64>>(scope.posed):
                std::span<const std::array<std::byte,64>>(scope.belt->ordinary);
            const auto choice=SelectSpasBeltOverlay(*scope.belt,reload&&reload->belt?reload->belt.get():nullptr,
                ReloadNanos(),coherent,fallback,cycleHidden);
            scope.beltVisible=!choice.fallback;
            if(choice.bytes.empty()){scope.changed=false;scope.belt.reset();scope.beltMask=0;}
            else if(choice.bytes.data()!=scope.posed.data())scope.posed.assign(choice.bytes.begin(),choice.bytes.end());
            if(choice.fallback){scope.beltMask=0;++beltFallbacks;}
        }
    }
    // The native rack's exact original authority is checked for BOTH native
    // copies. A released grip, new phase, deadline or physical revision restores
    // the original ordinary pose; a newer packet cannot renew this palette.
    if(scope.changed&&scope.nativeRack){
        const auto current=nativeRackGuard.load(std::memory_order_acquire);
        const auto shot=shotGuard.load(std::memory_order_acquire);const auto& old=*scope.nativeRack;
        const bool coherent=shot&&shot->valid&&current&&shot->soldier==scope.soldier&&shot->weak==scope.weak&&
            shot->weapon==old.owner.weapon&&shot->owner==old.owner.actorGeneration&&shot->space==old.trackingEpoch&&
            shot->equipmentGeneration==scope.equipmentGeneration&&shot->generation>=old.inputEvidence.sequence&&Fresh(shot->deadline);
        std::vector<std::array<std::byte,64>> actual(count);
        if(!Read(reinterpret_cast<unsigned>(source),actual.data(),count*64)||actual!=scope.before){
            scope.changed=false;scope.nativeRack.reset();
        }else{
            const auto choice=scope.nativeRackFallback?ReloadPackedPaletteChoice{scope.nativeRackBase,true}:
                SelectNativeRackPackedPalette(old,current.get(),ReloadNanos(),scope.posed,scope.nativeRackBase,coherent);
            if(choice.bytes.empty())scope.changed=false;
            else if(choice.bytes.data()!=scope.posed.data())scope.posed.assign(choice.bytes.begin(),choice.bytes.end());
            if(choice.fallback){scope.nativeRackFallback=true;++nativeRackFallbacks;}else ++nativeRackCopies;
        }
    }
    // A magazine's original owner/claims and source deadlines are checked at
    // each native copy. A newer packet cannot renew an older private palette.
    if(scope.magazine){
        const auto current=magazineGuard.load(std::memory_order_acquire);
        const auto shot=shotGuard.load(std::memory_order_acquire);
        const auto& old=*scope.magazine;
        const bool coherent=shot&&shot->valid&&shot->leftTracked&&current&&
            shot->soldier==scope.soldier&&shot->weak==scope.weak&&shot->weapon==old.owner.weapon&&
            shot->owner==old.owner.actorGeneration&&shot->space==old.owner.space&&
            shot->generation>=old.inputEvidence.sequence&&shot->generation==current->inputEvidence.sequence&&
            !shot->previewToken&&Fresh(shot->deadline);
        const auto observeFallback=[&](std::uint64_t flags,std::int64_t now){
            MagazineFallbackEvent e;e.flags=flags;e.nowNs=now;e.drawSerial=scope.drawSerial;
            e.source=reinterpret_cast<std::uintptr_t>(source);e.count=count;e.pack=first?1u:2u;
            e.coherent=coherent;e.original=MagazineFallbackSnapshotOf(&old);e.current=MagazineFallbackSnapshotOf(current.get());
            e.shotPresent=bool(shot);if(shot){e.shotSequence=shot->generation;e.shotDeadlineQpcTicks=shot->deadline;e.shotValid=shot->valid;e.leftTracked=shot->leftTracked;}
            else e.flags|=MagazineFallbackBit(MagazineFallbackReason::ShotMissing);
            magazineFallbackJournal.Record(e);
        };
        std::vector<std::array<std::byte,64>> actual(count);
        const bool sourceRead=Read(reinterpret_cast<unsigned>(source),actual.data(),count*64);
        if(!sourceRead||actual!=scope.before){
            observeFallback(MagazineFallbackBit(sourceRead?MagazineFallbackReason::SourceChanged:MagazineFallbackReason::SourceReadFailed),ReloadNanos());
            scope.changed=false;scope.magazine.reset();scope.magazineMask=0;++magazineFallbacks;
        }else{
            const auto now=ReloadNanos();
            const auto choice=SelectMagazinePackedPalette(old,current.get(),now,scope.posed,scope.magazineBase,coherent);
            if(choice.fallback)observeFallback(ClassifyMagazinePaletteFallback(old,current.get(),now,scope.posed,coherent),now);
            if(choice.bytes.empty())scope.changed=false;
            else if(choice.fallback)scope.posed.assign(choice.bytes.begin(),choice.bytes.end());
            if(choice.fallback){scope.magazine.reset();scope.magazineMask=0;++magazineFallbacks;packMagazineFallback=true;}
        }
    }
    if(scope.freeRight&&(!scope.visibility||!FreeRightCurrent(*scope.freeRight))){
        scope.posed=scope.before;scope.freeRight.reset();scope.visibility.reset();scope.visibilityMask=scope.freeRightMask=0;++freeRightFallbacks;
    }
    if(scope.ordinaryRecovery&&(!scope.changed||!OrdinaryGuardCurrent(*scope.ordinaryRecovery)||
       !EquipmentStillCurrent(scope.ordinaryRecovery->equipment)||ReloadNanos()>=scope.ordinaryDeadlineNs)){
        scope.ordinaryRecovery.reset();scope.ordinaryMask=0;
    }
    const bool changed=scope.changed&&scope.count==count&&scope.sourceA==scope.sourceB;
    if(GunEvents().Enabled()){GunEvent e;e.phase=GunEventPhase::Pack;e.wide=std::int64_t(scope.drawSerial);e.flags|=gun_flag::Wide;
        e.v[0]=first?1:2;e.v[1]=changed;e.v[2]=int(packMiss);e.v[3]=scope.magazine?1:packMagazineFallback?2:0;e.v[4]=scope.reloadPack;
        e.v[5]=(scope.visibility?1:0)|(scope.shellHide?2:0)|(scope.shellHidden?4:0)|(scope.belt?8:0)|(scope.beltVisible?16:0)|(scope.freeRight?32:0)|(scope.ordinaryRecovery?64:0);
        GunEvents().Push(e);}
    // Capture-only (FVR_BC2_CAPTURE_HIDE_LEFT_HAND=1 in the game's environment):
    // collapse the left wrist subtree so images show what the hand covers (rack
    // handle checks). Never set in play sessions; the posed rig is unchanged otherwise.
    static const bool hideLeftHand=[]{char v[4]{};return GetEnvironmentVariableA("FVR_BC2_CAPTURE_HIDE_LEFT_HAND",v,sizeof v)&&v[0]=='1';}();
    if(changed&&hideLeftHand){
        const RigMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
        if(const auto rig=ReadFirstPersonRig(memory,scope.soldier,scope.weak);rig&&rig->parents.size()==count)
            for(unsigned n=0;n<count;++n){bool inHand=n==unsigned(rig->left.wrist);
                for(auto at=rig->parents[n];at!=-1&&!inHand;at=rig->parents[at])inHand=unsigned(at)==unsigned(rig->left.wrist);
                if(inHand)std::memset(scope.posed[n].data(),0,48);}
    }
    const auto* input=changed?scope.posed.data():source;
    // The verified packer only reads source, copies twelve scalars per bone to
    // the native request arena, and returns. Private source memory cannot escape.
    const auto result=packOriginal(input,destination,count);
    if(second&&!changed&&nativeHandPoseConsumer.load(std::memory_order_acquire))StoreNativeHandPose(scope.drawSerial,{});
    if(changed){++changedCopies;if(second)++pairedCopies;
        std::vector<std::array<std::byte,64>> after(count);
        const bool unchanged=Read(reinterpret_cast<unsigned>(source),after.data(),count*64)&&after==scope.before;
        if(!unchanged)++sourceChanges;
        const auto afterGuard=shotGuard.load(std::memory_order_acquire);
        const bool equipmentRetained=!hands||(afterGuard&&afterGuard->valid&&afterGuard->equipmentGeneration==scope.equipmentGeneration&&
            afterGuard->equipment==scope.equipment&&EquipmentStillCurrent(scope.equipment)&&AuthoredCurrent(scope.authoredGrip));
        std::vector<std::byte> packed(count*48);bool match=equipmentRetained&&Read(reinterpret_cast<unsigned>(destination),packed.data(),packed.size());
        if(match)for(unsigned n=0;n<count&&match;++n)for(unsigned col=0;col<3&&match;++col)for(unsigned row=0;row<4;++row)
            if(std::memcmp(packed.data()+n*48+col*16+row*4,scope.posed[n].data()+row*16+col*4,4)){match=false;break;}
        if(!match)++packingFailures;
        // Independent hand geometry consumes the FINAL private palette after
        // rebasing and every fallback. No worker-eye lease participates here.
        if(second&&nativeHandPoseConsumer.load(std::memory_order_acquire)){
            const auto& pose=scope.handPose;std::shared_ptr<const NativeHandPosePublication> next;
            if(match&&unchanged&&pose&&pose->handLayout&&afterGuard&&afterGuard->valid&&
               afterGuard->soldier==scope.soldier&&afterGuard->weak==scope.weak&&afterGuard->weapon==pose->weapon&&
               afterGuard->owner==pose->ownerGeneration&&afterGuard->space==pose->space&&
               afterGuard->generation>=pose->generation&&Fresh(afterGuard->deadline)&&Fresh(pose->deadline)&&
               (!pose->handTracked[0]||afterGuard->leftTracked)){
                const RigMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
                const auto rig=ReadFirstPersonRig(memory,scope.soldier,scope.weak);
                if(rig&&rig->identity==pose->handLayout->rig&&rig->nativeEvaluated==scope.before){
                    const NativeHandPoseSource source{scope.soldier,scope.weak,pose->ownerGeneration,pose->weapon,
                        pose->equipmentGeneration,pose->space,pose->generation,scope.drawSerial,pose->handObservedNs,
                        std::min(ReloadNanos(pose->deadline),ReloadNanos(afterGuard->deadline))};
                    if(const auto final=BuildNativeHandPose(*pose->handLayout,scope.posed,source,pose->handTracked,ReloadNanos()))
                        try{next=std::make_shared<const NativeHandPosePublication>(NativeHandPosePublication{*final,scope.equipment});}catch(...){}
                }
            }
            // A rejected current publication must not leave a stale candidate.
            StoreNativeHandPose(scope.drawSerial,std::move(next));
        }
        if(match&&unchanged&&scope.authoredGrip)++authoredGripCopies;
        if(match&&unchanged&&scope.ordinaryRecovery&&OrdinaryGuardCurrent(*scope.ordinaryRecovery)&&
           EquipmentStillCurrent(scope.ordinaryRecovery->equipment)&&ReloadNanos()<scope.ordinaryDeadlineNs){
            ++ordinaryEquipmentCopies;scope.ordinaryMask|=first?1u:2u;
            if(second&&scope.ordinaryMask==3){
                const OrdinaryEquipmentPair receipt{*scope.ordinaryRecovery,scope.ordinaryRig,scope.drawSerial,
                    ReloadNanos(),scope.ordinaryDeadlineNs,scope.ordinaryMask};
                if(OrdinaryEquipmentPairCurrent(receipt,*scope.ordinaryRecovery,receipt.observedNs)){
                    try{ordinaryEquipmentPair.store(std::make_shared<const OrdinaryEquipmentPair>(receipt),std::memory_order_release);++ordinaryEquipmentPairs;}
                    catch(...){ordinaryEquipmentPair.store({},std::memory_order_release);}
                }
            }
        }else scope.ordinaryMask=0;
        if(match&&unchanged&&scope.magazine){
            ++magazineCopies;scope.magazineMask|=first?1u:2u;
            if(second&&scope.magazineMask==3){++magazinePairs;
                const auto current=magazineGuard.load(std::memory_order_acquire);
                if(scope.magazine->detach&&current&&current->detach&&scope.magazine->target&&
                   !scope.magazine->retainedVisualSuppression&&!current->retainedVisualSuppression){
                    const auto pair=MagazineDetachPackedPair(*scope.magazine->detach,*current->detach,
                        scope.magazine->target->role==interaction::MagazinePropRole::Attached,scope.drawSerial,scope.magazineMask,
                        match&&unchanged,ReloadNanos());
                    if(pair)magazineDetachPair.store(std::make_shared<const MagazineDetachPairReceipt>(*pair),std::memory_order_release);
                }
            }
            const auto role=scope.magazine->target?unsigned(scope.magazine->target->role):4u;
            if(role<4){++magazineRoleCopies[role];if(second&&scope.magazineMask==3)++magazineRolePairs[role];}
        }else scope.magazineMask=0;
        if(match&&unchanged&&scope.belt&&scope.beltVisible){
            ++beltCopies;scope.beltMask|=first?1u:2u;
            if(second&&scope.beltMask==3)++beltPairs;
        }else scope.beltMask=0;
        if(match&&unchanged&&scope.shellHide&&scope.shellHidden&&!scope.beltVisible){
            ++reloadShellHideCopies;scope.shellHideMask|=first?1u:2u;
            if(second&&scope.shellHideMask==3)++reloadShellHidePairs;
        }
        if(match&&unchanged&&scope.freeRight&&FreeRightCurrent(*scope.freeRight)){
            ++freeRightPackedCopies;scope.freeRightMask|=first?1u:2u;
            if(second&&scope.freeRightMask==3)++freeRightPairedCopies;
        }else scope.freeRightMask=0;
        // Diagnostic only: count a private palette only after both original
        // source preservation and actual packed destination byte comparison.
        if(match&&unchanged&&scope.reloadPack>=0&&scope.reloadPack<=ReloadPackFallback){
            if(scope.reloadPack==ReloadPackFallback){++reloadVerifiedFallbackPacks;if(second)++reloadVerifiedPairedFallbackPacks;}
            else {++reloadVerifiedPacks[scope.reloadPack];if(second)++reloadVerifiedPairedPacks[scope.reloadPack];}
        }
        if(scope.visibility){
            if(match&&unchanged){
                scope.visibilityMask|=first?1u:2u;
                if(scope.visibility->hidden)++visibilityHiddenCopies;else ++visibilityShownCopies;
                const auto requested=visibilityGuard.load(std::memory_order_acquire);const auto ns=ReloadNanos();
                const auto current=shotGuard.load(std::memory_order_acquire);
                const auto reload=reloadGuard.load(std::memory_order_acquire);
                const auto& owner=scope.visibility->nativeOwner;
                // Reacquire guards after the native copy too: its completion
                // cannot acknowledge an intent revoked while the packer ran.
                const bool receiptCurrent=requested&&WeaponVisibilityCurrent(*scope.visibility,*requested,ns)&&
                    current&&current->valid&&current->leftTracked&&!current->previewToken&&(!reload||!reload->preview)&&
                    current->soldier==owner.soldier&&current->weak==owner.weak&&current->weapon==owner.weapon&&
                    current->owner==owner.actorGeneration&&current->space==owner.space&&
                    current->generation>=scope.visibility->inputSequence&&current->equipmentGeneration==scope.equipmentGeneration&&current->equipment==scope.equipment&&Fresh(current->deadline)&&EquipmentStillCurrent(scope.equipment);
                if(!receiptCurrent){scope.visibilityMask=0;visibilityReceipt.store({},std::memory_order_release);}
                else if(scope.visibilityMask==3){
                    const auto& p=*scope.visibility;
                    try{visibilityReceipt.store(std::make_shared<const WeaponVisibilityReceipt>(WeaponVisibilityReceipt{
                        p.nativeOwner,p.rig,p.request,p.inputSequence,p.physicalEquipGeneration,scope.drawSerial,ns,
                        std::min(p.deadlineNs,requested->input.deadlineNs),3,p.hidden,scope.visibility}),std::memory_order_release);++visibilityPairedReceipts;}catch(...){}
                }
            }else {scope.visibilityMask=0;visibilityReceipt.store({},std::memory_order_release);}
        }
        if(match&&(ReloadProducerBinding().Enabled()||Bc2NativeHandWorkerSample::Scope::Current())){
            LARGE_INTEGER now{},frequency{};
            if(QueryPerformanceCounter(&now)&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0){
                const auto ns=(now.QuadPart/frequency.QuadPart)*1000000000+(now.QuadPart%frequency.QuadPart)*1000000000/frequency.QuadPart;
                // A rejected worker may expose a real owned pack. Retain scalar
                // facts only, pending its independent post-original identity
                // check; this grants no producer, eye or replacement authority.
                if(auto* sample=Bc2NativeHandWorkerSample::Scope::Current())
                    sample->Observe(scope.drawSerial,first,scope.drawSource,reinterpret_cast<std::uintptr_t>(destination),ns,
                        {reinterpret_cast<const std::byte*>(input),std::size_t(count)*64},packed,unchanged);
                ReloadProducerBinding().ObservePacked(scope.drawSerial,first,scope.drawSource,
                    reinterpret_cast<std::uintptr_t>(destination),ns,
                    {reinterpret_cast<const std::byte*>(input),std::size_t(count)*64},packed);
                if(const auto view=ReloadProducerBinding().CurrentHandsView();view&&
                   nativeHandCapture::WantPacked(first,scope.drawSerial,*view,scope.drawSource,ns)){
                    const RigMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
                    if(const auto rig=ReadFirstPersonRig(memory,scope.soldier,scope.weak))
                        nativeHandCapture::ObservePacked(first,scope.drawSerial,*view,scope.drawSource,*rig,
                            reinterpret_cast<std::uintptr_t>(destination),packed,ns);
                }
            }
        }
    }
    // The loop returns its advanced source pointer in EAX. Preserve that native
    // incidental result even for the two known callers that do not inspect it.
    const auto adjusted=changed?result-reinterpret_cast<std::uintptr_t>(input)+reinterpret_cast<std::uintptr_t>(source):result;
    if(second)scope={};return adjusted;
}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t image,ResolveOwner resolver,bool enablePulse,bool enableHands){
    if(hooks[0]||!resolver)return false;const auto found=DiscoverRigConsumer(bytes,pe);if(!found)return false;
    profile=*found;base=image;resolveOwner=resolver;pulse=enablePulse;hands=enableHands;if(pulse&&hands)return false;
    for(const auto [rva,n]:{std::pair{profile.prepare,0x314u},std::pair{profile.getterA,0x13u},std::pair{profile.getterB,0x13u},std::pair{profile.pack,0xceu}}){
        const auto at=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!at||!Read(unsigned(base+rva),live.data(),n)||std::memcmp(live.data(),bytes.data()+*at,n))return false;
    }
    const auto add=[&](unsigned slot,unsigned rva,void* callback,void** original){const auto address=reinterpret_cast<void*>(base+rva);if(MH_CreateHook(address,callback,original)!=MH_OK)return false;hooks[slot]=address;return true;};
    return add(0,profile.getterA,reinterpret_cast<void*>(GetA),reinterpret_cast<void**>(&getterA))&&add(1,profile.getterB,reinterpret_cast<void*>(GetB),reinterpret_cast<void**>(&getterB))&&add(2,profile.pack,reinterpret_cast<void*>(PackHook),reinterpret_cast<void**>(&packOriginal));
}
std::optional<ReloadProducerOwner> ReadReloadProducerOwner()noexcept {
    const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!ReloadProducerBinding().Enabled()||!guard||!guard->valid||!Fresh(guard->deadline)||!EquipmentStillCurrent(guard->equipment))return {};
    return ReloadProducerOwner{guard->soldier,guard->weak,guard->weapon,guard->owner,guard->space};
}
std::optional<NativeSkinOwner> ReadNativeSkinOwner()noexcept {
    const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!enabled.load(std::memory_order_acquire)||!guard||!guard->valid||!Fresh(guard->deadline)||!EquipmentStillCurrent(guard->equipment))return {};
    unsigned weak=0;
    if(!resolveOwner||!resolveOwner(guard->soldier,weak)||weak!=guard->weak||shotGuard.load(std::memory_order_acquire)!=guard)return {};
    NativeSkinOwner owner{guard->soldier,guard->weak,guard->weapon,guard->owner,guard->space,guard->equipmentGeneration,guard->equipment};
    if(!NativeSkinOwnerValid(owner))return {};
    return owner;
}
void EnableNativeHandPosePublication(bool value)noexcept {
    nativeHandPoseConsumer.store(value,std::memory_order_release);
    if(!value)nativeHandPose.store({},std::memory_order_release);
}
std::shared_ptr<const NativeHandPose> ReadNativeHandPose(std::int64_t now)noexcept {
    if(!enabled.load(std::memory_order_acquire)||!nativeHandPoseConsumer.load(std::memory_order_acquire))return {};
    const auto receipt=nativeHandPose.load(std::memory_order_acquire);const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!receipt||!NativeHandSourceFresh(receipt->pose.source,now)||!guard||!guard->valid||!Fresh(guard->deadline)||
       guard->equipment!=receipt->equipment||!EquipmentStillCurrent(receipt->equipment))return {};
    const auto& pose=receipt->pose;const auto& s=pose.source;
    if(guard->soldier!=s.actor||guard->weak!=s.weak||guard->weapon!=s.weapon||guard->owner!=s.actorGeneration||
       guard->equipmentGeneration!=s.equipmentGeneration||guard->space!=s.space||guard->generation<s.input||
       (pose.tracked[0]&&!guard->leftTracked))return {};
    return std::shared_ptr<const NativeHandPose>(receipt,&receipt->pose);
}
std::uint64_t ReadReticleEquipmentGeneration()noexcept {
    const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!ReloadProducerBinding().Enabled()||!guard||!guard->valid||!Fresh(guard->deadline)||!EquipmentStillCurrent(guard->equipment))return 0;
    return guard->equipmentGeneration;
}
void PublishNativeEye(unsigned soldier,unsigned weak,const math::Matrix4& camera)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire)||!interaction::InverseAnimatedTransform(camera))return;
    nativeEye.store(std::make_shared<const NativeEye>(NativeEye{soldier,weak,camera,GetTickCount64()}),std::memory_order_release);
}
std::optional<math::Matrix4> ReadEyeBase(unsigned soldier,unsigned weak,std::uint64_t space)noexcept {
    const auto eye=eyeTracking.load(std::memory_order_acquire);
    if(!eye||!eye->eyeBaseValid||eye->soldier!=soldier||eye->weak!=weak||eye->input.spaceGeneration!=space||
       !Fresh(eye->deadline)||!eye->input.focused||!eye->input.headValid)return {};
    return eye->eyeBase;
}
void PublishWeaponVisibility(const WeaponVisibilityRequest& request)noexcept {
    if(!request.enabled){visibilityGuard.store({},std::memory_order_release);visibilityReceipt.store({},std::memory_order_release);return;}
    const auto old=visibilityGuard.load(std::memory_order_acquire);
    if(!old||old->request!=request.request||old->nativeOwner!=request.nativeOwner||old->hide!=request.hide||
        old->input.owner!=request.input.owner)visibilityReceipt.store({},std::memory_order_release);
    try{visibilityGuard.store(std::make_shared<const WeaponVisibilityRequest>(request),std::memory_order_release);}
    catch(...){visibilityGuard.store({},std::memory_order_release);visibilityReceipt.store({},std::memory_order_release);}
}
void PublishOrdinaryEquipment(const std::optional<OrdinaryEquipmentRequest>& request)noexcept {
    if(!request||!OrdinaryEquipmentFresh(*request,ReloadNanos())){
        ordinaryEquipmentGuard.store({},std::memory_order_release);ordinaryEquipmentPair.store({},std::memory_order_release);return;
    }
    const auto old=ordinaryEquipmentGuard.load(std::memory_order_acquire);
    if(!old||!SameOrdinaryEquipment(*old,*request))ordinaryEquipmentPair.store({},std::memory_order_release);
    try{ordinaryEquipmentGuard.store(std::make_shared<const OrdinaryEquipmentRequest>(*request),std::memory_order_release);}
    catch(...){ordinaryEquipmentGuard.store({},std::memory_order_release);ordinaryEquipmentPair.store({},std::memory_order_release);}
}
std::optional<OrdinaryEquipmentPair> ReadOrdinaryEquipmentPair(const ReloadStateOwner& owner,std::uint64_t request,std::int64_t now)noexcept {
    const auto pair=ordinaryEquipmentPair.load(std::memory_order_acquire);
    const auto guard=ordinaryEquipmentGuard.load(std::memory_order_acquire);
    if(!pair||!guard||guard->owner!=owner||guard->request!=request||!OrdinaryGuardCurrent(pair->source)||
       !EquipmentStillCurrent(guard->equipment)||!OrdinaryEquipmentPairCurrent(*pair,*guard,now))return {};
    return *pair;
}
std::optional<WeaponVisibilityReceipt> ReadWeaponVisibilityReceipt(const ReloadStateOwner& owner,std::uint64_t request,std::int64_t now)noexcept {
    const auto receipt=visibilityReceipt.load(std::memory_order_acquire);const auto guard=visibilityGuard.load(std::memory_order_acquire);
    const auto current=shotGuard.load(std::memory_order_acquire);
    const auto reload=reloadGuard.load(std::memory_order_acquire);
    if(!enabled.load(std::memory_order_acquire)||!receipt||!guard||!guard->enabled||receipt->nativeOwner!=owner||receipt->request!=request||
        receipt->verifiedCopyMask!=3||receipt->observedNs<=0||receipt->observedNs>now||receipt->deadlineNs<=now||
        guard->nativeOwner!=owner||guard->request!=request||guard->hide!=receipt->hidden||guard->input.sequence<receipt->inputSequence||
        guard->input.observedNs>now||guard->input.deadlineNs<=now||!guard->input.focused||!guard->input.tracked[0]||!guard->input.tracked[1]||
        guard->input.owner.equipGeneration!=receipt->physicalEquipGeneration||!receipt->evidence||
        !WeaponVisibilityCurrent(*receipt->evidence,*guard,now)||!current||!current->valid||!current->leftTracked||
        current->previewToken||(reload&&reload->preview)||current->generation<receipt->inputSequence||
        current->soldier!=owner.soldier||current->weak!=owner.weak||current->weapon!=owner.weapon||
        current->owner!=owner.actorGeneration||current->space!=owner.space||current->equipmentGeneration!=receipt->physicalEquipGeneration||!Fresh(current->deadline)||!EquipmentStillCurrent(current->equipment))return {};
    return *receipt;
}
// Body props consume one immutable tracking publication. Separate atomics for
// shot/magazine/reload guards may already contain the next input packet; that
// safe renewal must not make the older, still-live prop disappear mid-read.
static bool BodyRenderInputFresh(const Tracking& t,std::int64_t now)noexcept {
    return t.eyeBaseValid&&!t.freeRight&&t.deadline&&now<ReloadNanos(t.deadline)&&
        interaction::ValidInput(t.input)&&t.input.focused&&t.input.headValid&&
        t.input.hands[0].gripTracked&&t.input.hands[1].gripTracked&&t.input.hands[1].aimTracked&&
        EquipmentStillCurrent(t.nativeEquipment);
}
static bool BodyRenderInputRetained(const Tracking& old,const Tracking& current,std::int64_t now)noexcept {
    return BodyRenderInputFresh(old,now)&&BodyRenderInputFresh(current,now)&&
        old.soldier==current.soldier&&old.weak==current.weak&&old.weapon==current.weapon&&
        old.ownerGeneration==current.ownerGeneration&&old.equipmentGeneration==current.equipmentGeneration&&
        old.nativeEquipment==current.nativeEquipment&&old.input.spaceGeneration==current.input.spaceGeneration&&
        old.input.generation<=current.input.generation;
}
static std::uint64_t BodyMaterialRevision(const BodyAmmoMaterial& original)noexcept {
    const auto view=physicalWeaponRuntime::Read(original.clientWeapon);
    if(!view||view->suspended||view->identity!=original.weapon||!view->revision||view->readiness.projectionPending)return 0;
    for(const auto& m:view->magazines)if(m.location==interaction::PhysicalMagazineLocation::Held)return 0;
    const interaction::HandInteractionKey pool{view->native.ammoAddress,view->identity.weaponGeneration};
    const auto current=BodyAmmoMaterialFor(view->identity,original.clientWeapon,pool,view->state,view->magazines);
    return current&&*current==original?view->revision:0;
}
static bool RackGuidanceMagazineFresh(const MagazineTracking& original,std::int64_t now)noexcept {
    if(!original.physicalAuthority)return MagazineTrackingFresh(original,now);
    const auto view=physicalWeaponRuntime::Read(original.authorityClientWeapon);
    if(!view||view->suspended||view->identity!=original.authorityWeapon||!view->revision||
       view->readiness.projectionPending||view->readiness.cue!=interaction::WeaponReadyCue::Rack||
       original.detach||!original.target||original.target->handTarget||
       (original.target->role!=interaction::MagazinePropRole::Attached&&original.target->role!=interaction::MagazinePropRole::Hidden))return false;
    auto render=original;render.authorityRevision=view->revision;
    return MagazineTrackingFresh(render,now);
}
static std::optional<BodyAmmoTracking> BodyAmmoTrackingFor(const Tracking& input,std::int64_t now){
    BodyAmmoTracking source;
    if(input.bodyAmmo)source=*input.bodyAmmo;
    else if(input.bodyMagazine){source.magazine=input.magazine;source.visual=*input.bodyMagazine;}
    else {
        const auto& reload=input.reload;
        if(!reload.belt||reload.inputEvidence.sequence!=input.input.generation||!ReloadBeltCompatible(reload,now))return {};
        source.shell=*reload.belt;source.visual=reload.belt->visual;
    }
    if(!BodyAmmoFresh(source,now))return {};
    const auto& nativeOwner=source.magazine?source.magazine->owner:source.shell->owner;
    const auto& physicalOwner=source.visual.input.owner;
    if(nativeOwner.soldier!=input.soldier||nativeOwner.weak!=input.weak||nativeOwner.weapon!=input.weapon||
       nativeOwner.actorGeneration!=input.ownerGeneration||nativeOwner.space!=input.input.spaceGeneration||
       physicalOwner.actor!=((std::uint64_t(input.weak)<<32)|input.soldier)||
       physicalOwner.actorGeneration!=input.ownerGeneration||physicalOwner.equipGeneration!=input.equipmentGeneration||
       physicalOwner.space!=input.input.spaceGeneration)return {};
    if(source.magazine&&source.magazine->family.carried&&source.magazine->family.binding.equipment!=input.nativeEquipment)return {};
    return source;
}
static std::optional<BodyAmmoRenderSource> BodyAmmoRenderSourceFor(const Tracking& input,std::int64_t now){
    const auto source=BodyAmmoTrackingFor(input,now);if(!source)return {};
    const auto pose=BuildBodyAmmoPose(*source,input.input,input.eyeBase,now);if(!pose)return {};
    BodyAmmoRenderSource result;result.prop={pose->source.visual,pose->partWorld};
    result.authority=std::make_shared<const BodyAmmoTracking>(pose->source);
    result.asset=pose->asset;result.mesh=pose->mesh;result.part=pose->part;result.rigFingerprint=pose->rigFingerprint;
    result.geometry=MakeBodyAmmoGeometryKey(pose->asset,pose->mesh,pose->part,pose->rigFingerprint);
    return result;
}
std::optional<BodyAmmoRenderSource> ReadBodyAmmoRenderSource(std::int64_t now)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    try {
        const auto input=eyeTracking.load(std::memory_order_acquire);
        if(!input||!BodyRenderInputFresh(*input,now))return {};
        const auto result=BodyAmmoRenderSourceFor(*input,now);if(!result)return {};
        const auto latest=eyeTracking.load(std::memory_order_acquire);
        if(!latest||!BodyRenderInputRetained(*input,*latest,now))return {};
        const auto current=BodyAmmoTrackingFor(*latest,now);
        if(!current||!BodyAmmoRetained(*result->authority,*current,now))return {};
        return result;
    }catch(...){return {};}
}
graphics::BodyPropEye ReadReloadGuidanceProps(std::int64_t now,bool pouchGeometryAvailable)noexcept {
    graphics::BodyPropEye out;
    if(!hands||!enabled.load(std::memory_order_acquire)||!gameplay::ReloadGuidanceEnabled())return out;
    try {
        const auto input=eyeTracking.load(std::memory_order_acquire);
        if(!input||!BodyRenderInputFresh(*input,now))return out;
        const auto hints=gameplay::ReadReloadHints(now);const auto& m=input->guidanceMagazine?*input->guidanceMagazine:input->magazine;
        const auto& who=m.inputEvidence.owner;
        const bool hintCurrent=hints&&hints->owner==who&&hints->space==input->input.spaceGeneration&&
            hints->inputSequence<=input->input.generation;
        const float units=input->input.worldUnitsPerMeter;
        // The existing typed body source owns finite availability and clears
        // during pickup/pending carry. Replace only missing mesh geometry.
        std::optional<BodyAmmoRenderSource> pouchSource;
        if(!pouchGeometryAvailable)if(const auto body=BodyAmmoRenderSourceFor(*input,now);body&&body->authority->magazine){
            pouchSource=body;
            const auto original=BodyAmmoHostInstance(*body,*body,now);
            const auto anchor=interaction::AmmoSupplyAnchorWorld(input->input,input->eyeBase,body->prop.source);
            const float alpha=hintCurrent?std::max(.16f,hints->alpha[std::size_t(interaction::ReloadHintPart::Pouch)]):.16f;
            if(original&&anchor)if(const auto zone=ReloadGuidanceZone(*anchor,std::min(.10f,body->prop.source.contact.radiusMeters),units,
                alpha,body->prop.source.input.owner,original->observedNs,original->deadlineNs,now,0))out.instances[out.count++]=*zone;
        }
        if(hintCurrent&&RackGuidanceMagazineFresh(m,now)&&m.family.binding.profile->chargingHandle){
            const auto& handle=*m.family.binding.profile->chargingHandle;
            const auto pose=ReadBodyWeaponFrame(input->soldier,input->weak,input->weapon);
            if(pose&&pose->ownerGeneration==m.owner.actorGeneration&&pose->space==m.owner.space&&
               pose->bodyRigFingerprint==m.family.binding.profile->geometry->rigFingerprint&&
               interaction::ValidChargingHandleProfile(handle)){
                auto local=interaction::reload_insertion_detail::Identity();
                const float travel=std::clamp(input->handleTravel,0.f,1.f)*handle.strokeMeters;
                for(unsigned n=0;n<3;++n)local.values[3][n]=(handle.handleMeters[n]+travel*handle.pullDirection[n])*units;
                if(input->nativeRackRequested){
                    const auto& rack=input->nativeRack;
                    if(!rack||!NativeRackPresentationFresh(*rack,now))return {};
                    const auto bone=std::find(rack->partBones.begin(),rack->partBones.end(),"jntWpn_17");
                    if(bone==rack->partBones.end())return {};
                    local=interaction::ScaleTranslation(rack->pose.partsInWeapon[std::size_t(bone-rack->partBones.begin())],units);
                }
                const auto center=interaction::Multiply(local,pose->trackedWeapon);
                const auto observed=std::max(hints->observedNs,m.inputEvidence.observedNs);
                const auto deadline=std::min({hints->deadlineNs,m.inputEvidence.deadlineNs,ReloadNanos(pose->deadline)});
                if(const auto zone=ReloadGuidanceZone(center,std::min(.04f,handle.grabRadiusMeters),units,
                    hints->alpha[std::size_t(interaction::ReloadHintPart::Handle)],who,observed,deadline,now,1))out.instances[out.count++]=*zone;
            }
        }
        const auto latest=eyeTracking.load(std::memory_order_acquire);
        if(!latest||!BodyRenderInputRetained(*input,*latest,now))return {};
        if(hintCurrent&&hints->alpha[std::size_t(interaction::ReloadHintPart::Handle)]>0&&
           !RackGuidanceMagazineFresh(m,now))return {};
        if(pouchSource){const auto current=BodyAmmoTrackingFor(*latest,now);
            if(!current||!BodyAmmoRetained(*pouchSource->authority,*current,now))return {};}
        return out;
    }catch(...){return {};}
}
std::optional<BodyHolsteredRenderSource> ReadHolsteredBodyRenderSource(std::int64_t now)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    const auto input=eyeTracking.load(std::memory_order_acquire);
    const auto guard=shotGuard.load(std::memory_order_acquire);
    const auto free=freeRightGuard.load(std::memory_order_acquire);
    const auto visibility=visibilityGuard.load(std::memory_order_acquire);
    if(!input||!guard||!free||!visibility||!input->freeRight||input->freeRight!=free||!input->bodyHolsterSlot||
       !input->eyeBaseValid||!input->weaponActionsBlocked||!guard->valid||!guard->leftTracked||!guard->weaponActionsBlocked||
       guard->generation!=input->input.generation||guard->space!=input->input.spaceGeneration||
       guard->soldier!=input->soldier||guard->weak!=input->weak||guard->weapon!=input->weapon||
       guard->owner!=input->ownerGeneration||guard->equipmentGeneration!=input->equipmentGeneration||
       guard->equipment!=input->nativeEquipment||!Fresh(guard->deadline)||now>=ReloadNanos(input->deadline)||
       !EquipmentStillCurrent(input->nativeEquipment)||!BodyFreeRightRenderCurrent(*free,free.get(),visibility.get(),now))return {};
    const auto& owner=free->visibility.nativeOwner;const auto& physical=free->input.owner;
    if(owner.soldier!=input->soldier||owner.weak!=input->weak||owner.weapon!=input->weapon||
       owner.actorGeneration!=input->ownerGeneration||owner.space!=input->input.spaceGeneration||
       physical.actor!=((std::uint64_t(input->weak)<<32)|input->soldier)||physical.actorGeneration!=input->ownerGeneration||
       physical.equipGeneration!=input->equipmentGeneration||physical.space!=input->input.spaceGeneration)return {};
    auto result=BuildBodyHolsteredSource(free,*input->bodyHolsterSlot,input->nativeEquipment,input->input,input->eyeBase,input->bodyAnchors,now);
    if(!result)return {};
    result->prop.deadlineNs=std::min({result->prop.deadlineNs,ReloadNanos(input->deadline),ReloadNanos(guard->deadline)});
    if(eyeTracking.load(std::memory_order_acquire)!=input||shotGuard.load(std::memory_order_acquire)!=guard||
       freeRightGuard.load(std::memory_order_acquire)!=free||visibilityGuard.load(std::memory_order_acquire)!=visibility||
       !EquipmentStillCurrent(input->nativeEquipment)||!BodyHolsteredSourceFresh(*result,now))return {};
    return result;
}
std::optional<BodyCarriedRenderBatch> ReadCarriedBodyRenderSource(std::int64_t now)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    const auto input=eyeTracking.load(std::memory_order_acquire);const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!input||!guard||!input->bodyInventoryDisplay||!input->eyeBaseValid||!guard->valid||
       guard->generation!=input->input.generation||guard->space!=input->input.spaceGeneration||
       guard->soldier!=input->soldier||guard->weak!=input->weak||guard->weapon!=input->weapon||
       guard->owner!=input->ownerGeneration||guard->equipmentGeneration!=input->equipmentGeneration||
       guard->equipment!=input->nativeEquipment||!Fresh(guard->deadline)||now>=ReloadNanos(input->deadline)||
       !EquipmentStillCurrent(input->nativeEquipment))return {};
    const auto& d=*input->bodyInventoryDisplay;const auto& owner=d.selectedOwner;
    if(owner.soldier!=input->soldier||owner.weak!=input->weak||owner.weapon!=input->weapon||owner.actorGeneration!=input->ownerGeneration||
       owner.space!=input->input.spaceGeneration||d.physicalOwner.equipGeneration!=input->equipmentGeneration||
       !gameplay::BodyDisplayNativeCurrent(d))return {};
    auto out=BuildBodyCarriedBatch(input->bodyInventoryDisplay,input->bodyCarriedMeshes,input->input,input->eyeBase,input->bodyAnchors,now);
    if(!out)return {};
    const auto itemsCurrent=[&](){for(unsigned n=0;n<out->count;++n){const auto e=body_carried_detail::Equipment(*out->configured[n]);
        if(!e||!EquipmentStillCurrent(*e))return false;}return true;};
    if(!itemsCurrent())return {};
    for(unsigned n=0;n<out->count;++n)out->instances[n].deadlineNs=std::min({out->instances[n].deadlineNs,ReloadNanos(input->deadline),ReloadNanos(guard->deadline)});
    if(eyeTracking.load(std::memory_order_acquire)!=input||shotGuard.load(std::memory_order_acquire)!=guard||
       !EquipmentStillCurrent(input->nativeEquipment)||!gameplay::BodyDisplayNativeCurrent(d)||!itemsCurrent()||!BodyCarriedBatchFresh(*out,ReloadNanos()))return {};
    return out;
}
void PublishTracking(const Tracking& source)noexcept {
    auto next=source;
    if(next.freeRight){const auto& p=*next.freeRight;const auto& o=p.visibility.nativeOwner;
        if(!next.weaponActionsBlocked||!BodyFreeRightEvidenceCurrent(p,ReloadNanos())||p.input.sequence!=next.input.generation||
            p.input.owner.space!=next.input.spaceGeneration||o.soldier!=next.soldier||o.weak!=next.weak||o.weapon!=next.weapon||
            o.actorGeneration!=next.ownerGeneration||p.input.deadlineNs>ReloadNanos(next.deadline)||
            !next.input.focused||!next.input.headValid||!next.input.hands[0].gripTracked||!next.input.hands[1].gripTracked)next.freeRight.reset();
    }
    if(!next.freeRight)next.bodyHolsterSlot.reset();
    if(next.bodyInventoryDisplay&&(!BodyInventoryDisplayFresh(*next.bodyInventoryDisplay,ReloadNanos())||
       next.bodyInventoryDisplay->sequence!=next.input.generation||next.bodyInventoryDisplay->physicalOwner.space!=next.input.spaceGeneration||
       next.bodyInventoryDisplay->deadlineNs>ReloadNanos(next.deadline)||!next.input.focused||!next.input.headValid)){
        next.bodyInventoryDisplay.reset();next.bodyCarriedMeshes={};
    }
    freeRightGuard.store(next.freeRight,std::memory_order_release);
    // Chest availability and guidance have purpose-specific render authority.
    // Capture the producer's exact material before strict held-part validation
    // rejects an older ledger revision after an otherwise unrelated shot.
    next.bodyAmmo.reset();next.guidanceMagazine.reset();
    if(next.bodyMagazine){BodyAmmoTracking body;body.magazine=next.magazine;body.visual=*next.bodyMagazine;
        if(next.magazine.physicalAuthority)body.materialCurrentRevision=BodyMaterialRevision;
        if(BodyAmmoFresh(body,ReloadNanos()))next.bodyAmmo=std::make_shared<const BodyAmmoTracking>(std::move(body));}
    if(RackGuidanceMagazineFresh(next.magazine,ReloadNanos()))next.guidanceMagazine=std::make_shared<const MagazineTracking>(next.magazine);
    const auto& magazineOwner=next.magazine.owner;
    if(!interaction::ValidInput(next.input)||!next.input.focused||!next.input.headValid||
       !next.input.hands[0].gripTracked||!next.input.hands[1].gripTracked||!next.input.hands[1].aimTracked||
       magazineOwner.soldier!=next.soldier||magazineOwner.weak!=next.weak||magazineOwner.weapon!=next.weapon||
       magazineOwner.actorGeneration!=next.ownerGeneration||magazineOwner.space!=next.input.spaceGeneration||
       next.magazine.inputEvidence.sequence!=next.input.generation||
       next.magazine.inputEvidence.deadlineNs>ReloadNanos(next.deadline)||
       !MagazineTrackingFresh(next.magazine,ReloadNanos()))next.magazine={};
    magazineGuard.store(next.magazine.enabled?std::make_shared<const MagazineTracking>(next.magazine):
        std::shared_ptr<const MagazineTracking>{},std::memory_order_release);
    next.nativeRackRequested=bool(next.nativeRack);
    if(next.nativeRack){const auto& rack=*next.nativeRack;
        if(!interaction::ValidInput(next.input)||!next.input.focused||!next.input.headValid||
           !next.input.hands[0].gripTracked||!next.input.hands[1].gripTracked||!next.input.hands[1].aimTracked||
           rack.owner.soldier!=next.soldier||rack.owner.weak!=next.weak||rack.owner.weapon!=next.weapon||
           rack.owner.actorGeneration!=next.ownerGeneration||rack.owner.space!=next.input.spaceGeneration||
           rack.trackingEpoch!=next.input.spaceGeneration||rack.assetName!=next.assetName.data()||
           rack.inputEvidence.sequence>next.input.generation||rack.inputEvidence.deadlineNs>ReloadNanos(next.deadline)||
           (rack.ownsHand&&next.input.hands[0].squeeze<=.35f)||!NativeRackPresentationFresh(rack,ReloadNanos()))next.nativeRack.reset();
    }
    // Always publish invalidation, even when the pose mutex below is busy.
    nativeRackGuard.store(next.nativeRack,std::memory_order_release);
    const auto& reloadOwner=next.reload.owner;
    if(!interaction::ValidInput(next.input)||!next.input.focused||!next.input.headValid||!next.input.hands[0].gripTracked||!next.input.hands[1].gripTracked||
       !next.input.hands[1].aimTracked||reloadOwner.soldier!=next.soldier||reloadOwner.weak!=next.weak||
       reloadOwner.weapon!=next.weapon||reloadOwner.actorGeneration!=next.ownerGeneration||reloadOwner.space!=next.input.spaceGeneration||
       next.reload.inputEvidence.sequence!=next.input.generation||next.reload.inputEvidence.deadlineNs>ReloadNanos(next.deadline)||
       !ReloadTrackingFresh(next.reload,ReloadNanos()))next.reload={};
    if(next.input.hands[0].squeeze<=.35f&&next.reload.preview&&next.reload.preview->actionHandOwned)next.reload.preview.reset();
    if(next.reload.belt&&(next.weaponActionsBlocked||!ReloadBeltCompatible(next.reload,ReloadNanos())))next.reload.belt.reset();
    // Always publish invalidation, including while the pose mutex is occupied.
    reloadGuard.store(next.reload.enabled?std::make_shared<const ReloadTracking>(next.reload):std::shared_ptr<const ReloadTracking>{},std::memory_order_release);
    if(next.deadline&&next.input.focused&&next.input.headValid&&interaction::ValidInput(next.input)){
        const auto eye=nativeEye.load(std::memory_order_acquire);const auto now=GetTickCount64();
        if(eye&&eye->soldier==next.soldier&&eye->weak==next.weak&&now>=eye->ms&&now-eye->ms<=150){
            auto eyeBase=interaction::MakeComfortCamera(eye->camera,(next.bodyYaw-3.141592653589793f)*57.29577951308232f);
            if(eyeBase){
                // The verified soldier camera supplies native eye/stance height.
                // Horizontal origin follows the collision actor, not gun sway.
                eyeBase->values[3][0]=next.actorPosition.x;eyeBase->values[3][2]=next.actorPosition.z;
                for(unsigned axis=0;axis<3;++axis)eyeBase->values[3][axis]-=next.input.worldUnitsPerMeter*(next.consumed.x*eyeBase->values[0][axis]-next.consumed.z*eyeBase->values[2][axis]);
                next.eyeBase=*eyeBase;next.eyeBaseValid=true;
            }
        }
    }
    eyeTracking.store(next.eyeBaseValid?std::make_shared<const Tracking>(next):std::shared_ptr<const Tracking>{},std::memory_order_release);
    if(!hands)return;
    const bool valid=next.deadline&&next.input.focused&&next.input.headValid&&next.input.hands[1].gripTracked&&next.input.hands[1].aimTracked&&interaction::ValidInput(next.input);
    shotGuard.store(std::make_shared<const ShotGuard>(ShotGuard{next.soldier,next.weak,next.weapon,next.ownerGeneration,next.input.spaceGeneration,next.input.generation,next.deadline,valid,next.input.hands[0].gripTracked,
        valid&&next.sightPreview.valid&&next.input.hands[0].gripTracked&&next.input.hands[0].squeeze>.35f&&Fresh(next.sightPreview.deadline)?next.sightPreview.token:0,next.weaponActionsBlocked,next.equipmentGeneration,next.nativeEquipment}),std::memory_order_release);
    if(!valid){shotPublication.store({},std::memory_order_release);PublishOrdinaryEquipment({});}
    std::unique_lock lock(frameMutex,std::try_to_lock);if(!lock.owns_lock())return;
    tracking=next;if(!next.deadline)published.reset();
}
std::optional<WeaponShotFrame> ReadWeaponFrame(unsigned soldier,unsigned weak,unsigned weapon,bool allowSuppressed)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    // An ordinary input publication may advance while these immutable pointers
    // are read. Retry a bounded number of times; do not weaken invalidation.
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=shotGuard.load(std::memory_order_acquire);const auto pose=shotPublication.load(std::memory_order_acquire);
        if(!guard||!guard->valid||(!allowSuppressed&&guard->weaponActionsBlocked)||!pose||guard->soldier!=soldier||guard->weak!=weak||guard->weapon!=weapon||
           pose->identity.soldier!=soldier||pose->identity.weak!=weak||pose->identity.weapon!=weapon||
           pose->identity.owner!=guard->owner||pose->identity.space!=guard->space||pose->identity.equipmentGeneration!=guard->equipmentGeneration||pose->identity.generation>guard->generation){++shotFrameUnavailable;return {};}
        if(!Fresh(guard->deadline)||!Fresh(pose->frame.deadline)||!EquipmentStillCurrent(guard->equipment)||!AuthoredCurrent(pose->authoredGrip)){++shotFrameExpired;return {};}
        if(shotGuard.load(std::memory_order_acquire)!=guard){++shotFrameRaces;continue;}
        return pose->frame;
    }
    return {};
}

std::optional<math::Matrix4> ReadWeaponBoreInGrip(unsigned soldier,unsigned weak,unsigned weapon,
    std::uint64_t owner,std::uint64_t equipmentGeneration,std::uint64_t space,const WeaponEquipmentIdentity& equipment)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=shotGuard.load(std::memory_order_acquire);
        const auto source=gripBorePublication.load(std::memory_order_acquire);
        if(!guard||!guard->valid||!source)return {};
        const auto& id=source->identity;
        if(guard->soldier!=soldier||guard->weak!=weak||guard->weapon!=weapon||guard->owner!=owner||
           guard->equipmentGeneration!=equipmentGeneration||guard->space!=space||guard->equipment!=equipment||
           id.soldier!=soldier||id.weak!=weak||id.weapon!=weapon||id.owner!=owner||
           id.equipmentGeneration!=equipmentGeneration||id.space!=space||id.equipment!=equipment||
           !id.generation||id.generation>guard->generation||!Fresh(guard->deadline)||!Fresh(id.deadline)||
           !EquipmentStillCurrent(equipment)||!AuthoredCurrent(source->authoredGrip))return {};
        if(shotGuard.load(std::memory_order_acquire)!=guard||gripBorePublication.load(std::memory_order_acquire)!=source)continue;
        return source->relation;
    }
    return {};
}
std::optional<WeaponShotFrame> ReadWeaponShotFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept {return ReadWeaponFrame(soldier,weak,weapon,false);}
std::optional<WeaponShotFrame> ReadBodyWeaponFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept {return ReadWeaponFrame(soldier,weak,weapon,true);}

MagazineRawContact ReadMagazineContact(const ReloadStateOwner& owner)noexcept {
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=magazineGuard.load(std::memory_order_acquire);const auto now=ReloadNanos();
        if(!guard||guard->owner!=owner||!MagazineTrackingFresh(*guard,now))return {};
        // Suppressed Fire does not suppress raw reload contact. This getter is
        // private to magazine interaction and never grants a firing pose.
        const auto shot=ReadWeaponFrame(owner.soldier,owner.weak,owner.weapon,true);
        if(!shot||!shot->magazine.valid||shot->magazine.owner!=owner||
           shot->magazine.inputEvidence.sequence!=shot->generation||
           shot->magazine.inputEvidence.sequence>guard->inputEvidence.sequence||
           shot->magazine.inputEvidence.observedNs>now||shot->magazine.inputEvidence.deadlineNs<=now)return {};
        if(magazineGuard.load(std::memory_order_acquire)!=guard)continue;
        return shot->magazine;
    }return {};
}
ReloadRawContact ReadReloadContact(const ReloadStateOwner& owner)noexcept {
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=reloadGuard.load(std::memory_order_acquire);const auto now=ReloadNanos();
        if(!guard||guard->owner!=owner||!ReloadTrackingFresh(*guard,now))return {};
        const auto shot=ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
        if(!shot||!shot->reload.valid||shot->reload.owner!=owner||shot->reload.inputEvidence.sequence!=shot->generation||
           shot->reload.inputEvidence.sequence>guard->inputEvidence.sequence||shot->reload.inputEvidence.observedNs>now||
           shot->reload.inputEvidence.deadlineNs<=now)return {};
        if(reloadGuard.load(std::memory_order_acquire)!=guard)continue;
        return shot->reload;
    }return {};
}
interaction::SupportGripContact ReadSupportContact(unsigned soldier,unsigned weak,unsigned weapon,std::uint64_t owner,std::uint64_t space)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=shotGuard.load(std::memory_order_acquire);const auto pose=shotPublication.load(std::memory_order_acquire);
        if(!guard||!guard->valid||guard->weaponActionsBlocked||!pose||guard->soldier!=soldier||guard->weak!=weak||guard->weapon!=weapon||
           guard->owner!=owner||guard->space!=space||pose->identity.soldier!=soldier||pose->identity.weak!=weak||
           pose->identity.weapon!=weapon||pose->identity.owner!=owner||pose->identity.space!=space||
           pose->identity.equipmentGeneration!=guard->equipmentGeneration||pose->identity.generation>guard->generation||!Fresh(guard->deadline)||!Fresh(pose->frame.deadline)||!AuthoredCurrent(pose->authoredGrip))return {};
        if(shotGuard.load(std::memory_order_acquire)!=guard||!EquipmentStillCurrent(guard->equipment))continue;
        return pose->frame.support;
    }
    return {};
}
WeaponSightContact ReadSightContact(unsigned soldier,unsigned weak,unsigned weapon,std::uint64_t owner,std::uint64_t space)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=shotGuard.load(std::memory_order_acquire);const auto pose=shotPublication.load(std::memory_order_acquire);
        if(!guard||!guard->valid||guard->weaponActionsBlocked||!guard->leftTracked||!pose||!pose->frame.sight.valid||!pose->identity.leftTracked||
           guard->soldier!=soldier||guard->weak!=weak||guard->weapon!=weapon||guard->owner!=owner||guard->space!=space||
           pose->identity.soldier!=soldier||pose->identity.weak!=weak||pose->identity.weapon!=weapon||
           pose->identity.owner!=owner||pose->identity.space!=space||pose->identity.equipmentGeneration!=guard->equipmentGeneration||pose->identity.generation>guard->generation||
           pose->frame.sight.generation!=pose->identity.generation||pose->frame.sight.deadline!=pose->frame.deadline||
           !Fresh(guard->deadline)||!Fresh(pose->frame.sight.deadline)||!AuthoredCurrent(pose->authoredGrip))return {};
        if(shotGuard.load(std::memory_order_acquire)!=guard||!EquipmentStillCurrent(guard->equipment))continue;
        return pose->frame.sight;
    }
    return {};
}
// Animation thread only. Up to eight placement rows (>=100 ms apart) after any
// carried item, equipment generation or orientation-source change, then one
// per second while the 40mm launcher is carried. Rig space, canonical +Z.
static void PublishPlacementEvidence(const Tracking& input,WeaponOrientationSource source,const math::Matrix4& nativeWeapon,
                                     const math::Matrix4& placed,const math::Matrix4& body,bool attachmentPending)noexcept {
    static unsigned lastWeapon=0;static std::uint64_t lastEquipment=0;static WeaponOrientationSource lastSource{};
    static unsigned burst=0;static ULONGLONG lastMs=0;
    const auto now=GetTickCount64();const std::string_view asset(input.assetName.data());
    if(input.weapon!=lastWeapon||input.equipmentGeneration!=lastEquipment||source!=lastSource){
        lastWeapon=input.weapon;lastEquipment=input.equipmentGeneration;lastSource=source;burst=0;lastMs=0;
    }
    if(!((burst<8&&now-lastMs>=100)||(asset=="40mmgl"&&now-lastMs>=1000)))return;
    ++burst;lastMs=now;
    WeaponEffectRecord row;row.kind=WeaponEffectRecordKind::Placement;row.source=source;row.tick=gameplay::CurrentGatherTick();
    row.qpcNs=ReloadNanos();row.weapon=input.weapon;row.generation=input.input.generation;row.a=std::uint32_t(input.equipmentGeneration);
    SetRecordAsset(row,asset);
    row.m[0]=nativeWeapon;row.m[1]=placed;row.m[3]=body;row.matrices=1|2|8;
    row.flags=(attachmentPending?weapon_effect_flag::AttachmentPending:0u)|(input.weaponProfile?weapon_effect_flag::ProfileAdmitted:0u);
    if(const auto aim=interaction::TrackedAimFrame(input.input,body)){
        row.m[2]=*aim;row.matrices|=4;row.flags|=weapon_effect_flag::AimValid;
        // Measurement axes come from the asset's registry entry even when the
        // visual family was not admitted; they never feed the rig.
        if(const auto* axes=input.weaponProfile?input.weaponProfile:FindWeaponProfile(asset))
            row.delta=MeasureBasisDelta(placed,axes->core.modelForward,axes->core.modelUp,*aim,{0,0,1},{0,1,0});
    }
    WeaponEffectEvidence().Push(row);
}
// XM8 VR bolt (Bc2Xm8BoltRuntime): publish the posed carrier (jntWpnwpnJnt_16,
// including any handle travel already written above) for the post-eye draw, and
// collapse the jntWpn_4 card in THIS palette once both eyes have drawn the bolt.
// The colour and ZOnly passes and both eyes pack this one palette.
static void PoseXm8Bolt(const RigSnapshot& rig,const Tracking& input,std::span<const interaction::BoneWrite> writes,
    const math::Matrix4& placed,bool gunRetargeted,std::vector<std::array<std::byte,64>>& posed)noexcept {
    namespace bolt=xm8BoltRuntime;
    const auto asset=bolt::Asset();if(!asset)return;
    if(std::string_view(input.assetName.data())!=Xm8BoltWeaponAsset){bolt::PublishSource({});return;}
    const auto binding=BindXm8BoltRig(rig);
    std::optional<math::Matrix4> carrier;
    if(binding)for(const auto& w:writes)if(w.index==binding->carrier){carrier=w.transform;break;}
    const float units=input.input.worldUnitsPerMeter;
    const auto world=carrier?Xm8BoltPartWorld(*carrier,units):std::nullopt;
    const auto now=bolt::NowNs();
    const auto reason=DecideXm8BoltCard({true,true,true,bool(binding),gunRetargeted&&bool(world),bolt::CardCollapseAllowed(now)});
    bolt::NoteCardDecision(reason);
    if(!binding||!world||!gunRetargeted||now<=0){bolt::PublishSource({});return;}
    bolt::RenderSource source;source.world=*world;
    source.actor=(std::uint64_t(input.weak)<<32)|input.soldier;source.actorGeneration=input.ownerGeneration?input.ownerGeneration:1;
    source.weapon=input.weapon?input.weapon:1;source.equipmentGeneration=input.equipmentGeneration?input.equipmentGeneration:1;
    source.space=input.input.spaceGeneration?input.input.spaceGeneration:1;source.input=input.input.generation?input.input.generation:1;
    source.observedNs=now;source.deadlineNs=std::min(ReloadNanos(input.deadline),now+100000000);
    if(const auto inverse=interaction::InverseAnimatedTransform(placed))
        if(const auto travel=Xm8BoltObservedTravel(*asset,interaction::Multiply(*carrier,*inverse),units))source.travel=*travel;
    if(reason==Xm8BoltCardReason::Collapse)if(auto collapsed=CollapseXm8BoltCard(posed,*binding)){posed=std::move(*collapsed);source.cardCollapsed=true;}
    bolt::PublishSource(source);
}
void RetargetWeapon(unsigned animation,void* destination)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return;Callback callback;
    Tracking input;
    {std::unique_lock lock(frameMutex,std::try_to_lock);if(!lock.owns_lock()||!Fresh(tracking.deadline))return;input=tracking;
        if(!input.input.focused||!input.input.headValid||(!input.input.hands[0].gripTracked&&!input.input.hands[1].gripTracked)){++trackingUnavailable;published.reset();return;}
    }
    unsigned weak=0;if(!resolveOwner||!resolveOwner(input.soldier,weak)||weak!=input.weak){RejectTracking(0);return;}
    // The producer already reflected this exact SoldierWeaponData. Here require
    // its unchanged pointer and coherent immutable asset/persistence snapshot.
    const WeaponModeMemory equipmentMemory{&input.nativeEquipment,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);},
        [](void* context,unsigned at,const char*){return static_cast<const WeaponEquipmentIdentity*>(context)->data==at;}};
    const auto currentEquipment=ReadWeaponEquipmentIdentity(equipmentMemory,input.weapon);
    if(!currentEquipment||*currentEquipment!=input.nativeEquipment){RejectTracking(0);return;}
    const RigMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto rig=ReadFirstPersonRig(memory,input.soldier,input.weak);
    if(!rig||rig->identity.animation!=animation){RejectTracking(1);return;}
    std::array<std::byte,64> original{};const auto at=reinterpret_cast<unsigned>(destination);if(!Read(at,original.data(),64)){RejectTracking(2);return;}
    math::Matrix4 weapon{};std::memcpy(&weapon,original.data(),64);
    for(unsigned row=0;row<4;++row)weapon.values[row][3]=row==3?1.f:0.f;
    for(unsigned n=0;n<4;++n){weapon.values[2][n]=-weapon.values[2][n];weapon.values[n][2]=-weapon.values[n][2];}
    // The verified native eye height and collision actor define the same
    // compensated reference origin used by HMD views.
    auto body=input.eyeBaseValid?std::optional<math::Matrix4>{input.eyeBase}:std::nullopt;
    if(!body){RejectTracking(3);return;}
    std::unique_lock lock(frameMutex,std::try_to_lock);
    if(!lock.owns_lock()||!Fresh(input.deadline)||tracking.soldier!=input.soldier||tracking.weak!=input.weak||tracking.weapon!=input.weapon||tracking.ownerGeneration!=input.ownerGeneration||tracking.equipmentGeneration!=input.equipmentGeneration||tracking.nativeEquipment!=input.nativeEquipment||tracking.input.spaceGeneration!=input.input.spaceGeneration)return;
    const interaction::TrackedRigOwner owner{(std::uint64_t(input.weak)<<32)|input.soldier,input.ownerGeneration,input.weapon,rig->identity.skeleton,input.equipmentGeneration};
    const auto animatedBody=*body;
    const auto& native=rig->evaluatedWorld;
    const auto captureNow=GetTickCount64();
    if(input.assetName[0]&&captureNow-captureTime>=100){
        captureTime=captureNow;
        WeaponCaptureSample sample;sample.assetName=input.assetName.data();
        sample.provenance=WeaponCaptureProvenance{input.equipmentGeneration,input.nativeEquipment.data,input.nativeEquipment.persistence,rig->identity};
        sample.unitsPerMeter=input.input.worldUnitsPerMeter;sample.generation=input.input.generation;sample.predictedNs=input.input.predictedNs;sample.capturedMs=captureNow;
        sample.space=input.input.spaceGeneration;sample.ownerGeneration=input.ownerGeneration;sample.actor=input.soldier;sample.weapon=input.weapon;
        sample.nativeWeapon=native[rig->weaponBone];sample.nativeLeft=native[rig->left.wrist];sample.nativeRight=native[rig->right.wrist];
        sample.rootName=rig->names[rig->weaponBone];sample.leftName=rig->names[rig->left.wrist];sample.rightName=rig->names[rig->right.wrist];
        std::uint64_t fingerprint=14695981039346656037ull;
        const auto hashWord=[&](std::uint32_t word){for(unsigned n=0;n<4;++n){fingerprint^=(word>>(n*8))&255;fingerprint*=1099511628211ull;}};
        for(unsigned n=0;n<rig->names.size();++n){
            for(unsigned char c:rig->names[n]){fingerprint^=c;fingerprint*=1099511628211ull;}
            hashWord(0);hashWord(std::uint32_t(rig->parents[n]));
            for(const auto& row:rig->inverseBind[n].values)for(float v:row)hashWord(std::bit_cast<std::uint32_t>(v));
            bool weaponChild=n==rig->weaponBone;
            for(auto at=rig->parents[n];at!=-1&&!weaponChild;at=rig->parents[at])weaponChild=unsigned(at)==rig->weaponBone;
            if(weaponChild){
                if(sample.nativeWeaponBones.size()<WeaponCapture::MaxWeaponBones){
                    WeaponBoneCapture bone;bone.name=rig->names[n];if(rig->parents[n]!=-1)bone.parentName=rig->names[rig->parents[n]];
                    bone.native=native[n];bone.inverseBind=rig->inverseBind[n];bone.hidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)!=rig->nativeHiddenLeaves.end();
                    sample.nativeWeaponBones.push_back(std::move(bone));
                }else ++sample.weaponBonesDropped;
            }
            // Retain authored finger motion alongside the weapon component
            // poses, from this same snapshot BEFORE VR hand/IK edits below.
            bool leftHandChild=n==rig->left.wrist;
            for(auto at=rig->parents[n];at!=-1&&!leftHandChild;at=rig->parents[at])leftHandChild=unsigned(at)==rig->left.wrist;
            if(leftHandChild){
                if(sample.nativeLeftHandBones.size()<WeaponCapture::MaxLeftHandBones){
                    WeaponBoneCapture bone;bone.name=rig->names[n];if(rig->parents[n]!=-1)bone.parentName=rig->names[rig->parents[n]];
                    bone.native=native[n];bone.inverseBind=rig->inverseBind[n];
                    bone.hidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)!=rig->nativeHiddenLeaves.end();
                    sample.nativeLeftHandBones.push_back(std::move(bone));
                }else ++sample.leftHandBonesDropped;
            }
            if(rig->names[n]=="jntWpn_Flash"){
                bool descendant=false;for(auto at=rig->parents[n];at!=-1;at=rig->parents[at])if(unsigned(at)==rig->weaponBone){descendant=true;break;}
                if(descendant&&interaction::InverseAnimatedTransform(native[n])){sample.muzzleName=rig->names[n];sample.nativeMuzzle=native[n];}
            }
        }
        sample.weaponBonesComplete=sample.weaponBonesDropped==0;
        sample.leftHandBonesCaptured=true;sample.leftHandBonesComplete=sample.leftHandBonesDropped==0;
        std::ostringstream identity;identity<<"fnv1a64:"<<std::hex<<fingerprint;sample.skeleton=identity.str();
        weaponCapture.Observe(std::move(sample));
    }
    const auto point=[](const math::Matrix4& m){return math::Vec3{m.values[3][0],m.values[3][1],m.values[3][2]};};
    // Physical torso translation follows the tracked head, independently of
    // native collider lag and its allowed lean radius. Hands already contain
    // the same physical travel. Using only collider motion strands shoulders
    // during a step/crouch even though the controller-to-head pose is unchanged.
    // Physical heading rotates anatomy only. Hands/weapon/aim retain the shared
    // tracking base, independent of HMD yaw (no double rotation).
    const auto torso=interaction::PhysicalTorsoFrame(input.input,*body);
    if(!torso){RejectTracking(4);published.reset();return;}const auto& anatomy=*torso;
    // Stabilize the shared torso parent as well as arm branches. Otherwise the
    // native gun animation still drags clavicle/shoulder vertices between arms.
    unsigned torsoRoot=rig->parents.size();
    for(auto p=rig->parents[rig->left.shoulder];p!=-1&&torsoRoot==rig->parents.size();p=rig->parents[p])
        for(auto q=rig->parents[rig->right.shoulder];q!=-1;q=rig->parents[q])if(p==q){torsoRoot=unsigned(p);break;}
    bool weaponBelowTorso=false;
    for(auto p=std::int32_t(rig->weaponBone);p!=-1;p=rig->parents[p])if(unsigned(p)==torsoRoot)weaponBelowTorso=true;
    const bool stabilizeTorso=torsoRoot<rig->parents.size()&&torsoRoot!=0&&!weaponBelowTorso&&input.input.hands[0].gripTracked&&input.input.hands[1].gripTracked;
    const auto headBone=std::find(rig->names.begin(),rig->names.end(),"Head");
    if(headBone==rig->names.end()){RejectTracking(5);return;}
    const auto bindHead=interaction::InverseAnimatedTransform(rig->inverseBind[headBone-rig->names.begin()]);
    const auto bindLeft=interaction::InverseAnimatedTransform(rig->inverseBind[rig->left.shoulder]);
    const auto bindRight=interaction::InverseAnimatedTransform(rig->inverseBind[rig->right.shoulder]);
    if(!bindHead||!bindLeft||!bindRight){RejectTracking(6);return;}
    const auto bindFrame=interaction::BindAnatomyFrame(point(*bindHead),point(*bindLeft),point(*bindRight));
    if(!bindFrame){RejectTracking(7);return;}const auto inverseBindFrame=interaction::InverseRigid(*bindFrame);
    if(!inverseBindFrame){RejectTracking(8);return;}
    const auto bindToBody=interaction::Multiply(*inverseBindFrame,anatomy);
    const auto restAnchor=[&](interaction::ArmJoints arm)->std::optional<interaction::ArmAnchor>{
        const auto shoulder=interaction::InverseAnimatedTransform(rig->inverseBind[arm.shoulder]),elbow=interaction::InverseAnimatedTransform(rig->inverseBind[arm.elbow]);
        if(!shoulder||!elbow)return {};
        const auto s=point(interaction::Multiply(*shoulder,bindToBody)),e=point(interaction::Multiply(*elbow,bindToBody));
        return interaction::ArmAnchor{s,{e.x-s.x,e.y-s.y,e.z-s.z}};
    };
    const auto leftAnchor=restAnchor(rig->left),rightAnchor=restAnchor(rig->right);
    if(!leftAnchor||!rightAnchor){RejectTracking(9);return;}
    std::optional<math::Matrix4> restTorso;
    if(stabilizeTorso){const auto bind=interaction::InverseAnimatedTransform(rig->inverseBind[torsoRoot]);if(!bind){RejectTracking(10);return;}restTorso=interaction::Multiply(*bind,bindToBody);}
    std::optional<math::Matrix4> weaponOrientation;
    if(input.weaponProfile&&interaction::WeaponFeatureEnabled(&input.weaponProfile->core,interaction::WeaponFeature::AimAlignment)&&input.input.hands[1].gripTracked&&input.input.hands[1].aimTracked){
        weaponOrientation=interaction::TrackedAimFrame(input.input,*body);
        if(!weaponOrientation){RejectTracking(11);return;}
        // Axis conversion belongs to the verified adapter profile; every item
        // uses the same XR aim and authored attachment pipeline.
        weaponOrientation=interaction::WeaponAimFrame(input.weaponProfile->core,*weaponOrientation);
        if(!weaponOrientation){RejectTracking(11);return;}
    }
    const auto rigFingerprint=SightRigFingerprint(rig->names,rig->parents,rig->inverseBind);
    constexpr bool selectAcceptedAuthored=
#ifdef FVR_BC2_AUTHORED_GRIP_ACCEPTED_BASELINES
        true;
#else
        false;
#endif
    const auto authoredNow=ReloadNanos();
    const auto authored=BindAuthoredGrip(AuthoredGripProfiles(),gameplay::ReadCurrentSelectedMeshes(authoredNow),
        input.nativeEquipment,{input.soldier,input.weak,input.ownerGeneration,input.equipmentGeneration,
        input.input.spaceGeneration,input.input.generation,ReloadNanos(input.deadline)},rigFingerprint,
        authoredNow,input.weaponProfile!=nullptr,selectAcceptedAuthored);
    ++authoredGripStatus[unsigned(authored.status)];
    if(authored.status!=AuthoredGripStatus::NoProfile&&authored.status!=AuthoredGripStatus::PreservedBaseline&&!authored.binding){
        // A known generated entry with missing/expired identity restores the
        // current ordinary output/palette. Never reuse a previous static pose.
        published.reset();shotPublication.store({},std::memory_order_release);return;
    }
    if(authored.binding&&AuthoredAimReference(*authored.binding)&&
       input.input.hands[1].gripTracked&&input.input.hands[1].aimTracked){
        const auto aim=interaction::TrackedAimFrame(input.input,*body);
        weaponOrientation=aim?AuthoredWeaponAimFrame(*authored.binding,*aim):std::nullopt;
        if(!weaponOrientation){RejectTracking(11);published.reset();shotPublication.store({},std::memory_order_release);return;}
    }
    if(rigFingerprint!=handBindingFingerprint||input.input.worldUnitsPerMeter!=handBindingUnits){
        handBinding=BindLeftHandPose(rig->names,rig->parents,rig->inverseBind,input.input.worldUnitsPerMeter);
        rightHandBinding=BindRightHandPose(rig->names,rig->parents,rig->inverseBind,input.input.worldUnitsPerMeter);
        handBindingFingerprint=rigFingerprint;handBindingUnits=input.input.worldUnitsPerMeter;
    }
    // The anatomical controller wrist owns the item orientation when available.
    // Legacy rigs without that binding retain the admitted axis fallback.
    const auto orientationSource=rightHandBinding?WeaponOrientationSource::ControllerGrip:!weaponOrientation?WeaponOrientationSource::NativeAttachment:
        (authored.binding&&AuthoredAimReference(*authored.binding))?WeaponOrientationSource::AuthoredAxes:WeaponOrientationSource::ProfileAxes;
    auto explicitGrip=authored.binding?std::optional<interaction::AuthoredGripAttachment>{{authored.binding->profile->rightInWeapon}}:std::nullopt;
    // A stock authored gun contact is a stable item-in-hand source for baseline
    // weapons too. Do not reacquire their rotating equip/reload wrist each frame.
    if(!explicitGrip)if(const auto* contact=AuthoredContactFor(input.assetName.data(),interaction::AuthoredContactKind::GunGrip,1,rigFingerprint,*rig)){
        if(const auto local=AuthoredContactWeaponLocal(*contact,*rig,native,input.input.worldUnitsPerMeter)){
            auto meters=*local;for(unsigned n=0;n<3;++n)meters.values[3][n]/=input.input.worldUnitsPerMeter;
            explicitGrip=interaction::AuthoredGripAttachment{meters};
        }
    }
    const auto primaryMapping=rightHandBinding?std::optional(rightHandBinding->wristToGrip):std::nullopt;
    // Weapon fit identity: exact asset + rig layout, never a native pointer, so a
    // re-equip or cutscene return reuses the settled native weapon-in-wrist fit.
    // FVR_BC2_WEAPON_FIT_CACHE=0 re-samples every equip (old behaviour);
    // FVR_BC2_LEGACY_HAND_FROM_WEAPON=1 lets a profile aim frame rotate the wrist again.
    static const bool fitCache=[]{char v[4]{};return !(GetEnvironmentVariableA("FVR_BC2_WEAPON_FIT_CACHE",v,sizeof v)&&v[0]=='0');}();
    static const bool legacyHand=[]{char v[4]{};return GetEnvironmentVariableA("FVR_BC2_LEGACY_HAND_FROM_WEAPON",v,sizeof v)&&v[0]=='1';}();
    calibratedRig.SetLegacyHandFromWeapon(legacyHand);
    std::uint64_t weaponFitKey=0;
    if(fitCache&&input.assetName[0]){weaponFitKey=1469598103934665603ull;
        for(std::size_t n=0;n<input.assetName.size()&&input.assetName[n];++n)weaponFitKey=(weaponFitKey^std::uint8_t(input.assetName[n]))*1099511628211ull;
        weaponFitKey^=rigFingerprint*0x9e3779b97f4a7c15ull;if(!weaponFitKey)weaponFitKey=1;}
    const auto targets=calibratedRig.Update(owner,input.input,*body,native[rig->left.wrist],native[rig->right.wrist],weapon,{*leftAnchor,*rightAnchor},anatomy,restTorso,weaponOrientation,explicitGrip,primaryMapping,weaponFitKey);
    if(!targets){RejectTracking(11);published.reset();return;}
    // Free/mechanism wrists and raw interaction contact share one anatomical
    // controller mapping. Native support calibration stays independent.
    auto freeLeft=targets->left;math::Matrix4 leftGripWorld{};bool handRoleReady=false;
    if(handBinding&&targets->tracked[0]){
        auto relative=math::MakeRelativePose(input.input.referenceHead,input.input.hands[0].grip);
        if(relative){
            relative->position.x*=input.input.worldUnitsPerMeter;relative->position.y*=input.input.worldUnitsPerMeter;relative->position.z*=input.input.worldUnitsPerMeter;
            if(const auto view=math::MakeLhViewFromOpenXRPose(*relative))if(const auto grip=interaction::InverseRigid(*view)){
                leftGripWorld=interaction::Multiply(*grip,*body);
                freeLeft=interaction::Multiply(handBinding->wristToGrip,leftGripWorld);
                // Existing absolute wrist position includes the roomscale mapping.
                freeLeft.values[3]=targets->left.values[3];
                handRoleReady=bool(interaction::InverseAnimatedTransform(freeLeft));
            }
        }
    }
    auto rightTarget=targets->right;bool freeRightReady=false;
    if(input.freeRight&&rightHandBinding&&targets->tracked[1]&&FreeRightCurrent(*input.freeRight)){
        if(auto relative=math::MakeRelativePose(input.input.referenceHead,input.input.hands[1].grip)){
            relative->position.x*=input.input.worldUnitsPerMeter;relative->position.y*=input.input.worldUnitsPerMeter;relative->position.z*=input.input.worldUnitsPerMeter;
            if(const auto view=math::MakeLhViewFromOpenXRPose(*relative))if(const auto grip=interaction::InverseRigid(*view)){
                const auto rawGrip=interaction::Multiply(*grip,*body);
                if(const auto target=HolsterRightTarget(*input.freeRight,ReloadNanos(),*rightHandBinding,rawGrip,targets->right)){
                    rightTarget=*target;freeRightReady=true;
                }
            }
        }
        if(!freeRightReady){RejectTracking(13);published.reset();return;}
    }
    // Native aiming rotates BOTH shoulders with the gun. Keeping those origins
    // would force the stationary off-hand to chase the gun at the reach limit.
    // Use calibrated body anchors, while preserving current native limb lengths,
    // finger animation and all original source buffers.
    auto armNative=native;std::vector<interaction::BoneWrite> torsoWrites;
    if(stabilizeTorso&&targets->torso&&targets->tracked[0]&&targets->tracked[1]){
        const auto posed=interaction::RetargetRigSubtree(rig->parents,native,torsoRoot,*targets->torso);
        if(!posed){RejectTracking(12);return;}torsoWrites=*posed;
        for(const auto& write:torsoWrites)armNative[write.index]=write.transform;++torsoPoses;
    }
    const auto handsMode=gameplay::HandsMode();
    auto solved=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
        {freeLeft,targets->arms[0].poleDirection,targets->arms[0].shoulder,targets->tracked[0]},
        {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,targets->tracked[1]});
    if(!solved){RejectTracking(13);published.reset();return;}
    // IK keeps the item with its resolved wrist; floating mode retains the
    // authoritative tracked item frame, before any cosmetic contact attraction.
    const auto itemFrame=PresentedWeaponFrame(handsMode,targets->weapon,targets->right,solved->writes,rig->right.wrist);
    if(!itemFrame){RejectTracking(13);published.reset();return;}
    auto placed=*itemFrame;
    // Retain the ordinary tracked/IK output for native root/effect consumers and
    // all interaction measurements. Recoil is private palette presentation;
    // feeding it back through the native weapon getter would also rotate the
    // native shot base before its own spread/recoil layers.
    const auto interactionPlaced=placed;
    std::optional<math::Matrix4> boreInModel,boreInGrip;
    // Recover model bore axes from the same verified reference that admitted
    // the item. A hand-pose-only reference cannot grant native aiming.
    math::Matrix4 identityAim{};for(unsigned n=0;n<4;++n)identityAim.values[n][n]=1;
    std::optional<math::Matrix4> modelInAim;
    if(authored.binding&&AuthoredFlashCapability(*authored.binding))
        modelInAim=AuthoredWeaponAimFrame(*authored.binding,identityAim);
    else if(input.weaponProfile)modelInAim=interaction::WeaponAimFrame(input.weaponProfile->core,identityAim);
    if(modelInAim)boreInModel=interaction::InverseRigid(*modelInAim);
    if(boreInModel&&primaryMapping&&targets->tracked[1]&&!targets->weaponAttachmentPending&&!freeRightReady){
        if(const auto inverseWrist=interaction::InverseAnimatedTransform(targets->right))
            boreInGrip=GripBoreRelation(*boreInModel,interaction::Multiply(targets->weapon,*inverseWrist),*primaryMapping);
    }
    auto physicalBore=interaction::TrackedAimFrame(input.input,*body);
    // Recoil basis = the VISIBLE barrel. With the controller-owned wrist
    // (aaa36be) the drawn gun follows the native weapon-in-wrist fit, not the
    // separate XR aim pose, so the aim pose is only a last resort. Root->muzzle
    // (the drawn jntWpn_Flash bone) defines +Z; the
    // controller aim's up, made orthogonal, defines roll. Verified model axes
    // stay first unless they disagree with the drawn barrel by more than 60
    // degrees (a reversed axis pitches the recoil DOWN and mirrors its yaw).
    // FVR_BC2_RECOIL_MUZZLE_BORE=0 restores model axes / XR aim only.
    static const bool recoilMuzzleBore=[]{char v[4]{};return !(GetEnvironmentVariableA("FVR_BC2_RECOIL_MUZZLE_BORE",v,sizeof v)==1&&v[0]=='0');}();
    std::optional<math::Matrix4> muzzleBore;
    if(recoilMuzzleBore&&(input.recoil||input.kick)&&physicalBore&&targets->tracked[1]){
        // Only the evaluated native Flash bone: it shares the drawn mesh's
        // coordinates, so it cannot carry a sign error. Guns whose Flash sits at
        // the root (M416) fail the distance check and keep their model axes.
        std::optional<math::Matrix4> muzzleLocal;
        if(const auto inverseRoot=interaction::InverseAnimatedTransform(native[rig->weaponBone])){
            for(unsigned n=0;n<rig->names.size();++n)if(rig->names[n]=="jntWpn_Flash"){
                bool child=false;for(auto parent=rig->parents[n];parent!=-1;parent=rig->parents[parent])if(unsigned(parent)==rig->weaponBone){child=true;break;}
                if(child)muzzleLocal=interaction::Multiply(native[n],*inverseRoot);
                break;
            }
        }
        if(muzzleLocal)muzzleBore=MuzzleBoreFrame(interactionPlaced,interaction::Multiply(*muzzleLocal,interactionPlaced),
            *physicalBore,.05f*input.input.worldUnitsPerMeter);
    }
    if(boreInModel){
        auto bore=interaction::Multiply(*boreInModel,interactionPlaced);bore.values[3]={0,0,0,1};
        if(interaction::InverseRigid(bore)){
            const float agree=muzzleBore?bore.values[2][0]*muzzleBore->values[2][0]+bore.values[2][1]*muzzleBore->values[2][1]+bore.values[2][2]*muzzleBore->values[2][2]:1.f;
            if(agree<.5f){physicalBore=muzzleBore;++nativeRecoilBoreMismatch;}
            else physicalBore=bore;
        }
    }else if(muzzleBore){physicalBore=muzzleBore;++nativeRecoilMuzzleBore;}
    bool recoilApplied=false,recoilNonidentity=false;float recoilRadians=0;
    bool kickApplied=false;float kickMeters=0;
    std::optional<math::Matrix4> angularDelta;
    auto presentationDeadline=input.deadline;
    if(input.recoil&&targets->tracked[1]&&!freeRightReady&&!targets->weaponAttachmentPending){
        LARGE_INTEGER recoilNow{};QueryPerformanceCounter(&recoilNow);
        const auto& aim=physicalBore;
        const auto wrist=std::find_if(solved->writes.begin(),solved->writes.end(),[&](const auto& write){return write.index==rig->right.wrist;});
        if(NativeRecoilPoseCurrent(*input.recoil,input.soldier,input.weak,input.weapon,input.ownerGeneration,
            input.equipmentGeneration,input.nativeEquipment,input.input.spaceGeneration,recoilNow.QuadPart)&&
            aim&&wrist!=solved->writes.end()){
            // Native GunSway is a damped spring; its recovery overshoot (and wide
            // authored heading ranges) can drive the bore BELOW the held baseline.
            // The held gun keeps the native rise, yaw and recovery but never dives
            // below where the hand holds it. FVR_BC2_RECOIL_MUZZLE_UP_ONLY=0 keeps
            // the raw native descent.
            static const bool muzzleUpOnly=[]{char v[4]{};return !(GetEnvironmentVariableA("FVR_BC2_RECOIL_MUZZLE_UP_ONLY",v,sizeof v)==1&&v[0]=='0');}();
            bool rectified=false;
            if(const auto delta=NativeRecoilGripDelta(input.recoil->native.camera,*aim,wrist->transform,muzzleUpOnly,&rectified)){
                if(rectified)++nativeRecoilRectified;
                const auto recoiledRight=interaction::Multiply(wrist->transform,*delta);
                auto recoilSolved=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
                    {freeLeft,targets->arms[0].poleDirection,targets->arms[0].shoulder,targets->tracked[0]},
                    {recoiledRight,targets->arms[1].poleDirection,targets->arms[1].shoulder,true});
                // Keep the gun on the solved wrist (as the kick path does) if IK
                // cannot fully reach the recoiled target.
                const auto recoiledWeapon=recoilSolved?PresentedWeaponFrame(handsMode,interaction::Multiply(placed,*delta),
                    recoiledRight,recoilSolved->writes,rig->right.wrist):std::nullopt;
                if(recoilSolved&&recoiledWeapon&&interaction::InverseAnimatedTransform(*recoiledWeapon)){
                    rightTarget=recoiledRight;placed=*recoiledWeapon;solved=std::move(recoilSolved);recoilApplied=true;
                    angularDelta=*delta;
                    presentationDeadline=std::min(input.deadline,input.recoil->deadlineQpc);
                    if(const auto rotation=NativeRecoilRotation(input.recoil->native.camera)){
                        recoilRadians=std::acos(std::clamp((rotation->values[0][0]+rotation->values[1][1]+rotation->values[2][2]-1.f)*.5f,-1.f,1.f));
                        for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)
                            recoilNonidentity|=std::abs(rotation->values[row][column]-(row==column?1.f:0.f))>1e-6f;
                    }
                }else ++nativeRecoilRejected;
            }else ++nativeRecoilRejected;
        }else ++nativeRecoilRejected;
    }
    if(input.kick&&targets->tracked[1]&&!freeRightReady&&!targets->weaponAttachmentPending){
        LARGE_INTEGER kickNow{},kickFrequency{};QueryPerformanceCounter(&kickNow);QueryPerformanceFrequency(&kickFrequency);
        auto aim=physicalBore;
        const auto meters=EvaluateNativeRecoilKick(*input.kick,kickNow.QuadPart,kickFrequency.QuadPart);
        if(NativeRecoilKickCurrent(*input.kick,input.soldier,input.weak,input.weapon,input.ownerGeneration,
            input.equipmentGeneration,input.nativeEquipment,input.input.spaceGeneration,kickNow.QuadPart,kickFrequency.QuadPart)&&aim&&meters){
            if(angularDelta){*aim=interaction::Multiply(*aim,*angularDelta);aim->values[3]={0,0,0,1};}
            if(*meters>0){
                const auto wrist=std::find_if(solved->writes.begin(),solved->writes.end(),[&](const auto& write){return write.index==rig->right.wrist;});
                const auto delta=NativeRecoilKickDelta(*aim,*meters,input.input.worldUnitsPerMeter);
                if(delta&&wrist!=solved->writes.end()){
                    const auto kickedRight=interaction::Multiply(wrist->transform,*delta),kickedWeapon=interaction::Multiply(placed,*delta);
                    auto kickSolved=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
                        {freeLeft,targets->arms[0].poleDirection,targets->arms[0].shoulder,targets->tracked[0]},
                        {kickedRight,targets->arms[1].poleDirection,targets->arms[1].shoulder,true});
                    const auto item=kickSolved?PresentedWeaponFrame(handsMode,kickedWeapon,kickedRight,kickSolved->writes,rig->right.wrist):std::nullopt;
                    if(item){
                        rightTarget=kickedRight;placed=*item;solved=std::move(kickSolved);kickApplied=true;kickMeters=*meters;
                        presentationDeadline=std::min(presentationDeadline,NativeRecoilKickDeadline(*input.kick,kickFrequency.QuadPart));
                    }else ++nativeRecoilKickRejected;
                }else ++nativeRecoilKickRejected;
            }
        }else ++nativeRecoilKickRejected;
    }
    // Contact and carried ammunition use the SAME anatomical controller wrist
    // as the free hand. targets->left retains the old native support calibration;
    // switching to it on pickup can invert the wrist despite unchanged tracking.
    // No support attachment, guided insertion or solved wrist feeds contact.
    const auto reloadContact=handRoleReady?
        BuildReloadRawContact(input.reload,*rig,input.assetName.data(),freeLeft,interactionPlaced,input.input.worldUnitsPerMeter,ReloadNanos(),&*body):ReloadRawContact{};
    const auto magazineContact=handRoleReady?
        BuildMagazineRawContact(input.magazine,*rig,input.assetName.data(),freeLeft,interactionPlaced,input.input.worldUnitsPerMeter,ReloadNanos(),&*body):MagazineRawContact{};
    auto leftTarget=freeLeft;bool supportAttached=false;
    const auto inverseSupportItem=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
    if(!inverseSupportItem){RejectTracking(13);return;}
    const AuthoredSupportProfile* measuredSupport=nullptr;
#ifdef FVR_BC2_AUTHORED_SUPPORT_HEADER
    const auto supportSnapshot=gameplay::ReadCurrentSelectedMeshes(authoredNow);
    if(supportSnapshot)measuredSupport=BindAuthoredSupport(generated::AuthoredSupports,*supportSnapshot,input.nativeEquipment,
        {input.soldier,input.weak,input.ownerGeneration,input.equipmentGeneration,input.input.spaceGeneration,input.input.generation,ReloadNanos(input.deadline)},rigFingerprint,authoredNow);
#endif
    // Authored support contact (private runtime library, Bc2AuthoredContacts.h):
    // the native HandsIkPose wrist-in-part and 15 fingers for this exact asset and
    // skeleton. Compiled reviewed profiles keep priority for the wrist; the
    // current native wrist remains the fallback when no contact exists.
    const auto supportContact=AuthoredContactFor(input.assetName.data(),interaction::AuthoredContactKind::Support,0,rigFingerprint,*rig);
    std::optional<math::Matrix4> librarySupportLocal;
    if(supportContact&&!authored.binding&&!measuredSupport)
        librarySupportLocal=AuthoredContactWeaponLocal(*supportContact,*rig,native,input.input.worldUnitsPerMeter);
    const auto authoredSupportLocal=authored.binding?AuthoredGripWorldUnits(authored.binding->profile->leftInWeapon,input.input.worldUnitsPerMeter):
        measuredSupport?AuthoredGripWorldUnits(measuredSupport->leftInWeapon,input.input.worldUnitsPerMeter):
        librarySupportLocal?*librarySupportLocal:interaction::Multiply(native[rig->left.wrist],*inverseSupportItem);
    const auto fixedSupportLocal=supportAttachment.Update(
        {owner.actor,owner.generation,owner.equipped,owner.skeleton,input.input.spaceGeneration},
        input.supportHolding&&targets->tracked[0]&&targets->tracked[1]?input.supportToken:0,authoredSupportLocal);
    // Attach only the published hand while explicit grip is held. Contact and
    // steering below still use the independent raw target, so pulling away or
    // releasing cannot leave a self-sustaining grab.
    // Controller progress scrubs the original BoltAction trajectory. Wrist,
    // fingers and moving parts share its translation/quaternion interpolation.
    const NativeActionPoseProfile* pumpProfile=nullptr;
    const NativeActionPoseFrame* pumpPose=nullptr;
    std::optional<EvaluatedNativeActionPose> pumpEvaluation;NativeActionPoseFrame pumpFrame;
    std::array<interaction::BoneWrite,3> pumpWrites{};unsigned pumpWriteCount=0;
    if(input.pump&&input.pump->shot&&targets->tracked[1]){
        static const NativeActionPoseProfile* cachedPump=nullptr;
        static unsigned cachedWeapon=0;static std::uint64_t cachedEquipment=0,cachedRig=0;
        if(cachedWeapon!=input.weapon||cachedEquipment!=input.equipmentGeneration||cachedRig!=rigFingerprint){
            cachedWeapon=input.weapon;cachedEquipment=input.equipmentGeneration;cachedRig=rigFingerprint;cachedPump=nullptr;
            for(const auto& profile:NativeActionPoseProfiles())
                if(profile.assetName==input.assetName.data()&&profile.rigFingerprint==rigFingerprint&&
                   profile.action==NativeActionPoseKind::Pump&&ValidNativeActionPose(profile)&&
                   BindBc2ReloadPresentation(*rig,input.assetName.data(),profile.meshPath)){
                    if(cachedPump){cachedPump=nullptr;break;}cachedPump=&profile;
                }
        }
        pumpProfile=cachedPump;
        const float fraction=std::clamp(input.pump->fraction,0.f,1.f);
        if(pumpProfile){
            pumpEvaluation=EvaluateNativeActionPose(*pumpProfile,fraction,input.pump->returning);
            if(pumpEvaluation){pumpFrame=pumpEvaluation->View();pumpPose=&pumpFrame;}
            if(pumpPose)if(const auto part=NativeActionPartWrites(*pumpProfile,*pumpPose,*rig,
                interaction::reload_insertion_detail::Identity(),input.input.worldUnitsPerMeter)){
                if(part->size()<=pumpWrites.size())for(const auto& write:*part)pumpWrites[pumpWriteCount++]=write;
                else pumpPose=nullptr;
            }else pumpPose=nullptr;
        }
    }
    // A completed pump keeps its original closed PART samples while the
    // native firing transaction settles. That visual must not take the hand
    // back from a new support grip, reload, or the free controller.
    const bool pumpOwnsHand=pumpPose&&input.pump->handOwned&&PumpNativeHandEnabled();
    if(input.supportHolding&&fixedSupportLocal&&targets->tracked[0]&&targets->tracked[1]){
        const auto inverseWeapon=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
        if(!inverseWeapon){RejectTracking(13);return;}
        if(pumpOwnsHand){
            const auto sampled=NativeActionWrist(*pumpPose,placed,input.input.worldUnitsPerMeter);
            if(!sampled){RejectTracking(13);return;}leftTarget=*sampled;
        }else{
            const auto magnet=supportMagnet.Update(ReloadNanos(),freeLeft,*fixedSupportLocal,placed,
                gameplay::InteractionZoneTable()[interaction::InteractionZoneKind::Support],input.supportToken);
            leftTarget=magnet.target;if(magnet.blending)++supportMagnetBlendPoses;
        }
        solved=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
            {leftTarget,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
            {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,true});
        if(!solved){RejectTracking(13);published.reset();return;}
        supportAttached=true;
    }else if(supportMagnet.Active()){
        // Released: only the last visual correction decays, in the tracked
        // hand's frame. Ownership is already gone; nothing follows the gun.
        const auto magnet=supportMagnet.Update(ReloadNanos(),freeLeft,std::nullopt,placed,
            gameplay::InteractionZoneTable()[interaction::InteractionZoneKind::Support]);
        if(magnet.weight>0&&targets->tracked[0]&&targets->tracked[1]){
            if(auto returning=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
                {magnet.target,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
                {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,true})){
                solved=std::move(returning);leftTarget=magnet.target;++supportMagnetReturnPoses;}
        }
    }
    // The active pump stroke owns the hand without a support claim (grip
    // closed in the pump zone): the left wrist
    // and fingers follow the same original BoltAction sample as the fore-end.
    // The right hand stays on its controller target. FVR_BC2_PUMP_NATIVE_HAND=0
    // leaves the hand free, as before.
    bool pumpHandAttached=false;
    if(!supportAttached&&pumpOwnsHand&&(!input.reload.preview||!input.reload.preview->actionHandOwned)&&
       targets->tracked[0]&&targets->tracked[1]&&!targets->weaponAttachmentPending){
        const auto sampled=NativeActionWrist(*pumpPose,placed,input.input.worldUnitsPerMeter);
        auto attached=sampled?SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
            {*sampled,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
            {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,true}):std::nullopt;
        if(attached){solved=std::move(attached);leftTarget=*sampled;pumpHandAttached=true;++pumpHandPoses;}
        else ++pumpHandRejects;
    }
    // The first-person gun geometry shares this rig's skin palette; changing
    // only the attachment getter moves effects/root consumers, not those verts.
    auto writes=torsoWrites;
    const auto merge=[&](const auto& added){for(const auto& write:added){
        const auto found=std::find_if(writes.begin(),writes.end(),[&](const auto& prior){return prior.index==write.index;});
        if(found==writes.end())writes.push_back(write);else *found=write;
    }};
    merge(solved->writes);
    auto handRole=interaction::HandPoseRole::Free;
    const auto poseHand=[&](interaction::HandPoseRole role,const auto& armWrites)->bool{
        if(!handRoleReady)return false;
        const auto wrist=std::find_if(armWrites.begin(),armWrites.end(),[&](const auto& w){return w.index==rig->left.wrist;});
        if(wrist==armWrites.end())return false;
        const auto desired=interaction::ApplyHandTouch(LeftHandTargets(role,input.input.hands[0].squeeze,input.input.hands[0].trigger),input.input.hands[0]);
        const auto hand=interaction::GenerateHandPose(rig->parents,handBinding->referenceWorld,native,handBinding->pose,wrist->transform,desired);
        if(!hand){++handPoseRejected;return false;}
        merge(hand->writes);return true;
    };
    bool handPosed=false;
    if(supportAttached){handRole=interaction::HandPoseRole::WeaponSupport;handPosed=handRoleReady;
        if(librarySupportLocal&&input.supportHolding)++authoredSupportWrists;
        // Authored support fingers on the final solved wrist (native fingers otherwise).
        if(pumpOwnsHand){
            const auto wrist=std::find_if(writes.begin(),writes.end(),[&](const auto& w){return w.index==rig->left.wrist;});
            if(wrist!=writes.end()&&ApplyNativeActionFingers(*pumpPose,*rig,writes,wrist->transform,input.input.worldUnitsPerMeter))
                ++authoredSupportFingers;else ++authoredFingerFailures;
        }else if(supportContact&&supportContact->fingerCount){
            const auto wrist=std::find_if(writes.begin(),writes.end(),[&](const auto& w){return w.index==rig->left.wrist;});
            if(wrist!=writes.end()&&interaction::ApplyAuthoredFingers(rig->parents,rig->names,writes,*supportContact,wrist->transform,input.input.worldUnitsPerMeter))
                ++authoredSupportFingers;else ++authoredFingerFailures;}}
    else if(pumpHandAttached){handRole=interaction::HandPoseRole::MechanismGrip;
        const auto wrist=std::find_if(writes.begin(),writes.end(),[&](const auto& w){return w.index==rig->left.wrist;});
        handPosed=wrist!=writes.end()&&ApplyNativeActionFingers(*pumpPose,*rig,writes,wrist->transform,input.input.worldUnitsPerMeter);
        if(!handPosed){++pumpHandRejects;if(handRoleReady)handPosed=poseHand(handRole,solved->writes);}}
    else if(handRoleReady)handPosed=poseHand(handRole,solved->writes);
    if(targets->tracked[1]){
        const auto weaponWrites=interaction::RetargetRigSubtree(rig->parents,native,rig->weaponBone,placed,rig->nativeHiddenLeaves);
        if(!weaponWrites){RejectTracking(14);published.reset();return;}
        merge(*weaponWrites);
    }
    std::optional<TriggerEvidence> triggerEvidence;
    const auto poseTrigger=[&]()->bool{
        if(!targets->tracked[1]||!rightHandBinding)return true;
        auto current=native;for(const auto& w:writes)current[w.index]=w.transform;
        const auto posed=PoseRightTrigger(rig->parents,current,*rightHandBinding,input.input.hands[1].trigger);
        if(!posed){++triggerFailures;return false;}
        auto after=current;for(const auto& w:posed->writes)after[w.index]=w.transform;
        TriggerEvidence evidence;evidence.generation=input.input.generation;evidence.weapon=input.weapon;evidence.trigger=input.input.hands[1].trigger;
        evidence.wristPreserved=after[rig->right.wrist].values==current[rig->right.wrist].values;
        evidence.otherBranchesPreserved=true;
        const auto& index=rightHandBinding->pose.fingers[std::size_t(interaction::HandFinger::Index)];
        for(unsigned n=0;n<rig->parents.size();++n){
            bool below=false;for(auto atBone=std::int32_t(n);atBone!=-1;atBone=rig->parents[atBone])if(unsigned(atBone)==index.joints[0].index){below=true;break;}
            if(!below&&after[n].values!=current[n].values)evidence.otherBranchesPreserved=false;
        }
        for(unsigned j=0;j<3;++j){
            const auto n=index.joints[j].index,parent=unsigned(rig->parents[n]);evidence.indices[j]=n;
            const auto a=interaction::InverseAnimatedTransform(current[parent]),b=interaction::InverseAnimatedTransform(after[parent]);
            if(!a||!b){++triggerFailures;return false;}
            evidence.before[j]=interaction::Multiply(current[n],*a);evidence.after[j]=interaction::Multiply(after[n],*b);
        }
        if(!evidence.wristPreserved||!evidence.otherBranchesPreserved){++triggerFailures;return false;}
        triggerEvidence=evidence;merge(posed->writes);return true;
    };
    if(freeRightReady){
        const auto wrist=std::find_if(solved->writes.begin(),solved->writes.end(),[&](const auto& w){return w.index==rig->right.wrist;});
        if(wrist==solved->writes.end()){RejectTracking(15);return;}
        auto current=native;for(const auto& write:writes)current[write.index]=write.transform;
        const auto freePose=PoseHolsteredRight(*input.freeRight,ReloadNanos(),*rightHandBinding,rig->parents,current,wrist->transform,input.input.hands[1]);
        if(!freePose){RejectTracking(15);return;}merge(freePose->writes);++freeRightPoses;
    }else if(!poseTrigger()){RejectTracking(15);return;}
    auto plan=BuildRigPosePlan(*rig,writes);if(!plan){RejectTracking(15);published.reset();return;}
    std::vector<std::array<std::byte,64>> previewBase;bool sightAttached=false;
    std::optional<SightPreviewRecord> sightEvidence;
    const auto& preview=input.sightPreview;
    if(handRoleReady&&preview.valid&&preview.token&&preview.weapon==input.weapon&&preview.generation<=input.input.generation&&
       preview.generation&&Fresh(preview.deadline)&&!input.supportHolding&&
       preview.physicalItem&&preview.physicalItem==input.sightPhysicalItem&&
       preview.phase!=interaction::SightFlipPhase::Idle&&
       (!targets->weaponAttachmentPending||preview.phase==interaction::SightFlipPhase::AwaitingAcknowledgement||preview.phase==interaction::SightFlipPhase::Latched)&&
       targets->tracked[0]&&targets->tracked[1]&&input.input.hands[0].squeeze>.35f&&
       input.sightAdapter&&input.sightAdapter->nativeSightAccepted&&preview.adapter==input.sightAdapter&&
       rigFingerprint==0xa7f219a1426216abull){
        const auto rear=std::find(rig->names.begin(),rig->names.end(),input.sightAdapter->rear);
        const auto front=std::find(rig->names.begin(),rig->names.end(),input.sightAdapter->front);
        const auto visibleLeaf=[&](auto it){
            if(it==rig->names.end())return false;const auto n=unsigned(it-rig->names.begin());
            return rig->parents[n]==std::int32_t(rig->weaponBone)&&
                std::find(rig->parents.begin(),rig->parents.end(),std::int32_t(n))==rig->parents.end()&&
                std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)==rig->nativeHiddenLeaves.end();
        };
        const auto world=[&](math::Matrix4 local){for(unsigned n=0;n<3;++n)local.values[3][n]*=input.input.worldUnitsPerMeter;return interaction::Multiply(local,placed);};
        const auto desiredHand=world(preview.wrist),desiredRear=world(preview.rear),desiredFront=world(preview.front);
        const bool subtree=input.sightAdapter->assembly==SightAssemblyKind::Subtree;
        const auto attachmentWrites=BuildSightAssemblyWrites(*input.sightAdapter,rig->names,rig->parents,native,rig->nativeHiddenLeaves,desiredRear,subtree?std::nullopt:std::optional(desiredFront));
        if(attachmentWrites&&(subtree||(visibleLeaf(rear)&&visibleLeaf(front)))&&interaction::InverseAnimatedTransform(desiredHand)){
            const auto previewSolved=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
                {desiredHand,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
                {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,true});
            if(previewSolved){
                const auto baseWrites=writes;
                merge(previewSolved->writes);
                const bool mechanismPosed=poseHand(interaction::HandPoseRole::MechanismGrip,previewSolved->writes)&&poseTrigger();
                merge(*attachmentWrites);
                const auto previewPlan=mechanismPosed?BuildRigPosePlan(*rig,writes):std::optional<RigPosePlan>{};
                if(previewPlan){
                    previewBase=rig->nativeEvaluated;for(const auto& edit:plan->edits)previewBase[edit.index]=edit.after;
                    plan=previewPlan;solved=previewSolved;leftTarget=desiredHand;sightAttached=true;
                    handRole=interaction::HandPoseRole::MechanismGrip;handPosed=true;
                    SightPreviewRecord record;record.preview=preview;record.inputGeneration=input.input.generation;
                    record.owner=input.ownerGeneration;record.space=input.input.spaceGeneration;
                    const auto inverseNative=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
                    const auto inversePlaced=interaction::InverseAnimatedTransform(placed);
                    if(inverseNative&&inversePlaced){
                        record.nativeRear=interaction::Multiply(native[rear-rig->names.begin()],*inverseNative);
                        if(!subtree)record.nativeFront=interaction::Multiply(native[front-rig->names.begin()],*inverseNative);
                        record.rawHand=interaction::Multiply(freeLeft,*inversePlaced);
                        for(const auto& write:solved->writes)if(write.index==rig->left.wrist)record.resolvedHand=interaction::Multiply(write.transform,*inversePlaced);
                        for(auto matrix:{&record.nativeRear,&record.nativeFront,&record.rawHand,&record.resolvedHand})
                            for(unsigned n=0;n<3;++n)matrix->values[3][n]/=input.input.worldUnitsPerMeter;
                        const auto p=preview.palmPointWrist;const auto& m=record.resolvedHand.values;
                        const math::Vec3 palm{p.x*m[0][0]+p.y*m[1][0]+p.z*m[2][0]+m[3][0],p.x*m[0][1]+p.y*m[1][1]+p.z*m[2][1]+m[3][1],p.x*m[0][2]+p.y*m[1][2]+p.z*m[2][2]+m[3][2]};
                        record.palmError=std::hypot(palm.x-preview.graspPoint.x,palm.y-preview.graspPoint.y,palm.z-preview.graspPoint.z);
                        record.clamped=solved->targetError[0]>.001f*input.input.worldUnitsPerMeter;sightEvidence=record;
                    }
                }else{writes=baseWrites;++sightPreviewRejected;}
            }else ++sightPreviewRejected;
        }else ++sightPreviewRejected;
    }
    std::vector<std::array<std::byte,64>> reloadBase;bool reloadAttached=false;std::int64_t reloadDeadline=0;
    bool sampledReloadHand=false,sampledMagazineHand=false,sampledRackHand=false;
    std::optional<std::uint32_t> reloadVisibleShell;
    if(input.reload.preview&&!sightAttached&&(!supportAttached||!input.reload.preview->actionHandOwned)&&!targets->weaponAttachmentPending){
        Bc2ReloadPaletteSample sample;sample.enabled=true;sample.baseWrites=writes;sample.armNativeWorld=armNative;
        sample.leftPoleDirection=targets->arms[0].poleDirection;sample.leftShoulder=targets->arms[0].shoulder;
        sample.presentation.unitsPerMetre=input.input.worldUnitsPerMeter;sample.presentation.placedWeaponWorld=placed;
        sample.presentation.nativeActions=NativeActionPoseProfiles();
        const auto reloadNow=ReloadNanos();
        auto reloadPlan=BuildReloadPreviewPalette(*input.reload.preview,input.reload,reloadContact,*rig,sample,input.assetName.data(),reloadNow);
        const auto reloadTargets=ResolveReloadPreviewTargets(*input.reload.preview,input.reload,reloadContact,reloadNow);
        if(reloadPlan.palette&&reloadTargets){
            reloadBase=rig->nativeEvaluated;for(const auto& edit:plan->edits)reloadBase[edit.index]=edit.after;
            writes=std::move(reloadPlan.writes);plan=std::move(reloadPlan.palette);reloadAttached=true;reloadDeadline=reloadTargets->deadlineNs;
            reloadVisibleShell=reloadPlan.ownedShellVisibility;
            sampledReloadHand=reloadPlan.sampledNativeHand;
            if(input.reload.preview->actionHandOwned){
                solved->writes=writes;solved->targetError[0]=reloadPlan.leftWristError;solved->reachClamped[0]=false;
                for(const auto& write:writes)if(write.index==rig->left.wrist){leftTarget=write.transform;break;}
                handPosed=true;handRole=interaction::HandPoseRole::MechanismGrip;
            }
            ++reloadPreviewPoses;
            ++reloadPhasePoses[unsigned(input.reload.preview->phase)];
        }else {++reloadPreviewRejected;
            const auto reason=unsigned(reloadPlan.presentationReason);
            if(reason<reloadPresentationRejections.size())++reloadPresentationRejections[reason];
        }
    }
    std::vector<std::array<std::byte,64>> magazineBase;
    std::optional<MagazinePresentationBinding> magazineBinding;
    bool magazineAttached=false,magazineHidden=false;
    // M3 (AS2 pickup curve): remember the last offered pouch contact, so a
    // replacement taken from it can start there. The offer stops while the
    // grab is pending (run on 0dbdcf1: pickup_eases 0), so the anchor is
    // re-evaluated against this frame's head at pickup, not a world point
    // that had to be under 300 ms old.
    if(input.bodyMagazine){lastPouch=*input.bodyMagazine;lastPouchNs=ReloadNanos();}
    if(input.magazine.target&&!reloadAttached&&!sightAttached&&!targets->weaponAttachmentPending){
        ++magazinePlanAttempts;
        if(const auto binding=input.magazine.family.binding.profile?BindMagazinePresentation(*rig,*input.magazine.family.binding.profile):std::nullopt){
            auto presentation=BuildMagazinePresentation(*rig,*binding,input.magazine,placed,input.input.worldUnitsPerMeter,ReloadNanos(),NativeActionPoseProfiles());
            if(input.magazine.actionConstrained){easeStartNs=0;easeItem=input.magazine.target->item;}
            // M3: a new replacement eases from the pouch point into the grasp over
            // 0.05 s + 0.10 s per metre (smoothstep), drawn only: the magazine bone
            // and the bones it carries are offset, the hand stays on the real hand.
            if(const auto& v=*input.magazine.target;v.role==interaction::MagazinePropRole::Replacement&&!input.magazine.actionConstrained&&presentation.binding&&!presentation.writes.empty()){
                const auto now=ReloadNanos();const float units=input.input.worldUnitsPerMeter;
                const auto& item=presentation.writes.front().transform.values[3];
                if(!(v.item==easeItem)){easeItem=v.item;easeStartNs=0;
                    // Misses: 0 no pouch contact, 1 last offer older than 2 s or
                    // another tracking space, 2 anchor unavailable, 3 over 1.5 m.
                    // The nearest of the consumer's pouch contacts, evaluated
                    // against this frame's head; else the last body-ammo offer.
                    std::optional<math::Matrix4> anchor;
                    const auto at=[&](const interaction::AmmoSupplyContact& c,interaction::SupplyAnchorFrame frame){
                        interaction::AmmoSupplyVisualSample s;s.contact=c;s.frame=frame;s.input.sequence=input.input.generation;s.input.owner.space=input.input.spaceGeneration;
                        return input.eyeBaseValid?interaction::AmmoSupplyAnchorWorld(input.input,input.eyeBase,s):std::nullopt;};
                    double best=1e30;
                    for(const auto& c:input.pouchContacts)if(c)if(const auto a=at(*c,input.pouchFrame)){
                        const double d=std::hypot(item[0]-a->values[3][0],item[1]-a->values[3][1],item[2]-a->values[3][2]);if(d<best){best=d;anchor=a;}}
                    if(!anchor){
                        if(!lastPouch)++pickupEaseMisses[0];
                        else if(now-lastPouchNs>2000000000||lastPouch->input.owner.space!=input.input.spaceGeneration)++pickupEaseMisses[1];
                        else {anchor=at(lastPouch->contact,lastPouch->frame);if(!anchor)++pickupEaseMisses[2];}}
                    if(anchor&&std::isfinite(units)&&units>0){pouchWorld={anchor->values[3][0],anchor->values[3][1],anchor->values[3][2]};pouchWorldNs=now;
                        const double d=std::hypot(item[0]-pouchWorld[0],item[1]-pouchWorld[1],item[2]-pouchWorld[2])/units;
                        if(d<1.5){easeFrom=pouchWorld;easeStartNs=now;easeLengthNs=50000000+std::int64_t(d*100000000);++pickupEases;}else ++pickupEaseMisses[3];}}
                if(easeStartNs&&now-easeStartNs<easeLengthNs){float t=float(double(now-easeStartNs)/double(easeLengthNs));t=t*t*(3-2*t);
                    const std::array<float,3> delta{(easeFrom[0]-item[0])*(1-t),(easeFrom[1]-item[1])*(1-t),(easeFrom[2]-item[2])*(1-t)};
                    for(auto& w:presentation.writes){const bool moved=w.index==binding->magazine||
                        std::find(binding->carried.begin(),binding->carried.begin()+binding->carriedCount,w.index)!=binding->carried.begin()+binding->carriedCount;
                        if(moved)for(unsigned k=0;k<3;++k)w.transform.values[3][k]+=delta[k];}}
                else easeStartNs=0;}
            if(presentation.binding){
                const auto baseWrites=writes;bool valid=true;
                auto magazineSolved=solved;
                if(presentation.wristTarget&&!supportAttached){
                    magazineSolved=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
                        {*presentation.wristTarget,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
                        {{},{},{},false});
                    valid=magazineSolved&&!magazineSolved->reachClamped[0]&&magazineSolved->targetError[0]<=.0001f*input.input.worldUnitsPerMeter;
                    if(!valid){++magazineReachRejects;if(magazineReachCount<magazineReachRecords.size())
                        magazineReachRecords[magazineReachCount++]={input.input.generation,unsigned(input.magazine.target->role),magazineSolved?magazineSolved->targetError[0]:-1.f,bool(magazineSolved),magazineSolved&&magazineSolved->reachClamped[0]};}
                    if(valid)merge(magazineSolved->writes);
                }
                if(valid){
                    merge(MagazinePresentationWrites(presentation,supportAttached));
                    if(const auto candidate=BuildRigPosePlan(*rig,writes)){
                        magazineBase=rig->nativeEvaluated;
                        for(const auto& e:plan->edits)magazineBase[e.index]=e.after;
                        plan=candidate;magazineBinding=binding;magazineAttached=true;magazineHidden=presentation.hideMagazine;
                        sampledMagazineHand=presentation.sampledNativeHand&&!supportAttached;
                        if(presentation.wristTarget&&!supportAttached){solved->writes=writes;solved->targetError[0]=magazineSolved->targetError[0];solved->reachClamped[0]=false;leftTarget=*presentation.wristTarget;
                            handPosed=true;handRole=interaction::HandPoseRole::MechanismGrip;}
                        ++magazinePoses;
                    }else {valid=false;++magazinePoseRejects;}
                }
                if(!valid)writes=baseWrites;
            }else {++magazinePlanRejects;
                if(std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),binding->magazine)!=rig->nativeHiddenLeaves.end())++magazineHiddenRejects;
                if(!MagazineTargetFresh(input.magazine,ReloadNanos()))++magazineFreshRejects;}
        }else ++magazineBindingRejects;
    }
    std::vector<std::array<std::byte,64>> nativeRackBase;bool nativeRackAttached=false;
    // C2: the charging handle (and its carrier) follow the rack stroke and
    // spring back over ~60 ms on release; native pose while no rack is owed.
    const auto* mechanismEquipment=input.magazine.family.binding.profile;
    if(!input.nativeRackRequested){
    static const MagazineMechanismProfile* shownMechanism=nullptr;
    static unsigned shownMechanismWeapon=0;static std::uint64_t shownMechanismEquipment=0;
    static float lastRackProgress=0;static bool rackReturning=false;
    const auto* selectedMechanism=mechanismEquipment?mechanismEquipment->mechanism:nullptr;
    // Generated samples are immutable: full matrix validation belongs to
    // selection changes, not every rendered pose.
    static const NativeActionPoseProfile* cachedRackAction=nullptr;
    static const MagazineEquipmentProfile* cachedRackEquipment=nullptr;
    static std::uint64_t cachedRackRig=0;static std::string cachedRackAsset;
    // Same-receiver donor (hand only): its handle part index, -1 for an exact profile.
    static int cachedRackDonorPart=-1;static std::string_view cachedRackDonorHandle;
    if(cachedRackEquipment!=mechanismEquipment||cachedRackRig!=rigFingerprint||cachedRackAsset!=input.assetName.data()){
        cachedRackEquipment=mechanismEquipment;cachedRackRig=rigFingerprint;cachedRackAsset=input.assetName.data();
        cachedRackAction=mechanismEquipment&&mechanismEquipment->geometry?
            FindNativeActionPose(NativeActionPoseProfiles(),cachedRackAsset,mechanismEquipment->geometry->mesh,
                rigFingerprint,NativeActionPoseKind::Rack):nullptr;
        cachedRackDonorPart=-1;cachedRackDonorHandle={};
        if(!cachedRackAction&&mechanismEquipment&&mechanismEquipment->chargingHandle&&RackFamilyGraspEnabled())
            if(const auto* donor=FindNativeRackGraspDonor(cachedRackAsset)){
                const auto* profile=FindNativeActionPose(NativeActionPoseProfiles(),donor->donorAsset,donor->donorMesh,rigFingerprint,NativeActionPoseKind::Rack);
                const int part=profile?NativeRackGraspDonorPart(*profile,*donor):-1;
                if(part>=0){cachedRackAction=profile;cachedRackDonorPart=part;cachedRackDonorHandle=donor->handleBone;++rackFamilyGraspBindings;}
            }
    }
    const auto* rackAction=MagazineTrackingFresh(input.magazine,ReloadNanos())?cachedRackAction:nullptr;
    const bool rackDonor=cachedRackDonorPart>=0;
    const NativeActionPoseFrame* rackActionSample=nullptr;
    std::optional<EvaluatedNativeActionPose> rackEvaluation;NativeActionPoseFrame rackFrame;
    if(shownMechanism!=selectedMechanism||shownMechanismWeapon!=input.weapon||shownMechanismEquipment!=input.equipmentGeneration){
        handleShown=0;handleShownNs=0;lastRackProgress=0;rackReturning=false;
        shownMechanism=selectedMechanism;shownMechanismWeapon=input.weapon;shownMechanismEquipment=input.equipmentGeneration;
    }
    const float rackProgress=std::clamp(input.handleTravel,0.f,1.f);
    if(rackProgress<lastRackProgress)rackReturning=true;else if(rackProgress>lastRackProgress)rackReturning=false;
    lastRackProgress=rackProgress;
    const auto rackSample=[&](float fraction)->const NativeActionPoseFrame*{
        rackEvaluation=rackAction?EvaluateNativeActionPose(*rackAction,fraction,rackReturning):std::nullopt;
        if(!rackEvaluation)return nullptr;rackFrame=rackEvaluation->View();return &rackFrame;
    };
    if(const auto* mechanism=mechanismEquipment?mechanismEquipment->mechanism:nullptr;
       mechanism&&mechanism->rigFingerprint==rigFingerprint&&(input.handleTravel>=0||handleShown>0)){
        const auto now=ReloadNanos();const float dt=handleShownNs?float(double(now-handleShownNs)*1e-9):0.f;handleShownNs=now;
        const float target=std::clamp(input.handleTravel,0.f,1.f);
        handleShown=target>=handleShown?target:std::max(target,handleShown-dt/.06f);
        if(handleShown>0||input.handleOwned){
            rackActionSample=rackSample(handleShown);
            const auto part=rackActionSample&&!rackDonor?NativeActionPartWrites(*rackAction,*rackActionSample,*rig,placed,input.input.worldUnitsPerMeter):
                MagazineMechanismWrites(*mechanism,*rig,placed,handleShown,input.input.worldUnitsPerMeter);
            if(part){const auto baseWrites=writes;merge(*part);
                if(const auto candidate=BuildRigPosePlan(*rig,writes)){plan=candidate;++handlePoses;}else{writes=baseWrites;++handleRejects;}}
            else ++handleRejects;
        }
    }else if(const auto* pistol=FindPistolMechanism(input.assetName.data());pistol&&input.handleTravel>=0&&
       pistol->rigFingerprint==SightRigFingerprint(rig->names,rig->parents,rig->inverseBind)){
        // Absolute closed-part transforms avoid adding a second slide offset
        // on top of the engine's already-open empty animation. When in battery
        // and untouched, leave authored firing/idle motion entirely native.
        const float travel=std::clamp(input.handleTravel,0.f,1.f);
        if(travel>0||input.handleOwned){std::vector<interaction::BoneWrite> part;bool complete=true;
            for(const auto& p:pistol->parts){const auto at=std::find(rig->names.begin(),rig->names.end(),p.bone);
                if(at==rig->names.end()){complete=false;break;}
                auto local=p.closedInWeaponMeters;
                for(unsigned i=0;i<3;++i)local.values[3][i]=(local.values[3][i]+travel*pistol->handle.strokeMeters*pistol->handle.pullDirection[i])*input.input.worldUnitsPerMeter;
                part.push_back({unsigned(at-rig->names.begin()),interaction::Multiply(local,placed)});
            }
            if(complete&&!part.empty()){const auto baseWrites=writes;merge(part);
                if(const auto candidate=BuildRigPosePlan(*rig,writes)){plan=candidate;++handlePoses;}else{writes=baseWrites;++handleRejects;}}
        }
        handleShown=0;handleShownNs=0;
    }else if(const auto* parts=(input.handleTravel>=0||handleShown>0)?FindChargingHandleParts(input.assetName.data()):nullptr){
        const auto now=ReloadNanos();const float dt=handleShownNs?float(double(now-handleShownNs)*1e-9):0.f;handleShownNs=now;
        const float target=std::clamp(input.handleTravel,0.f,1.f);
        handleShown=target>=handleShown?target:std::max(target,handleShown-dt/.06f);
        if(handleShown>0){std::vector<interaction::BoneWrite> part;
            for(unsigned n=0;n<parts->count;++n){const auto at=std::find(rig->names.begin(),rig->names.end(),parts->bones[n]);
                if(at==rig->names.end())continue;const auto bone=unsigned(at-rig->names.begin());
                auto local=interaction::Multiply(native[bone],*inverseSupportItem);
                local.values[3][2]+=handleShown*parts->strokeMeters*input.input.worldUnitsPerMeter;
                part.push_back({bone,interaction::Multiply(local,placed)});}
            if(!part.empty()){const auto baseWrites=writes;merge(part);
                if(const auto candidate=BuildRigPosePlan(*rig,writes)){plan=candidate;++handlePoses;}else{writes=baseWrites;++handleRejects;}}}
    }else{handleShown=0;handleShownNs=0;}
    if(!rackActionSample)rackActionSample=rackSample(rackProgress);
    // Only an actual held mechanism owns this visual hand. Raw contact above
    // remains anatomical tracking; neither this pin nor its fingers are contact
    // evidence. An open pistol action alone must never capture the hand.
    if(handRoleReady&&input.handleHandOwned&&input.input.hands[0].squeeze>.35f&&
       targets->tracked[0]&&targets->tracked[1]&&!targets->weaponAttachmentPending&&
       !supportAttached&&!pumpHandAttached&&!sightAttached&&!reloadAttached&&!magazineAttached&&
       MagazineTrackingFresh(input.magazine,ReloadNanos())&&mechanismEquipment&&mechanismEquipment->chargingHandle){
        const auto& handle=*mechanismEquipment->chargingHandle;
        const auto* contact=AuthoredContactFor(input.assetName.data(),interaction::AuthoredContactKind::Handle,0,rigFingerprint,*rig);
        const auto contactMoves=[&]{
            if(!contact)return false;
            if(selectedMechanism)for(const auto& p:selectedMechanism->parts)if(p.bone==contact->part)return true;
            if(const auto* pistol=FindPistolMechanism(input.assetName.data()))for(const auto& p:pistol->parts)if(p.bone==contact->part)return true;
            if(const auto* parts=FindChargingHandleParts(input.assetName.data()))for(unsigned n=0;n<parts->count;++n)if(parts->bones[n]==contact->part)return true;
            return false;
        };
        if(!contactMoves())contact=nullptr;
        auto desired=freeLeft;
        std::optional<math::Matrix4> sampledWrist;
        if(rackActionSample&&!rackDonor)sampledWrist=NativeActionWrist(*rackActionSample,placed,input.input.worldUnitsPerMeter);
        else if(rackActionSample){
            // Donor grasp: the original sister-gun wrist relative to the handle it
            // racks, placed on THIS gun's handle as currently drawn (with travel).
            const auto at=std::find(rig->names.begin(),rig->names.end(),cachedRackDonorHandle);
            if(at!=rig->names.end()&&std::find(at+1,rig->names.end(),cachedRackDonorHandle)==rig->names.end()){
                const auto bone=std::size_t(at-rig->names.begin());auto handleWorld=native[bone];
                for(const auto& write:writes)if(write.index==bone)handleWorld=write.transform;
                sampledWrist=NativeActionWristOnPart(*rackActionSample,std::size_t(cachedRackDonorPart),handleWorld,input.input.worldUnitsPerMeter);
            }
            if(sampledWrist)++rackFamilyGraspPoses;else ++rackFamilyGraspRejects;
        }
        if(sampledWrist)desired=*sampledWrist;
        else if(contact){
            auto current=native;for(const auto& write:writes)current[write.index]=write.transform;
            const auto part=std::find(rig->names.begin(),rig->names.end(),contact->part);
            desired=interaction::BindAuthoredWrist(*contact,current[part-rig->names.begin()],input.input.worldUnitsPerMeter);
        }else{
            // Keep controller rotation, placing the hand's existing anatomical
            // mechanism point on the admitted handle's actual moving contact.
            auto local=interaction::reload_insertion_detail::Identity();const float fraction=handle.pistolSlide?std::clamp(input.handleTravel,0.f,1.f):handleShown;
            for(unsigned n=0;n<3;++n)local.values[3][n]=(handle.handleMeters[n]+fraction*handle.strokeMeters*handle.pullDirection[n])*input.input.worldUnitsPerMeter;
            const auto target=interaction::Multiply(local,placed);
            const auto p=handBinding->mechanismPointWristMeters;const float units=input.input.worldUnitsPerMeter;
            for(unsigned n=0;n<3;++n)desired.values[3][n]=target.values[3][n]-units*(p.x*desired.values[0][n]+p.y*desired.values[1][n]+p.z*desired.values[2][n]);
        }
        if(const auto attached=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
            {desired,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
            {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,true})){
            const auto baseWrites=writes;
            for(const auto& write:attached->writes){bool left=false;for(auto at=std::int32_t(write.index);at!=-1;at=rig->parents[at])if(unsigned(at)==rig->left.shoulder){left=true;break;}
                if(left)merge(std::array<interaction::BoneWrite,1>{write});}
            const auto wrist=std::find_if(writes.begin(),writes.end(),[&](const auto& write){return write.index==rig->left.wrist;});
            bool fingers=false;
            if(rackActionSample&&sampledWrist&&wrist!=writes.end())
                fingers=ApplyNativeActionFingers(*rackActionSample,*rig,writes,wrist->transform,input.input.worldUnitsPerMeter);
            else {
                fingers=contact&&contact->fingerCount&&wrist!=writes.end()&&
                    interaction::ApplyAuthoredFingers(rig->parents,rig->names,writes,*contact,wrist->transform,input.input.worldUnitsPerMeter);
                if(!fingers)fingers=poseHand(interaction::HandPoseRole::MechanismGrip,attached->writes);
            }
            if(const auto candidate=fingers?BuildRigPosePlan(*rig,writes):std::nullopt){plan=candidate;leftTarget=desired;handRole=interaction::HandPoseRole::MechanismGrip;handPosed=true;sampledRackHand=sampledWrist.has_value()&&!rackDonor;}
            else{writes=baseWrites;++handleRejects;}
        }else ++handleRejects;
    }
    }else {handleShown=0;handleShownNs=0;}
    // The admitted scoped-XM8 action owns its ORIGINAL complete handle matrix
    // and hand animation. Never run its donor/linear presentation in parallel,
    // including when current safety rejects this native pose.
    if(input.nativeRack&&targets->tracked[1]&&!targets->weaponAttachmentPending&&!sightAttached&&
       (!reloadAttached||!input.reload.preview->actionHandOwned)&&!freeRightReady){
        const auto& rack=*input.nativeRack;
        const auto sampled=BuildNativeRackPose(rack,*rig,placed,input.input.worldUnitsPerMeter,ReloadNanos());
        bool accepted=sampled.has_value()&&(!rack.ownsHand||
            (handRoleReady&&targets->tracked[0]&&input.input.hands[0].squeeze>.35f&&!supportAttached&&!pumpHandAttached&&!sampledMagazineHand));
        const auto originalWrites=writes;const auto originalPlan=plan;
        if(accepted&&sampled->wristTarget){
            const auto arms=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
                {*sampled->wristTarget,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},{{},{},{},false});
            accepted=arms&&!arms->reachClamped[0]&&arms->targetError[0]<=.0001f*input.input.worldUnitsPerMeter;
            if(accepted)merge(arms->writes);
        }
        if(accepted){
            merge(sampled->writes);
            const auto candidate=BuildRigPosePlan(*rig,writes);
            if(candidate){
                auto basePlan=originalPlan;
                if(handsMode==HandsPresentationMode::Floating){
                    const auto collapsed=CollapseArmsToWrists(*rig,*basePlan,originalWrites,targets->tracked[0],targets->tracked[1]);
                    if(collapsed)basePlan=collapsed;else accepted=false;
                }
                nativeRackBase=rig->nativeEvaluated;for(const auto& e:basePlan->edits)nativeRackBase[e.index]=e.after;
                if(magazineHidden&&magazineBinding){const auto hidden=HideMagazinePackedPalette(*rig,*magazineBinding,nativeRackBase);
                    if(hidden)nativeRackBase=*hidden;else accepted=false;}
                if(accepted){plan=candidate;nativeRackAttached=true;++nativeRackPoses;
                    if(sampled->wristTarget){leftTarget=*sampled->wristTarget;solved->writes=writes;
                        handRole=interaction::HandPoseRole::MechanismGrip;handPosed=true;sampledRackHand=true;}}
            }else accepted=false;
        }
        if(!accepted){writes=originalWrites;plan=originalPlan;++nativeRackRejects;}
    }
    if(pumpWriteCount&&(!reloadAttached||!input.reload.preview->actionHandOwned)&&!magazineAttached){
        std::vector<interaction::BoneWrite> part;for(unsigned n=0;n<pumpWriteCount;++n)part.push_back({pumpWrites[n].index,interaction::Multiply(pumpWrites[n].transform,placed)});
        const auto baseWrites=writes;merge(part);
        std::optional<RigPosePlan> candidate;
        if(reloadVisibleShell){const auto binding=bc2_reload_detail::DerivePresentationBinding(*rig);
            if(binding)candidate=bc2_reload_detail::BuildShellVisibilityPalette(*rig,*binding,writes,reloadVisibleShell);}
        else candidate=BuildRigPosePlan(*rig,writes);
        if(candidate){plan=candidate;++pumpPartPoses;}else{writes=baseWrites;++pumpPartRejects;}
    }
    // KF2-style visual acquisition/release. The recognizers above still consume
    // raw freeLeft; this correction never renews a claim or steers the weapon.
    // Fingers are blended in wrist space with the same transition as the wrist.
    static unsigned actionHandWeapon=0;static std::uint64_t actionHandOwner=0,actionHandEquipment=0,actionHandSpace=0;
    if(actionHandWeapon!=input.weapon||actionHandOwner!=input.ownerGeneration||actionHandEquipment!=input.equipmentGeneration||actionHandSpace!=input.input.spaceGeneration){
        nativeActionHandTransition.Reset();actionHandWeapon=input.weapon;actionHandOwner=input.ownerGeneration;
        actionHandEquipment=input.equipmentGeneration;actionHandSpace=input.input.spaceGeneration;
    }
    const bool sampledPumpHand=pumpOwnsHand&&(pumpHandAttached||supportAttached);
    const bool sampledActionHand=sampledPumpHand||sampledReloadHand||sampledMagazineHand||sampledRackHand;
    const bool otherHandOwner=sightAttached||(!sampledActionHand&&(supportAttached||pumpHandAttached||(reloadAttached&&input.reload.preview->actionHandOwned)||
        (magazineAttached&&input.magazine.target&&input.magazine.target->handTarget)||input.handleHandOwned));
    if(otherHandOwner||!handRoleReady||!targets->tracked[0]||!targets->tracked[1])nativeActionHandTransition.Reset();
    else if(sampledActionHand||nativeActionHandTransition.Active()){
        const auto freeTargets=interaction::ApplyHandTouch(LeftHandTargets(interaction::HandPoseRole::Free,input.input.hands[0].squeeze,input.input.hands[0].trigger),input.input.hands[0]);
        const auto freePose=interaction::GenerateHandPose(rig->parents,handBinding->referenceWorld,native,handBinding->pose,freeLeft,freeTargets);
        const auto readHand=[&](std::span<const interaction::BoneWrite> source,const math::Matrix4& wrist)->std::optional<NativeActionHandPose>{
            const auto inverse=interaction::InverseAnimatedTransform(wrist);if(!inverse)return {};
            NativeActionHandPose out;out.wrist=wrist;
            for(unsigned f=0;f<5;++f)for(unsigned j=0;j<3;++j){
                const auto bone=handBinding->pose.fingers[f].joints[j].index;
                const auto at=std::find_if(source.begin(),source.end(),[&](const auto& w){return w.index==bone;});if(at==source.end())return {};
                out.fingersInWrist[f*3+j]=interaction::ScaleTranslation(interaction::Multiply(at->transform,*inverse),1/input.input.worldUnitsPerMeter);
            }
            return out;
        };
        const auto freeHand=freePose?readHand(freePose->writes,freeLeft):std::nullopt;
        const auto actionHand=sampledActionHand?readHand(writes,leftTarget):std::nullopt;
        std::uint64_t token=0;
        if(sampledMagazineHand)token=(input.magazine.target->handClaim.id<<4)^(input.magazine.actionConstrained?9:1);
        else if(sampledReloadHand)token=(input.reload.preview->shellClaim.token.id<<4)^(input.reload.preview->actionConstrained?10:2);
        else if(sampledPumpHand)token=(input.pump->shot<<3)^3;
        else if(sampledRackHand)token=((nativeRackAttached?input.nativeRack->actionToken:std::uint64_t(input.weapon))<<3)^4;
        const auto transitioned=freeHand?nativeActionHandTransition.Update(ReloadNanos(),*freeHand,actionHand?&*actionHand:nullptr,token,input.input.worldUnitsPerMeter):std::nullopt;
        if(transitioned){
            const auto arms=SolvePresentedArms(handsMode,rig->parents,armNative,rig->left,rig->right,
                {transitioned->wrist,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},{{},{},{},false});
            if(arms&&!arms->reachClamped[0]){
                const auto before=writes;const auto oldWrist=leftTarget;merge(arms->writes);
                NativeActionPoseFrame fingers;fingers.fingersInWrist=transitioned->fingersInWrist;
                bool valid=ApplyNativeActionFingers(fingers,*rig,writes,transitioned->wrist,input.input.worldUnitsPerMeter);
                // A freely carried prop follows the blended native grasp. A
                // constrained item remains on the native mechanical trajectory.
                if(valid&&((sampledMagazineHand&&!input.magazine.actionConstrained)||(sampledReloadHand&&!input.reload.preview->actionConstrained))){
                    const auto inverse=interaction::InverseAnimatedTransform(oldWrist);if(!inverse)valid=false;
                    else {const auto delta=interaction::Multiply(*inverse,transitioned->wrist);
                        for(auto& w:writes){bool carried=sampledReloadHand&&rig->names[w.index]=="jntWpn_7";
                            if(sampledMagazineHand&&magazineBinding)carried=w.index==magazineBinding->magazine||
                                std::find(magazineBinding->carried.begin(),magazineBinding->carried.begin()+magazineBinding->carriedCount,w.index)!=magazineBinding->carried.begin()+magazineBinding->carriedCount;
                            if(carried)w.transform=interaction::Multiply(w.transform,delta);}
                    }
                }
                std::optional<RigPosePlan> candidate;
                if(valid&&reloadVisibleShell){const auto binding=bc2_reload_detail::DerivePresentationBinding(*rig);
                    if(binding)candidate=bc2_reload_detail::BuildShellVisibilityPalette(*rig,*binding,writes,reloadVisibleShell);}
                else if(valid)candidate=BuildRigPosePlan(*rig,writes);
                if(candidate){plan=std::move(candidate);leftTarget=transitioned->wrist;solved->writes=writes;handPosed=true;}
                else {writes=before;nativeActionHandTransition.Reset();}
            }else nativeActionHandTransition.Reset();
        }
    }
    auto pose=std::make_shared<PublishedPose>();pose->authoredGrip=authored.binding;pose->equipment=input.nativeEquipment;pose->identity=rig->identity;pose->weapon=input.weapon;pose->generation=input.input.generation;pose->ownerGeneration=input.ownerGeneration;pose->equipmentGeneration=input.equipmentGeneration;pose->space=input.input.spaceGeneration;pose->deadline=input.deadline;pose->before=rig->nativeEvaluated;pose->posed=pose->before;
    pose->deadline=presentationDeadline;pose->recoilDeadline=(recoilApplied||kickApplied)?presentationDeadline:0;
    // Diagnostic metadata travels WITH the immutable palette actually packed.
    // No selected Meshes1p association is invented from the item display name.
    if(ReloadProducerBinding().Enabled()){
        LARGE_INTEGER now{},frequency{};
        if(QueryPerformanceCounter(&now)&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0){
            const auto ns=[&](std::int64_t tick){return (tick/frequency.QuadPart)*1000000000+(tick%frequency.QuadPart)*1000000000/frequency.QuadPart;};
            auto& d=pose->drawSource;d.owner={input.soldier,input.weak,input.weapon,input.ownerGeneration,input.input.spaceGeneration};
            d.rigPose=rig->identity.pose;d.rigFingerprint=SightRigFingerprint(rig->names,rig->parents,rig->inverseBind);
            d.inputGeneration=input.input.generation;d.observedNs=ns(now.QuadPart);d.deadlineNs=ns(input.deadline);d.boneCount=rig->identity.count;
            d.physicalEquipmentGeneration=input.equipmentGeneration;
            d.handsNamed=nativeHandCapture::Enabled()&&rig->identity.count==147&&
                d.rigFingerprint==0xa7f219a1426216abull&&native_hand_detail::Bones(*rig).has_value();
            const auto selected=gameplay::ReadCurrentSelectedMeshes(d.observedNs);
            if(selected&&ReticleSelectedValid(*selected,d.observedNs)&&selected->sequence<=input.input.generation&&
               selected->owner.soldier==input.soldier&&selected->owner.weak==input.weak&&selected->owner.weapon==input.weapon&&
               selected->owner.actorGeneration==input.ownerGeneration&&selected->owner.space==input.input.spaceGeneration&&
               selected->weaponData==input.nativeEquipment.data){
                d.opticSelected=selected;d.observedNs=std::max(d.observedNs,selected->observedNs);d.deadlineNs=std::min(d.deadlineNs,selected->deadlineNs);
            }
            const std::string_view item(input.assetName.data());
            for(unsigned n=0;n<rig->names.size();++n)if(rig->parents[n]==std::int32_t(rig->weaponBone)){
                if(item=="SPAS12_sp"&&rig->names[n]=="jntWpn_7"){
                    d.shellNamed=true;d.shellIndex=n;d.shellHidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)!=rig->nativeHiddenLeaves.end();}
                if((item=="XM8_sp_s"||item=="40mmgl")&&rig->names[n]=="jntWpn_10"){d.opticNamed=true;d.opticIndex=n;}
            }
        }
    }
    // FVR_HANDS_MODE=floating: collapse the tracked arms' skin onto the final
    // wrists (Bc2FloatingHands.h). Raw contacts precede cosmetic hand targets;
    // floating item/wrist placement does not inherit anatomical arm reach.
    if(nativeHandPoseConsumer.load(std::memory_order_acquire)){
        pose->handLayout=NativeHandLayout(*rig);pose->handTracked=targets->tracked;pose->handObservedNs=ReloadNanos();
    }
    if(gameplay::HandsMode()==HandsPresentationMode::Floating&&(targets->tracked[0]||targets->tracked[1])){
        if(auto floating=CollapseArmsToWrists(*rig,*plan,writes,targets->tracked[0],targets->tracked[1])){plan=std::move(floating);++floatingPoses;}
        else ++floatingRejects;
    }
    for(const auto& edit:plan->edits)pose->posed[edit.index]=edit.after;
    if(magazineAttached){
        if(magazineHidden){
            const auto hidden=HideMagazinePackedPalette(*rig,*magazineBinding,pose->posed);
            if(!hidden){RejectTracking(15);published.reset();return;}pose->posed=*hidden;
        }
        pose->magazine=std::make_shared<const MagazineTracking>(input.magazine);pose->magazineBase=std::move(magazineBase);
    }
    PoseXm8Bolt(*rig,input,writes,placed,targets->tracked[1],pose->posed);
    if(freeRightReady)pose->freeRight=input.freeRight;
    if(sightAttached){pose->previewToken=preview.token;pose->previewBase=std::move(previewBase);}
    if(reloadAttached){pose->reloadPreview=input.reload.preview;pose->reloadBase=std::move(reloadBase);pose->reloadDeadlineNs=reloadDeadline;}
    if(nativeRackAttached){pose->nativeRack=input.nativeRack;pose->nativeRackBase=std::move(nativeRackBase);}
    for(auto leaf:rig->nativeHiddenLeaves)if(pose->posed[leaf]!=pose->before[leaf]&&
        !(reloadAttached&&reloadVisibleShell&&leaf==*reloadVisibleShell)){++fallbackFailures;published.reset();return;}
    // Independently unavailable arms must remain byte-for-byte native, including
    // their fingers/twists. Also leave the weapon chain native without right grip.
    const auto unchangedBranch=[&](unsigned root){for(unsigned n=0;n<rig->parents.size();++n){
        bool below=false;for(auto at=std::int32_t(n);at!=-1;at=rig->parents[at])if(unsigned(at)==root){below=true;break;}
        if(below&&pose->posed[n]!=pose->before[n])return false;
    }return true;};
    if((!targets->tracked[0]&&!unchangedBranch(rig->left.shoulder))||
       (!targets->tracked[1]&&(!unchangedBranch(rig->right.shoulder)||!unchangedBranch(rig->weaponBone)))){
        ++fallbackFailures;published.reset();return;
    }
    // This attachment is observed by its native name and validated ancestry,
    // never an inherited or hard-coded bone index. The firing consumer binds its own event.
    if(targets->tracked[1])for(unsigned n=0;n<rig->names.size();++n)if(rig->names[n]=="jntWpn_Flash"){
        bool child=false;for(auto parent=rig->parents[n];parent!=-1;parent=rig->parents[parent])if(unsigned(parent)==rig->weaponBone){child=true;break;}
        if(child)for(const auto& write:writes)if(write.index==n){
            pose->shot=WeaponShotFrame{weapon,placed,native[n],write.transform,input.input.generation,presentationDeadline,n,input.input.worldUnitsPerMeter,input.ownerGeneration,input.input.spaceGeneration};break;
        }
        break;
    }

    if(pose->shot&&authored.binding){
        pose->shot->authoredSupportReference=AuthoredRifleSupport(*authored.binding);
        pose->shot->configuredMuzzleReference=AuthoredFlashCapability(*authored.binding)&&
            (orientationSource==WeaponOrientationSource::AuthoredAxes||
             (orientationSource==WeaponOrientationSource::ControllerGrip&&boreInGrip.has_value()))&&
            std::count(rig->names.begin(),rig->names.end(),"jntWpn_Flash")==1;
        if(pose->shot->configuredMuzzleReference&&authored.binding->profile->useMuzzleEffectOffset){
            // Native M416 keeps its Flash bone at the root; the configured
            // ordinary muzzle effect supplies the physical attachment offset.
            const auto offset=authored.binding->profile->muzzleEffectOffset;
            math::Matrix4 local{};for(unsigned n=0;n<4;++n)local.values[n][n]=1;
            const float units=pose->shot->unitsPerMetre;
            local.values[3]={offset.x*units,offset.y*units,offset.z*units,1};
            pose->shot->nativeFlash=interaction::Multiply(local,weapon);
            pose->shot->trackedFlash=interaction::Multiply(local,placed);
        }
    }
    if(pose->shot)pose->shot->orientationSource=std::uint8_t(orientationSource);
    // The native support wrist is authored relative to this evaluated weapon
    // root. Contact always uses the independent tracked target, including while
    // the visible hand is attached. No guessed attachment offset.
    if(pose->shot&&targets->tracked[0]&&targets->tracked[1]){
        const auto inverseWeapon=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
        const auto inverseBody=interaction::InverseAnimatedTransform(*body);
        const auto left=math::MakeRelativePose(input.input.referenceHead,input.input.hands[0].grip);
        const auto right=math::MakeRelativePose(input.input.referenceHead,input.input.hands[1].grip);
        if(inverseWeapon&&inverseBody&&left&&right){
            const auto support=interaction::Multiply(fixedSupportLocal?*fixedSupportLocal:authoredSupportLocal,interactionPlaced);
            const auto l=point(targets->left),r=point(targets->right),s=point(support);
            const float units=input.input.worldUnitsPerMeter;
            const math::Vec3 delta{l.x-r.x,l.y-r.y,l.z-r.z};
            const auto& b=inverseBody->values;
            const math::Vec3 relative{(delta.x*b[0][0]+delta.y*b[1][0]+delta.z*b[2][0])/units,
                (delta.x*b[0][1]+delta.y*b[1][1]+delta.z*b[2][1])/units,
                -(delta.x*b[0][2]+delta.y*b[1][2]+delta.z*b[2][2])/units};
            pose->shot->support={true,std::hypot(l.x-s.x,l.y-s.y,l.z-s.z)/units,
                {relative.x-(left->position.x-right->position.x),relative.y-(left->position.y-right->position.y),relative.z-(left->position.z-right->position.z)}};
        }
    }
    // Contact is computed from the independent left controller target, never
    // the support-snapped or reach-clamped visible wrist. Source sight geometry
    // remains authored by BC2; only its relation to the placed gun is measured.
    if(pose->shot&&handRoleReady&&targets->tracked[0]&&targets->tracked[1]&&
       input.sightAdapter&&input.sightAdapter->nativeSightAccepted&&input.sightAdapter->assembly==SightAssemblyKind::OpposedLeaves){
        const auto sight=std::find(rig->names.begin(),rig->names.end(),"jntWpn_9");
        if(sight!=rig->names.end()){
            const auto index=unsigned(sight-rig->names.begin());
            if(rig->parents[index]==std::int32_t(rig->weaponBone)){
                WeaponSightObservation observation;
                observation.assetName=input.assetName.data();observation.rootName=rig->names[rig->weaponBone];
                observation.sightName=*sight;observation.sightParentName=rig->names[rig->parents[index]];
                observation.skeletonFingerprint=rigFingerprint;
                observation.generation=input.input.generation;observation.deadline=presentationDeadline;
                observation.nativeWeapon=native[rig->weaponBone];observation.nativeSight=native[index];
                observation.placedWeapon=interactionPlaced;observation.rawLeftTarget=freeLeft;
                observation.unitsPerMetre=input.input.worldUnitsPerMeter;
                observation.attachmentReady=!targets->weaponAttachmentPending;
                // The recognition point and captured mechanism pose use the same
                // bind-derived landmark and independent pre-IK anatomical wrist.
                // Native grip animation cannot move the activation point, and no
                // support-snapped or preview wrist can sustain its own contact.
                observation.palmValid=true;observation.palmPointWristMeters=handBinding->mechanismPointWristMeters;
                observation.visualHandValid=true;observation.visualLeftTarget=freeLeft;
                observation.visualPointWristMeters=handBinding->mechanismPointWristMeters;
                const auto front=std::find(rig->names.begin(),rig->names.end(),"jntWpn_11");
                if(front!=rig->names.end()){
                    const auto frontIndex=unsigned(front-rig->names.begin());
                    observation.frontValid=rig->parents[frontIndex]==std::int32_t(rig->weaponBone)&&
                        std::find(rig->parents.begin(),rig->parents.end(),std::int32_t(frontIndex))==rig->parents.end()&&
                        std::find(rig->parents.begin(),rig->parents.end(),std::int32_t(index))==rig->parents.end()&&
                        std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),frontIndex)==rig->nativeHiddenLeaves.end();
                    observation.nativeFront=native[frontIndex];
                }
                observation.hidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),index)!=rig->nativeHiddenLeaves.end();
                pose->shot->sight=MeasureWeaponSightContact(observation);
                // Source animation can be observed while raw-item attachment
                // calibration is pending. It cannot arm or renew contact; the
                // held visual uses only its measured rotation catching up.
                if(input.sightPhysicalItem&&rigFingerprint==0xa7f219a1426216abull&&
                   observation.rootName=="jntWpn_1"&&!observation.hidden&&observation.frontValid&&
                   std::isfinite(observation.unitsPerMetre)&&observation.unitsPerMetre>0){
                    const auto inverse=interaction::InverseAnimatedTransform(observation.nativeWeapon);
                    if(inverse&&interaction::InverseAnimatedTransform(observation.nativeSight)&&interaction::InverseAnimatedTransform(observation.nativeFront)){
                        NativeSightState state;
                        state.rear=interaction::Multiply(observation.nativeSight,*inverse);
                        state.front=interaction::Multiply(observation.nativeFront,*inverse);
                        for(auto matrix:{&state.rear,&state.front})for(unsigned n=0;n<3;++n)matrix->values[3][n]/=observation.unitsPerMetre;
                        state.physicalItem=input.sightPhysicalItem;
                        // Use the independent target computed before any support,
                        // mechanism pose or arm IK, never the displayed wrist.
                        const auto inversePlaced=interaction::InverseAnimatedTransform(interactionPlaced);
                        if(inversePlaced&&interaction::InverseAnimatedTransform(freeLeft)){
                            state.rawHand=interaction::Multiply(freeLeft,*inversePlaced);
                            for(unsigned n=0;n<3;++n)state.rawHand.values[3][n]/=observation.unitsPerMetre;
                            state.rawHandValid=bool(interaction::InverseAnimatedTransform(state.rawHand));
                        }
                        state.valid=true;pose->shot->nativeSight=state;
                    }
                }
            }
        }
    }
    if(pose->shot&&handRoleReady&&targets->tracked[0]&&targets->tracked[1]&&input.sightAdapter&&
       input.sightAdapter->nativeSightAccepted&&input.sightAdapter->assembly==SightAssemblyKind::Subtree&&
       input.sightAdapter->authored&&input.sightPhysicalItem&&authored.binding&&authored.binding->profile&&
       authored.binding->profile->meshPath==input.sightAdapter->authored->meshPath){
        const auto& p=*input.sightAdapter;
        AuthoredSightObservation observation;
        observation.selectedMeshPath=authored.binding->profile->meshPath;
        observation.asset=input.assetName.data();observation.configuration=input.sightSecondary?p.family->secondaryConfiguration:p.family->primaryConfiguration;
        observation.rigFingerprint=rigFingerprint;observation.generation=input.input.generation;
        LARGE_INTEGER sightClock{};QueryPerformanceCounter(&sightClock);observation.now=sightClock.QuadPart;observation.deadline=presentationDeadline;observation.names=rig->names;observation.parents=rig->parents;
        observation.native=native;observation.hidden=rig->nativeHiddenLeaves;observation.placedWeapon=interactionPlaced;
        observation.rawAnatomicalWrist=freeLeft;observation.mechanismPointWristMeters=handBinding->mechanismPointWristMeters;
        observation.unitsPerMetre=input.input.worldUnitsPerMeter;observation.attachmentReady=!targets->weaponAttachmentPending;
        pose->shot->sight=MeasureSightAdapterContact(p,observation);
        const auto nativeProgress=ReadSightAdapterNativeFrame(p,observation);
        if(nativeProgress){
            auto& state=pose->shot->nativeSight;state.rear=nativeProgress->rear;
            state.rawHand=nativeProgress->rawHand;state.rawHandValid=true;
            state.physicalItem=input.sightPhysicalItem;state.valid=true;
        }
    }
    if(pose->shot)pose->shot->bodyRigFingerprint=rigFingerprint;
    if(pose->shot&&magazineContact.valid){pose->shot->magazine=magazineContact;++magazineContacts;}
    if(pose->shot&&reloadContact.valid){pose->shot->reload=reloadContact;++reloadRawContacts;
        if(reloadContact.nativeShellVisible)++reloadNativeVisibleContacts;else ++reloadNativeHiddenContacts;
    }
    const auto canonicalPlaced=placed;
    auto nativePlaced=interactionPlaced;
    for(unsigned n=0;n<4;++n){nativePlaced.values[2][n]=-nativePlaced.values[2][n];nativePlaced.values[n][2]=-nativePlaced.values[n][2];}
    std::array<std::byte,64> current{};if(!Read(at,current.data(),64)||current!=original||!Fresh(presentationDeadline)||!EquipmentStillCurrent(input.nativeEquipment)||!AuthoredCurrent(authored.binding)){if(authored.binding)++authoredGripExpired;RejectTracking(16);return;}
    // Only the verified weapon getter's caller-owned output is replaced. Bone
    // animation buffers remain untouched; native root/effect consumers run once.
    if(targets->tracked[1]){for(unsigned row=0;row<4;++row)std::memcpy(reinterpret_cast<std::byte*>(destination)+row*16,nativePlaced.values[row].data(),12);++weaponOutputs;
        PublishPlacementEvidence(input,orientationSource,weapon,canonicalPlaced,*body,targets->weaponAttachmentPending);}
    const ShotGuard boreIdentity{input.soldier,input.weak,input.weapon,input.ownerGeneration,input.input.spaceGeneration,input.input.generation,input.deadline,true,targets->tracked[0],0,input.weaponActionsBlocked,input.equipmentGeneration,input.nativeEquipment};
    gripBorePublication.store(boreInGrip?std::make_shared<const GripBorePublication>(GripBorePublication{*boreInGrip,boreIdentity,authored.binding}):
        std::shared_ptr<const GripBorePublication>{},std::memory_order_release);
    if(pose->shot){
        const ShotGuard identity{input.soldier,input.weak,input.weapon,input.ownerGeneration,input.input.spaceGeneration,input.input.generation,input.deadline,true,targets->tracked[0],0,input.weaponActionsBlocked,input.equipmentGeneration,input.nativeEquipment};
        shotPublication.store(std::make_shared<const ShotPublication>(ShotPublication{*pose->shot,identity,authored.binding}),std::memory_order_release);
    }else shotPublication.store({},std::memory_order_release);
    if(const auto requested=visibilityGuard.load(std::memory_order_acquire);requested&&requested->enabled&&
        requested->input.sequence==input.input.generation&&requested->nativeOwner.soldier==input.soldier&&requested->nativeOwner.weak==input.weak&&
        requested->nativeOwner.weapon==input.weapon&&requested->nativeOwner.actorGeneration==input.ownerGeneration&&
        requested->nativeOwner.space==input.input.spaceGeneration&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken){
        auto visibility=BuildWeaponVisibilityPalette(*rig,pose->posed,*requested,ReloadNanos());
        if(visibility.reason==WeaponVisibilityReason::None){pose->visibility=std::make_shared<const WeaponVisibilityPlan>(std::move(visibility));++visibilityPlans;}
        else ++visibilityRejected;
    }
    if(!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility&&input.reload.shellControl){
        if(auto hidden=BuildReloadShellHidePalette(input.reload,*rig,pose->posed,input.assetName.data(),ReloadNanos())){
            pose->shellHide=std::make_shared<const ReloadShellHidePlan>(std::move(*hidden));++reloadShellHidePlans;
        }
    }
    if(!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility&&!pose->freeRight&&
        !input.weaponActionsBlocked&&!targets->weaponAttachmentPending&&ReloadBeltCompatible(input.reload,ReloadNanos())&&
        (!input.reload.belt->heldCycle||pose->shellHide)){
        if(auto belt=BuildSpasBeltAmmoPalette(*input.reload.belt,*rig,pose->posed,input.input,*body,input.assetName.data(),ReloadNanos())){
            pose->belt=std::make_shared<const BeltAmmoPalette>(std::move(*belt));++beltPlans;
        }
    }
    if(pose->freeRight&&(!pose->visibility||!pose->visibility->hidden)){RejectTracking(15);published.reset();return;}
    if(const auto request=ordinaryEquipmentGuard.load(std::memory_order_acquire);request&&
        OrdinaryGuardCurrent(*request)&&request->input.sequence==input.input.generation&&
        request->equipment==input.nativeEquipment&&request->input.owner.equipGeneration==input.equipmentGeneration&&
        targets->tracked[0]&&targets->tracked[1]&&!targets->weaponAttachmentPending&&
        !pose->visibility&&!pose->freeRight&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->shellHide&&!pose->belt)
        pose->ordinaryRecovery=*request;
    published=std::move(pose);
    if(kickApplied){
        ++nativeRecoilKickPoses;auto maximum=nativeRecoilMaximumKickMeters.load(std::memory_order_relaxed);
        while(kickMeters>maximum&&!nativeRecoilMaximumKickMeters.compare_exchange_weak(maximum,kickMeters,std::memory_order_relaxed)){}
    }
    if(recoilApplied){
        ++nativeRecoilPoses;if(recoilNonidentity)++nativeRecoilNonidentityPoses;
        auto maximum=nativeRecoilMaximumRadians.load(std::memory_order_relaxed);
        while(recoilRadians>maximum&&!nativeRecoilMaximumRadians.compare_exchange_weak(maximum,recoilRadians,std::memory_order_relaxed)){}
    }
    if(reloadVisibleShell)++reloadOwnedShellVisibilityPoses;
    const auto now=GetTickCount64();if(now-handRecordTime>=(NativeActionCaptureRequested()?20u:100u)){auto& record=handRecords[handRecordNext];handRecordNext=(handRecordNext+1)%unsigned(handRecords.size());handRecordCount=(std::min)(handRecordCount+1,unsigned(handRecords.size()));record={};record.attachmentPending=targets->weaponAttachmentPending;record.authoredGrip=targets->weaponAttachmentAuthored;record.supportAttached=supportAttached;record.sightAttached=sightAttached;record.nativeRight=native[rig->right.wrist];record.nativeLeft=native[rig->left.wrist];record.generation=input.input.generation;record.space=input.input.spaceGeneration;record.eyeBase=*body;
        for(unsigned side=0;side<2;++side)if(input.input.hands[side].gripTracked)if(const auto local=math::MakeRelativePose(input.input.referenceHead,input.input.hands[side].grip))record.localGrips[side]=local->position;
        if(NativeActionCaptureRequested()&&authored.binding&&authored.binding->profile&&
           AuthoredCurrent(authored.binding)){
            NativeActionCaptureRecord action;const auto& profile=*authored.binding->profile;
            bool valid=profile.assetName.size()<action.asset.size()&&profile.meshPath.size()<action.configuredMesh.size();
            if(valid){std::memcpy(action.asset.data(),profile.assetName.data(),profile.assetName.size());
                std::memcpy(action.configuredMesh.data(),profile.meshPath.data(),profile.meshPath.size());}
            action.rigFingerprint=rigFingerprint;action.equipmentGeneration=input.equipmentGeneration;
            action.capturedMs=now;action.animation=rig->identity.animation;action.pose=rig->identity.pose;
            const auto firingNow=ReloadNanos();const auto& firing=input.nativeActionFiring;
            if(firing&&firing->owner.soldier==input.soldier&&firing->owner.weak==input.weak&&firing->owner.weapon==input.weapon&&
               firing->owner.actorGeneration==input.ownerGeneration&&firing->owner.space==input.input.spaceGeneration&&
               firing->config.weaponData==input.nativeEquipment.data&&firing->sequence<=input.input.generation&&
               firing->observedNs<=firingNow&&firingNow-firing->observedNs<200000000)action.firing=firing;
            const auto inverseWeapon=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
            const auto inverseWrist=interaction::InverseAnimatedTransform(native[rig->left.wrist]);
            const auto relative=[&](const math::Matrix4& m,const math::Matrix4& inverse){
                return interaction::ScaleTranslation(interaction::Multiply(m,inverse),1.f/input.input.worldUnitsPerMeter);};
            valid=valid&&inverseWeapon.has_value()&&inverseWrist.has_value();
            if(valid){
                action.nativeWeapon=interaction::ScaleTranslation(native[rig->weaponBone],1.f/input.input.worldUnitsPerMeter);
                action.nativeLeftWrist=interaction::ScaleTranslation(native[rig->left.wrist],1.f/input.input.worldUnitsPerMeter);
                action.wristInWeapon=relative(native[rig->left.wrist],*inverseWeapon);
                valid=interaction::authored_contact_detail::Rigid(action.wristInWeapon,2.f);
                constexpr std::array<const char*,5> fingers{"Thumb","Index","Middle","Ring","Pinky"};
                for(unsigned finger=0;finger<5&&valid;++finger)for(unsigned joint=0;joint<3&&valid;++joint){
                    const auto name=std::string("LeftHand")+fingers[finger]+std::to_string(joint+1);
                    const auto found=std::find(rig->names.begin(),rig->names.end(),name);
                    if(found==rig->names.end()){valid=false;break;}const auto index=unsigned(found-rig->names.begin());
                    bool child=false;for(auto n=rig->parents[index];n!=-1;n=rig->parents[n])if(unsigned(n)==rig->left.wrist){child=true;break;}
                    auto& matrix=action.fingersInWrist[finger*3+joint];matrix=relative(native[index],*inverseWrist);
                    valid=child&&interaction::authored_contact_detail::Rigid(matrix,.5f);
                }
                for(unsigned n=0;n<rig->names.size()&&valid;++n){
                    if(n==rig->weaponBone)continue;
                    bool child=false;for(auto parent=rig->parents[n];parent!=-1;parent=rig->parents[parent])if(unsigned(parent)==rig->weaponBone){child=true;break;}
                    if(!child)continue;
                    // Hidden leaves are not usable action transforms. Preserve
                    // their absence explicitly rather than fabricate a bind pose.
                    if(std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)!=rig->nativeHiddenLeaves.end())continue;
                    if(action.partCount==action.parts.size()||rig->names[n].size()>=action.parts[0].bone.size()){valid=false;break;}
                    auto& part=action.parts[action.partCount++];std::memcpy(part.bone.data(),rig->names[n].data(),rig->names[n].size());
                    part.partInWeapon=relative(native[n],*inverseWeapon);
                    valid=interaction::authored_contact_detail::Rigid(part.partInWeapon,2.f);
                }
            }
            if(valid&&action.partCount)record.nativeAction=std::move(action);
        }
        record.rawAnchorValid=handRoleReady;record.rawLeftTarget=freeLeft;record.interactionWeapon=interactionPlaced;record.leftGripWorld=leftGripWorld;record.leftController=input.input.hands[0].grip;record.referenceHead=input.input.referenceHead;record.unitsPerMeter=input.input.worldUnitsPerMeter;
        record.nativeWeapon=weapon;record.placedWeapon=canonicalPlaced;record.grip=input.input.hands[1].gripTracked?input.input.hands[1].grip.position:math::Vec3{};
        record.resolvedRight=native[rig->right.wrist];
        record.actorPosition=input.actorPosition;record.nativeRoot=point(animatedBody);record.bodyRoot=point(*body);record.anatomyRoot=point(anatomy);
        record.tracked=targets->tracked;record.weapon=input.weapon;record.ownerGeneration=input.ownerGeneration;
        for(unsigned n=0;n<2;++n)record.grips[n]=input.input.hands[n].gripTracked?input.input.hands[n].grip.position:math::Vec3{};
        record.resolved={point(native[rig->left.wrist]),point(native[rig->right.wrist])};
        record.shoulders={point(native[rig->left.shoulder]),point(native[rig->right.shoulder])};record.anchors={targets->arms[0].shoulder,targets->arms[1].shoulder};record.targets={point(leftTarget),point(targets->right)};record.errors=solved->targetError;
        for(const auto& w:solved->writes){if(w.index==rig->left.wrist)record.resolved[0]=point(w.transform);if(w.index==rig->right.wrist){record.resolved[1]=point(w.transform);record.resolvedRight=w.transform;}}handRecordTime=now;}
    if(triggerEvidence){
        if(triggerEvidence->trigger>0)++triggerPoses;
        if(now-triggerRecordTime>=100){
            triggerRecords[triggerNext]=*triggerEvidence;triggerNext=(triggerNext+1)%triggerRecords.size();
            triggerCount=std::min(unsigned(triggerRecords.size()),triggerCount+1);triggerRecordTime=now;
        }
    }
    if(handPosed){
        ++handRolePoses[unsigned(handRole)];
        if(now-handPoseRecordTime>=100){
            HandPoseEvidence record;record.generation=input.input.generation;record.space=input.input.spaceGeneration;
            record.owner=input.ownerGeneration;record.weapon=input.weapon;record.role=handRole;
            record.squeeze=input.input.hands[0].squeeze;record.trigger=input.input.hands[0].trigger;
            record.touchActive=input.input.hands[0].touchActive;record.touched=input.input.hands[0].touched;
            record.grip=leftGripWorld;record.mechanismPoint=handBinding->mechanismPointWristMeters;
            auto evaluated=native;for(const auto& w:writes)evaluated[w.index]=w.transform;
            record.wrist=evaluated[rig->left.wrist];bool valid=true;unsigned atFinger=0;
            for(const auto& finger:handBinding->pose.fingers)for(unsigned joint=0;joint<finger.count;++joint){
                const auto index=finger.joints[joint].index,parent=unsigned(rig->parents[index]);
                const auto r=interaction::InverseAnimatedTransform(handBinding->referenceWorld[parent]);
                const auto n=interaction::InverseAnimatedTransform(native[parent]);
                const auto p=interaction::InverseAnimatedTransform(evaluated[parent]);
                if(!r||!n||!p||atFinger>=record.fingers.size()){valid=false;break;}
                record.fingers[atFinger++]={index,parent,interaction::Multiply(handBinding->referenceWorld[index],*r),
                    interaction::Multiply(native[index],*n),interaction::Multiply(evaluated[index],*p)};
            }
            if(valid&&atFinger==record.fingers.size()){
                handPoseRecords[handPoseNext]=record;handPoseNext=(handPoseNext+1)%handPoseRecords.size();
                handPoseCount=std::min(unsigned(handPoseRecords.size()),handPoseCount+1);handPoseRecordTime=now;
            }
        }
    }
    if(!rig->nativeHiddenLeaves.empty())++hiddenLeafPoses;
    if(supportAttached)++supportAttachedPoses;
    if(sightAttached){
        ++sightPreviewPoses;
        if(sightEvidence&&now-sightPreviewRecordTime>=20){
            sightPreviewRecords[sightPreviewNext]=*sightEvidence;sightPreviewNext=(sightPreviewNext+1)%sightPreviewRecords.size();
            sightPreviewCount=std::min(unsigned(sightPreviewRecords.size()),sightPreviewCount+1);sightPreviewRecordTime=now;
        }
    }
    ++trackedPoses;if(targets->calibrated)++calibrations;
    for(unsigned n=0;n<2;++n){if(targets->tracked[n])++handPoses[n];if(targets->calibratedHands[n])++handCalibrations[n];}
    if(!targets->tracked[0]||!targets->tracked[1])++partialPoses;
}
void Start()noexcept {started=GetTickCount64();enabled.store(true,std::memory_order_release);}
std::optional<MagazineDetachPairReceipt> ReadMagazineDetachPair(const ReloadStateOwner& owner,std::int64_t now)noexcept {
    const auto pair=magazineDetachPair.load(std::memory_order_acquire);
    const auto current=magazineGuard.load(std::memory_order_acquire);
    if(!pair||pair->authorization.current.identity.owner!=owner||!current||!current->detach||current->owner!=owner||
       !MagazineDetachPairCurrent(*pair,*current->detach,pair->attached,now))return {};
    return *pair;
}
MagazinePackCounters ReadMagazinePackCounters()noexcept {
    MagazinePackCounters out;out.copies=magazineCopies.load();out.pairs=magazinePairs.load();out.fallbacks=magazineFallbacks.load();
    for(unsigned n=0;n<4;++n){out.roleCopies[n]=magazineRoleCopies[n].load();out.rolePairs[n]=magazineRolePairs[n].load();}return out;
}
BodyHolsterPackCounters ReadBodyHolsterPackCounters()noexcept {
    return {freeRightPoses.load(),freeRightPackedCopies.load(),freeRightPairedCopies.load(),freeRightFallbacks.load()};
}
bool Stop()noexcept {EnableNativeHandPosePublication(false);enabled.store(false,std::memory_order_release);PublishOrdinaryEquipment({});freeRightGuard.store({},std::memory_order_release);visibilityGuard.store({},std::memory_order_release);visibilityReceipt.store({},std::memory_order_release);bool okay=true;for(auto entry:hooks)if(entry){auto r=MH_DisableHook(entry);okay&=r==MH_OK||r==MH_ERROR_DISABLED;}const auto until=GetTickCount64()+2000;while(inFlight.load()&&GetTickCount64()<until)Sleep(1);return okay&&!inFlight.load();}
void Report(std::ostream& out){out<<std::setprecision(9);out<<'{';weaponCapture.Report(out);out<<",\"free_right_poses\":"<<freeRightPoses<<",\"free_right_fallbacks\":"<<freeRightFallbacks<<",\"free_right_packed_copies\":"<<freeRightPackedCopies<<",\"free_right_paired_copies\":"<<freeRightPairedCopies;
    out<<",\"native_recoil_transfer\":{\"poses\":"<<nativeRecoilPoses.load()<<",\"nonidentity_poses\":"<<nativeRecoilNonidentityPoses.load()
       <<",\"rejected\":"<<nativeRecoilRejected.load()<<",\"maximum_radians\":"<<nativeRecoilMaximumRadians.load()
       <<",\"muzzle_down_rectified\":"<<nativeRecoilRectified.load()<<",\"muzzle_bore_frames\":"<<nativeRecoilMuzzleBore.load()<<",\"model_bore_mismatch\":"<<nativeRecoilBoreMismatch.load()
       <<",\"kick_poses\":"<<nativeRecoilKickPoses.load()<<",\"kick_rejected\":"<<nativeRecoilKickRejected.load()<<",\"maximum_kick_meters\":"<<nativeRecoilMaximumKickMeters.load()
       <<",\"native_ballistic_basis_preserved\":true,\"same_frame_authority\":false}";
    out<<",\"pump_part\":{\"poses\":"<<pumpPartPoses.load()<<",\"rejects\":"<<pumpPartRejects.load()<<",\"captures\":"<<pumpPartCaptures.load()<<",\"pickup_eases\":"<<pickupEases.load()<<",\"pickup_ease_misses\":["<<pickupEaseMisses[0].load()<<','<<pickupEaseMisses[1].load()<<','<<pickupEaseMisses[2].load()<<','<<pickupEaseMisses[3].load()<<']'<<",\"handle_poses\":"<<handlePoses.load()<<",\"handle_rejects\":"<<handleRejects.load()<<",\"open_learned\":"<<pumpOpenLearned.load()<<",\"parts\":"<<pumpMechanismCount<<",\"learned_travel\":"<<pumpLearnedTravel<<'}';
    out<<",\"native_grasps\":{\"pump_hand_enabled\":"<<(PumpNativeHandEnabled()?"true":"false")<<",\"pump_hand_poses\":"<<pumpHandPoses.load()
        <<",\"pump_hand_rejects\":"<<pumpHandRejects.load()<<",\"rack_family_enabled\":"<<(RackFamilyGraspEnabled()?"true":"false")
        <<",\"rack_family_bindings\":"<<rackFamilyGraspBindings.load()<<",\"rack_family_poses\":"<<rackFamilyGraspPoses.load()
        <<",\"rack_family_rejects\":"<<rackFamilyGraspRejects.load()<<'}';
    out<<",\"native_rack\":{\"poses\":"<<nativeRackPoses.load()<<",\"rejects\":"<<nativeRackRejects.load()
       <<",\"packed_copies\":"<<nativeRackCopies.load()<<",\"fallbacks\":"<<nativeRackFallbacks.load()<<'}';
    out<<",\"magazine_presentation\":{\"poses\":"<<magazinePoses.load()<<",\"contacts\":"<<magazineContacts.load()
       <<",\"copies\":"<<magazineCopies.load()<<",\"pairs\":"<<magazinePairs.load()<<",\"fallbacks\":"<<magazineFallbacks.load()
       <<",\"attempts\":"<<magazinePlanAttempts.load()<<",\"binding_rejects\":"<<magazineBindingRejects.load()<<",\"plan_rejects\":"<<magazinePlanRejects.load()
       <<",\"reach_rejects\":"<<magazineReachRejects.load()<<",\"pose_rejects\":"<<magazinePoseRejects.load()
       <<",\"hidden_rejects\":"<<magazineHiddenRejects.load()<<",\"fresh_rejects\":"<<magazineFreshRejects.load()<<",\"reach_records\":[";
    for(unsigned n=0;n<magazineReachCount;++n){if(n)out<<',';const auto&r=magazineReachRecords[n];out<<"{\"sequence\":"<<r.sequence<<",\"role\":"<<r.role<<",\"error\":"<<r.error<<",\"solved\":"<<r.solved<<",\"clamped\":"<<r.clamped<<'}';}out<<"],\"fallback_observation\":{\"total\":"<<magazineFallbackJournal.Total()<<",\"dropped\":"<<magazineFallbackJournal.Dropped()<<",\"reason_counts\":{";
    for(unsigned n=0;n<MagazineFallbackNames.size();++n){if(n)out<<',';out<<'"'<<MagazineFallbackNames[n]<<"\":"<<magazineFallbackJournal.ReasonCount(MagazineFallbackReason(n));}
    out<<"},\"events\":[";bool firstFallback=true;
    magazineFallbackJournal.ForEach([&](const MagazineFallbackEvent& e){
        if(!firstFallback)out<<',';firstFallback=false;
        out<<"{\"flags\":"<<e.flags<<",\"now_ns\":"<<e.nowNs<<",\"draw_serial\":"<<e.drawSerial<<",\"source\":"<<e.source<<",\"bone_count\":"<<e.count<<",\"pack\":"<<e.pack
           <<",\"shot_present\":"<<e.shotPresent<<",\"shot_valid\":"<<e.shotValid<<",\"left_tracked\":"<<e.leftTracked<<",\"shot_coherent\":"<<e.coherent<<",\"shot_sequence\":"<<e.shotSequence<<",\"shot_deadline_qpc_ticks\":"<<e.shotDeadlineQpcTicks;
        const auto snapshot=[&](const char* name,const MagazineFallbackSnapshot& s){
            out<<",\""<<name<<"\":{\"present\":"<<s.present<<",\"target_present\":"<<s.targetPresent<<",\"role\":"<<s.role
               <<",\"owner\":{\"soldier\":"<<s.owner.soldier<<",\"weak\":"<<s.owner.weak<<",\"weapon\":"<<s.owner.weapon<<",\"actor_generation\":"<<s.owner.actorGeneration<<",\"equip_generation\":"<<s.owner.equipGeneration<<",\"space\":"<<s.owner.space<<'}'
               <<",\"target_owner\":{\"actor\":"<<s.targetOwner.actor<<",\"actor_generation\":"<<s.targetOwner.actorGeneration<<",\"equip_generation\":"<<s.targetOwner.equipGeneration<<",\"space\":"<<s.targetOwner.space<<'}'
               <<",\"cycle\":"<<s.cycle<<",\"input_sequence\":"<<s.input<<",\"reserve_sequence\":"<<s.reserveSequence<<",\"target_input_sequence\":"<<s.targetInput<<",\"input_observed_ns\":"<<s.inputObservedNs<<",\"target_observed_ns\":"<<s.targetObservedNs
               <<",\"item\":"<<s.item<<",\"item_generation\":"<<s.itemGeneration<<",\"hand_claim\":"<<s.hand.id<<",\"gun_claim\":"<<s.gun.id<<",\"hand_item\":"<<s.hand.item.id<<",\"gun_item\":"<<s.gun.item.id
               <<",\"removal_frame\":"<<s.removalFrame<<",\"replacement_frame\":"<<s.replacementFrame<<",\"deadlines_ns\":[";
            for(unsigned n=0;n<s.deadlines.size();++n){if(n)out<<',';out<<s.deadlines[n];}out<<"]}";
        };snapshot("original",e.original);snapshot("current",e.current);out<<'}';
    });out<<"]}}";
    out<<",\"belt_ammo\":{\"plans\":"<<beltPlans.load()<<",\"copies\":"<<beltCopies.load()<<",\"pairs\":"<<beltPairs.load()
       <<",\"fallbacks\":"<<beltFallbacks.load()<<",\"source_rejected\":"<<beltSourceRejected.load()<<",\"independent_magazine_instance\":false}";
    out<<",\"reload_presentation\":{\"poses\":"<<reloadPreviewPoses.load()<<",\"rejected\":"<<reloadPreviewRejected.load()
       <<",\"base_fallbacks\":"<<reloadPreviewFallbacks.load()<<",\"cycle_shell_hide_plans\":"<<reloadShellHidePlans.load()
        <<",\"cycle_shell_hide_copies\":"<<reloadShellHideCopies.load()<<",\"cycle_shell_hide_pairs\":"<<reloadShellHidePairs.load()
        <<",\"cycle_shell_hide_fallbacks\":"<<reloadShellHideFallbacks.load()<<",\"cycle_shell_source_rejects\":"<<reloadShellHideSourceRejects.load()
        <<",\"raw_contacts\":"<<reloadRawContacts.load()
       <<",\"ordinary_equipment_copies\":"<<ordinaryEquipmentCopies.load()<<",\"ordinary_equipment_pairs\":"<<ordinaryEquipmentPairs.load()
       <<",\"native_visible_contacts\":"<<reloadNativeVisibleContacts.load()<<",\"native_hidden_contacts\":"<<reloadNativeHiddenContacts.load()
       <<",\"owned_shell_visibility_poses\":"<<reloadOwnedShellVisibilityPoses.load()
       <<",\"verified_private_packs\":["<<reloadVerifiedPacks[0].load()<<','<<reloadVerifiedPacks[1].load()<<','<<reloadVerifiedPacks[2].load()<<','<<reloadVerifiedPacks[3].load()<<','<<reloadVerifiedPacks[4].load()<<']'
       <<",\"verified_paired_private_packs\":["<<reloadVerifiedPairedPacks[0].load()<<','<<reloadVerifiedPairedPacks[1].load()<<','<<reloadVerifiedPairedPacks[2].load()<<','<<reloadVerifiedPairedPacks[3].load()<<','<<reloadVerifiedPairedPacks[4].load()<<']'
       <<",\"verified_fallback_packs\":"<<reloadVerifiedFallbackPacks.load()
       <<",\"verified_paired_fallback_packs\":"<<reloadVerifiedPairedFallbackPacks.load()
       <<",\"native_hidden_shell_rejections\":"<<reloadPresentationRejections[unsigned(Bc2ReloadPresentationReason::NativeShellHidden)].load()
       <<",\"phase_poses\":["<<reloadPhasePoses[0].load()<<','<<reloadPhasePoses[1].load()<<','<<reloadPhasePoses[2].load()<<','<<reloadPhasePoses[3].load()<<','<<reloadPhasePoses[4].load()
       <<"],\"rejections_by_presentation_reason\":[";
    for(unsigned i=0;i<reloadPresentationRejections.size();++i){if(i)out<<',';out<<reloadPresentationRejections[i].load();}
    out<<"],\"native_animation_written\":false}";
    out<<",\"weapon_visibility\":{\"private_palette_consumer_integrated\":true,\"plans\":"<<visibilityPlans.load()
        <<",\"rejected\":"<<visibilityRejected.load()<<",\"verified_hidden_copies\":"<<visibilityHiddenCopies.load()
        <<",\"verified_shown_copies\":"<<visibilityShownCopies.load()<<",\"ordinary_fallbacks\":"<<visibilityFallbacks.load()
        <<",\"paired_pack_receipts\":"<<visibilityPairedReceipts.load()
        <<",\"native_animation_written\":false,\"submitted_visibility_verified\":false,\"headset_verified\":false}";
    out<<",\"authored_grip\":{\"experimental\":true,\"active_animation_verified\":false,\"copies\":"<<authoredGripCopies.load()<<",\"expired_before_output\":"<<authoredGripExpired.load()<<",\"status_counts\":[";
    for(unsigned n=0;n<unsigned(AuthoredGripStatus::Count);++n){if(n)out<<',';out<<authoredGripStatus[n].load();}
    out<<"],\"profiles\":[";bool authoredComma=false;for(const auto& p:AuthoredGripProfiles()){
        if(authoredComma)out<<',';authoredComma=true;out<<"{\"asset\":\""<<p.assetName<<"\",\"binding_digest\":\""<<p.bindingDigest<<"\",\"authored_rifle_support\":"<<(p.authoredRifleSupport?"true":"false")<<",\"authored_model_axis_evidence\":\""<<p.authoredAxisEvidence<<"\"}";
    }out<<"]}";
    out<<",\"weapon_profiles\":[";
    bool comma=false;for(const auto& item:WeaponProfiles()){if(comma)out<<',';comma=true;
        out<<"{\"asset_name\":\""<<item.assetName<<"\",\"profile_id\":\""<<item.core.stableId<<"\",\"revision\":"<<item.core.revision<<",\"features\":{";
        for(unsigned n=0;n<unsigned(interaction::WeaponFeature::Count);++n){if(n)out<<',';const auto feature=interaction::WeaponFeature(n);out<<'"'<<interaction::WeaponFeatureName(feature)<<"\":\""<<interaction::WeaponStatusName(interaction::WeaponFeatureStatus(&item.core,feature))<<'"';}out<<"}}";
    }out<<"],\"support_attached_poses\":"<<supportAttachedPoses.load()<<",\"support_magnet_blend_poses\":"<<supportMagnetBlendPoses.load()
        <<",\"support_magnet_return_poses\":"<<supportMagnetReturnPoses.load()
        <<",\"hands_mode\":\""<<HandsModeName(gameplay::HandsMode())<<"\",\"floating_poses\":"<<floatingPoses.load()<<",\"floating_rejects\":"<<floatingRejects.load()
        <<",\"floating_capped_mesh\":\"not_attempted_native_draw_unverified\""
        <<",\"authored_contacts\":"<<AuthoredContactsReport()<<",\"authored_support_wrists\":"<<authoredSupportWrists.load()
        <<",\"authored_support_fingers\":"<<authoredSupportFingers.load()<<",\"authored_finger_failures\":"<<authoredFingerFailures.load()<<",\"shot_frame_races\":"<<shotFrameRaces.load()<<",\"shot_frame_expired\":"<<shotFrameExpired.load()<<",\"shot_frame_unavailable\":"<<shotFrameUnavailable.load()<<",\"pulse_requested\":"<<(pulse?"true":"false")<<",\"hands_requested\":"<<(hands?"true":"false")<<",\"torso_stabilized_poses\":"<<torsoPoses.load()<<",\"hidden_leaf_poses\":"<<hiddenLeafPoses.load()<<",\"tracked_poses\":"<<trackedPoses.load()<<",\"tracking_rejected\":"<<trackingRejected.load()<<",\"tracking_unavailable\":"<<trackingUnavailable.load()<<",\"weapon_outputs\":"<<weaponOutputs.load()<<",\"calibrations\":"<<calibrations.load()<<",\"pose_misses\":"<<poseMisses.load()<<",\"pose_rebases\":"<<poseRebases.load()<<",\"pose_holds\":"<<poseHolds.load()<<",\"owners\":"<<owners.load()<<",\"copies\":"<<copies.load()<<",\"changed_copies\":"<<changedCopies.load()<<",\"paired_copies\":"<<pairedCopies.load()<<",\"rejected\":"<<rejected.load()<<",\"source_changes\":"<<sourceChanges.load()<<",\"packing_failures\":"<<packingFailures.load()<<",\"partial_poses\":"<<partialPoses.load()<<",\"fallback_failures\":"<<fallbackFailures.load()<<",\"hand_poses\":["<<handPoses[0].load()<<','<<handPoses[1].load()<<"],\"hand_calibrations\":["<<handCalibrations[0].load()<<','<<handCalibrations[1].load()<<"],\"native_animation_written\":false,\"hand_evidence\":[";
    for(unsigned n=0;n<handRecordCount;++n){const auto& r=handRecords[(handRecordNext+handRecords.size()-handRecordCount+n)%handRecords.size()];if(n)out<<',';out<<"{\"attachment_pending\":"<<(r.attachmentPending?"true":"false")<<",\"authored_grip\":"<<(r.authoredGrip?"true":"false")<<",\"support_attached\":"<<(r.supportAttached?"true":"false")<<",\"sight_attached\":"<<(r.sightAttached?"true":"false")<<",\"generation\":"<<r.generation<<",\"weapon\":"<<r.weapon<<",\"owner_generation\":"<<r.ownerGeneration<<",\"space\":"<<r.space<<",\"grip\":["<<r.grip.x<<','<<r.grip.y<<','<<r.grip.z<<"],\"native\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.nativeWeapon.values[k/4][k%4];}out<<"],\"eye_base\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.eyeBase.values[k/4][k%4];}out<<"],\"local_grips\":[";
        // Same-input anchors for reconstructing measured private replay contacts.
        for(unsigned side=0;side<2;++side){if(side)out<<',';const auto p=r.localGrips[side];out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}out<<"],\"placed\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.placedWeapon.values[k/4][k%4];}
        const auto anchorMatrix=[&](const char* name,const math::Matrix4& m){out<<"],\""<<name<<"\":[";for(unsigned k=0;k<16;++k){if(k)out<<',';out<<m.values[k/4][k%4];}};
        anchorMatrix("raw_left_target",r.rawLeftTarget);anchorMatrix("interaction_weapon",r.interactionWeapon);anchorMatrix("left_grip_world",r.leftGripWorld);
        out<<"],\"raw_anchor_valid\":"<<(r.rawAnchorValid?"true":"false")<<",\"units_per_meter\":"<<r.unitsPerMeter;
        const auto anchorPose=[&](const char* name,const math::Pose& p){out<<",\""<<name<<"\":["<<p.position.x<<','<<p.position.y<<','<<p.position.z<<','<<p.orientation.x<<','<<p.orientation.y<<','<<p.orientation.z<<','<<p.orientation.w<<']';};
        anchorPose("left_controller_pose",r.leftController);anchorPose("reference_head_pose",r.referenceHead);
        out<<",\"native_left_wrist\":[";for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.nativeLeft.values[k/4][k%4];}out<<"],\"native_right_wrist\":[";for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.nativeRight.values[k/4][k%4];}out<<"],\"right_wrist_matrix\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.resolvedRight.values[k/4][k%4];}out<<"],\"actor_position\":["<<r.actorPosition.x<<','<<r.actorPosition.y<<','<<r.actorPosition.z<<"],\"native_root\":["<<r.nativeRoot.x<<','<<r.nativeRoot.y<<','<<r.nativeRoot.z<<"],\"body_root\":["<<r.bodyRoot.x<<','<<r.bodyRoot.y<<','<<r.bodyRoot.z<<"],\"anatomy_root\":["<<r.anatomyRoot.x<<','<<r.anatomyRoot.y<<','<<r.anatomyRoot.z<<"],\"arms\":[";
        for(unsigned side=0;side<2;++side){if(side)out<<',';const auto vector=[&](math::Vec3 p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};out<<"{\"tracked\":"<<(r.tracked[side]?"true":"false")<<",\"grip\":";vector(r.grips[side]);out<<",\"shoulder\":";vector(r.shoulders[side]);out<<",\"anchor\":";vector(r.anchors[side]);out<<",\"target\":";vector(r.targets[side]);out<<",\"resolved\":";vector(r.resolved[side]);out<<",\"error\":"<<r.errors[side]<<'}';}out<<"],\"native_action\":";
        if(!r.nativeAction)out<<"null";
        else {const auto& action=*r.nativeAction;
            const auto actionMatrix=[&](const math::Matrix4& m){out<<'[';for(unsigned k=0;k<16;++k){if(k)out<<',';out<<m.values[k/4][k%4];}out<<']';};
            out<<"{\"asset\":"<<std::quoted(action.asset.data())<<",\"configured_mesh\":"<<std::quoted(action.configuredMesh.data())
               <<",\"rig_fingerprint\":"<<action.rigFingerprint<<",\"equipment_generation\":"<<action.equipmentGeneration
               <<",\"captured_ms\":"<<action.capturedMs<<",\"animation_object\":"<<action.animation<<",\"pose_object\":"<<action.pose
               <<",\"source\":\"native_evaluated_before_vr\",\"matrix_convention\":\"canonical_row_vector_metres\""
               <<",\"configured_mesh_binding_verified\":true,\"submitted_mesh_binding_verified\":false"
               <<",\"clip_guid\":null,\"clip_progress\":null,\"authoritative_native_reload_phase\":null,\"hidden_parts_omitted\":true,\"native_weapon\":";
            actionMatrix(action.nativeWeapon);out<<",\"native_left_wrist\":";actionMatrix(action.nativeLeftWrist);
            out<<",\"native_left_wrist_in_weapon\":";actionMatrix(action.wristInWeapon);
            out<<",\"native_left_fingers_in_wrist\":[";
            for(unsigned finger=0;finger<15;++finger){if(finger)out<<',';actionMatrix(action.fingersInWrist[finger]);}
            out<<"],\"native_action_parts\":[";
            for(unsigned part=0;part<action.partCount;++part){if(part)out<<',';out<<"{\"bone\":"<<std::quoted(action.parts[part].bone.data())<<",\"part_in_weapon\":";
                actionMatrix(action.parts[part].partInWeapon);out<<'}';}out<<"],\"native_firing_observation\":";
            if(!action.firing)out<<"null";
            else {const auto& firing=*action.firing;out<<"{\"sequence\":"<<firing.sequence<<",\"observed_ns\":"<<firing.observedNs
                <<",\"owner_equip_generation\":"<<firing.owner.equipGeneration<<",\"branches_agree_on_phase\":"<<(firing.branchesAgreeOnPhase?"true":"false")
                <<",\"atomic\":false,\"authoritative_branch_known\":false,\"branches\":[";
                for(unsigned branch=0;branch<2;++branch){if(branch)out<<',';const auto& f=firing.branches[branch];
                    out<<"{\"wrapper_offset\":"<<f.wrapperOffset<<",\"address\":"<<f.address<<",\"phase\":"<<unsigned(f.phase)
                       <<",\"state\":"<<f.currentState<<",\"previous_state\":"<<f.previousState<<",\"next_state\":"<<f.nextState
                       <<",\"phase_timer\":"<<f.phaseTimer<<",\"loaded\":"<<f.loaded<<",\"reserve\":"<<f.reserve<<'}';}out<<"]}";
            }out<<'}';
        }out<<'}';
    }out<<"],\"native_action_capture\":{\"requested\":"<<(NativeActionCaptureRequested()?"true":"false")
        <<",\"frame_limit\":"<<handRecords.size()<<",\"storage_bytes\":"<<sizeof(handRecords)<<",\"json_byte_upper_bound\":16777216,\"sample_interval_ms\":"<<(NativeActionCaptureRequested()?20:100)<<"},\"sight_preview\":{\"poses\":"<<sightPreviewPoses.load()<<",\"base_fallbacks\":"<<sightPreviewBaseFallbacks.load()<<",\"rejected\":"<<sightPreviewRejected.load()<<",\"records\":[";
    const auto matrix=[&](const math::Matrix4& m){out<<'[';for(unsigned n=0;n<16;++n){if(n)out<<',';out<<m.values[n/4][n%4];}out<<']';};
    const auto vector=[&](math::Vec3 p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};
    for(unsigned n=0;n<sightPreviewCount;++n){
        if(n)out<<',';const auto& r=sightPreviewRecords[(sightPreviewNext+sightPreviewRecords.size()-sightPreviewCount+n)%sightPreviewRecords.size()];
        out<<"{\"token\":"<<r.preview.token<<",\"generation\":"<<r.preview.generation<<",\"input_generation\":"<<r.inputGeneration<<",\"owner\":"<<r.owner<<",\"space\":"<<r.space<<",\"weapon\":"<<r.preview.weapon
           <<",\"physical_item\":"<<r.preview.physicalItem<<",\"grab_generation\":"<<r.preview.grabGeneration<<",\"phase\":"<<unsigned(r.preview.phase)
           <<",\"gesture_angle\":"<<r.preview.gestureAngle<<",\"native_progress\":"<<r.preview.nativeProgress<<",\"native_observed\":"<<(r.preview.nativeObserved?"true":"false")
           <<",\"raw_observed\":"<<(r.preview.rawObserved?"true":"false")<<",\"raw_progress\":"<<r.preview.rawProgress<<",\"raw_generation\":"<<r.preview.rawGeneration
           <<",\"angle\":"<<r.preview.angle<<",\"palm_error_m\":"<<r.palmError<<",\"reach_clamped\":"<<(r.clamped?"true":"false")<<",\"native_rear\":";matrix(r.nativeRear);
        out<<",\"native_front\":";matrix(r.nativeFront);out<<",\"rear\":";matrix(r.preview.rear);out<<",\"front\":";matrix(r.preview.front);
        out<<",\"wrist\":";matrix(r.preview.wrist);out<<",\"resolved_wrist\":";matrix(r.resolvedHand);out<<",\"raw_wrist\":";matrix(r.rawHand);
        out<<",\"palm_wrist\":";vector(r.preview.palmPointWrist);out<<",\"grasp\":";vector(r.preview.graspPoint);out<<'}';
    }
    out<<"]},\"hand_roles\":{\"binding_fingerprint\":\""<<std::hex<<handBindingFingerprint<<std::dec<<"\",\"rejected\":"<<handPoseRejected.load()<<",\"poses\":[";
    for(unsigned role=0;role<3;++role){if(role)out<<',';out<<handRolePoses[role].load();}
    out<<"],\"records\":[";
    for(unsigned n=0;n<handPoseCount;++n){
        if(n)out<<',';const auto& r=handPoseRecords[(handPoseNext+handPoseRecords.size()-handPoseCount+n)%handPoseRecords.size()];
        out<<"{\"generation\":"<<r.generation<<",\"owner\":"<<r.owner<<",\"space\":"<<r.space<<",\"weapon\":"<<r.weapon<<",\"role\":"<<unsigned(r.role)
           <<",\"squeeze\":"<<r.squeeze<<",\"trigger\":"<<r.trigger<<",\"touch_active\":"<<r.touchActive<<",\"touched\":"<<r.touched<<",\"wrist\":";matrix(r.wrist);out<<",\"grip\":";matrix(r.grip);
        out<<",\"mechanism_point\":";vector(r.mechanismPoint);out<<",\"fingers\":[";
        for(unsigned j=0;j<r.fingers.size();++j){if(j)out<<',';const auto& f=r.fingers[j];out<<"{\"index\":"<<f.index<<",\"parent\":"<<f.parent<<",\"reference\":";matrix(f.reference);out<<",\"native\":";matrix(f.native);out<<",\"posed\":";matrix(f.posed);out<<'}';}
        out<<"]}";
    }
    out<<"]},\"right_trigger\":{\"poses\":"<<triggerPoses.load()<<",\"failures\":"<<triggerFailures.load()<<",\"free_hand_binding_available\":"<<(rightHandBinding?"true":"false")<<",\"empty_hands_enabled\":false,\"mesh_trigger_enabled\":false,\"records\":[";
    for(unsigned n=0;n<triggerCount;++n){
        if(n)out<<',';const auto& t=triggerRecords[(triggerNext+triggerRecords.size()-triggerCount+n)%triggerRecords.size()];
        out<<"{\"generation\":"<<t.generation<<",\"weapon\":"<<t.weapon<<",\"trigger\":"<<t.trigger<<",\"wrist_preserved\":"<<(t.wristPreserved?"true":"false")<<",\"other_branches_preserved\":"<<(t.otherBranchesPreserved?"true":"false")<<",\"joints\":[";
        for(unsigned j=0;j<3;++j){if(j)out<<',';out<<"{\"index\":"<<t.indices[j]<<",\"before\":";matrix(t.before[j]);out<<",\"after\":";matrix(t.after[j]);out<<'}';}out<<"]}";
    }
    out<<"]},\"tracking_rejection_reasons\":{";for(unsigned n=0;n<rejectionCounts.size();++n){if(n)out<<',';out<<'"'<<rejectionNames[n]<<"\":"<<rejectionCounts[n].load();}out<<"}}";}
}
