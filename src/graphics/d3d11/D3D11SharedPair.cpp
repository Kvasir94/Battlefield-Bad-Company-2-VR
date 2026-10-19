#include "fvr/graphics/D3D11SharedPair.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
// A copy still incomplete after CopyLateNs is only COUNTED (it stays pending: GPU
// contention from a game, compositor or capture tool routinely delays it past a
// frame). Only CopyRetireNs, or a removed device, retires the generation.
constexpr std::uint64_t CopyLateNs=20000000,CopyRetireNs=250000000,ReclaimRetireNs=250000000;
bool PairReuseEnabled() noexcept {
    // FVR_PAIR_REUSE=0 restores the old behaviour: a discarded pair is reset and
    // both shared textures are recreated.
    static const bool enabled=[]{wchar_t value[8]{};const auto length=GetEnvironmentVariableW(L"FVR_PAIR_REUSE",value,8);return !(length==1&&value[0]==L'0');}();
    return enabled;
}
std::atomic<CopyLateTestHook> copyLateTestHook{nullptr};
std::uint64_t MonotonicNs() noexcept {
    LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
    return std::uint64_t(q.QuadPart/f.QuadPart)*1000000000ull+std::uint64_t(q.QuadPart%f.QuadPart)*1000000000ull/std::uint64_t(f.QuadPart);
}
struct SharedEye {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IDXGIKeyedMutex> mutex;
    HANDLE handle=nullptr;
    ~SharedEye(){if(handle)CloseHandle(handle);}
};
struct PairState {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    TextureDescriptor descriptor{};
    std::array<SharedEye,2> eyes{};
    std::uint64_t sequence=0,frameId=0;
    bool faulted=false,publishing=false,awaitingAck=false;FrameMetadata pending{};
    // Keyed pair discarded by the consumer before it took the mutex: per-eye
    // progress of handing the key back, so a contended retry resumes.
    bool reclaiming=false;std::array<bool,2> reclaimed{};std::uint64_t reclaimStartedNs=0;
    ComPtr<ID3D11Query> completion;
};
std::wstring ResourceName(const TextureDescriptor& d,unsigned eye) {
    std::wstring name=L"Local\\FrostbiteVR.Texture.v2.";
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(auto b:d.session){name+=hex[b>>4];name+=hex[b&15];}
    name+=L"."+std::to_wstring(d.resourceEpoch)+L"."+std::to_wstring(eye);
    return name;
}
HRESULT AdapterIdentity(ID3D11Device* device,TextureDescriptor& descriptor) noexcept {
    ComPtr<IDXGIDevice> dxgi;HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&dxgi));if(FAILED(hr))return hr;
    ComPtr<IDXGIAdapter> adapter;hr=dxgi->GetAdapter(&adapter);if(FAILED(hr))return hr;
    DXGI_ADAPTER_DESC desc{};hr=adapter->GetDesc(&desc);if(FAILED(hr))return hr;
    descriptor.adapterLow=desc.AdapterLuid.LowPart;descriptor.adapterHigh=desc.AdapterLuid.HighPart;return S_OK;
}
bool SlicesValid(const PairState& s,const std::array<TextureSlice,2>& slices) noexcept {
    if(!slices[0].texture||!slices[1].texture)return false;
    ComPtr<IUnknown> identities[2];
    for(unsigned eye=0;eye<2;++eye){
        const auto& slice=slices[eye];D3D11_TEXTURE2D_DESC d{};slice.texture->GetDesc(&d);
        if(!CopyCompatibleFormats(std::uint32_t(d.Format),s.descriptor.format) || d.SampleDesc.Count!=1 || d.SampleDesc.Quality ||
            !d.MipLevels || d.MipLevels>15 || !d.ArraySize ||
            slice.subresource>=std::uint64_t(d.MipLevels)*d.ArraySize || d.Usage!=D3D11_USAGE_DEFAULT)return false;
        const auto mip=slice.subresource%d.MipLevels;
        if((std::max)(1u,d.Width>>mip)!=s.descriptor.width ||
           (std::max)(1u,d.Height>>mip)!=s.descriptor.height)return false;
        ComPtr<ID3D11Device> owner;slice.texture->GetDevice(&owner);if(owner.Get()!=s.device.Get())return false;
        if(FAILED(slice.texture->QueryInterface(IID_PPV_ARGS(&identities[eye]))))return false;
        for(const auto& shared:s.eyes){
            ComPtr<IUnknown> identity;if(FAILED(shared.texture.As(&identity)) || identity==identities[eye])return false;
        }
    }
    return identities[0]!=identities[1] || slices[0].subresource!=slices[1].subresource;
}
TransferResult LockPair(PairState& s,std::uint64_t key,HRESULT& error) noexcept {
    unsigned held=0;
    for(;held<2;++held){
        error=s.eyes[held].mutex->AcquireSync(key,0);
        // WAIT_TIMEOUT/WAIT_ABANDONED are positive HRESULTs. SUCCEEDED is wrong.
        if(error!=S_OK){
            const auto failure=error;
            for(unsigned i=0;i<held;++i){const auto release=s.eyes[i].mutex->ReleaseSync(key);if(release!=S_OK)s.faulted=true;}
            if(failure!=HRESULT(WAIT_TIMEOUT))s.faulted=true;
            return s.faulted?TransferResult::Faulted:TransferResult::Busy;
        }
    }
    error=S_OK;return TransferResult::Ok;
}
TransferResult UnlockPair(PairState& s,std::uint64_t key,HRESULT& error) noexcept {
    HRESULT first=S_OK;
    for(auto& eye:s.eyes){const auto hr=eye.mutex->ReleaseSync(key);if(first==S_OK && hr!=S_OK)first=hr;}
    if(first!=S_OK){s.faulted=true;error=first;return TransferResult::Faulted;}
    return TransferResult::Ok;
}
TransferResult DeviceStatus(PairState& s,HRESULT& error) noexcept {
    error=s.device->GetDeviceRemovedReason();
    if(error!=S_OK){s.faulted=true;return TransferResult::Faulted;}
    return TransferResult::Ok;
}
}
struct D3D11PairProducer::State:PairState{};
namespace {
// Key sequence (n = producer sequence after its last Publish): the producer
// released the mutex at 2n-1 and the consumer would take 2n-1 and release 2n,
// which is the key the next Publish acquires. When the consumer never took the
// pair, take 2n-1 ourselves and release 2n; if the consumer already completed
// (mutex at 2n) the pair is equally reusable.
TransferResult ReclaimKeyed(PairState& s,HRESULT& error) noexcept {
    const auto held=s.sequence*2-1,next=s.sequence*2;
    for(unsigned eye=0;eye<2;++eye){
        if(s.reclaimed[eye])continue;
        auto& mutex=*s.eyes[eye].mutex.Get();
        error=mutex.AcquireSync(held,0);
        if(error==S_OK){const auto release=mutex.ReleaseSync(next);if(release!=S_OK){error=release;s.faulted=true;return TransferResult::Faulted;}}
        else if(error==HRESULT(WAIT_TIMEOUT)){
            error=mutex.AcquireSync(next,0);
            if(error==S_OK){const auto release=mutex.ReleaseSync(next);if(release!=S_OK){error=release;s.faulted=true;return TransferResult::Faulted;}}
            else if(error==HRESULT(WAIT_TIMEOUT))return TransferResult::Busy;
            else {s.faulted=true;return TransferResult::Faulted;}
        }else {s.faulted=true;return TransferResult::Faulted;}
        s.reclaimed[eye]=true;
    }
    s.reclaiming=false;error=S_OK;return TransferResult::Ok;
}
}
struct D3D11PairConsumer::State:PairState {
    PairTicket copyTicket{};
    std::array<ComPtr<ID3D11Texture2D>,2> destinations;
    std::uint64_t copyStartedNs=0;
    bool hasCopy=false;
    CopyPollResult copyResult=CopyPollResult::Invalid;
    HRESULT copyError=S_OK;
    bool lateCounted=false;
};
D3D11PairProducer::D3D11PairProducer()=default;
D3D11PairProducer::~D3D11PairProducer()=default;
void D3D11PairProducer::Reset() noexcept{state_.reset();error_=S_OK;}
HRESULT D3D11PairProducer::LastError()const noexcept{return error_;}
TextureDescriptor D3D11PairProducer::Descriptor()const noexcept{return state_?state_->descriptor:TextureDescriptor{};}
TransferResult D3D11PairProducer::Create(ID3D11Device* device,UINT width,UINT height,DXGI_FORMAT format,std::uint64_t epoch,TextureSharing sharing) noexcept {
    Reset();operation_=1;if(!device){error_=E_INVALIDARG;return TransferResult::Invalid;}
    try {
        auto next=std::make_unique<State>();auto& d=next->descriptor;
        d.width=width;d.height=height;d.format=format;d.resourceEpoch=epoch;
        operation_=2;GUID id{};error_=CoCreateGuid(&id);if(FAILED(error_))return TransferResult::Unavailable;
        std::memcpy(d.session.data(),&id,sizeof(id));
        operation_=3;if(!Valid(d)){error_=E_INVALIDARG;return TransferResult::Invalid;}
        next->device=device;device->GetImmediateContext(&next->context);
        operation_=4;error_=AdapterIdentity(device,d);if(FAILED(error_))return TransferResult::Unavailable;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=sharing==TextureSharing::LegacyFenced?(CopyCompatibleFormats(format,28)?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM):format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags=sharing==TextureSharing::LegacyFenced?D3D11_RESOURCE_MISC_SHARED:D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        std::array<std::uint64_t,2> legacyIds{};
        for(unsigned eye=0;eye<2;++eye){
            auto& target=next->eyes[eye];operation_=5;error_=device->CreateTexture2D(&desc,nullptr,&target.texture);
            if(FAILED(error_))return TransferResult::Unavailable;
            if(sharing==TextureSharing::LegacyFenced){ComPtr<IDXGIResource> resource;error_=target.texture.As(&resource);if(FAILED(error_))return TransferResult::Unavailable;
                HANDLE id=nullptr;error_=resource->GetSharedHandle(&id);if(FAILED(error_)||!id)return TransferResult::Unavailable;legacyIds[eye]=reinterpret_cast<std::uintptr_t>(id);continue;}
            operation_=6;error_=target.texture.As(&target.mutex);if(FAILED(error_))return TransferResult::Unavailable;
            ComPtr<IDXGIResource1> resource;operation_=7;error_=target.texture.As(&resource);if(FAILED(error_))return TransferResult::Unavailable;
            const auto name=ResourceName(d,eye);
            operation_=8;error_=resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,name.c_str(),&target.handle);
            if(FAILED(error_))return TransferResult::Unavailable;
        }
        if(sharing==TextureSharing::LegacyFenced){d.sharing=sharing;std::memcpy(d.session.data(),legacyIds.data(),16);
            D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};error_=device->CreateQuery(&query,&next->completion);if(FAILED(error_))return TransferResult::Unavailable;}
        state_=std::move(next);return TransferResult::Ok;
    }catch(...){error_=E_OUTOFMEMORY;return TransferResult::Unavailable;}
}
TransferResult D3D11PairProducer::Publish(const std::array<TextureSlice,2>& sources,const FrameMetadata& frame,PairTicket& ticket) noexcept {
    ticket={};
    if(!state_){error_=E_UNEXPECTED;return TransferResult::Unavailable;}
    auto& s=*state_;if(s.faulted)return TransferResult::Faulted;
    if(!frame.frameId||frame.frameId<=s.frameId||!frame.spaceGeneration||!frame.trackingGeneration||
        frame.predictedNs<=0||s.sequence>=0x7fffffffffffffffULL||!SlicesValid(s,sources)){
        error_=E_INVALIDARG;return TransferResult::Invalid;
    }
    if(DeviceStatus(s,error_)!=TransferResult::Ok)return TransferResult::Faulted;
    if(s.reclaiming){
        const auto reclaim=ReclaimKeyed(s,error_);
        if(reclaim==TransferResult::Busy){
            if(MonotonicNs()-s.reclaimStartedNs>=ReclaimRetireNs){s.faulted=true;return TransferResult::Faulted;}
            return TransferResult::Busy;
        }
        if(reclaim!=TransferResult::Ok)return reclaim;
    }
    if(s.descriptor.sharing==TextureSharing::LegacyFenced){
        if(s.awaitingAck)return TransferResult::Busy;
        if(!s.publishing){for(unsigned eye=0;eye<2;++eye)s.context->CopySubresourceRegion(s.eyes[eye].texture.Get(),0,0,0,0,sources[eye].texture,sources[eye].subresource,nullptr);
            s.context->End(s.completion.Get());s.context->Flush();s.pending=frame;s.publishing=true;}
        if(frame.frameId!=s.pending.frameId||frame.trackingGeneration!=s.pending.trackingGeneration||frame.spaceGeneration!=s.pending.spaceGeneration||frame.predictedNs!=s.pending.predictedNs){error_=E_INVALIDARG;return TransferResult::Invalid;}
        BOOL done=FALSE;error_=s.context->GetData(s.completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(error_==S_FALSE||(error_==S_OK&&!done))return TransferResult::Busy;
        if(FAILED(error_)){s.faulted=true;return TransferResult::Faulted;}
        s.publishing=false;s.awaitingAck=true;++s.sequence;s.frameId=frame.frameId;
        ticket.resourceEpoch=s.descriptor.resourceEpoch;ticket.sequence=s.sequence;ticket.frameId=frame.frameId;
        ticket.spaceGeneration=frame.spaceGeneration;ticket.trackingGeneration=frame.trackingGeneration;ticket.predictedNs=frame.predictedNs;ticket.session=s.descriptor.session;return TransferResult::Ok;
    }
    const auto locked=LockPair(s,s.sequence*2,error_);if(locked!=TransferResult::Ok)return locked;
    for(unsigned eye=0;eye<2;++eye)s.context->CopySubresourceRegion(s.eyes[eye].texture.Get(),0,0,0,0,sources[eye].texture,sources[eye].subresource,nullptr);
    s.context->Flush();
    const auto deviceResult=DeviceStatus(s,error_);
    const auto next=s.sequence+1;
    const auto unlocked=UnlockPair(s,deviceResult==TransferResult::Ok?next*2-1:s.sequence*2,error_);
    if(deviceResult!=TransferResult::Ok||unlocked!=TransferResult::Ok)return TransferResult::Faulted;
    s.sequence=next;s.frameId=frame.frameId;
    ticket.resourceEpoch=s.descriptor.resourceEpoch;ticket.sequence=next;ticket.frameId=frame.frameId;
    ticket.spaceGeneration=frame.spaceGeneration;ticket.trackingGeneration=frame.trackingGeneration;
    ticket.predictedNs=frame.predictedNs;ticket.session=s.descriptor.session;
    return TransferResult::Ok;
}
void D3D11PairProducer::Acknowledge(const PairTicket& ticket,bool consumed)noexcept {
    if(!state_||state_->descriptor.sharing!=TextureSharing::LegacyFenced)return;
    if(!consumed){Reset();return;}
    if(Valid(ticket,state_->descriptor)&&ticket.sequence==state_->sequence&&ticket.frameId==state_->frameId)state_->awaitingAck=false;
}
bool D3D11PairProducer::Discard(const PairTicket& ticket)noexcept {
    if(!state_||!PairReuseEnabled())return false;
    auto& s=*state_;
    // Only the most recent publication can be handed back; anything else leaves
    // the key sequence unknowable, so the caller recreates the pair.
    if(s.faulted||!Valid(ticket,s.descriptor)||ticket.sequence!=s.sequence||ticket.frameId!=s.frameId)return false;
    if(s.descriptor.sharing==TextureSharing::LegacyFenced){
        if(!s.awaitingAck)return false;
        s.awaitingAck=false;return true;
    }
    if(s.reclaiming)return false;
    s.reclaiming=true;s.reclaimed={};s.reclaimStartedNs=MonotonicNs();
    const auto result=ReclaimKeyed(s,error_);
    // Busy: the consumer holds a mutex right now; Publish retries until the bound.
    return result!=TransferResult::Faulted;
}
D3D11PairConsumer::D3D11PairConsumer()=default;
D3D11PairConsumer::~D3D11PairConsumer()=default;
void D3D11PairConsumer::Reset()noexcept {
    // Reset abandons this resource generation, never acknowledges its ticket.
    // D3D11 retains resources referenced by submitted GPU commands until their
    // execution finishes, including when application COM references are released.
    state_.reset();error_=S_OK;
}
HRESULT D3D11PairConsumer::LastError()const noexcept{return error_;}
bool D3D11PairConsumer::CopyPending()const noexcept {
    return state_&&state_->hasCopy&&state_->copyResult==CopyPollResult::Pending;
}
TransferResult D3D11PairConsumer::Open(ID3D11Device* device,const TextureDescriptor& descriptor) noexcept {
    Reset();if(!device||!Valid(descriptor)){error_=E_INVALIDARG;return TransferResult::Invalid;}
    try {
        auto next=std::make_unique<State>();next->device=device;next->descriptor=descriptor;device->GetImmediateContext(&next->context);
        TextureDescriptor actual{};error_=AdapterIdentity(device,actual);if(FAILED(error_))return TransferResult::Unavailable;
        if(actual.adapterLow!=descriptor.adapterLow||actual.adapterHigh!=descriptor.adapterHigh){error_=E_INVALIDARG;return TransferResult::Invalid;}
        ComPtr<ID3D11Device1> device1;if(descriptor.sharing==TextureSharing::NamedKeyed){error_=device->QueryInterface(IID_PPV_ARGS(&device1));if(FAILED(error_))return TransferResult::Unavailable;}
        for(unsigned eye=0;eye<2;++eye){
            auto& target=next->eyes[eye];
            if(descriptor.sharing==TextureSharing::NamedKeyed){const auto name=ResourceName(descriptor,eye);error_=device1->OpenSharedResourceByName(name.c_str(),DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,IID_PPV_ARGS(&target.texture));}
            else {const auto id=LegacyResourceIds(descriptor)[eye];if(id>UINTPTR_MAX){error_=E_INVALIDARG;return TransferResult::Invalid;}error_=device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(id)),IID_PPV_ARGS(&target.texture));}
            if(FAILED(error_))return TransferResult::Unavailable;
            D3D11_TEXTURE2D_DESC d{};target.texture->GetDesc(&d);
            if(d.Width!=descriptor.width||d.Height!=descriptor.height||!CopyCompatibleFormats(d.Format,descriptor.format)||d.MipLevels!=1||
                d.ArraySize!=1||d.SampleDesc.Count!=1||d.SampleDesc.Quality||d.Usage!=D3D11_USAGE_DEFAULT||
                (descriptor.sharing==TextureSharing::NamedKeyed?(!(d.MiscFlags&D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)||!(d.MiscFlags&D3D11_RESOURCE_MISC_SHARED_NTHANDLE)):(!(d.MiscFlags&D3D11_RESOURCE_MISC_SHARED)))){
                error_=E_INVALIDARG;return TransferResult::Invalid;
            }
            if(descriptor.sharing==TextureSharing::NamedKeyed){error_=target.texture.As(&target.mutex);if(FAILED(error_))return TransferResult::Unavailable;}
        }
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};error_=device->CreateQuery(&query,&next->completion);if(FAILED(error_))return TransferResult::Unavailable;
        state_=std::move(next);return TransferResult::Ok;
    }catch(...){error_=E_OUTOFMEMORY;return TransferResult::Unavailable;}
}
TransferResult D3D11PairConsumer::QueueCopy(const PairTicket& ticket,const std::array<TextureSlice,2>& destinations) noexcept {
    if(!state_){error_=E_UNEXPECTED;return TransferResult::Unavailable;}
    auto& s=*state_;if(s.faulted)return TransferResult::Faulted;
    if(!Valid(ticket,s.descriptor)||ticket.sequence<=s.sequence||ticket.frameId<=s.frameId||!SlicesValid(s,destinations)){
        error_=E_INVALIDARG;return TransferResult::Invalid;
    }
    if(CopyPending())return TransferResult::Busy;
    if(DeviceStatus(s,error_)!=TransferResult::Ok)return TransferResult::Faulted;
    if(s.descriptor.sharing==TextureSharing::NamedKeyed){
        const auto locked=LockPair(s,ticket.sequence*2-1,error_);if(locked!=TransferResult::Ok)return locked;
    }
    s.copyTicket=ticket;s.hasCopy=true;s.copyResult=CopyPollResult::Pending;s.copyError=S_OK;s.lateCounted=false;
    for(unsigned eye=0;eye<2;++eye){
        s.destinations[eye]=destinations[eye].texture;
        s.context->CopySubresourceRegion(destinations[eye].texture,destinations[eye].subresource,0,0,0,s.eyes[eye].texture.Get(),0,nullptr);
    }
    s.context->End(s.completion.Get());s.context->Flush();s.copyStartedNs=MonotonicNs();
    const auto deviceResult=DeviceStatus(s,error_);
    const auto deviceError=error_;
    const auto unlocked=s.descriptor.sharing==TextureSharing::NamedKeyed?UnlockPair(s,ticket.sequence*2,error_):TransferResult::Ok;
    if(deviceResult!=TransferResult::Ok)error_=deviceError;
    if(deviceResult!=TransferResult::Ok||unlocked!=TransferResult::Ok){s.copyResult=CopyPollResult::Faulted;s.copyError=error_;return TransferResult::Faulted;}
    return TransferResult::Ok;
}
CopyPollResult D3D11PairConsumer::PollCopy(const PairTicket& ticket) noexcept {
    if(!state_){error_=E_UNEXPECTED;return CopyPollResult::Unavailable;}
    auto& s=*state_;
    if(!s.hasCopy||std::memcmp(&ticket,&s.copyTicket,sizeof(ticket))){error_=E_INVALIDARG;return CopyPollResult::Invalid;}
    if(s.copyResult!=CopyPollResult::Pending){error_=s.copyError;return s.copyResult;}
    const auto hook=copyLateTestHook.load(std::memory_order_acquire);
    const bool forcedLate=hook&&hook(ticket);
    BOOL done=FALSE;error_=forcedLate?S_FALSE:s.context->GetData(s.completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(error_==S_OK&&done){
        s.sequence=ticket.sequence;s.frameId=ticket.frameId;s.copyResult=CopyPollResult::Complete;
        for(auto& destination:s.destinations)destination.Reset();
    }else if(FAILED(error_)){
        s.faulted=true;s.copyResult=CopyPollResult::Faulted;
    }else {
        const auto elapsed=MonotonicNs()-s.copyStartedNs;
        if(!forcedLate&&elapsed>=CopyLateNs&&!s.lateCounted){s.lateCounted=true;++lateCopies_;}
        if(!forcedLate&&elapsed>=CopyLateNs&&DeviceStatus(s,error_)!=TransferResult::Ok){
            s.copyResult=CopyPollResult::Faulted; // Device removed while waiting.
        }else if(forcedLate||elapsed>=CopyRetireNs){
            // A late completed event is still safe to acknowledge, so lateness alone
            // never retires the generation. An event incomplete after the (much
            // larger) retire bound does: negative feedback must not reuse its pixels.
            s.faulted=true;error_=HRESULT(WAIT_TIMEOUT);s.copyResult=CopyPollResult::TimedOut;
        }
    }
    s.copyError=error_;return s.copyResult;
}
ID3D11Query* D3D11PairConsumer::TimedOutCompletion() const noexcept {
    return state_&&state_->hasCopy&&state_->copyResult==CopyPollResult::TimedOut?state_->completion.Get():nullptr;
}
void SetCopyLateTestHook(CopyLateTestHook hook) noexcept{copyLateTestHook.store(hook,std::memory_order_release);}
TransferResult D3D11PairConsumer::Copy(const PairTicket& ticket,const std::array<TextureSlice,2>& destinations) noexcept {
    if(!state_){error_=E_UNEXPECTED;return TransferResult::Unavailable;}
    auto& s=*state_;if(s.faulted)return TransferResult::Faulted;
    if(s.descriptor.sharing==TextureSharing::LegacyFenced){
        const auto queued=QueueCopy(ticket,destinations);if(queued!=TransferResult::Ok)return queued;
        while(true){
            switch(PollCopy(ticket)){
            case CopyPollResult::Complete:return TransferResult::Ok;
            case CopyPollResult::Pending:
                // The synchronous path keeps the original bounded 20 ms wait; nobody
                // polls it afterwards, so it retires the generation itself.
                if(MonotonicNs()-s.copyStartedNs>=CopyLateNs){s.faulted=true;s.copyResult=CopyPollResult::TimedOut;error_=HRESULT(WAIT_TIMEOUT);return TransferResult::Busy;}
                SwitchToThread();break;
            case CopyPollResult::TimedOut:return TransferResult::Busy;
            case CopyPollResult::Invalid:return TransferResult::Invalid;
            case CopyPollResult::Unavailable:return TransferResult::Unavailable;
            case CopyPollResult::Faulted:return TransferResult::Faulted;
            }
        }
    }
    if(!Valid(ticket,s.descriptor)||ticket.sequence<=s.sequence||ticket.frameId<=s.frameId||!SlicesValid(s,destinations)){
        error_=E_INVALIDARG;return TransferResult::Invalid;
    }
    if(CopyPending())return TransferResult::Busy;
    if(DeviceStatus(s,error_)!=TransferResult::Ok)return TransferResult::Faulted;
    const auto locked=LockPair(s,ticket.sequence*2-1,error_);if(locked!=TransferResult::Ok)return locked;
    for(unsigned eye=0;eye<2;++eye)s.context->CopySubresourceRegion(destinations[eye].texture,destinations[eye].subresource,0,0,0,s.eyes[eye].texture.Get(),0,nullptr);
    s.context->Flush();const auto deviceResult=DeviceStatus(s,error_);
    const auto unlocked=UnlockPair(s,ticket.sequence*2,error_);
    if(deviceResult!=TransferResult::Ok||unlocked!=TransferResult::Ok)return TransferResult::Faulted;
    s.sequence=ticket.sequence;s.frameId=ticket.frameId;return TransferResult::Ok;
}
}
