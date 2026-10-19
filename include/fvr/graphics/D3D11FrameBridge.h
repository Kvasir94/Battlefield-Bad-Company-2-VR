#pragma once
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11LegacyRelay.h"
#include "fvr/graphics/D3D11DepthCopy.h"
#include "fvr/ipc/StagedFrameProducer.h"
#include <wrl/client.h>
#include <array>
#include <atomic>
namespace fvr::graphics {
enum class FrameSharingPreference {Automatic,LegacyRelay,LegacyIpc};
// Reusable native-renderer transport: no game layout, hooks, or XR SDK types.
// All GPU/setup calls belong to the original immediate-context thread.
class D3D11FrameBridge final:public runtime::IFrameProvider {
public:
    D3D11FrameBridge():channel_(*this){}
    ~D3D11FrameBridge(){CloseGraphics();}
    bool ConnectGraphics(ID3D11Device*,const std::wstring&,FrameSharingPreference preference=FrameSharingPreference::Automatic)noexcept;
    bool TryBegin(const ipc::NativeFrameKey&,ipc::FrameLease&)noexcept;
    ipc::ChannelResult ReadInput(interaction::InputFrame& out,std::int64_t* deadlineQpc=nullptr)noexcept{return channel_.ReadInput(out,deadlineQpc);}
    bool PublishFeedback(const interaction::FeedbackEvent& event)noexcept{return channel_.PublishFeedback(event);}
    // Late latch (see ipc::StagedFrameProducer::Relatch). No GPU work.
    bool ReadPose(runtime::TrackingFrame& out)noexcept{return connected_.load(std::memory_order_acquire)&&channel_.ReadPose(out);}
    ipc::ChannelResult Relatch(ipc::FrameLease& lease,const runtime::TrackingFrame& tracking)noexcept{return channel_.Relatch(lease,tracking);}
    bool CaptureEye(unsigned,ID3D11Texture2D*,const ipc::FrameLease&)noexcept;
    // Scene depth for XR depth layers, right after CaptureEye of the same eye.
    // Optional: a failure never affects the color pair. Planes are metres.
    bool DepthWanted(const ipc::FrameLease& lease)const noexcept{return depthEnabled_&&!depthDisabled_&&lease.depthRequested&&sharingMode_.load()==1;}
    bool CaptureEyeDepth(unsigned eye,ID3D11Texture2D* depth,float nearMeters,float farMeters,const ipc::FrameLease&)noexcept;
    ipc::ChannelResult SetBodyProps(const ipc::FrameLease& lease,unsigned eye,const BodyPropEye& props)noexcept {
        return channel_.SetBodyProps(lease,eye,props);
    }
    bool PublishRestored(const ipc::FrameLease&)noexcept;
    void Cancel(const ipc::FrameLease&)noexcept;
    void PumpGraphics()noexcept;
    void CloseGraphics()noexcept;
    std::uint64_t Published()const noexcept{return published_.load();}
    std::uint64_t Consumed()const noexcept{return consumed_.load();}
    std::uint64_t Discarded()const noexcept{return discarded_.load();}
    std::uint64_t Reclaimed()const noexcept{return reclaimed_.load();} // Discards that reused the pair (no recreation).
    // Captures that used a pair other than pair 0; captures dropped because every allowed pair was in flight.
    std::uint64_t SecondPairCaptures()const noexcept{return secondPair_.load();}
    std::uint64_t PairsBusy()const noexcept{return pairsBusy_.load();}
    // Frame-delivery telemetry (docs/HEADSET-FRAME-DELIVERY-20261011.md). eyeFrameMismatch:
    // pairs refused because the two private eye images did not both hold the lease's
    // native frame (left and right from different game frames). Export: time from
    // native publication (both eyes captured, state restored) to the fenced/relay GPU
    // copy completing and the pair becoming publishable; zero samples for keyed sharing,
    // which publishes synchronously.
    // lockRetries/lockDrops: captures or publications that found the producer gate
    // busy and succeeded after the bounded spin / still had to drop the frame.
    // unqueuedRefusals: CaptureEye refused because an active legacy export had not
    // yet queued its copy out of the shared private eyes (H2b guard; only with
    // FVR_BC2_PER_PAIR_EYES=0, since each legacy pair otherwise has its own eyes).
    struct DeliveryStats {std::uint64_t eyeFrameMismatch=0,exports=0,exportTotalUs=0,exportMaxUs=0,lockRetries=0,lockDrops=0,unqueuedRefusals=0;unsigned pairLimit=0,lockSpinUs=0;bool queueAtPublish=true,perPairEyes=true;};
    DeliveryStats Delivery()const noexcept{return {eyeFrameMismatch_.load(),exports_.load(),exportTotalUs_.load(),exportMaxUs_.load(),lockRetries_.load(),lockDrops_.load(),unqueuedRefusals_.load(),pairLimit_,lockSpinUs_,queueAtPublish_,perPairEyes_};}
    // Depth submission evidence (FVR_XR_DEPTH_SUBMIT=0 in the game disables).
    struct DepthStats {std::uint64_t captured=0,published=0,captureFailures=0,publishFailures=0;unsigned stage=0;long error=0;bool enabled=false,disabled=false;};
    DepthStats Depth()const noexcept{return {depthCaptured_.load(),depthPublished_.load(),depthCaptureFailures_.load(),depthPublishFailures_.load(),depthStage_.load(),depthError_.load(),depthEnabled_.load(),depthDisabled_.load()};}
    unsigned FailureStage()const noexcept{return failureStage_.load();}
    long FailureCode()const noexcept{return failureCode_.load();}
    std::uint64_t CapturedEyes()const noexcept{return capturedEyes_.load();}
    long Compatibility(unsigned index)const noexcept{return compatibility_[index].load();}
    unsigned DeviceFlags()const noexcept{return deviceFlags_.load();}
    unsigned FeatureLevel()const noexcept{return featureLevel_.load();}
    unsigned FailureOperation()const noexcept{return failureOperation_.load();}
    // Stage 2 only: which source checks failed (1 device, 2 width, 4 height, 8 mips,
    // 16 array, 32 samples, 64 format) and the rejected source's description.
    struct SourceMismatch {unsigned mask=0,width=0,height=0,format=0,mips=0,array=0,samples=0;};
    SourceMismatch RejectedSource()const noexcept{return rejected_;}
    unsigned SharingMode()const noexcept{return sharingMode_.load();}
    bool Fatal()const noexcept{const auto stage=failureStage_.load();return (stage>=2&&stage<=5)||stage>=10;}
    bool Connected()const noexcept{return connected_.load(std::memory_order_acquire);}
private:
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,TextureDescriptor&,PairTicket&)noexcept override{return false;}
    void PairConsumed(const PairTicket&,bool)noexcept override;
    D3D11LegacyRelay relay_;bool forceLegacy_=false,forceLegacyIpc_=false,waitingGameCopies_=false;
    std::atomic<unsigned> sharingMode_=0;
    ipc::StagedFrameProducer channel_;
    // One pair per request slot the producer may hold, so the game can fill frame
    // N+k while the host still owns older frames. pairLimit_ (2 for pipeline depth
    // 1..2, else FrameSlotCount) bounds them; pairs are created on first use.
    // The helper-device relay (mode 2) alone uses pair 0 only.
    static constexpr unsigned PairSlots=ipc::FrameSlotCount;
    std::array<D3D11PairProducer,PairSlots> producers_;std::array<bool,PairSlots> inFlight_{};unsigned current_=0,pairLimit_=2;
    D3D11PairProducer& Producer()noexcept{return producers_[current_];}
    int PairIndex(const PairTicket&)const noexcept;
    // copyQueued: the private-eye -> shared-pair copy is on the immediate context, so
    // the private eyes may be overwritten by the next frame (D3D11 orders the copies).
    struct LegacyExport {ipc::FrameLease lease{};PairTicket ticket{};bool active=false,ready=false,failed=false,copyQueued=false;std::int64_t startQpc=0;};
    std::array<LegacyExport,PairSlots> legacyExports_{};
    void PumpLegacyExport(unsigned pair)noexcept;
    // FVR_BC2_EXPORT_QUEUE_AT_PUBLISH=0: queue the legacy copy at the next pump, as
    // before (CaptureEye then refuses until it is queued). FVR_BC2_BRIDGE_LOCK_SPIN_US
    // (default 500, 0 = old zero-wait drop): bounded spin on the producer gate.
    // FVR_BC2_PER_PAIR_EYES=0: every legacy pair shares one private eye set again
    // (guarded by copyQueued); default gives each fenced pair its own eye images so
    // a pair's source can never be overwritten by a later frame.
    bool queueAtPublish_=true,perPairEyes_=true;unsigned lockSpinUs_=500;
    bool DrainFeedbackBounded()noexcept;
    // Depth attachment: private R32F eye images and one depth pair per color pair.
    D3D11DepthCapture depthCapture_;std::array<D3D11PairProducer,PairSlots> depthProducers_;
    std::array<PairTicket,PairSlots> depthTickets_{};std::array<bool,PairSlots> depthInFlight_{};
    std::array<float,4> nearFar_{};unsigned depthMask_=0;std::uint64_t depthRequest_=0;DepthAttachment pendingDepth_{};
    std::atomic<bool> depthEnabled_=true,depthDisabled_=false;
    void ReleaseDepth(unsigned pair)noexcept;
    void ResetPairs()noexcept;
    void PublishDepth(const ipc::FrameLease&)noexcept;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    // Private eye images per pair (fenced legacy with perPairEyes_), else set 0 only.
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2>,PairSlots> eyes_;D3D11_TEXTURE2D_DESC eyeDesc_{};
    unsigned EyeSet(unsigned pair)const noexcept{return perPairEyes_&&sharingMode_.load()==3&&pair<PairSlots?pair:0u;}
    runtime::PresentationRequirements allocated_{};std::uint64_t epoch_=0,capturedRequest_=0;unsigned mask_=0;
    ipc::FrameLease pending_{};PairTicket ticket_{};bool submitting_=false,canceling_=false;
    void DiagnoseSharing()noexcept;
    std::array<std::atomic<long>,9> compatibility_{};
    std::atomic<unsigned> deviceFlags_=0,featureLevel_=0,failureOperation_=0;
    SourceMismatch rejected_{};
    std::atomic<unsigned> failureStage_=0;std::atomic<long> failureCode_=0;std::atomic<std::uint64_t> capturedEyes_=0;
    bool Fail(unsigned stage,HRESULT code)noexcept{if(!failureStage_.load()){failureCode_.store(code);failureStage_.store(stage);}return false;}
    std::atomic<bool> connected_=false;
    std::atomic<std::uint64_t> published_=0,consumed_=0,discarded_=0,reclaimed_=0,secondPair_=0,pairsBusy_=0;
    std::array<std::array<std::uint64_t,2>,PairSlots> eyeFrame_{}; // Native frame id each private eye image holds (graphics thread).
    std::int64_t exportStartQpc_=0;
    std::atomic<std::uint64_t> eyeFrameMismatch_=0,exports_=0,exportTotalUs_=0,exportMaxUs_=0,lockRetries_=0,lockDrops_=0,unqueuedRefusals_=0;
    std::int64_t ExportStarted()noexcept;void ExportFinished(std::int64_t start)noexcept;
    std::atomic<std::uint64_t> depthCaptured_=0,depthPublished_=0,depthCaptureFailures_=0,depthPublishFailures_=0;
    std::atomic<unsigned> depthStage_=0;std::atomic<long> depthError_=0;
};
}
