#pragma once
#include "Bc2ReloadDrawCapture.h"
#include "Bc2ReloadProducerBinding.h"
#include "Bc2Rig.h"
#include <filesystem>

namespace fvr::bc2::nativeHandCapture {
struct FrozenOwner {
    std::uint64_t world=0,request=0,actor=0,weak=0,weapon=0,owner=0,equipment=0,
        space=0,rigPose=0,input=0,packedHash=0;
    unsigned frame=0;
    bool operator==(const FrozenOwner&)const=default;
};
inline std::optional<FrozenOwner> ExactOwner(const ReloadDrawFrameEvidence& frame)noexcept {
    const auto& p=frame.producer;
    if(!frame.world||!frame.request||!frame.view||!frame.nativeFrame||frame.eye>1||
       !p.exactRequestAssociation||p.request!=frame.request||p.nativeFrame!=frame.nativeFrame||
       p.rigFingerprint!=0xa7f219a1426216abull||!p.rigPose||!p.actor||!p.weak||!p.weapon||
       !p.ownerGeneration||!p.physicalEquipmentGeneration||!p.space||!p.inputGeneration||!p.packedPaletteHash||
       p.observedNs<=0||p.observedNs>frame.nowNs||p.deadlineNs<=frame.nowNs||p.deadlineNs-p.observedNs>250000000)return {};
    return FrozenOwner{frame.world,frame.request,p.actor,p.weak,p.weapon,p.ownerGeneration,p.physicalEquipmentGeneration,
        p.space,p.rigPose,p.inputGeneration,p.packedPaletteHash,frame.nativeFrame};
}
// Retries may advance frame/input/hash, never the selected native player/rig.
inline bool SameSelection(const FrozenOwner& a,const FrozenOwner& b)noexcept {
    return a.world==b.world&&a.request==b.request&&a.actor==b.actor&&a.weak==b.weak&&
        a.weapon==b.weapon&&a.owner==b.owner&&a.equipment==b.equipment&&
        a.space==b.space&&a.rigPose==b.rigPose;
}
enum class CandidateStartDecision {Admit,WrongEye,SelectionMismatch,RetiredFrame};
inline CandidateStartDecision CandidateStart(const FrozenOwner& candidate,unsigned eye,
    const std::optional<FrozenOwner>& selection,unsigned retiredFrame)noexcept {
    if(eye!=0)return CandidateStartDecision::WrongEye;
    if(selection&&!SameSelection(*selection,candidate))return CandidateStartDecision::SelectionMismatch;
    if(retiredFrame&&candidate.frame<=retiredFrame)return CandidateStartDecision::RetiredFrame;
    return CandidateStartDecision::Admit;
}
struct CandidateEyes {
    FrozenOwner owner{};std::array<std::uint64_t,2> views{};
    unsigned admitted=0,completed=0,catalogCandidates=0;
    bool Admit(const ReloadDrawFrameEvidence& frame)noexcept {
        const auto exact=ExactOwner(frame);if(!exact||*exact!=owner||(completed&(1u<<frame.eye)))return false;
        if((views[frame.eye]&&views[frame.eye]!=frame.view)||views[1-frame.eye]==frame.view)return false;
        views[frame.eye]=frame.view;admitted|=1u<<frame.eye;return true;
    }
    bool Complete(const ReloadDrawFrameEvidence& frame)noexcept {
        const auto exact=ExactOwner(frame);if(!exact||*exact!=owner||!(admitted&(1u<<frame.eye))||
            views[frame.eye]!=frame.view||(completed&(1u<<frame.eye)))return false;
        completed|=1u<<frame.eye;return true;
    }
    bool RetryEmpty()const noexcept{return admitted==3&&completed==3&&catalogCandidates==0;}
};
inline bool InstanceTransformSupported(bool apiObserved,unsigned instances,unsigned startInstance)noexcept {
    return apiObserved&&instances==1&&startInstance==0;
}
inline bool CandidateBusyScope(unsigned activeEyeThread,unsigned currentThread)noexcept {
    return activeEyeThread!=0&&activeEyeThread==currentThread;
}
// Explicit diagnostic only. Registrars observe public SDK calls and preserve
// originals once. All game-derived bytes are kept exclusively in this local
// capture folder; Report returns scalar counts/gaps, never raw bytes/angles.
// Actual native eye observation only; never animator or selected-player authority.
inline bool MaterialEyeMatches(const ReloadDrawFrameEvidence& a,const ReloadDrawFrameEvidence& b)noexcept {
    return a.world&&a.request&&a.view&&a.nativeFrame&&a.eye<2&&a.nowNs>0&&
        a.world==b.world&&a.request==b.request&&a.view==b.view&&
        a.nativeFrame==b.nativeFrame&&a.eye==b.eye&&a.nowNs==b.nowNs;
}
bool Configure(const std::filesystem::path& reportFolder,void* device,void* immediateContext,std::int64_t now,
    bool materialObservation=false)noexcept;
bool BeginMaterialEye(const ReloadDrawFrameEvidence&)noexcept;
void EndMaterialEye()noexcept;
bool StartEarlySdkObservers()noexcept; // only the six owned public SDK entries
bool SameSdkDevice(void* device,void* immediateContext)noexcept;
bool Enabled()noexcept;
bool AdmitEye(const ReloadDrawFrameEvidence&)noexcept;
// The original admitted frame must be retained across the native eye call.
// True requests an EMPTY general capture window reset, still bounded by the
// original global deadline. A matching count, even unsupported, prevents retry.
bool EndEye(const ReloadDrawFrameEvidence&)noexcept;
void CountCandidate(ID3D11DeviceContext*,unsigned count,unsigned instances=1,
    unsigned startInstance=0,bool indexedInstanced=false)noexcept;
std::span<const WeaponDrawSection> Catalog();
bool WantPacked(bool first,std::uint64_t serial,const ReloadProducerView&,const ReloadPackedSource&,std::int64_t now)noexcept;
void ObservePacked(bool first,std::uint64_t serial,const ReloadProducerView&,const ReloadPackedSource&,const RigSnapshot&,
    std::uint64_t destination,std::span<const std::byte> packed,std::int64_t now)noexcept;
void Pipeline(ID3D11DeviceContext*,const ReloadDrawCapturePacket&)noexcept;
void IndexedReturned(ID3D11DeviceContext*,unsigned count,unsigned start,std::int32_t base)noexcept;
void Complete(const ReloadDrawCapturePacket&)noexcept;
void Poll(ID3D11DeviceContext*)noexcept;
bool Stop()noexcept; // closes admission, disables only owned registrar entries, no wait
void WriteLocal()noexcept; // after Stop/drain only
void Report(std::ostream&);
} // namespace fvr::bc2::nativeHandCapture
