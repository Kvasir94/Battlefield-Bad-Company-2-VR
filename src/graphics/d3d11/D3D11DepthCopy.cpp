#include "fvr/graphics/D3D11DepthCopy.h"
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>

namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char Shader[]=R"(
Texture2D<float> source:register(t0);
float4 vs(uint id:SV_VertexID):SV_Position{float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);}
float capture(float4 p:SV_Position):SV_Target{return source.Load(int3(p.xy,0));}
float write(float4 p:SV_Position):SV_Depth{return saturate(source.Load(int3(p.xy,0)));}
)";
// Typeless family and the readable SRV format of a depth source.
bool DepthFormats(DXGI_FORMAT format,DXGI_FORMAT& typeless,DXGI_FORMAT& read)noexcept {
    switch(format){
    case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        typeless=DXGI_FORMAT_R24G8_TYPELESS;read=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;return true;
    case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_R32_FLOAT:
        typeless=DXGI_FORMAT_R32_TYPELESS;read=DXGI_FORMAT_R32_FLOAT;return true;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        typeless=DXGI_FORMAT_R32G8X24_TYPELESS;read=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;return true;
    case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_R16_UNORM:
        typeless=DXGI_FORMAT_R16_TYPELESS;read=DXGI_FORMAT_R16_UNORM;return true;
    default:return false;
    }
}
struct PrivateState {
    ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> previous;
    PrivateState(ID3D11DeviceContext1* c,ID3DDeviceContextState* next):context(c){context->SwapDeviceContextState(next,&previous);context->ClearState();}
    // Drop our bindings before parking the private state so it never retains a
    // borrowed resource, then restore the caller's exact state.
    ~PrivateState(){context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);}
};
HRESULT CreateState(ID3D11Device* device,ComPtr<ID3D11Device1>& device1,ComPtr<ID3D11DeviceContext1>& context,ComPtr<ID3DDeviceContextState>& state)noexcept {
    HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&device1));if(FAILED(hr))return hr;
    ComPtr<ID3D11DeviceContext> immediate;device->GetImmediateContext(&immediate);
    hr=immediate.As(&context);if(FAILED(hr))return hr;
    const auto level=device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
    const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
    hr=device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&state);
    return SUCCEEDED(hr)&&chosen!=level?E_FAIL:hr;
}
HRESULT Compile(const char* entry,const char* profile,ComPtr<ID3DBlob>& blob)noexcept {
    ComPtr<ID3DBlob> errors;return D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&errors);
}
}
struct D3D11DepthCapture::Impl {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11Device1> device1;ComPtr<ID3D11DeviceContext1> context;ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11RasterizerState> raster;
    // Private typeless twin of a source that cannot be sampled, and its view.
    ComPtr<ID3D11Texture2D> twin;ComPtr<ID3D11ShaderResourceView> twinView;D3D11_TEXTURE2D_DESC twinDesc{};
    std::array<ComPtr<ID3D11Texture2D>,2> eyes;std::array<ComPtr<ID3D11RenderTargetView>,2> targets;UINT width=0,height=0;
};
D3D11DepthCapture::D3D11DepthCapture()=default;
D3D11DepthCapture::~D3D11DepthCapture()=default;
void D3D11DepthCapture::Reset()noexcept{impl_.reset();}
ID3D11Texture2D* D3D11DepthCapture::Eye(unsigned eye)const noexcept{return impl_&&eye<2?impl_->eyes[eye].Get():nullptr;}
bool D3D11DepthCapture::Capture(ID3D11Device* device,unsigned eye,ID3D11Texture2D* depth,UINT width,UINT height)noexcept {
    if(!device||!depth||eye>1||!width||!height||width>16384||height>16384)return Fail(2,E_INVALIDARG);
    try {
        if(impl_&&(impl_->device.Get()!=device||impl_->width!=width||impl_->height!=height))impl_.reset();
        if(!impl_){
            auto next=std::make_unique<Impl>();next->device=device;next->width=width;next->height=height;
            HRESULT hr=CreateState(device,next->device1,next->context,next->state);if(FAILED(hr))return Fail(1,hr);
            ComPtr<ID3DBlob> vs,ps;
            if(FAILED(hr=Compile("vs","vs_4_0",vs))||FAILED(hr=Compile("capture","ps_4_0",ps)))return Fail(1,hr);
            if(FAILED(hr=device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&next->vs))||
               FAILED(hr=device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&next->ps)))return Fail(1,hr);
            D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
            if(FAILED(hr=device->CreateRasterizerState(&rs,&next->raster)))return Fail(1,hr);
            D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R32_FLOAT;
            desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            for(unsigned i=0;i<2;++i){
                if(FAILED(hr=device->CreateTexture2D(&desc,nullptr,&next->eyes[i])))return Fail(3,hr);
                if(FAILED(hr=device->CreateRenderTargetView(next->eyes[i].Get(),nullptr,&next->targets[i])))return Fail(4,hr);
            }
            impl_=std::move(next);
        }
        auto& p=*impl_;
        D3D11_TEXTURE2D_DESC source{};depth->GetDesc(&source);DXGI_FORMAT typeless{},read{};
        ComPtr<ID3D11Device> owner;depth->GetDevice(&owner);
        if(owner.Get()!=device||source.Width!=width||source.Height!=height||source.SampleDesc.Count!=1||source.ArraySize!=1||!source.MipLevels||
           !DepthFormats(source.Format,typeless,read))return Fail(2,E_INVALIDARG);
        // Always copy into a private typeless twin: the source is still bound as the
        // game's DSV, and a copy (as the ground probe does at this boundary) never
        // creates an input/output binding hazard in either context state.
        HRESULT hr=S_OK;
        if(!p.twin||p.twinDesc.Format!=typeless||p.twinDesc.MipLevels!=source.MipLevels){
            p.twin.Reset();p.twinView.Reset();
            D3D11_TEXTURE2D_DESC desc=source;desc.Format=typeless;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.MiscFlags=0;
            if(FAILED(hr=device->CreateTexture2D(&desc,nullptr,&p.twin)))return Fail(3,hr);
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=read;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
            if(FAILED(hr=device->CreateShaderResourceView(p.twin.Get(),&srv,&p.twinView)))return Fail(4,hr);
            p.twinDesc=desc;
        }
        // Whole-resource copy within one typeless family (depth-stencil rule).
        p.context->CopyResource(p.twin.Get(),depth);ID3D11ShaderResourceView* view=p.twinView.Get();
        {PrivateState scope(p.context.Get(),p.state.Get());auto* c=p.context.Get();
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(p.vs.Get(),nullptr,0);c->PSSetShader(p.ps.Get(),nullptr,0);
            c->PSSetShaderResources(0,1,&view);c->RSSetState(p.raster.Get());
            const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&viewport);
            auto* target=p.targets[eye].Get();c->OMSetRenderTargets(1,&target,nullptr);c->Draw(3,0);}
        error_=S_OK;stage_=0;return true;
    }catch(...){return Fail(1,E_OUTOFMEMORY);}
}
struct D3D11DepthWriter::Impl {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11Device1> device1;ComPtr<ID3D11DeviceContext1> context;ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11DepthStencilState> depth;
};
D3D11DepthWriter::D3D11DepthWriter()=default;
D3D11DepthWriter::~D3D11DepthWriter()=default;
bool D3D11DepthWriter::Initialize(ID3D11DeviceContext* context)noexcept {
    impl_.reset();if(!context){error_=E_INVALIDARG;return false;}
    try {
        auto p=std::make_unique<Impl>();context->GetDevice(&p->device);
        HRESULT hr=CreateState(p->device.Get(),p->device1,p->context,p->state);if(FAILED(hr)){error_=hr;return false;}
        ComPtr<ID3DBlob> vs,ps;
        if(FAILED(hr=Compile("vs","vs_4_0",vs))||FAILED(hr=Compile("write","ps_4_0",ps))||
           FAILED(hr=p->device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p->vs))||
           FAILED(hr=p->device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p->ps))){error_=hr;return false;}
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
        if(FAILED(hr=p->device->CreateRasterizerState(&rs,&p->raster))){error_=hr;return false;}
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_ALWAYS;
        if(FAILED(hr=p->device->CreateDepthStencilState(&ds,&p->depth))){error_=hr;return false;}
        impl_=std::move(p);error_=S_OK;return true;
    }catch(...){error_=E_OUTOFMEMORY;return false;}
}
bool D3D11DepthWriter::Write(ID3D11ShaderResourceView* source,ID3D11DepthStencilView* target,UINT width,UINT height)noexcept {
    if(!impl_||!source||!target||!width||!height){error_=E_INVALIDARG;return false;}
    auto& p=*impl_;
    {PrivateState scope(p.context.Get(),p.state.Get());auto* c=p.context.Get();
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(p.vs.Get(),nullptr,0);c->PSSetShader(p.ps.Get(),nullptr,0);
        c->PSSetShaderResources(0,1,&source);c->RSSetState(p.raster.Get());
        const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&viewport);
        c->OMSetRenderTargets(0,nullptr,target);c->OMSetDepthStencilState(p.depth.Get(),0);c->Draw(3,0);}
    error_=S_OK;return true;
}
}
