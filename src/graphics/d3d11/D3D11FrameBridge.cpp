#include "fvr/graphics/D3D11FrameBridge.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <d3d11_1.h>
namespace fvr::graphics {
namespace {std::int64_t Qpc()noexcept{LARGE_INTEGER q{};return QueryPerformanceCounter(&q)?q.QuadPart:0;}
std::int64_t QpcFrequency()noexcept{LARGE_INTEGER f{};return QueryPerformanceFrequency(&f)?f.QuadPart:0;}}
std::int64_t D3D11FrameBridge::ExportStarted()noexcept{return exportStartQpc_=Qpc();}
void D3D11FrameBridge::ExportFinished(std::int64_t start)noexcept{
    const auto now=Qpc(),frequency=QpcFrequency();
    if(start<=0||now<start||frequency<=0)return;
    const auto us=std::uint64_t((now-start)/frequency*1000000+(now-start)%frequency*1000000/frequency);
    exports_.fetch_add(1,std::memory_order_relaxed);exportTotalUs_.fetch_add(us,std::memory_order_relaxed);
    if(us>exportMaxUs_.load(std::memory_order_relaxed))exportMaxUs_.store(us,std::memory_order_relaxed);
}
bool D3D11FrameBridge::ConnectGraphics(ID3D11Device* device,const std::wstring& token,FrameSharingPreference preference)noexcept {
    if(!device||connected_.load(std::memory_order_acquire))return false;
    forceLegacy_=preference==FrameSharingPreference::LegacyRelay;forceLegacyIpc_=preference==FrameSharingPreference::LegacyIpc;device_=device;deviceFlags_=device->GetCreationFlags();featureLevel_=device->GetFeatureLevel();device_->GetImmediateContext(&context_);
    // FVR_XR_DEPTH_SUBMIT=0 (game or host environment) disables depth layers.
    {wchar_t value[8]{};const auto length=GetEnvironmentVariableW(L"FVR_XR_DEPTH_SUBMIT",value,8);depthEnabled_=!(length==1&&value[0]==L'0');depthDisabled_=false;}
    // Pairs follow the leases the producer may hold (FVR_XR_PIPELINE_DEPTH): at
    // least the original two, one per request slot for depth 3+.
    pairLimit_=(std::max)(2u,(std::min)(PairSlots,channel_.OverlapLimit()+1));
    // H2b fix (FVR_BC2_EXPORT_QUEUE_AT_PUBLISH=0 restores queueing at the next pump).
    {wchar_t value[8]{};const auto length=GetEnvironmentVariableW(L"FVR_BC2_EXPORT_QUEUE_AT_PUBLISH",value,8);queueAtPublish_=!(length==1&&value[0]==L'0');}
    // Own private eye images per fenced pair (FVR_BC2_PER_PAIR_EYES=0 shares one set).
    {wchar_t value[8]{};const auto length=GetEnvironmentVariableW(L"FVR_BC2_PER_PAIR_EYES",value,8);perPairEyes_=!(length==1&&value[0]==L'0');}
    // Bounded spin on the producer gate instead of dropping a rendered stereo frame.
    {wchar_t value[16]{};const auto length=GetEnvironmentVariableW(L"FVR_BC2_BRIDGE_LOCK_SPIN_US",value,16);
        if(length>0&&length<16){wchar_t* end=nullptr;const auto parsed=std::wcstoul(value,&end,10);if(end&&!*end&&parsed<=5000)lockSpinUs_=unsigned(parsed);}}
    if(!channel_.Connect(token)){device_.Reset();context_.Reset();return false;}
    connected_.store(true,std::memory_order_release);return true;
}
bool D3D11FrameBridge::DrainFeedbackBounded()noexcept {
    if(channel_.TryDrainFeedbackGraphics())return true;
    // The gate is held only by zero-wait CPU work on the visibility thread (outcome
    // polls, request take, pose/input reads), never across a wait, so a short
    // bounded spin keeps a fully rendered stereo frame instead of dropping it.
    if(!lockSpinUs_){lockDrops_.fetch_add(1,std::memory_order_relaxed);return false;}
    const auto frequency=QpcFrequency(),start=Qpc();
    const auto limit=frequency>0?frequency*std::int64_t(lockSpinUs_)/1000000:0;
    for(unsigned spins=1;;++spins){
        if(spins&15)YieldProcessor();else SwitchToThread();
        if(channel_.TryDrainFeedbackGraphics()){lockRetries_.fetch_add(1,std::memory_order_relaxed);return true;}
        if(Qpc()-start>=limit)break;
    }
    lockDrops_.fetch_add(1,std::memory_order_relaxed);return false;
}
bool D3D11FrameBridge::TryBegin(const ipc::NativeFrameKey& native,ipc::FrameLease& lease)noexcept {
    return connected_.load(std::memory_order_acquire)&&!Fatal()&&(sharingMode_.load(std::memory_order_acquire)!=2||relay_.SourceWritable())&&channel_.TryBegin(native,lease);
}
void D3D11FrameBridge::DiagnoseSharing()noexcept {
    Microsoft::WRL::ComPtr<ID3D11Device1> newer;compatibility_[0]=device_.As(&newer);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};compatibility_[1]=device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options));compatibility_[2]=options.ExtendedResourceSharing;
    for(unsigned i=0;i<6;++i){D3D11_TEXTURE2D_DESC d{};d.Width=i==3?1920:32;d.Height=i==3?1080:16;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=(i==1||i==3)?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags=i==4?D3D11_RESOURCE_MISC_SHARED:i==5?0:D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|(i==2?0:D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;compatibility_[3+i]=device_->CreateTexture2D(&d,nullptr,&texture);
    }
}
int D3D11FrameBridge::PairIndex(const PairTicket& ticket)const noexcept {
    for(unsigned i=0;i<producers_.size();++i){const auto d=producers_[i].Descriptor();
        if(Valid(d)&&d.resourceEpoch==ticket.resourceEpoch&&d.session==ticket.session)return int(i);}
    return -1;
}
void D3D11FrameBridge::ReleaseDepth(unsigned pair)noexcept {
    if(pair>=depthProducers_.size()||!depthInFlight_[pair])return;
    depthInFlight_[pair]=false;
    // The host copies the depth pair (or not) inside its synchronous color copy, so
    // after any outcome the key is either untouched (2n-1) or released (2n): the
    // reclaim reuses the same textures in both cases.
    if(!depthProducers_[pair].Discard(depthTickets_[pair]))depthProducers_[pair].Reset();
    depthTickets_[pair]={};
}
void D3D11FrameBridge::ResetPairs()noexcept {
    for(auto& producer:producers_)producer.Reset();
    for(auto& producer:depthProducers_)producer.Reset();
    legacyExports_={};inFlight_={};depthInFlight_={};depthTickets_={};current_=0;depthMask_=0;pendingDepth_={};
}
bool D3D11FrameBridge::CaptureEye(unsigned eye,ID3D11Texture2D* source,const ipc::FrameLease& lease)noexcept {
    if(!connected_.load(std::memory_order_acquire)||!source||eye>1||submitting_||canceling_||waitingGameCopies_)return Fail(1,E_UNEXPECTED);
    // Visibility may have reaped the previous CPU outcome already. Never
    // capture/recreate textures until its GPU release/reset runs here; a busy
    // try-lock (after a bounded spin) drops this capture rather than mutating the
    // old owned resources.
    if(!DrainFeedbackBounded())return false;
    // H2b: with one shared private eye set, never overwrite it while an active
    // export has not queued its copy out of it yet. Per-pair eyes need no guard:
    // a pair is only chosen below when its export is no longer active.
    if(eye==0&&sharingMode_.load()==3&&!perPairEyes_){
        for(unsigned i=0;i<legacyExports_.size();++i){auto& item=legacyExports_[i];if(item.active&&!item.copyQueued&&!item.failed)PumpLegacyExport(i);}
        for(const auto& item:legacyExports_)if(item.active&&!item.copyQueued&&!item.failed){unqueuedRefusals_.fetch_add(1,std::memory_order_relaxed);return false;}
    }
    const auto& req=lease.requirements;D3D11_TEXTURE2D_DESC description{};source->GetDesc(&description);
    Microsoft::WRL::ComPtr<ID3D11Device> sourceDevice;source->GetDevice(&sourceDevice);
    const unsigned mismatch=(sourceDevice.Get()!=device_.Get()?1u:0u)|(description.Width!=req.width?2u:0u)|(description.Height!=req.height?4u:0u)|
        (description.MipLevels!=1?8u:0u)|(description.ArraySize!=1?16u:0u)|(description.SampleDesc.Count!=1?32u:0u)|(!CopyCompatibleFormats(description.Format,req.format)?64u:0u);
    if(mismatch){if(!failureStage_.load())rejected_={mismatch,description.Width,description.Height,unsigned(description.Format),description.MipLevels,description.ArraySize,description.SampleDesc.Count};
        return Fail(2,E_INVALIDARG);}
    if(eye==0){
        capturedRequest_=lease.requestId;mask_=0;depthMask_=0;depthRequest_=0;
        if(!sharingMode_.load()||std::memcmp(&allocated_,&req,sizeof(req))){
            ResetPairs();for(auto& set:eyes_)for(auto& texture:set)texture.Reset();relay_.Reset();eyeFrame_={};depthCapture_.Reset();sharingMode_.store(0);
            auto& producer=producers_[0];
            if(!forceLegacy_&&!forceLegacyIpc_){
                const auto result=producer.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_);
                if(result!=TransferResult::Ok){failureOperation_=producer.LastOperation();const auto error=producer.LastError();
                    if(error!=E_INVALIDARG||failureOperation_.load()!=5)return Fail(4,error);
                    forceLegacyIpc_=true;
                }
            }
            if(forceLegacyIpc_){
                if(producer.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_,TextureSharing::LegacyFenced)!=TransferResult::Ok)return Fail(4,producer.LastError());
                description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=0;description.CPUAccessFlags=0;description.MiscFlags=0;eyeDesc_=description;
                for(auto& texture:eyes_[0]){const auto hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return Fail(3,hr);}
                sharingMode_.store(3,std::memory_order_release);
            }else if(forceLegacy_){
                if(!relay_.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format))){failureOperation_=100+relay_.LastOperation();return Fail(10,relay_.LastError());}
                if(producer.Create(relay_.Device(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_)!=TransferResult::Ok){failureOperation_=200+producer.LastOperation();return Fail(10,producer.LastError());}
                sharingMode_.store(2,std::memory_order_release);
            }else{
                description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=0;description.CPUAccessFlags=0;description.MiscFlags=0;eyeDesc_=description;
                for(auto& texture:eyes_[0]){const auto hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return Fail(3,hr);}
                sharingMode_.store(1,std::memory_order_release);
            }
            allocated_=req;
        }
        // A pair whose outcome is still pending belongs to the host. Pipelining fills
        // another pair. Fenced legacy exports keep separate GPU events.
        const unsigned pairs=sharingMode_.load()==2?1u:pairLimit_;current_=pairs;
        for(unsigned i=0;i<pairs;++i)if(!inFlight_[i]&&!legacyExports_[i].active){current_=i;break;}
        if(current_==pairs){current_=0;++pairsBusy_;return false;}
        if(current_)++secondPair_;
        auto& producer=Producer();
        if(!Valid(producer.Descriptor())){
            auto* exporting=sharingMode_.load()==2?relay_.Device():device_.Get();
            if(producer.Create(exporting,req.width,req.height,DXGI_FORMAT(req.format),++epoch_,sharingMode_.load()==3?TextureSharing::LegacyFenced:TextureSharing::NamedKeyed)!=TransferResult::Ok)return Fail(4,producer.LastError());
        }
        const auto descriptor=producer.Descriptor();if(descriptor.adapterLow!=req.adapterLow||descriptor.adapterHigh!=req.adapterHigh)return Fail(5,E_INVALIDARG);
        // Per-pair private eyes are created on the pair's first use (same description).
        if(sharingMode_.load()!=2)for(auto& texture:eyes_[EyeSet(current_)])if(!texture){const auto hr=device_->CreateTexture2D(&eyeDesc_,nullptr,&texture);if(FAILED(hr))return Fail(3,hr);}
    }else if(capturedRequest_!=lease.requestId||mask_!=1)return Fail(6,E_UNEXPECTED);
    if(sharingMode_.load()==2){if(!relay_.SourceWritable())return Fail(12,E_UNEXPECTED);relay_.Capture(eye,source);}
    else context_->CopyResource(eyes_[EyeSet(current_)][eye].Get(),source);
    mask_|=1u<<eye;eyeFrame_[EyeSet(current_)][eye]=lease.native.frameId;++capturedEyes_;return true;
}
bool D3D11FrameBridge::CaptureEyeDepth(unsigned eye,ID3D11Texture2D* depth,float nearMeters,float farMeters,const ipc::FrameLease& lease)noexcept {
    if(!DepthWanted(lease)||eye>1||!depth||capturedRequest_!=lease.requestId||!(mask_&(1u<<eye))||submitting_||canceling_)return false;
    if(eye==0){depthMask_=0;depthRequest_=lease.requestId;}
    else if(depthRequest_!=lease.requestId||depthMask_!=1)return false;
    if(!DepthPlanesValid(nearMeters,farMeters)){++depthCaptureFailures_;return false;}
    if(!depthCapture_.Capture(device_.Get(),eye,depth,allocated_.width,allocated_.height)){
        ++depthCaptureFailures_;depthStage_=depthCapture_.LastStage();depthError_=depthCapture_.LastError();
        // A setup failure (state object, shaders) will not change during this
        // session: stop paying for attempts. A rejected source is only counted.
        if(depthCapture_.LastStage()==1)depthDisabled_=true;
        return false;
    }
    nearFar_[eye*2]=nearMeters;nearFar_[eye*2+1]=farMeters;depthMask_|=1u<<eye;++depthCaptured_;return true;
}
void D3D11FrameBridge::PublishDepth(const ipc::FrameLease& lease)noexcept {
    pendingDepth_={};
    if(depthMask_!=3||depthRequest_!=lease.requestId||!DepthWanted(lease))return;
    depthMask_=0;
    auto& producer=depthProducers_[current_];
    if(depthInFlight_[current_]){++depthPublishFailures_;return;} // Cannot happen: released with the color pair.
    if(!Valid(producer.Descriptor())&&producer.Create(device_.Get(),allocated_.width,allocated_.height,DXGI_FORMAT_R32_FLOAT,++epoch_)!=TransferResult::Ok){
        // R32_FLOAT keyed NT sharing unavailable on this device: color only.
        ++depthPublishFailures_;depthStage_=100+producer.LastOperation();depthError_=producer.LastError();depthDisabled_=true;producer.Reset();return;
    }
    const std::array<TextureSlice,2> sources={TextureSlice{depthCapture_.Eye(0),0},TextureSlice{depthCapture_.Eye(1),0}};
    const auto& t=lease.tracking;PairTicket depthTicket{};
    const auto result=producer.Publish(sources,{lease.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},depthTicket);
    if(result!=TransferResult::Ok){++depthPublishFailures_;if(result==TransferResult::Faulted)producer.Reset();return;}
    depthInFlight_[current_]=true;depthTickets_[current_]=depthTicket;
    pendingDepth_.descriptor=producer.Descriptor();pendingDepth_.ticket=depthTicket;pendingDepth_.nearFar=nearFar_;pendingDepth_.present=1;
    ++depthPublished_;
}
bool D3D11FrameBridge::PublishRestored(const ipc::FrameLease& lease)noexcept {
    if(mask_!=3||capturedRequest_!=lease.requestId||submitting_||canceling_||waitingGameCopies_)return Fail(7,E_UNEXPECTED);
    if(!DrainFeedbackBounded())return false;
    // Both private eye images must hold this lease's game frame. Refused (the caller
    // cancels the lease) rather than ever showing left and right from different frames.
    const auto& frames=eyeFrame_[EyeSet(current_)];
    if(frames[0]!=lease.native.frameId||frames[1]!=lease.native.frameId){eyeFrameMismatch_.fetch_add(1,std::memory_order_relaxed);return false;}
    pending_=lease;mask_=0;pendingDepth_={};
    if(sharingMode_.load()==3){
        auto& exportPair=legacyExports_[current_];if(exportPair.active)return Fail(7,E_UNEXPECTED);
        exportPair={lease,{},true,false,false,false};exportPair.startQpc=Qpc();inFlight_[current_]=true;
        // H2b: queue this pair's copy out of the shared private eyes NOW. The gate
        // was drained just above, so its resources are ours; PumpGraphics can
        // return early on a busy gate, which used to leave the copy unqueued while
        // the next frame's CaptureEye overwrote the left eye.
        if(queueAtPublish_)PumpLegacyExport(current_);
        // Pump polls each slot's OWN query/metadata and never publishes early.
        PumpGraphics();
        return true;
    }
    if(sharingMode_.load()==2){relay_.SealGameCopies();waitingGameCopies_=true;ExportStarted();PumpGraphics();return true;}
    const std::array<TextureSlice,2> sources={TextureSlice{eyes_[0][0].Get(),0},TextureSlice{eyes_[0][1].Get(),0}};
    const auto& tracking=lease.tracking;
    auto& producer=Producer();
    const auto published=producer.Publish(sources,{lease.native.frameId,tracking.spaceGeneration,tracking.generation,tracking.predictedNs},ticket_);
    if(published!=TransferResult::Ok){const auto error=producer.LastError();if(published==TransferResult::Faulted)producer.Reset();return Fail(8,error);}
    inFlight_[current_]=true;PublishDepth(lease);
    submitting_=true;PumpGraphics();return true;
}
void D3D11FrameBridge::Cancel(const ipc::FrameLease& lease)noexcept {
    // Called on the graphics thread after restoration. TryBegin is the only
    // method permitted on the visibility thread.
    pending_=lease;canceling_=true;submitting_=false;mask_=0;depthMask_=0;PumpGraphics();
}
void D3D11FrameBridge::PumpGraphics()noexcept {
    if(!connected_.load(std::memory_order_acquire))return;
    if(!channel_.TryDrainFeedbackGraphics())return;
    if(sharingMode_.load()==2){
        const auto relayRead=relay_.PollRelayRead();if(FAILED(relayRead))Fail(11,relayRead);
        if(waitingGameCopies_){const auto ready=relay_.GameCopiesReady();
            if(FAILED(ready)){waitingGameCopies_=false;Fail(11,ready);canceling_=true;}
            else if(ready==S_OK){
                waitingGameCopies_=false;const auto& t=pending_.tracking;
                const auto result=Producer().Publish(relay_.Sources(),{pending_.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},ticket_);
                // Even rejected/abandoned host pairs cannot allow the game to
                // overwrite a legacy source until the helper GPU read finishes.
                relay_.SealRelayRead();
                if(result==TransferResult::Ok){submitting_=true;inFlight_[current_]=true;ExportFinished(exportStartQpc_);exportStartQpc_=0;}
                else {Fail(8,Producer().LastError());canceling_=true;}
            }
        }
    }
    // Request order, so the host (which takes the oldest request first) never
    // waits on a newer pair that a lower pair index happened to hold.
    std::array<unsigned,PairSlots> order{};unsigned count=0;
    for(unsigned i=0;i<legacyExports_.size();++i)if(legacyExports_[i].active)order[count++]=i;
    for(unsigned i=1;i<count;++i)for(unsigned j=i;j>0&&legacyExports_[order[j]].lease.requestId<legacyExports_[order[j-1]].lease.requestId;--j)std::swap(order[j],order[j-1]);
    for(unsigned i=0;i<count;++i)PumpLegacyExport(order[i]);
    if(submitting_){const auto result=channel_.Submit(pending_,Producer().Descriptor(),ticket_,pendingDepth_.present?&pendingDepth_:nullptr);
        if(result==ipc::ChannelResult::Ok){submitting_=false;pendingDepth_={};++published_;}
        else if(result!=ipc::ChannelResult::Busy){Fail(9,HRESULT(result));submitting_=false;Producer().Reset();inFlight_[current_]=false;ReleaseDepth(current_);pendingDepth_={};canceling_=true;}
    }
    if(canceling_){const auto result=channel_.Cancel(pending_);if(result!=ipc::ChannelResult::Busy)canceling_=false;}
    channel_.PumpGraphics();
}
// Graphics thread, with the producer gate's feedback drained by the caller.
void D3D11FrameBridge::PumpLegacyExport(unsigned i)noexcept {
    auto& item=legacyExports_[i];if(!item.active)return;auto& producer=producers_[i];
    if(!item.ready&&!item.failed){
        const auto set=EyeSet(i);
        if(!item.copyQueued&&(eyeFrame_[set][0]!=item.lease.native.frameId||eyeFrame_[set][1]!=item.lease.native.frameId)){
            // The private eyes no longer hold this lease's frame: never export them.
            eyeFrameMismatch_.fetch_add(1,std::memory_order_relaxed);inFlight_[i]=false;item.failed=true;
        }else{
            const std::array<TextureSlice,2> sources={TextureSlice{eyes_[set][0].Get(),0},TextureSlice{eyes_[set][1].Get(),0}};
            const auto& t=item.lease.tracking;
            const auto result=producer.Publish(sources,{item.lease.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},item.ticket);
            if(result==TransferResult::Ok){item.copyQueued=item.ready=true;ExportFinished(item.startQpc);}
            else if(result==TransferResult::Busy){
                // Publish has queued the source copies before returning Busy.
                // A busy channel gate merely retains serial admission this time.
                item.copyQueued=true;channel_.DeferGpu(item.lease);
            }
            else {Fail(8,producer.LastError());producer.Reset();inFlight_[i]=false;item.failed=true;}
        }
    }
    if(item.ready){
        const auto result=channel_.Submit(item.lease,producer.Descriptor(),item.ticket);
        if(result==ipc::ChannelResult::Ok){item={};++published_;return;}
        if(result!=ipc::ChannelResult::Busy){Fail(9,HRESULT(result));producer.Reset();inFlight_[i]=false;item.ready=false;item.failed=true;}
    }
    if(item.failed&&channel_.Cancel(item.lease)!=ipc::ChannelResult::Busy)item={};
}
void D3D11FrameBridge::PairConsumed(const PairTicket& ticket,bool consumed)noexcept {
    const int pair=PairIndex(ticket);
    if(pair>=0){inFlight_[pair]=false;ReleaseDepth(unsigned(pair));}
    if(consumed){if(pair>=0)producers_[pair].Acknowledge(ticket,true);++consumed_;return;}
    ++discarded_;
    // Reclaim the mutex/ack and keep the textures, descriptor and generation so the
    // host reopens nothing; only a pair that cannot be reused is recreated.
    if(pair<0)return; // Already recreated (requirements changed or faulted).
    if(producers_[pair].Discard(ticket))++reclaimed_;else producers_[pair].Reset();
}
void D3D11FrameBridge::CloseGraphics()noexcept {
    connected_.store(false,std::memory_order_release);channel_.CloseGraphics();ResetPairs();relay_.Reset();depthCapture_.Reset();waitingGameCopies_=false;
    for(auto& set:eyes_)for(auto& eye:set)eye.Reset();eyeFrame_={};context_.Reset();device_.Reset();submitting_=canceling_=false;mask_=0;
}
}
