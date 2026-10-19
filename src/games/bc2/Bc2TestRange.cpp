#include "Bc2TestRange.h"
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
namespace {
char Lower(char c)noexcept{return c>='A'&&c<='Z'?char(c-'A'+'a'):c;}
bool Same(std::string_view a,std::string_view b)noexcept{
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return Lower(x)==Lower(y);});
}
bool NeedsRestore(RangeOperation op)noexcept{return op==RangeOperation::PrepareLoadout;}
void Json(std::string& o,std::string_view s){
    o+='"';
    for(char c:s){if(c=='"'||c=='\\'){o+='\\';o+=c;}else if(c>=0x20&&c<0x7f)o+=c;else o+='?';}
    o+='"';
}
}
std::string_view RangeOperationName(RangeOperation op)noexcept{
    constexpr std::array<std::string_view,std::size_t(RangeOperation::Count)> names{
        "write_settings","load_level","skip_cinematic","prepare_loadout","clear_inventory","give_weapon","select_weapon","set_ammo","teleport"};
    return op<RangeOperation::Count?names[std::size_t(op)]:"unknown";
}
std::string_view RangeStepName(RangeStep s)noexcept{
    constexpr std::array<std::string_view,14> names{"none","write_settings","prepare_loadout","await_level","load_level","await_player",
        "skip_cinematic","clear_inventory","give_weapon","select_weapon","set_ammo","teleport","ready","restore"};
    return std::size_t(s)<names.size()?names[std::size_t(s)]:"unknown";
}
std::string_view RangeReasonName(RangeReason r)noexcept{
    constexpr std::array<std::string_view,16> names{"none","invalid_spec","unavailable","not_started","unknown_dispatch","readback_timeout",
        "ready_timeout","run_timeout","wrong_level","unobservable","owner_lost","stopped","restore_failed","level_already_loaded",
        "player_already_spawned","boot"};
    return std::size_t(r)<names.size()?names[std::size_t(r)]:"unknown";
}
std::string_view RangeResultName(RangeResult r)noexcept{
    constexpr std::array<std::string_view,7> names{"dispatch","ok","wait","skip","fail","ready","finished"};
    return std::size_t(r)<names.size()?names[std::size_t(r)]:"unknown";
}
bool RangeOperationAdmitted(const NativeOperationBinding& binding,RangeOperation op,
    std::span<const RangeOperationCapability> records,std::string_view build)noexcept{
    if(op>=RangeOperation::Count||!CompleteOperationBinding(binding)||!OperationDigest(build))return false;
    unsigned matches=0;
    for(const auto& r:records){
        if(r.binding!=binding||r.operation!=op||r.algorithm!=RangeOperationAlgorithm||r.sourceSha256!=build)continue;
        // Contradictory duplicates cannot be resolved by array order.
        if(!r.reviewed||!r.readbackVerified||!OperationDigest(r.executableSha256)||!OperationDigest(r.readbackReceiptSha256))return false;
        if(NeedsRestore(op)&&(!r.restoreVerified||!OperationDigest(r.restoreReceiptSha256)))return false;
        ++matches;
    }
    return matches==1;
}
bool RangeHookBound(const RangeHooks& h,RangeOperation op)noexcept{
    switch(op){
    case RangeOperation::WriteSettings:return h.writeSettings;
    case RangeOperation::LoadLevel:return h.loadLevel;
    case RangeOperation::SkipCinematic:return h.skipCinematic;
    case RangeOperation::PrepareLoadout:return h.prepareLoadout&&h.restoreLoadout;
    case RangeOperation::ClearInventory:return h.clearInventory;
    case RangeOperation::GiveWeapon:return h.giveWeapon;
    case RangeOperation::SelectWeapon:return h.selectWeapon;
    case RangeOperation::SetAmmo:return h.setAmmo;
    case RangeOperation::Teleport:return h.teleport;
    default:return false;
    }
}
RangeHooks UnmappedRangeHooks()noexcept{return {};}
std::string FormatRangeEvent(const RangeEvent& e,std::uint64_t tick,std::int64_t qpcUs){
    std::string o="{\"v\":1,\"tick\":"+std::to_string(tick)+",\"qpc_us\":"+std::to_string(qpcUs)+
        ",\"src\":\"fvr\",\"phase\":\"range\",\"serial\":"+std::to_string(e.serial)+",\"step\":";
    Json(o,RangeStepName(e.step));o+=",\"result\":";Json(o,RangeResultName(e.result));
    o+=",\"reason\":";Json(o,RangeReasonName(e.reason));
    if(!e.asset.empty()){o+=",\"weapon\":{\"asset\":";Json(o,e.asset);o+='}';}
    return o+'}';
}
void TestRange::Emit(std::int64_t now,RangeStep step,RangeResult result,RangeReason reason,std::string_view asset){
    if(events_.size()>=RangeEventLimit){++dropped_;return;}
    events_.push_back({++serial_,now,step,result,reason,std::string(asset)});
}
std::vector<RangeEvent> TestRange::TakeEvents(){std::vector<RangeEvent> out;out.swap(events_);return out;}
bool TestRange::Restore(std::int64_t now){
    if(!loadoutPatched_||loadoutRestored_)return true;
    loadoutRestored_=true;
    const bool ok=hooks_.restoreLoadout&&hooks_.restoreLoadout(hooks_.context);
    Emit(now,RangeStep::Restore,ok?RangeResult::Ok:RangeResult::Fail,ok?RangeReason::None:RangeReason::RestoreFailed);
    return ok;
}
void TestRange::Fail(std::int64_t now,RangeStep step,RangeReason reason,std::string_view asset){
    awaiting_=false;Emit(now,step,RangeResult::Fail,reason,asset);
    phase_=RangePhase::Failed;reason_=Restore(now)?reason:RangeReason::RestoreFailed;
}
void TestRange::Finish(std::int64_t now,RangeReason reason){
    awaiting_=false;phase_=RangePhase::Finished;reason_=Restore(now)?reason:RangeReason::RestoreFailed;
    Emit(now,step_,RangeResult::Finished,reason_);
}
bool TestRange::Begin(const runtime::RangeSpec& spec,const NativeOperationBinding& binding,
    std::span<const RangeOperationCapability> records,const RangeHooks& hooks,std::int64_t now,std::string_view build,
    const RangeBootReceipt* boot){
    if(phase_!=RangePhase::Inactive)return false; // one range per process, like the input tape
    std::string error;
    if(!runtime::ValidateRange(spec,error)){Fail(now,RangeStep::None,RangeReason::InvalidSpec);return false;}
    spec_=spec;hooks_=hooks;beginNs_=now;
    for(std::size_t n=0;n<available_.size();++n){
        const auto op=RangeOperation(n);
        available_[n]=RangeHookBound(hooks,op)&&RangeOperationAdmitted(binding,op,records,build);
    }
    using Route=runtime::RangeLevelRoute;
    if(spec.route==Route::Menu){
        settings_=runtime::RangeSettings(spec);
        if(!Available(RangeOperation::WriteSettings)){Fail(now,RangeStep::WriteSettings,RangeReason::Unavailable);return false;}
    }
    if(spec.route==Route::Override&&!(boot&&boot->applied)){Fail(now,RangeStep::WriteSettings,RangeReason::Unavailable);return false;}
    if(spec.route!=Route::Manual&&!Available(RangeOperation::LoadLevel)){Fail(now,RangeStep::LoadLevel,RangeReason::Unavailable);return false;}
    phase_=RangePhase::Running;
    if(spec.route==Route::Override){Emit(now,RangeStep::WriteSettings,RangeResult::Ok,RangeReason::Boot);step_=RangeStep::LoadLevel;}
    else step_=spec.route==Route::Menu?RangeStep::WriteSettings:RangeStep::AwaitLevel;
    return true;
}
void TestRange::Stop(std::int64_t now){if(Active())Finish(now,RangeReason::Stopped);}
bool TestRange::Dispatch(std::int64_t now,RangeStep step,RangeOperation op,RangeDispatch result,std::int64_t readbackNs,std::string_view asset){
    if(op==RangeOperation::PrepareLoadout&&result!=RangeDispatch::NotStarted)loadoutPatched_=true;
    if(result==RangeDispatch::NotStarted){Fail(now,step,RangeReason::NotStarted,asset);return false;}
    if(result!=RangeDispatch::Accepted){Fail(now,step,RangeReason::UnknownDispatch,asset);return false;}
    Emit(now,step,RangeResult::Dispatch,RangeReason::None,asset);
    awaiting_=true;dispatchSequence_=sequence_;deadlineNs_=now+readbackNs;return true;
}
const RangeWeaponObservation* TestRange::Find(const RangeObservation& o,std::string_view asset)const{
    for(const auto& w:o.weapons)if(Same(w.asset,asset))return &w;
    return nullptr;
}
bool TestRange::Carries(const RangeObservation& o,std::string_view asset)const{return Find(o,asset)!=nullptr;}
bool TestRange::AmmoTarget(std::string_view asset,RangeAmmoRequest& out)const{
    out={asset,spec_.ammo==runtime::RangeAmmoMode::Full,-1,-1};
    for(const auto& a:spec_.ammoOverrides)if(Same(a.asset,asset)){out.full=a.full;out.loaded=a.loaded;out.reserve=a.reserve;}
    return out.full||out.loaded>=0||out.reserve>=0;
}
bool TestRange::AmmoMatches(const RangeWeaponObservation& w,const RangeAmmoRequest& r)const{
    if(r.full)return w.capacity>=0&&w.reserveCapacity>=0&&w.loaded==w.capacity&&w.reserve==w.reserveCapacity;
    return (r.loaded<0||w.loaded==r.loaded)&&(r.reserve<0||w.reserve==r.reserve);
}
bool TestRange::Verified(const RangeObservation& o)const{
    const auto near=[&](){const auto& t=*spec_.teleport;
        return o.hasPosition&&std::hypot(o.x-t.x,o.y-t.y,o.z-t.z)<=0.5f;};
    switch(step_){
    case RangeStep::LoadLevel:return o.levelLoaded;
    case RangeStep::SkipCinematic:return !o.cinematic;
    case RangeStep::ClearInventory:
        return std::all_of(o.weapons.begin(),o.weapons.end(),[&](const RangeWeaponObservation& w){
            return std::any_of(spec_.weapons.begin(),spec_.weapons.end(),[&](const std::string& s){return Same(s,w.asset);});});
    case RangeStep::GiveWeapon:return Carries(o,pendingAsset_);
    case RangeStep::SelectWeapon:return Same(o.selected,spec_.Selected());
    case RangeStep::SetAmmo:{const auto* w=Find(o,pendingAmmoAsset_);return w&&AmmoMatches(*w,pendingAmmo_);}
    case RangeStep::Teleport:return near();
    default:return false;
    }
}
void TestRange::Tick(const RangeObservation& o,std::int64_t now){
    if(!Active())return;
    if(phase_==RangePhase::Running&&now-beginNs_>std::int64_t(spec_.readyTimeoutS)*1000000000){Fail(now,step_,RangeReason::ReadyTimeout,pendingAsset_);return;}
    if(now-beginNs_>std::int64_t(spec_.timeoutS)*1000000000){Finish(now,RangeReason::RunTimeout);return;}
    if(awaiting_&&now>deadlineNs_){Fail(now,step_,RangeReason::ReadbackTimeout,pendingAsset_);return;}
    if(o.sequence<=sequence_||o.observedNs>now||now-o.observedNs>RangeFreshNs)return;
    sequence_=o.sequence;
    if(owner_&&(o.owner!=owner_||!o.playerAlive)){
        // Death, respawn, checkpoint reload or vehicle entry ends the range, like the tape owner rule.
        if(phase_==RangePhase::Ready)Finish(now,RangeReason::OwnerLost);else Fail(now,step_,RangeReason::OwnerLost);
        return;
    }
    if(phase_==RangePhase::Ready)return;
    if(awaiting_){
        if(o.sequence<=dispatchSequence_||!Verified(o))return;
        awaiting_=false;
        if(step_==RangeStep::LoadLevel&&!o.level.empty()&&!Same(o.level,spec_.level)){Fail(now,step_,RangeReason::WrongLevel);return;}
        Emit(now,step_,RangeResult::Ok,RangeReason::None,step_==RangeStep::SetAmmo?std::string_view(pendingAmmoAsset_):std::string_view(pendingAsset_));
        if(step_==RangeStep::LoadLevel)step_=RangeStep::PrepareLoadout;
        else if(step_==RangeStep::SkipCinematic)step_=RangeStep::AwaitPlayer;
        // Clear/give/select/ammo/teleport re-evaluate their own step: one asset per pass.
        pendingAsset_.clear();
        return;
    }
    Advance(o,now);
}
void TestRange::Advance(const RangeObservation& o,std::int64_t now){
    switch(step_){
    case RangeStep::WriteSettings:
        // Route B: only at the main menu, before any level exists. The bound
        // hook reads every pair back through the registry before Accepted.
        if(o.levelLoaded){Fail(now,step_,RangeReason::LevelAlreadyLoaded);return;}
        if(!o.frontEnd)return;
        if(!Dispatch(now,step_,RangeOperation::WriteSettings,hooks_.writeSettings(hooks_.context,settings_),0))return;
        awaiting_=false;Emit(now,step_,RangeResult::Ok);step_=RangeStep::LoadLevel;return;
    case RangeStep::LoadLevel:
        if(o.levelLoaded&&!levelRequested_){Fail(now,step_,RangeReason::LevelAlreadyLoaded);return;} // never reuse or unload a level the range did not start
        if(!o.frontEnd)return;
        levelRequested_=true; // newSP loads Client.Level
        Dispatch(now,step_,RangeOperation::LoadLevel,hooks_.loadLevel(hooks_.context,spec_.level),RangeLevelReadbackNs);return;
    case RangeStep::AwaitLevel:
        if(!o.levelLoaded)return;
        if(!o.level.empty()&&!Same(o.level,spec_.level)){Fail(now,step_,RangeReason::WrongLevel);return;}
        Emit(now,step_,RangeResult::Ok);step_=RangeStep::PrepareLoadout;return;
    case RangeStep::PrepareLoadout:
        // Kit Weapons[] rewrite: after the level (assets loaded), before the soldier spawns.
        if(Available(RangeOperation::PrepareLoadout)&&!o.playerAlive){
            if(!Dispatch(now,step_,RangeOperation::PrepareLoadout,hooks_.prepareLoadout(hooks_.context,spec_.weapons,spec_.weaponSlots),0))return;
            // Proven later by the carried inventory observation, not by this receipt.
            awaiting_=false;Emit(now,step_,RangeResult::Ok);
        }else Emit(now,step_,RangeResult::Skip,Available(RangeOperation::PrepareLoadout)?RangeReason::PlayerAlreadySpawned:RangeReason::Unavailable);
        step_=RangeStep::AwaitPlayer;return;
    case RangeStep::AwaitPlayer:
        if(o.cinematic){
            if(spec_.skipCinematics&&Available(RangeOperation::SkipCinematic)&&!cinematicSkipped_){
                cinematicSkipped_=true;step_=RangeStep::SkipCinematic;
                Dispatch(now,step_,RangeOperation::SkipCinematic,hooks_.skipCinematic(hooks_.context),RangeReadbackNs);return;
            }
            if(!cinematicWaitLogged_){cinematicWaitLogged_=true;Emit(now,RangeStep::SkipCinematic,RangeResult::Wait,
                spec_.skipCinematics&&!Available(RangeOperation::SkipCinematic)?RangeReason::Unavailable:RangeReason::None);}
            return;
        }
        if(!o.playerAlive||!o.owner)return;
        cinematicSkipped_=false;owner_=o.owner;Emit(now,step_,RangeResult::Ok);step_=RangeStep::ClearInventory;return;
    case RangeStep::ClearInventory:{
        const bool extra=std::any_of(o.weapons.begin(),o.weapons.end(),[&](const RangeWeaponObservation& w){
            return std::none_of(spec_.weapons.begin(),spec_.weapons.end(),[&](const std::string& s){return Same(s,w.asset);});});
        if(!extra||!spec_.clearInventory){step_=RangeStep::GiveWeapon;return;}
        if(!Available(RangeOperation::ClearInventory)){Fail(now,step_,RangeReason::Unavailable);return;}
        Dispatch(now,step_,RangeOperation::ClearInventory,hooks_.clearInventory(hooks_.context,spec_.weapons),RangeReadbackNs);return;
    }
    case RangeStep::GiveWeapon:{
        const auto missing=std::find_if(spec_.weapons.begin(),spec_.weapons.end(),[&](const std::string& s){return !Carries(o,s);});
        if(missing==spec_.weapons.end()){step_=RangeStep::SelectWeapon;return;}
        if(!Available(RangeOperation::GiveWeapon)){Fail(now,step_,RangeReason::Unavailable,*missing);return;}
        pendingAsset_=*missing;
        Dispatch(now,step_,RangeOperation::GiveWeapon,hooks_.giveWeapon(hooks_.context,pendingAsset_),RangeReadbackNs,pendingAsset_);return;
    }
    case RangeStep::SelectWeapon:
        if(Same(o.selected,spec_.Selected())){step_=RangeStep::SetAmmo;return;}
        if(o.selected.empty()){Fail(now,step_,RangeReason::Unobservable,spec_.Selected());return;}
        if(!Available(RangeOperation::SelectWeapon)){Fail(now,step_,RangeReason::Unavailable,spec_.Selected());return;}
        pendingAsset_=spec_.Selected();
        Dispatch(now,step_,RangeOperation::SelectWeapon,hooks_.selectWeapon(hooks_.context,pendingAsset_),RangeReadbackNs,pendingAsset_);return;
    case RangeStep::SetAmmo:
        for(const auto& asset:spec_.weapons){
            RangeAmmoRequest target;if(!AmmoTarget(asset,target))continue;
            const auto* w=Find(o,asset);
            if(!w||(target.full&&(w->capacity<0||w->reserveCapacity<0))||(target.loaded>=0&&w->loaded<0)||(target.reserve>=0&&w->reserve<0)){
                Fail(now,step_,RangeReason::Unobservable,asset);return;}
            if(AmmoMatches(*w,target))continue;
            if(!Available(RangeOperation::SetAmmo)){Fail(now,step_,RangeReason::Unavailable,asset);return;}
            pendingAmmoAsset_=asset;pendingAmmo_=target;pendingAmmo_.asset=pendingAmmoAsset_;
            Dispatch(now,step_,RangeOperation::SetAmmo,hooks_.setAmmo(hooks_.context,pendingAmmo_),RangeReadbackNs,asset);return;
        }
        step_=RangeStep::Teleport;return;
    case RangeStep::Teleport:
        if(spec_.teleport){
            if(!o.hasPosition){Fail(now,step_,RangeReason::Unobservable);return;}
            if(!Verified(o)){
                if(!Available(RangeOperation::Teleport)){Fail(now,step_,RangeReason::Unavailable);return;}
                Dispatch(now,step_,RangeOperation::Teleport,hooks_.teleport(hooks_.context,*spec_.teleport),RangeReadbackNs);return;
            }
        }
        step_=RangeStep::Ready;phase_=RangePhase::Ready;Emit(now,step_,RangeResult::Ready,RangeReason::None,spec_.Selected());return;
    default:return;
    }
}
}
