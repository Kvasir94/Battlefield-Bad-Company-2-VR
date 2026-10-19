#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Bc2NativeHandRenderer.h"
#include "Bc2Xm8BoltAsset.h" // existing bounded FIPS180-4 hashing implementation
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>

namespace fvr::bc2 {
namespace {
using Microsoft::WRL::ComPtr;
constexpr char Shader[]=R"(
cbuffer Hands:register(b0){row_major float4x4 clip;row_major float4x4 skin[32];row_major float4x4 normalSkin[32];};
Texture2D diffuse:register(t0);SamplerState surface:register(s0);
struct V{float3 position:POSITION;float3 normal:NORMAL;float2 uv:TEXCOORD0;uint4 bones:BLENDINDICES;float4 weights:BLENDWEIGHT;};
struct P{float4 position:SV_POSITION;float3 normal:TEXCOORD0;float2 uv:TEXCOORD1;};
P vs(V v){P p;float4 position=0;float3 normal=0;
 [unroll]for(uint i=0;i<4;++i){position+=mul(float4(v.position,1),skin[v.bones[i]])*v.weights[i];
 normal+=mul(float4(v.normal,0),normalSkin[v.bones[i]]).xyz*v.weights[i];}
 p.position=mul(position,clip);p.normal=normalize(normal);p.uv=v.uv;return p;}
float4 ps(P p):SV_TARGET{float3 color=diffuse.Sample(surface,p.uv).rgb;
 return float4(color*(.55+.45*abs(normalize(p.normal).y)),1);}
)";
struct Constants {math::Matrix4 clip;std::array<math::Matrix4,32> skin,normalSkin;};
static_assert(sizeof(Constants)==4160);
struct RestoredState {
    ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> previous;
    RestoredState(ID3D11DeviceContext1* c,ID3DDeviceContextState* next):context(c){context->SwapDeviceContextState(next,&previous);}
    ~RestoredState(){context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);}
};
bool TextureIdentity(ID3D11View* view,ID3D11Device* expected,unsigned& width,unsigned& height,
    unsigned& samples,unsigned& quality,std::uint64_t& resource,bool& single)noexcept {
    if(!view)return false;ComPtr<ID3D11Device> device;view->GetDevice(&device);
    ComPtr<ID3D11Resource> r;view->GetResource(&r);resource=reinterpret_cast<std::uintptr_t>(r.Get());
    ComPtr<ID3D11Texture2D> t;if(FAILED(r.As(&t)))return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
    width=d.Width;height=d.Height;samples=d.SampleDesc.Count;quality=d.SampleDesc.Quality;
    single=d.ArraySize==1&&d.MipLevels==1;return device.Get()==expected;
}
BodyAmmoTargetIdentity Target(ID3D11DeviceContext* c,ID3D11Device* device,
    ComPtr<ID3D11RenderTargetView>& color,ComPtr<ID3D11DepthStencilView>& depth){
    BodyAmmoTargetIdentity t;c->OMGetRenderTargets(1,&color,&depth);
    t.colorView=reinterpret_cast<std::uintptr_t>(color.Get());t.depthView=reinterpret_cast<std::uintptr_t>(depth.Get());
    bool cs=false,ds=false;
    const bool cd=TextureIdentity(color.Get(),device,t.colorWidth,t.colorHeight,t.colorSamples,t.colorQuality,t.colorResource,cs);
    const bool dd=TextureIdentity(depth.Get(),device,t.depthWidth,t.depthHeight,t.depthSamples,t.depthQuality,t.depthResource,ds);
    t.sameDevice=cd&&dd;t.singleSliceMip=cs&&ds;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};D3D11_DEPTH_STENCIL_VIEW_DESC zd{};
    if(color)color->GetDesc(&rd);if(depth)depth->GetDesc(&zd);t.colorFormat=rd.Format;t.depthFormat=zd.Format;
    t.supportedViews=(rd.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D||rd.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DMS)&&
        (zd.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D||zd.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DMS);return t;
}
bool MeshValid(const NativeHandGeometry& mesh){
    const auto& p=mesh.provenance;
    if(!p.rigFingerprint||!p.cornerAttributesVerified||!p.materialBindingVerified||
       !native_hand_detail::Digest(p.archiveSha256)||!native_hand_detail::Digest(p.exportSha256)||!native_hand_detail::Digest(p.diffuseSha256)||
       mesh.vertices.empty()||mesh.vertices.size()>32768*3||mesh.first[0]!=0||!mesh.count[0]||!mesh.count[1]||
       mesh.first[1]!=mesh.count[0]||mesh.vertices.size()!=std::size_t(mesh.count[0])+mesh.count[1]||
       mesh.count[0]%3||mesh.count[1]%3||!mesh.capTriangles[0]||!mesh.capTriangles[1])return false;
    for(unsigned side=0;side<2;++side)for(unsigned n=mesh.first[side];n<mesh.first[side]+mesh.count[side];++n){const auto& v=mesh.vertices[n];
        for(float x:v.position)if(!std::isfinite(x)||std::abs(x)>10)return false;
        double normal=0;for(float x:v.normal){if(!std::isfinite(x))return false;normal+=double(x)*x;}
        if(std::abs(normal-1)>1e-3)return false;
        for(float x:v.uv)if(!std::isfinite(x)||std::abs(x)>16)return false;
        double sum=0;for(unsigned k=0;k<4;++k){if(!std::isfinite(v.weights[k])||v.weights[k]<0||v.weights[k]>1||v.bones[k]>=32)return false;
            if(v.weights[k]&&(v.bones[k]<side*16||v.bones[k]>=side*16+16))return false;sum+=v.weights[k];}
        if(std::abs(sum-1)>1e-5)return false;
    }return true;
}
}
struct Bc2NativeHandRenderer::Impl {
    DWORD thread=GetCurrentThreadId();graphics::RigidPropFrameGate gate{true};NativeHandStereoPose stereoPose;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11Device1> device1;ComPtr<ID3D11DeviceContext1> context;
    ID3D11DeviceContext* ownerContext=nullptr; // exact admitted interface identity, retained by context
    ComPtr<ID3DDeviceContextState> privateState;ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> vertices,constants;
    ComPtr<ID3D11ShaderResourceView> diffuse;ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> raster;std::array<ComPtr<ID3D11DepthStencilState>,2> depth;ComPtr<ID3D11BlendState> blend;
    std::shared_ptr<const NativeHandGeometry> mesh;
    bool Thread()const noexcept{return thread==GetCurrentThreadId();}
};
Bc2NativeHandRenderer::Bc2NativeHandRenderer()=default;
Bc2NativeHandRenderer::~Bc2NativeHandRenderer(){assert(!impl_||impl_->Thread());}
bool Bc2NativeHandRenderer::Initialize(ID3D11DeviceContext* c){
    if(impl_||!c||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    auto p=std::make_unique<Impl>();p->ownerContext=c;c->GetDevice(&p->device);
    if(FAILED(p->device.As(&p->device1))||FAILED(c->QueryInterface(IID_PPV_ARGS(&p->context))))return false;
    const auto level=p->device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
    const UINT flags=(p->device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
    if(FAILED(p->device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&p->privateState))||chosen!=level)return false;
    ComPtr<ID3DBlob> vs,ps,errors;
    if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,"vs","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors)))return false;
    errors.Reset();if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,"ps","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors)))return false;
    if(FAILED(p->device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p->vs))||
       FAILED(p->device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p->ps)))return false;
    const D3D11_INPUT_ELEMENT_DESC input[]{
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(p->device->CreateInputLayout(input,5,vs->GetBufferPointer(),vs->GetBufferSize(),&p->layout)))return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.Usage=D3D11_USAGE_DYNAMIC;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(p->device->CreateBuffer(&cb,nullptr,&p->constants)))return false;
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_BACK;rs.DepthClipEnable=TRUE;rs.MultisampleEnable=TRUE;
    if(FAILED(p->device->CreateRasterizerState(&rs,&p->raster)))return false;
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(p->device->CreateDepthStencilState(&ds,&p->depth[0])))return false;
    ds.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;if(FAILED(p->device->CreateDepthStencilState(&ds,&p->depth[1])))return false;
    D3D11_BLEND_DESC bs{};bs.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(p->device->CreateBlendState(&bs,&p->blend)))return false;
    D3D11_SAMPLER_DESC ss{};ss.Filter=D3D11_FILTER_ANISOTROPIC;ss.MaxAnisotropy=8;
    ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;ss.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(p->device->CreateSamplerState(&ss,&p->sampler)))return false;
    impl_=std::move(p);return true;
}
bool Bc2NativeHandRenderer::Upload(std::shared_ptr<const NativeHandGeometry> mesh,const NativeHandTexture& texture){
    if(!impl_||!impl_->Thread()||!mesh||!MeshValid(*mesh)||!NativeHandTextureShapeValid(texture)||
       texture.resource!=mesh->provenance.diffuseResource||texture.sha256!=mesh->provenance.diffuseSha256||
       Xm8BoltSha256Hex(texture.rgba)!=texture.sha256)return false;
    for(const auto& mip:texture.lowerMips)if(Xm8BoltSha256Hex(mip.rgba)!=mip.sha256)return false;
    ComPtr<ID3D11Buffer> vb;D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(mesh->vertices.size()*sizeof(NativeHandVertex));
    desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA data{};data.pSysMem=mesh->vertices.data();
    if(FAILED(impl_->device->CreateBuffer(&desc,&data,&vb)))return false;
    ComPtr<ID3D11Texture2D> image;ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_TEXTURE2D_DESC td{};td.Width=texture.width;td.Height=texture.height;td.ArraySize=1;
    td.MipLevels=UINT(texture.lowerMips.size()+1);td.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::array<D3D11_SUBRESOURCE_DATA,13> levels{};levels[0].pSysMem=texture.rgba.data();levels[0].SysMemPitch=texture.width*4;
    for(unsigned n=0;n<texture.lowerMips.size();++n){levels[n+1].pSysMem=texture.lowerMips[n].rgba.data();levels[n+1].SysMemPitch=texture.lowerMips[n].width*4;}
    if(FAILED(impl_->device->CreateTexture2D(&td,levels.data(),&image))||FAILED(impl_->device->CreateShaderResourceView(image.Get(),nullptr,&srv)))return false;
    impl_->vertices=std::move(vb);impl_->diffuse=std::move(srv);impl_->mesh=std::move(mesh);impl_->stereoPose.Reset();return true;
}
bool Bc2NativeHandRenderer::Ready()const noexcept{return impl_&&impl_->Thread()&&impl_->mesh&&impl_->vertices&&impl_->diffuse;}
bool Bc2NativeHandRenderer::Draw(const NativeHandPose& candidate,const graphics::RigidPropEye& current,std::int64_t now,const BodyAmmoBoundaryProof& proof){
    if(!Ready()||candidate.rigFingerprint!=impl_->mesh->provenance.rigFingerprint||
       candidate.skeletonBones!=impl_->mesh->skeletonBones||!NativeHandPoseForEye(candidate,current,now))return false;
    const BodyAmmoEyeKey eye{current.world,current.request,current.view,current.frame,current.eye};
    ComPtr<ID3D11RenderTargetView> color;ComPtr<ID3D11DepthStencilView> depth;
    const auto target=Target(impl_->context.Get(),impl_->device.Get(),color,depth);
    if(BodyAmmoBoundaryMissing(proof,eye,target,reinterpret_cast<std::uintptr_t>(impl_->ownerContext),now))return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC depthView{};depth->GetDesc(&depthView);
    if(depthView.Flags&D3D11_DSV_READ_ONLY_DEPTH)return false;
    math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;
    const auto clip=graphics::RigidPropClipTransform(identity,proof.eyeView,proof.projection);if(!clip)return false;
    const auto* selected=impl_->stereoPose.Select(candidate,current,now);if(!selected)return false;const auto& pose=*selected;
    Constants constants{};constants.clip=*clip;constants.skin=pose.skin;
    for(unsigned n=0;n<32;++n){const auto inverse=interaction::InverseAnimatedTransform(pose.skin[n]);if(!inverse)return false;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)constants.normalSkin[n].values[r][c]=inverse->values[c][r];}
    if(!impl_->gate.Admit(current,current,now))return false;
    auto* c=impl_->context.Get();RestoredState restore(c,impl_->privateState.Get());c->ClearState();
    D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(impl_->constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    std::memcpy(mapped.pData,&constants,sizeof(constants));c->Unmap(impl_->constants.Get(),0);
    c->IASetInputLayout(impl_->layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto* vb=impl_->vertices.Get();const UINT stride=sizeof(NativeHandVertex),offset=0;c->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    c->VSSetShader(impl_->vs.Get(),nullptr,0);c->PSSetShader(impl_->ps.Get(),nullptr,0);
    auto* cb=impl_->constants.Get();c->VSSetConstantBuffers(0,1,&cb);
    auto* srv=impl_->diffuse.Get();c->PSSetShaderResources(0,1,&srv);auto* sampler=impl_->sampler.Get();c->PSSetSamplers(0,1,&sampler);
    c->RSSetState(impl_->raster.Get());const D3D11_VIEWPORT viewport{0,0,float(target.colorWidth),float(target.colorHeight),0,1};c->RSSetViewports(1,&viewport);
    auto* rtv=color.Get();c->OMSetRenderTargets(1,&rtv,depth.Get());c->OMSetDepthStencilState(impl_->depth[proof.reversedDepth?1:0].Get(),0);
    c->OMSetBlendState(impl_->blend.Get(),nullptr,0xffffffffu);
    for(unsigned side=0;side<2;++side)if(pose.tracked[side])c->Draw(impl_->mesh->count[side],impl_->mesh->first[side]);
    return true;
}
} // namespace fvr::bc2
