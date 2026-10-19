#include "Test.h"
#include "Bc2ReloadSoundGate.h"
#include <cstdio>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Ms=1000000;
ReloadSoundCall Call(std::uint8_t tag){ReloadSoundCall c;c.listener=reinterpret_cast<void*>(std::uintptr_t(0x1000));c.guid[0]=tag;c.callback=7;c.soldier=0x2000;return c;}
ReloadSoundGate::Work Poll(ReloadSoundGate& gate,std::int64_t now){return gate.Poll(now,Call(0).listener,0x2000);}
// The first played clip after an owned physical begin is learned and withheld, then
// replayed on the pull and stopped after a bounded time; other clips pass.
int WithholdReplayStop(){
    ReloadSoundGate g;g.Weapon(42,0x2000);
    CHECK(g.OnStart(Call(1),true,0)&&!g.WorkListener());   // no reload: native, nothing owed
    g.Begin(1000*Ms);CHECK(!g.OnStart(Call(9),true,1010*Ms)&&g.Known()); // reload clip withheld
    CHECK(g.OnStart(Call(3),true,1020*Ms));                // another clip passes
    CHECK(!Poll(g,1500*Ms).replay&&g.WorkListener()==Call(9).listener); // not pulled yet; owed to its listener
    g.Trigger(1600*Ms);auto w=Poll(g,1601*Ms);CHECK(w.replay&&w.call.guid[0]==9&&w.call.callback==7);
    CHECK(!Poll(g,2000*Ms).stop);w=Poll(g,1601*Ms+ReloadSoundGate::StopAfterNs+1);CHECK(w.stop&&w.stopCall.guid[0]==9&&!g.WorkListener());
    // Next reload of the same weapon: the learned clip is withheld again.
    g.Begin(9000*Ms);CHECK(!g.OnStart(Call(9),true,9005*Ms));g.Cancel();CHECK(!Poll(g,9100*Ms).replay);
    const auto s=g.Stats();CHECK(s.learned==1&&s.withheld==2&&s.replayed==1&&s.stopped==1&&s.dropped==1);return 0;}
// Not played (other view), outside the learn window, never pulled (silent
// expiry), or a new weapon: handled without a replay.
int EdgesStaySafe(){
    ReloadSoundGate g;g.Weapon(1,0x2000);g.Begin(0);CHECK(g.OnStart(Call(5),false,1*Ms));
    CHECK(g.OnStart(Call(5),true,ReloadSoundGate::LearnWindowNs+1));
    g.Begin(10000*Ms);CHECK(!g.OnStart(Call(5),true,10001*Ms));
    CHECK(!Poll(g,10001*Ms+ReloadSoundGate::ReplayTimeoutNs+1).replay&&g.Stats().expired==1);
    g.Weapon(2,0x2000);CHECK(!g.Known());return 0;}
int OnlyAdmittedOwnerAndListenerCanDrain(){
    ReloadSoundGate g;g.Weapon(1,0x2000);g.Begin(1000*Ms);
    auto foreign=Call(9);foreign.soldier=0x3000;
    CHECK(g.OnStart(foreign,true,1010*Ms)&&!g.Known());
    CHECK(!g.OnStart(Call(9),true,1020*Ms));g.Trigger(1100*Ms);
    CHECK(!g.Poll(1200*Ms,Call(9).listener,0x3000).replay);
    CHECK(!g.Poll(1200*Ms,reinterpret_cast<void*>(std::uintptr_t(0x4000)),0x2000).replay);
    CHECK(g.Stats().replayed==0&&Poll(g,1200*Ms).replay);
    g.Begin(2000*Ms);foreign=Call(9);foreign.listener=reinterpret_cast<void*>(std::uintptr_t(0x4000));
    CHECK(g.OnStart(foreign,true,2010*Ms)); // another view cannot acquire our sound
    CHECK(!g.OnStart(Call(9),true,2020*Ms));g.Trigger(2100*Ms);
    const auto w=Poll(g,2200*Ms);CHECK(w.replay&&w.stop&&w.call.listener==Call(9).listener);
    return 0;
}
int OwnerChangeRetiresPointerWork(){
    for(bool played:{false,true}){
        ReloadSoundGate g;g.Weapon(1,0x2000);g.Begin(1000*Ms);
        CHECK(!g.OnStart(Call(9),true,1010*Ms));g.Trigger(1100*Ms);
        if(played)CHECK(Poll(g,1200*Ms).replay);
        g.Weapon(1,0x3000);CHECK(!g.Known()&&!g.WorkListener());
        CHECK(!Poll(g,10000*Ms).replay&&!Poll(g,10000*Ms).stop);
        g.Begin(11000*Ms);CHECK(g.OnStart(Call(9),true,11010*Ms)&&!g.Known());
        auto current=Call(3);current.soldier=0x3000;
        CHECK(!g.OnStart(current,true,11020*Ms));g.Trigger(11100*Ms);
        const auto w=g.Poll(11200*Ms,current.listener,current.soldier);
        CHECK(w.replay&&!w.stop&&w.call.soldier==0x3000&&w.call.guid[0]==3);
    }
    // A different weapon on the same soldier also cannot trigger old work.
    ReloadSoundGate g;g.Weapon(1,0x2000);g.Begin(1000*Ms);
    CHECK(!g.OnStart(Call(9),true,1010*Ms));g.Weapon(2,0x2000);g.Trigger(1100*Ms);
    CHECK(!g.WorkListener()&&!Poll(g,1200*Ms).replay);return 0;
}
int CancellationStopsOnlyOwnedReplay(){
    ReloadSoundGate g;g.Weapon(1,0x2000);g.Begin(1000*Ms);
    CHECK(!g.OnStart(Call(9),true,1010*Ms));g.Trigger(1100*Ms);CHECK(Poll(g,1200*Ms).replay);
    g.Cancel();CHECK(!g.Poll(1201*Ms,Call(9).listener,0x3000).stop);
    CHECK(!g.Poll(1201*Ms,reinterpret_cast<void*>(std::uintptr_t(0x4000)),0x2000).stop);
    const auto w=Poll(g,1202*Ms);CHECK(w.stop&&!w.replay&&w.stopCall.soldier==0x2000&&w.stopCall.guid[0]==9);
    CHECK(!g.WorkListener()&&!Poll(g,1203*Ms).stop);
    // Starting another admitted cycle supersedes a withheld start, not its owner.
    g.Begin(2000*Ms);CHECK(!g.OnStart(Call(9),true,2010*Ms));g.Trigger(2100*Ms);
    g.Begin(2200*Ms);CHECK(!g.WorkListener()&&!Poll(g,2201*Ms).replay);
    CHECK(!g.OnStart(Call(9),true,2210*Ms));g.Cancel();CHECK(!g.WorkListener());return 0;
}
}
// The SPAS pump clip: learned only after the fire clip (window from 100 ms),
// replayed on the rack; a rack before the clip lets it through natively.
int PumpClipOnTheStroke(){
    ReloadSoundGate g{ReloadSoundTiming{100*Ms,900*Ms,9000*Ms,4000*Ms}};g.Weapon(7,0x2000);
    g.Begin(1000*Ms);CHECK(g.OnStart(Call(1),true,1002*Ms)&&!g.Known());     // the fire clip: too early, passes
    CHECK(!g.OnStart(Call(2),true,1350*Ms)&&g.Known());                       // the pump clip: withheld
    CHECK(!Poll(g,6000*Ms).replay);                                           // still within the 9 s hold
    g.Trigger(7000*Ms);CHECK(Poll(g,7000*Ms).replay);
    // A second pump within 4 s: the first replay is stopped before the second.
    g.Begin(8000*Ms);CHECK(!g.OnStart(Call(2),true,8350*Ms));g.Trigger(8500*Ms);
    {const auto w=Poll(g,8500*Ms);CHECK(w.replay&&w.stop&&w.stopCall.guid[0]==2&&g.Stats().stopped==1);}
    g.Begin(20000*Ms);g.Trigger(20200*Ms);CHECK(g.OnStart(Call(2),true,20350*Ms)); // early rack: native timing
    CHECK(!Poll(g,20400*Ms).replay&&g.Stats().passedEarly==1&&g.Stats().withheld==2);return 0;}
int main(){
    if(WithholdReplayStop()||EdgesStaySafe()||PumpClipOnTheStroke()||OnlyAdmittedOwnerAndListenerCanDrain()||OwnerChangeRetiresPointerWork()||CancellationStopsOnlyOwnedReplay())return 1;
    std::puts("Bc2ReloadSoundGate: owned learn/replay/stop/cancel/expiry passed; live animation threads unverified.");return 0;
}
