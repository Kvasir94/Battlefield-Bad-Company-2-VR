#include "Bc2StereoCaptureGpu.h"
#include <algorithm>
#include <cstring>

namespace fvr::bc2::stereoCapture {
namespace {
using Microsoft::WRL::ComPtr;
std::uint32_t Id(const void* p)noexcept{return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));}
void Texture(ID3D11Resource* resource,unsigned mip,Surface& s)noexcept{
    s.resource=Id(resource);if(!resource)return;
    ComPtr<ID3D11Texture2D> texture;
    if(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))&&texture){D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        s.width=(std::max)(1u,unsigned(d.Width)>>mip);s.height=(std::max)(1u,unsigned(d.Height)>>mip);s.samples=d.SampleDesc.Count;if(!s.format)s.format=d.Format;}
}
template<class View> bool Reuse(View* view,const Surface* previous,Surface& out)noexcept{
    if(previous&&view&&previous->view==Id(view)){out=*previous;return true;}return false;
}
void Describe(ID3D11RenderTargetView* v,const Surface* previous,Surface& s)noexcept{
    s={};if(!v||Reuse(v,previous,s))return;s.view=Id(v);
    D3D11_RENDER_TARGET_VIEW_DESC d{};v->GetDesc(&d);s.format=d.Format;s.dimension=d.ViewDimension;
    s.mip=d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D?d.Texture2D.MipSlice:d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DARRAY?d.Texture2DArray.MipSlice:0;
    ComPtr<ID3D11Resource> r;v->GetResource(&r);Texture(r.Get(),s.mip,s);
}
void Describe(ID3D11DepthStencilView* v,const Surface* previous,Surface& s)noexcept{
    s={};if(!v||Reuse(v,previous,s))return;s.view=Id(v);
    D3D11_DEPTH_STENCIL_VIEW_DESC d{};v->GetDesc(&d);s.format=d.Format;s.dimension=d.ViewDimension;
    s.mip=d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D?d.Texture2D.MipSlice:d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DARRAY?d.Texture2DArray.MipSlice:0;
    ComPtr<ID3D11Resource> r;v->GetResource(&r);Texture(r.Get(),s.mip,s);
}
void Describe(ID3D11ShaderResourceView* v,const Surface* previous,Surface& s)noexcept{
    s={};if(!v||Reuse(v,previous,s))return;s.view=Id(v);
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};v->GetDesc(&d);s.format=d.Format;s.dimension=d.ViewDimension;
    s.mip=d.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D?d.Texture2D.MostDetailedMip:0;
    ComPtr<ID3D11Resource> r;v->GetResource(&r);Texture(r.Get(),s.mip,s);
}
std::uint32_t BufferBytes(ID3D11Buffer* b)noexcept{if(!b)return 0;D3D11_BUFFER_DESC d{};b->GetDesc(&d);return d.ByteWidth;}
// Typed format with a 4-byte texel whose mip chain the driver can generate.
DXGI_FORMAT ReadbackFormat(DXGI_FORMAT f)noexcept{
    switch(f){
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R11G11B10_FLOAT:return f;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
}

void GatherPipelineState(ID3D11DeviceContext* c,PipelineState& s,const PipelineState* previous)noexcept{
    s={};if(!c)return;
    {D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};UINT count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        c->RSGetViewports(&count,viewports);s.viewportCount=count;
        for(unsigned i=0;i<s.viewports.size()&&i<count;++i){const auto& v=viewports[i];s.viewports[i]={v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth};}}
    {ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11GeometryShader> gs;
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->GSGetShader(&gs,nullptr,nullptr);
        s.vs=Id(vs.Get());s.ps=Id(ps.Get());s.gs=Id(gs.Get());}
    {std::array<ID3D11RenderTargetView*,4> rtv{};ID3D11DepthStencilView* dsv=nullptr;c->OMGetRenderTargets(UINT(rtv.size()),rtv.data(),&dsv);
        std::array<ComPtr<ID3D11RenderTargetView>,4> owned;for(unsigned i=0;i<rtv.size();++i)owned[i].Attach(rtv[i]);ComPtr<ID3D11DepthStencilView> ownedDsv;ownedDsv.Attach(dsv);
        for(unsigned i=0;i<rtv.size();++i)Describe(rtv[i],previous?&previous->rtv[i]:nullptr,s.rtv[i]);
        Describe(dsv,previous?&previous->dsv:nullptr,s.dsv);}
    {std::array<ID3D11ShaderResourceView*,16> srv{};c->PSGetShaderResources(0,UINT(srv.size()),srv.data());
        std::array<ComPtr<ID3D11ShaderResourceView>,16> owned;for(unsigned i=0;i<srv.size();++i)owned[i].Attach(srv[i]);
        for(unsigned i=0;i<srv.size();++i)Describe(srv[i],previous?&previous->psSrv[i]:nullptr,s.psSrv[i]);}
    {ComPtr<ID3D11DepthStencilState> state;UINT reference=0;c->OMGetDepthStencilState(&state,&reference);
        s.depthState=Id(state.Get());s.stencilRef=reference;
        if(previous&&previous->depthState==s.depthState)s.depthBits=previous->depthBits;
        else if(state){D3D11_DEPTH_STENCIL_DESC d{};state->GetDesc(&d);
            s.depthBits=(d.DepthEnable?1u:0u)|(unsigned(d.DepthWriteMask)<<1)|((unsigned(d.DepthFunc)&15u)<<2)|((d.StencilEnable?1u:0u)<<6);}
        else s.depthBits=1u|(1u<<1)|(unsigned(D3D11_COMPARISON_LESS)<<2); // D3D11 default state
    }
    {ComPtr<ID3D11BlendState> state;FLOAT factor[4]{};UINT mask=0;c->OMGetBlendState(&state,factor,&mask);
        s.blendState=Id(state.Get());s.sampleMask=mask;for(unsigned i=0;i<4;++i)s.blendFactor[i]=factor[i];
        if(previous&&previous->blendState==s.blendState){s.blendBits=previous->blendBits;s.writeMask=previous->writeMask;}
        else if(state){D3D11_BLEND_DESC d{};state->GetDesc(&d);const auto& r=d.RenderTarget[0];
            s.blendBits=(r.BlendEnable?1u:0u)|((d.AlphaToCoverageEnable?1u:0u)<<1)|((d.IndependentBlendEnable?1u:0u)<<2)|((unsigned(r.SrcBlend)&31u)<<3)|
                ((unsigned(r.DestBlend)&31u)<<8)|((unsigned(r.BlendOp)&7u)<<13)|((unsigned(r.SrcBlendAlpha)&31u)<<16)|((unsigned(r.DestBlendAlpha)&31u)<<21)|((unsigned(r.BlendOpAlpha)&7u)<<26);
            s.writeMask=r.RenderTargetWriteMask;}
        else {s.blendBits=(unsigned(D3D11_BLEND_ONE)<<3)|(unsigned(D3D11_BLEND_ZERO)<<8)|(1u<<13)|(unsigned(D3D11_BLEND_ONE)<<16)|(unsigned(D3D11_BLEND_ZERO)<<21)|(1u<<26);s.writeMask=15;}
    }
    const auto buffers=[&](bool pixel,std::array<std::uint32_t,8>& ids,std::array<std::uint32_t,8>& bytes,const std::array<std::uint32_t,8>* oldIds,const std::array<std::uint32_t,8>* oldBytes){
        std::array<ID3D11Buffer*,8> b{};if(pixel)c->PSGetConstantBuffers(0,UINT(b.size()),b.data());else c->VSGetConstantBuffers(0,UINT(b.size()),b.data());
        std::array<ComPtr<ID3D11Buffer>,8> owned;for(unsigned i=0;i<b.size();++i)owned[i].Attach(b[i]);
        for(unsigned i=0;i<b.size();++i){ids[i]=Id(b[i]);bytes[i]=oldIds&&(*oldIds)[i]==ids[i]?(*oldBytes)[i]:BufferBytes(b[i]);}
    };
    buffers(false,s.vsCb,s.vsCbBytes,previous?&previous->vsCb:nullptr,previous?&previous->vsCbBytes:nullptr);
    buffers(true,s.psCb,s.psCbBytes,previous?&previous->psCb:nullptr,previous?&previous->psCbBytes:nullptr);
}

void ColorReadback::Queue(ID3D11DeviceContext* context,ID3D11Texture2D* source,ColorResult& target)noexcept{
    target.status=ColorStatus::Failed;target.pixels.clear();target.polls=0;target.error=0;
    if(!context||!source)return;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    target.sourceWidth=d.Width;target.sourceHeight=d.Height;target.sourceFormat=d.Format;target.sourceSamples=d.SampleDesc.Count;
    const auto format=ReadbackFormat(d.Format);
    if(!d.Width||!d.Height||d.SampleDesc.Count!=1||format==DXGI_FORMAT_UNKNOWN){target.status=ColorStatus::Unsupported;return;}
    Item* free=nullptr;for(auto& item:items_)if(!item.staging){free=&item;break;}
    if(!free){target.status=ColorStatus::InFlightCap;return;}
    unsigned level=0;while(level<15&&((d.Width>>level)>ColorMaxDimension||(d.Height>>level)>ColorMaxDimension))++level;
    const unsigned width=(std::max)(1u,unsigned(d.Width)>>level),height=(std::max)(1u,unsigned(d.Height)>>level);
    ComPtr<ID3D11Device> device,sourceDevice;context->GetDevice(&device);source->GetDevice(&sourceDevice);
    if(!device||device.Get()!=sourceDevice.Get()){target.error=E_INVALIDARG;return;}
    if(level){
        ComPtr<ID3D11Device> mipDevice;if(mip_)mip_->GetDevice(&mipDevice);
        if(!mip_||mipDevice.Get()!=device.Get()||mipDesc_.Width!=d.Width||mipDesc_.Height!=d.Height||mipDesc_.Format!=format||mipDesc_.MipLevels!=level+1){
            mip_.Reset();mipView_.Reset();
            UINT support=0;constexpr UINT needed=D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
            if(FAILED(device->CheckFormatSupport(format,&support))||(support&needed)!=needed){target.status=ColorStatus::Unsupported;return;}
            D3D11_TEXTURE2D_DESC m{};m.Width=d.Width;m.Height=d.Height;m.MipLevels=level+1;m.ArraySize=1;m.Format=format;m.SampleDesc={1,0};
            m.Usage=D3D11_USAGE_DEFAULT;m.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;m.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
            HRESULT hr=device->CreateTexture2D(&m,nullptr,&mip_);
            if(SUCCEEDED(hr))hr=device->CreateShaderResourceView(mip_.Get(),nullptr,&mipView_);
            if(FAILED(hr)){mip_.Reset();mipView_.Reset();target.error=hr;return;}
            mipDesc_=m;
        }
    }
    D3D11_TEXTURE2D_DESC s{};s.Width=width;s.Height=height;s.MipLevels=1;s.ArraySize=1;s.Format=format;s.SampleDesc={1,0};
    s.Usage=D3D11_USAGE_STAGING;s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;const HRESULT hr=device->CreateTexture2D(&s,nullptr,&staging);if(FAILED(hr)){target.error=hr;return;}
    // Private resources only: the game's texture is a copy source and no
    // pipeline binding is touched. Queued GPU work is ordered on the context.
    if(level){
        context->CopySubresourceRegion(mip_.Get(),0,0,0,0,source,0,nullptr);
        context->GenerateMips(mipView_.Get());
        context->CopySubresourceRegion(staging.Get(),0,0,0,0,mip_.Get(),level,nullptr);
    }else context->CopySubresourceRegion(staging.Get(),0,0,0,0,source,0,nullptr);
    target.mip=level;target.width=width;target.height=height;target.format=format;target.bytesPerPixel=4;target.status=ColorStatus::Pending;
    free->staging=std::move(staging);free->target=&target;
}
void ColorReadback::Poll(ID3D11DeviceContext* context)noexcept{
    if(!context)return;
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    for(auto& item:items_){if(!item.staging)continue;auto& t=*item.target;
        ComPtr<ID3D11Device> stagingDevice;item.staging->GetDevice(&stagingDevice);
        // Pending readback may outlive the device that produced it. Never map
        // another device's resource through the caller's current context.
        if(!device||device.Get()!=stagingDevice.Get()){t.status=ColorStatus::Failed;t.error=E_INVALIDARG;item={};continue;}
        D3D11_MAPPED_SUBRESOURCE mapped{};const HRESULT hr=context->Map(item.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(hr==DXGI_ERROR_WAS_STILL_DRAWING){if(++t.polls>=ColorPollLimit){t.status=ColorStatus::Timeout;item={};}continue;}
        ++t.polls;
        if(FAILED(hr)){t.status=ColorStatus::Failed;t.error=hr;item={};continue;}
        const std::size_t row=std::size_t(t.width)*t.bytesPerPixel;
        try{
            if(mapped.RowPitch<row)throw 0;
            t.pixels.resize(row*t.height);
            for(unsigned y=0;y<t.height;++y)std::memcpy(t.pixels.data()+row*y,static_cast<const unsigned char*>(mapped.pData)+std::size_t(mapped.RowPitch)*y,row);
            t.status=ColorStatus::Complete;
        }catch(...){t.pixels.clear();t.status=ColorStatus::Failed;}
        context->Unmap(item.staging.Get(),0);item={};
    }
}
unsigned ColorReadback::InFlight()const noexcept{unsigned n=0;for(const auto& item:items_)n+=item.staging?1u:0u;return n;}
void ColorReadback::Abandon()noexcept{
    for(auto& item:items_){if(item.staging&&item.target&&item.target->status==ColorStatus::Pending)item.target->status=ColorStatus::Unresolved;item={};}
    mipView_.Reset();mip_.Reset();
}
}
