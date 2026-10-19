#include "Bc2GunEventLog.h"
#include "Bc2ReloadObservation.h"
#include "Test.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#ifdef _WIN32
#include <process.h>
inline int GetCurrentProcessIdPortable(){return _getpid();}
#else
#include <unistd.h>
inline int GetCurrentProcessIdPortable(){return int(getpid());}
#endif
#include <fstream>
#include <limits>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace fvr::bc2;using fvr::interaction::DetachableMagazinePhase;using fvr::interaction::DetachableMagazineReason;
namespace {
std::int64_t fakeUs=0;
std::int64_t FakeClock()noexcept{return fakeUs;}
struct Capture {
    GunEventLog log;std::ostringstream* text=nullptr;
    explicit Capture(std::size_t capacity=4096){auto s=std::make_unique<std::ostringstream>();text=s.get();log.Open(std::move(s),FakeClock,capacity);}
    std::string Lines(){log.Drain();return text->str();}
};
// One tick = 10 ms of QPC; the gather sets the tick before any hook of that update.
void At(GunEventLog& log,std::uint64_t tick){log.SetTick(tick);fakeUs=1000000+std::int64_t(tick)*10000;}
GunCopyState State(std::uint32_t firing,std::uint32_t current,int loaded,int reserve,float timer=0){
    GunCopyState s;s.firing=firing;s.current=current;s.next=current;s.loaded=loaded;s.reserve=reserve;s.timer=timer;return s;}
const GunWeapon Xm8=MakeGunWeapon("XM8_sp_s",0x1a2b4c00);
constexpr std::array<std::uint32_t,3> Firing{0x50000,0x60000,0x70000};
std::size_t Count(const std::string& text,const std::string& needle){std::size_t n=0;for(auto at=text.find(needle);at!=std::string::npos;at=text.find(needle,at+1))++n;return n;}

int ConfigGate(){
    CHECK(ValidGunEventLogConfig(GunEventLogMode::Disabled,0));
    CHECK(ValidGunEventLogConfig(GunEventLogMode::Enabled,9u|0x800u));
    CHECK(ValidGunEventLogConfig(GunEventLogMode::Enabled,9u|0x197800u|0x2000000u|0x400u)); // continuous physical session
    CHECK(!ValidGunEventLogConfig(GunEventLogMode::Enabled,9u));          // no gather without controllers
    CHECK(!ValidGunEventLogConfig(GunEventLogMode::Enabled,8u|0x800u));   // not the tracked stream
    CHECK(!ValidGunEventLogConfig(GunEventLogMode(2),9u|0x800u));
    return 0;}
int FormatterWritesSchemaLines(){
    GunEvent e;e.phase=GunEventPhase::Transfer;e.tick=48211;e.qpcUs=812334100;e.copy=GunCopy::Server;SetGunText(e.asset,"XM8_sp_s");e.weaponId=0x1a2b4c00;
    e.v={3,60,30,33,0,0};std::string line;AppendGunEventLine(line,e);
    CHECK(line=="{\"v\":1,\"tick\":48211,\"qpc_us\":812334100,\"src\":\"native\",\"weapon\":{\"asset\":\"XM8_sp_s\",\"id\":\"0x1a2b4c00\"},\"copy\":\"server\",\"phase\":\"transfer\",\"loaded_before\":3,\"reserve_before\":60,\"loaded_after\":30,\"reserve_after\":33}\n");
    GunEvent d;d.phase=GunEventPhase::Dropped;d.tick=9;d.qpcUs=10;d.wide=17;line.clear();AppendGunEventLine(line,d);
    CHECK(line=="{\"v\":1,\"tick\":9,\"qpc_us\":10,\"src\":\"native\",\"copy\":null,\"phase\":\"dropped\",\"n\":17}\n");
    GunEvent f;f.phase=GunEventPhase::FiringState;f.tick=1;f.qpcUs=2;f.copy=GunCopy::Client1;f.v={10,11,11,0,0,0};f.f=.25f;f.flags=gun_flag::Next|gun_flag::Timer;line.clear();AppendGunEventLine(line,f);
    CHECK(line=="{\"v\":1,\"tick\":1,\"qpc_us\":2,\"src\":\"native\",\"copy\":\"client1\",\"phase\":\"firing_state\",\"from\":10,\"to\":11,\"next\":11,\"flags_a8\":0,\"timer\":0.25}\n");
    GunEvent c;c.phase=GunEventPhase::Cancel;c.tick=3;c.qpcUs=4;SetGunText(c.reason,"claim \"lost\"\n");SetGunText(c.magPhase,"Guided");line.clear();AppendGunEventLine(line,c,true);
    CHECK(line=="{\"v\":1,\"tick\":3,\"qpc_us\":4,\"src\":\"fvr\",\"copy\":null,\"phase\":\"cancel\",\"sim\":true,\"reason\":\"claim \\\"lost\\\"\",\"mag_phase\":\"Guided\"}\n");
    GunEvent s;s.phase=GunEventPhase::FiringState;s.f=std::numeric_limits<float>::infinity();s.flags=gun_flag::Timer;line.clear();AppendGunEventLine(line,s);
    CHECK(line.find("\"timer\":null")!=std::string::npos);
    // Every phase has a wire name and the right producer.
    for(unsigned p=0;p<unsigned(GunEventPhase::Count);++p)CHECK(!GunEventPhaseName(GunEventPhase(p)).empty());
    CHECK(GunEventPhaseName(GunEventPhase::NativeSample)=="sample"&&GunEventPhaseName(GunEventPhase::FvrSample)=="sample");
    CHECK(GunEventPhaseSource(GunEventPhase::Dropped)==GunEventSource::Native&&GunEventPhaseSource(GunEventPhase::Begin)==GunEventSource::Fvr);
    GunText<8> t;SetGunText(t,"abcdefghij");CHECK(GunTextView(t)=="abcdefg");
    return 0;}
int RingCountsOverflowAndKeepsOrder(){
    GunEventRing ring(5);CHECK(ring.Capacity()==8);
    for(unsigned n=0;n<10;++n){GunEvent e;e.tick=n;const bool ok=ring.Push(e);CHECK(ok==(n<8));}
    CHECK(ring.Pushed()==8&&ring.Dropped()==2&&ring.TakeUnreportedDrops()==2&&ring.TakeUnreportedDrops()==0);
    GunEvent out;for(unsigned n=0;n<8;++n){CHECK(ring.Pop(out)&&out.tick==n);}
    CHECK(!ring.Pop(out));
    for(unsigned n=0;n<20;++n){GunEvent e;e.tick=100+n;CHECK(ring.Push(e)&&ring.Pop(out)&&out.tick==100+n);} // wraps
    return 0;}
int RingIsSafeForManyProducers(){
    GunEventRing ring(1024);constexpr unsigned Threads=4,PerThread=20000;
    std::atomic<bool> go=false;std::vector<std::thread> producers;
    for(unsigned t=0;t<Threads;++t)producers.emplace_back([&,t]{while(!go.load())std::this_thread::yield();
        for(unsigned n=0;n<PerThread;++n){GunEvent e;e.tick=std::uint64_t(t)<<32|n;e.v[0]=std::int32_t(n);ring.Push(e);}});
    std::vector<std::uint32_t> next(Threads,0);std::uint64_t popped=0;bool ordered=true;go=true;
    const auto drain=[&]{GunEvent e;while(ring.Pop(e)){++popped;const auto t=unsigned(e.tick>>32),n=std::uint32_t(e.tick);
        if(t>=Threads||n<next[t]||e.v[0]!=std::int32_t(n))ordered=false;else next[t]=n+1;}};
    bool running=true;while(running){drain();running=false;for(auto& p:producers)if(p.joinable()){running=true;break;}
        if(ring.Pushed()+ring.Dropped()==std::uint64_t(Threads)*PerThread){for(auto& p:producers)p.join();running=false;}}
    drain();
    CHECK(ordered); // per-producer order survives; nothing torn or duplicated
    CHECK(popped==ring.Pushed()&&ring.Pushed()+ring.Dropped()==std::uint64_t(Threads)*PerThread);
    return 0;}
int LogIsInertUntilOpenedAndReportsDrops(){
    GunEventLog closed;GunEvent e;e.phase=GunEventPhase::Capture;CHECK(!closed.Enabled()&&!closed.Push(e)&&!closed.Drain());
    GunCopyTracker tracker;tracker.Observe(closed,Xm8,GunCopy::Server,State(1,2,30,90));tracker.Sample(closed,"Attached",100000);
    CHECK(!closed.Open(nullptr,FakeClock));
    Capture cap(4);At(cap.log,7);
    for(unsigned n=0;n<6;++n)cap.log.Push(e);
    const auto text=cap.Lines();
    CHECK(Count(text,"\"phase\":\"capture\"")==4&&Count(text,"\"phase\":\"dropped\",\"n\":2}")==1);
    CHECK(text.find("\"tick\":7,\"qpc_us\":1070000")!=std::string::npos); // stamped from the gather tick and clock
    CHECK(cap.log.Written()==5&&cap.log.DroppedRows()==1&&cap.log.Dropped()==2);
    closed.NoteLost();CHECK(closed.Dropped()==0);
    cap.log.NoteLost();CHECK(Count(cap.Lines(),"\"phase\":\"dropped\",\"n\":1}")==1&&cap.log.Dropped()==3); // a busy tracker's lost row
    cap.log.Push(e);CHECK(cap.log.Close()&&!cap.log.Enabled()&&!cap.log.Push(e)&&Count(cap.text->str(),"capture")==5);
    std::ostringstream report;cap.log.Report(report);CHECK(report.str().find("\"chamber_logged\":false")!=std::string::npos);
    CHECK(!cap.log.Open(std::make_unique<std::ostringstream>(),FakeClock)); // one log per process
    return 0;}
int CopyTrackerTurnsBoundariesIntoRows(){
    Capture cap;GunCopyTracker t;At(cap.log,1);
    t.Observe(cap.log,Xm8,GunCopy::Server,State(Firing[2],2,27,83));                 // baseline: no row
    CHECK(cap.Lines().empty());
    At(cap.log,2);t.Observe(cap.log,Xm8,GunCopy::Server,State(Firing[2],2,26,83));   // shot
    At(cap.log,3);t.Observe(cap.log,Xm8,GunCopy::Server,State(Firing[2],10,26,83,.1f));
    At(cap.log,4);t.Freeze(cap.log,Xm8,GunCopy::Server,true,State(Firing[2],11,26,83,.4f));
    t.Freeze(cap.log,Xm8,GunCopy::Server,true,State(Firing[2],11,26,83,.4f));         // still held: no row
    At(cap.log,9);t.Freeze(cap.log,Xm8,GunCopy::Server,false,State(Firing[2],11,26,83,.4f));
    At(cap.log,10);t.Transfer(cap.log,Xm8,GunCopy::Server,State(Firing[2],12,26,83),State(Firing[2],12,30,79),"magazine");
    t.Commit(cap.log,Xm8,GunCopy::Server,State(Firing[2],12,30,79),State(Firing[2],1,30,79));
    t.Commit(cap.log,Xm8,GunCopy::Server,State(Firing[2],1,30,79),State(Firing[2],1,30,79)); // unchanged: no row
    At(cap.log,11);t.Restore(cap.log,Xm8,GunCopy::Client0,State(Firing[0],2,30,79),State(Firing[0],2,30,79),true); // baseline then receipt
    At(cap.log,12);t.Observe(cap.log,Xm8,GunCopy::Server,State(Firing[2],2,31,79));   // unexplained gain -> step
    t.AutoReloadSuppressed(cap.log,Xm8,GunCopy::Server,State(Firing[2],2,0,79));
    At(cap.log,30);t.Sample(cap.log,"Attached",150000); // client0 last seen 190 ms ago, server 180 ms
    At(cap.log,40);t.Observe(cap.log,MakeGunWeapon("SPAS12_sp",0x2000),GunCopy::Server,State(Firing[2],2,8,20)); // new weapon: baseline
    At(cap.log,41);t.Freeze(cap.log,Xm8,GunCopy::Client1,true,State(Firing[1],11,1,1));
    t.CloseFreezes(cap.log,"trace_end");
    const auto text=cap.Lines();
    std::istringstream lines(text);std::vector<std::string> rows;for(std::string l;std::getline(lines,l);)rows.push_back(l);
    std::vector<std::string> got;for(const auto& r:rows){const auto at=r.find("\"phase\":\"")+9;got.push_back(r.substr(at,r.find('"',at)-at));}
    // Commit reports the transition only; the next boundary the tracker sees carries the state.
    const std::vector<std::string> want{"fire","firing_state","freeze_start","freeze_stop","firing_state","transfer","commit","restore","firing_state","step","auto_reload","sample","sample","freeze_start","freeze_stop"};
    if(got!=want){for(const auto& g:got)std::cerr<<g<<' ';std::cerr<<'\n';}
    CHECK(got==want);
    CHECK(rows[0].find("\"result\":\"shot\",\"loaded\":26,\"reserve\":83,\"consumed\":1")!=std::string::npos);
    CHECK(rows[1].find("\"from\":2,\"to\":10")!=std::string::npos);
    CHECK(rows[3].find("\"reason\":\"release\",\"held_us\":50000")!=std::string::npos);
    CHECK(rows[4].find("\"from\":10,\"to\":12")!=std::string::npos); // the transfer exit reports the state it saw
    CHECK(rows[5].find("\"loaded_before\":26,\"reserve_before\":83,\"loaded_after\":30,\"reserve_after\":79,\"path\":\"magazine\"")!=std::string::npos);
    CHECK(rows[7].find("\"copy\":\"client0\",\"phase\":\"restore\"")!=std::string::npos&&rows[7].find("\"matched\":true")!=std::string::npos);
    CHECK(rows[8].find("\"from\":12,\"to\":2")!=std::string::npos&&rows[9].find("\"state\":2,\"loaded\":31,\"reserve\":79")!=std::string::npos);
    CHECK(rows[10].find("\"suppressed\":true,\"loaded\":0,\"reserve\":79")!=std::string::npos);
    CHECK(rows[11].find("\"copy\":\"client0\"")!=std::string::npos&&rows[11].find("\"stale\":true")!=std::string::npos);
    CHECK(rows[12].find("\"copy\":\"server\"")!=std::string::npos&&rows[12].find("\"shots\":1,\"transfers\":1,\"mag_phase\":\"Attached\",\"stale\":true")!=std::string::npos);
    CHECK(rows[14].find("\"reason\":\"trace_end\"")!=std::string::npos);
    CHECK(text.find("chamber")==std::string::npos); // never game truth
    return 0;}
int FreezeStopSaysCancelWhenTheCopyLeftReload(){
    Capture cap;GunCopyTracker t;At(cap.log,1);
    t.Freeze(cap.log,Xm8,GunCopy::Server,true,State(Firing[2],11,10,50));
    At(cap.log,2);t.Freeze(cap.log,Xm8,GunCopy::Server,false,State(Firing[2],2,10,50));
    CHECK(cap.Lines().find("\"phase\":\"freeze_stop\",\"reason\":\"cancel\"")!=std::string::npos);return 0;}
int FvrTrackerMirrorsTheSimulatorPhases(){
    Capture cap;GunFvrTracker f;using M=DetachableMagazinePhase;
    const auto step=[&](std::uint64_t tick,M phase,DetachableMagazineReason why=DetachableMagazineReason::None,std::uint64_t cycle=0,unsigned returns=0){
        At(cap.log,tick);f.Magazine(cap.log,Xm8,{phase,why,cycle,returns});};
    step(1,M::PreparingRemoval);step(2,M::Pulling);step(3,M::RemovedHeld);step(4,M::WellEmpty);step(5,M::ReplacementHeld);
    CHECK(f.SessionActive());At(cap.log,6);f.Sample(cap.log,Xm8);
    step(7,M::Guided);step(8,M::AwaitingSeat);At(cap.log,9);f.Seat(cap.log,Xm8,4,3);At(cap.log,10);f.Credit(cap.log,Xm8,4,30,80);
    step(11,M::Complete,DetachableMagazineReason::None,4);CHECK(!f.SessionActive());
    step(12,M::Attached);step(13,M::PreparingRemoval);step(14,M::Cancelled,DetachableMagazineReason::ClaimLost);
    step(15,M::Attached);step(16,M::Pulling);step(17,M::AwaitingOriginalReturn);step(18,M::Attached,DetachableMagazineReason::None,0,1);
    const auto text=cap.Lines();
    std::istringstream lines(text);std::vector<std::string> got;for(std::string l;std::getline(lines,l);){const auto at=l.find("\"phase\":\"")+9;got.push_back(l.substr(at,l.find('"',at)-at));}
    const std::vector<std::string> want{"begin","unseat","release","pouch_start","sample","capture","seat","credit","complete","end","begin","cancel","end","begin","end"};
    if(got!=want){for(const auto& g:got)std::cerr<<g<<' ';std::cerr<<'\n';}
    CHECK(got==want);
    CHECK(text.find("\"phase\":\"begin\",\"hand\":\"left\",\"kind\":\"magazine\",\"mag_phase\":\"PreparingRemoval\"")!=std::string::npos);
    CHECK(text.find("\"phase\":\"release\",\"to\":\"pouch\"")!=std::string::npos);
    CHECK(text.find("\"phase\":\"seat\",\"cycle\":4,\"credits\":3")!=std::string::npos);
    CHECK(text.find("\"phase\":\"credit\",\"loaded\":30,\"reserve\":80,\"cycle\":4")!=std::string::npos);
    CHECK(text.find("\"phase\":\"cancel\",\"reason\":\"claim_lost\"")!=std::string::npos);
    CHECK(text.find("\"phase\":\"end\",\"reason\":\"original_return\"")!=std::string::npos);
    CHECK(Count(text,"\"src\":\"fvr\"")==want.size()&&Count(text,"\"copy\":null")==want.size());
    return 0;}
int ShellClaimsTapeAndSwitchRows(){
    Capture cap;GunFvrTracker f;const auto spas=MakeGunWeapon("SPAS12_sp",0x2000);
    At(cap.log,1);PushWeaponSwitch(cap.log,{},spas);PushWeaponSwitch(cap.log,spas,spas);
    At(cap.log,2);f.Shell(cap.log,spas,{true,false,0,0,1});At(cap.log,3);f.Shell(cap.log,spas,{true,true,0,0,1});
    At(cap.log,4);f.Claims(cap.log,spas,{1,5},{11,12});f.Claims(cap.log,spas,{1,5},{11,12});
    At(cap.log,5);f.Seat(cap.log,spas,1,1);f.Credit(cap.log,spas,1,7,19);f.Shell(cap.log,spas,{true,false,1,1,1});
    At(cap.log,6);f.Shell(cap.log,spas,{false,false,1,1,1});f.Claims(cap.log,spas,{1,0},{11,0});
    At(cap.log,7);f.Shell(cap.log,spas,{true,false,1,1,2});At(cap.log,8);f.Shell(cap.log,spas,{false,false,1,1,2});
    At(cap.log,9);f.Haptic(cap.log,spas,"reload_applied",0,true);f.Tape(cap.log,spas,"holding",0,-1,-1,ReloadReserveStageName(3));
    f.Tape(cap.log,spas,"precondition",1,7,19);f.Tape(cap.log,spas,"end");
    const auto text=cap.Lines();
    std::istringstream lines(text);std::vector<std::string> got;for(std::string l;std::getline(lines,l);){const auto at=l.find("\"phase\":\"")+9;got.push_back(l.substr(at,l.find('"',at)-at));}
    const std::vector<std::string> want{"weapon_switch","begin","pouch_start","claim","claim","seat","credit","complete","end","claim","begin","cancel","end","haptic","tape","tape","tape"};
    if(got!=want){for(const auto& g:got)std::cerr<<g<<' ';std::cerr<<'\n';}
    CHECK(text.find("\"phase\":\"tape\",\"reason\":\"holding\",\"name\":\"owner_missing\",\"met\":false")!=std::string::npos);
    CHECK(std::string_view(ReloadReserveStageName(0))=="request_mode_off"&&std::string_view(ReloadReserveStageName(9))=="accepted"&&
          std::string_view(ReloadReserveStageName(10))=="unknown");
    CHECK(got==want);
    CHECK(text.find("\"phase\":\"weapon_switch\",\"from\":null,\"to\":\"SPAS12_sp\"")!=std::string::npos);
    CHECK(text.find("\"hand\":\"right\",\"kind\":\"ammo_object\",\"token\":12,\"result\":\"acquired\"")!=std::string::npos);
    CHECK(text.find("\"hand\":\"right\",\"kind\":\"ammo_object\",\"token\":12,\"result\":\"released\"")!=std::string::npos);
    CHECK(text.find("\"phase\":\"begin\",\"hand\":\"left\",\"kind\":\"shell\"")!=std::string::npos);
    CHECK(text.find("\"phase\":\"cancel\",\"reason\":\"consumer\"")!=std::string::npos);
    CHECK(text.find("\"phase\":\"tape\",\"reason\":\"precondition\",\"met\":true,\"loaded\":7,\"reserve\":19")!=std::string::npos);
    CHECK(text.find("\"phase\":\"haptic\",\"kind\":\"reload_applied\",\"hand\":\"left\",\"sent\":true")!=std::string::npos);
    return 0;}
int SliderSettleRackAndChamberRows(){
    Capture cap;GunFvrTracker f;using M=DetachableMagazinePhase;
    At(cap.log,1);f.Chamber(cap.log,Xm8,{});                                  // unknown first read: silent
    f.Chamber(cap.log,Xm8,{true,false,false,false,0,0});                       // empty gun observed
    At(cap.log,2);f.Chamber(cap.log,Xm8,{true,false,false,false,0,0});         // unchanged: silent
    f.Magazine(cap.log,Xm8,{M::RemovedHeld,DetachableMagazineReason::None,1,0});
    At(cap.log,3);f.Chamber(cap.log,Xm8,{true,true,false,false,0,0});
    f.Magazine(cap.log,Xm8,{M::Guided,DetachableMagazineReason::None,1,0});
    At(cap.log,4);f.Settle(cap.log,Xm8);
    At(cap.log,5);f.Seat(cap.log,Xm8,1,30);f.Haptic(cap.log,Xm8,GunFeedbackKindName(fvr::interaction::FeedbackKind::ReloadSeated),0,true);
    At(cap.log,6);f.Chamber(cap.log,Xm8,{true,false,false,true,30,30});         // seated, waiting on a wired rack
    At(cap.log,7);f.Chamber(cap.log,Xm8,{true,false,false,true,29,29});         // count-only change: silent
    At(cap.log,8);f.Rack(cap.log,Xm8,true);f.Chamber(cap.log,Xm8,{true,false,true,false,30,30});
    At(cap.log,9);f.Chamber(cap.log,Xm8,{});                                   // reset on cancel
    const auto other=MakeGunWeapon("AEK971_sp",0x99);At(cap.log,10);f.Chamber(cap.log,other,{});f.Chamber(cap.log,other,{true,false,true,false,30,30});
    const auto text=cap.Lines();
    std::istringstream lines(text);std::vector<std::string> got;for(std::string l;std::getline(lines,l);){const auto at=l.find("\"phase\":\"")+9;got.push_back(l.substr(at,l.find('"',at)-at));}
    const std::vector<std::string> want{"chamber","begin","unseat","chamber","capture","release","seat","haptic","chamber","rack","chamber","chamber","chamber"};
    if(got!=want){for(const auto& g:got)std::cerr<<g<<' ';std::cerr<<'\n';}
    CHECK(got==want);
    CHECK(text.find("\"phase\":\"chamber\",\"event\":\"observe\",\"loaded\":0,\"display_ammo\":0,\"chamber_loaded\":false,\"needs_rack\":false,\"mag_phase\":\"Attached\"}")!=std::string::npos);
    CHECK(text.find("\"event\":\"eject\",\"loaded\":0,\"display_ammo\":0,\"chamber_loaded\":false,\"needs_rack\":false,\"label\":\"MAGAZINE OUT\",\"mag_phase\":\"RemovedHeld\",\"mag_out\":true")!=std::string::npos);
    CHECK(text.find("\"phase\":\"release\",\"to\":\"well\",\"mag_phase\":\"Guided\"")!=std::string::npos);
    CHECK(text.find("\"phase\":\"haptic\",\"kind\":\"reload_seated\",\"hand\":\"left\",\"sent\":true")!=std::string::npos);
    CHECK(text.find("\"event\":\"seat\",\"loaded\":30,\"display_ammo\":30,\"chamber_loaded\":false,\"needs_rack\":true")!=std::string::npos);
    CHECK(text.find("\"phase\":\"rack\",\"by_grip\":true")!=std::string::npos);
    CHECK(text.find("\"event\":\"rack\",\"loaded\":30,\"display_ammo\":30,\"chamber_loaded\":true,\"needs_rack\":false")!=std::string::npos);
    CHECK(Count(text,"\"event\":\"reset\"")==1&&Count(text,"\"event\":\"observe\"")==2);
    CHECK(text.find("\"needs_rack\":true,\"label\":\"RACK\"")!=std::string::npos&&Count(text,"\"label\"")==2);
    CHECK(GunFeedbackKindName(fvr::interaction::FeedbackKind(15))=="unknown"&&GunFeedbackKindName(fvr::interaction::FeedbackKind::PumpForward)=="pump_forward");
    CHECK(GunFeedbackKindName(fvr::interaction::FeedbackKind::ReloadReady)=="reload_ready");
    CHECK(GunFeedbackKindName(fvr::interaction::FeedbackKind::SwitchSelected)=="switch_selected"&&GunFeedbackKindName(fvr::interaction::FeedbackKind::SwitchRejected)=="switch_rejected");
    // The sim's row builders produce the same rows as the pushing entry points.
    GunFvrTracker g;GunEvent e=g.SettleRow(Xm8);e.tick=4;e.qpcUs=1;std::string line;AppendGunEventLine(line,e,true);
    CHECK(line.find("\"phase\":\"release\",\"sim\":true,\"to\":\"well\"")!=std::string::npos);
    return 0;}
// A complete live-shaped XM8 magazine reload on all three copies, produced
// only through the tracker entry points the DLL hooks call.
std::string Xm8Golden(){
    Capture cap;GunCopyTracker native;GunFvrTracker fvr;using M=DetachableMagazinePhase;
    const auto copies=[&](auto&& f){for(unsigned n=0;n<3;++n)f(GunCopy(n),Firing[n]);};
    const auto magazine=[&](std::uint64_t tick,M phase,std::uint64_t cycle=0){At(cap.log,tick);fvr.Magazine(cap.log,Xm8,{phase,DetachableMagazineReason::None,cycle,0});};
    const auto state=[&](std::uint64_t tick,std::uint32_t s,int loaded,int reserve,float timer){At(cap.log,tick);
        copies([&](GunCopy c,std::uint32_t firing){native.Observe(cap.log,Xm8,c,State(firing,s,loaded,reserve,timer));});};
    const auto sample=[&](std::uint64_t tick){At(cap.log,tick);native.Sample(cap.log,fvr.MagPhase(),500000);fvr.Sample(cap.log,Xm8);};
    At(cap.log,1);PushWeaponSwitch(cap.log,{},Xm8);fvr.Tape(cap.log,Xm8,"precondition",1,27,83);fvr.Tape(cap.log,Xm8,"start");
    state(1,2,27,83,0);sample(30);
    magazine(50,M::PreparingRemoval);state(51,10,27,83,0);magazine(55,M::Pulling);
    At(cap.log,58);fvr.Claims(cap.log,Xm8,{1,4},{21,22});magazine(60,M::RemovedHeld);sample(60);
    state(65,11,27,83,.35f);copies([&](GunCopy c,std::uint32_t firing){native.Freeze(cap.log,Xm8,c,true,State(firing,11,27,83,.35f));});
    magazine(70,M::WellEmpty);At(cap.log,79);fvr.Claims(cap.log,Xm8,{1,5},{21,23});magazine(80,M::ReplacementHeld);sample(90);
    magazine(100,M::Guided);magazine(108,M::AwaitingSeat);At(cap.log,110);fvr.Seat(cap.log,Xm8,1,3);
    At(cap.log,111);copies([&](GunCopy c,std::uint32_t firing){native.Freeze(cap.log,Xm8,c,false,State(firing,11,27,83,.35f));});
    state(112,12,27,83,.4f);
    At(cap.log,113);copies([&](GunCopy c,std::uint32_t firing){native.Transfer(cap.log,Xm8,c,State(firing,12,27,83,.4f),State(firing,12,30,80,.4f),"magazine");});
    At(cap.log,115);fvr.Credit(cap.log,Xm8,1,30,80);At(cap.log,116);fvr.Haptic(cap.log,Xm8,"reload_applied",0,true);fvr.Claims(cap.log,Xm8,{1,0},{21,0});
    state(120,1,30,80,0);state(121,2,30,80,0);magazine(125,M::Complete,1);magazine(126,M::Attached);sample(150);
    At(cap.log,160);fvr.Tape(cap.log,Xm8,"end");
    cap.log.Close();return cap.text->str();
}
int GoldenMatchesCommittedFile(){
    const auto text=Xm8Golden();
    CHECK(text==Xm8Golden()); // deterministic
    const std::string path=std::string(FVR_SOURCE_DIR)+"/tests/data/gun-events-live-xm8.golden.jsonl";
    if(std::getenv("FVR_UPDATE_GOLDEN")){std::ofstream out(path,std::ios::binary);out<<text;CHECK(bool(out));}
    std::ifstream in(path,std::ios::binary);CHECK(bool(in));std::ostringstream committed;committed<<in.rdbuf();
    if(committed.str()!=text)std::cerr<<"golden differs; rerun with FVR_UPDATE_GOLDEN=1 and review the diff\n";
    CHECK(committed.str()==text);
    CHECK(Count(text,"\"phase\":\"transfer\"")==3&&Count(text,"\"phase\":\"seat\"")==1&&Count(text,"\"phase\":\"credit\"")==1);
    return 0;}
}
// KF2 VRSpatialHUD readout labels for the VR chamber model.
int ChamberDisplayLabels(){
    CHECK(MagazineDisplayLabel({}).empty());
    CHECK(MagazineDisplayLabel({true,true,true,false,30,1})=="CHAMBERED");
    CHECK(MagazineDisplayLabel({true,true,false,false,0,0})=="MAGAZINE OUT");
    CHECK(MagazineDisplayLabel({true,false,false,true,30,30})=="RACK");
    CHECK(MagazineDisplayLabel({true,false,true,false,30,30}).empty());
    Capture cap;GunFvrTracker f;At(cap.log,1);f.Chamber(cap.log,Xm8,{true,false,true,false,1,1});
    At(cap.log,2);f.Chamber(cap.log,Xm8,{true,true,true,false,1,1});
    const auto text=cap.Lines();
    CHECK(text.find("\"event\":\"eject\",\"loaded\":1,\"display_ammo\":1,\"chamber_loaded\":true,\"needs_rack\":false,\"label\":\"CHAMBERED\"")!=std::string::npos);
    CHECK(Count(text,"\"label\"")==1);
    return 0;}
// Static RE (not live): locked while state 2 has loaded 0; latched until loaded
// rises or reload states 10-12; last_round when a shot emptied it.
int SlideLockFromExistingReads(){
    Capture cap;GunCopyTracker t;const auto copy=GunCopy::Server;
    At(cap.log,1);t.Observe(cap.log,Xm8,copy,State(Firing[2],2,1,30));     // baseline, one round
    At(cap.log,2);t.Observe(cap.log,Xm8,copy,State(Firing[2],6,0,30));     // last shot
    At(cap.log,3);t.Observe(cap.log,Xm8,copy,State(Firing[2],2,0,30));     // back to ready, empty: locks
    At(cap.log,4);t.Observe(cap.log,Xm8,copy,State(Firing[2],3,0,30));     // dry trigger: stays locked
    At(cap.log,5);t.Observe(cap.log,Xm8,copy,State(Firing[2],2,0,30));
    At(cap.log,6);t.Observe(cap.log,Xm8,copy,State(Firing[2],10,0,30));    // reload begin clears it
    At(cap.log,7);t.Observe(cap.log,Xm8,copy,State(Firing[2],2,0,30));     // aborted to ready, still empty: locks, no last round
    At(cap.log,8);t.Observe(cap.log,Xm8,copy,State(Firing[2],2,30,0));     // rounds back: clears
    const auto text=cap.Lines();
    CHECK(Count(text,"\"phase\":\"slide_lock\"")==4);
    CHECK(text.find("\"copy\":\"server\",\"phase\":\"slide_lock\",\"locked\":true,\"state\":2,\"loaded\":0,\"last_round\":true}")!=std::string::npos);
    CHECK(text.find("\"phase\":\"slide_lock\",\"locked\":false,\"state\":10,\"loaded\":0}")!=std::string::npos);
    CHECK(Count(text,"\"last_round\"")==1);
    CHECK(text.find("\"phase\":\"slide_lock\",\"locked\":false,\"state\":2,\"loaded\":30}")!=std::string::npos);
    // A gun first seen empty is a baseline, not a lock event; a new weapon rebaselines.
    Capture quiet;GunCopyTracker q;At(quiet.log,1);q.Observe(quiet.log,Xm8,copy,State(Firing[0],2,0,0));
    At(quiet.log,2);q.Observe(quiet.log,MakeGunWeapon("AEK971_sp",9),copy,State(Firing[0],2,0,0));
    CHECK(Count(quiet.Lines(),"slide_lock")==0);
    CHECK(GunEventPhaseSource(GunEventPhase::SlideLock)==GunEventSource::Native&&GunEventPhaseName(GunEventPhase::SlideLock)=="slide_lock");
    return 0;}
// Dry fire from the Update entry fire bit: a rising edge on a ready, empty
// copy that stays ready and empty. Held triggers, non-emitting (replay)
// updates, loaded guns and auto-reloads write nothing.
int DryFireFromTriggerEdge(){
    Capture cap;GunCopyTracker t;const auto s=GunCopy::Server;
    const auto empty=State(Firing[2],2,0,0),emptyReserve=State(Firing[2],2,0,90),loaded=State(Firing[2],2,5,90),reload=State(Firing[2],10,0,90);
    At(cap.log,1);t.Trigger(cap.log,Xm8,s,empty,empty,true,true);       // click, fully empty: native click
    At(cap.log,2);t.Trigger(cap.log,Xm8,s,empty,empty,true,true);       // still held: nothing
    At(cap.log,3);t.Trigger(cap.log,Xm8,s,empty,empty,false,true);
    At(cap.log,4);t.Trigger(cap.log,Xm8,s,emptyReserve,emptyReserve,true,true); // VR case: silent natively
    At(cap.log,5);t.Trigger(cap.log,Xm8,s,emptyReserve,emptyReserve,false,true);
    At(cap.log,6);t.Trigger(cap.log,Xm8,s,emptyReserve,emptyReserve,true,false); // no emit
    At(cap.log,7);t.Trigger(cap.log,Xm8,s,loaded,loaded,false,true);t.Trigger(cap.log,Xm8,s,loaded,loaded,true,true); // a shot path
    At(cap.log,8);t.Trigger(cap.log,Xm8,s,emptyReserve,emptyReserve,false,true);t.Trigger(cap.log,Xm8,s,emptyReserve,reload,true,true); // auto-reload
    const auto text=cap.Lines();
    CHECK(Count(text,"\"result\":\"dry\"")==2&&t.DryFires()==2);
    CHECK(text.find("\"result\":\"dry\",\"loaded\":0,\"reserve\":0,\"consumed\":0,\"native_click\":true")!=std::string::npos);
    CHECK(text.find("\"result\":\"dry\",\"loaded\":0,\"reserve\":90,\"consumed\":0,\"native_click\":false")!=std::string::npos);
    return 0;}
// A timed-out trace leaves the DLL (and this log) alive: Close must release
// the file so the rows are visible on disk, and later drains must not write.
int CloseReleasesTheFile(){
    const auto path=std::filesystem::temp_directory_path()/("fvr-gun-close-"+std::to_string(GetCurrentProcessIdPortable())+".jsonl");
    std::filesystem::remove(path);
    {GunEventLog log;CHECK(log.Open(std::make_unique<std::ofstream>(path,std::ios::binary),FakeClock));
     At(log,1);GunEvent e;e.phase=GunEventPhase::Tape;CHECK(log.Push(e));CHECK(log.Close());
     std::ofstream other(path,std::ios::binary|std::ios::app);CHECK(bool(other)); // handle released
     other.close();const auto size=std::filesystem::file_size(path);CHECK(size>0);
     CHECK(!log.Drain()&&log.Written()==1&&std::filesystem::file_size(path)==size);}
    std::filesystem::remove(path);return 0;}
int main(){
    if(ConfigGate()||FormatterWritesSchemaLines()||RingCountsOverflowAndKeepsOrder()||RingIsSafeForManyProducers()||LogIsInertUntilOpenedAndReportsDrops()||
       CopyTrackerTurnsBoundariesIntoRows()||FreezeStopSaysCancelWhenTheCopyLeftReload()||FvrTrackerMirrorsTheSimulatorPhases()||ShellClaimsTapeAndSwitchRows()||
       SliderSettleRackAndChamberRows()||ChamberDisplayLabels()||SlideLockFromExistingReads()||DryFireFromTriggerEdge()||CloseReleasesTheFile()||GoldenMatchesCommittedFile())return 1;
    std::cout<<"BC2 gun-event log: formatter, MPSC ring, drop rows, native/fvr trackers and live-shaped golden passed; DLL hooks and headset unverified\n";
}
