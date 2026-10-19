#include "Bc2GunEventLog.h"
#include <charconv>
#include <chrono>
#include <fstream>
#include <cmath>
#include <ostream>
namespace fvr::bc2 {
namespace {
using P=GunEventPhase;using interaction::DetachableMagazinePhase;using interaction::DetachableMagazineReason;
constexpr std::array<std::string_view,std::size_t(P::Count)> PhaseNames{
    "firing_state","transfer","commit","restore","step","auto_reload","freeze_start","freeze_stop","fire","weapon_switch","sample","dropped",
    "slide_lock","anim_sound",
    "begin","pouch_start","capture","seat","credit","rack","cancel","unseat","release","complete","end","haptic","claim","tape","sample",
    "chamber","prop","pack","rack_trace","hint"};
bool Has(const GunEvent& e,std::uint16_t bit)noexcept{return (e.flags&bit)!=0;}
// Writes "key": value pairs; strings are already printable ASCII (SetGunText).
struct Line {
    std::string& o;
    void Key(std::string_view k){o+=",\"";o+=k;o+="\":";}
    void Str(std::string_view text){o+='"';for(char c:text){if(c=='"'||c=='\\')o+='\\';o+=c;}o+='"';}
    void Int(std::string_view k,std::int64_t value){Key(k);char b[24];const auto r=std::to_chars(b,b+sizeof(b),value);o.append(b,r.ptr);}
    void Bool(std::string_view k,bool value){Key(k);o+=value?"true":"false";}
    void Text(std::string_view k,std::string_view value){Key(k);Str(value);}
    void OptText(std::string_view k,std::string_view value){if(!value.empty())Text(k,value);}
    void Float(std::string_view k,float value){Key(k);if(!std::isfinite(value)){o+="null";return;}
        char b[32];const auto r=std::to_chars(b,b+sizeof(b),value);o.append(b,r.ptr);}
};
std::string Hex(std::uint32_t value){char b[16]{'0','x'};const auto r=std::to_chars(b+2,b+sizeof(b),value,16);return std::string(b,r.ptr);}
}
std::string_view GunEventPhaseName(GunEventPhase p)noexcept{return std::size_t(p)<PhaseNames.size()?PhaseNames[std::size_t(p)]:std::string_view{};}
GunEventSource GunEventPhaseSource(GunEventPhase p)noexcept{return p<P::Begin?GunEventSource::Native:GunEventSource::Fvr;}
std::string_view GunCopyName(GunCopy c)noexcept{
    switch(c){case GunCopy::Client0:return "client0";case GunCopy::Client1:return "client1";case GunCopy::Server:return "server";default:return {};}
}
void AppendGunEventLine(std::string& o,const GunEvent& e,bool sim){
    Line l{o};const auto& v=e.v;
    o+="{\"v\":";o+=char('0'+GunEventSchemaVersion);
    l.Int("tick",std::int64_t(e.tick));l.Int("qpc_us",e.qpcUs);
    l.Text("src",GunEventPhaseSource(e.phase)==GunEventSource::Native?"native":"fvr");
    const auto asset=GunTextView(e.asset);
    if(!asset.empty()){l.Key("weapon");o+="{\"asset\":";l.Str(asset);o+=",\"id\":";l.Str(Hex(e.weaponId));o+='}';}
    l.Key("copy");if(const auto c=GunCopyName(e.copy);!c.empty())l.Str(c);else o+="null";
    l.Text("phase",GunEventPhaseName(e.phase));
    if(sim)l.Bool("sim",true);
    const auto reason=GunTextView(e.reason),detail=GunTextView(e.detail),hand=GunTextView(e.hand);
    switch(e.phase){
    case P::FiringState:l.Int("from",v[0]);l.Int("to",v[1]);if(Has(e,gun_flag::Next)){l.Int("next",v[2]);l.Int("flags_a8",v[3]);}
        if(Has(e,gun_flag::Timer))l.Float("timer",e.f);break;
    case P::Transfer:case P::Restore:l.Int("loaded_before",v[0]);l.Int("reserve_before",v[1]);l.Int("loaded_after",v[2]);l.Int("reserve_after",v[3]);
        if(e.phase==P::Transfer)l.OptText("path",reason);else l.Bool("matched",Has(e,gun_flag::Matched));break;
    case P::Commit:l.Int("from",v[0]);l.Int("to",v[1]);break;
    case P::Step:l.Int("state",v[0]);l.Int("loaded",v[1]);l.Int("reserve",v[2]);if(Has(e,gun_flag::EmptyOverride))l.Bool("empty_override",true);break;
    case P::AutoReload:l.Bool("suppressed",Has(e,gun_flag::Suppressed));if(Has(e,gun_flag::Ammo)){l.Int("loaded",v[0]);l.Int("reserve",v[1]);}break;
    case P::FreezeStart:l.OptText("reason",reason);if(Has(e,gun_flag::Timer))l.Float("timer",e.f);break;
    case P::FreezeStop:l.OptText("reason",reason);if(Has(e,gun_flag::Wide))l.Int("held_us",e.wide);break;
    case P::Fire:l.Text("result",reason.empty()?"shot":reason);l.Int("loaded",v[0]);l.Int("reserve",v[1]);l.Int("consumed",v[2]);if(reason=="dry")l.Bool("native_click",v[3]!=0);break;
    case P::WeaponSwitch:l.Key("from");if(detail.empty())o+="null";else l.Str(detail);l.Key("to");if(asset.empty())o+="null";else l.Str(asset);
        if(Has(e,gun_flag::Cancelled))l.Bool("cancelled",true);break;
    case P::NativeSample:l.Int("loaded",v[0]);l.Int("reserve",v[1]);l.Int("cur",v[2]);l.Int("next",v[3]);l.Int("shots",v[4]);l.Int("transfers",v[5]);
        l.Text("mag_phase",GunTextView(e.magPhase));l.Bool("stale",Has(e,gun_flag::Stale));break;
    case P::Dropped:l.Int("n",e.wide);break;
    case P::Begin:l.OptText("hand",hand);l.OptText("kind",detail);break;
    case P::PouchStart:l.OptText("hand",hand);l.OptText("item",detail);break;
    case P::Capture:if(Has(e,gun_flag::Value0))l.Int("distance_mm",v[0]);break;
    case P::Seat:if(Has(e,gun_flag::Cycle))l.Int("cycle",v[0]);if(Has(e,gun_flag::Value0))l.Int("credits",v[1]);break;
    case P::Credit:l.Int("loaded",v[0]);l.Int("reserve",v[1]);if(Has(e,gun_flag::Cycle))l.Int("cycle",v[2]);break;
    case P::Rack:l.Bool("by_grip",Has(e,gun_flag::ByGrip));break;
    case P::Cancel:l.Text("reason",reason.empty()?"unknown":reason);break;
    case P::Unseat:l.OptText("hand",hand);break;
    case P::Release:l.OptText("to",reason);break;
    case P::Complete:if(Has(e,gun_flag::Cycle))l.Int("cycle",v[0]);break;
    case P::End:l.Text("reason",reason.empty()?"unknown":reason);break;
    case P::Haptic:l.OptText("kind",detail);l.OptText("hand",hand);l.Bool("sent",Has(e,gun_flag::Sent));break;
    case P::Claim:l.OptText("hand",hand);l.OptText("kind",detail);if(Has(e,gun_flag::Wide))l.Int("token",e.wide);l.OptText("result",reason);break;
    case P::Tape:l.OptText("reason",reason);l.OptText("name",detail);if(Has(e,gun_flag::HasMet))l.Bool("met",Has(e,gun_flag::Met));
        if(Has(e,gun_flag::Ammo)){l.Int("loaded",v[0]);l.Int("reserve",v[1]);}break;
    case P::FvrSample:l.OptText("hand",hand);break;
    case P::AnimSound:l.Text("anim",detail);if(!reason.empty())l.Text("action",reason);l.Int("listener_view",v[0]);l.Int("soldier_view",v[1]);l.Bool("played",v[2]!=0);l.Int("callback",v[3]);l.Int("soldier",e.wide);break;
    case P::SlideLock:l.Bool("locked",v[2]!=0);l.Int("state",v[0]);l.Int("loaded",v[1]);if(v[3])l.Bool("last_round",true);break;
    // Per native palette pack (two per drawn frame): which rig pose went in.
    case P::Pack:l.Int("draw",e.wide);l.Int("pack",v[0]);l.Bool("posed",v[1]!=0);l.Int("miss",v[2]);l.Int("magazine",v[3]);
        l.Int("reload",v[4]);l.Int("flags",v[5]);l.OptText("reason",reason);break;
    // C2 rack diagnostic: millimetres, travel in 1/1000 of the stroke.
    case P::RackTrace:l.Int("rack_phase",v[0]);l.Int("rack_reason",v[1]);l.Int("distance_mm",v[2]);l.Int("along_mm",v[3]);
        l.Int("lateral_mm",v[4]);l.Int("travel_milli",v[5]);l.Bool("grip",(e.flags&gun_flag::Value0)!=0);l.OptText("points_mm",detail);break;
    // F5 reload tip: a part started or stopped glowing.
    case P::Hint:l.OptText("part",detail);l.Bool("lit",v[0]!=0);break;
    case P::Prop:l.OptText("event",detail);l.Int("id",v[0]);l.Int("x_mm",v[1]);l.Int("y_mm",v[2]);l.Int("z_mm",v[3]);l.Int("floor_mm",v[4]);break;
    case P::Chamber:l.OptText("event",detail);l.Int("loaded",v[0]);l.Int("display_ammo",v[1]);l.Bool("chamber_loaded",v[2]!=0);l.Bool("needs_rack",v[3]!=0);l.OptText("label",reason);break;
    default:break;
    }
    if(GunEventPhaseSource(e.phase)==GunEventSource::Fvr){
        l.OptText("mag_phase",GunTextView(e.magPhase));
        if(Has(e,gun_flag::MagOut))l.Bool("mag_out",true);
    }
    o+="}\n";
}

GunEventRing::GunEventRing(std::size_t capacity){
    std::size_t n=2;while(n<capacity&&n<(std::size_t(1)<<20))n<<=1;
    cells_=std::make_unique<Cell[]>(n);mask_=n-1;
    for(std::size_t i=0;i<n;++i)cells_[i].sequence.store(i,std::memory_order_relaxed);
}
bool GunEventRing::Push(const GunEvent& e)noexcept{
    auto pos=enqueue_.load(std::memory_order_relaxed);
    for(;;){
        auto& cell=cells_[pos&mask_];const auto seq=cell.sequence.load(std::memory_order_acquire);
        const auto diff=static_cast<std::intptr_t>(seq)-static_cast<std::intptr_t>(pos);
        if(diff==0){if(enqueue_.compare_exchange_weak(pos,pos+1,std::memory_order_relaxed)){
                cell.event=e;cell.sequence.store(pos+1,std::memory_order_release);pushed_.fetch_add(1,std::memory_order_relaxed);return true;}}
        else if(diff<0){dropped_.fetch_add(1,std::memory_order_relaxed);unreported_.fetch_add(1,std::memory_order_relaxed);return false;}
        else pos=enqueue_.load(std::memory_order_relaxed);
    }
}
bool GunEventRing::Pop(GunEvent& out)noexcept{
    auto& cell=cells_[dequeue_&mask_];const auto seq=cell.sequence.load(std::memory_order_acquire);
    if(static_cast<std::intptr_t>(seq)-static_cast<std::intptr_t>(dequeue_+1)<0)return false;
    out=cell.event;cell.sequence.store(dequeue_+mask_+1,std::memory_order_release);++dequeue_;return true;
}

bool GunEventLog::Open(std::unique_ptr<std::ostream> out,Clock clock,std::size_t capacity){
    if(ring_||closed_||!out||!*out||!clock||!capacity)return false;
    out_=std::move(out);clock_=clock;ring_=std::make_unique<GunEventRing>(capacity);line_.reserve(512);
    enabled_.store(true,std::memory_order_release);return true;
}
bool GunEventLog::Push(GunEvent e)noexcept{
    if(!enabled_.load(std::memory_order_acquire))return false;
    if(!e.tick)e.tick=Tick();if(!e.qpcUs)e.qpcUs=NowUs();
    return ring_->Push(e);
}
bool GunEventLog::Drain(){
    if(!ring_||!out_||failed_||closed_)return false;
    GunEvent e;std::uint64_t lastTick=Tick();
    while(ring_->Pop(e)){line_.clear();AppendGunEventLine(line_,e);out_->write(line_.data(),std::streamsize(line_.size()));++written_;lastTick=e.tick;}
    if(const auto lost=ring_->TakeUnreportedDrops()){
        GunEvent d;d.phase=P::Dropped;d.tick=lastTick;d.qpcUs=NowUs();d.wide=std::int64_t(lost);
        line_.clear();AppendGunEventLine(line_,d);out_->write(line_.data(),std::streamsize(line_.size()));++written_;++droppedRows_;
    }
    out_->flush();if(!*out_)failed_=true;return !failed_;
}
bool GunEventLog::Close(){
    if(!ring_)return false;
    enabled_.store(false,std::memory_order_release);
    const bool drained=Drain();closed_=true;
    // The DLL stays mapped after a trace, so the stream object is never
    // destroyed. Release the file handle here: Windows only updates the
    // directory entry's size when the handle closes, and a timed-out trace
    // otherwise shows gun-events.jsonl as 0 bytes although every row is on disk.
    if(auto* file=dynamic_cast<std::ofstream*>(out_.get()))file->close();
    return drained;
}
void GunEventLog::Report(std::ostream& o)const{
    o<<"{\"schema\":"<<GunEventSchemaVersion<<",\"enabled\":"<<(ring_?"true":"false")<<",\"closed\":"<<(closed_?"true":"false")
     <<",\"capacity\":"<<(ring_?ring_->Capacity():0)<<",\"pushed\":"<<Pushed()<<",\"written\":"<<written_<<",\"dropped\":"<<Dropped()
     <<",\"dropped_rows\":"<<droppedRows_<<",\"failed\":"<<(failed_?"true":"false")<<",\"last_tick\":"<<Tick()
     <<",\"chamber_logged\":false,\"headset_tested\":false}";
}
namespace {GunEventLog processLog;GunCopyTracker processNativeTracker;} // namespace-scope: no lazy init inside a native callback
GunEventLog& GunEvents()noexcept{return processLog;}
GunCopyTracker& GunNativeTracker()noexcept{return processNativeTracker;}
GunWeapon MakeGunWeapon(std::string_view asset,std::uint32_t id)noexcept{GunWeapon w;SetGunText(w.asset,asset);w.id=id;return w;}
void PushWeaponSwitch(GunEventLog& log,const GunWeapon& from,const GunWeapon& to)noexcept{
    if(!log.Enabled()||from==to)return;GunEvent e;e.phase=P::WeaponSwitch;e.asset=to.asset;e.weaponId=to.id;e.detail=from.asset;log.Push(e);
}

namespace {
GunEvent NativeRow(GunEventPhase phase,const GunWeapon& w,GunCopy copy)noexcept{GunEvent e;e.phase=phase;e.copy=copy;e.asset=w.asset;e.weaponId=w.id;return e;}
bool ReloadState(std::uint32_t s)noexcept{return s==10||s==11||s==12;}
}
void GunCopyTracker::Baseline(Copy& c,const GunWeapon& w,const GunCopyState& s)noexcept{
    c.known=true;c.weapon=w;c.last=s;c.frozen=false;c.shots=c.transfers=0;
    c.slideLocked=s.current==2&&s.loaded==0;c.lastRound=false; // A first sight is a baseline, not an event.
}
void GunCopyTracker::Observe(GunEventLog& log,const GunWeapon& w,GunCopy copy,const GunCopyState& s)noexcept{
    if(!log.Enabled()||copy==GunCopy::None)return;auto& c=copies_[unsigned(copy)];Guard g(c.lock);if(!g.held){busy_.fetch_add(1,std::memory_order_relaxed);log.NoteLost();return;}
    c.seenUs=log.NowUs();
    if(!c.known||c.weapon!=w||c.last.firing!=s.firing){Baseline(c,w,s);return;}
    if(s.current!=c.last.current){auto e=NativeRow(P::FiringState,w,copy);e.v={std::int32_t(c.last.current),std::int32_t(s.current),std::int32_t(s.next),s.flagsA8};
        e.f=s.timer;e.flags=gun_flag::Next|gun_flag::Timer;log.Push(e);}
    if(s.loaded!=c.last.loaded||s.reserve!=c.last.reserve){
        if(s.loaded<c.last.loaded&&s.reserve==c.last.reserve){auto e=NativeRow(P::Fire,w,copy);SetGunText(e.reason,"shot");
            e.v[0]=s.loaded;e.v[1]=s.reserve;e.v[2]=c.last.loaded-s.loaded;++c.shots;log.Push(e);}
        else {auto e=NativeRow(P::Step,w,copy);e.v[0]=std::int32_t(s.current);e.v[1]=s.loaded;e.v[2]=s.reserve;log.Push(e);}
        if(c.last.loaded>0&&s.loaded==0&&s.reserve==c.last.reserve)c.lastRound=true;
    }
    const bool locked=c.slideLocked?!(s.loaded>0||(s.current>=10&&s.current<=12)):s.current==2&&s.loaded==0;
    if(locked!=c.slideLocked){
        auto e=NativeRow(P::SlideLock,w,copy);e.v[0]=std::int32_t(s.current);e.v[1]=s.loaded;e.v[2]=locked;e.v[3]=locked&&c.lastRound;
        if(locked&&c.lastRound&&!lastRoundLockNs_.load(std::memory_order_relaxed))
            lastRoundLockNs_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),std::memory_order_release);
        log.Push(e);c.slideLocked=locked;c.lastRound=false;
    }
    if(s.loaded>0)c.lastRound=false;
    c.last=s;
}
void GunCopyTracker::Transfer(GunEventLog& log,const GunWeapon& w,GunCopy copy,const GunCopyState& before,const GunCopyState& after,std::string_view path)noexcept{
    if(!log.Enabled()||copy==GunCopy::None)return;
    Observe(log,w,copy,before);
    auto& c=copies_[unsigned(copy)];Guard g(c.lock);if(!g.held){busy_.fetch_add(1,std::memory_order_relaxed);log.NoteLost();return;}
    auto e=NativeRow(P::Transfer,w,copy);e.v={before.loaded,before.reserve,after.loaded,after.reserve,0,0};SetGunText(e.reason,path);log.Push(e);
    ++c.transfers;
    if(c.known&&c.last.current!=after.current){auto s=NativeRow(P::FiringState,w,copy);s.v={std::int32_t(c.last.current),std::int32_t(after.current),std::int32_t(after.next),after.flagsA8};
        s.f=after.timer;s.flags=gun_flag::Next|gun_flag::Timer;log.Push(s);}
    c.last=after;c.seenUs=log.NowUs();
}
void GunCopyTracker::Restore(GunEventLog& log,const GunWeapon& w,GunCopy copy,const GunCopyState& before,const GunCopyState& after,bool matched)noexcept{
    if(!log.Enabled()||copy==GunCopy::None)return;
    Observe(log,w,copy,before);
    auto& c=copies_[unsigned(copy)];Guard g(c.lock);if(!g.held){busy_.fetch_add(1,std::memory_order_relaxed);log.NoteLost();return;}
    auto e=NativeRow(P::Restore,w,copy);e.v={before.loaded,before.reserve,after.loaded,after.reserve,0,0};if(matched)e.flags|=gun_flag::Matched;log.Push(e);
    if(c.known&&c.last.current!=after.current){auto s=NativeRow(P::FiringState,w,copy);s.v={std::int32_t(c.last.current),std::int32_t(after.current),std::int32_t(after.next),after.flagsA8};
        s.f=after.timer;s.flags=gun_flag::Next|gun_flag::Timer;log.Push(s);}
    c.last=after;c.seenUs=log.NowUs();
}
void GunCopyTracker::Commit(GunEventLog& log,const GunWeapon& w,GunCopy copy,const GunCopyState& before,const GunCopyState& after)noexcept{
    if(!log.Enabled()||copy==GunCopy::None||before.current==after.current)return;
    auto e=NativeRow(P::Commit,w,copy);e.v[0]=std::int32_t(before.current);e.v[1]=std::int32_t(after.current);log.Push(e);
}
void GunCopyTracker::Freeze(GunEventLog& log,const GunWeapon& w,GunCopy copy,bool applied,const GunCopyState& after)noexcept{
    if(!log.Enabled()||copy==GunCopy::None)return;auto& c=copies_[unsigned(copy)];Guard g(c.lock);if(!g.held){busy_.fetch_add(1,std::memory_order_relaxed);log.NoteLost();return;}
    if(applied==c.frozen)return;
    const auto now=log.NowUs();
    if(applied){c.frozen=true;c.frozenUs=now;c.pumpHold=after.current==7;auto e=NativeRow(P::FreezeStart,w,copy);SetGunText(e.reason,c.pumpHold?"pump":"hold");e.f=after.timer;e.flags=gun_flag::Timer;log.Push(e);return;}
    c.frozen=false;auto e=NativeRow(P::FreezeStop,w,copy);SetGunText(e.reason,ReloadState(after.current)?"release":(after.current==7||after.current==8||after.current==1)&&c.pumpHold?"pump":"cancel");
    e.wide=now>c.frozenUs?now-c.frozenUs:0;e.flags=gun_flag::Wide;log.Push(e);
}
void GunCopyTracker::Trigger(GunEventLog& log,const GunWeapon& w,GunCopy copy,const GunCopyState& before,const GunCopyState& after,bool fire,bool emit)noexcept{
    if(!log.Enabled()||copy==GunCopy::None)return;auto& c=copies_[unsigned(copy)];Guard g(c.lock);if(!g.held){busy_.fetch_add(1,std::memory_order_relaxed);log.NoteLost();return;}
    const bool edge=fire&&!c.fireHeld;c.fireHeld=fire;
    if(!edge||!emit||before.current!=2||before.next!=2||before.loaded>0||after.current!=2||after.loaded>0)return;
    auto e=NativeRow(P::Fire,w,copy);SetGunText(e.reason,"dry");e.v[0]=after.loaded;e.v[1]=after.reserve;e.v[2]=0;e.v[3]=after.reserve<=0;
    dryFires_.fetch_add(1,std::memory_order_relaxed);log.Push(e);
}
void GunCopyTracker::AutoReloadSuppressed(GunEventLog& log,const GunWeapon& w,GunCopy copy,const GunCopyState& before)noexcept{
    if(!log.Enabled()||copy==GunCopy::None)return;
    auto e=NativeRow(P::AutoReload,w,copy);e.flags=gun_flag::Suppressed|gun_flag::Ammo;e.v[0]=before.loaded;e.v[1]=before.reserve;log.Push(e);
}
void GunCopyTracker::Sample(GunEventLog& log,std::string_view magPhase,std::int64_t staleUs)noexcept{
    if(!log.Enabled())return;const auto now=log.NowUs();
    for(unsigned n=0;n<3;++n){auto& c=copies_[n];Guard g(c.lock);if(!g.held){busy_.fetch_add(1,std::memory_order_relaxed);continue;}
        if(!c.known)continue;auto e=NativeRow(P::NativeSample,c.weapon,GunCopy(n));
        e.v={c.last.loaded,c.last.reserve,std::int32_t(c.last.current),std::int32_t(c.last.next),std::int32_t(c.shots),std::int32_t(c.transfers)};
        SetGunText(e.magPhase,magPhase);if(now-c.seenUs>staleUs)e.flags|=gun_flag::Stale;log.Push(e);}
}
bool GunCopyTracker::Last(GunCopy copy,GunCopyState& out)noexcept{
    if(copy==GunCopy::None)return false;auto& c=copies_[unsigned(copy)];Guard g(c.lock);if(!g.held||!c.known)return false;out=c.last;return true;
}
void GunCopyTracker::CloseFreezes(GunEventLog& log,std::string_view reason)noexcept{
    for(unsigned n=0;n<3;++n){auto& c=copies_[n];Guard g(c.lock);if(!g.held||!c.frozen)continue;c.frozen=false;
        auto e=NativeRow(P::FreezeStop,c.weapon,GunCopy(n));SetGunText(e.reason,reason);const auto now=log.NowUs();
        e.wide=now>c.frozenUs?now-c.frozenUs:0;e.flags=gun_flag::Wide;log.Push(e);}
}

std::string_view GunMagPhaseName(DetachableMagazinePhase p)noexcept{
    static constexpr std::array<std::string_view,11> names{"Attached","PreparingRemoval","Pulling","RemovedHeld","WellEmpty","ReplacementHeld","Guided","AwaitingSeat","Complete","Cancelled","AwaitingOriginalReturn"};
    const auto i=unsigned(p);return i<names.size()?names[i]:"Unknown";
}
std::string_view GunMagReasonName(DetachableMagazineReason r)noexcept{
    static constexpr std::array<std::string_view,20> names{"none","invalid_config","invalid_input","stale_input","identity_changed","tracking_lost","unverified_native","native_cycle_changed","gun_claim_lost","need_neutral","outside_magazine","hand_unavailable","claim_lost","pose_jump","pull_abandoned","replacement_rejected","native_rejected","explicit","needs_reconciliation","counter_exhausted"};
    const auto i=unsigned(r);return i<names.size()?names[i]:"unknown";
}
GunEvent GunFvrTracker::Row(GunEventPhase phase,const GunWeapon& w)const noexcept{
    GunEvent e;e.phase=phase;e.asset=w.asset;e.weaponId=w.id;SetGunText(e.magPhase,GunMagPhaseName(magPhase_));
    return e;
}
void GunFvrTracker::Magazine(GunEventLog& log,const GunWeapon& w,const GunMagazineProbe& st)noexcept{
    using M=DetachableMagazinePhase;
    if(st.phase==magPhase_)return;magPhase_=st.phase;
    if(!log.Enabled())return;
    const auto emit=[&](P phase,auto&& fill){auto e=Row(phase,w);fill(e);log.Push(e);};
    const auto none=[](GunEvent&){};
    if(!magSession_&&st.phase!=M::Attached&&st.phase!=M::Complete&&st.phase!=M::Cancelled){magSession_=true;
        emit(P::Begin,[](GunEvent& e){SetGunText(e.hand,"left");SetGunText(e.detail,"magazine");});}
    switch(st.phase){
    case M::RemovedHeld:emit(P::Unseat,[](GunEvent& e){SetGunText(e.hand,"left");});break;
    case M::WellEmpty:emit(P::Release,[](GunEvent& e){SetGunText(e.reason,"pouch");});break;
    case M::ReplacementHeld:emit(P::PouchStart,[](GunEvent& e){SetGunText(e.hand,"left");});break;
    case M::Guided:emit(P::Capture,none);break;
    case M::Complete:emit(P::Complete,[&](GunEvent& e){e.v[0]=std::int32_t(st.cycle);e.flags|=gun_flag::Cycle;});
        if(magSession_)emit(P::End,[](GunEvent& e){SetGunText(e.reason,"complete");});magSession_=false;break;
    case M::Cancelled:emit(P::Cancel,[&](GunEvent& e){SetGunText(e.reason,GunMagReasonName(st.reason));});
        if(magSession_)emit(P::End,[](GunEvent& e){SetGunText(e.reason,"cancel");});magSession_=false;break;
    case M::Attached:if(magSession_&&st.originalReturns){emit(P::End,[](GunEvent& e){SetGunText(e.reason,"original_return");});magSession_=false;}break;
    default:break;
    }
}
void GunFvrTracker::Shell(GunEventLog& log,const GunWeapon& w,const GunShellProbe& st)noexcept{
    const auto previous=shell_;shell_=st;if(!log.Enabled())return;
    if(st.active&&!shellSession_){shellSession_=true;shellBegunCompleted_=st.completed;auto e=Row(P::Begin,w);SetGunText(e.hand,"left");SetGunText(e.detail,"shell");log.Push(e);}
    if(shellSession_&&st.held&&!previous.held){auto e=Row(P::PouchStart,w);SetGunText(e.hand,"left");log.Push(e);}
    if(shellSession_&&!st.active&&!st.held){shellSession_=false;
        const bool done=st.completed>shellBegunCompleted_;
        if(done){auto e=Row(P::Complete,w);e.v[0]=std::int32_t(st.cycle);e.flags|=gun_flag::Cycle;log.Push(e);}
        else {auto e=Row(P::Cancel,w);SetGunText(e.reason,"consumer");log.Push(e);}
        auto e=Row(P::End,w);SetGunText(e.reason,done?"complete":"cancel");log.Push(e);
    }
}
void GunFvrTracker::Seat(GunEventLog& log,const GunWeapon& w,std::uint64_t cycle,int credits)noexcept{
    if(!log.Enabled())return;auto e=Row(P::Seat,w);e.v[0]=std::int32_t(cycle);e.v[1]=credits;e.flags|=gun_flag::Cycle|gun_flag::Value0;log.Push(e);
}
void GunFvrTracker::Credit(GunEventLog& log,const GunWeapon& w,std::uint64_t cycle,int loaded,int reserve)noexcept{
    if(!log.Enabled())return;auto e=Row(P::Credit,w);e.v[0]=loaded;e.v[1]=reserve;e.v[2]=std::int32_t(cycle);e.flags|=gun_flag::Cycle;log.Push(e);
}
void GunFvrTracker::Claims(GunEventLog& log,const GunWeapon& w,const std::array<std::uint8_t,2>& kinds,const std::array<std::uint64_t,2>& tokens)noexcept{
    static constexpr std::array<std::string_view,7> names{"none","gun_hold","weapon_support","sight","mechanism","ammo_object","body_inventory"};
    for(unsigned h=0;h<2;++h){
        if(kinds[h]==claimKinds_[h]&&tokens[h]==claimTokens_[h])continue;
        if(log.Enabled()){auto e=Row(P::Claim,w);SetGunText(e.hand,h?"right":"left");
            const auto kind=kinds[h]?kinds[h]:claimKinds_[h];SetGunText(e.detail,kind<names.size()?names[kind]:"unknown");
            e.wide=std::int64_t(kinds[h]?tokens[h]:claimTokens_[h]);e.flags|=gun_flag::Wide;
            SetGunText(e.reason,!kinds[h]?"released":claimKinds_[h]?"replaced":"acquired");log.Push(e);}
        claimKinds_[h]=kinds[h];claimTokens_[h]=tokens[h];
    }
}
std::string_view GunFeedbackKindName(interaction::FeedbackKind kind)noexcept{
    switch(kind){
    case interaction::FeedbackKind::ReloadCapture:return "reload_capture";
    case interaction::FeedbackKind::ReloadApplied:return "reload_applied";
    case interaction::FeedbackKind::ReloadSeated:return "reload_seated";
    case interaction::FeedbackKind::ReloadGrab:return "reload_grab";
    case interaction::FeedbackKind::ReloadFailed:return "reload_failed";
    case interaction::FeedbackKind::ReloadRack:return "reload_rack";
    case interaction::FeedbackKind::ReloadGunSeat:return "reload_gun_seat";
    case interaction::FeedbackKind::ReloadHover:return "reload_hover";
    case interaction::FeedbackKind::PumpRear:return "pump_rear";
    case interaction::FeedbackKind::PumpGunKnock:return "pump_gun_knock";
    case interaction::FeedbackKind::PumpForward:return "pump_forward";
    case interaction::FeedbackKind::SwitchSelected:return "switch_selected";
    case interaction::FeedbackKind::SwitchRejected:return "switch_rejected";
    case interaction::FeedbackKind::ReloadReady:return "reload_ready";
    case interaction::FeedbackKind::GunFire:return "gun_fire";
    case interaction::FeedbackKind::DryFire:return "dry_fire";
    default:return "unknown";
    }
}
GunEvent GunFvrTracker::HapticRow(const GunWeapon& w,std::string_view kind,unsigned hand,bool sent)const noexcept{
    auto e=Row(P::Haptic,w);SetGunText(e.detail,kind);SetGunText(e.hand,hand?"right":"left");if(sent)e.flags|=gun_flag::Sent;return e;
}
void GunFvrTracker::Haptic(GunEventLog& log,const GunWeapon& w,std::string_view kind,unsigned hand,bool sent)noexcept{
    if(!log.Enabled())return;log.Push(HapticRow(w,kind,hand,sent));
}
GunEvent GunFvrTracker::SettleRow(const GunWeapon& w,std::string_view to)const noexcept{auto e=Row(P::Release,w);SetGunText(e.reason,to);return e;}
void GunFvrTracker::Settle(GunEventLog& log,const GunWeapon& w,std::string_view to)noexcept{if(log.Enabled())log.Push(SettleRow(w,to));}
GunEvent GunFvrTracker::RackRow(const GunWeapon& w,bool byGrip)const noexcept{auto e=Row(P::Rack,w);if(byGrip)e.flags|=gun_flag::ByGrip;return e;}
void GunFvrTracker::Rack(GunEventLog& log,const GunWeapon& w,bool byGrip)noexcept{if(log.Enabled())log.Push(RackRow(w,byGrip));}
std::optional<GunEvent> GunFvrTracker::ChamberRow(const GunWeapon& w,const GunChamberProbe& c)noexcept{
    const auto before=chamber_;const bool sameWeapon=w==chamberWeapon_;chamber_=c;chamberWeapon_=w;
    // The model, not the native count, is what this row reports: a loaded-only
    // change with the same flags is ordinary firing and stays silent.
    if(sameWeapon&&before.known==c.known&&before.magazineOut==c.magazineOut&&before.chamberLoaded==c.chamberLoaded&&before.needsRack==c.needsRack)return std::nullopt;
    if(!sameWeapon&&!c.known)return std::nullopt;
    const std::string_view event=!c.known?"reset":!sameWeapon||!before.known?"observe":
        !before.magazineOut&&c.magazineOut?"eject":before.magazineOut&&!c.magazineOut?"seat":
        before.needsRack&&!c.needsRack?"rack":"observe";
    auto e=Row(P::Chamber,w);SetGunText(e.detail,event);SetGunText(e.reason,MagazineDisplayLabel(c));
    e.v[0]=c.loaded;e.v[1]=c.display;e.v[2]=c.chamberLoaded;e.v[3]=c.needsRack;if(c.magazineOut)e.flags|=gun_flag::MagOut;
    return e;
}
void GunFvrTracker::Chamber(GunEventLog& log,const GunWeapon& w,const GunChamberProbe& c)noexcept{
    const auto row=ChamberRow(w,c);if(row&&log.Enabled())log.Push(*row);
}
void GunFvrTracker::Prop(GunEventLog& log,const GunWeapon& w,std::string_view event,std::uint64_t id,
    const std::array<float,3>& position,float floorY)noexcept{
    if(!log.Enabled())return;auto e=Row(P::Prop,w);SetGunText(e.detail,event);
    const auto mm=[](float m){return std::isfinite(m)?std::int32_t(std::clamp(m*1000.f,-2.e9f,2.e9f)):0;};
    e.v[0]=std::int32_t(id);e.v[1]=mm(position[0]);e.v[2]=mm(position[1]);e.v[3]=mm(position[2]);e.v[4]=mm(floorY);log.Push(e);
}
void GunFvrTracker::Tape(GunEventLog& log,const GunWeapon& w,std::string_view reason,int met,int loaded,int reserve,std::string_view name)noexcept{
    if(!log.Enabled())return;auto e=Row(P::Tape,w);SetGunText(e.reason,reason);if(!name.empty())SetGunText(e.detail,name);
    if(met>=0){e.flags|=gun_flag::HasMet;if(met)e.flags|=gun_flag::Met;}
    if(loaded>=0&&reserve>=0){e.v[0]=loaded;e.v[1]=reserve;e.flags|=gun_flag::Ammo;}
    log.Push(e);
}
void GunFvrTracker::Sample(GunEventLog& log,const GunWeapon& w)noexcept{
    if(!log.Enabled()||!SessionActive())return;auto e=Row(P::FvrSample,w);SetGunText(e.hand,"left");SetGunText(e.magPhase,GunMagPhaseName(magPhase_));log.Push(e);
}
}
