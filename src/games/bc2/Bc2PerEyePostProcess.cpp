#include "Bc2PerEyePostProcess.h"
#include "Bc2PatternScan.h"
#include "NativeViewAbi.h"
#include <Windows.h>
#include <atomic>
namespace fvr::bc2::perEyePost {
namespace {
// Byte bodies (build 795745, DIS; see BC2_EYE_PARITY_POST_RE.md).
constexpr const char* factoryBody="56 68 B4 01 00 00 8B F1 E8 ?? ?? ?? ?? 83 C4 04 85 C0 74 1B 8B 4C 24 10 8B 54 24 0C 51 8B 4C 24 0C 52 51 56 8B C8 E8 ?? ?? ?? ?? 5E C2 0C 00 33 C0 5E C2 0C 00";
constexpr std::uint32_t factoryCtorCall=0x26;
constexpr const char* viewCtorHead="81 EC C4 02 00 00 8B 84 24 C8 02 00 00 53 55 56 8B F1 8B 8C 24 D8 02 00 00 C7 06 ?? ?? ?? ?? 33 DB 89 5E 04 89 46 08 C7 06 ?? ?? ?? ??";
constexpr std::uint32_t viewCtorVtable=41;
constexpr const char* viewDtorHead="53 8B D9 56 8B 73 08 C7 03 ?? ?? ?? ?? 8B 4E 20 8B 46 1C 3B C1 74 0B 39 18 74 07 83 C0 04 3B C1 75 F5";
constexpr const char* viewDeletingDtorBody="56 8B F1 E8 ?? ?? ?? ?? F6 44 24 08 01 74 09 56 E8 ?? ?? ?? ?? 83 C4 04 8B C6 5E C2 04 00";
constexpr const char* addRefBody="83 C1 04 B8 01 00 00 00 F0 0F C1 01 40 C3";
constexpr const char* postReleaseBody="56 8D 41 04 83 CE FF F0 0F C1 30 4E 75 0D 85 C9 74 09 8B 11 8B 42 38 6A 01 FF D0 8B C6 5E C3";
constexpr const char* viewReleaseBody="56 8D 41 04 83 CE FF F0 0F C1 30 4E 75 0D 85 C9 74 09 8B 11 8B 42 0C 6A 01 FF D0 8B C6 5E C3";
constexpr const char* updateBody="8B 41 1C 56 8B 71 20 3B C6 74 1D D9 44 24 08 8B 10 D9 82 B0 01 00 00 83 C0 04 3B C6 D8 C1 D9 9A B0 01 00 00 75 E9 DD D8 E8 ?? ?? ?? ?? 5E C2 04 00";
constexpr const char* processHead="55 8B EC 83 E4 F0 81 EC 54 06 00 00 53 8B 5D 08 56 8B F1 8B 0D ?? ?? ?? ?? 8B 01 8B 50 20 57 8B 3B";
// Draw 0xbb1db0: ECX = WR+0x7c (drawView+0x121d), stored to block[0]
// (drawView+0x1257), then PostProcess vf+0x18(&block) (drawView+0x1a94).
constexpr const char* drawSelectBody="8B 86 E8 00 00 00 85 C0 8B 4E 7C";constexpr std::uint32_t drawSelectOffset=0x121d;
constexpr const char* drawStoreBody="89 8C 24 C0 01 00 00";constexpr std::uint32_t drawStoreOffset=0x1257;
constexpr const char* drawProcessBody="8B 4E 70 8B 01 8B 40 18 8D 94 24 C0 01 00 00 52 FF D0";constexpr std::uint32_t drawProcessOffset=0x1a94;
// WorldUpdate 0xbb4980+0x108: PostProcess(WR+0x70) vf+0xc(dt).
constexpr const char* worldUpdateBody="8B 4E 70 85 C9 74 0D 8B 11 8B 42 0C 51 D9 1C 24 FF D0";constexpr std::uint32_t worldUpdateOffset=0x108;
// WorldRenderer ctor 0xba7c10+0x5fb: WR+0x7c = WR+0x70->vf8("Shared",WR+0xbc,WR+0xc0).
constexpr const char* worldCtorBody="8B 86 C0 00 00 00 8B 4E 70 8B 11 8B 52 08 83 C4 24 50 8B 86 BC 00 00 00 50 8D 44 24 3C 50 FF D2 8B 4E 7C 8B F8 3B F9 74 1D 3B CB 74 0A 8B 01 8B 50 04 FF D2 89 5E 7C 3B FB 89 7E 7C 74 08 8B 07 8B 10 8B CF FF D2";

// Native object offsets (DIS).
constexpr std::uint32_t worldPost=0x70,worldPostView=0x7c,worldWidth=0xbc,worldHeight=0xc0;
constexpr std::uint32_t viewOwner=0x08,viewWidth=0x18,viewHeight=0x1c;

using RefFn=unsigned(__thiscall*)(void*);
using FactoryFn=void*(__thiscall*)(void*,const NativeStringRange*,unsigned,unsigned);

struct Owned {std::uintptr_t world=0,post=0,view=0;};
struct Runtime {
    Candidates c{};std::uintptr_t image=0;std::uint32_t worldVtable=0;bool bound=false;
    Owned owned{},pending{};unsigned createFailures=0;
    std::atomic<std::uint64_t> created=0,released=0,deferred=0,swaps=0,failures=0;
    std::atomic<std::uint32_t> width=0,height=0;
} runtime;
// Published to the render thread. Dekker pair with `drawing`: a draw raises
// drawing before it loads the PPV; a retire clears the PPV before it reads
// drawing. With seq_cst one side always sees the other.
std::atomic<std::uintptr_t> activeWorld{0},activeView{0};
std::atomic<int> drawing{0};
std::atomic<int> enabledCache{-1};

bool Read(std::uintptr_t at,void* out,std::size_t n)noexcept{
    if(at<0x10000||n>0x1000||at>UINT32_MAX-n)return false;SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,n,&got)&&got==n;
}
std::uint32_t U32(std::uintptr_t at)noexcept{std::uint32_t v=0;Read(at,&v,4);return v;}
template<class T> T Fn(std::uint32_t rva)noexcept{return reinterpret_cast<T>(runtime.image+rva);}

// Main thread. Frees a retired PPV once no draw can be using it. The PPV dtor
// (0x9c1250) unregisters from its owner PostProcess, which our reference keeps alive.
void DrainPending()noexcept{
    auto& p=runtime.pending;if(!p.view)return;
    if(drawing.load(std::memory_order_seq_cst)!=0){runtime.deferred.fetch_add(1,std::memory_order_relaxed);return;}
    if(U32(p.view)==runtime.image+runtime.c.viewVtable&&U32(p.view+viewOwner)==p.post)Fn<RefFn>(runtime.c.viewRelease)(reinterpret_cast<void*>(p.view));
    else runtime.failures.fetch_add(1,std::memory_order_relaxed); // unrecognised: leak rather than touch
    if(U32(p.post)==runtime.image+runtime.c.postVtable)Fn<RefFn>(runtime.c.postRelease)(reinterpret_cast<void*>(p.post));
    p={};runtime.released.fetch_add(1,std::memory_order_relaxed);
}
}

std::optional<Candidates> Discover(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t drawView,std::uint32_t worldUpdate){
    const auto base=scan::Base(b,pe);if(!base||!drawView||!worldUpdate)return {};
    Candidates c{};
    const auto factory=scan::Find(b,pe,factoryBody);if(!factory)return {};c.factory=*factory;
    const auto ctor=scan::Rel32(b,pe,c.factory+factoryCtorCall,0xe8);if(!ctor||!scan::Match(b,pe,*ctor,viewCtorHead))return {};c.viewCtor=*ctor;
    const auto ctorAt=scan::At(b,pe,c.viewCtor+viewCtorVtable,4);if(!ctorAt)return {};const auto viewVa=scan::U(b,*ctorAt);if(viewVa<base)return {};c.viewVtable=viewVa-base;
    // PostProcess vtable: the unique data slot holding the factory is entry 2.
    const auto slot=scan::FindDword(b,pe,base+c.factory);if(!slot||*slot<8)return {};c.postVtable=*slot-8;
    const auto entry=[&](std::uint32_t table,unsigned i)->std::uint32_t{const auto at=scan::At(b,pe,table+i*4,4);if(!at)return 0;const auto va=scan::U(b,*at);return va<base?0:va-base;};
    c.postAddRef=entry(c.postVtable,0);c.postRelease=entry(c.postVtable,1);c.update=entry(c.postVtable,3);c.process=entry(c.postVtable,6);
    if(entry(c.postVtable,2)!=c.factory||!scan::Match(b,pe,c.postAddRef,addRefBody)||!scan::Match(b,pe,c.postRelease,postReleaseBody)||
       !scan::Match(b,pe,c.update,updateBody)||!scan::Match(b,pe,c.process,processHead))return {};
    c.viewAddRef=entry(c.viewVtable,0);c.viewRelease=entry(c.viewVtable,1);c.viewDeletingDtor=entry(c.viewVtable,3);
    if(!scan::Match(b,pe,c.viewAddRef,addRefBody)||!scan::Match(b,pe,c.viewRelease,viewReleaseBody)||!scan::Match(b,pe,c.viewDeletingDtor,viewDeletingDtorBody))return {};
    const auto dtor=scan::Rel32(b,pe,c.viewDeletingDtor+3,0xe8);if(!dtor||!scan::Match(b,pe,*dtor,viewDtorHead))return {};c.viewDtor=*dtor;
    const auto dtorAt=scan::At(b,pe,c.viewDtor+9,4);if(!dtorAt||scan::U(b,*dtorAt)!=viewVa)return {};
    // The only WR+0x7c consumer in the draw, and the per-frame dt feed.
    c.drawSelect=drawView+drawSelectOffset;c.drawProcess=drawView+drawProcessOffset;c.worldUpdateSite=worldUpdate+worldUpdateOffset;
    if(!scan::Match(b,pe,c.drawSelect,drawSelectBody)||scan::Find(b,pe,drawSelectBody)!=c.drawSelect||!scan::Match(b,pe,drawView+drawStoreOffset,drawStoreBody)||
       !scan::Match(b,pe,c.drawProcess,drawProcessBody)||scan::Find(b,pe,drawProcessBody)!=c.drawProcess||
       !scan::Match(b,pe,c.worldUpdateSite,worldUpdateBody))return {};
    const auto site=scan::Find(b,pe,worldCtorBody);if(!site)return {};c.worldCtorSite=*site;
    return c;
}

std::vector<std::pair<std::uint32_t,std::uint32_t>> LiveSpans(const Candidates& c){
    const auto n=[](const char* p)->std::uint32_t{const auto v=engine::ParsePattern(p);return v?std::uint32_t(v->size()):0u;};
    return {{c.factory,n(factoryBody)},{c.viewCtor,n(viewCtorHead)},{c.viewDtor,n(viewDtorHead)},{c.viewDeletingDtor,n(viewDeletingDtorBody)},
        {c.postAddRef,n(addRefBody)},{c.postRelease,n(postReleaseBody)},{c.viewAddRef,n(addRefBody)},{c.viewRelease,n(viewReleaseBody)},
        {c.update,n(updateBody)},{c.process,n(processHead)},{c.postVtable,7*4},{c.viewVtable,4*4},
        {c.drawSelect,n(drawSelectBody)},{c.drawProcess,n(drawProcessBody)},{c.worldUpdateSite,n(worldUpdateBody)}};
}

bool EnabledByEnvironment()noexcept{
    auto cached=enabledCache.load(std::memory_order_acquire);
    if(cached<0){char value[4]{};cached=(GetEnvironmentVariableA("FVR_BC2_PER_EYE_POST",value,sizeof(value))&&value[0]=='0')?0:1;enabledCache.store(cached,std::memory_order_release);}
    return cached==1;
}
void Bind(const Candidates& c,std::uintptr_t imageBase,std::uint32_t worldVtable)noexcept{
    runtime.c=c;runtime.image=imageBase;runtime.worldVtable=worldVtable;runtime.bound=imageBase&&worldVtable&&c.factory&&c.postVtable&&c.viewVtable;
}
bool Bound()noexcept{return runtime.bound;}

void Retire()noexcept{
    if(!runtime.bound)return;
    if(runtime.owned.view){
        activeView.store(0,std::memory_order_seq_cst);activeWorld.store(0,std::memory_order_seq_cst);
        runtime.pending=runtime.owned;runtime.owned={};
    }
    DrainPending();
}

void Update(std::uintptr_t world,bool eyeArmed)noexcept{
    if(!runtime.bound||!EnabledByEnvironment())return;
    DrainPending();
    const auto image=runtime.image;
    const auto post=std::uintptr_t(U32(world+worldPost));
    const bool worldValid=eyeArmed&&world&&U32(world)==image+runtime.worldVtable&&post&&U32(post)==image+runtime.c.postVtable;
    const auto width=U32(world+worldWidth),height=U32(world+worldHeight);
    auto& o=runtime.owned;
    // Another WorldRenderer's update says nothing about ours. Owner changes are
    // retired explicitly (RecoverStereoOwner) while the old world is alive.
    if(o.view&&world!=o.world)return;
    if(o.view){
        // Keep while the same world/PostProcess own it and the size still matches.
        if(worldValid&&o.world==world&&o.post==post&&U32(o.view)==image+runtime.c.viewVtable&&
           U32(o.view+viewWidth)==width&&U32(o.view+viewHeight)==height)return;
        Retire(); // pending is always empty while a PPV is owned (creation waits for it)
    }
    if(!worldValid||runtime.pending.view||runtime.createFailures>=3)return;
    // The native shared PPV must be in place (not mid-swap: no eye PPV is active).
    const auto shared=std::uintptr_t(U32(world+worldPostView));
    if(!shared||U32(shared)!=image+runtime.c.viewVtable||U32(shared+viewOwner)!=post)return;
    if(!width||!height||width>16384||height>16384)return;
    // Hold the PostProcess so the PPV can always be released into a live owner.
    if(Fn<RefFn>(runtime.c.postAddRef)(reinterpret_cast<void*>(post))<2){runtime.createFailures=3;runtime.failures.fetch_add(1);return;}
    static const char name[]="Fvr.EyePostProcessView";const NativeStringRange range{name,name+sizeof(name)-1,name+sizeof(name)};
    void* const fresh=Fn<FactoryFn>(runtime.c.factory)(reinterpret_cast<void*>(post),&range,width,height);
    const auto view=reinterpret_cast<std::uintptr_t>(fresh);
    if(!view||U32(view)!=image+runtime.c.viewVtable||U32(view+viewOwner)!=post){
        ++runtime.createFailures;runtime.failures.fetch_add(1,std::memory_order_relaxed);
        if(!view)Fn<RefFn>(runtime.c.postRelease)(reinterpret_cast<void*>(post)); // else leak both: unknown object
        return;
    }
    if(Fn<RefFn>(runtime.c.viewAddRef)(fresh)!=1){++runtime.createFailures;runtime.failures.fetch_add(1,std::memory_order_relaxed);return;}
    o={world,post,view};runtime.createFailures=0;
    runtime.width.store(width,std::memory_order_relaxed);runtime.height.store(height,std::memory_order_relaxed);runtime.created.fetch_add(1,std::memory_order_relaxed);
    activeWorld.store(world,std::memory_order_seq_cst);activeView.store(view,std::memory_order_seq_cst);
}

DrawScope::DrawScope(std::uintptr_t world,bool eyeDraw)noexcept{
    if(!eyeDraw||!runtime.bound)return;
    drawing.fetch_add(1,std::memory_order_seq_cst);entered_=true;
    const auto view=activeView.load(std::memory_order_seq_cst);
    if(!view||activeWorld.load(std::memory_order_seq_cst)!=world)return;
    auto* const slot=reinterpret_cast<volatile std::uint32_t*>(world+worldPostView);
    prior_=*slot;if(!prior_||prior_==view)return;
    *slot=std::uint32_t(view);world_=world;swapped_=std::uint32_t(view);
    runtime.swaps.fetch_add(1,std::memory_order_relaxed);
}
DrawScope::~DrawScope(){
    if(swapped_){auto* const slot=reinterpret_cast<volatile std::uint32_t*>(world_+worldPostView);if(*slot==swapped_)*slot=prior_;}
    if(entered_)drawing.fetch_sub(1,std::memory_order_seq_cst);
}

Status Snapshot()noexcept{
    return {EnabledByEnvironment(),runtime.bound,runtime.created.load(),runtime.released.load(),runtime.deferred.load(),runtime.swaps.load(),runtime.failures.load(),
        std::uint32_t(activeView.load()),runtime.width.load(),runtime.height.load()};
}
}
