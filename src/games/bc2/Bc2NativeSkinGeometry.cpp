#include "Bc2NativeSkinGeometry.h"
#include "Bc2Xm8BoltAsset.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include <ostream>
#include <bit>
#include <cmath>

namespace fvr::bc2::nativeSkinGeometry {
namespace {
using Microsoft::WRL::ComPtr;
using Digest=std::array<std::byte,32>;
static_assert(sizeof(unsigned)==4);
enum class Gate : unsigned {Owner,Device,Verifier,Variant,Palette,GraphicsThread,Asset,LodRead,ContainerRead,PartsRead,
    BindingsRead,BucketCount,BucketIndex,Coverage,BindingRead,BoneCount,RemapRead,RemapMismatch,GeometryRead,Streams,
    Topology,TriangleCount,VbWrapperRead,IbWrapperRead,DeclarationRead,Stride,VbType,IbType,IndexFormat,IndexGetter,
    BufferNull,BufferDevice,BufferUsage,BufferCpu,BufferBind,BufferRange,CacheLimit,FrameFailure,SourcePending,Build,Exception,ActivationPending,AssetMatch,CacheBytes,Count};
constexpr const char* gateNames[]{"owner","device","verifier","variant","palette_count","graphics_thread","asset",
    "lod_read","container_read","parts_read","bindings_read","bucket_count","bucket_index","coverage","binding_read",
    "bone_count","remap_read","remap_mismatch","geometry_read","streams","topology","triangle_count","vb_wrapper_read",
    "ib_wrapper_read","declaration_read","stride","vb_type","ib_type","index_format","index_getter","buffer_null",
    "buffer_device","buffer_usage","buffer_cpu","buffer_bind","buffer_range","cache_limit","frame_failure","source_pending","build","exception","activation_pending","asset_match","cache_bytes"};
static_assert(std::size(gateNames)==unsigned(Gate::Count));
struct Rejection {std::atomic<std::uint64_t> count=0,actual=0,expected=0;std::atomic<unsigned> part=UINT32_MAX;};
std::array<Rejection,unsigned(Gate::Count)> rejections{};
std::atomic<std::uint64_t> attempts=0,accepted=0;
bool Reject(Gate gate,unsigned part=UINT32_MAX,std::uint64_t actual=0,std::uint64_t expected=0){
    auto& r=rejections[unsigned(gate)];if(r.count.fetch_add(1,std::memory_order_relaxed)==0){
        r.part.store(part,std::memory_order_relaxed);r.actual.store(actual,std::memory_order_relaxed);r.expected.store(expected,std::memory_order_relaxed);}
    return false;
}
template<class T>T At(const void* p,std::size_t offset){T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof v);return v;}
template<class T>void Put(void* p,std::size_t offset,T v){std::memcpy(static_cast<std::byte*>(p)+offset,&v,sizeof v);}
bool HashMatches(std::span<const std::byte> bytes,const Digest& expected){
    const auto hex=Xm8BoltSha256Hex(bytes);constexpr char digits[]="0123456789abcdef";
    if(hex.size()!=64)return false;
    for(unsigned i=0;i<32;++i){const auto b=std::to_integer<unsigned>(expected[i]);
        if(hex[i*2]!=digits[b>>4]||hex[i*2+1]!=digits[b&15])return false;}
    return true;
}
struct Section {
    unsigned part=0,action=0,stride=0,sourceVertices=0,sourceIndices=0,sourceOffset=0,sourceFirstIndex=0,paletteCount=0;
    unsigned vertices=0,indices=0,layout=0;Digest sourceVB{},sourceIB{};
    std::array<std::uint16_t,64> remap{};
    std::vector<std::byte> vb,ib;
};
// BC2NSK03 explicitly separates the two incompatible 64-byte streams.
// Tags 0/2 store half4 position + byte4 IDs/weights; tag1 stores float3.
bool Color(const Section& s){return s.stride==(s.layout==0?48u:64u);}
bool LayoutValid(const Section& s){
    return s.layout==0?(s.stride==16||s.stride==48):
        s.layout==1?(s.stride==32||s.stride==64):
        s.layout==2&&(s.stride==16||s.stride==64);
}
unsigned IdOffset(const Section& s){return s.layout==1?12u:8u;}
float Half(std::uint16_t h){
    const auto exponent=(h>>10)&31u,mantissa=h&1023u;
    const float value=exponent?std::ldexp(float(1024+mantissa),int(exponent)-25):std::ldexp(float(mantissa),-24);
    return h&0x8000?-value:value;
}
std::array<float,3> Position(const Section& s,const std::byte* v){
    if(s.layout==1)return At<std::array<float,3>>(v,0);
    return {Half(At<std::uint16_t>(v,0)),Half(At<std::uint16_t>(v,2)),Half(At<std::uint16_t>(v,4))};
}
struct Asset {std::uint64_t rig=0;Digest body{};unsigned sectionCount=0;std::array<Section,MaxSections> sections;};
struct State {
    std::mutex mutex;std::vector<std::unique_ptr<Asset>> assets;bool sealed=false,buildFailedThisFrame=false;
    std::size_t publishedBytes=0;
#if defined(_M_IX86)
    struct Entry;
    std::vector<std::unique_ptr<Entry>> entries;
#endif
};
// Deliberately never destroyed: native CPU commands can outlive mod stop and
// GPU work can outlive CPU queue consumption. Admission is bounded below.
State& Get(){static State* state=new State;return *state;}
// Separate per-prop pools also disambiguate two rigid parts of one source gun.
std::array<State,32>& Props(){static auto* states=new std::array<State,32>;return *states;}
std::mutex propConfigMutex;unsigned propCount=0;
bool Range(std::size_t size,unsigned offset,std::uint64_t count,unsigned payloadStart){return offset>=payloadStart&&offset<=size&&count<=size-offset;}
std::unique_ptr<Asset> Parse(std::span<const std::byte> bytes,std::uint64_t expectedRig){
    if(bytes.size()<128||bytes.size()>8*1024*1024)return {};
    const auto version=At<unsigned>(bytes.data(),8),count=At<unsigned>(bytes.data(),24);
    const bool legacy=version==1&&!std::memcmp(bytes.data(),"BC2NSK01",8)&&count==4;
    const bool current=version==2&&!std::memcmp(bytes.data(),"BC2NSK02",8)&&count>=1&&count<=MaxSections;
    const bool explicitLayout=version==3&&!std::memcmp(bytes.data(),"BC2NSK03",8)&&count>=1&&count<=MaxSections;
    const unsigned payloadStart=128+count*320;
    if((!legacy&&!current&&!explicitLayout)||bytes.size()<payloadStart||At<unsigned>(bytes.data(),12)!=bytes.size()||
       At<std::uint64_t>(bytes.data(),16)!=expectedRig||!expectedRig||
       At<unsigned>(bytes.data(),28)!=1)return {};
    auto asset=std::make_unique<Asset>();asset->rig=expectedRig;asset->body=At<Digest>(bytes.data(),96);asset->sectionCount=count;
    if(!HashMatches(bytes.subspan(128),asset->body))return {};
    unsigned colorCount=0,depthPart=UINT32_MAX;
    for(unsigned i=0;i<count;++i){auto& s=asset->sections[i];const auto* p=bytes.data()+128+i*320;
        s.part=At<unsigned>(p,0);s.action=At<unsigned>(p,4);s.stride=At<unsigned>(p,8);
        s.sourceVertices=At<unsigned>(p,12);s.sourceIndices=At<unsigned>(p,16);
        s.sourceOffset=At<unsigned>(p,20);s.sourceFirstIndex=At<unsigned>(p,24);s.paletteCount=At<unsigned>(p,28);
        s.vertices=At<unsigned>(p,32);s.indices=At<unsigned>(p,36);s.layout=At<unsigned>(p,56);
        const auto vo=At<unsigned>(p,40),vs=At<unsigned>(p,44),io=At<unsigned>(p,48),is=At<unsigned>(p,52);
        s.sourceVB=At<Digest>(p,64);s.sourceIB=At<Digest>(p,96);s.remap=At<decltype(s.remap)>(p,192);
        if(s.part!=i||s.action>1||!s.sourceVertices||s.sourceVertices>65535||
           !s.sourceIndices||s.sourceIndices>1000000||s.sourceIndices%3||!s.paletteCount||s.paletteCount>64||
           !LayoutValid(s)||(!explicitLayout&&s.layout)||At<unsigned>(p,60))return {};
        for(unsigned n=0;n<64;++n)if(n<s.paletteCount?s.remap[n]>=147:s.remap[n]!=0xffff)return {};
        if(!s.action){if(s.vertices||s.indices||vo||vs||io||is)return {};continue;}
        if(Color(s))++colorCount;
        else {if(depthPart!=UINT32_MAX)return {};depthPart=i;}
        if(!s.vertices||s.vertices>65535||!s.indices||s.indices>1000000||s.indices%3||
           vs!=std::uint64_t(s.vertices)*s.stride||is!=std::uint64_t(s.indices)*2||
           !Range(bytes.size(),vo,vs,payloadStart)||!Range(bytes.size(),io,is,payloadStart))return {};
        const auto vb=bytes.subspan(vo,vs),ib=bytes.subspan(io,is);
        if(!HashMatches(vb,At<Digest>(p,128))||!HashMatches(ib,At<Digest>(p,160)))return {};
        for(unsigned n=0;n<s.indices;++n)if(At<std::uint16_t>(ib.data(),n*2)>=s.vertices)return {};
        for(unsigned n=0;n<s.vertices;++n){const auto* v=vb.data()+n*s.stride;
            const auto ids=IdOffset(s);unsigned weight=0;for(unsigned b=0;b<4;++b){
                const auto w=std::to_integer<unsigned>(v[ids+4+b]);weight+=w;
                if(w&&std::to_integer<unsigned>(v[ids+b])>=s.paletteCount)return {};}
            if(weight!=255)return {};
            if(s.layout==1){for(const auto value:Position(s,v))if(!std::isfinite(value))return {};}
            else for(unsigned b=0;b<4;++b)if((At<std::uint16_t>(v,b*2)&0x7c00)==0x7c00)return {};
        }
        s.vb.assign(vb.begin(),vb.end());s.ib.assign(ib.begin(),ib.end());
    }
    // Different vertex deduplication and palettes are allowed between color
    // and depth, but every indexed corner must describe the same skinned point.
    if(!colorCount||depthPart==UINT32_MAX)return {};
    const auto& depth=asset->sections[depthPart];unsigned totalIndices=0;
    for(const auto& section:asset->sections)if(section.action&&Color(section))totalIndices+=section.indices;
    if(totalIndices!=depth.indices)return {};
    const auto skin=[](const Section& s,const std::byte* vertex){
        std::array<std::uint16_t,147> weights{};const auto ids=IdOffset(s);
        for(unsigned n=0;n<4;++n){const auto w=std::to_integer<unsigned>(vertex[ids+4+n]);
            if(w)weights[s.remap[std::to_integer<unsigned>(vertex[ids+n])]]+=std::uint16_t(w);}
        return weights;
    };
    unsigned depthIndex=0;
    for(const auto& color:asset->sections)if(color.action&&Color(color))
        for(unsigned i=0;i<color.indices;++i,++depthIndex){
            const auto* a=color.vb.data()+At<std::uint16_t>(color.ib.data(),i*2)*color.stride;
            const auto* b=depth.vb.data()+At<std::uint16_t>(depth.ib.data(),depthIndex*2)*depth.stride;
            if(Position(color,a)!=Position(depth,b)||skin(color,a)!=skin(depth,b))return {};
        }
    return asset;
}
#if defined(_M_IX86)
bool Read(std::uint64_t address,void* out,std::size_t bytes){SIZE_T got=0;
    return address>=0x10000&&bytes&&address+bytes<=UINT32_MAX&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(std::uintptr_t(address)),out,bytes,&got)&&got==bytes;
}
template<class T>bool Read(std::uint64_t address,T& out){return Read(address,&out,sizeof out);}
unsigned Ptr(const void* p){return reinterpret_cast<unsigned>(p);}
struct Source {
    std::array<std::byte,0x50> geometry{};
    std::array<std::byte,0x34> vbWrapper{};
    std::array<std::byte,0x38> ibWrapper{};
    std::array<std::byte,0x5e> declaration{};
    ID3D11Buffer* vb=nullptr;ID3D11Buffer* ib=nullptr;
    bool operator==(const Source& other)const {
        // Native reference counts and allocator bookkeeping are not resource
        // identity. The complete geometry and declaration snapshot are.
        return !std::memcmp(geometry.data()+0x10,other.geometry.data()+0x10,0x40)&&
            declaration==other.declaration&&vb==other.vb&&ib==other.ib;
    }
};
struct Owned {
    alignas(16) std::array<std::byte,0x50> geometry{};
    std::array<std::byte,0x34> vbWrapper{};
    std::array<std::byte,0x38> ibWrapper{};
    ComPtr<ID3D11Buffer> vb,ib,sourceVB,sourceIB;
};
struct State::Entry {
    ComPtr<ID3D11Device> device;const Asset* asset=nullptr;bool ready=false;
    std::array<Source,MaxSections> sources;std::array<Owned,MaxSections> sections;
};
bool Matches(const Asset& asset,unsigned sectionCount,const std::array<Source,MaxSections>& sources,const std::array<unsigned,MaxSections>& bones,
    const std::array<std::array<std::uint16_t,64>,MaxSections>& remaps){
    if(asset.sectionCount!=sectionCount)return false;
    for(unsigned i=0;i<sectionCount;++i){const auto& s=asset.sections[i];
        if(bones[i]!=s.paletteCount||At<unsigned>(sources[i].geometry.data(),0x48)!=s.sourceIndices/3||
           std::to_integer<unsigned>(sources[i].declaration[0x5d])!=s.stride||
           !std::equal(remaps[i].begin(),remaps[i].begin()+bones[i],s.remap.begin()))return false;}
    return true;
}
bool Buffer(ID3D11Buffer* buffer,ID3D11Device* device,unsigned bind,std::uint64_t offset,std::uint64_t bytes,unsigned part){
    if(!buffer)return Reject(Gate::BufferNull,part,0,bind);ComPtr<ID3D11Device> actual;buffer->GetDevice(&actual);
    if(actual.Get()!=device)return Reject(Gate::BufferDevice,part,Ptr(actual.Get()),Ptr(device));D3D11_BUFFER_DESC d{};buffer->GetDesc(&d);
    if(d.Usage!=D3D11_USAGE_IMMUTABLE)return Reject(Gate::BufferUsage,part,d.Usage,D3D11_USAGE_IMMUTABLE);
    if(d.CPUAccessFlags)return Reject(Gate::BufferCpu,part,d.CPUAccessFlags,0);
    if(!(d.BindFlags&bind))return Reject(Gate::BufferBind,part,d.BindFlags,bind);
    if(offset>d.ByteWidth||bytes>d.ByteWidth-offset)return Reject(Gate::BufferRange,part,offset+bytes,d.ByteWidth);
    return true;
}
// Only RPM and scalar comparisons: legal on native collection workers even
// when the game's device was created SINGLETHREADED. No COM ownership changes.
bool ReadSource(unsigned geometry,unsigned part,Source& out){
    if(!Read(geometry,out.geometry))return Reject(Gate::GeometryRead,part,geometry);
    const auto streams=std::to_integer<unsigned>(out.geometry[0x10]),topology=std::to_integer<unsigned>(out.geometry[0x44]),triangles=At<unsigned>(out.geometry.data(),0x48);
    if(streams!=1)return Reject(Gate::Streams,part,streams,1);
    if(topology!=3)return Reject(Gate::Topology,part,topology,3);
    if(!triangles||triangles>333333)return Reject(Gate::TriangleCount,part,triangles,333333);
    if(!Read(At<unsigned>(out.geometry.data(),0x18),out.vbWrapper))return Reject(Gate::VbWrapperRead,part);
    if(!Read(At<unsigned>(out.geometry.data(),0x14),out.ibWrapper))return Reject(Gate::IbWrapperRead,part);
    if(!Read(At<unsigned>(out.geometry.data(),0x40),out.declaration))return Reject(Gate::DeclarationRead,part);
    const auto stride=std::to_integer<unsigned>(out.declaration[0x5d]);
    if(stride!=16&&stride!=32&&stride!=48&&stride!=64)return Reject(Gate::Stride,part,stride);
    // Only the verified build's complete native wrapper layouts are cloned.
    if(At<unsigned>(out.vbWrapper.data(),0)!=0x14495b8)return Reject(Gate::VbType,part,At<unsigned>(out.vbWrapper.data(),0),0x14495b8);
    if(At<unsigned>(out.ibWrapper.data(),0)!=0x1449534)return Reject(Gate::IbType,part,At<unsigned>(out.ibWrapper.data(),0),0x1449534);
    if(At<unsigned>(out.ibWrapper.data(),0x28)!=0)return Reject(Gate::IndexFormat,part,At<unsigned>(out.ibWrapper.data(),0x28),0);
    unsigned getter=0,code=0;
    if(!Read(0x1449534+0x20,getter)||getter!=0x9e9b00||!Read(getter,code)||code!=0xc328418b)return Reject(Gate::IndexGetter,part,getter,0x9e9b00);
    out.vb=reinterpret_cast<ID3D11Buffer*>(At<unsigned>(out.vbWrapper.data(),0xc));
    out.ib=reinterpret_cast<ID3D11Buffer*>(At<unsigned>(out.ibWrapper.data(),0xc));
    if(!out.vb||!out.ib)return Reject(Gate::BufferNull,part);
    return true;
}
bool MakeBuffer(ID3D11Device* device,const std::vector<std::byte>& bytes,unsigned bind,ComPtr<ID3D11Buffer>& out){
    D3D11_BUFFER_DESC d{};d.ByteWidth=unsigned(bytes.size());d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=bind;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=bytes.data();
    return SUCCEEDED(device->CreateBuffer(&d,&data,&out))&&out;
}
bool Build(State::Entry& e,const Asset& asset){
    for(unsigned i=0;i<asset.sectionCount;++i){auto& o=e.sections[i];const auto& source=e.sources[i];const auto& s=asset.sections[i];
        o.sourceVB=source.vb;o.sourceIB=source.ib;
        if(!s.action)continue;
        if(!MakeBuffer(e.device.Get(),s.vb,D3D11_BIND_VERTEX_BUFFER,o.vb)||!MakeBuffer(e.device.Get(),s.ib,D3D11_BIND_INDEX_BUFFER,o.ib))return false;
        o.geometry=source.geometry;o.vbWrapper=source.vbWrapper;o.ibWrapper=source.ibWrapper;
        Put(o.vbWrapper.data(),0xc,Ptr(o.vb.Get()));Put(o.ibWrapper.data(),0xc,Ptr(o.ib.Get()));
        Put(o.vbWrapper.data(),0x24,unsigned(s.vb.size()));Put(o.ibWrapper.data(),0x24,unsigned(s.ib.size()));
        Put(o.geometry.data(),0x18,Ptr(o.vbWrapper.data()));Put(o.geometry.data(),0x14,Ptr(o.ibWrapper.data()));
        Put(o.geometry.data(),0x28,0u);Put(o.geometry.data(),0x48,s.indices/3);Put(o.geometry.data(),0x4c,0u);
        // Neither native AddRef nor destructor is used on these private clones.
        // Only 9a0480 reads wrapper+c and the verified IB vf+20 getter.
    }
    return true;
}
#endif
}
bool ConfigureSet(std::span<const std::span<const std::byte>> packs,std::uint64_t expectedRig)noexcept {
    try {
        if(packs.empty()||packs.size()>32)return false;
        std::size_t total=0;for(const auto pack:packs){if(pack.size()>64*1024*1024-total)return false;total+=pack.size();}
        std::vector<std::unique_ptr<Asset>> parsed;
        for(const auto pack:packs){auto asset=Parse(pack,expectedRig);if(!asset)return false;
            bool duplicate=false;
            for(const auto& prior:parsed){if(prior->body==asset->body&&prior->sectionCount==asset->sectionCount){duplicate=true;break;}
                bool sameSource=prior->sectionCount==asset->sectionCount;
                for(unsigned i=0;i<asset->sectionCount;++i){const auto& a=prior->sections[i];const auto& b=asset->sections[i];
                    sameSource= sameSource&&a.sourceVB==b.sourceVB&&a.sourceIB==b.sourceIB&&a.stride==b.stride&&
                        a.sourceVertices==b.sourceVertices&&a.sourceIndices==b.sourceIndices&&a.paletteCount==b.paletteCount&&a.remap==b.remap;}
                // Exact same native source cannot select two different authored
                // outputs. File ordering must never decide visible geometry.
                if(sameSource)return false;
            }
            if(!duplicate)parsed.push_back(std::move(asset));
        }
        auto& state=Get();std::lock_guard lock(state.mutex);
        if(!state.assets.empty()){
            if(state.assets.size()!=parsed.size())return false;
            for(const auto& prior:state.assets){bool found=false;for(const auto& candidate:parsed)
                found=found||(prior->rig==candidate->rig&&prior->body==candidate->body&&prior->sectionCount==candidate->sectionCount);if(!found)return false;}
            return true;
        }
        if(state.sealed)return false;
        state.assets=std::move(parsed);return true;
    }catch(...){return false;}
}
bool ConfigureProps(std::span<const PropPack> packs)noexcept {
    try {
        std::lock_guard config(propConfigMutex);
        if(propCount||packs.empty()||packs.size()>32)return false;
        std::vector<std::unique_ptr<Asset>> parsed;std::size_t total=0;
        for(const auto& pack:packs){
            if(pack.bone>=147||pack.bytes.size()>64*1024*1024-total)return false;total+=pack.bytes.size();
            auto asset=Parse(pack.bytes,pack.rig);if(!asset)return false;
            // Only indexed replacement corners are rigid. Unused original VB
            // vertices deliberately retain every original UV/tangent/skin byte.
            for(const auto& section:asset->sections)if(section.action)
                for(unsigned n=0;n<section.indices;++n){
                    const auto* v=section.vb.data()+At<std::uint16_t>(section.ib.data(),n*2)*section.stride;
                    const auto ids=IdOffset(section);unsigned sum=0;
                    for(unsigned k=0;k<4;++k){const auto w=std::to_integer<unsigned>(v[ids+4+k]);
                        if(w&&section.remap[std::to_integer<unsigned>(v[ids+k])]!=pack.bone)return false;sum+=w;}
                    if(sum!=255)return false;
                }
            parsed.push_back(std::move(asset));
        }
        // Reserve before mutation: allocation failure cannot leave half a set.
        for(unsigned n=0;n<parsed.size();++n)Props()[n].assets.reserve(1);
        // Commit only after all digests, paired corners and rigid bones pass.
        for(unsigned n=0;n<parsed.size();++n){auto& state=Props()[n];std::lock_guard lock(state.mutex);
            state.assets.push_back(std::move(parsed[n]));}
        propCount=unsigned(packs.size());return true;
    }catch(...){return false;}
}
bool Configure(std::span<const std::byte> bytes,std::uint64_t expectedRig)noexcept {
    const std::array packs{bytes};return ConfigureSet(packs,expectedRig);
}
void BeginStateFrame(State& state)noexcept {try{std::lock_guard lock(state.mutex);state.buildFailedThisFrame=false;
#if defined(_M_IX86)
    // Readiness changes only at the caller's native frame boundary, never on
    // the second eye or another worker finishing within the same frame.
    for(auto& entry:state.entries)entry->ready=true;
#endif
}catch(...){}}
void BeginFrame()noexcept {BeginStateFrame(Get());for(unsigned n=0;n<propCount;++n)BeginStateFrame(Props()[n]);}
void Report(std::ostream& out){
    auto& state=Get();std::lock_guard lock(state.mutex);
    out<<"{\"prop_assets\":"<<propCount<<",\"assets\":"<<state.assets.size()<<",\"published_bytes\":"<<state.publishedBytes
       <<",\"attempts\":"<<attempts.load(std::memory_order_relaxed)<<",\"accepted\":"<<accepted.load(std::memory_order_relaxed)<<",\"rejections\":[";
    bool comma=false;for(unsigned i=0;i<unsigned(Gate::Count);++i){const auto& r=rejections[i];const auto count=r.count.load(std::memory_order_relaxed);
        if(!count)continue;if(comma)out<<',';comma=true;
        out<<"{\"gate\":\""<<gateNames[i]<<"\",\"count\":"<<count<<",\"first_part\":"<<r.part.load(std::memory_order_relaxed)
           <<",\"first_actual\":"<<r.actual.load(std::memory_order_relaxed)<<",\"first_expected\":"<<r.expected.load(std::memory_order_relaxed)<<'}';}
    out<<"]}";
}
unsigned View::Lod()const noexcept {
#if defined(_M_IX86)
    return Ptr(lod.data());
#else
    return 0;
#endif
}
bool ReplaceIn(State& state,unsigned lod,unsigned variant,unsigned paletteCount,const NativeSkinOwner& owner,ID3D11Device* device,
    VerifySource verify,void* verifyContext,View& out,unsigned graphicsThreadId)noexcept {
#if defined(_M_IX86)
    attempts.fetch_add(1,std::memory_order_relaxed);
    try {
        if(!NativeSkinOwnerValid(owner))return Reject(Gate::Owner);
        if(!device)return Reject(Gate::Device);
        if(!verify)return Reject(Gate::Verifier);
        if(variant>=8)return Reject(Gate::Variant,UINT32_MAX,variant,7);
        if(paletteCount!=147)return Reject(Gate::Palette,UINT32_MAX,paletteCount,147);
        std::lock_guard lock(state.mutex);state.sealed=true;if(state.assets.empty())return Reject(Gate::Asset);
        if(!Read(lod,out.lod))return Reject(Gate::LodRead,UINT32_MAX,lod);const auto container=At<unsigned>(out.lod.data(),0x60);
        if(!Read(container,out.container))return Reject(Gate::ContainerRead,UINT32_MAX,container);const auto parts=At<unsigned>(out.container.data(),0x14);
        unsigned variants=At<unsigned>(out.lod.data(),0x50),bindings=0;
        if(!Read(std::uint64_t(variants)+variant*0x14+4,bindings))return Reject(Gate::BindingsRead,UINT32_MAX,variants);
        std::array<Source,MaxSections> sources{};std::array<unsigned,MaxSections> bones{};std::array<unsigned,4> bucketCounts{};
        std::array<std::array<std::uint16_t,64>,MaxSections> remaps{};unsigned covered=0;
        for(unsigned b=0;b<4;++b){const auto count=At<unsigned>(out.container.data(),0x18+b*4),indices=At<unsigned>(out.container.data(),0x28+b*4);
            if(count>MaxSections)return Reject(Gate::BucketCount,b,count,MaxSections);bucketCounts[b]=count;
            for(unsigned n=0;n<count;++n){std::uint8_t index=0;if(!Read(std::uint64_t(indices)+n,index)||index>=MaxSections)return Reject(Gate::BucketIndex,b,index,MaxSections-1);
                covered|=1u<<index;out.buckets[b][n]=index;}
        }
        // Every source section must occur in the native bucket union. A hole
        // would leave an unverified section and cannot admit a complete asset.
        if(!covered||(covered&(covered+1)))return Reject(Gate::Coverage,UINT32_MAX,covered);
        const unsigned sectionCount=unsigned(std::bit_width(covered));
        if(!Read(parts,out.parts.data(),sectionCount*0x2c))return Reject(Gate::PartsRead,UINT32_MAX,parts);
        for(unsigned i=0;i<sectionCount;++i){auto& part=out.parts[i];unsigned binding=0;
            if(!bindings||!Read(std::uint64_t(bindings)+i*4,binding))return Reject(Gate::BindingRead,i,bindings);
            bones[i]=std::to_integer<unsigned>(part[0x13]);
            if(!bones[i]||bones[i]>64)return Reject(Gate::BoneCount,i,bones[i],64);
            if(!Read(At<unsigned>(part.data(),0x14),remaps[i].data(),bones[i]*2))return Reject(Gate::RemapRead,i);
            for(unsigned n=0;n<bones[i];++n)if(remaps[i][n]>=147)return Reject(Gate::RemapMismatch,i,remaps[i][n],146);
            if(!ReadSource(At<unsigned>(part.data(),0x28),i,sources[i]))return false;
        }
        State::Entry* entry=nullptr;
        for(const auto& e:state.entries)if(e->device.Get()==device&&e->sources==sources&&Matches(*e->asset,sectionCount,sources,bones,remaps)){entry=e.get();break;}
        if(!entry){
            std::array<const Asset*,32> candidates{};unsigned candidateCount=0;
            for(const auto& asset:state.assets)if(Matches(*asset,sectionCount,sources,bones,remaps))candidates[candidateCount++]=asset.get();
            if(!candidateCount)return Reject(Gate::AssetMatch,UINT32_MAX,state.assets.size());
            // Only cache misses touch COM. Existing entries pin their source
            // buffers, so pointer identity cannot be recycled under a worker.
            // ComPtr::Get above is a raw pointer read, not a COM method call.
            const auto thread=GetCurrentThreadId();
            if(!graphicsThreadId||thread!=graphicsThreadId)return Reject(Gate::GraphicsThread,UINT32_MAX,thread,graphicsThreadId);
            if(state.entries.size()>=128)return Reject(Gate::CacheLimit,UINT32_MAX,state.entries.size(),128);
            if(state.buildFailedThisFrame)return Reject(Gate::FrameFailure);
            const Asset* selected=nullptr;
            for(unsigned c=0;c<candidateCount;++c){const auto& asset=*candidates[c];bool valid=true;
                for(unsigned i=0;i<sectionCount;++i){const auto& s=asset.sections[i];const auto& source=sources[i];
                    // Native uploads may repack the archive's byte ranges.
                    if(!Buffer(source.vb,device,D3D11_BIND_VERTEX_BUFFER,At<unsigned>(source.geometry.data(),0x28),std::uint64_t(s.sourceVertices)*s.stride,i)||
                       !Buffer(source.ib,device,D3D11_BIND_INDEX_BUFFER,std::uint64_t(At<unsigned>(source.geometry.data(),0x4c))*2,std::uint64_t(s.sourceIndices)*2,i)){valid=false;break;}}
                if(!valid)continue;
                // Metadata is a candidate filter, never authorization. Queue
                // all exact digest checks, including for colliding LODs.
                bool verified=true;
                for(unsigned i=0;i<sectionCount;++i){const auto& s=asset.sections[i];const auto& source=sources[i];
                    verified=verify(verifyContext,source.vb,At<unsigned>(source.geometry.data(),0x28),s.sourceVertices*s.stride,s.sourceVB)&&verified;
                    verified=verify(verifyContext,source.ib,At<unsigned>(source.geometry.data(),0x4c)*2,s.sourceIndices*2,s.sourceIB)&&verified;}
                if(verified){selected=&asset;break;}
            }
            if(!selected)return Reject(Gate::SourcePending);
            std::size_t bytes=0;for(const auto& section:selected->sections)bytes+=section.vb.size()+section.ib.size();
            if(bytes>64*1024*1024-state.publishedBytes)return Reject(Gate::CacheBytes,UINT32_MAX,state.publishedBytes+bytes,64*1024*1024);
            // Latch before allocation: bad_alloc and device failures must not
            // produce first-eye fallback followed by second-eye replacement.
            state.buildFailedThisFrame=true;
            auto fresh=std::make_unique<State::Entry>();fresh->device=device;fresh->sources=sources;fresh->asset=selected;
            if(!Build(*fresh,*selected))return Reject(Gate::Build);entry=fresh.get();state.entries.push_back(std::move(fresh));
            state.publishedBytes+=bytes;
            state.buildFailedThisFrame=false;
        }
        if(!entry->ready)return Reject(Gate::ActivationPending);
        const auto& asset=*entry->asset;
        for(unsigned b=0;b<4;++b){unsigned kept=0;
            for(unsigned n=0;n<bucketCounts[b];++n){const auto index=out.buckets[b][n];
                if(asset.sections[index].action)out.buckets[b][kept++]=index;}
            Put(out.container.data(),0x18+b*4,kept);Put(out.container.data(),0x28+b*4,Ptr(out.buckets[b].data()));}
        for(unsigned i=0;i<sectionCount;++i)if(asset.sections[i].action)
            Put(out.parts[i].data(),0x28,Ptr(entry->sections[i].geometry.data()));
        Put(out.container.data(),0x14,Ptr(out.parts.data()));Put(out.lod.data(),0x60,Ptr(out.container.data()));
        accepted.fetch_add(1,std::memory_order_relaxed);return true;
    }catch(...){return Reject(Gate::Exception);}
#else
    (void)lod;(void)variant;(void)paletteCount;(void)owner;(void)device;(void)verify;(void)verifyContext;(void)out;(void)graphicsThreadId;return false;
#endif
}
bool Replace(unsigned lod,unsigned variant,unsigned paletteCount,const NativeSkinOwner& owner,ID3D11Device* device,
    VerifySource verify,void* context,View& out,unsigned thread)noexcept {
    return ReplaceIn(Get(),lod,variant,paletteCount,owner,device,verify,context,out,thread);
}
bool ReplaceProp(unsigned pack,unsigned lod,unsigned variant,unsigned paletteCount,const NativeSkinOwner& owner,ID3D11Device* device,
    VerifySource verify,void* context,View& out,unsigned thread)noexcept {
    return pack<propCount&&ReplaceIn(Props()[pack],lod,variant,paletteCount,owner,device,verify,context,out,thread);
}
}
