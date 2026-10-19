#include "fvr/xr/OpenXrHost.h"
#include "fvr/xr/ReloadAudioMixer.h"
#include "fvr/ipc/ReloadAudioChannel.h"
#include "fvr/runtime/RetainedPresentation.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11DepthCopy.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_PLATFORM_WIN32
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <type_traits>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <memory>
#include "OpenXrInput.h"
#include "OpenXrMenu.h"
#include "XrRenderViews.h"
#include "XrDeliveryTelemetry.h"
#include "fvr/interaction/RecenterPolicy.h"
#include "fvr/runtime/InputTape.h"

namespace fvr::xr {
using Microsoft::WRL::ComPtr;
namespace {
#define XR_FUNCTIONS(F) \
 F(DestroyInstance) F(GetInstanceProperties) F(GetSystem) F(GetSystemProperties) \
 F(GetD3D11GraphicsRequirementsKHR) F(EnumerateViewConfigurationViews) F(EnumerateEnvironmentBlendModes) \
 F(CreateSession) F(DestroySession) F(PollEvent) F(BeginSession) F(EndSession) F(RequestExitSession) \
 F(CreateReferenceSpace) F(DestroySpace) F(LocateSpace) F(LocateViews) \
 F(EnumerateSwapchainFormats) F(CreateSwapchain) F(DestroySwapchain) F(EnumerateSwapchainImages) \
 F(AcquireSwapchainImage) F(WaitSwapchainImage) F(ReleaseSwapchainImage) F(WaitFrame) F(BeginFrame) F(EndFrame)
struct Api {
    PFN_xrGetInstanceProcAddr get=nullptr;
#define FIELD(name) PFN_xr##name name=nullptr;
    XR_FUNCTIONS(FIELD)
#undef FIELD
    template<class T>void Load(XrInstance instance,const char* name,T& function){
        PFN_xrVoidFunction raw=nullptr;const auto result=get(instance,name,&raw);
        if(XR_FAILED(result)||!raw)throw std::runtime_error(std::string("Cannot resolve ")+name);
        function=reinterpret_cast<T>(raw);
    }
    void LoadInstance(XrInstance instance){
#define LOAD(name) Load(instance,"xr" #name,name);
        XR_FUNCTIONS(LOAD)
#undef LOAD
    }
};
void Check(XrResult result,const char* operation){if(XR_FAILED(result))throw std::runtime_error(std::string(operation)+" XrResult="+std::to_string(result));}
void Hr(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(std::uint32_t(result)));}
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
math::Pose Pose(const XrPosef& pose){return {{pose.position.x,pose.position.y,pose.position.z},{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w}};}
std::int64_t BodyPropClockNs()noexcept {LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
    return (q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
struct Host {
    Api api;
    ipc::ReloadAudioChannel* audioChannel=nullptr;
    std::unique_ptr<ReloadAudioMixer> audioMixer;
    ipc::ReloadAudioPolicy audioPolicy;
    ipc::ReloadAudioOwnerSnapshot audioOwnerSnapshot;
    float audioGain=.8f;
    void AudioStats()noexcept {if(audioMixer){const auto& a=audioMixer->Statistics();report.reloadAudioPlayed=a.played;report.reloadAudioDropped=a.dropped;report.reloadAudioStops=a.stops;if(a.error)report.reloadAudioError=a.error;}}
    void StopAudio()noexcept {if(audioChannel)audioChannel->AcknowledgeReady({},false);if(audioMixer)audioMixer->Stop();audioOwnerSnapshot={};audioPolicy.Update({},spaceGeneration,inputGeneration,BodyPropClockNs(),false);AudioStats();}
    void DispatchAudio(const interaction::InputFrame& input)noexcept {
        if(!audioMixer||!audioChannel)return;
        const auto now=BodyPropClockNs();
        const bool active=input.focused&&input.headValid&&input.hands[0].gripTracked&&input.hands[1].gripTracked&&interaction::ValidInput(input);
        for(unsigned i=0;i<16;++i){ipc::ReloadAudioOwner owner;ipc::ReloadAudioEvent event;
            bool busy=false;const bool connected=audioChannel->Poll(owner,event,&busy);
            const auto snapshot=audioOwnerSnapshot.Observe(connected,busy,owner);
            if(audioPolicy.Update(snapshot,input.spaceGeneration,input.generation,now,active))audioMixer->Stop();
            if(!event.id)break;
            if(audioPolicy.Accept(event)){event.gain*=audioGain;audioMixer->Play(event,input.head);}else ++report.reloadAudioRejected;
        }
        const auto owner=audioOwnerSnapshot.Current();
        const bool ready=active&&ipc::ValidReloadAudioOwner(owner,now)&&owner.space==input.spaceGeneration;
        if(ready)audioMixer->UpdateHead(input.head,&input.hands[1].grip.position);AudioStats();
        audioChannel->AcknowledgeReady(owner,ready&&report.reloadAudioReady&&!audioMixer->Statistics().error);
    }

    HMODULE loader=nullptr;
    XrInstance instance=XR_NULL_HANDLE;
    XrSystemId system=XR_NULL_SYSTEM_ID;
    XrSession session=XR_NULL_HANDLE;
    XrSpace local=XR_NULL_HANDLE,headSpace=XR_NULL_HANDLE;
    std::unique_ptr<OpenXrInput> controls;
    std::unique_ptr<OpenXrMenu> menu;
    interaction::RecenterGesture recenter;
    interaction::RecenterPolicy trackingRecovery;
    ULONGLONG lastFrameWall=0,presenceReturnAt=0;bool recoveryPending=false,userPresent=true,presenceKnown=false;
    std::ofstream eventLog;
    using Clock=std::chrono::steady_clock;
    Clock::time_point previousFrame{},lastTimingLog{};
    static unsigned TimingBucket(std::uint64_t us)noexcept {
        constexpr std::uint64_t limits[]{5000,11000,22000,50000,100000,150000,250000};
        unsigned n=0;while(n<7&&us>limits[n])++n;return n;
    }
    void Measure(unsigned stage,Clock::time_point start)noexcept {
        const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
        report.stageTotalUs[stage]+=us;report.stageMaxUs[stage]=(std::max)(report.stageMaxUs[stage],us);
    }
    void TimingLog(){
        const auto now=Clock::now();if(now-lastTimingLog<std::chrono::seconds(1))return;lastTimingLog=now;
        if(controls)controls->ReadProfiles(report.controllerCurrentProfiles,report.controllerProfileResults);
        if(!eventLog)return;
        FILETIME utc{};GetSystemTimeAsFileTime(&utc);
        eventLog<<"{\"event\":\"frame_timing\",\"wall_ms\":"<<GetTickCount64()
            <<",\"utc_filetime_100ns\":"<<((std::uint64_t(utc.dwHighDateTime)<<32)|utc.dwLowDateTime)
            <<",\"frames\":"<<report.endedFrames<<",\"fresh\":"<<report.submittedPairs<<",\"reused\":"<<report.reusedFrames
            <<",\"no_world_layer\":"<<report.blankFrames<<",\"menu_frames\":"<<report.menuFrames<<",\"tracked_hands\":"<<report.trackedHandFrames;
        auto values=[&](const char* name,const auto& data){eventLog<<",\""<<name<<"\":[";bool comma=false;for(auto v:data){if(comma)eventLog<<',';eventLog<<v;comma=true;}eventLog<<']';};
        // Stage order: xrWaitFrame, xrBeginFrame, tracking/input/menu, GPU pair
        // copy, props/mirror, XR image acquire/wait/copy/release, xrEndFrame,
        // controller Sample alone (also included in tracking/input/menu).
        values("stage_total_us",report.stageTotalUs);values("stage_max_us",report.stageMaxUs);
        values("controller_sampling",report.controllerSampling);
        values("pair_reject_reasons",report.pairRejectReasons);
        values("async_copy",report.asyncCopyEvidence);
        eventLog<<",\"last_pair_transfer_error\":"<<report.lastPairTransferError;
        eventLog<<",\"reload_audio\":{\"ready\":"<<report.reloadAudioReady<<",\"route\":\""<<report.reloadAudioRoute
            <<"\",\"played\":"<<report.reloadAudioPlayed<<",\"dropped\":"<<report.reloadAudioDropped<<",\"rejected\":"<<report.reloadAudioRejected
            <<",\"stops\":"<<report.reloadAudioStops<<",\"error\":"<<report.reloadAudioError<<'}';
        values("controller_profile_results",report.controllerProfileResults);
        eventLog<<",\"controller_current_profiles\":[\""<<report.controllerCurrentProfiles[0]<<"\",\""<<report.controllerCurrentProfiles[1]<<"\"]";
        values("frame_gap_buckets",report.frameGapBuckets);values("fresh_age_buckets",report.freshAgeBuckets);
        values("gated_frames",report.gatedFrames);
        values("copy_timeout_presentation",report.copyTimeoutPresentation);
        values("copy_latency_buckets",report.copyLatencyBuckets);values("late_copy_buckets",report.lateCopyBuckets);
        eventLog<<",\"refresh_hz\":"<<report.refreshCurrentHz<<",\"pace\":{\"on\":"<<(report.paceToContent?"true":"false")<<",\"waits\":"<<report.paceWaits
            <<",\"satisfied\":"<<report.paceSatisfied<<",\"timeouts\":"<<report.paceTimeouts<<",\"wait_total_us\":"<<report.paceWaitTotalUs<<",\"wait_max_us\":"<<report.paceWaitMaxUs<<'}';
        eventLog<<"}\n";eventLog.flush();
    }
    // Host companion tape: the final InputFrame of every XR frame, after recenter and
    // menu filtering, at XR rate. For the offline simulator and host-feature replay;
    // in-game replay uses the DLL gather recording (BC2NativeTrace --record-tape).
    std::ofstream tapeFile;runtime::InputTapeWriter tapeWriter;bool taping=false;
    void Log(const char* kind){if(eventLog){eventLog<<"{\"event\":\""<<kind<<"\",\"wall_ms\":"<<GetTickCount64()<<",\"state\":"<<int(sessionState)<<",\"space\":"<<spaceGeneration<<",\"present\":"<<(userPresent?"true":"false")<<",\"reference\":["<<referenceHead.position.x<<','<<referenceHead.position.y<<','<<referenceHead.position.z<<"],\"recenters\":"<<report.recenters<<",\"automatic_recenters\":"<<report.automaticRecenters<<"}\n";eventLog.flush();}}
    bool roomscale=false;float worldUnitsPerMeter=1;
    XrReferenceSpaceType referenceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    std::uint64_t inputGeneration=0;
    XrSessionState sessionState=XR_SESSION_STATE_UNKNOWN;
    std::array<XrSwapchain,2> swapchains{};
    std::array<std::vector<XrSwapchainImageD3D11KHR>,2> images;
    // Swapchain upload state. acquired/waited persist across frames only for an
    // image whose wait timed out; uploadedEyes records which eyes already hold the
    // current scratch pair, so an interrupted upload resumes (uploadHeld) instead
    // of ending the session.
    std::array<bool,2> acquired{},waited{};
    std::array<std::uint32_t,2> imageIndex{};unsigned uploadedEyes=0;
    bool uploadHeld=false;runtime::RetainedPresentation heldCandidate;runtime::TrackingFrame heldRendered{};
    std::uint64_t heldFrameId=0;Clock::time_point heldDelivered{};
    static constexpr XrDuration MinImageWaitNs=4000000,MaxImageWaitNs=33000000,DefaultFramePeriodNs=11000000;
    XrDuration imageWaitNs=DefaultFramePeriodNs;
    unsigned consecutiveFailures=0,consecutiveEndFailures=0;
    // FVR_XR_RETAIN_VISIBLE (default on): keep the retained world layer through a
    // dashboard (VISIBLE) and brief head loss. 0 restores reset-on-not-FOCUSED.
    bool retainVisible=true;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    // Opened shared pairs, by descriptor: pipelining rotates up to four color
    // pairs (one per request slot), and each may carry a depth pair. Reopening per
    // frame would cost an OpenSharedResourceByName each time (and a reopened color
    // pair resets the retained presentation), so a small LRU keeps them all open.
    struct OpenedPair {graphics::TextureDescriptor descriptor{};graphics::D3D11PairConsumer consumer;std::uint64_t used=0;};
    std::array<OpenedPair,8> opened{};std::uint64_t openUses=0;
    graphics::D3D11PairConsumer* OpenPair(const graphics::TextureDescriptor& descriptor,bool& reopened)noexcept {
        reopened=false;OpenedPair* slot=nullptr;
        for(auto& entry:opened)if(graphics::Valid(entry.descriptor)&&std::memcmp(&entry.descriptor,&descriptor,sizeof(descriptor))==0){slot=&entry;break;}
        if(!slot){
            for(auto& entry:opened)if(!slot||entry.used<slot->used)slot=&entry;
            reopened=true;slot->descriptor={};
            if(slot->consumer.Open(device.Get(),descriptor)!=graphics::TransferResult::Ok){report.lastPairTransferError=slot->consumer.LastError();slot->consumer.Reset();return nullptr;}
            slot->descriptor=descriptor;
        }
        slot->used=++openUses;return &slot->consumer;
    }
    void DropPair(graphics::D3D11PairConsumer* consumer)noexcept {
        for(auto& entry:opened)if(&entry.consumer==consumer){entry.consumer.Reset();entry.descriptor={};entry.used=0;}
    }
    std::uint64_t LateCopies()const noexcept {std::uint64_t n=0;for(const auto& entry:opened)n+=entry.consumer.LateCopies();return n;}
    graphics::D3D11PairConsumer* pendingCopyConsumer=nullptr;
    IFrameProvider* pendingCopyProvider=nullptr;
    graphics::PairTicket pendingCopyTicket{};
    Clock::time_point pendingCopyQueued{};
    // A timed-out copy is late, not wrong: its scratch->swapchain copies follow
    // it on this same immediate context, and the producer cannot rewrite the
    // source (awaiting ack, then retired by the negative ack while our queued
    // commands keep the allocation alive). Keeping the already-submitted pair
    // retained avoids no-world frames until the next pair. Kill switch:
    // FVR_XR_RETAIN_ON_COPY_TIMEOUT=0 restores the old reset.
    bool retainOnCopyTimeout=true,afterCopyTimeout=false;
    // Telemetry only: abandoned GPU events of timed-out copies, observed until
    // completion so a session shows whether they were late or never finished.
    struct LateCopy {ComPtr<ID3D11Query> query;Clock::time_point queued{};};
    std::array<LateCopy,4> lateCopies{};
    static std::uint64_t ElapsedUs(Clock::time_point since)noexcept {return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-since).count());}
    static unsigned CopyLatencyBucket(std::uint64_t us)noexcept {
        constexpr std::uint64_t limits[]{2000,5000,11000,17000,20000,25000,33000,50000};
        unsigned n=0;while(n<8&&us>limits[n])++n;return n;
    }
    static unsigned LateCopyBucket(std::uint64_t us)noexcept {
        constexpr std::uint64_t limits[]{20000,25000,33000,50000,100000,250000};
        unsigned n=0;while(n<6&&us>limits[n])++n;return n;
    }
    void WatchLateCopy(ID3D11Query* query,Clock::time_point queued)noexcept {
        if(query)for(auto& slot:lateCopies)if(!slot.query){slot.query=query;slot.queued=queued;return;}
        ++report.lateCopyBuckets[7];
    }
    void PollLateCopies()noexcept {
        for(auto& slot:lateCopies){
            if(!slot.query)continue;
            BOOL done=FALSE;const auto hr=context->GetData(slot.query.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const auto us=ElapsedUs(slot.queued);
            if(hr==S_OK&&done){++report.lateCopyBuckets[LateCopyBucket(us)];slot.query.Reset();}
            else if(FAILED(hr)||us>250000){++report.lateCopyBuckets[6];slot.query.Reset();}
        }
    }
    void RetirePendingCopy()noexcept {
        if(!pendingCopyProvider)return;
        auto* provider=pendingCopyProvider;const auto ticket=pendingCopyTicket;pendingCopyProvider=nullptr;
        DropPair(pendingCopyConsumer);pendingCopyConsumer=nullptr;retained.Reset();++report.asyncCopyEvidence[4];
        provider->Suspend();provider->PairConsumed(ticket,false);
    }
    void PollPendingCopy()noexcept {
        PollLateCopies();
        if(!pendingCopyProvider)return;
        auto& consumer=*pendingCopyConsumer;
        const auto started=Clock::now();const auto result=consumer.PollCopy(pendingCopyTicket);Measure(8,started);
        if(result==graphics::CopyPollResult::Pending){++report.asyncCopyEvidence[5];return;}
        auto* provider=pendingCopyProvider;const auto ticket=pendingCopyTicket;pendingCopyProvider=nullptr;pendingCopyConsumer=nullptr;
        const bool complete=result==graphics::CopyPollResult::Complete;
        if(complete){++report.asyncCopyEvidence[1];++report.copyLatencyBuckets[CopyLatencyBucket(ElapsedUs(pendingCopyQueued))];}
        else {
            const bool timedOut=result==graphics::CopyPollResult::TimedOut;
            ++report.asyncCopyEvidence[timedOut?2:3];
            report.lastPairTransferError=consumer.LastError();
            if(timedOut)WatchLateCopy(consumer.TimedOutCompletion(),pendingCopyQueued);
            DropPair(&consumer);
            if(timedOut&&retainOnCopyTimeout){afterCopyTimeout=true;++report.copyTimeoutPresentation[0];}
            else {afterCopyTimeout=timedOut;retained.Reset();}
        }
        provider->PairConsumed(ticket,complete);
    }
    runtime::RetainedPresentation retained;
    std::array<ComPtr<ID3D11Texture2D>,2> scratch;
    // ---- Depth layers (XR_KHR_composition_layer_depth) ----------------------------
    // FVR_XR_DEPTH_SUBMIT=0 disables. Enabled only when the runtime enumerates the
    // extension and creates a D32_FLOAT depth swapchain; any setup failure falls
    // back to color-only layers. The producer's R32_FLOAT window depth is copied
    // into private scratch with the color pair, then drawn into the depth swapchain
    // (SV_Depth) after the color upload of the SAME pair completes.
    bool depthOn=false;
    std::array<XrSwapchain,2> depthSwapchains{};
    std::array<std::vector<XrSwapchainImageD3D11KHR>,2> depthImages;
    std::array<std::vector<ComPtr<ID3D11DepthStencilView>>,2> depthViews;
    std::array<ComPtr<ID3D11Texture2D>,2> depthScratch;std::array<ComPtr<ID3D11ShaderResourceView>,2> depthScratchViews;
    graphics::D3D11DepthWriter depthWriter;
    std::array<bool,2> depthAcquired{},depthWaited{};std::array<std::uint32_t,2> depthIndex{};
    // The retained color pair's depth: valid only when both eyes of the SAME pair
    // reached the depth swapchains (planes in metres, eye0 near/far, eye1 near/far).
    struct RetainedDepth {bool valid=false;std::array<float,4> nearFar{};} retainedDepth,heldDepth;
    void DisableDepth(const char* why)noexcept {
        if(depthOn&&eventLog)eventLog<<"{\"event\":\"depth_layers_disabled\",\"wall_ms\":"<<GetTickCount64()<<",\"reason\":\""<<why<<"\"}\n";
        depthOn=false;report.depthLayers=false;retainedDepth={};
    }
    // Depth swapchains and resources. Leaves depthOn false (color-only) on any failure;
    // report.depthSetupFailure names the step: 1 no D32_FLOAT format, 2 swapchain,
    // 3 images, 4 image layout, 5 DSV, 6 scratch, 7 writer.
    void InitializeDepth(const std::vector<int64_t>& formats) {
        const auto& req=report.requirements;
        const auto fail=[&](std::uint32_t step){report.depthSetupFailure=step;depthOn=false;};
        if(std::find(formats.begin(),formats.end(),int64_t(DXGI_FORMAT_D32_FLOAT))==formats.end())return fail(1);
        for(unsigned eye=0;eye<2;++eye){
            XrSwapchainCreateInfo chain{XR_TYPE_SWAPCHAIN_CREATE_INFO};chain.usageFlags=XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            chain.format=DXGI_FORMAT_D32_FLOAT;chain.sampleCount=1;chain.width=req.width;chain.height=req.height;chain.faceCount=chain.arraySize=chain.mipCount=1;
            if(XR_FAILED(api.CreateSwapchain(session,&chain,&depthSwapchains[eye]))){depthSwapchains[eye]=XR_NULL_HANDLE;return fail(2);}
            std::uint32_t count=0;
            if(XR_FAILED(api.EnumerateSwapchainImages(depthSwapchains[eye],0,&count,nullptr))||!count||count>64)return fail(3);
            depthImages[eye].assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            if(XR_FAILED(api.EnumerateSwapchainImages(depthSwapchains[eye],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(depthImages[eye].data()))))return fail(3);
            depthViews[eye].assign(count,nullptr);
            for(std::uint32_t i=0;i<count;++i){
                auto* texture=depthImages[eye][i].texture;if(!texture)return fail(4);
                D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
                if(desc.Width<req.width||desc.Height<req.height||desc.SampleDesc.Count!=1||!(desc.BindFlags&D3D11_BIND_DEPTH_STENCIL))return fail(4);
                D3D11_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;
                if(desc.ArraySize>1){view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.ArraySize=1;}
                else view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
                if(FAILED(device->CreateDepthStencilView(texture,&view,&depthViews[eye][i])))return fail(5);
            }
            D3D11_TEXTURE2D_DESC scratchDesc{};scratchDesc.Width=req.width;scratchDesc.Height=req.height;scratchDesc.Format=DXGI_FORMAT_R32_FLOAT;
            scratchDesc.MipLevels=scratchDesc.ArraySize=1;scratchDesc.SampleDesc.Count=1;scratchDesc.Usage=D3D11_USAGE_DEFAULT;scratchDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device->CreateTexture2D(&scratchDesc,nullptr,&depthScratch[eye]))||
               FAILED(device->CreateShaderResourceView(depthScratch[eye].Get(),nullptr,&depthScratchViews[eye])))return fail(6);
        }
        if(!depthWriter.Initialize(context.Get()))return fail(7);
        depthOn=true;report.depthLayers=true;
    }
    // Copies the delivered pair's depth attachment into the private depth scratch.
    // Synchronous keyed copy only (the producer sends depth with named keyed pairs).
    bool CopyDepth(IFrameProvider* provider,const graphics::TextureDescriptor& color,const graphics::PairTicket& ticket,std::array<float,4>& nearFar)noexcept {
        if(!depthOn)return false;
        graphics::DepthAttachment depth{};
        if(!provider->ReadDepth(ticket,depth)){++report.depthMissing;return false;}
        if(!graphics::ValidDepth(depth,color,ticket)){++report.depthRejected;return false;}
        bool reopened=false;auto* consumer=OpenPair(depth.descriptor,reopened);
        if(!consumer){++report.depthCopyFailures;return false;}
        const std::array<graphics::TextureSlice,2> destinations{{{depthScratch[0].Get(),0},{depthScratch[1].Get(),0}}};
        if(consumer->Copy(depth.ticket,destinations)!=graphics::TransferResult::Ok){
            ++report.depthCopyFailures;report.lastPairTransferError=consumer->LastError();DropPair(consumer);return false;}
        nearFar=depth.nearFar;++report.depthPairs;return true;
    }
    // Per-eye acquire/wait/draw/release of the depth swapchains, after the color
    // upload of the same pair. A timed-out wait keeps its image acquired and is
    // finished on a later call; depth is then not attached until a pair fully lands.
    bool WriteDepthSwapchains(bool fresh)noexcept {
        bool complete=true;
        for(unsigned eye=0;eye<2;++eye){
            if(!depthAcquired[eye]){
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};std::uint32_t index=0;
                if(!Accept(api.AcquireSwapchainImage(depthSwapchains[eye],&acquire,&index),"xrAcquireSwapchainImage(depth)")){complete=false;continue;}
                depthAcquired[eye]=true;depthWaited[eye]=false;depthIndex[eye]=index;
            }
            if(!depthWaited[eye]){
                XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=imageWaitNs;
                if(!Accept(api.WaitSwapchainImage(depthSwapchains[eye],&imageWait),"xrWaitSwapchainImage(depth)")){complete=false;continue;}
                depthWaited[eye]=true;
            }
            const auto index=depthIndex[eye];
            if(!fresh||index>=depthViews[eye].size()||!depthWriter.Write(depthScratchViews[eye].Get(),depthViews[eye][index].Get(),report.requirements.width,report.requirements.height))complete=false;
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if(!Accept(api.ReleaseSwapchainImage(depthSwapchains[eye],&release),"xrReleaseSwapchainImage(depth)")){
                complete=false;DisableDepth("release_failed");
            }
            depthWaited[eye]=depthAcquired[eye]=false;
        }
        if(!complete)++report.depthUploadFailures;
        return complete&&fresh&&depthOn;
    }
    bool AnyDepthImageHeld()const noexcept {return depthAcquired[0]||depthAcquired[1];}
    unsigned depthEndFailures=0;
    RenderViewSampler renderViews; // Request prediction, overscan, late-latch lane.
    // Per-second fresh/reused/age/period/GPU-latency summary (FVR_XR_DELIVERY_LOG=0 disables).
    DeliveryTelemetry delivery;HostGpuTimer gpuTimer;
    // Pace-to-content state (see HostReport::paceToContent). paceArmed: the last
    // frame presented the world while focused, tracked and menu-free, so a wait
    // only ever replaces a stale re-submission. framePeriodNs: last runtime period.
    bool paceArmed=false;XrDuration framePeriodNs=0;
    PFN_xrRequestDisplayRefreshRateFB requestRefresh=nullptr;PFN_xrGetDisplayRefreshRateFB getRefresh=nullptr;
    std::unique_ptr<graphics::D3D11BodyPropCompositor> bodyProps;
    graphics::D3D11MirrorWindow mirror;bool mirrorOn=false,syntheticHands=false,ignorePresence=false;
    HostReport& report;
    bool running=false,exit=false,frameOpen=false,referenceValid=false;
    XrTime frameTime=0;
    std::vector<XrTime> spaceChanges;
    math::Pose referenceHead{};
    std::uint64_t trackingGeneration=0,spaceGeneration=1;
    explicit Host(HostReport& r):report(r){}
    ~Host(){
        StopAudio();audioMixer.reset();
        if(taping)tapeWriter.Finish();
        AbortFrame();RetirePendingCopy();for(auto& entry:opened){entry.consumer.Reset();entry.descriptor={};}
        // Release views of runtime-owned images before destroying their swapchains.
        for(auto& views:depthViews)views.clear();
        for(auto chain:depthSwapchains)if(chain&&api.DestroySwapchain)api.DestroySwapchain(chain);
        for(auto& slot:lateCopies)if(slot.query){++report.lateCopyBuckets[7];slot.query.Reset();} // Unresolved at shutdown.
        controls.reset();menu.reset();bodyProps.reset();
        for(auto chain:swapchains)if(chain&&api.DestroySwapchain)api.DestroySwapchain(chain);
        if(headSpace&&api.DestroySpace)api.DestroySpace(headSpace);
        if(local&&api.DestroySpace)api.DestroySpace(local);
        if(session&&api.DestroySession)api.DestroySession(session);
        context.Reset();device.Reset();
        if(instance&&api.DestroyInstance)api.DestroyInstance(instance);
        if(loader)FreeLibrary(loader);
    }
    void AbortFrame()noexcept {
        for(unsigned eye=0;eye<2;++eye)if(waited[eye]&&api.ReleaseSwapchainImage){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            api.ReleaseSwapchainImage(swapchains[eye],&release);waited[eye]=acquired[eye]=false;
        }
        for(unsigned eye=0;eye<2;++eye)if(depthWaited[eye]&&api.ReleaseSwapchainImage){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            api.ReleaseSwapchainImage(depthSwapchains[eye],&release);depthWaited[eye]=depthAcquired[eye]=false;
        }
        retainedDepth={};heldDepth={};
        if(frameOpen&&api.EndFrame){
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frameTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            api.EndFrame(session,&end);frameOpen=false;
        }
    }
    // XR_FB_display_refresh_rate: request the supported rate nearest the target.
    // Never fatal: a runtime that refuses keeps its own rate, which is reported.
    void RequestRefreshRate()noexcept {
        PFN_xrEnumerateDisplayRefreshRatesFB enumerateRates=nullptr;
        auto load=[&](const char* name,auto& function){PFN_xrVoidFunction raw=nullptr;
            if(XR_FAILED(api.get(instance,name,&raw))||!raw)return false;function=reinterpret_cast<std::remove_reference_t<decltype(function)>>(raw);return true;};
        if(!load("xrEnumerateDisplayRefreshRatesFB",enumerateRates)||!load("xrRequestDisplayRefreshRateFB",requestRefresh)||!load("xrGetDisplayRefreshRateFB",getRefresh)){report.refreshResult=XR_ERROR_FUNCTION_UNSUPPORTED;requestRefresh=nullptr;getRefresh=nullptr;return;}
        float current=0;if(XR_SUCCEEDED(getRefresh(session,&current)))report.refreshInitialHz=report.refreshCurrentHz=current;
        std::array<float,32> rates{};std::uint32_t count=0;
        auto result=enumerateRates(session,std::uint32_t(rates.size()),&count,rates.data());
        if(XR_FAILED(result)||!count||count>rates.size()){report.refreshResult=XR_FAILED(result)?result:XR_ERROR_SIZE_INSUFFICIENT;return;}
        float best=0;for(std::uint32_t i=0;i<count;++i){
            if(!report.refreshRates.empty())report.refreshRates+=',';report.refreshRates+=std::to_string(int(rates[i]+.5f));
            if(!best||std::fabs(rates[i]-report.refreshTargetHz)<std::fabs(best-report.refreshTargetHz))best=rates[i];}
        report.refreshRequestedHz=best;
        if(std::fabs(best-current)<.01f){report.refreshResult=XR_SUCCESS;return;}
        result=requestRefresh(session,best);report.refreshResult=result;
        if(XR_SUCCEEDED(result)&&XR_SUCCEEDED(getRefresh(session,&current)))report.refreshCurrentHz=current;
    }
    // Pace-to-content: before xrWaitFrame, give the game up to one display period
    // (or FVR_XR_PACE_MAX_WAIT_MS) to publish the next stereo pair, instead of
    // re-submitting the retained pair at the full refresh. Bounded: on the limit
    // the frame proceeds and re-submits exactly as without pacing.
    void PaceToContent(IFrameProvider* provider){
        if(!report.paceToContent||!paceArmed||!provider||pendingCopyProvider||uploadHeld||!provider->PacesContent())return;
        if(provider->CompletedPairReady())return;
        const auto limitUs=report.paceMaxWaitMs?std::int64_t(report.paceMaxWaitMs)*1000:
            std::clamp<std::int64_t>((framePeriodNs>0?framePeriodNs:DefaultFramePeriodNs)/1000,4000,50000);
        const auto start=Clock::now();++report.paceWaits;bool ready=false;
        for(;;){
            const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();
            if(elapsed>=limitUs)break;
            // Short slices: the wake event is a hint shared with request admission.
            provider->WaitForPublication(unsigned((std::min<std::int64_t>)(2,(limitUs-elapsed+999)/1000)));
            if(provider->CompletedPairReady()){ready=true;break;}
        }
        const auto waitedUs=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
        report.paceWaitTotalUs+=waitedUs;report.paceWaitMaxUs=(std::max)(report.paceWaitMaxUs,waitedUs);
        if(ready)++report.paceSatisfied;else ++report.paceTimeouts;
    }
    void Initialize(const HostOptions& options){
        audioChannel=options.reloadAudio;
        wchar_t audioEnabled[8]{};const auto audioEnabledLength=GetEnvironmentVariableW(L"FVR_BC2_RELOAD_AUDIO",audioEnabled,8);
        report.reloadAudioRequested=audioChannel&&!options.probeOnly&&!(audioEnabledLength==1&&audioEnabled[0]==L'0');
        {wchar_t gain[64]{};const auto n=GetEnvironmentVariableW(L"FVR_BC2_RELOAD_AUDIO_GAIN",gain,64);if(n&&n<64){wchar_t* end=nullptr;const auto v=std::wcstof(gain,&end);if(end==gain+n&&std::isfinite(v)&&v>=0&&v<=1)audioGain=v;}}

        wchar_t asyncCopySwitch[8]{};const auto length=GetEnvironmentVariableW(L"FVR_XR_ASYNC_CONSUMER_COPY",asyncCopySwitch,8);
        report.asyncConsumerCopy=!(length==1&&asyncCopySwitch[0]==L'0');
        wchar_t retainSwitch[8]{};const auto retainLength=GetEnvironmentVariableW(L"FVR_XR_RETAIN_ON_COPY_TIMEOUT",retainSwitch,8);
        retainOnCopyTimeout=report.retainOnCopyTimeout=!(retainLength==1&&retainSwitch[0]==L'0');
        wchar_t visibleSwitch[8]{};const auto visibleLength=GetEnvironmentVariableW(L"FVR_XR_RETAIN_VISIBLE",visibleSwitch,8);
        retainVisible=report.retainVisible=!(visibleLength==1&&visibleSwitch[0]==L'0');retained.SetRetainThroughGaps(retainVisible);
        renderViews.Configure(RenderViewPolicy::FromEnvironment());
        {wchar_t value[8]{};const auto chars=GetEnvironmentVariableW(L"FVR_XR_PACE_TO_CONTENT",value,8);report.paceToContent=!(chars==1&&value[0]==L'0');}
        {wchar_t value[16]{};const auto chars=GetEnvironmentVariableW(L"FVR_XR_PACE_MAX_WAIT_MS",value,16);
            if(chars>0&&chars<16){wchar_t* end=nullptr;const auto parsed=std::wcstoul(value,&end,10);if(end&&!*end&&parsed<=50)report.paceMaxWaitMs=unsigned(parsed);}}
        // Headset refresh target: FVR_XR_REFRESH_HZ (default 72; 0 = runtime setting).
        report.refreshTargetHz=72.f;
        {wchar_t value[16]{};const auto chars=GetEnvironmentVariableW(L"FVR_XR_REFRESH_HZ",value,16);
            if(chars>0&&chars<16){wchar_t* end=nullptr;const auto parsed=std::wcstod(value,&end);if(end&&!*end&&std::isfinite(parsed)&&parsed>=0&&parsed<=1000)report.refreshTargetHz=float(parsed);}}
        report.overscan=renderViews.Policy().overscan;report.requestPrediction=renderViews.Policy().predict;report.latePose=renderViews.Policy().latePose;
        Require(std::isfinite(options.worldUnitsPerMeter)&&options.worldUnitsPerMeter>=.01f&&options.worldUnitsPerMeter<=1000.f,"World scale must be .01..1000 native units per metre");
        roomscale=options.roomscale;worldUnitsPerMeter=options.worldUnitsPerMeter;syntheticHands=options.syntheticHands;ignorePresence=options.ignorePresence;
        if(!options.readyPath.empty()){eventLog.open(options.readyPath.parent_path()/L"xr-events.jsonl");Require(bool(eventLog),"Open XR event log");}
        delivery.Enable(eventLog.is_open()&&DeliveryTelemetry::EnabledFromEnvironment());
        if(!options.recordTape.empty()){
            Require(options.recordTape.is_absolute()&&!std::filesystem::exists(options.recordTape),"Recorded tape requires a new absolute path");
            tapeFile.open(options.recordTape,std::ios::binary);Require(bool(tapeFile),"Create recorded tape");
            Require(tapeWriter.Begin(tapeFile,11111,0,{"source=host_xr_frame world_units_per_meter="+std::to_string(options.worldUnitsPerMeter)+(options.roomscale?" roomscale=1":" roomscale=0"),
                "notes=XR-frame_rate_not_gather_rate"}),"Write recorded tape header");taping=true;
        }
        Require(options.loaderPath.is_absolute(),"OpenXR loader requires an absolute path");
        loader=LoadLibraryExW(options.loaderPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        Require(loader!=nullptr,"Cannot load the local x64 OpenXR loader");
        api.get=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
        Require(api.get!=nullptr,"OpenXR loader has no xrGetInstanceProcAddr");
        PFN_xrEnumerateInstanceExtensionProperties enumerate=nullptr;api.Load(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",enumerate);
        uint32_t count=0;Check(enumerate(nullptr,0,&count,nullptr),"Enumerate extension count");Require(count<=4096,"Invalid OpenXR extension count");
        std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});
        Check(enumerate(nullptr,count,&count,extensions.data()),"Enumerate extensions");
        bool d3d11=false,presenceExtension=false,depthExtension=false;for(const auto& extension:extensions){d3d11|=std::strcmp(extension.extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME)==0;presenceExtension|=std::strcmp(extension.extensionName,XR_EXT_USER_PRESENCE_EXTENSION_NAME)==0;
            depthExtension|=std::strcmp(extension.extensionName,XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME)==0;
            report.reloadAudioExtension|=std::strcmp(extension.extensionName,XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME)==0;
            report.refreshExtension|=std::strcmp(extension.extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)==0;}
        wchar_t depthSwitch[8]{};const auto depthLength=GetEnvironmentVariableW(L"FVR_XR_DEPTH_SUBMIT",depthSwitch,8);
        report.depthExtension=depthExtension;report.depthRequested=!(depthLength==1&&depthSwitch[0]==L'0');
        Require(d3d11,"Active runtime does not expose XR_KHR_D3D11_enable");
        PFN_xrCreateInstance create=nullptr;api.Load(XR_NULL_HANDLE,"xrCreateInstance",create);
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(info.applicationInfo.applicationName,"FrostbiteVR BC2 Host");strcpy_s(info.applicationInfo.engineName,"FrostbiteVR Modular");
        info.applicationInfo.applicationVersion=1;info.applicationInfo.engineVersion=1;
        // 2142 compatibility lesson: core 1.0 + advertised extensions, not an
        // unnecessary 1.1 requirement merely because the headers are newer.
        info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        std::array<const char*,5> enabled{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};std::uint32_t enabledCount=1;
        if(presenceExtension)enabled[enabledCount++]=XR_EXT_USER_PRESENCE_EXTENSION_NAME;
        // Depth layers only when the runtime has the extension; otherwise color-only.
        const bool depthWanted=depthExtension&&report.depthRequested;if(depthWanted)enabled[enabledCount++]=XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME;
        // Display refresh control only when a target is set and the runtime offers it
        // (Meta/Oculus and Virtual Desktop runtimes do; SteamVR sets it in its own UI).
        const bool refreshWanted=report.refreshExtension&&report.refreshTargetHz>0&&!options.probeOnly;if(refreshWanted)enabled[enabledCount++]=XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
        if(report.reloadAudioRequested&&report.reloadAudioExtension)enabled[enabledCount++]=XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME;
        info.enabledExtensionCount=enabledCount;info.enabledExtensionNames=enabled.data();
        Check(create(&info,&instance),"Create OpenXR instance");report.instanceCreated=true;api.LoadInstance(instance);
        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};Check(api.GetInstanceProperties(instance,&props),"Runtime properties");report.runtimeName=props.runtimeName;
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        Check(api.GetSystem(instance,&systemInfo,&system),"Find HMD system");report.systemAvailable=true;
        XrSystemUserPresencePropertiesEXT presence{XR_TYPE_SYSTEM_USER_PRESENCE_PROPERTIES_EXT};
        XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};if(presenceExtension)systemProps.next=&presence;
        Check(api.GetSystemProperties(instance,system,&systemProps),"System properties");report.systemName=systemProps.systemName;report.userPresenceSupported=presenceExtension&&presence.supportsUserPresence;
        XrGraphicsRequirementsD3D11KHR graphics{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        Check(api.GetD3D11GraphicsRequirementsKHR(instance,system,&graphics),"D3D11 graphics requirements");
        report.requirements.adapterLow=graphics.adapterLuid.LowPart;report.requirements.adapterHigh=graphics.adapterLuid.HighPart;report.minimumFeatureLevel=graphics.minFeatureLevel;
        std::array<XrViewConfigurationView,2> viewInfo{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        Check(api.EnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,viewInfo.data()),"Stereo view configuration");
        Require(count==2,"Runtime must expose two primary stereo views");
        auto& req=report.requirements;req.width=(std::max)(viewInfo[0].recommendedImageRectWidth,viewInfo[1].recommendedImageRectWidth);
        req.height=(std::max)(viewInfo[0].recommendedImageRectHeight,viewInfo[1].recommendedImageRectHeight);
        Require((options.eyeWidth==0)==(options.eyeHeight==0),"Both eye dimensions are required for an override");
        if(options.eyeWidth){req.width=options.eyeWidth;req.height=options.eyeHeight;}
        for(const auto& view:viewInfo)Require(req.width&&req.height&&req.width<=view.maxImageRectWidth&&req.height<=view.maxImageRectHeight,"Incompatible per-eye image sizes");
        if(options.probeOnly)return;
        if(report.reloadAudioRequested){
            std::wstring endpoint;wchar_t overrideId[4096]{};const auto n=GetEnvironmentVariableW(L"FVR_BC2_RELOAD_AUDIO_DEVICE",overrideId,4096);
            report.reloadAudioRoute="windows-default";
            if(n&&n<4096){endpoint=overrideId;report.reloadAudioRoute="launcher-endpoint";}
            else if(report.reloadAudioExtension){
                PFN_xrVoidFunction raw=nullptr;
                if(XR_SUCCEEDED(api.get(instance,"xrGetAudioOutputDeviceGuidOculus",&raw))&&raw){
                    wchar_t id[XR_MAX_AUDIO_DEVICE_STR_SIZE_OCULUS]{};
                    const auto result=reinterpret_cast<PFN_xrGetAudioOutputDeviceGuidOculus>(raw)(instance,id);
                    if(XR_SUCCEEDED(result)&&id[0]&&std::wmemchr(id,0,XR_MAX_AUDIO_DEVICE_STR_SIZE_OCULUS)){endpoint=id;report.reloadAudioRoute="openxr-endpoint";}
                }
            }
            audioMixer=std::make_unique<ReloadAudioMixer>();
            report.reloadAudioReady=audioMixer->Initialize(options.reloadAudioDirectory,endpoint);
            AudioStats();if(!report.reloadAudioReady)audioMixer.reset();
            if(eventLog)eventLog<<"{\"event\":\"reload_audio_setup\",\"ready\":"<<report.reloadAudioReady<<",\"route\":\""<<report.reloadAudioRoute<<"\",\"error\":"<<report.reloadAudioError<<"}\n";
        }

        Require(req.width<=16384&&req.height<=16384,"Runtime eye dimensions exceed the D3D11 bridge limits");
        std::array<XrEnvironmentBlendMode,16> modes{};
        Check(api.EnumerateEnvironmentBlendModes(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,UINT(modes.size()),&count,modes.data()),"Environment blend modes");
        Require(count<=modes.size()&&std::find(modes.begin(),modes.begin()+count,XR_ENVIRONMENT_BLEND_MODE_OPAQUE)!=modes.begin()+count,"Opaque HMD presentation required");
        ComPtr<IDXGIFactory1> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");ComPtr<IDXGIAdapter1> selected;
        for(UINT index=0;;++index){ComPtr<IDXGIAdapter1> candidate;const auto result=factory->EnumAdapters1(index,&candidate);
            if(result==DXGI_ERROR_NOT_FOUND)break;Hr(result,"Enumerate graphics adapter");DXGI_ADAPTER_DESC1 desc{};Hr(candidate->GetDesc1(&desc),"Graphics adapter properties");
            if(desc.AdapterLuid.LowPart==graphics.adapterLuid.LowPart&&desc.AdapterLuid.HighPart==graphics.adapterLuid.HighPart){selected=candidate;break;}}
        Require(bool(selected),"OpenXR-required graphics adapter not found");
        std::vector<D3D_FEATURE_LEVEL> levels;for(auto level:{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0})if(level>=graphics.minFeatureLevel)levels.push_back(level);
        Require(!levels.empty(),"Runtime requires an unsupported D3D11 feature level");D3D_FEATURE_LEVEL level{};
        Hr(D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels.data(),UINT(levels.size()),D3D11_SDK_VERSION,&device,&level,&context),"Create XR graphics device");
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device.Get();
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&binding;sessionInfo.systemId=system;
        Check(api.CreateSession(instance,&sessionInfo,&session),"Create OpenXR session");report.sessionCreated=true;
        if(refreshWanted)RequestRefreshRate();
        if(options.controllers){controls=std::make_unique<OpenXrInput>();controls->Initialize(instance,session,api.get);report.controllersEnabled=true;report.controllerProfiles=controls->ProfilesSuggested();}
        if(roomscale){
            PFN_xrEnumerateReferenceSpaces enumerateSpaces=nullptr;api.Load(instance,"xrEnumerateReferenceSpaces",enumerateSpaces);
            std::array<XrReferenceSpaceType,16> spaces{};std::uint32_t spaceCount=0;Check(enumerateSpaces(session,UINT(spaces.size()),&spaceCount,spaces.data()),"Enumerate tracking spaces");
            Require(spaceCount<=spaces.size(),"Invalid tracking space count");
            if(std::find(spaces.begin(),spaces.begin()+spaceCount,XR_REFERENCE_SPACE_TYPE_STAGE)!=spaces.begin()+spaceCount)referenceType=XR_REFERENCE_SPACE_TYPE_STAGE;
        }
        report.floorRelative=referenceType==XR_REFERENCE_SPACE_TYPE_STAGE;
        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};spaceInfo.poseInReferenceSpace.orientation.w=1;spaceInfo.referenceSpaceType=referenceType;
        Check(api.CreateReferenceSpace(session,&spaceInfo,&local),"Create tracking reference space");spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
        Check(api.CreateReferenceSpace(session,&spaceInfo,&headSpace),"Create VIEW reference space");
        Check(api.EnumerateSwapchainFormats(session,0,&count,nullptr),"Swapchain format count");Require(count<=4096,"Invalid swapchain format count");
        std::vector<int64_t> formats(count);Check(api.EnumerateSwapchainFormats(session,count,&count,formats.data()),"Swapchain formats");
        for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM})if(std::find(formats.begin(),formats.end(),int64_t(format))!=formats.end()){req.format=format;break;}
        Require(req.format!=0,"Runtime has no supported RGBA8/BGRA8 swapchain format");
        for(unsigned eye=0;eye<2;++eye){
            XrSwapchainCreateInfo chain{XR_TYPE_SWAPCHAIN_CREATE_INFO};chain.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            chain.format=req.format;chain.sampleCount=1;chain.width=req.width;chain.height=req.height;chain.faceCount=chain.arraySize=chain.mipCount=1;
            Check(api.CreateSwapchain(session,&chain,&swapchains[eye]),"Create eye swapchain");
            Check(api.EnumerateSwapchainImages(swapchains[eye],0,&count,nullptr),"Eye image count");Require(count&&count<=64,"Invalid eye image count");
            images[eye].resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            Check(api.EnumerateSwapchainImages(swapchains[eye],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())),"Eye images");
        }
        // Copy incoming pairs into private scratch first. A failed/partial copy must
        // never replace the swapchain images that supply the retained pair.
        D3D11_TEXTURE2D_DESC scratchDesc{};scratchDesc.Width=req.width;scratchDesc.Height=req.height;
        scratchDesc.Format=DXGI_FORMAT(req.format);scratchDesc.MipLevels=scratchDesc.ArraySize=1;
        scratchDesc.SampleDesc.Count=1;scratchDesc.Usage=D3D11_USAGE_DEFAULT;
        for(auto& texture:scratch)Hr(device->CreateTexture2D(&scratchDesc,nullptr,&texture),"Create private eye scratch");
        if(depthWanted)InitializeDepth(formats);
        if(options.bodyProps){
            auto compositor=std::make_unique<graphics::D3D11BodyPropCompositor>();
            if(compositor->Initialize(context.Get(),req.width,req.height,req.format,options.bodyProps)){bodyProps=std::move(compositor);report.bodyPropsReady=true;}
        }
        mirrorOn=options.mirrorWindow&&mirror.Create(device.Get(),req.width,req.height,req.format);report.mirror=mirror.Statistics();
        if(!options.readyPath.empty()){
            Require(options.readyPath.is_absolute()&&!std::filesystem::exists(options.readyPath),"Readiness report requires a new absolute path");
            std::ofstream ready(options.readyPath);ready<<"{\"session_created\":true,\"width\":"<<req.width<<",\"height\":"<<req.height<<",\"format\":"<<req.format<<",\"user_presence_supported\":"<<(report.userPresenceSupported?"true":"false")<<"}\n";
            ready.close();Require(bool(ready),"Cannot write XR readiness report");
        }
    }
    void DispatchFeedback(IFrameProvider* provider,const interaction::InputFrame& input){
        DispatchAudio(input);
        if(!controls)return;
        LARGE_INTEGER q{},frequency{};std::int64_t now=0;
        if(QueryPerformanceCounter(&q)&&QueryPerformanceFrequency(&frequency)&&q.QuadPart>0&&frequency.QuadPart>0&&frequency.QuadPart<=1000000000)
            now=q.QuadPart/frequency.QuadPart*1000000000+(q.QuadPart%frequency.QuadPart)*1000000000/frequency.QuadPart;
        controls->Feedback(input,now);
        if(provider)for(unsigned n=0;n<8;++n){
            interaction::FeedbackEvent event;if(!provider->TakeFeedback(event))break;
            const auto applied=controls->FeedbackApplied(),rejected=controls->FeedbackRejected(),errors=controls->FeedbackErrors();
            controls->Feedback(input,now,&event);
            // Persist actual dispatch at event time. Forced host termination cannot
            // erase the only evidence of a cue's acceptance/rejection by OpenXR.
            if(eventLog){eventLog<<"{\"event\":\"haptic_dispatch\",\"wall_ms\":"<<GetTickCount64()
                <<",\"id\":"<<event.id<<",\"kind\":"<<unsigned(event.kind)<<",\"hand\":"<<event.hand
                <<",\"observed_ns\":"<<event.observedNs<<",\"deadline_ns\":"<<event.deadlineNs<<",\"now_ns\":"<<now
                <<",\"source_input\":"<<event.inputSequence<<",\"current_input\":"<<input.generation
                <<",\"source_space\":"<<event.space<<",\"current_space\":"<<input.spaceGeneration
                <<",\"focused\":"<<(input.focused?"true":"false")
                <<",\"head_valid\":"<<(input.headValid?"true":"false")
                <<",\"left_tracked\":"<<(input.hands[0].gripTracked?"true":"false")
                <<",\"right_tracked\":"<<(input.hands[1].gripTracked?"true":"false")
                <<",\"applied\":"<<(controls->FeedbackApplied()-applied)
                <<",\"rejected\":"<<(controls->FeedbackRejected()-rejected)
                <<",\"errors\":"<<(controls->FeedbackErrors()-errors)<<"}\n";} // Flushed by the one-second timing log, not per dispatch.
        }
        report.feedbackApplied=controls->FeedbackApplied();report.feedbackRejected=controls->FeedbackRejected();report.feedbackErrors=controls->FeedbackErrors();
        report.captureFeedbackApplied=controls->CaptureFeedbackApplied();report.receiptFeedbackApplied=controls->ReceiptFeedbackApplied();report.seatedFeedbackApplied=controls->SeatedFeedbackApplied();report.lastFeedbackEvent=controls->LastFeedbackEvent();
    }
    // ---- Result handling for the frame path ------------------------------------
    // Only session/instance loss ends the host. Everything else is counted and the
    // frame degrades (no world layer, or the retained one when that is safe).
    enum class XrClass {Ok,Timeout,Recoverable,Lost};
    static XrClass Classify(XrResult result)noexcept {
        if(result==XR_ERROR_SESSION_LOST||result==XR_ERROR_INSTANCE_LOST||result==XR_SESSION_LOSS_PENDING)return XrClass::Lost;
        if(result==XR_TIMEOUT_EXPIRED)return XrClass::Timeout;
        return XR_FAILED(result)?XrClass::Recoverable:XrClass::Ok;
    }
    // True when the call succeeded. A timeout or recoverable failure is counted and
    // returns false; loss requests the exit (and still returns true for the success
    // code XR_SESSION_LOSS_PENDING, whose call did complete).
    bool Accept(XrResult result,const char* operation)noexcept {
        switch(Classify(result)){
        case XrClass::Ok:return true;
        case XrClass::Timeout:++report.swapchainTimeouts;return false;
        case XrClass::Lost:
            StopAudio();
            if(!exit)Log("xr_lost");
            exit=true;report.sessionLost=true;report.lastXrError=result;return XR_SUCCEEDED(result);
        case XrClass::Recoverable:break;
        }
        report.lastXrError=result;
        if(++report.xrRecoverableErrors<=16||report.xrRecoverableErrors%256==0)
            if(eventLog)eventLog<<"{\"event\":\"xr_recoverable\",\"wall_ms\":"<<GetTickCount64()<<",\"operation\":\""<<operation<<"\",\"result\":"<<result<<",\"count\":"<<report.xrRecoverableErrors<<"}\n";
        return false;
    }
    // A failing call must not turn the frame loop into a busy spin.
    void BackOff()noexcept {Sleep(++consecutiveFailures<4?1:5);}
    // Swapchain waits get a slice of the frame period: long enough to ride out a
    // compositor hiccup, short enough that a stall never costs the frame it is in.
    static XrDuration ImageWaitNs(XrDuration period)noexcept {
        return std::clamp(period>0?period:DefaultFramePeriodNs,MinImageWaitNs,MaxImageWaitNs);
    }
    // States in which the retained world layer is still shown. FVR_XR_RETAIN_VISIBLE=0
    // limits it to FOCUSED, as before.
    bool SessionHoldsWorld(XrSessionState state)const noexcept {
        return state==XR_SESSION_STATE_FOCUSED||(retainVisible&&state==XR_SESSION_STATE_VISIBLE);
    }
    void Events(){
        while(true){XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};const auto result=api.PollEvent(instance,&event);
            if(result==XR_EVENT_UNAVAILABLE)break;if(!Accept(result,"xrPollEvent")||exit)break;
            if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING){report.sessionLost=true;exit=true;return;}
            if(event.type==XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB){
                report.refreshCurrentHz=reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB&>(event).toDisplayRefreshRate;
                if(eventLog){eventLog<<"{\"event\":\"display_refresh_changed\",\"wall_ms\":"<<GetTickCount64()<<",\"hz\":"<<report.refreshCurrentHz<<"}\n";eventLog.flush();}
                continue;
            }
            if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){
                const auto& change=reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(event);
                if(change.session==session&&change.referenceSpaceType==referenceType)spaceChanges.push_back(change.changeTime);
            }
            if(event.type==XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT&&report.userPresenceSupported){
                const auto& presence=reinterpret_cast<const XrEventDataUserPresenceChangedEXT&>(event);
                if(presence.session==session){
                    const bool wasAbsent=presenceKnown&&!userPresent;
                    presenceKnown=true;userPresent=presence.isUserPresent==XR_TRUE;++report.userPresenceEvents;
                    if(!userPresent){StopAudio();if(referenceValid)recoveryPending=true;presenceReturnAt=0;retained.Reset();if(controls)controls->StopFeedback();}
                    else if(wasAbsent)presenceReturnAt=GetTickCount64();
                    Log("presence");
                }
            }
            if(event.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)continue;
            const auto& change=reinterpret_cast<const XrEventDataSessionStateChanged&>(event);if(change.session!=session)continue;
            sessionState=change.state;report.lastSessionState=std::int32_t(sessionState);Log("session_state");
            // Gameplay input is gated whenever the session is not FOCUSED. The world
            // layer is kept through VISIBLE (dashboard open) and dropped once the
            // session no longer shows it at all.
            if(sessionState!=XR_SESSION_STATE_FOCUSED){StopAudio();if(controls)controls->StopFeedback();}
            if(!SessionHoldsWorld(sessionState))retained.Reset();
            if(sessionState==XR_SESSION_STATE_READY&&!running)BeginSession();
            if(sessionState==XR_SESSION_STATE_STOPPING&&running)StopSession();
            if(sessionState==XR_SESSION_STATE_EXITING||sessionState==XR_SESSION_STATE_LOSS_PENDING){
                if(sessionState==XR_SESSION_STATE_LOSS_PENDING)report.sessionLost=true;
                exit=true; // Teardown is the destructor: it releases images, drains the copy and destroys in order.
            }
        }
    }
    void BeginSession(){
        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        // A failed begin is retried by Run while the session stays READY.
        if(!Accept(api.BeginSession(session,&begin),"xrBeginSession"))return;
        running=true;referenceValid=false;++spaceGeneration;
    }
    void StopSession(){
        AbortFrame();RetirePendingCopy();retained.Reset();uploadHeld=false;
        Accept(api.EndSession(session),"xrEndSession");
        running=false; // STOPPING always ends: a failed xrEndSession leaves nothing to retry.
    }

    // ---- One XR frame -----------------------------------------------------------
    // Rejection reasons index HostReport::pairRejectReasons; keep the values stable.
    enum class Rejection:unsigned {Metadata=0,SpaceGeneration=1,Future=2,TooOld=3,OpenFailed=4,CopyBusy=5,CopyInvalid=6,CopyUnavailable=7,
        CopyFaulted=8,Other=9,UploadFailed=10};
    struct FrameContext {
        XrFrameState state{XR_TYPE_FRAME_STATE};
        ULONGLONG wall=0;
        bool sessionFocused=false;                    // FOCUSED with the user present and settled.
        bool located=false,menuActive=false,fresh=false;
        runtime::TrackingFrame tracking{};
    };
    // A pair taken from the provider this frame. PairConsumed is reported exactly
    // once when the attempt ends, unless the asynchronous copy defers it.
    struct PairAttempt {
        IFrameProvider* provider=nullptr;
        graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};runtime::TrackingFrame rendered{};
        runtime::RetainedPresentation candidate;
        Rejection rejection=Rejection::Other;
        bool taken=false,copied=false,deferred=false,held=false,depthCopied=false;
        std::array<float,4> depthNearFar{}; // Metres, eye0 near/far, eye1 near/far.
        Clock::time_point delivered{}; // When the host took the completed pair (telemetry).
        PairAttempt()=default;PairAttempt(const PairAttempt&)=delete;PairAttempt& operator=(const PairAttempt&)=delete;
        ~PairAttempt(){if(taken&&!deferred&&provider)provider->PairConsumed(ticket,copied);}
    };
    void Frame(IFrameProvider* provider){
        const auto frameStart=Clock::now();
        if(previousFrame!=Clock::time_point{})++report.frameGapBuckets[TimingBucket(std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(frameStart-previousFrame).count()))];
        previousFrame=frameStart;
        FrameContext f;
        PaceToContent(provider);
        if(!BeginXrFrame(f,frameStart))return;
        consecutiveFailures=0;
        ResumeHeldUpload(f);
        ApplySpaceChanges();
        if(f.state.shouldRender){
            LocateAndTrack(provider,f);
            if(f.located){
                PairAttempt pair;
                if(AcquireFreshPair(provider,f,pair)&&pair.copied)UploadToSwapchain(f,pair);
                AccountPair(f,pair);
            }
        }
        if(!f.located)SendInactiveInput(provider,f);
        SubmitFrame(provider,f);
    }
    // xrWaitFrame + xrBeginFrame. False: no frame is open and nothing was submitted.
    bool BeginXrFrame(FrameContext& f,Clock::time_point frameStart){
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        const auto waited=api.WaitFrame(session,&wait,&f.state);Measure(0,frameStart);
        if(!Accept(waited,"xrWaitFrame")){StopAudio();BackOff();return false;}
        ++report.waitedFrames;framePeriodNs=f.state.predictedDisplayPeriod;delivery.Waited(f.state.predictedDisplayTime,f.state.predictedDisplayPeriod,Clock::now());if(mirrorOn)mirror.Pump();frameTime=f.state.predictedDisplayTime;imageWaitNs=ImageWaitNs(f.state.predictedDisplayPeriod);
        const auto beginStart=Clock::now();
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        const auto began=api.BeginFrame(session,&begin);Measure(1,beginStart);
        if(!Accept(began,"xrBeginFrame")){StopAudio();BackOff();return false;}
        frameOpen=true;
        f.wall=GetTickCount64();
        f.sessionFocused=sessionState==XR_SESSION_STATE_FOCUSED&&(userPresent||ignorePresence)&&(ignorePresence||!presenceReturnAt||f.wall-presenceReturnAt>=250);
        // Some wireless runtimes stop delivering frames instead of reporting a
        // tracking-loss frame. Treat a long delivery gap as a reference break.
        if(roomscale&&referenceValid&&lastFrameWall&&f.wall-lastFrameWall>=1500)recoveryPending=true;
        lastFrameWall=f.wall;
        return true;
    }
    void ApplySpaceChanges(){
        for(auto at=spaceChanges.begin();at!=spaceChanges.end();)if(*at<=frameTime){++spaceGeneration;referenceValid=false;retained.Reset();at=spaceChanges.erase(at);}else ++at;
    }
    // Locates the views and head, maintains the tracking reference and recentering,
    // builds and publishes the InputFrame (menu-filtered), and gates the provider.
    // Leaves f.located false when the runtime could not locate a tracked head.
    void LocateAndTrack(IFrameProvider* provider,FrameContext& f){
        const auto inputStart=Clock::now();
        std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewState viewState{XR_TYPE_VIEW_STATE};
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=frameTime;locate.space=local;uint32_t count=0;
        if(!Accept(api.LocateViews(session,&locate,&viewState,2,&count,views.data()),"xrLocateViews"))return;
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        if(!Accept(api.LocateSpace(headSpace,local,frameTime,&head),"xrLocateSpace(head)"))return;
        constexpr auto validViews=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        constexpr auto validHead=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        if(!(count==2&&(viewState.viewStateFlags&validViews)==validViews&&(head.locationFlags&validHead)==validHead))return;
        f.located=true;auto& tracking=f.tracking;const auto wall=f.wall;
        tracking.generation=++trackingGeneration;tracking.spaceGeneration=spaceGeneration;tracking.predictedNs=frameTime;
        delivery.TrackingSampled(tracking.generation,inputStart);
        tracking.worldUnitsPerMeter=worldUnitsPerMeter;
        tracking.focused=f.sessionFocused;tracking.headValid=true;tracking.head=Pose(head.pose);
        if(roomscale&&trackingRecovery.Update(tracking.focused,wall))recoveryPending=true;
        if(roomscale&&tracking.focused&&recoveryPending&&interaction::UprightReference(tracking.head)){
            referenceValid=false;++spaceGeneration;tracking.spaceGeneration=spaceGeneration;
            retained.Reset();if(provider)provider->Suspend();
            recoveryPending=false;++report.recenters;++report.automaticRecenters;
        }
        if(!referenceValid&&tracking.focused&&math::MakeRelativePose(tracking.head,tracking.head)){
            const auto reference=roomscale?interaction::UprightReference(tracking.head):std::optional<math::Pose>{tracking.head};
            if(reference){referenceHead=*reference;referenceValid=true;Log("capture_reference");}
        }
        tracking.referenceHead=referenceHead;
        bool valid=referenceValid&&bool(math::MakeRelativePose(tracking.referenceHead,tracking.head));
        for(unsigned eye=0;eye<2;++eye){tracking.eyes[eye]=Pose(views[eye].pose);const auto& fov=views[eye].fov;
            tracking.fov[eye]={std::tan(fov.angleLeft),std::tan(fov.angleRight),std::tan(fov.angleUp),std::tan(fov.angleDown)};
            valid&=bool(math::MakeRelativePose(tracking.referenceHead,tracking.eyes[eye]))&&bool(math::MakeLhProjectionFromFovTangents(tracking.fov[eye],.05f,100.f));}
        tracking.headValid=valid;if(valid)++report.validTrackingFrames;
        interaction::InputFrame input{};input.generation=++inputGeneration;input.spaceGeneration=spaceGeneration;input.predictedNs=frameTime;
        input.focused=tracking.focused;input.headValid=tracking.headValid;input.floorRelative=report.floorRelative;input.worldUnitsPerMeter=worldUnitsPerMeter;
        input.referenceHead=referenceHead;input.head=tracking.head;
        if(controls){const auto controlsStart=Clock::now();controls->Sample(local,frameTime,input);Measure(7,controlsStart);report.controllerSampling=controls->SamplingEvidence();}
        if(syntheticHands){
            // Same packet as BC2NativeIpcProbe's magazine fixture: identity
            // head and reference, both hands tracked at fixed poses.
            input.referenceHead=math::Pose{};input.head=math::Pose{};input.headValid=true;
            for(auto& hand:input.hands){hand=interaction::ControllerState{};hand.gripTracked=hand.aimTracked=true;hand.active=interaction::Components;}
            input.hands[0].grip.position={-.2f,-.25f,-.45f};input.hands[1].grip.position={.15f,-.10f,-.20f};
            for(auto& hand:input.hands)hand.aim=hand.grip;
            ++report.syntheticInputFrames;
        }
        if(roomscale&&recenter.Update(input))if(const auto reference=interaction::ExplicitRecenterReference(tracking.head,referenceHead)){
            referenceHead=*reference;++spaceGeneration;retained.Reset();if(provider)provider->Suspend();++report.recenters;
            tracking.referenceHead=input.referenceHead=referenceHead;tracking.spaceGeneration=input.spaceGeneration=spaceGeneration;Log("manual_recenter");
        }
        if(menu){f.menuActive=menu->Frame(input);menu->FilterGameplayInput(input);if(f.menuActive){input.focused=false;input.hands={};retained.Reset();}}
        if(taping){if(tapeWriter.Append(input,0,input.focused,spaceGeneration?spaceGeneration-1:0))report.recordedTicks=tapeWriter.Ticks();else{taping=false;report.recordFailed=true;}}
        if(provider)provider->UpdateInput(input);DispatchFeedback(provider,input);++report.inputFrames;
        if(input.hands[0].gripTracked&&input.hands[1].aimTracked)++report.trackedHandFrames;
        Measure(2,inputStart);
        if(valid&&tracking.focused&&!f.menuActive){
            renderViews.Refine(api.LocateViews,api.LocateSpace,session,local,headSpace,frameTime,tracking);
            renderViews.Publish(provider,report.requirements,tracking);
        }
        if(provider){
            if(f.menuActive||!valid||!tracking.focused)provider->Suspend();
            else provider->PrepareFrame(report.requirements,tracking);
        }
    }
    // No tracked head this frame: input is released and the menu still sees it.
    void SendInactiveInput(IFrameProvider* provider,FrameContext& f){
        if(roomscale)trackingRecovery.Update(false,f.wall);
        interaction::InputFrame inactive{};inactive.generation=++inputGeneration;inactive.spaceGeneration=spaceGeneration;inactive.predictedNs=frameTime;
        inactive.worldUnitsPerMeter=worldUnitsPerMeter;inactive.floorRelative=report.floorRelative;
        recenter.Update(inactive);if(menu)f.menuActive=menu->Frame(inactive);if(provider)provider->UpdateInput(inactive);DispatchFeedback(provider,inactive);++report.inputFrames;
    }
    // Takes a completed pair if the provider has one, validates it against this
    // frame, and queues (or performs) the copy into the private scratch images.
    // True when a pair was taken (whether or not its copy succeeded).
    bool AcquireFreshPair(IFrameProvider* provider,FrameContext& f,PairAttempt& pair){
        auto& tracking=f.tracking;
        PollPendingCopy();
        const auto requestStart=std::chrono::steady_clock::now();
        const bool available=!pendingCopyProvider&&!f.menuActive&&tracking.headValid&&tracking.focused&&provider&&provider->TryGetCompletedPair(report.requirements,tracking,pair.rendered,pair.descriptor,pair.ticket);
        const auto requestUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-requestStart).count();
        report.maxProviderWaitUs=(std::max)(report.maxProviderWaitUs,std::uint64_t(requestUs));
        if(!f.menuActive&&tracking.headValid&&tracking.focused&&provider&&!available)++report.providerMisses;
        if(!available)return false;
        pair.provider=provider;pair.taken=true;pair.delivered=Clock::now();
        const auto& renderedTracking=pair.rendered;const auto& descriptor=pair.descriptor;const auto& ticket=pair.ticket;
        // Validate metadata before replacing either released image. If
        // Commit failed after release, old poses could label new pixels.
        if(!pair.candidate.Commit(report.requirements,renderedTracking,descriptor,ticket)){pair.rejection=Rejection::Metadata;return true;}
        if(renderedTracking.spaceGeneration!=tracking.spaceGeneration){pair.rejection=Rejection::SpaceGeneration;return true;}
        if(renderedTracking.predictedNs>tracking.predictedNs){pair.rejection=Rejection::Future;return true;}
        if(tracking.predictedNs-renderedTracking.predictedNs>runtime::RetainedPresentation::MaxAgeNs){pair.rejection=Rejection::TooOld;return true;}
        bool reopened=false;auto* consumerPointer=OpenPair(descriptor,reopened);
        if(reopened)retained.Reset(); // A new resource generation (as before the pair cache).
        if(!consumerPointer){pair.rejection=Rejection::OpenFailed;return true;}
        auto& consumer=*consumerPointer;
        const std::array<graphics::TextureSlice,2> destinations{{{scratch[0].Get(),0},{scratch[1].Get(),0}}};
        gpuTimer.Begin(device.Get(),context.Get(),delivery); // Ended just before xrEndFrame.
        const auto copyStart=Clock::now();
        const bool asynchronous=report.asyncConsumerCopy&&descriptor.sharing==graphics::TextureSharing::LegacyFenced;
        const auto transfer=asynchronous?consumer.QueueCopy(ticket,destinations):consumer.Copy(ticket,destinations);Measure(3,copyStart);
        pair.copied=transfer==graphics::TransferResult::Ok;
        if(pair.copied&&asynchronous){
            pendingCopyProvider=provider;pendingCopyConsumer=consumerPointer;pendingCopyTicket=ticket;pendingCopyQueued=copyStart;
            pair.deferred=true;++report.asyncCopyEvidence[0];
        }
        if(pair.copied){
            uploadedEyes=0; // The scratch images hold a new pair: both eyes still need uploading.
            // Depth rides only with a synchronous color copy (the async path is legacy sharing).
            if(!asynchronous)pair.depthCopied=CopyDepth(provider,descriptor,ticket,pair.depthNearFar);
        }
        else {
            report.lastPairTransferError=consumer.LastError();
            switch(transfer){
            case graphics::TransferResult::Busy:pair.rejection=Rejection::CopyBusy;break;
            case graphics::TransferResult::Invalid:pair.rejection=Rejection::CopyInvalid;break;
            case graphics::TransferResult::Unavailable:pair.rejection=Rejection::CopyUnavailable;break;
            case graphics::TransferResult::Faulted:pair.rejection=Rejection::CopyFaulted;break;
            default:break;
            }
        }
        return true;
    }
    // Per-eye acquire/wait/copy/release from the scratch images. Eyes whose upload
    // already finished for the current scratch contents are skipped, so a timed-out
    // wait is resumed on a later call instead of redone or abandoned. Waited images
    // are always copied and released immediately; only an unwaited image can stay
    // acquired (the spec allows no release before a successful wait).
    enum class UploadResult {Done,Held};
    UploadResult WriteSwapchains(){
        for(unsigned eye=0;eye<2;++eye){
            if(uploadedEyes&(1u<<eye))continue;
            if(!acquired[eye]){
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};uint32_t index=0;
                if(!Accept(api.AcquireSwapchainImage(swapchains[eye],&acquire,&index),"xrAcquireSwapchainImage"))continue;
                acquired[eye]=true;waited[eye]=false;imageIndex[eye]=index;
            }
            if(!waited[eye]){
                XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=imageWaitNs;
                if(!Accept(api.WaitSwapchainImage(swapchains[eye],&imageWait),"xrWaitSwapchainImage"))continue;
                waited[eye]=true;
            }
            if(imageIndex[eye]<images[eye].size())context->CopyResource(images[eye][imageIndex[eye]].texture,scratch[eye].Get());
            else {++report.uploadFailed;retained.Reset();}
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            const bool released=Accept(api.ReleaseSwapchainImage(swapchains[eye],&release),"xrReleaseSwapchainImage");
            waited[eye]=acquired[eye]=false;uploadedEyes|=1u<<eye; // After a failed release the image state is unknowable: do not retry it.
            if(!released){++report.uploadFailed;retained.Reset();}
        }
        // One eye new and the other old would label mismatched pixels with one pose.
        if(uploadedEyes!=3&&uploadedEyes!=0)retained.Reset();
        return uploadedEyes==3?UploadResult::Done:UploadResult::Held;
    }
    bool AnyImageHeld()const noexcept {return acquired[0]||acquired[1];}
    // Composes body props and the desktop mirror from the scratch pair, then uploads
    // it to both eye swapchains. On Done the pair becomes the retained presentation.
    void UploadToSwapchain(FrameContext& f,PairAttempt& pair){
        const auto propsStart=Clock::now();
        if(bodyProps){graphics::BodyPropFrame props;
            if(pair.provider->ReadBodyProps(pair.ticket,props))bodyProps->Compose(props,pair.ticket,{scratch[0].Get(),scratch[1].Get()},BodyPropClockNs);
            report.bodyProps=bodyProps->Statistics();
        }
        // The composed left eye, the same image the headset gets next.
        if(mirrorOn){mirror.Show(context.Get(),scratch[0].Get());report.mirror=mirror.Statistics();}
        Measure(4,propsStart);const auto imagesStart=Clock::now();
        if(WriteSwapchains()==UploadResult::Done){
            const bool depthDone=depthOn&&(pair.depthCopied||AnyDepthImageHeld())&&WriteDepthSwapchains(pair.depthCopied);
            CommitFresh(f,pair.candidate,pair.rendered,depthDone?&pair.depthNearFar:nullptr,pair.ticket.frameId,pair.delivered);
        }
        else {uploadHeld=true;heldCandidate=pair.candidate;heldRendered=pair.rendered;heldFrameId=pair.ticket.frameId;heldDelivered=pair.delivered;
            heldDepth={pair.depthCopied,pair.depthNearFar};pair.held=true;++report.uploadHeld;}
        Measure(5,imagesStart);
    }
    void CommitFresh(FrameContext& f,const runtime::RetainedPresentation& candidate,const runtime::TrackingFrame& rendered,const std::array<float,4>* depth,
        std::uint64_t frameId,Clock::time_point delivered){
        delivery.FreshCommitted(frameId,rendered.generation,delivered);
        retained=candidate;f.fresh=true;afterCopyTimeout=false;uploadHeld=false;
        retainedDepth={};if(depth){retainedDepth.valid=true;retainedDepth.nearFar=*depth;}
        heldDepth={};
        const auto age=f.tracking.predictedNs>rendered.predictedNs?f.tracking.predictedNs-rendered.predictedNs:0;
        ++report.freshAgeBuckets[TimingBucket(std::uint64_t(age)/1000)];
        renderViews.ObservePresentation(f.tracking.predictedNs-rendered.predictedNs);
    }
    // A pair whose upload was interrupted (swapchain wait timed out) finishes as
    // soon as the runtime releases the image, from the scratch it was copied to.
    void ResumeHeldUpload(FrameContext& f){
        if(!uploadHeld)return;
        if(WriteSwapchains()==UploadResult::Done){
            // Scratch cannot be replaced while uploadHeld, so this is still the
            // depth copied alongside heldCandidate, even across several XR frames.
            const bool depthDone=depthOn&&(heldDepth.valid||AnyDepthImageHeld())&&WriteDepthSwapchains(heldDepth.valid);
            CommitFresh(f,heldCandidate,heldRendered,depthDone?&heldDepth.nearFar:nullptr,heldFrameId,heldDelivered);
        }
    }
    void AccountPair(FrameContext& f,PairAttempt& pair){
        if(!pair.taken||f.fresh||pair.held)return;
        // A copied pair that could not be uploaded was consumed but never presented.
        if(pair.copied)pair.rejection=Rejection::UploadFailed;
        ++report.rejectedPairs;++report.pairRejectReasons[std::size_t(pair.rejection)];
    }
    void SubmitFrame(IFrameProvider* provider,FrameContext& f){
        auto& tracking=f.tracking;const bool menuActive=f.menuActive;
        report.recenterInput=recenter.Evidence();
        // Menu entry also cancels an outstanding world request/flushes deferred
        // feedback. Repeat the zero-wait suspension while menu-owned so a busy
        // IPC gate cannot carry a pre-menu pair through to the resumed world.
        if(menuActive)retained.Reset();
        if(!f.state.shouldRender)++report.gatedFrames[0];
        if(!tracking.headValid)++report.gatedFrames[1];
        if(!tracking.focused)++report.gatedFrames[2];
        if(menuActive)++report.gatedFrames[3];
        if(provider&&(menuActive||!f.state.shouldRender||!tracking.focused||!tracking.headValid))provider->Suspend();
        PollPendingCopy(); // Drain GPU ownership even when XR world rendering is gated.
        std::array<XrCompositionLayerProjectionView,2> projectionViews{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=local;layer.viewCount=2;layer.views=projectionViews.data();
        // The retained world layer rides through a dashboard (VISIBLE) and a brief
        // head-tracking loss with its ORIGINAL poses; only input/gameplay is gated.
        runtime::RetentionContext retention{};retention.predictedNs=frameTime;retention.spaceGeneration=spaceGeneration;
        retention.shouldRender=f.state.shouldRender&&!menuActive;retention.focused=f.sessionFocused;retention.headValid=tracking.headValid;
        bool submit=false;
        // Depth rides on each projection view when the retained pair's depth landed.
        std::array<XrCompositionLayerDepthInfoKHR,2> depthInfos{{{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR},{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR}}};
        const bool withDepth=depthOn&&retainedDepth.valid&&!AnyDepthImageHeld();
        if(const auto* rendered=retained.Select(report.requirements,retention)){
            if(AnyImageHeld())++report.uploadHeldFrames; // An acquired image must not be in a submitted layer.
            else {
                for(unsigned eye=0;eye<2;++eye){auto& projection=projectionViews[eye];const auto& pose=rendered->eyes[eye];const auto& fov=rendered->fov[eye];
                    projection.pose={{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w},{pose.position.x,pose.position.y,pose.position.z}};
                    projection.fov={std::atan(fov.left),std::atan(fov.right),std::atan(fov.up),std::atan(fov.down)};
                    projection.subImage.swapchain=swapchains[eye];projection.subImage.imageRect.extent={std::int32_t(report.requirements.width),std::int32_t(report.requirements.height)};
                    if(withDepth){auto& depth=depthInfos[eye];
                        depth.subImage.swapchain=depthSwapchains[eye];depth.subImage.imageRect=projection.subImage.imageRect;depth.subImage.imageArrayIndex=0;
                        // D3D [0,1] window depth, not reversed-Z (docs/re/BC2_LATE_LATCH_DEPTH_RE.md section 4).
                        depth.minDepth=0.f;depth.maxDepth=1.f;depth.nearZ=retainedDepth.nearFar[eye*2];depth.farZ=retainedDepth.nearFar[eye*2+1];
                        projection.next=&depth;}
                }
                if(withDepth)++report.depthFrames;
                submit=true;report.maxRetainedAgeNs=(std::max)(report.maxRetainedAgeNs,std::uint64_t(frameTime-rendered->predictedNs));
                if(!f.sessionFocused)++report.retainedVisibleFrames;else if(!tracking.headValid)++report.retainedHeadLossFrames;
            }
        }
        std::array<const XrCompositionLayerBaseHeader*,1+OpenXrMenu::MaxLayers> layers{};std::uint32_t layerCount=0;
        if(submit)layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
        if(menu){layerCount+=menu->Layers(local,layers.data()+layerCount,std::uint32_t(layers.size())-layerCount);report.menuFrames=menu->Frames();report.menuErrors=menu->Errors();}
        const auto endFailuresBefore=report.endFrameFailures;
        gpuTimer.End(context.Get());const auto endCall=Clock::now();
        const bool ended=EndXrFrame(layers.data(),layerCount);
        if(ended)delivery.Ended(submit,f.fresh,endCall);else delivery.Failed();
        paceArmed=ended&&submit&&f.state.shouldRender&&!menuActive&&f.sessionFocused&&tracking.headValid&&tracking.focused;
        // A runtime that keeps rejecting frames with depth attached falls back to
        // color-only layers instead of degrading every frame to the empty retry.
        if(submit&&withDepth){
            if(report.endFrameFailures!=endFailuresBefore){if(++depthEndFailures>=3){DisableDepth("end_frame_rejected");if(provider)provider->RequestDepth(false);}}
            else depthEndFailures=0;
        }
        // Attribute this frame before the post-EndFrame poll can time out the
        // copy; that timeout only affects later frames. Counts frames until the
        // next pair after a timed-out copy: presented (retained) vs no world
        // layer while the world was otherwise presentable.
        if(afterCopyTimeout){
            if(submit)++report.copyTimeoutPresentation[1];
            else if(f.state.shouldRender&&!menuActive&&tracking.focused&&tracking.headValid)++report.copyTimeoutPresentation[2];
        }
        // EndFrame may yield enough GPU time to finish the copy. One nonblocking
        // poll releases the source now, without waiting for the next XR frame.
        PollPendingCopy();
        if(ended){
            if(submit){++report.presentedFrames;if(f.fresh)++report.submittedPairs;else ++report.reusedFrames;}
            else ++report.blankFrames;
        }
        report.lateCopies=LateCopies();
        {const auto& v=renderViews.Stats();report.renderViews={v.predicted,v.predictionFallbacks,v.overscanned,v.posesPublished,v.leadNs};}
        gpuTimer.Poll(context.Get(),delivery);
        if(eventLog&&delivery.MaybeWrite(eventLog,GetTickCount64()))eventLog.flush();
        TimingLog();
    }
    // xrEndFrame with whatever layers were built; on a recoverable failure the frame
    // is ended again with no layers (it stays open until an EndFrame succeeds).
    // Repeated failures with the world layer drop the retained presentation.
    bool EndXrFrame(const XrCompositionLayerBaseHeader* const* layers,std::uint32_t layerCount){
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frameTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount=layerCount;end.layers=layers;
        const auto endStart=Clock::now();
        auto result=api.EndFrame(session,&end);Measure(6,endStart);
        bool ended=Accept(result,"xrEndFrame");
        if(!ended&&!exit){
            ++report.endFrameFailures;
            if(++consecutiveEndFailures>=3)retained.Reset();
            if(layerCount){end.layerCount=0;end.layers=nullptr;result=api.EndFrame(session,&end);ended=Accept(result,"xrEndFrame(empty)");}
        }else if(ended)consecutiveEndFailures=0;
        frameOpen=false;
        if(ended)++report.endedFrames;
        return ended;
    }
    void Run(const HostOptions& options){
        if(eventLog){eventLog<<"{\"event\":\"display_refresh\",\"wall_ms\":"<<GetTickCount64()<<",\"extension\":"<<(report.refreshExtension?"true":"false")
            <<",\"target_hz\":"<<report.refreshTargetHz<<",\"initial_hz\":"<<report.refreshInitialHz<<",\"requested_hz\":"<<report.refreshRequestedHz
            <<",\"current_hz\":"<<report.refreshCurrentHz<<",\"result\":"<<report.refreshResult<<",\"rates\":\""<<report.refreshRates<<"\""
            <<",\"pace_to_content\":"<<(report.paceToContent?"true":"false")<<",\"pace_max_wait_ms\":"<<report.paceMaxWaitMs<<"}\n";eventLog.flush();}
        if(options.menu){menu=std::make_unique<OpenXrMenu>();menu->Initialize(instance,session,api.get,device.Get(),options.menu);}
        if(options.provider)options.provider->RequestDepth(depthOn);
        const auto start=std::chrono::steady_clock::now();bool requested=false;auto shutdown=start;
        while(!exit){Events();if(exit)break;const auto now=std::chrono::steady_clock::now();
            if(!requested&&((options.seconds&&now-start>=std::chrono::seconds(options.seconds))||(options.stopRequested&&options.stopRequested()))){
                if(controls)controls->ReadProfiles(report.controllerCurrentProfiles,report.controllerProfileResults);
                Log("application_exit_requested");if(!running)break;Accept(api.RequestExitSession(session),"xrRequestExitSession");requested=true;shutdown=now;
            }
            if(requested&&(!running||now-shutdown>=std::chrono::seconds(2)))break;
            if(running)Frame(options.provider);
            else {
                if(sessionState==XR_SESSION_STATE_READY)BeginSession(); // Retry a begin that failed recoverably.
                StopAudio();if(options.provider)options.provider->Suspend();PollPendingCopy();Sleep(10);
            }
        }
    }
};
}
HostReport RunOpenXrHost(const HostOptions& options)noexcept {
    HostReport report;
    try {Require(options.probeOnly||options.seconds||bool(options.stopRequested),"Continuous session requires a stop source");Host host(report);
        struct ReleaseInput {Host& host;IFrameProvider* provider;~ReleaseInput(){if(provider){interaction::InputFrame f{};f.generation=++host.inputGeneration;f.spaceGeneration=host.spaceGeneration;f.predictedNs=host.frameTime>0?host.frameTime:1;provider->UpdateInput(f);provider->Suspend();}}} release{host,options.provider};
        host.Initialize(options);if(!options.probeOnly)host.Run(options);report.okay=true;}
    catch(const std::exception& error){report.error=error.what();}
    catch(...){report.error="Unknown OpenXR host failure";}
    return report;
}
}
