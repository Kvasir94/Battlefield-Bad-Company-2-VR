#include "Bc2TestRange.h"
#include "Test.h"
#include <string>
#include <vector>
using namespace fvr::bc2;
using fvr::runtime::RangeSpec;
namespace {
constexpr std::string_view Digest="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const NativeOperationBinding Key{1,2,0x400000,0x10000,{1,2,3,4},{4,4,4,4},{11,12,13,14}};
RangeOperationCapability Record(RangeOperation op){
    return {Key,op,RangeOperationAlgorithm,Digest,Digest,Digest,Digest,true,true,true};
}
std::vector<RangeOperationCapability> AllRecords(){
    std::vector<RangeOperationCapability> out;
    for(unsigned n=0;n<unsigned(RangeOperation::Count);++n)out.push_back(Record(RangeOperation(n)));
    return out;
}
RangeSpec Spec(std::string_view body){
    RangeSpec r;std::string error;
    if(!fvr::runtime::ParseRange(std::string("FVR_BC2_RANGE 1\n")+std::string(body),r,error))std::fprintf(stderr,"spec: %s\n",error.c_str());
    return r;
}
// Fake engine: records every call; observable state is edited by the test.
struct Fake {
    std::vector<std::string> calls;
    RangeDispatch result=RangeDispatch::Accepted;bool restoreOk=true;
    RangeObservation world;
    void Call(std::string c){calls.push_back(std::move(c));}
};
Fake* F(void* c){return static_cast<Fake*>(c);}
RangeHooks Hooks(Fake& f){
    RangeHooks h;h.context=&f;
    h.writeSettings=[](void* c,std::span<const fvr::runtime::RangeSetting> s){F(c)->Call("settings:"+std::to_string(s.size()));return F(c)->result;};
    h.loadLevel=[](void* c,std::string_view l){F(c)->Call("load:"+std::string(l));return F(c)->result;};
    h.skipCinematic=[](void* c){F(c)->Call("skip");return F(c)->result;};
    h.prepareLoadout=[](void* c,std::span<const std::string> w,std::span<const int> s){
        F(c)->Call("prepare:"+std::to_string(w.size())+":"+std::to_string(s.empty()?-9:s[0]));return F(c)->result;};
    h.restoreLoadout=[](void* c){F(c)->Call("restore");return F(c)->restoreOk;};
    h.clearInventory=[](void* c,std::span<const std::string> keep){F(c)->Call("clear:"+std::to_string(keep.size()));return F(c)->result;};
    h.giveWeapon=[](void* c,std::string_view a){F(c)->Call("give:"+std::string(a));return F(c)->result;};
    h.selectWeapon=[](void* c,std::string_view a){F(c)->Call("select:"+std::string(a));return F(c)->result;};
    h.setAmmo=[](void* c,const RangeAmmoRequest& r){F(c)->Call("ammo:"+std::string(r.asset)+(r.full?":full":""));return F(c)->result;};
    h.teleport=[](void* c,const fvr::runtime::RangeTeleport&){F(c)->Call("teleport");return F(c)->result;};
    return h;
}
constexpr std::int64_t Ms=1000000;
struct Clock {
    std::int64_t now=1000*Ms;std::uint64_t seq=0;
    void Tick(TestRange& range,RangeObservation& o,std::int64_t step=16*Ms){now+=step;o.sequence=++seq;o.observedNs=now;range.Tick(o,now);}
};
RangeWeaponObservation W(std::string asset,int loaded=-1,int reserve=-1,int capacity=-1,int reserveCapacity=-1){
    return {std::move(asset),loaded,reserve,capacity,reserveCapacity};
}
bool HasEvent(const std::vector<RangeEvent>& e,RangeStep s,RangeResult r,RangeReason reason=RangeReason::None){
    for(const auto& x:e)if(x.step==s&&x.result==r&&x.reason==reason)return true;
    return false;
}
}
int main(){
    {   // Inert by default: no spec, no hooks, no events.
        TestRange range;Fake f;Clock c;RangeObservation o;o.playerAlive=true;o.owner=1;
        c.Tick(range,o);CHECK(range.Phase()==RangePhase::Inactive&&!range.Active()&&range.TakeEvents().empty());
        range.Stop(c.now);CHECK(range.Phase()==RangePhase::Inactive);
    }
    {   // Admission mirrors the native operation receipts: exact, reviewed, unique, build-bound.
        auto records=AllRecords();
        CHECK(RangeOperationAdmitted(Key,RangeOperation::GiveWeapon,records,Digest));
        CHECK(!RangeOperationAdmitted(Key,RangeOperation::GiveWeapon,records)); // default build: off
        CHECK(!RangeOperationAdmitted(Key,RangeOperation::GiveWeapon,RangeOperationCapabilities,Digest));
        auto wrong=Key;wrong.codeFingerprint[1]++;CHECK(!RangeOperationAdmitted(wrong,RangeOperation::GiveWeapon,records,Digest));
        CHECK(!RangeOperationAdmitted(Key,RangeOperation::Count,records,Digest));
        auto r=Record(RangeOperation::GiveWeapon);r.readbackVerified=false;CHECK(!RangeOperationAdmitted(Key,RangeOperation::GiveWeapon,std::array{r},Digest));
        r=Record(RangeOperation::GiveWeapon);r.algorithm="other";CHECK(!RangeOperationAdmitted(Key,RangeOperation::GiveWeapon,std::array{r},Digest));
        r=Record(RangeOperation::GiveWeapon);CHECK(!RangeOperationAdmitted(Key,RangeOperation::GiveWeapon,std::array{r,r},Digest));
        r=Record(RangeOperation::PrepareLoadout);r.restoreVerified=false; // reversible patch needs a restore receipt
        CHECK(!RangeOperationAdmitted(Key,RangeOperation::PrepareLoadout,std::array{r},Digest));
        r.restoreVerified=true;r.restoreReceiptSha256="";CHECK(!RangeOperationAdmitted(Key,RangeOperation::PrepareLoadout,std::array{r},Digest));
        r=Record(RangeOperation::GiveWeapon);r.restoreVerified=false;r.restoreReceiptSha256="";
        CHECK(RangeOperationAdmitted(Key,RangeOperation::GiveWeapon,std::array{r},Digest));
        const auto none=UnmappedRangeHooks();
        for(unsigned n=0;n<unsigned(RangeOperation::Count);++n)CHECK(!RangeHookBound(none,RangeOperation(n)));
        Fake f;auto h=Hooks(f);h.restoreLoadout=nullptr;CHECK(!RangeHookBound(h,RangeOperation::PrepareLoadout));
    }
    {   // Fallback sp02-xm8 on today's build: manual route, nothing mapped or admitted, still reaches ready.
        TestRange range;Clock c;
        auto spec=Spec("level=Levels/sp_02\nroute=manual\nweapons=XM8_sp_s@Primary1\nclear_inventory=false\nskip_cinematics=true\nready_timeout=300\ntimeout=600\n");
        CHECK(range.Begin(spec,Key,RangeOperationCapabilities,UnmappedRangeHooks(),c.now));
        for(unsigned n=0;n<unsigned(RangeOperation::Count);++n)CHECK(!range.Available(RangeOperation(n)));
        RangeObservation o;o.frontEnd=true;
        for(int n=0;n<10;++n)c.Tick(range,o,1000*Ms);          // menu: a person or launcher picks SP_02
        CHECK(range.Phase()==RangePhase::Running&&range.Step()==RangeStep::AwaitLevel);
        o.frontEnd=false;o.levelLoaded=true;o.level="levels/SP_02";o.cinematic=true;c.Tick(range,o);
        c.Tick(range,o);c.Tick(range,o);c.Tick(range,o);       // cutscene: wait, logged once as unavailable
        CHECK(range.Step()==RangeStep::AwaitPlayer);
        o.cinematic=false;o.playerAlive=true;o.owner=77;
        o.weapons={W("XM8_sp_s"),W("40mmgl"),W("Knife")};o.selected="XM8_sp_s";
        for(int n=0;n<8&&range.Phase()==RangePhase::Running;++n)c.Tick(range,o);
        CHECK(range.Phase()==RangePhase::Ready&&range.Owner()==77&&range.Reason()==RangeReason::None);
        auto events=range.TakeEvents();
        CHECK(HasEvent(events,RangeStep::PrepareLoadout,RangeResult::Skip,RangeReason::Unavailable));
        CHECK(HasEvent(events,RangeStep::SkipCinematic,RangeResult::Wait,RangeReason::Unavailable));
        CHECK(HasEvent(events,RangeStep::Ready,RangeResult::Ready));
        CHECK(events.back().asset=="XM8_sp_s");
        unsigned waits=0;for(const auto& e:events)waits+=e.result==RangeResult::Wait;CHECK(waits==1);
        o.owner=78;c.Tick(range,o);                             // respawn ends the range
        CHECK(range.Phase()==RangePhase::Finished&&range.Reason()==RangeReason::OwnerLost);
        c.Tick(range,o);CHECK(range.Phase()==RangePhase::Finished);
        CHECK(!range.Begin(spec,Key,RangeOperationCapabilities,UnmappedRangeHooks(),c.now)); // one range per process
    }
    {   // Missing weapon without an admitted give: fail closed, name the weapon, call nothing.
        TestRange range;Fake f;Clock c;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=XM8_sp_s,SPAS12_sp\nclear_inventory=false\n"),Key,AllRecords(),Hooks(f),c.now)); // default build digest: nothing admitted
        RangeObservation o;o.levelLoaded=true;o.playerAlive=true;o.owner=5;o.weapons={W("XM8_sp_s")};o.selected="XM8_sp_s";
        for(int n=0;n<8&&range.Active();++n)c.Tick(range,o);
        CHECK(range.Phase()==RangePhase::Failed&&range.Reason()==RangeReason::Unavailable&&range.Step()==RangeStep::GiveWeapon);
        const auto events=range.TakeEvents();CHECK(events.back().asset=="SPAS12_sp");CHECK(f.calls.empty());
    }
    {   // Menu and override routes refuse at Begin when their operations or boot receipt are missing.
        TestRange a;Fake f;CHECK(!a.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\n"),Key,{},Hooks(f),1,Digest)); // default route=menu
        CHECK(a.Phase()==RangePhase::Failed&&a.Reason()==RangeReason::Unavailable&&a.Step()==RangeStep::None);
        const auto onlySettings=std::array{Record(RangeOperation::WriteSettings)};
        TestRange b;CHECK(!b.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\n"),Key,onlySettings,Hooks(f),1,Digest));
        CHECK(b.Reason()==RangeReason::Unavailable); // newSP (LoadLevel) missing
        TestRange o1;CHECK(!o1.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\nroute=override\n"),Key,AllRecords(),Hooks(f),1,Digest));
        CHECK(o1.Reason()==RangeReason::Unavailable);
        RangeBootReceipt unapplied;TestRange o2;
        CHECK(!o2.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\nroute=override\n"),Key,AllRecords(),Hooks(f),1,Digest,&unapplied));
        TestRange d;RangeSpec bad;CHECK(!d.Begin(bad,Key,{},Hooks(f),1,Digest));CHECK(d.Reason()==RangeReason::InvalidSpec);
        CHECK(f.calls.empty());
    }
    {   // Override route: boot stage already queued the settings; the executor only fires newSP.
        TestRange range;Fake f;Clock c;RangeBootReceipt boot{true,8,1};
        CHECK(range.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\nroute=override\nclear_inventory=false\n"),Key,AllRecords(),Hooks(f),c.now,Digest,&boot));
        CHECK(range.Step()==RangeStep::LoadLevel);
        CHECK(HasEvent(range.TakeEvents(),RangeStep::WriteSettings,RangeResult::Ok,RangeReason::Boot));
        RangeObservation o;o.frontEnd=true;c.Tick(range,o);CHECK(f.calls==std::vector<std::string>{"load:Levels/sp_02"});
    }
    {   // Full admitted menu-route flow, in order, every effect proven by a later observation.
        TestRange range;Fake f;Clock c;const auto records=AllRecords();
        auto spec=Spec("level=Levels/sp_02\nweapons=XM8_sp_s@Primary1,SPAS12_sp@Primary2\nselect=SPAS12_sp\n"
            "skip_cinematics=true\nammo.SPAS12_sp=7/24\nteleport=10,0,5\nmenu=false\nai=false\n");
        CHECK(range.Begin(spec,Key,records,Hooks(f),c.now,Digest));
        RangeObservation& o=f.world;
        // Ticks until `call` is the latest hook call (bounded); proves one call per wait.
        const auto until=[&](std::string_view call){
            for(int n=0;n<12;++n){if(!f.calls.empty()&&f.calls.back()==call)return true;c.Tick(range,o);}
            return !f.calls.empty()&&f.calls.back()==call;};
        const auto settle=[&](RangeStep step){for(int n=0;n<12&&range.Step()!=step;++n)c.Tick(range,o);return range.Step()==step;};
        c.Tick(range,o);c.Tick(range,o);CHECK(f.calls.empty()); // waits for the main menu
        o.frontEnd=true;
        CHECK(until("settings:3")); // Client.Level, Client.LoadMenu, Server.IsAiEnabled
        CHECK(until("load:Levels/sp_02"));
        c.Tick(range,o);c.Tick(range,o);CHECK(range.Step()==RangeStep::LoadLevel); // waits for the level
        o.frontEnd=false;o.levelLoaded=true;o.level="Levels/sp_02";
        CHECK(until("prepare:2:0")); // kit rewrite after level load, before spawn
        o.cinematic=true;
        CHECK(until("skip")&&range.Step()==RangeStep::SkipCinematic);
        c.Tick(range,o);c.Tick(range,o);CHECK(range.Step()==RangeStep::SkipCinematic); // not verified while still playing
        o.cinematic=false;CHECK(settle(RangeStep::AwaitPlayer));
        o.playerAlive=true;o.owner=9;o.weapons={W("XM8_sp_s",30,90,30,90),W("M18")};o.selected="XM8_sp_s";
        CHECK(until("clear:2"));
        c.Tick(range,o);c.Tick(range,o);CHECK(range.Step()==RangeStep::ClearInventory); // waits for proof
        o.weapons={W("XM8_sp_s",30,90,30,90)};
        CHECK(until("give:SPAS12_sp"));
        c.Tick(range,o);c.Tick(range,o);CHECK(range.Step()==RangeStep::GiveWeapon&&range.Phase()==RangePhase::Running);
        o.weapons.push_back(W("SPAS12_sp",4,28,7,28));
        CHECK(until("select:SPAS12_sp"));
        o.selected="SPAS12_sp";
        CHECK(until("ammo:SPAS12_sp"));
        o.weapons[1].loaded=7;o.weapons[1].reserve=24;o.hasPosition=true;o.x=0;o.y=0;o.z=0;
        CHECK(until("teleport"));
        c.Tick(range,o);CHECK(range.Phase()==RangePhase::Running);
        o.x=10.2f;o.z=4.9f;
        for(int n=0;n<6&&range.Phase()==RangePhase::Running;++n)c.Tick(range,o);
        CHECK(range.Phase()==RangePhase::Ready);
        const std::vector<std::string> order{"settings:3","load:Levels/sp_02","prepare:2:0","skip","clear:2","give:SPAS12_sp","select:SPAS12_sp","ammo:SPAS12_sp","teleport"};
        CHECK(f.calls==order);
        range.Stop(c.now);CHECK(range.Phase()==RangePhase::Finished&&range.Reason()==RangeReason::Stopped);
        CHECK(f.calls.back()=="restore");range.Stop(c.now);
        unsigned restores=0;for(const auto& s:f.calls)restores+=s=="restore";CHECK(restores==1);
        const auto events=range.TakeEvents();
        CHECK(HasEvent(events,RangeStep::GiveWeapon,RangeResult::Ok)&&HasEvent(events,RangeStep::Restore,RangeResult::Ok));
    }
    {   // Unknown dispatch is never retried; a patched loadout is restored on failure.
        TestRange range;Fake f;Clock c;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=AEK971_sp\n"),Key,AllRecords(),Hooks(f),c.now,Digest));
        RangeObservation o;o.levelLoaded=true;c.Tick(range,o);c.Tick(range,o);CHECK(f.calls.back()=="prepare:1:-1");
        o.playerAlive=true;o.owner=3;o.weapons={W("XM8_sp_s")};o.selected="XM8_sp_s";
        f.result=RangeDispatch::Unknown;
        for(int n=0;n<6&&range.Active();++n)c.Tick(range,o);
        CHECK(range.Phase()==RangePhase::Failed&&range.Reason()==RangeReason::UnknownDispatch&&range.Step()==RangeStep::ClearInventory);
        CHECK(f.calls.back()=="restore");
    }
    {   // Restore failure is reported over the original reason.
        TestRange range;Fake f;Clock c;f.restoreOk=false;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=AEK971_sp\n"),Key,AllRecords(),Hooks(f),c.now,Digest));
        RangeObservation o;o.levelLoaded=true;c.Tick(range,o);c.Tick(range,o);CHECK(f.calls.back()=="prepare:1:-1");
        range.Stop(c.now);CHECK(range.Reason()==RangeReason::RestoreFailed);
    }
    {   // Accepted give that never appears: readback timeout, not an endless wait.
        TestRange range;Fake f;Clock c;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=AEK971_sp\nclear_inventory=false\n"),Key,AllRecords(),Hooks(f),c.now,Digest));
        RangeObservation o;o.levelLoaded=true;o.playerAlive=true;o.owner=3;o.weapons={W("XM8_sp_s")};o.selected="XM8_sp_s";
        for(int n=0;n<10;++n){c.Tick(range,o);}
        CHECK(f.calls.back()=="give:AEK971_sp");
        for(int n=0;n<40&&range.Active();++n)c.Tick(range,o,200*Ms);
        CHECK(range.Phase()==RangePhase::Failed&&range.Reason()==RangeReason::ReadbackTimeout);
        unsigned gives=0;for(const auto& s:f.calls)gives+=s.rfind("give:",0)==0;CHECK(gives==1);
        CHECK(HasEvent(range.TakeEvents(),RangeStep::PrepareLoadout,RangeResult::Skip,RangeReason::PlayerAlreadySpawned));
    }
    {   // NotStarted fails without restore of anything never patched.
        TestRange range;Fake f;Clock c;f.result=RangeDispatch::NotStarted;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=AEK971_sp\n"),Key,AllRecords(),Hooks(f),c.now,Digest));
        RangeObservation o;o.levelLoaded=true;c.Tick(range,o);c.Tick(range,o);CHECK(range.Reason()==RangeReason::NotStarted);
        CHECK(f.calls.size()==1&&f.calls[0]=="prepare:1:-1");
    }
    {   // Stale and replayed observations never advance; the ready deadline still applies.
        TestRange range;Clock c;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=XM8_sp_s\nready_timeout=2\ntimeout=5\n"),Key,{},UnmappedRangeHooks(),c.now));
        RangeObservation o;o.levelLoaded=true;o.playerAlive=true;o.owner=1;o.weapons={W("XM8_sp_s")};o.selected="XM8_sp_s";
        o.sequence=1;o.observedNs=c.now-300*Ms;c.now+=Ms;range.Tick(o,c.now);
        CHECK(range.Step()==RangeStep::AwaitLevel);
        o.observedNs=c.now+Ms;range.Tick(o,c.now);CHECK(range.Step()==RangeStep::AwaitLevel); // from the future
        c.now+=3000*Ms;o.sequence=2;o.observedNs=c.now;range.Tick(o,c.now);
        CHECK(range.Phase()==RangePhase::Failed&&range.Reason()==RangeReason::ReadyTimeout);
    }
    {   // A different level is never accepted; the menu route never reuses a level it did not start.
        TestRange range;Clock c;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=XM8_sp_s\n"),Key,{},UnmappedRangeHooks(),c.now));
        RangeObservation o;o.levelLoaded=true;o.level="Levels/sp_005";c.Tick(range,o);c.Tick(range,o);
        CHECK(range.Phase()==RangePhase::Failed&&range.Reason()==RangeReason::WrongLevel);
        TestRange menu;Fake f;Clock d;
        CHECK(menu.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\n"),Key,AllRecords(),Hooks(f),d.now,Digest));
        RangeObservation m;m.levelLoaded=true;m.level="Levels/sp_02";for(int n=0;n<3;++n)d.Tick(menu,m);
        CHECK(menu.Reason()==RangeReason::LevelAlreadyLoaded&&f.calls.empty());
        TestRange late;Fake g;Clock e;
        CHECK(late.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\n"),Key,AllRecords(),Hooks(g),e.now,Digest));
        RangeObservation l;l.frontEnd=true;e.Tick(late,l);CHECK(g.calls.size()==1);
        l.frontEnd=false;l.levelLoaded=true;l.level="Levels/sp_01";for(int n=0;n<3;++n)e.Tick(late,l);
        CHECK(late.Reason()==RangeReason::LevelAlreadyLoaded&&g.calls.size()==1); // someone else loaded a level first
    }
    {   // newSP loading the wrong level (Client.Level not applied) fails as WrongLevel.
        TestRange range;Fake f;Clock c;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nweapons=XM8_sp_s\n"),Key,AllRecords(),Hooks(f),c.now,Digest));
        RangeObservation o;o.frontEnd=true;
        for(int n=0;n<3;++n){c.Tick(range,o);}
        CHECK(f.calls.back()=="load:Levels/sp_02");
        o.frontEnd=false;o.levelLoaded=true;o.level="Levels/SP_01";c.Tick(range,o);
        CHECK(range.Reason()==RangeReason::WrongLevel);
    }
    {   // Unobservable ammo with an ammo target fails instead of guessing.
        TestRange range;Fake f;Clock c;
        CHECK(range.Begin(Spec("level=Levels/sp_02\nroute=manual\nweapons=XM8_sp_s\nammo=full\n"),Key,AllRecords(),Hooks(f),c.now,Digest));
        RangeObservation o;o.playerAlive=true;o.levelLoaded=true;o.owner=2;o.weapons={W("XM8_sp_s",30,90)};o.selected="XM8_sp_s";
        for(int n=0;n<10&&range.Active();++n)c.Tick(range,o);
        CHECK(range.Phase()==RangePhase::Failed&&range.Reason()==RangeReason::Unobservable&&range.Step()==RangeStep::SetAmmo);
        for(const auto& s:f.calls){CHECK(s.rfind("prepare",0)!=0);}
    }
    {   // Gun event log v1 line.
        RangeEvent e{4,0,RangeStep::GiveWeapon,RangeResult::Ok,RangeReason::None,"SPAS12_sp"};
        CHECK(FormatRangeEvent(e,120,99)=="{\"v\":1,\"tick\":120,\"qpc_us\":99,\"src\":\"fvr\",\"phase\":\"range\",\"serial\":4,"
            "\"step\":\"give_weapon\",\"result\":\"ok\",\"reason\":\"none\",\"weapon\":{\"asset\":\"SPAS12_sp\"}}");
        e.asset="a\"b";e.step=RangeStep::Ready;e.result=RangeResult::Ready;
        CHECK(FormatRangeEvent(e,1,2).find("\"asset\":\"a\\\"b\"")!=std::string::npos);
        e.asset.clear();CHECK(FormatRangeEvent(e,1,2).find("weapon")==std::string::npos);
    }
    {   // Every typed settings key has a reflected field (registry route can write all of them).
        auto spec=Spec("level=Levels/sp_02\nweapons=XM8_sp_s\nspawn=Start\nsavepoint=Cp1\nmenu=false\nskip_intro=true\nfast_startup=true\ncutscenes=false\n"
            "ai=false\ngod=true\ncheckpoints=false\nunlocks=true\ninstant_spawn=true\nunlimited_ammo=true\nunlimited_mags=false\n"
            "kit=1\nteam=1\nfixed_dt=0.02\nscripted_input=false\n");
        const auto settings=fvr::runtime::RangeSettings(spec);CHECK(settings.size()==20);
        for(const auto& s:settings){
            const auto* f=FindRangeSettingField(s.key);
            if(!f){std::fprintf(stderr,"no field for %s\n",s.key.c_str());return 1;}
            CHECK(f->typeInfo==(s.key.starts_with("Server.")?RangeServerSettingsType:s.key.starts_with("Client.")?RangeClientSettingsType:RangeGameSettingsType));
            const auto limit=s.key.starts_with("Server.")?RangeServerSettingsBytes:s.key.starts_with("Client.")?RangeClientSettingsBytes:RangeGameSettingsBytes;
            CHECK(f->offset+f->bytes<=limit);
        }
        CHECK(!FindRangeSettingField("Client.DisableMenuFlow")); // passthrough only, not a typed key
        CHECK(!FindRangeSettingField("Server.Level")&&FindRangeSettingField("Client.Level")->offset==0x5c); // newSP reads Client.Level
    }
    std::puts("Test range executor stays inert by default, fails closed and proves every effect.");return 0;
}
