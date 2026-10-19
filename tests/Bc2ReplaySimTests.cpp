#include "Bc2ReplaySim.h"
#include "Test.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace fvr;
namespace {
std::string Field(const std::string& line,const std::string& key){const auto k="\""+key+"\":";const auto at=line.find(k);if(at==std::string::npos)return {};
 auto s=line.substr(at+k.size());if(!s.empty()&&s[0]=='"')return s.substr(1,s.find('"',1)-1);return s.substr(0,s.find_first_of(",}"));}
std::vector<std::string> Phases(const std::vector<std::string>& rows,bool withTape=false){std::vector<std::string> out;
 for(const auto& r:rows){const auto p=Field(r,"phase");if(p=="sample"||(!withTape&&p=="tape"))continue;out.push_back(p+(p=="firing_state"?":"+Field(r,"from")+">"+Field(r,"to"):""));}return out;}
bool Ordered(const std::vector<std::string>& phases,const std::vector<std::string>& expected){std::size_t at=0;
 for(const auto& p:phases)if(at<expected.size()&&p==expected[at])++at;return at==expected.size();}
struct Generated {std::string tape;std::vector<std::string> events;sim::SimSummary summary;bool ok=false;};
Generated Generate(bool carry){Generated g;std::ostringstream out;g.ok=sim::GenerateXm8MagazineTape(out,carry,g.summary,[&](const std::string& l){g.events.push_back(l);});g.tape=out.str();return g;}
bool Replay(const std::string& text,std::vector<std::string>& events,sim::SimSummary& s,sim::SimOptions o={}){
 runtime::InputTape tape;std::istringstream in(text);std::string error;if(!tape.Load(in,error)){std::cerr<<error<<'\n';return false;}
 events.clear();return sim::RunTape(tape,o,[&](const std::string& l){events.push_back(l);},s);}
const std::vector<std::string> MagazineOrder{"firing_state:2>10","begin","firing_state:10>11","freeze_start","unseat","pouch_start","capture","seat",
 "freeze_stop","firing_state:11>12","transfer","credit","firing_state:12>1","firing_state:1>2","complete","end"};
int SyntheticTapeIsAValidRecording(){for(bool carry:{false,true}){const auto g=Generate(carry);
 CHECK(g.ok&&g.summary.probeCompleted&&g.summary.loaded==30&&g.summary.reserve==80&&g.summary.transfers==1);
 runtime::InputTape t;std::istringstream in(g.tape);std::string error;CHECK(t.Load(in,error));
 CHECK(t.TotalTicks()==g.summary.ticks&&t.Samples().size()<t.TotalTicks()); // run-length encoding engaged
 CHECK(t.Header().Value("weapon")==std::optional<std::string>("XM8_sp_s")&&!t.Header().expect.empty());
 CHECK(Ordered(Phases(g.events),MagazineOrder));}return 0;}
int OpenLoopReplayReproducesTheReload(){for(bool carry:{false,true}){const auto g=Generate(carry);std::vector<std::string> events;sim::SimSummary s;
 CHECK(Replay(g.tape,events,s));
 if(s.completed!=1){for(const auto& e:events)std::cerr<<e<<'\n';}
 CHECK(s.preconditionMet&&s.tapeFinished&&s.completed==1&&s.submits==1&&s.transfers==1&&s.loaded==30&&s.reserve==80);
 CHECK(Ordered(Phases(events),MagazineOrder));
 // Replaying a recording of the closed loop must give the closed loop's own event sequence.
 auto live=Phases(g.events),replayed=Phases(events);replayed.resize(std::min(replayed.size(),live.size()));CHECK(replayed==live);
 const auto withTape=Phases(events,true);CHECK(withTape.front()=="tape"&&withTape[1]=="tape");
 for(const auto& e:events)CHECK(e.find("{\"v\":1,\"tick\":")==0&&e.find("\"sim\":true")!=std::string::npos&&e.find("\"qpc_us\":")!=std::string::npos);
 // The tape has no hidden state: two runs are line-identical.
 std::vector<std::string> again;sim::SimSummary s2;CHECK(Replay(g.tape,again,s2)&&again==events);}return 0;}
int TruncatedTapeNeverCredits(){const auto g=Generate(false);
 // Cut before the seat: EOF publishes unavailable input, the consumer cancels, no native transfer.
 std::istringstream lines(g.tape);std::string line,cut;unsigned ticks=0;
 while(std::getline(lines,line)){if(!line.empty()&&line[0]!='#'&&line.rfind("FVR_BC2_INPUT",0)!=0){ticks+=std::stoul(line.substr(0,line.find(' ')));}cut+=line+'\n';if(ticks>=950)break;} // after capture (~949), before seat (~1087)
 CHECK(ticks>=950&&ticks<1080);
 std::vector<std::string> events;sim::SimSummary s;CHECK(Replay(cut,events,s));
 CHECK(s.transfers==0&&s.loaded==27&&s.reserve==83&&s.completed==0);
 const auto phases=Phases(events);CHECK(std::find(phases.begin(),phases.end(),"credit")==phases.end());return 0;}
int PreconditionMustMatch(){const auto g=Generate(false);std::vector<std::string> events;sim::SimSummary s;sim::SimOptions full;full.loaded=30;
 CHECK(!Replay(g.tape,events,s,full)&&!s.preconditionMet&&s.stopReason=="precondition_unmet"&&s.submits==0);
 sim::SimOptions low;low.reserve=10;CHECK(!Replay(g.tape,events,s,low));return 0;}
}
int main(){if(SyntheticTapeIsAValidRecording()||OpenLoopReplayReproducesTheReload()||TruncatedTapeNeverCredits()||PreconditionMustMatch())return 1;
 std::cout<<"BC2 replay sim: synthetic XM8 tape, open-loop replay, truncation and precondition checks passed (mock native, sim only)\n";}
