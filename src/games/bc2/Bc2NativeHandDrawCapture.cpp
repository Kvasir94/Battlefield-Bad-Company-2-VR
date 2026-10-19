#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Bc2NativeHandDrawCapture.h"
#include "Bc2NativeHandMesh.h"
#include "Bc2ReloadDrawGeometry.h"
#include "Bc2Xm8BoltAsset.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>

namespace fvr::bc2::nativeHandCapture {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned MaxDraws=32,MaxPacks=16,MaxUploads=64,MaxLayouts=64,MaxShaders=96;
constexpr std::int64_t AdmissionNs=60000000000ll;
struct Element {std::string semantic;unsigned index=0,format=0,slot=0,offset=0,classification=0,step=0;};
struct Layout {std::uint64_t id=0;ComPtr<ID3D11DeviceChild> object;std::vector<Element> elements;};
struct Shader {std::uint64_t id=0;ComPtr<ID3D11DeviceChild> object;bool pixel=false;std::vector<std::byte> bytes;};
struct Pack {ReloadPackedSource source{};std::uint64_t serial=0,hash=0;std::array<std::uint64_t,2> destination{};
    std::vector<std::byte> bytes;std::int64_t now=0;unsigned eye=0,frame=0;bool paired=false;};
struct Upload {ComPtr<ID3D11Resource> resource;std::uint64_t base=0;unsigned bytes=0;
    std::uint64_t serial=0,hash=0;unsigned offset=0,copy=0;bool mapped=false,committed=false;std::int64_t now=0;};
struct ResourceCopy {unsigned stage=0,slot=0,format=0,width=0,height=0,mip=0,rowBytes=0,rows=0;
    std::uint64_t view=0,resource=0;ComPtr<ID3D11Resource> staging;std::vector<std::byte> bytes;
    unsigned firstElement=0,elements=0,structureStride=0;
    ComPtr<ID3D11ShaderResourceView> owner;bool done=false,failed=false;long result=0;};
struct Draw {ReloadDrawCapturePacket packet{};std::vector<ResourceCopy> resources;
    std::array<std::vector<std::byte>,16> bytes;std::array<ComPtr<ID3D11Buffer>,14> constants;
    std::vector<Upload> lineage;ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> depth;
    unsigned depthWrite=0,depthFunc=0;bool depthEnabled=false,originalReturned=false;
    bool apiObserved=false,indexedInstanced=false;unsigned instances=1,startInstance=0;
    bool layoutMissing=false,vsMissing=false,psMissing=false;
    unsigned section=UINT32_MAX;bool matched=false;};
using MakeLayout=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**);
using MakeVs=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**);
using MakePs=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
using Map=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
using Unmap=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);
using Update=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
struct State {
    std::atomic<bool> enabled=false,stopping=false,disableFailed=false;std::atomic<unsigned> callbacks=0,busy=0,errors=0,rejected=0;
    std::mutex mutex;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    ComPtr<ID3D11Device> deviceOwner;ComPtr<ID3D11DeviceContext> contextOwner;
    std::filesystem::path folder;std::int64_t begin=0,deadline=0;std::optional<FrozenOwner> frozen;
    std::optional<FrozenOwner> selection;CandidateEyes eyes{};
    std::optional<ReloadDrawFrameEvidence> activeEye;DWORD eyeThread=0;unsigned retiredFrame=0;
    std::atomic<unsigned> activeEyeThread=0;
    std::array<std::atomic<unsigned>,2> eyeAttempts{},missingProducer{},exactOwnerRejected{};
    unsigned eye1StartsRejected=0,candidateFrameRejected=0;
    unsigned retries=0,selectionRejected=0,ownerRejected=0,endRejected=0,catalogCandidates=0,
        unsupportedInstances=0,indexedCandidates=0,instancedCandidates=0,
        layoutEvictions=0,shaderEvictions=0,layoutPinnedFull=0,shaderPinnedFull=0,
        missingLayoutMetadata=0,missingVsBytecode=0,missingPsBytecode=0;
    std::atomic<unsigned> unclassifiedCandidates=0;
    std::optional<ReloadProducerView> candidateFrame;ReloadPackedSource candidateSource{};
    std::int64_t candidateDeadline=0;
    std::vector<Layout> layouts;std::vector<Shader> shaders;std::vector<Pack> packs;std::vector<Upload> uploads;std::vector<Draw> draws;
    std::array<void*,6> hooks{};MakeLayout layoutOriginal=nullptr;MakeVs vsOriginal=nullptr;MakePs psOriginal=nullptr;
    Map mapOriginal=nullptr;Unmap unmapOriginal=nullptr;Update updateOriginal=nullptr;
    bool written=false,configured=false,materialObservation=false;unsigned materialEyes=0;std::string error;
};
thread_local unsigned returningDraw=UINT32_MAX;
thread_local bool localReadback=false;
struct DrawApi {ID3D11DeviceContext* context=nullptr;unsigned count=0,instances=1,startInstance=0;bool instanced=false;};
thread_local DrawApi drawApi;
State& S(){static auto* s=new State();return *s;} // retained with trampolines until process exit
std::atomic<bool> captureEnabled=false,captureConfigured=false;
struct Admission {bool entered=false;Admission(){if(!captureEnabled.load(std::memory_order_seq_cst))return;auto& s=S();if(!s.enabled.load(std::memory_order_seq_cst))return;
    s.callbacks.fetch_add(1,std::memory_order_seq_cst);entered=s.enabled.load(std::memory_order_seq_cst);
    if(!entered)s.callbacks.fetch_sub(1,std::memory_order_seq_cst);}~Admission(){if(entered)S().callbacks.fetch_sub(1,std::memory_order_seq_cst);}};
std::int64_t Now(){LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
    return (q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
std::uint64_t Id(const void* p){return reinterpret_cast<std::uintptr_t>(p);}
bool Read(std::uint64_t p,void* out,std::size_t count){SIZE_T got=0;return p&&count<=128*1024&&
    ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(std::uintptr_t(p)),out,count,&got)&&got==count;}
void ShaderRecord(ID3D11Device* device,ID3D11DeviceChild* object,const void* bytes,SIZE_T size,bool pixel)noexcept {
    Admission a;if(!a.entered||device!=S().device||!object||!bytes||!size||size>256*1024)return;
    try{auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return;}
        if(Now()>=s.deadline)return;
        if(s.shaders.size()>=MaxShaders){const auto oldest=std::find_if(s.shaders.begin(),s.shaders.end(),[&](const auto& sh){
            return std::none_of(s.draws.begin(),s.draws.end(),[&](const auto& d){return d.packet.vs==sh.id||d.packet.ps==sh.id;});});
            if(oldest==s.shaders.end()){++s.shaderPinnedFull;++s.rejected;return;}s.shaders.erase(oldest);++s.shaderEvictions;}
        Shader item;item.id=Id(object);item.object=object;item.pixel=pixel;
        item.bytes.resize(size);std::memcpy(item.bytes.data(),bytes,size);s.shaders.push_back(std::move(item));}catch(...){++S().errors;}
}
HRESULT STDMETHODCALLTYPE LayoutHook(ID3D11Device* d,const D3D11_INPUT_ELEMENT_DESC* e,UINT count,const void* bytes,SIZE_T size,ID3D11InputLayout** out){
    const auto hr=S().layoutOriginal(d,e,count,bytes,size,out);Admission a;
    if(a.entered&&d==S().device&&SUCCEEDED(hr)&&out&&*out&&e&&count&&count<=32)try{
        auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return hr;}
        if(Now()>=s.deadline)return hr;
        if(s.layouts.size()>=MaxLayouts){const auto oldest=std::find_if(s.layouts.begin(),s.layouts.end(),[&](const auto& l){
            return std::none_of(s.draws.begin(),s.draws.end(),[&](const auto& draw){return draw.packet.layout==l.id;});});
            if(oldest==s.layouts.end()){++s.layoutPinnedFull;++s.rejected;return hr;}s.layouts.erase(oldest);++s.layoutEvictions;}
        Layout item;item.id=Id(*out);item.object=*out;
        for(unsigned n=0;n<count;++n){if(!e[n].SemanticName||strnlen_s(e[n].SemanticName,65)>64){++s.rejected;return hr;}
            item.elements.push_back({e[n].SemanticName,e[n].SemanticIndex,unsigned(e[n].Format),e[n].InputSlot,e[n].AlignedByteOffset,unsigned(e[n].InputSlotClass),e[n].InstanceDataStepRate});}
        s.layouts.push_back(std::move(item));}catch(...){++S().errors;}return hr;
}
HRESULT STDMETHODCALLTYPE VsHook(ID3D11Device* d,const void* bytes,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11VertexShader** out){
    const auto hr=S().vsOriginal(d,bytes,size,linkage,out);if(SUCCEEDED(hr)&&out)ShaderRecord(d,*out,bytes,size,false);return hr;}
HRESULT STDMETHODCALLTYPE PsHook(ID3D11Device* d,const void* bytes,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11PixelShader** out){
    const auto hr=S().psOriginal(d,bytes,size,linkage,out);if(SUCCEEDED(hr)&&out)ShaderRecord(d,*out,bytes,size,true);return hr;}
HRESULT STDMETHODCALLTYPE MapHook(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* out){
    const auto hr=S().mapOriginal(c,r,sub,type,flags,out);Admission a;
    if(a.entered&&c==S().context&&SUCCEEDED(hr)&&out&&out->pData&&sub==0&&
       (type==D3D11_MAP_WRITE||type==D3D11_MAP_WRITE_DISCARD||type==D3D11_MAP_WRITE_NO_OVERWRITE))try{
        ComPtr<ID3D11Buffer> b;if(FAILED(r->QueryInterface(IID_PPV_ARGS(&b))))return hr;D3D11_BUFFER_DESC desc{};b->GetDesc(&desc);
        if(!desc.ByteWidth||desc.ByteWidth>128*1024)return hr;auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return hr;}
        for(auto& u:s.uploads)if(u.resource.Get()==r)u.committed=false;
        s.uploads.erase(std::remove_if(s.uploads.begin(),s.uploads.end(),[&](const auto& u){return (!u.mapped&&!u.serial)||(u.resource.Get()==r&&!u.serial);}),s.uploads.end());
        if(s.uploads.size()>=MaxUploads){++s.rejected;return hr;}Upload u;u.resource=r;u.base=Id(out->pData);u.bytes=desc.ByteWidth;u.mapped=true;u.now=Now();s.uploads.push_back(std::move(u));
    }catch(...){++S().errors;}return hr;
}
void STDMETHODCALLTYPE UnmapHook(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub){
    Admission a;if(a.entered&&!localReadback&&c==S().context&&sub==0)try{auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);
        if(!lock)++s.busy;else {for(auto& u:s.uploads)if(u.mapped&&u.resource.Get()==r){
            if(u.serial&&u.copy){std::vector<std::byte> current(u.copy);const auto p=std::find_if(s.packs.begin(),s.packs.end(),[&](const auto& p){return p.serial==u.serial&&p.hash==u.hash;});
                u.committed=p!=s.packs.end()&&Read(u.base+u.offset,current.data(),current.size())&&current==p->bytes;}
            u.mapped=false;u.now=Now();}
            s.uploads.erase(std::remove_if(s.uploads.begin(),s.uploads.end(),[](const auto& u){return !u.mapped&&!u.serial;}),s.uploads.end());}}
    catch(...){++S().errors;}S().unmapOriginal(c,r,sub);
}
void STDMETHODCALLTYPE UpdateHook(ID3D11DeviceContext* c,ID3D11Resource* r,UINT sub,const D3D11_BOX* box,const void* src,UINT row,UINT depth){
    Admission a;if(a.entered&&c==S().context&&sub==0&&src)try{
        ComPtr<ID3D11Buffer> b;if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&b)))){D3D11_BUFFER_DESC desc{};b->GetDesc(&desc);
            const auto left=box?box->left:0u,right=box?box->right:desc.ByteWidth;
            if(right>=left&&right<=desc.ByteWidth&&right-left<=128*1024){auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);
                if(!lock)++s.busy;else {for(auto& u:s.uploads)if(u.resource.Get()==r)u.committed=false;
                if(s.uploads.size()<MaxUploads)for(const auto& p:s.packs)if(p.paired)for(auto destination:p.destination){
                    const auto at=Id(src);if(destination<at||destination-at>right-left||p.bytes.size()>right-left-(destination-at))continue;
                    std::vector<std::byte> current(p.bytes.size());if(!Read(destination,current.data(),current.size())||current!=p.bytes)continue;
                    if(s.uploads.size()>=MaxUploads){++s.rejected;break;}
                    Upload u;u.resource=r;u.serial=p.serial;u.hash=p.hash;u.offset=left+unsigned(destination-at);u.copy=unsigned(p.bytes.size());u.committed=true;u.now=Now();s.uploads.push_back(std::move(u));break;}}}}
    }catch(...){++S().errors;}S().updateOriginal(c,r,sub,box,src,row,depth);
}
bool Hook(State& s,unsigned n,void* address,void* callback,void** original){HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&module)||module!=GetModuleHandleW(L"d3d11.dll"))return false;
    if(MH_CreateHook(address,callback,original)!=MH_OK)return false;s.hooks[n]=address;return true;
}
void Binary(const std::filesystem::path& path,std::span<const std::byte> bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("capture write");}
void Reflection(std::ostream& out,const Shader* shader){out<<"{\"observed\":"<<(shader?"true":"false");if(!shader){out<<'}';return;}
    out<<",\"sha256\":"<<std::quoted(Xm8BoltSha256Hex(shader->bytes));ComPtr<ID3D11ShaderReflection> reflect;
    if(FAILED(D3DReflect(shader->bytes.data(),shader->bytes.size(),__uuidof(ID3D11ShaderReflection),reinterpret_cast<void**>(reflect.GetAddressOf())))){out<<",\"reflection_complete\":false}";return;}
    D3D11_SHADER_DESC desc{};if(FAILED(reflect->GetDesc(&desc))){out<<",\"reflection_complete\":false}";return;}
    for(unsigned n=0;n<desc.InputParameters;++n){D3D11_SIGNATURE_PARAMETER_DESC p{};if(FAILED(reflect->GetInputParameterDesc(n,&p))){out<<",\"reflection_complete\":false}";return;}}
    for(unsigned n=0;n<desc.BoundResources;++n){D3D11_SHADER_INPUT_BIND_DESC r{};if(FAILED(reflect->GetResourceBindingDesc(n,&r))){out<<",\"reflection_complete\":false}";return;}}
    out<<",\"reflection_complete\":true,\"inputs\":[";bool comma=false;
    for(unsigned n=0;n<desc.InputParameters;++n){D3D11_SIGNATURE_PARAMETER_DESC p{};if(FAILED(reflect->GetInputParameterDesc(n,&p)))continue;
        if(comma)out<<',';comma=true;out<<"{\"semantic\":"<<std::quoted(p.SemanticName?p.SemanticName:"")<<",\"index\":"<<p.SemanticIndex<<",\"register\":"<<p.Register<<",\"mask\":"<<unsigned(p.Mask)<<",\"type\":"<<p.ComponentType<<'}';}
    out<<"],\"resources\":[";comma=false;for(unsigned n=0;n<desc.BoundResources;++n){D3D11_SHADER_INPUT_BIND_DESC r{};if(FAILED(reflect->GetResourceBindingDesc(n,&r)))continue;
        if(comma)out<<',';comma=true;out<<"{\"name\":"<<std::quoted(r.Name?r.Name:"")<<",\"type\":"<<r.Type<<",\"slot\":"<<r.BindPoint<<",\"count\":"<<r.BindCount<<",\"dimension\":"<<r.Dimension<<'}';}out<<"]}";
}
}
bool Enabled()noexcept{return captureEnabled.load(std::memory_order_seq_cst);}
std::span<const WeaponDrawSection> Catalog(){static const WeaponDrawSection sections[]{
    {"Characters/US/US_FP_Arms/US_FP_ArmsStandard_Mesh.res","6a9e83c77b51f172705b1f17cecb6f5d0068e1e53f0a2e8601b1eb53e0ab7d62","export|arm_left|arm_leftShape_ArmStandard",0,0,{4794,48,12,0,graphics::RigidPropPosition::Half4,0xb1b4919cd9ced567ull,0xbf112b93fc653fe0ull,0},0},
    {"Characters/US/US_FP_Arms/US_FP_ArmsStandard_Mesh.res","6a9e83c77b51f172705b1f17cecb6f5d0068e1e53f0a2e8601b1eb53e0ab7d62","export|arm_left|arm_leftShape_MOLLE",0,1,{1020,48,12,0,graphics::RigidPropPosition::Half4,0x2fd3dcfd7c41a964ull,0x3c7d7479fd286159ull,0},0},
    {"Characters/US/US_FP_Arms/US_FP_ArmsStandard_Mesh.res","6a9e83c77b51f172705b1f17cecb6f5d0068e1e53f0a2e8601b1eb53e0ab7d62","export|hand_left|hand_leftShape_Gloves",0,2,{10164,48,35,0,graphics::RigidPropPosition::Half4,0xa95276bf7113e308ull,0x801c7a6b7e9e5d8eull,0},0},
    {"Characters/US/US_FP_Arms/US_FP_ArmsStandard_Mesh.res","6a9e83c77b51f172705b1f17cecb6f5d0068e1e53f0a2e8601b1eb53e0ab7d62","export|arm_left|arm_leftShape_ArmStandard_ZOnly",0,3,{15978,16,44,0,graphics::RigidPropPosition::Half4,0x8fc17b721610ba0aull,0x73922dde79914067ull,0},0}};return sections;}
bool Configure(const std::filesystem::path& folder,void* device,void* context,std::int64_t now,bool materialObservation)noexcept {
    auto& s=S();if(s.configured||!folder.is_absolute()||!device||!context||now<=0)return false;
    try{s.configured=true;captureConfigured.store(true);s.folder=folder/"native-hands-contract";s.device=static_cast<ID3D11Device*>(device);s.context=static_cast<ID3D11DeviceContext*>(context);
        s.deviceOwner=s.device;s.contextOwner=s.context;s.materialObservation=materialObservation;
        if(s.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        ComPtr<ID3D11Device> actual;s.context->GetDevice(&actual);if(actual.Get()!=s.device)return false;
        s.begin=now;s.deadline=now+AdmissionNs;s.packs.reserve(MaxPacks);s.uploads.reserve(MaxUploads);s.draws.reserve(MaxDraws);
        auto** dv=*reinterpret_cast<void***>(s.device);auto** cv=*reinterpret_cast<void***>(s.context);
        if(!Hook(s,0,dv[11],reinterpret_cast<void*>(LayoutHook),reinterpret_cast<void**>(&s.layoutOriginal))||
           !Hook(s,1,dv[12],reinterpret_cast<void*>(VsHook),reinterpret_cast<void**>(&s.vsOriginal))||
           !Hook(s,2,dv[15],reinterpret_cast<void*>(PsHook),reinterpret_cast<void**>(&s.psOriginal))||
           !Hook(s,3,cv[14],reinterpret_cast<void*>(MapHook),reinterpret_cast<void**>(&s.mapOriginal))||
           !Hook(s,4,cv[15],reinterpret_cast<void*>(UnmapHook),reinterpret_cast<void**>(&s.unmapOriginal))||
           !Hook(s,5,cv[48],reinterpret_cast<void*>(UpdateHook),reinterpret_cast<void**>(&s.updateOriginal))){s.error="public SDK registrar unavailable";return false;}
        // Root's existing global MH_EnableHook owns activation. No second enable.
        s.enabled.store(true,std::memory_order_seq_cst);captureEnabled.store(true,std::memory_order_seq_cst);return true;
    }catch(...){++s.errors;return false;}
}
bool StartEarlySdkObservers()noexcept {
    auto& s=S();if(!s.configured||!s.enabled.load()||s.stopping.load())return false;
    for(auto entry:s.hooks){if(!entry)return false;const auto result=MH_EnableHook(entry);
        if(result!=MH_OK&&result!=MH_ERROR_ENABLED)return false;}
    return true;
}
bool SameSdkDevice(void* device,void* context)noexcept {
    auto& s=S();return s.configured&&s.device==device&&s.context==context;
}
bool BeginMaterialEye(const ReloadDrawFrameEvidence& frame)noexcept {
    Admission a;if(!a.entered)return false;auto& s=S();
    std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return false;}
    if(!s.materialObservation||s.activeEye||!MaterialEyeMatches(frame,frame)||
        frame.nowNs<s.begin||frame.nowNs>=s.deadline||s.draws.size()>=MaxDraws)return false;
    s.activeEye=frame;s.activeEye->producer={};s.eyeThread=GetCurrentThreadId();
    s.activeEyeThread.store(s.eyeThread,std::memory_order_release);++s.materialEyes;return true;
}
void EndMaterialEye()noexcept {
    Admission a;if(!a.entered)return;auto& s=S();
    std::lock_guard lock(s.mutex);if(s.materialObservation&&s.eyeThread==GetCurrentThreadId()){
        s.activeEye.reset();s.activeEyeThread.store(0,std::memory_order_release);}
}
bool AdmitEye(const ReloadDrawFrameEvidence& frame)noexcept {
    Admission a;if(!a.entered)return false;auto& s=S();
    if(frame.eye>1){++s.rejected;return false;}++s.eyeAttempts[frame.eye];
    if(!frame.producer.exactRequestAssociation)++s.missingProducer[frame.eye];
    const auto owner=ExactOwner(frame);if(!owner){++s.exactOwnerRejected[frame.eye];++s.rejected;return false;}
    std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return false;}
    if(frame.nowNs<s.begin||frame.nowNs>=s.deadline||frame.nowNs>=s.candidateDeadline)return false;
    if(!s.candidateFrame||s.candidateFrame->world!=owner->world||s.candidateFrame->request!=owner->request||s.candidateFrame->nativeFrame!=owner->frame){++s.candidateFrameRejected;return false;}
    if(s.selection&&!SameSelection(*s.selection,*owner)){++s.selectionRejected;return false;}
    if(!s.frozen){s.frozen=*owner;s.eyes={};s.eyes.owner=*owner;if(!s.selection)s.selection=*owner;}
    if(!s.eyes.Admit(frame)){++s.ownerRejected;return false;}
    s.activeEye=frame;s.eyeThread=GetCurrentThreadId();
    s.activeEyeThread.store(s.eyeThread,std::memory_order_release);return true;
}
void CountCandidate(ID3D11DeviceContext* context,unsigned count,unsigned instances,unsigned startInstance,bool instanced)noexcept {
    drawApi={context,count,instances,startInstance,instanced};Admission a;
    if(!a.entered||context!=S().context||!WeaponDrawCountCandidate(Catalog(),count))return;
    auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);
    // Only an exact admitted render-eye thread can make a busy count evidence
    // against emptiness. Loading/pre-admission callbacks must not poison retry.
    if(!lock){++s.busy;if(CandidateBusyScope(s.activeEyeThread.load(std::memory_order_acquire),GetCurrentThreadId()))
        ++s.unclassifiedCandidates;return;}if(!s.activeEye||s.eyeThread!=GetCurrentThreadId()||(!s.materialObservation&&!s.frozen))return;
    ++s.eyes.catalogCandidates;++s.catalogCandidates;
    if(instanced)++s.instancedCandidates;else ++s.indexedCandidates;
    if(instances!=1||startInstance!=0)++s.unsupportedInstances;
}
bool EndEye(const ReloadDrawFrameEvidence& frame)noexcept {
    Admission a;if(!a.entered)return false;auto& s=S();
    unsigned endingThread=GetCurrentThreadId();
    s.activeEyeThread.compare_exchange_strong(endingThread,0,std::memory_order_acq_rel);
    std::unique_lock lock(s.mutex,std::try_to_lock);
    if(!lock){++s.busy;return false;}
    auto ended=frame;ended.nowNs=Now();
    if(ended.nowNs>=s.deadline||s.eyeThread!=GetCurrentThreadId()||!s.activeEye||!s.frozen||
       s.activeEye->view!=frame.view||!s.eyes.Complete(ended)){++s.endRejected;return false;}
    s.activeEye.reset();if(!s.eyes.RetryEmpty()||!s.draws.empty()||s.unclassifiedCandidates.load())return false;
    // Completed unrelated write maps were discarded at Unmap. Keep outstanding
    // maps so a later exact Pack destination can still be associated correctly.
    s.uploads.erase(std::remove_if(s.uploads.begin(),s.uploads.end(),[](const auto& u){return !u.mapped;}),s.uploads.end());
    for(auto& u:s.uploads){u.serial=u.hash=0;u.offset=u.copy=0;u.committed=false;}
    s.retiredFrame=s.frozen->frame;++s.retries;s.frozen.reset();s.candidateFrame.reset();
    s.candidateSource={};s.candidateDeadline=0;s.packs.clear();s.eyes={};return true;
}
bool WantPacked(bool first,std::uint64_t serial,const ReloadProducerView& view,const ReloadPackedSource& source,std::int64_t now)noexcept {
    Admission a;if(!a.entered||!source.handsNamed||source.boneCount!=147||source.rigFingerprint!=0xa7f219a1426216abull||
       !view.world||!view.request||!view.nativeFrame||view.eye>1||now<S().begin||now>=S().deadline)return false;
    auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return false;}
    if(!s.candidateFrame){const FrozenOwner owner{view.world,view.request,source.owner.actor,source.owner.weak,
        source.owner.weapon,source.owner.ownerGeneration,source.physicalEquipmentGeneration,source.owner.space,
        source.rigPose,source.inputGeneration,0,view.nativeFrame};
        const auto start=CandidateStart(owner,view.eye,s.selection,s.retiredFrame);
        if(start==CandidateStartDecision::WrongEye){++s.eye1StartsRejected;return false;}
        if(start!=CandidateStartDecision::Admit){++s.selectionRejected;return false;}}
    if(!s.candidateFrame){s.candidateFrame=view;s.candidateSource=source;s.candidateDeadline=std::min(s.deadline,now+15000000000ll);}
    if(now>=s.candidateDeadline)return false;
    const auto& f=*s.candidateFrame;if(f.world!=view.world||f.request!=view.request||f.nativeFrame!=view.nativeFrame||
       s.candidateSource!=source)return false;
    const auto p=std::find_if(s.packs.begin(),s.packs.end(),[&](const auto& p){return p.serial==serial;});
    return first?(s.packs.size()<MaxPacks&&p==s.packs.end()):(p!=s.packs.end()&&!p->paired);
}
void ObservePacked(bool first,std::uint64_t serial,const ReloadProducerView& view,const ReloadPackedSource& source,const RigSnapshot& rig,
    std::uint64_t destination,std::span<const std::byte> packed,std::int64_t now)noexcept {
    Admission a;if(!a.entered||!serial||!destination||!source.handsNamed||source.boneCount!=147||rig.identity.count!=147||
       rig.identity.pose!=source.rigPose||rig.identity.soldier!=source.owner.actor||rig.identity.weak!=source.owner.weak||
       source.rigFingerprint!=0xa7f219a1426216abull||packed.size()!=147*48||now<S().begin||now>=S().deadline)return;
    try{if(SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)!=source.rigFingerprint||!native_hand_detail::Bones(rig))return;
        auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return;}const auto hash=ReloadDrawHash(packed);
        if(!s.candidateFrame||s.candidateFrame->world!=view.world||s.candidateFrame->request!=view.request||s.candidateFrame->nativeFrame!=view.nativeFrame||s.candidateSource!=source)return;
        auto p=std::find_if(s.packs.begin(),s.packs.end(),[&](const auto& p){return p.serial==serial;});
        if(first){if(p!=s.packs.end()||s.packs.size()>=MaxPacks){++s.rejected;return;}Pack item;item.source=source;item.serial=serial;item.hash=hash;
            item.destination[0]=destination;item.bytes.assign(packed.begin(),packed.end());item.now=now;item.eye=view.eye;item.frame=view.nativeFrame;s.packs.push_back(std::move(item));p=s.packs.end()-1;
        }else{if(p==s.packs.end()||p->paired||p->source!=source||p->hash!=hash||p->destination[0]==destination||p->bytes.size()!=packed.size()||
            !std::equal(packed.begin(),packed.end(),p->bytes.begin())){++s.rejected;return;}p->destination[1]=destination;p->paired=true;}
        for(auto& u:s.uploads)if(u.mapped&&destination>=u.base&&destination-u.base<=u.bytes&&packed.size()<=u.bytes-(destination-u.base)){
            u.serial=serial;u.hash=hash;u.offset=unsigned(destination-u.base);u.copy=unsigned(packed.size());}
    }catch(...){++S().errors;}
}
namespace {
// Only supported texture families are copied. A format/view/mip the decoder
// cannot represent is a recorded gap, never a guessed texture receipt.
bool TextureRows(unsigned format,unsigned width,unsigned height,unsigned& pitch,unsigned& rows){
    if(format==DXGI_FORMAT_BC1_UNORM||format==DXGI_FORMAT_BC1_UNORM_SRGB||format==DXGI_FORMAT_BC4_UNORM||format==DXGI_FORMAT_BC4_SNORM){pitch=((width+3)/4)*8;rows=(height+3)/4;return true;}
    if((format>=DXGI_FORMAT_BC2_TYPELESS&&format<=DXGI_FORMAT_BC3_UNORM_SRGB)||(format>=DXGI_FORMAT_BC5_TYPELESS&&format<=DXGI_FORMAT_BC5_SNORM)||
       (format>=DXGI_FORMAT_BC6H_TYPELESS&&format<=DXGI_FORMAT_BC7_UNORM_SRGB)){pitch=((width+3)/4)*16;rows=(height+3)/4;return true;}
    if(format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB){pitch=width*4;rows=height;return true;}
    return false;
}
void StageSrv(ID3D11DeviceContext* context,Draw& draw,ID3D11ShaderResourceView* srv,unsigned stage,unsigned slot,unsigned& budget){
    if(!srv||draw.resources.size()>=48)return;ResourceCopy item;item.stage=stage;item.slot=slot;item.view=Id(srv);item.owner=srv;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};srv->GetDesc(&view);item.format=view.Format;ComPtr<ID3D11Resource> resource;srv->GetResource(&resource);item.resource=Id(resource.Get());
    ComPtr<ID3D11Device> device;context->GetDevice(&device);ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11Buffer> buffer;
    if(stage==1&&view.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D&&SUCCEEDED(resource.As(&texture))){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        item.mip=view.Texture2D.MostDetailedMip;if(item.mip>=desc.MipLevels||item.mip>=32){item.failed=true;draw.resources.push_back(std::move(item));return;}
        item.width=std::max(1u,desc.Width>>item.mip);item.height=std::max(1u,desc.Height>>item.mip);
        if(desc.ArraySize!=1||desc.SampleDesc.Count!=1||item.width>2048||item.height>2048||!TextureRows(item.format,item.width,item.height,item.rowBytes,item.rows)||
           std::uint64_t(item.rowBytes)*item.rows>budget){item.failed=true;draw.resources.push_back(std::move(item));return;}
        desc.Width=item.width;desc.Height=item.height;desc.MipLevels=1;desc.ArraySize=1;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;item.result=device->CreateTexture2D(&desc,nullptr,&staging);if(SUCCEEDED(item.result)){item.staging=staging;
            context->CopySubresourceRegion(staging.Get(),0,0,0,0,texture.Get(),item.mip,nullptr);budget-=item.rowBytes*item.rows;}
    }else if(stage==0&&(view.ViewDimension==D3D11_SRV_DIMENSION_BUFFER||view.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX)&&SUCCEEDED(resource.As(&buffer))){D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
        item.firstElement=view.ViewDimension==D3D11_SRV_DIMENSION_BUFFER?view.Buffer.FirstElement:view.BufferEx.FirstElement;
        item.elements=view.ViewDimension==D3D11_SRV_DIMENSION_BUFFER?view.Buffer.NumElements:view.BufferEx.NumElements;item.structureStride=desc.StructureByteStride;
        if(!desc.ByteWidth||desc.ByteWidth>128*1024||desc.ByteWidth>budget){item.failed=true;draw.resources.push_back(std::move(item));return;}
        item.rowBytes=desc.ByteWidth;item.rows=1;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
        ComPtr<ID3D11Buffer> staging;item.result=device->CreateBuffer(&desc,nullptr,&staging);if(SUCCEEDED(item.result)){item.staging=staging;context->CopyResource(staging.Get(),buffer.Get());budget-=item.rowBytes;}
    }else{item.failed=true;draw.resources.push_back(std::move(item));return;}
    item.failed=!item.staging;draw.resources.push_back(std::move(item));
}
bool SameDraw(const ReloadDrawCapturePacket& a,const ReloadDrawCapturePacket& b){return a.frame.world==b.frame.world&&a.frame.request==b.frame.request&&a.frame.view==b.frame.view&&
    a.frame.nativeFrame==b.frame.nativeFrame&&a.frame.eye==b.frame.eye&&a.ordinal==b.ordinal&&a.count==b.count&&a.startIndex==b.startIndex&&a.baseVertex==b.baseVertex&&a.sources==b.sources;}
}
void Pipeline(ID3D11DeviceContext* context,const ReloadDrawCapturePacket& packet)noexcept {
    returningDraw=UINT32_MAX;Admission a;if(!a.entered||context!=S().context)return;
    try{auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return;}
        const auto owner=ExactOwner(packet.frame);
        const bool material=s.materialObservation&&s.activeEye&&s.eyeThread==GetCurrentThreadId()&&
            MaterialEyeMatches(*s.activeEye,packet.frame);
        if((s.materialObservation?!material:(!owner||s.frozen!=owner))||s.draws.size()>=MaxDraws){++s.rejected;return;}
        Draw draw;draw.packet=packet;if(material)draw.packet.frame.producer={};
        if(drawApi.context==context&&drawApi.count==packet.count){draw.apiObserved=true;draw.indexedInstanced=drawApi.instanced;draw.instances=drawApi.instances;draw.startInstance=drawApi.startInstance;}
        // Retain unsupported instance parameters as diagnostics. They cannot
        // establish an instance transform or authorize replacement geometry.
        draw.layoutMissing=!packet.layout||std::none_of(s.layouts.begin(),s.layouts.end(),[&](const auto& l){return l.id==packet.layout;});
        draw.vsMissing=!packet.vs||std::none_of(s.shaders.begin(),s.shaders.end(),[&](const auto& sh){return !sh.pixel&&sh.id==packet.vs;});
        draw.psMissing=packet.ps&&std::none_of(s.shaders.begin(),s.shaders.end(),[&](const auto& sh){return sh.pixel&&sh.id==packet.ps;});
        s.missingLayoutMetadata+=draw.layoutMissing;s.missingVsBytecode+=draw.vsMissing;s.missingPsBytecode+=draw.psMissing;
        context->IAGetInputLayout(&draw.layout);context->VSGetShader(&draw.vs,nullptr,nullptr);context->PSGetShader(&draw.ps,nullptr,nullptr);
        context->OMGetRenderTargets(1,&draw.target,&draw.depth);ComPtr<ID3D11DepthStencilState> ds;UINT stencil=0;context->OMGetDepthStencilState(&ds,&stencil);
        if(ds){D3D11_DEPTH_STENCIL_DESC d{};ds->GetDesc(&d);draw.depthEnabled=d.DepthEnable;draw.depthWrite=d.DepthWriteMask;draw.depthFunc=d.DepthFunc;}
        else{draw.depthEnabled=true;draw.depthWrite=D3D11_DEPTH_WRITE_MASK_ALL;draw.depthFunc=D3D11_COMPARISON_LESS;}
        std::array<ID3D11Buffer*,14> cb{};context->VSGetConstantBuffers(0,14,cb.data());for(unsigned n=0;n<14;++n)draw.constants[n].Attach(cb[n]);
        unsigned budget=4*1024*1024;for(unsigned stage=0;stage<2;++stage){std::array<ID3D11ShaderResourceView*,128> raw{};
            if(stage)context->PSGetShaderResources(0,128,raw.data());else context->VSGetShaderResources(0,128,raw.data());
            for(unsigned slot=0;slot<128;++slot){ComPtr<ID3D11ShaderResourceView> srv;srv.Attach(raw[slot]);StageSrv(context,draw,srv.Get(),stage,slot,budget);}}
        for(const auto& u:s.uploads)if(!material&&owner&&u.committed&&u.hash==owner->packedHash){const auto p=std::find_if(s.packs.begin(),s.packs.end(),[&](const auto& p){return p.paired&&p.serial==u.serial&&
            p.source.owner.actor==owner->actor&&p.source.owner.weak==owner->weak&&p.source.owner.weapon==owner->weapon&&p.source.owner.ownerGeneration==owner->owner&&
            p.source.owner.space==owner->space&&p.source.rigPose==owner->rigPose&&p.source.inputGeneration==owner->input&&p.source.physicalEquipmentGeneration==owner->equipment;});
            if(p!=s.packs.end())draw.lineage.push_back(u);}
        returningDraw=unsigned(s.draws.size());s.draws.push_back(std::move(draw));
    }catch(...){++S().errors;}
}
void IndexedReturned(ID3D11DeviceContext* context,unsigned count,unsigned start,std::int32_t base)noexcept {
    Admission a;const auto n=returningDraw;returningDraw=UINT32_MAX;if(!a.entered||context!=S().context||n==UINT32_MAX)return;
    auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return;}if(n<s.draws.size()){auto& d=s.draws[n];
        if(d.packet.count==count&&d.packet.startIndex==start&&d.packet.baseVertex==base)d.originalReturned=true;}
}
void Complete(const ReloadDrawCapturePacket& packet)noexcept {
    Admission a;if(!a.entered)return;try{auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return;}
        const auto at=std::find_if(s.draws.begin(),s.draws.end(),[&](const auto& d){return SameDraw(d.packet,packet);});if(at==s.draws.end())return;
        for(unsigned n=0;n<16;++n)at->bytes[n].assign(packet.bytes[n].begin(),packet.bytes[n].end());at->matched=packet.match.status==WeaponDrawMatchStatus::Unique;
        at->section=at->matched?packet.match.index:UINT32_MAX;
    }catch(...){++S().errors;}
}
void Poll(ID3D11DeviceContext* context)noexcept {
    Admission a;if(!a.entered||context!=S().context)return;try{auto& s=S();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.busy;return;}
        struct LocalReadback {LocalReadback(){localReadback=true;}~LocalReadback(){localReadback=false;}} local;
        for(auto& draw:s.draws)for(auto& r:draw.resources)if(!r.done&&!r.failed&&r.staging){D3D11_MAPPED_SUBRESOURCE mapped{};
            r.result=context->Map(r.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);if(r.result==DXGI_ERROR_WAS_STILL_DRAWING)continue;
            if(FAILED(r.result)){r.failed=true;continue;}if(mapped.pData&&mapped.RowPitch>=r.rowBytes){r.bytes.resize(std::size_t(r.rowBytes)*r.rows);
                for(unsigned row=0;row<r.rows;++row)std::memcpy(r.bytes.data()+std::size_t(row)*r.rowBytes,static_cast<const std::byte*>(mapped.pData)+std::size_t(row)*mapped.RowPitch,r.rowBytes);r.done=true;
            }else r.failed=true;context->Unmap(r.staging.Get(),0);r.staging.Reset();}
    }catch(...){++S().errors;}
}
bool Stop()noexcept {captureEnabled.store(false,std::memory_order_seq_cst);if(!captureConfigured.load())return true;auto& s=S();s.enabled.store(false,std::memory_order_seq_cst);
    if(!s.stopping.exchange(true))for(auto entry:s.hooks)if(entry){const auto result=MH_DisableHook(entry);if(result!=MH_OK&&result!=MH_ERROR_DISABLED)s.disableFailed.store(true);}
    return !s.disableFailed.load()&&s.callbacks.load(std::memory_order_seq_cst)==0;
}
namespace {
void Counters(std::ostream& out,const State& s){
    out<<",\"material_observation_only\":"<<(s.materialObservation?"true":"false")<<",\"material_eye_scopes\":"<<s.materialEyes
       <<",\"selected_draw_owner_verified\":false"
       <<",\"empty_frame_retries\":"<<s.retries<<",\"selection_anchor_present\":"<<(s.selection?"true":"false")
       <<",\"eye1_candidate_starts_rejected\":"<<s.eye1StartsRejected<<",\"candidate_frame_rejected\":"<<s.candidateFrameRejected
       <<",\"eye_admission_attempts\":["<<s.eyeAttempts[0].load()<<','<<s.eyeAttempts[1].load()<<']'
       <<",\"eye_missing_exact_producer\":["<<s.missingProducer[0].load()<<','<<s.missingProducer[1].load()<<']'
       <<",\"eye_exact_owner_rejected\":["<<s.exactOwnerRejected[0].load()<<','<<s.exactOwnerRejected[1].load()<<']'
       <<",\"selection_rejected\":"<<s.selectionRejected<<",\"owner_rejected\":"<<s.ownerRejected
       <<",\"end_eye_rejected\":"<<s.endRejected<<",\"admitted_eye_mask\":"<<s.eyes.admitted
       <<",\"completed_eye_mask\":"<<s.eyes.completed<<",\"catalog_candidates\":"<<s.catalogCandidates
       <<",\"indexed_candidates\":"<<s.indexedCandidates<<",\"indexed_instanced_candidates\":"<<s.instancedCandidates
       <<",\"unsupported_instance_candidates\":"<<s.unsupportedInstances
       <<",\"unclassified_catalog_candidates\":"<<s.unclassifiedCandidates.load()
       <<",\"layout_evictions\":"<<s.layoutEvictions<<",\"shader_evictions\":"<<s.shaderEvictions
       <<",\"layout_pinned_full\":"<<s.layoutPinnedFull<<",\"shader_pinned_full\":"<<s.shaderPinnedFull
       <<",\"missing_layout_at_admission\":"<<s.missingLayoutMetadata<<",\"missing_vs_at_admission\":"<<s.missingVsBytecode
       <<",\"missing_ps_at_admission\":"<<s.missingPsBytecode;
}
}
void WriteLocal()noexcept {
    if(!captureConfigured.load())return;auto& s=S();if(!s.configured||s.written||Enabled()||s.callbacks.load())return;
    try{std::lock_guard lock(s.mutex);std::filesystem::create_directories(s.folder);std::ofstream out(s.folder/"metadata.json");
        out<<"{\"schema\":\"bc2.private.native_hand_draw_capture.v1\",\"read_only\":true,\"replacement_draw_active\":false,\"frozen\":";
        if(s.frozen){const auto& f=*s.frozen;out<<"{\"world\":"<<f.world<<",\"request\":"<<f.request<<",\"actor\":"<<f.actor<<",\"weak\":"<<f.weak<<",\"weapon\":"<<f.weapon<<",\"owner\":"<<f.owner<<",\"equipment\":"<<f.equipment
            <<",\"space\":"<<f.space<<",\"rig_pose\":"<<f.rigPose<<",\"input\":"<<f.input<<",\"packed_hash\":"<<f.packedHash<<",\"frame\":"<<f.frame<<'}';}else out<<"null";
        out<<",\"layouts\":[";for(unsigned n=0;n<s.layouts.size();++n){if(n)out<<',';const auto& l=s.layouts[n];out<<"{\"id\":"<<l.id<<",\"elements\":[";
            for(unsigned k=0;k<l.elements.size();++k){if(k)out<<',';const auto& e=l.elements[k];out<<"{\"semantic\":"<<std::quoted(e.semantic)<<",\"index\":"<<e.index<<",\"format\":"<<e.format<<",\"slot\":"<<e.slot<<",\"offset\":"<<e.offset<<",\"classification\":"<<e.classification<<",\"step\":"<<e.step<<'}';}out<<"]}";}
        out<<"],\"shaders\":[";for(unsigned n=0;n<s.shaders.size();++n){if(n)out<<',';const auto& sh=s.shaders[n];const auto file="shader-"+std::to_string(n)+".dxbc";Binary(s.folder/file,sh.bytes);
            out<<"{\"id\":"<<sh.id<<",\"pixel\":"<<(sh.pixel?"true":"false")<<",\"file\":"<<std::quoted(file)<<",\"reflection\":";Reflection(out,&sh);out<<'}';}
        out<<"],\"packs\":[";for(unsigned n=0;n<s.packs.size();++n){if(n)out<<',';const auto& p=s.packs[n];const auto file="pack-"+std::to_string(n)+".bin";Binary(s.folder/file,p.bytes);
            out<<"{\"serial\":"<<p.serial<<",\"hash\":"<<p.hash<<",\"paired\":"<<(p.paired?"true":"false")<<",\"eye\":"<<p.eye<<",\"frame\":"<<p.frame<<",\"rig_pose\":"<<p.source.rigPose<<",\"actor\":"<<p.source.owner.actor<<",\"weak\":"<<p.source.owner.weak<<",\"weapon\":"<<p.source.owner.weapon
                <<",\"owner\":"<<p.source.owner.ownerGeneration<<",\"space\":"<<p.source.owner.space<<",\"equipment\":"<<p.source.physicalEquipmentGeneration<<",\"input\":"<<p.source.inputGeneration<<",\"file\":"<<std::quoted(file)<<'}';}
        out<<"],\"draws\":[";for(unsigned n=0;n<s.draws.size();++n){if(n)out<<',';const auto& d=s.draws[n];const auto& p=d.packet;
            out<<"{\"id\":"<<n<<",\"section\":"<<d.section<<",\"geometry_unique\":"<<(d.matched?"true":"false")<<",\"eye\":"<<p.frame.eye<<",\"view\":"<<p.frame.view<<",\"world\":"<<p.frame.world<<",\"request\":"<<p.frame.request<<",\"frame\":"<<p.frame.nativeFrame
                <<",\"actor\":"<<p.frame.producer.actor<<",\"weak\":"<<p.frame.producer.weak<<",\"weapon\":"<<p.frame.producer.weapon<<",\"owner\":"<<p.frame.producer.ownerGeneration<<",\"equipment\":"<<p.frame.producer.physicalEquipmentGeneration<<",\"space\":"<<p.frame.producer.space<<",\"rig_pose\":"<<p.frame.producer.rigPose<<",\"input\":"<<p.frame.producer.inputGeneration<<",\"packed_hash\":"<<p.frame.producer.packedPaletteHash
                <<",\"count\":"<<p.count<<",\"stride\":"<<p.stride<<",\"vertex_offset\":"<<p.vertexOffset<<",\"index_bytes\":"<<p.indexBytes<<",\"base_vertex\":"<<p.baseVertex<<",\"layout\":"<<p.layout<<",\"vs\":"<<p.vs<<",\"ps\":"<<p.ps<<",\"target\":"<<p.target<<",\"depth\":"<<p.depth
                <<",\"producer_authority_verified\":"<<(!s.materialObservation&&ExactOwner(p.frame)?"true":"false")
                <<",\"api_observed\":"<<(d.apiObserved?"true":"false")<<",\"indexed_instanced\":"<<(d.indexedInstanced?"true":"false")
                <<",\"instances\":"<<d.instances<<",\"start_instance\":"<<d.startInstance
                <<",\"instance_transform_contract_supported\":"<<(InstanceTransformSupported(d.apiObserved,d.instances,d.startInstance)?"true":"false")
                <<",\"layout_metadata_missing_at_admission\":"<<(d.layoutMissing?"true":"false")
                <<",\"vs_bytecode_missing_at_admission\":"<<(d.vsMissing?"true":"false")
                <<",\"ps_bytecode_missing_at_admission\":"<<(d.psMissing?"true":"false")
                <<",\"depth_enabled\":"<<(d.depthEnabled?"true":"false")<<",\"depth_write\":"<<d.depthWrite<<",\"depth_func\":"<<d.depthFunc<<",\"original_returned\":"<<(d.originalReturned?"true":"false")<<",\"buffers\":[";
            for(unsigned k=0;k<16;++k){if(k)out<<',';const auto file="draw-"+std::to_string(n)+"-buffer-"+std::to_string(k)+".bin";if(!d.bytes[k].empty())Binary(s.folder/file,d.bytes[k]);
                out<<"{\"binding\":"<<k<<",\"resource\":"<<p.sources[k]<<",\"bytes\":"<<d.bytes[k].size()<<",\"file\":"<<std::quoted(d.bytes[k].empty()?"":file)<<'}';}
            out<<"],\"srvs\":[";for(unsigned k=0;k<d.resources.size();++k){if(k)out<<',';const auto& r=d.resources[k];const auto file="draw-"+std::to_string(n)+"-srv-"+std::to_string(k)+".bin";if(r.done)Binary(s.folder/file,r.bytes);
                out<<"{\"stage\":"<<r.stage<<",\"slot\":"<<r.slot<<",\"view\":"<<r.view<<",\"resource\":"<<r.resource<<",\"format\":"<<r.format<<",\"width\":"<<r.width<<",\"height\":"<<r.height<<",\"mip\":"<<r.mip<<",\"first_element\":"<<r.firstElement<<",\"elements\":"<<r.elements<<",\"structure_stride\":"<<r.structureStride
                    <<",\"done\":"<<(r.done?"true":"false")<<",\"failed\":"<<(r.failed?"true":"false")<<",\"file\":"<<std::quoted(r.done?file:"")<<'}';}
            out<<"],\"uploads\":[";for(unsigned k=0;k<d.lineage.size();++k){if(k)out<<',';const auto& u=d.lineage[k];out<<"{\"resource\":"<<Id(u.resource.Get())<<",\"offset\":"<<u.offset<<",\"bytes\":"<<u.copy<<",\"serial\":"<<u.serial<<",\"hash\":"<<u.hash<<",\"committed\":"<<(u.committed?"true":"false")<<'}';}out<<"]}";}
        out<<"]";Counters(out,s);out<<",\"errors\":"<<s.errors.load()<<",\"busy\":"<<s.busy.load()<<",\"rejected\":"<<s.rejected.load()<<",\"setup_error\":"<<std::quoted(s.error)<<'}';
        out.close();if(!out)throw std::runtime_error("capture metadata write");s.written=true;
    }catch(...){++s.errors;}
}
void Report(std::ostream& out){if(!captureConfigured.load()){out<<"{\"requested\":false,\"replacement_draw_active\":false}";return;}auto& s=S();
    if(Enabled()||s.callbacks.load()){out<<"{\"requested\":true,\"disabled\":false,\"drained\":false,\"replacement_draw_active\":false}";return;}
    out<<"{\"requested\":"<<(s.configured?"true":"false")<<",\"disabled\":true,\"drained\":true,\"local_written\":"<<(s.written?"true":"false");
    Counters(out,s);out<<",\"errors\":"<<s.errors.load()<<",\"busy\":"<<s.busy.load()<<",\"rejected\":"<<s.rejected.load()<<",\"replacement_draw_active\":false}";
}
} // namespace fvr::bc2::nativeHandCapture
