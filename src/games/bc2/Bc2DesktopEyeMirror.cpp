#include "Bc2DesktopEyeMirror.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
namespace fvr::bc2 {
using Microsoft::WRL::ComPtr;
namespace {
bool SameFamily(DXGI_FORMAT a,DXGI_FORMAT b)noexcept {
    const auto rgba=[](DXGI_FORMAT f){return f==DXGI_FORMAT_R8G8B8A8_TYPELESS||f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;};
    const auto bgra=[](DXGI_FORMAT f){return f==DXGI_FORMAT_B8G8R8A8_TYPELESS||f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;};
    return (rgba(a)&&rgba(b))||(bgra(a)&&bgra(b));
}
}
struct DesktopEyeMirror::Impl {
    bool mirror=false,capture=false;std::filesystem::path folder;
    ComPtr<ID3D11Texture2D> eye;D3D11_TEXTURE2D_DESC eyeDesc{};std::int64_t eyeNs=0;unsigned eyeFrame=0;
    // Capture: a staging ring read three presents later without waiting.
    std::array<ComPtr<ID3D11Texture2D>,4> staging;std::array<unsigned,4> stagedPresent{},stagedFrame{};std::array<bool,4> stagedMirror{},stagedPending{};
    unsigned presentIndex=0;std::ofstream rows;
    std::mutex m;std::condition_variable cv;std::deque<std::pair<std::filesystem::path,std::vector<unsigned char>>> queue;bool stop=false;std::thread writer;
    DesktopEyeMirrorStats stats{};
    void Write(std::filesystem::path p,std::vector<unsigned char> bytes){
        {std::lock_guard lock(m);if(queue.size()>=64){++stats.captureDropped;return;}queue.emplace_back(std::move(p),std::move(bytes));}cv.notify_one();}
};
DesktopEyeMirror::DesktopEyeMirror():impl_(std::make_unique<Impl>()){}
DesktopEyeMirror::~DesktopEyeMirror(){Finish();}
bool DesktopEyeMirror::Active()const noexcept{return impl_->mirror||impl_->capture;}
DesktopEyeMirrorStats DesktopEyeMirror::Statistics()const noexcept{
    // The writer changes captureDropped under this same mutex.
    std::lock_guard lock(impl_->m);return impl_->stats;
}
void DesktopEyeMirror::Configure(bool mirror,bool capture,const std::filesystem::path& folder){
    auto& s=*impl_;s.mirror=mirror;s.capture=capture&&!folder.empty();s.folder=folder;
    if(!s.capture)return;
    std::filesystem::create_directories(folder/"desktop");s.rows.open(folder/"desktop.jsonl");
    s.writer=std::thread([&s]{for(;;){std::pair<std::filesystem::path,std::vector<unsigned char>> item;
        {std::unique_lock lock(s.m);s.cv.wait(lock,[&s]{return s.stop||!s.queue.empty();});if(s.queue.empty())return;item=std::move(s.queue.front());s.queue.pop_front();}
        // Skip (and count) rather than fill the disk: raw frames filled D: on 2026-10-08.
        ULARGE_INTEGER free{};if(GetDiskFreeSpaceExW(item.first.parent_path().c_str(),&free,nullptr,nullptr)&&free.QuadPart<(2ull<<30)){std::lock_guard lock(s.m);++s.stats.captureDropped;continue;}
        std::ofstream out(item.first,std::ios::binary);out.write(reinterpret_cast<const char*>(item.second.data()),std::streamsize(item.second.size()));}});
}
void DesktopEyeMirror::Finish()noexcept{
    auto& s=*impl_;{std::lock_guard lock(s.m);s.stop=true;}s.cv.notify_one();
    if(s.writer.joinable())s.writer.join();if(s.rows.is_open())s.rows.close();
}
void DesktopEyeMirror::OnLeftEye(ID3D11DeviceContext* context,ID3D11Texture2D* source,std::int64_t nowNs,unsigned frame)noexcept{
    auto& s=*impl_;if(!s.mirror||!context||!source)return;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    ComPtr<ID3D11Device> device,contextDevice,eyeDevice;
    source->GetDevice(&device);context->GetDevice(&contextDevice);
    if(!device||device.Get()!=contextDevice.Get()||d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1){
        s.eye.Reset();s.eyeNs=0;return;
    }
    if(s.eye)s.eye->GetDevice(&eyeDevice);
    if(!s.eye||device.Get()!=eyeDevice.Get()||d.Width!=s.eyeDesc.Width||d.Height!=s.eyeDesc.Height||d.Format!=s.eyeDesc.Format){
        s.eye.Reset();
        D3D11_TEXTURE2D_DESC copy=d;copy.MipLevels=1;copy.ArraySize=1;copy.SampleDesc={1,0};copy.Usage=D3D11_USAGE_DEFAULT;copy.BindFlags=0;copy.CPUAccessFlags=0;copy.MiscFlags=0;
        if(d.ArraySize!=1||d.SampleDesc.Count!=1||!device||FAILED(device->CreateTexture2D(&copy,nullptr,&s.eye)))return;s.eyeDesc=d;
    }
    context->CopyResource(s.eye.Get(),source);s.eyeNs=nowNs;s.eyeFrame=frame;++s.stats.eyes;
}
void DesktopEyeMirror::OnPresent(IDXGISwapChain* chain,ID3D11DeviceContext* context,std::int64_t nowNs,unsigned frame)noexcept{
    auto& s=*impl_;if(!Active()||!chain||!context)return;++s.stats.presents;
    ComPtr<ID3D11Texture2D> back;if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&back))))return;
    D3D11_TEXTURE2D_DESC b{};back->GetDesc(&b);bool mirrored=false;
    ComPtr<ID3D11Device> device,contextDevice,eyeDevice;back->GetDevice(&device);context->GetDevice(&contextDevice);
    if(s.eye)s.eye->GetDevice(&eyeDevice);
    const bool deviceMatches=device&&device.Get()==contextDevice.Get();
    if(s.mirror){
        if(!s.eye||nowNs<s.eyeNs||nowNs-s.eyeNs>300000000ll)++s.stats.stale;
        else if(!deviceMatches||device.Get()!=eyeDevice.Get()||b.Width!=s.eyeDesc.Width||b.Height!=s.eyeDesc.Height||b.ArraySize!=1||b.MipLevels!=1||b.SampleDesc.Count!=1||!SameFamily(b.Format,s.eyeDesc.Format))++s.stats.mismatched;
        else {context->CopyResource(back.Get(),s.eye.Get());mirrored=true;++s.stats.mirrored;}
        if(!mirrored){
            // Explicit VR desktop output must not reveal a different native
            // camera when its eye producer pauses or changes dimensions.
            // Clear only this presentation buffer; XR freshness is independent.
            // Do not cache the buffer/RTV: retaining it would impede ResizeBuffers.
            ++s.stats.waitingAttempts;
            ComPtr<ID3D11RenderTargetView> waiting;
            const HRESULT result=deviceMatches?device->CreateRenderTargetView(back.Get(),nullptr,&waiting):E_INVALIDARG;
            s.stats.waitingLastHresult=static_cast<std::int32_t>(result);
            if(SUCCEEDED(result)){
                constexpr float waitingColor[4]{0.025f,0.035f,0.05f,1.f};
                context->ClearRenderTargetView(waiting.Get(),waitingColor);
                ++s.stats.waitingClears; // Command issued; not a GPU completion receipt.
            }else ++s.stats.waitingFailures;
        }
    }
    if(!s.capture||!deviceMatches)return;
    const unsigned slot=s.presentIndex%unsigned(s.staging.size());
    // Decode each old slot using its own size/format, never the new backbuffer.
    // A changed device cannot map an old device's staging resource.
    D3D11_TEXTURE2D_DESC staged{};
    if(s.staging[slot]){
        ComPtr<ID3D11Device> stagedDevice;s.staging[slot]->GetDevice(&stagedDevice);
        if(stagedDevice.Get()!=device.Get()){s.staging[slot].Reset();s.stagedPending[slot]=false;}
        else s.staging[slot]->GetDesc(&staged);
    }
    if(s.stagedPending[slot]){
        D3D11_MAPPED_SUBRESOURCE map{};const auto hr=context->Map(s.staging[slot].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
        if(hr==DXGI_ERROR_WAS_STILL_DRAWING){++s.stats.captureBusy;}
        else if(SUCCEEDED(hr)){
            const unsigned w=staged.Width/4,h=staged.Height/4;std::vector<unsigned char> out(std::size_t(w)*h*3);
            const bool bgr=staged.Format==DXGI_FORMAT_B8G8R8A8_UNORM||staged.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||staged.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS;
            for(unsigned y=0;y<h;++y){const auto* row=static_cast<const unsigned char*>(map.pData)+std::size_t(y*4+1)*map.RowPitch;
                for(unsigned x=0;x<w;++x){const auto* p=row+std::size_t(x*4+1)*4;auto* q=&out[(std::size_t(y)*w+x)*3];
                    q[0]=p[bgr?2:0];q[1]=p[1];q[2]=p[bgr?0:2];}}
            context->Unmap(s.staging[slot].Get(),0);
            char name[32];std::snprintf(name,sizeof name,"d-%05u.rgb",s.stagedPresent[slot]);s.Write(s.folder/"desktop"/name,std::move(out));
            s.rows<<"{\"present\":"<<s.stagedPresent[slot]<<",\"frame\":"<<s.stagedFrame[slot]<<",\"mirrored\":"<<(s.stagedMirror[slot]?"true":"false")
                <<",\"width\":"<<w<<",\"height\":"<<h<<"}\n";++s.stats.captured;
        }
        s.stagedPending[slot]=false;
    }
    if(b.ArraySize!=1||b.MipLevels!=1||b.SampleDesc.Count!=1||
       (!SameFamily(b.Format,DXGI_FORMAT_R8G8B8A8_UNORM)&&!SameFamily(b.Format,DXGI_FORMAT_B8G8R8A8_UNORM)))return;
    if(s.staging[slot]&&(staged.Width!=b.Width||staged.Height!=b.Height||staged.Format!=b.Format||
       staged.ArraySize!=b.ArraySize||staged.MipLevels!=b.MipLevels||staged.SampleDesc.Count!=b.SampleDesc.Count||
       staged.SampleDesc.Quality!=b.SampleDesc.Quality))s.staging[slot].Reset();
    if(!s.staging[slot]){D3D11_TEXTURE2D_DESC d=b;d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&s.staging[slot])))return;}
    context->CopyResource(s.staging[slot].Get(),back.Get());
    s.stagedPresent[slot]=s.presentIndex;s.stagedFrame[slot]=frame;s.stagedMirror[slot]=mirrored;s.stagedPending[slot]=true;++s.presentIndex;
}
}
