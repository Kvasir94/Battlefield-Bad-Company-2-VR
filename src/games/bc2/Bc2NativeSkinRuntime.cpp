#include "Bc2NativeSkinRuntime.h"
#include "Bc2NativeSkinRequestRegistry.h"
#include "Bc2RigPublication.h"
#include "Bc2NativeSkinGeometry.h"
#include "Bc2Xm8BoltAsset.h"
#include "Bc2NativePropManifest.h"
#include "Bc2BodyAmmoHost.h"
#include <chrono>
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>

namespace fvr::bc2::nativeSkinRuntime {
void WriteSourceVerificationTelemetry(std::ostream& o){
    const auto s=ReadSourceVerificationCounters();
    o<<"{\"capacity\":"<<s.capacity<<",\"registered\":"<<s.registered<<",\"available_hits\":"<<s.availableHits
     <<",\"pending_hits\":"<<s.pendingHits<<",\"rejected_hits\":"<<s.rejectedHits<<",\"full\":"<<s.full
     <<",\"copy_starts\":"<<s.copyStarts<<",\"read_pending\":"<<s.readPending<<",\"read_busy\":"<<s.readBusy
     <<",\"matches\":"<<s.matches<<",\"hash_mismatches\":"<<s.hashMismatches
     <<",\"create_failures\":"<<s.createFailures<<",\"map_failures\":"<<s.mapFailures<<'}';
}
#if defined(_M_IX86)
namespace {
using Allocate=std::uintptr_t(__thiscall*)(void*,unsigned,unsigned,unsigned);
using Render=std::uintptr_t(__stdcall*)(unsigned,unsigned,unsigned,unsigned);
using Submit=std::uintptr_t(__cdecl*)(const void*,void*);
Allocate allocateOriginal=nullptr;Render renderOriginal=nullptr;Submit submitOriginal=nullptr;
std::array<void*,3> hooks{};unsigned imageBase=0;
std::atomic<bool> admitted=false,entryEnabled=false;
std::atomic<unsigned> inFlight=0;
std::atomic<std::uint64_t> allocations=0,ownedAllocations=0,renderCalls=0,ownedRenders=0,submissions=0,rejected=0;
std::atomic<std::uint64_t> replacements=0,sourceMatches=0,sourceMismatches=0;
using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> skinDevice;
bool assetConfigured=false;
std::vector<NativePropRecord> propRecords;
std::atomic<std::uint64_t> propSubmissions=0,propScopeMisses=0,propRejected=0;
thread_local PropEyeScope* propEye=nullptr;
std::int64_t PropNow(){return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
bool SameProp(const graphics::BodyPropInstance& a,const graphics::BodyPropInstance& b){
    return a.geometry==b.geometry&&a.actorGeneration==b.actorGeneration&&a.equipmentGeneration==b.equipmentGeneration&&
        a.spaceGeneration==b.spaceGeneration&&a.opacity==b.opacity&&
        !std::memcmp(&a.world,&b.world,sizeof a.world)&&a.tintRgba==b.tintRgba;
}
std::filesystem::path PropPath(){
    wchar_t path[32768]{};HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PropPath),&module))return {};
    const auto n=GetModuleFileNameW(module,path,std::size(path));
    return n&&n<std::size(path)?std::filesystem::path(path).parent_path()/L"native-skin-props":std::filesystem::path{};
}
std::vector<std::byte> ReadAsset(const std::filesystem::path& path,std::size_t limit){
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return {};
    const auto n=f.tellg();if(n<=0||std::uint64_t(n)>limit)return {};
    std::vector<std::byte> bytes(std::size_t(n),std::byte{});f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(bytes.data()),n))return {};return bytes;
}
bool LoadProps(){
    char disabled[4]{};if(GetEnvironmentVariableA("FVR_BC2_NATIVE_PROPS",disabled,4)==1&&disabled[0]=='0')return true;
    const auto root=PropPath();if(root.empty()||!std::filesystem::exists(root))return true;
    const auto manifest=ReadAsset(root/L"manifest.fvrnprop",65536);
    auto records=ParseNativePropManifest(manifest);if(!records)return false;
    std::vector<std::vector<std::byte>> storage;std::vector<nativeSkinGeometry::PropPack> packs;std::size_t total=0;
    storage.reserve(records->size());packs.reserve(records->size());
    for(const auto& r:*records){auto bytes=ReadAsset(root/r.filename,8*1024*1024);
        if(bytes.empty()||bytes.size()>64*1024*1024-total||!NativePropDigest(bytes,r.digest))return false;
        total+=bytes.size();storage.push_back(std::move(bytes));packs.push_back({storage.back(),r.rig,r.bone});}
    if(!nativeSkinGeometry::ConfigureProps(packs))return false;propRecords=std::move(*records);return true;
}
unsigned assetFileCount=0;
std::filesystem::path AssetPath(){
    wchar_t value[32768]{};const auto length=GetEnvironmentVariableW(L"FVR_BC2_NATIVE_SKIN_ASSET",value,std::size(value));
    if(length){if(length>=std::size(value))return {};return value;}
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&AssetPath),&module))return {};
    const auto n=GetModuleFileNameW(module,value,std::size(value));if(!n||n>=std::size(value))return {};
    const auto directory=std::filesystem::path(value).parent_path()/L"native-skin-hands";
    if(std::filesystem::is_directory(directory))return directory;
    return std::filesystem::path(value).parent_path()/L"native-skin-hands.fvrskin";
}
std::atomic<DWORD> graphicsOwner=0;
struct SourceCheck {
    ComPtr<ID3D11Buffer> source,staging;
    unsigned offset=0,count=0;std::array<std::byte,32> expected{};
    // 0 queued, 1 copy pending, 2 exact immutable bytes, 3 mismatch/failure.
    unsigned state=0;bool available=false;
};
std::mutex sourceMutex;std::array<SourceCheck,512> sourceChecks{};unsigned sourceCount=0;
std::atomic<std::uint64_t> sourceRegistered=0,sourceAvailableHits=0,sourcePendingHits=0,sourceRejectedHits=0,sourceFull=0;
std::atomic<std::uint64_t> sourceCopyStarts=0,sourceReadPending=0,sourceReadBusy=0,sourceHashMismatches=0;
std::atomic<std::uint64_t> sourceCreateFailures=0,sourceMapFailures=0;
std::uint64_t frameKey=0;
bool VerifySource(void*,ID3D11Buffer* buffer,unsigned offset,unsigned count,const std::array<std::byte,32>& expected){
    if(!buffer||!count||count>8*1024*1024||!skinDevice)return false;
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);ComPtr<ID3D11Device> device;buffer->GetDevice(&device);
    // Caching a mutable buffer's digest would incorrectly authorize later bytes.
    if(device.Get()!=skinDevice.Get()||desc.Usage!=D3D11_USAGE_IMMUTABLE||offset>desc.ByteWidth||count>desc.ByteWidth-offset)return false;
    std::lock_guard lock(sourceMutex);
    for(unsigned i=0;i<sourceCount;++i){const auto& check=sourceChecks[i];
        if(check.source.Get()==buffer&&check.offset==offset&&check.count==count&&check.expected==expected){
            (check.available?sourceAvailableHits:check.state==3?sourceRejectedHits:sourcePendingHits).fetch_add(1,std::memory_order_relaxed);
            return check.available;}}
    if(sourceCount==sourceChecks.size()){sourceFull.fetch_add(1,std::memory_order_relaxed);return false;}
    auto& check=sourceChecks[sourceCount++];check.source=buffer;check.offset=offset;check.count=count;check.expected=expected;
    sourceRegistered.fetch_add(1,std::memory_order_relaxed);
    return false;
}
Bc2NativeSkinRequestRegistry registry;
struct Callback {Callback(){++inFlight;}~Callback(){--inFlight;}};
bool Read(std::uint64_t at,void* dst,std::size_t n)noexcept {
    SIZE_T got=0;return at>=0x10000&&n&&n<=16384&&at+n<=UINT32_MAX&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(std::uintptr_t(at)),dst,n,&got)&&got==n;
}
template<class T>bool Read(std::uint64_t at,T& out)noexcept{return Read(at,&out,sizeof out);}
struct Descriptor {
    unsigned state=0;float distanceSquared=0;std::uint8_t flags=0,variant=0;std::uint16_t mode=0;
    unsigned lod=0,palette=0,paletteCount=0;
};
static_assert(sizeof(Descriptor)==24);
struct Sample {
    unsigned request=0,caller=0,context=0,lod=0,variant=0,mode=0,palette=0,paletteCount=0;
    std::uint64_t actor=0,ownerGeneration=0,token=0;
    std::array<unsigned,4> pass{};
    unsigned bucket=0,section=0,binding=0,geometry=0,boneCount=0;
    std::array<unsigned,20> geometryWords{};
    std::array<std::uint16_t,64> remap{};
};
std::mutex samplesMutex;std::array<Sample,256> samples{};unsigned sampleCount=0;
struct Scope {
    unsigned request=0,passAddress=0;
    std::array<unsigned,4> pass{}; // Observed numeric type/view/mask/context.
    std::optional<NativeSkinRequestRecord> record;
};
thread_local Scope* current=nullptr;
struct ScopeGuard {Scope* previous;explicit ScopeGuard(Scope* s):previous(current){current=s;}~ScopeGuard(){current=previous;}};

std::uintptr_t __fastcall AllocateHook(void* collector,void*,unsigned row,unsigned size,unsigned flags){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    std::optional<NativeSkinOwner> owner;std::array<unsigned,8> before{};
    const bool character=caller==imageBase+0x1f29fd||caller==imageBase+0x1f3574;
    if(admitted.load(std::memory_order_acquire)&&character&&size==0xd0){
        owner=rigPublication::ReadNativeSkinOwner();
        if(!owner||!Read(row,before)||before[0]!=owner->actor)owner.reset();
    }
    const auto result=allocateOriginal(collector,row,size,flags);
    ++allocations;
    if(owner){std::array<unsigned,8> after{};const auto now=rigPublication::ReadNativeSkinOwner();
        if(!admitted.load(std::memory_order_acquire)||now!=owner||!Read(row,after)||before!=after)owner.reset();}
    // All allocations invalidate overlapping records, including non-character
    // requests. Never insert after a separate invalidation: that races reuse.
    if(result){const auto token=registry.RecordAllocation(unsigned(result),size,owner);if(token)++ownedAllocations;}
    return result;
}
std::uintptr_t __stdcall RenderHook(unsigned request,unsigned state,unsigned container,unsigned pass){
    Callback callback;++renderCalls;Scope scope{};ScopeGuard guard(&scope);
    if(admitted.load(std::memory_order_acquire))if(const auto owner=rigPublication::ReadNativeSkinOwner()){
        scope.record=registry.Read(request,*owner);
        std::uint8_t firstPerson=0,againFirstPerson=0;std::array<unsigned,4> againPass{};
        bool exact=scope.record&&Read(std::uint64_t(request)+0xcc,firstPerson)&&firstPerson==1&&
            Read(pass,scope.pass)&&scope.pass[3]>=0x10000&&Read(pass,againPass)&&scope.pass==againPass&&
            Read(std::uint64_t(request)+0xcc,againFirstPerson)&&firstPerson==againFirstPerson;
        const auto finalOwner=exact?rigPublication::ReadNativeSkinOwner():std::nullopt;
        exact=exact&&admitted.load(std::memory_order_acquire)&&finalOwner==owner&&registry.Matches(*scope.record,*finalOwner);
        if(exact){scope.request=request;scope.passAddress=pass;++ownedRenders;}
        else scope.record.reset();
    }
    return renderOriginal(request,state,container,pass);
}
void Observe(const Descriptor& d,unsigned caller,unsigned context){
    // Diagnostic sampling is bounded independently of per-draw replacement
    // validation. Request tokens and palette addresses change every frame.
    using Key=std::array<std::uint64_t,6>;
    static std::array<std::pair<Key,unsigned>,64> observed{};static unsigned observedCount=0;
    const Key key{d.lod,d.variant,d.mode,context,current->record->owner.actor,current->record->owner.ownerGeneration};
    {std::lock_guard lock(samplesMutex);unsigned index=0;
        for(;index<observedCount;++index)if(observed[index].first==key)break;
        if(index==observedCount){if(observedCount==observed.size())return;observed[observedCount++].first=key;}
        if(observed[index].second>=2)return;++observed[index].second;
    }
    unsigned partContainer=0,parts=0,variants=0,bindings=0;
    if(!Read(std::uint64_t(d.lod)+0x60,partContainer)||!Read(std::uint64_t(partContainer)+0x14,parts)||
       !Read(std::uint64_t(d.lod)+0x50,variants)||!Read(std::uint64_t(variants)+4+std::uint64_t(d.variant)*0x14,bindings))return;
    for(unsigned bucket=0;bucket<4;++bucket){unsigned count=0,indices=0;
        if(!Read(std::uint64_t(partContainer)+0x18+bucket*4,count)||!Read(std::uint64_t(partContainer)+0x28+bucket*4,indices)||count>128)return;
        for(unsigned n=0;n<count;++n){std::uint8_t index=0;
            if(!Read(std::uint64_t(indices)+n,index)||index>=128)return;
            const auto part=std::uint64_t(parts)+index*0x2c;
            Sample s{};s.request=current->request;s.caller=caller;s.context=context;s.lod=d.lod;s.variant=d.variant;s.mode=d.mode;
            s.actor=current->record->owner.actor;s.ownerGeneration=current->record->owner.ownerGeneration;s.token=current->record->token;s.pass=current->pass;
            s.palette=d.palette;s.paletteCount=d.paletteCount;s.bucket=bucket;s.section=index;
            unsigned remap=0;std::uint8_t boneCount=0;
            if(!Read(std::uint64_t(bindings)+index*4,s.binding)||!s.binding||!Read(part+0x28,s.geometry)||
               !Read(s.geometry,s.geometryWords)||!Read(part+0x13,boneCount)||boneCount>64||
               !Read(part+0x14,remap)||(boneCount&&!Read(remap,s.remap.data(),boneCount*2)))continue;
            s.boneCount=boneCount;
            const auto owner=rigPublication::ReadNativeSkinOwner();
            if(!owner||!current->record||!registry.Matches(*current->record,*owner))return;
            std::lock_guard lock(samplesMutex);bool duplicate=false;
            for(unsigned i=0;i<sampleCount;++i)if(samples[i].lod==s.lod&&samples[i].variant==s.variant&&samples[i].mode==s.mode&&samples[i].context==s.context&&
                samples[i].bucket==s.bucket&&samples[i].section==s.section){duplicate=true;break;}
            if(!duplicate&&sampleCount<samples.size())samples[sampleCount++]=s;
        }
    }
}
// 5d7910/5d7960 are the two original selected-weapon submissions inside
// the already owner-checked first-person character request. +c0 is the weapon
// variant; +8c/+90 and +94/+98 are its ordinary/alternate LOD arrays.
void SubmitProps(const Descriptor& d,void* context,const NativeSkinOwner& owner){
    if(propRecords.empty())return;
    if(!propEye){++propScopeMisses;return;}
    const auto now=PropNow();
    if(propEye->key.eye>=2||!propEye->key.world||!propEye->key.request||!propEye->key.view||d.paletteCount!=147)return;
    for(unsigned n=0;n<propEye->props.count;++n){const auto& instance=propEye->props.instances[n];
        if(instance.tintRgba||instance.opacity||instance.actorGeneration!=owner.ownerGeneration||instance.spaceGeneration!=owner.space||
           !graphics::BodyPropFresh(instance,now))continue;
        for(unsigned p=0;p<propRecords.size();++p){const auto& record=propRecords[p];
            if(owner.equipment.Asset()!=record.asset||instance.geometry!=MakeBodyAmmoGeometryKey(record.asset,record.mesh,record.part,record.rig))continue;
            const auto matrix=NativePropPackedTransform(record.inverseBind,instance.world);if(!matrix)continue;
            nativeSkinGeometry::View view;
            if(!nativeSkinGeometry::ReplaceProp(p,d.lod,d.variant,d.paletteCount,owner,skinDevice.Get(),VerifySource,nullptr,view,graphicsOwner.load(std::memory_order_acquire)))continue;
            // Only acknowledge buckets whose actual native bindings are nonzero.
            unsigned mask=0,variants=0,bindings=0;bool valid=Read(std::uint64_t(d.lod)+0x50,variants)&&
                Read(std::uint64_t(variants)+d.variant*0x14+4,bindings);
            for(unsigned b=0;b<4&&valid;++b)if(d.mode&(1u<<b)){
                unsigned count=0;std::memcpy(&count,view.container.data()+0x18+b*4,4);
                for(unsigned k=0;k<count;++k){unsigned binding=0;
                    if(!Read(std::uint64_t(bindings)+view.buckets[b][k]*4,binding)||!binding){valid=false;break;}}
                if(count)mask|=b==3?2u:1u;
            }
            if(!valid||!mask||(propEye->submitted[n]&mask)==mask||(mask==2&&!(propEye->submitted[n]&1)))continue;
            alignas(16) std::array<std::array<float,12>,147> palette{};
            if(!Read(d.palette,palette)||!admitted.load(std::memory_order_acquire)||
               rigPublication::ReadNativeSkinOwner()!=std::optional{owner}||!registry.Matches(*current->record,owner)||
               !graphics::BodyPropFresh(instance,PropNow())){++propRejected;continue;}
            palette[record.bone]=*matrix;
            auto extra=d;extra.lod=view.Lod();extra.palette=reinterpret_cast<unsigned>(palette.data());
            // 970870 copies each remapped48B matrix into its command arena now.
            // View/palette cannot escape; immutable geometry wrappers do persist.
            submitOriginal(&extra,context);propEye->submitted[n]|=mask;++propSubmissions;
        }
    }
}
bool WeaponAssociated(const void* descriptor,void* context,unsigned caller,Descriptor& d,NativeSkinOwner& owner){
    const auto live=rigPublication::ReadNativeSkinOwner();std::array<unsigned,0xd0/4> request{},again{};
    if(!live||!current||!current->record||reinterpret_cast<unsigned>(context)!=current->pass[3]||
       !registry.Matches(*current->record,*live)||!Read(reinterpret_cast<unsigned>(descriptor),d)||!Read(current->request,request)||
       (request[0xcc/4]&255)!=1||((request[0xcc/4]>>16)&255)!=0||d.paletteCount!=147||d.paletteCount!=request[0x88/4]||
       d.palette<0x10000||(d.palette&15)||d.variant!=(request[0xc0/4]&255))return false;
    // Native 5d7910 returns into the +84/+94/+98 alternate draw loop;
    // 5d7960 returns into the ordinary +80/+8c/+90 loop.
    const bool alternate=caller==imageBase+0x1d7915;
    if(d.palette!=request[(alternate?0x84:0x80)/4])return false;
    const auto count=request[(alternate?0x98:0x90)/4],address=request[(alternate?0x94:0x8c)/4];
    std::array<unsigned,128> lods{},againLods{};std::array<unsigned,4> pass{};Descriptor final{};
    if(!count||count>lods.size()||!Read(address,lods.data(),count*4)||
       std::find(lods.begin(),lods.begin()+count,d.lod)==lods.begin()+count||
       !Read(address,againLods.data(),count*4)||lods!=againLods||!Read(current->request,again)||again!=request||
       !Read(current->passAddress,pass)||pass!=current->pass||!Read(reinterpret_cast<unsigned>(descriptor),final)||
       std::memcmp(&d,&final,sizeof d)||rigPublication::ReadNativeSkinOwner()!=live||!registry.Matches(*current->record,*live))return false;
    owner=*live;return true;
}
std::uintptr_t __cdecl SubmitHook(const void* descriptor,void* context){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    const bool character=caller==imageBase+0x1d7808||caller==imageBase+0x1d7868;
    if(admitted.load(std::memory_order_acquire)&&current&&current->record&&
       (caller==imageBase+0x1d7915||caller==imageBase+0x1d7965)){
        Descriptor d{};NativeSkinOwner owner{};
        if(WeaponAssociated(descriptor,context,caller,d,owner))SubmitProps(d,context,owner);else ++propRejected;
    }
    if(admitted.load(std::memory_order_acquire)&&character&&current&&current->record){
        const auto owner=rigPublication::ReadNativeSkinOwner();Descriptor d{};
        std::array<unsigned,0xd0/4> request{},again{};
        std::array<unsigned,128> lods{},metadata{},lodsAgain{},metadataAgain{};
        Descriptor finalDescriptor{};
        bool associated=owner&&current->request==current->record->request&&
            reinterpret_cast<unsigned>(context)==current->pass[3]&&registry.Matches(*current->record,*owner)&&
            Read(reinterpret_cast<unsigned>(descriptor),d)&&Read(current->request,request)&&
            (request[0xcc/4]&0xff)==1&&d.lod>=0x10000&&d.palette>=0x10000&&!(d.palette&15)&&
            d.paletteCount>0&&d.paletteCount<=1024&&d.paletteCount==request[0x88/4]&&
            (d.palette==request[0x80/4]||d.palette==request[0x84/4]);
        const auto count=request[0xa0/4];
        associated=associated&&count>0&&count<=lods.size()&&request[0xa8/4]==count&&
            Read(request[0x9c/4],lods.data(),count*4)&&Read(request[0xa4/4],metadata.data(),count*4);
        if(associated){bool selected=false;
            for(unsigned n=0;n<count;++n)if(lods[n]==d.lod&&metadata[n]==d.variant){selected=true;break;}
            // Request arrays may be aliased by companion requests. Keep their
            // exact native references, and require a coherent repeated read.
            std::array<unsigned,4> passAgain{};
            associated=selected&&Read(current->passAddress,passAgain)&&current->pass==passAgain&&
                Read(current->request,again)&&request==again&&
                Read(request[0x9c/4],lodsAgain.data(),count*4)&&lods==lodsAgain&&
                Read(request[0xa4/4],metadataAgain.data(),count*4)&&metadata==metadataAgain&&
                Read(reinterpret_cast<unsigned>(descriptor),finalDescriptor)&&!std::memcmp(&d,&finalDescriptor,sizeof d);
        }
        const auto finalOwner=associated?rigPublication::ReadNativeSkinOwner():std::nullopt;
        if(associated&&admitted.load(std::memory_order_acquire)&&finalOwner==owner&&
           registry.Matches(*current->record,*finalOwner)){
            ++submissions;Observe(d,caller,reinterpret_cast<unsigned>(context));
            if(assetConfigured){nativeSkinGeometry::View replacement;
                if(nativeSkinGeometry::Replace(d.lod,d.variant,d.paletteCount,*owner,skinDevice.Get(),VerifySource,nullptr,replacement,graphicsOwner.load(std::memory_order_acquire))&&
                   admitted.load(std::memory_order_acquire)&&rigPublication::ReadNativeSkinOwner()==owner&&registry.Matches(*current->record,*owner)){
                    auto replaced=d;replaced.lod=replacement.Lod();++replacements;
                    return submitOriginal(&replaced,context);
                }
            }
        }else ++rejected;
    }
    return submitOriginal(descriptor,context);
}
std::uint64_t Hash(std::span<const std::byte> data){std::uint64_t h=14695981039346656037ull;
    for(auto c:data){h^=std::to_integer<unsigned char>(c);h*=1099511628211ull;}return h;}
bool Validate(std::span<const std::byte> bytes,const engine::PeImage& pe,unsigned base,unsigned rva,unsigned size,std::uint64_t hash){
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&rva>=s.rva){
        const auto delta=std::uint64_t(rva)-s.rva,offset=std::uint64_t(s.rawOffset)+delta;
        if(delta>s.rawSize||size>s.rawSize-delta||offset>bytes.size()||size>bytes.size()-offset)continue;
        const auto file=bytes.subspan(std::size_t(offset),size);std::array<std::byte,4096> live{};
        return size<=live.size()&&Hash(file)==hash&&Read(std::uint64_t(base)+rva,live.data(),size)&&!std::memcmp(file.data(),live.data(),size);
    }return false;
}
}
PropEyeScope::PropEyeScope(BodyAmmoEyeKey k,const graphics::BodyPropEye& p)noexcept:key(k),props(p),previous(propEye){
    if(props.count>props.instances.size())props.count=0;propEye=this;
}
PropEyeScope::~PropEyeScope(){propEye=previous;}
bool PropEyeScope::Rendered(const graphics::BodyPropInstance& instance,std::int64_t now)const noexcept {
    if(!graphics::BodyPropFresh(instance,now))return false;
    for(unsigned n=0;n<props.count;++n)if(submitted[n]==3&&graphics::BodyPropFresh(props.instances[n],now)&&SameProp(props.instances[n],instance))return true;return false;
}
bool Requested()noexcept {
    char value[16]{};const auto n=GetEnvironmentVariableA("FVR_BC2_NATIVE_SKIN",value,sizeof value);
    if(n==1&&value[0]=='0')return false;
    if(n==7&&!std::memcmp(value,"observe",7))return true;
    if(n==1&&value[0]=='1')return true;
    if(GetEnvironmentVariableW(L"FVR_BC2_NATIVE_SKIN_ASSET",nullptr,0))return true;
    try{const auto path=AssetPath();return !path.empty()&&(std::filesystem::is_regular_file(path)||std::filesystem::is_directory(path));}catch(...){return false;}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,unsigned base,ID3D11Device* device){
    if(hooks[0]||base!=0x400000||pe.machine!=0x14c||!device)return false;
    if(!Validate(bytes,pe,base,0x7aa970,0x19c,0xc4fa934cd7f873f8ull)||
       !Validate(bytes,pe,base,0x1d73d0,0x643,0xd2df096c69ef96c8ull)||
       !Validate(bytes,pe,base,0x570870,0x2b3,0xd3cee4a36fac766eull)||
       !Validate(bytes,pe,base,0x1f2860,0xe09,0x66f00c79c72351bcull))return false;
    imageBase=base;
    skinDevice=device;
    char mode[16]{};const auto modeLength=GetEnvironmentVariableA("FVR_BC2_NATIVE_SKIN",mode,sizeof mode);
    const bool observeOnly=modeLength==7&&!std::memcmp(mode,"observe",7);
    if(!observeOnly){
        const auto root=AssetPath();std::vector<std::filesystem::path> paths;
        if(std::filesystem::is_directory(root)){
            for(const auto& item:std::filesystem::directory_iterator(root)){
                if(!item.is_regular_file()||item.path().extension()!=L".fvrskin")continue;
                if(paths.size()==32)return false;paths.push_back(item.path());
            }
            std::sort(paths.begin(),paths.end());
        }else paths.push_back(root);
        if(paths.empty())return false;
        std::vector<std::vector<std::byte>> storage;storage.reserve(paths.size());std::size_t total=0;
        for(const auto& path:paths){
            std::ifstream asset(path,std::ios::binary|std::ios::ate);
            if(!asset)return false;const auto size=asset.tellg();if(size<=0||size>8*1024*1024)return false;
            total+=std::size_t(size);if(total>64*1024*1024)return false;
            auto& data=storage.emplace_back(std::size_t(size),std::byte{});asset.seekg(0);
            if(!asset.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size())))return false;
        }
        std::vector<std::span<const std::byte>> packs;packs.reserve(storage.size());
        for(const auto& data:storage)packs.emplace_back(data);
        assetConfigured=nativeSkinGeometry::ConfigureSet(packs,0xa7f219a1426216abull);
        if(assetConfigured)assetFileCount=unsigned(packs.size());
        if(!assetConfigured)return false;
    }
    if(!observeOnly&&!LoadProps())++propRejected; // Malformed optional props cannot disable valid hands.
    const std::array<unsigned,3> rvas{0x7aa970,0x1d73d0,0x570870};
    const std::array<void*,3> callbacks{reinterpret_cast<void*>(AllocateHook),reinterpret_cast<void*>(RenderHook),reinterpret_cast<void*>(SubmitHook)};
    const std::array<void**,3> originals{reinterpret_cast<void**>(&allocateOriginal),reinterpret_cast<void**>(&renderOriginal),reinterpret_cast<void**>(&submitOriginal)};
    for(unsigned i=0;i<3;++i){auto* address=reinterpret_cast<void*>(base+rvas[i]);
        if(MH_CreateHook(address,callbacks[i],originals[i])!=MH_OK){Disable();return false;}hooks[i]=address;}
    return true;
}
void Started(bool success)noexcept {success=success&&hooks[0]&&hooks[1]&&hooks[2];entryEnabled.store(success,std::memory_order_release);admitted.store(success,std::memory_order_release);if(!success)Disable();}
void BeginFrame(unsigned world,unsigned nativeFrame)noexcept {
    if(!admitted.load(std::memory_order_acquire)||!world)return;
    {std::lock_guard lock(sourceMutex);const auto key=(std::uint64_t(world)<<32)|nativeFrame;
        if(key==frameKey)return;frameKey=key;
        // Readiness changes once before the frame's worker/draw dispatch.
        for(unsigned i=0;i<sourceCount;++i)sourceChecks[i].available=sourceChecks[i].state==2;}
    nativeSkinGeometry::BeginFrame();
}
void Graphics(ID3D11DeviceContext* context)noexcept {
    Callback callback;if(!admitted.load(std::memory_order_acquire)||(!assetConfigured&&propRecords.empty())||!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
    ComPtr<ID3D11Device> device;context->GetDevice(&device);if(device.Get()!=skinDevice.Get())return;
    DWORD zero=0;const auto thread=GetCurrentThreadId();
    if(!graphicsOwner.compare_exchange_strong(zero,thread)&&zero!=thread){admitted.store(false,std::memory_order_release);return;}
    try{std::lock_guard lock(sourceMutex);unsigned budget=2;
        for(unsigned i=0;i<sourceCount;++i){auto& check=sourceChecks[i];
            if(check.state==1){D3D11_MAPPED_SUBRESOURCE mapped{};
                const auto hr=context->Map(check.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
                if(hr==DXGI_ERROR_WAS_STILL_DRAWING){sourceReadBusy.fetch_add(1,std::memory_order_relaxed);continue;}
                if(FAILED(hr)){check.state=3;check.staging.Reset();++sourceMismatches;
                    sourceMapFailures.fetch_add(1,std::memory_order_relaxed);sourceReadPending.fetch_sub(1,std::memory_order_relaxed);continue;}
                std::string hash;try{hash=Xm8BoltSha256Hex({static_cast<const std::byte*>(mapped.pData),check.count});}
                catch(...){context->Unmap(check.staging.Get(),0);throw;}
                context->Unmap(check.staging.Get(),0);check.staging.Reset();
                static constexpr char hex[]="0123456789abcdef";std::string expected;expected.reserve(64);
                for(auto byte:check.expected){const auto n=std::to_integer<unsigned>(byte);expected+=hex[n>>4];expected+=hex[n&15];}
                check.state=hash==expected?2:3;if(check.state==2)++sourceMatches;
                else {++sourceMismatches;sourceHashMismatches.fetch_add(1,std::memory_order_relaxed);}
                sourceReadPending.fetch_sub(1,std::memory_order_relaxed);
            }else if(check.state==0&&budget){--budget;D3D11_BUFFER_DESC desc{};desc.ByteWidth=check.count;
                desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                if(FAILED(device->CreateBuffer(&desc,nullptr,&check.staging))){check.state=3;++sourceMismatches;
                    sourceCreateFailures.fetch_add(1,std::memory_order_relaxed);continue;}
                const D3D11_BOX box{check.offset,0,0,check.offset+check.count,1,1};
                context->CopySubresourceRegion(check.staging.Get(),0,0,0,0,check.source.Get(),0,&box);check.state=1;
                sourceCopyStarts.fetch_add(1,std::memory_order_relaxed);sourceReadPending.fetch_add(1,std::memory_order_relaxed);
            }
        }
    }catch(...){admitted.store(false,std::memory_order_release);}
}
void Disable()noexcept {admitted.store(false,std::memory_order_release);bool disabled=true;
    for(auto* hook:hooks)if(hook){const auto r=MH_DisableHook(hook);disabled&=r==MH_OK||r==MH_ERROR_DISABLED;}
    entryEnabled.store(!disabled,std::memory_order_release);registry.Clear();}
bool Quiescent()noexcept{return !entryEnabled.load(std::memory_order_acquire)&&!inFlight.load(std::memory_order_acquire);}
SourceVerificationCounters ReadSourceVerificationCounters()noexcept {
    return {sourceRegistered.load(std::memory_order_relaxed),sourceAvailableHits.load(std::memory_order_relaxed),
        sourcePendingHits.load(std::memory_order_relaxed),sourceRejectedHits.load(std::memory_order_relaxed),sourceFull.load(std::memory_order_relaxed),
        sourceCopyStarts.load(std::memory_order_relaxed),sourceReadPending.load(std::memory_order_relaxed),sourceReadBusy.load(std::memory_order_relaxed),
        sourceMatches.load(std::memory_order_relaxed),sourceHashMismatches.load(std::memory_order_relaxed),
        sourceCreateFailures.load(std::memory_order_relaxed),sourceMapFailures.load(std::memory_order_relaxed),unsigned(sourceChecks.size())};
}
void Report(std::ostream& o){std::lock_guard lock(samplesMutex);
    o<<"{\"enabled\":"<<(admitted.load()?"true":"false")<<",\"allocations\":"<<allocations.load()<<",\"owned_allocations\":"<<ownedAllocations.load()
     <<",\"render_calls\":"<<renderCalls.load()<<",\"owned_renders\":"<<ownedRenders.load()<<",\"submissions\":"<<submissions.load()<<",\"rejected\":"<<rejected.load()
     <<",\"asset_files\":"<<assetFileCount<<",\"asset_configured\":"<<(assetConfigured?"true":"false")<<",\"replacements\":"<<replacements.load()
     <<",\"prop_assets\":"<<propRecords.size()<<",\"prop_submissions\":"<<propSubmissions.load()<<",\"prop_scope_misses\":"<<propScopeMisses.load()<<",\"prop_rejected\":"<<propRejected.load()
     <<",\"source_matches\":"<<sourceMatches.load()<<",\"source_mismatches\":"<<sourceMismatches.load()<<",\"source_verification\":";
    WriteSourceVerificationTelemetry(o);o<<",\"samples\":[";
    for(unsigned i=0;i<sampleCount;++i){if(i)o<<',';const auto& s=samples[i];
        o<<"{\"request\":"<<s.request<<",\"caller\":"<<s.caller<<",\"context\":"<<s.context<<",\"lod\":"<<s.lod<<",\"variant\":"<<s.variant
         <<",\"actor\":"<<s.actor<<",\"owner_generation\":"<<s.ownerGeneration<<",\"allocation_token\":"<<s.token
         <<",\"pass_type\":"<<s.pass[0]<<",\"pass_view_candidate\":"<<s.pass[1]<<",\"pass_mask_index\":"<<s.pass[2]
         <<",\"mode\":"<<s.mode<<",\"palette\":"<<s.palette<<",\"palette_count\":"<<s.paletteCount<<",\"bucket\":"<<s.bucket<<",\"section\":"<<s.section
         <<",\"binding\":"<<s.binding<<",\"geometry\":"<<s.geometry<<",\"geometry_words\":[";
        for(unsigned k=0;k<s.geometryWords.size();++k){if(k)o<<',';o<<s.geometryWords[k];}o<<"],\"remap\":[";
        for(unsigned k=0;k<s.boneCount;++k){if(k)o<<',';o<<s.remap[k];}o<<"]}";
    }o<<"],\"geometry\":";nativeSkinGeometry::Report(o);o<<"}";
}
#else
PropEyeScope::PropEyeScope(BodyAmmoEyeKey k,const graphics::BodyPropEye& p)noexcept:key(k),props(p){}
PropEyeScope::~PropEyeScope(){}
bool PropEyeScope::Rendered(const graphics::BodyPropInstance&,std::int64_t)const noexcept{return false;}
bool Requested()noexcept{return false;}
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uint32_t,ID3D11Device*){return false;}
void Graphics(ID3D11DeviceContext*)noexcept{}
void BeginFrame(std::uint32_t,std::uint32_t)noexcept{}
void Started(bool)noexcept{}void Disable()noexcept{}bool Quiescent()noexcept{return true;}
SourceVerificationCounters ReadSourceVerificationCounters()noexcept{return {};}
void Report(std::ostream& o){o<<"{\"unsupported_architecture\":true}";}
#endif
}
