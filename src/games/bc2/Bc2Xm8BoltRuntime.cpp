#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "Bc2Xm8BoltRuntime.h"
#include "fvr/graphics/D3D11RigidPropRenderer.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>

namespace fvr::bc2::xm8BoltRuntime {
namespace {
using Microsoft::WRL::ComPtr;
struct State {
    std::atomic<bool> enabled=false,stopping=false;
    bool switchOn=true;
    Xm8BoltAssetResult load{};
    std::shared_ptr<const Xm8BoltAsset> asset;
    std::atomic<std::shared_ptr<const RenderSource>> source;
    Xm8BoltDrawHealth health;
    // Graphics thread only.
    graphics::D3D11RigidPropRenderer* renderer=nullptr; // Destroyed on its thread (PumpStop) or intentionally leaked.
    ID3D11DeviceContext* context=nullptr;DWORD thread=0;bool uploaded=false,initFailed=false;
    std::atomic<std::uint64_t> drawn{0},failed{0},stale{0},noTarget{0},wrongThread{0},backend{0},skippedNoSource{0};
    std::array<std::atomic<std::uint64_t>,unsigned(Xm8BoltCardReason::Count)> card{};
    std::atomic<float> lastTravel{0};
    // Gameplay thread.
    std::mutex cycleMutex;std::optional<Bc2Xm8BoltCycle> cycle;
    std::atomic<std::uint64_t> fireBlocked{0},completions{0};
};
State& S()noexcept {static State* state=new State();return *state;} // Never destroyed: no COM release at unload.
std::optional<std::string> Environment(const char* name){
    char value[1024]{};const auto n=GetEnvironmentVariableA(name,value,sizeof(value));
    if(!n||n>=sizeof(value))return {};return std::string(value,n);
}
std::filesystem::path ModuleDirectory()noexcept {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Configure),&module))return {};
    wchar_t name[32768]{};if(!GetModuleFileNameW(module,name,32768))return {};
    return std::filesystem::path(name).parent_path();
}
void Escaped(std::ostream& o,const std::string& s){o<<'"';for(char c:s){if(c=='"'||c=='\\')o<<'\\';if(static_cast<unsigned char>(c)>=32)o<<c;}o<<'"';}
} // namespace

std::int64_t NowNs()noexcept {
    LARGE_INTEGER f{},q{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0||!QueryPerformanceCounter(&q))return 0;
    return (q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;
}
void Configure(const std::filesystem::path& reportPath)noexcept {
    auto& s=S();
    try {
        const auto switchValue=Environment("FVR_XM8_BOLT");
        s.switchOn=Xm8BoltSwitchEnabled(switchValue?std::optional<std::string_view>(*switchValue):std::nullopt);
        if(!s.switchOn){s.load={};s.load.status=Xm8BoltAssetStatus::Disabled;s.load.detail="FVR_XM8_BOLT=0";return;}
        const auto override_=Environment("FVR_XM8_BOLT_ASSET").value_or(std::string{});
        const std::array<std::filesystem::path,2> anchors{reportPath.parent_path(),ModuleDirectory()};
        const auto path=ResolveXm8BoltAssetPath(override_,anchors,[](const std::filesystem::path& p){std::error_code e;return std::filesystem::is_regular_file(p,e);});
        if(!path){s.load={};s.load.status=Xm8BoltAssetStatus::NotFound;s.load.detail="default asset not found above report/probe folders";return;}
        s.load=LoadXm8BoltAsset(*path);
        if(s.load.status!=Xm8BoltAssetStatus::Loaded||!s.load.asset)return;
        s.asset=s.load.asset;
        {std::lock_guard lock(s.cycleMutex);s.cycle.emplace(*s.asset);}
        s.enabled.store(true,std::memory_order_release);
    }catch(...){s.load={};s.load.status=Xm8BoltAssetStatus::Exception;s.load.detail="configure";}
}
bool Active()noexcept {const auto& s=S();return s.enabled.load(std::memory_order_acquire)&&!s.stopping.load(std::memory_order_acquire);}
std::shared_ptr<const Xm8BoltAsset> Asset()noexcept {return Active()?S().asset:nullptr;}
void PublishSource(const std::optional<RenderSource>& source)noexcept {
    auto& s=S();
    if(!source){s.source.store({},std::memory_order_release);return;}
    s.lastTravel.store(source->travel,std::memory_order_relaxed);
    s.source.store(std::make_shared<const RenderSource>(*source),std::memory_order_release);
}
bool CardCollapseAllowed(std::int64_t now)noexcept {return Active()&&S().health.Healthy(now);}
void NoteCardDecision(Xm8BoltCardReason reason)noexcept {if(unsigned(reason)<S().card.size())S().card[unsigned(reason)].fetch_add(1,std::memory_order_relaxed);}

void DrawEye(ID3D11DeviceContext* c,unsigned eye,std::uint64_t world,std::uint64_t request,std::uint64_t frame,
    const math::Matrix4& view,const math::Matrix4& projection,std::int64_t now)noexcept {
    auto& s=S();if(!Active()||!c||eye>1)return;
    const auto source=s.source.load(std::memory_order_acquire);
    if(!source){++s.skippedNoSource;return;}
    const auto fail=[&](std::atomic<std::uint64_t>& reason){++reason;++s.failed;s.health.Record(frame,eye,false,now);};
    // A bolt pose older than the native frame lease is not drawn; the latch
    // then drops and the next palette keeps the native card.
    if(now<source->observedNs||now>=source->deadlineNs){fail(s.stale);return;}
    try {
        if(!s.context){s.context=c;s.thread=GetCurrentThreadId();}
        if(c!=s.context||GetCurrentThreadId()!=s.thread){fail(s.wrongThread);return;}
        if(s.initFailed){fail(s.backend);return;}
        if(!s.renderer){auto* r=new graphics::D3D11RigidPropRenderer(true);
            if(!r->Initialize(c)){delete r;s.initFailed=true;fail(s.backend);return;}s.renderer=r;}
        if(!s.uploaded){const std::array<graphics::RigidPropSectionUpload,1> sections{{{s.asset->mesh,{.16f,.16f,.17f,1.f}}}};
            if(!s.renderer->Upload(s.asset->key,sections)){fail(s.backend);return;}s.uploaded=true;}
        ID3D11RenderTargetView* rtv=nullptr;ID3D11DepthStencilView* dsv=nullptr;c->OMGetRenderTargets(1,&rtv,&dsv);
        struct Release{ID3D11RenderTargetView*& r;ID3D11DepthStencilView*& d;~Release(){if(r)r->Release();if(d)d->Release();}} release{rtv,dsv};
        if(!rtv||!dsv){fail(s.noTarget);return;}
        ComPtr<ID3D11Resource> cr,dr;rtv->GetResource(&cr);dsv->GetResource(&dr);ComPtr<ID3D11Texture2D> ct,dt;
        if(!cr||!dr||FAILED(cr.As(&ct))||FAILED(dr.As(&dt))){fail(s.noTarget);return;}
        D3D11_TEXTURE2D_DESC cd{},dd{};ct->GetDesc(&cd);dt->GetDesc(&dd);
        if(cd.Width!=dd.Width||cd.Height!=dd.Height||cd.SampleDesc.Count!=dd.SampleDesc.Count){fail(s.noTarget);return;}
        const auto observed=source->observedNs,deadline=(std::min)(source->deadlineNs,source->observedNs+100000000);
        const graphics::RigidPropEye lease{world?world:1,request?request:1,std::uint64_t(eye)+1,frame?frame:1,source->actor,source->actorGeneration,
            source->weapon,source->equipmentGeneration,source->space,source->input,eye,observed,deadline};
        // Same convention as the in-game dropped-magazine draw: scene depth is
        // tested (receiver shell occludes the bolt), never written.
        const bool reversed=projection.values[3][2]>0;
        if(!s.renderer->Draw(s.asset->key,lease,lease,now,source->world,view,projection,{rtv,dsv,cd.Width,cd.Height,reversed,false})){fail(s.backend);return;}
        ++s.drawn;s.health.Record(frame,eye,true,now);
    }catch(...){fail(s.backend);}
}

Xm8BoltCycleResult TickCycle(const Xm8BoltCycleSample& sample,interaction::HandInteraction& hands,std::uint64_t& intent)noexcept {
    auto& s=S();if(!Active())return {};
    std::lock_guard lock(s.cycleMutex);if(!s.cycle)return {};
    const auto out=s.cycle->Tick(sample,hands,intent);
    if(out.blocksFire)++s.fireBlocked;if(out.presentationComplete)++s.completions;
    return out;
}
float HandleFraction(float travel,float stroke)noexcept {
    if(!std::isfinite(travel)||!std::isfinite(stroke)||stroke<=0||travel<=0)return 0;
    return (std::min)(1.f,travel/stroke);
}
void RequestStop()noexcept {auto& s=S();s.stopping.store(true,std::memory_order_release);s.source.store({},std::memory_order_release);s.health.Reset();}
void PumpStop()noexcept {
    auto& s=S();if(!s.stopping.load(std::memory_order_acquire)||!s.renderer||GetCurrentThreadId()!=s.thread)return;
    delete s.renderer;s.renderer=nullptr;s.uploaded=false;s.context=nullptr;
}
void Report(std::ostream& o){
    auto& s=S();const auto& l=s.load;
    o<<"{\"switch\":"<<(s.switchOn?"true":"false")<<",\"active\":"<<(Active()?"true":"false")
     <<",\"asset_status\":\""<<Xm8BoltAssetStatusName(l.status)<<"\",\"detail\":";Escaped(o,l.detail);
    o<<",\"path\":";Escaped(o,l.path.string());o<<",\"validation_path\":";Escaped(o,l.validationPath.string());
    if(s.asset){o<<",\"sha256\":\""<<s.asset->sha256<<"\",\"vertices\":"<<s.asset->vertices<<",\"triangles\":"<<s.asset->triangles
        <<",\"stroke_m\":"<<s.asset->stroke<<",\"axis\":["<<s.asset->axis[0]<<','<<s.asset->axis[1]<<','<<s.asset->axis[2]<<']';}
    o<<",\"draw\":{\"drawn\":"<<s.drawn.load()<<",\"failed\":"<<s.failed.load()<<",\"stale\":"<<s.stale.load()<<",\"no_target\":"<<s.noTarget.load()
     <<",\"wrong_thread\":"<<s.wrongThread.load()<<",\"backend\":"<<s.backend.load()<<",\"no_source\":"<<s.skippedNoSource.load()
     <<",\"init_failed\":"<<(s.initFailed?"true":"false")<<",\"healthy\":"<<(s.health.Healthy(NowNs())?"true":"false")<<'}';
    o<<",\"card\":{";for(unsigned n=0;n<s.card.size();++n){if(n)o<<',';o<<'"'<<Xm8BoltCardReasonName(Xm8BoltCardReason(n))<<"\":"<<s.card[n].load();}o<<'}';
    o<<",\"observed_travel_m\":"<<s.lastTravel.load()<<",\"fire_blocked_ticks\":"<<s.fireBlocked.load()<<",\"presentation_completions\":"<<s.completions.load();
    o<<",\"cycle\":";{std::unique_lock lock(s.cycleMutex,std::try_to_lock);if(lock.owns_lock()&&s.cycle)s.cycle->Report(o);else o<<"null";}
    o<<",\"native_completion_hook\":false}";
}
} // namespace fvr::bc2::xm8BoltRuntime
