#include "Bc2InputTapeSource.h"
#include "Bc2InputTapeConfig.h"
#include <cstring>
#include "Test.h"
#include <iostream>
#include <sstream>
#include <vector>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using ipc::ChannelResult;
namespace {
constexpr std::int64_t Freq=10000000; // 10 MHz QPC, as on current Windows
InputFrame Frame(std::uint64_t generation,float squeeze,std::uint64_t space=0x200000001ull){InputFrame f;f.generation=generation;f.spaceGeneration=space;f.predictedNs=std::int64_t(generation)*11111111;
 f.focused=f.headValid=true;for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 f.hands[0].grip.position={-.2f,-.3f-squeeze*.1f,-.4f};f.hands[0].squeeze=squeeze;f.hands[1].grip.position={.2f,-.25f,-.45f};return f;}
std::string Tape(const std::string& header,unsigned rows){std::ostringstream out;runtime::InputTapeWriter w;
 std::vector<std::string> lines;if(!header.empty())lines.push_back(header);w.Begin(out,11111,0,lines);
 for(unsigned i=0;i<rows;++i)w.Append(Frame(i+1,i%2?.8f:0),3);w.Append(Frame(rows+1,0),400);w.Finish();return out.str();}
int ReplayHoldsForPreconditionThenPlays(){InputTapeGatherSource s;std::istringstream in(Tape("weapon=XM8 loaded<capacity reserve>=30",4));std::string error;CHECK(s.Load(in,error));
 InputFrame out;std::int64_t deadline=0;std::int64_t qpc=Freq;
 // While held, the first (idle) frame stays fresh so gameplay keeps publishing the lease the precondition reads.
 CHECK(s.Read(out,deadline,qpc,Freq,std::nullopt)==ChannelResult::Ok&&s.Holding()&&out.generation==1&&deadline==qpc+Freq/10);
 CHECK(s.Read(out,deadline,qpc+=Freq/90,Freq,runtime::TapeLiveState{"XM8_sp_s",30,191,30})==ChannelResult::Ok&&s.Holding()); // full magazine
 CHECK(s.Read(out,deadline,qpc+=Freq/90,Freq,runtime::TapeLiveState{"AEK971_sp",22,191,30})==ChannelResult::Ok&&s.Holding()); // wrong weapon
 CHECK(out.generation==1&&out.hands[0].squeeze==0&&s.Statistics().holdFrames==3&&s.Statistics().ticks==0);
 CHECK(s.Read(out,deadline,qpc+=Freq/90,Freq,runtime::TapeLiveState{"XM8_sp_s",22,191,30})==ChannelResult::Ok&&!s.Holding());
 CHECK(deadline==qpc+Freq/10&&out.generation==1&&out.hands[0].squeeze==0&&ValidInput(out));
 for(unsigned i=1;i<4;++i){CHECK(s.Read(out,deadline,qpc+=Freq/90,Freq,std::nullopt)==ChannelResult::Ok&&out.generation==i+1);}
 CHECK(out.hands[0].squeeze==.8f);
 CHECK(s.Read(out,deadline,qpc+=Freq/90,Freq,std::nullopt)==ChannelResult::Timeout); // recorded 400 ms age: stale
 for(int i=0;i<3;++i)CHECK(s.Read(out,deadline,qpc+=Freq/90,Freq,std::nullopt)==ChannelResult::Timeout); // EOF: unavailable forever
 CHECK(s.Statistics().finished&&s.Statistics().holdGathers==4&&s.Statistics().available==4&&s.Statistics().preconditionMet);
 std::ostringstream report;s.Report(report);CHECK(report.str().find("\"precondition_met\":true")!=std::string::npos);return 0;}
int HoldTimesOutAndStops(){InputTapeGatherSource s;std::istringstream in(Tape("reserve>=500",2));std::string error;CHECK(s.Load(in,error));
 InputFrame out;std::int64_t deadline=0;const runtime::TapeLiveState live{"XM8_sp_s",22,191,30};
 CHECK(s.Read(out,deadline,Freq,Freq,live)==ChannelResult::Ok&&s.Holding());
 CHECK(s.Read(out,deadline,Freq+20*Freq,Freq,live)==ChannelResult::Ok&&s.Holding());
 CHECK(s.Read(out,deadline,Freq+21*Freq,Freq,live)==ChannelResult::Closed&&s.Statistics().preconditionTimedOut);
 CHECK(s.Read(out,deadline,Freq+22*Freq,Freq,runtime::TapeLiveState{"XM8_sp_s",22,600,30})==ChannelResult::Closed); // never resumes
 InputTapeGatherSource bad;std::istringstream junk("FVR_BC2_INPUT 1 11111 0\n1 0\n");CHECK(!bad.Load(junk,error));
 CHECK(bad.Read(out,deadline,Freq,Freq,live)==ChannelResult::Closed);return 0;}
int LeadingUnavailableUsesNeutralRecordedPoseWithoutSkippingTicks(){
 auto original=Frame(2,.8f,1);original.head.position={.1f,1.6f,-.2f};original.referenceHead.position={0,1.5f,0};
 for(auto& hand:original.hands){hand.held=Secondary;hand.trigger=.9f;hand.stickX=.4f;hand.stickY=-.7f;}
 std::ostringstream encoded;runtime::InputTapeWriter writer;
 CHECK(writer.Begin(encoded,11111,0,{"loaded<capacity after_end=idle"}));
 CHECK(writer.Append({},runtime::TapeStaleMs+1,false));CHECK(writer.Append(original,3));CHECK(writer.Finish());
 InputTapeGatherSource source;std::istringstream in(encoded.str());std::string error;CHECK(source.Load(in,error));
 InputFrame out;std::int64_t deadline=0,qpc=Freq;
 for(unsigned n=0;n<3;++n){
  CHECK(source.Read(out,deadline,qpc,Freq,std::nullopt)==ChannelResult::Ok&&source.Holding());
  CHECK(ValidInput(out)&&out.focused&&out.headValid&&deadline==qpc+Freq/10);
  CHECK(out.head.position.y==original.head.position.y&&out.referenceHead.position.y==original.referenceHead.position.y);
  for(unsigned h=0;h<2;++h){
   CHECK(out.hands[h].gripTracked&&out.hands[h].aimTracked&&out.hands[h].active==original.hands[h].active);
   CHECK(out.hands[h].grip.position.y==original.hands[h].grip.position.y);
   CHECK(!out.hands[h].held&&out.hands[h].trigger==0&&out.hands[h].squeeze==0&&out.hands[h].stickX==0&&out.hands[h].stickY==0);
  }
  qpc+=Freq/90;
 }
 CHECK(source.Statistics().ticks==0&&source.Statistics().holdFrames==3);
 CHECK(source.Read(out,deadline,qpc,Freq,runtime::TapeLiveState{"XM8_sp_s",22,191,30})==ChannelResult::Timeout&&!source.Holding());
 CHECK(source.Statistics().ticks==1&&source.Statistics().unavailable==1);
 CHECK(source.Read(out,deadline,qpc+=Freq/90,Freq,std::nullopt)==ChannelResult::Ok&&out.generation==2);
 CHECK(out.hands[0].squeeze==original.hands[0].squeeze&&out.hands[0].grip.position.y==original.hands[0].grip.position.y);
 for(unsigned h=0;h<2;++h){
  CHECK(out.hands[h].held==original.hands[h].held&&out.hands[h].trigger==original.hands[h].trigger);
  CHECK(out.hands[h].stickX==original.hands[h].stickX&&out.hands[h].stickY==original.hands[h].stickY);
 }
 CHECK(source.Read(out,deadline,qpc+=Freq/90,Freq,std::nullopt)==ChannelResult::Ok&&source.Statistics().idleAfterEnd==1);
 CHECK(out.hands[0].squeeze==0&&out.hands[0].trigger==0&&!out.hands[0].held&&out.hands[0].stickY==0);
 return 0;}
// Mirror of Bc2Gameplay.cpp GatherHook freshness, used as the oracle for what the game consumed.
struct Gather {InputFrame cached{};std::int64_t deadline=0;
 std::optional<InputFrame> See(ChannelResult r,const InputFrame& read,std::int64_t d,std::int64_t qpc){
  if(r==ChannelResult::Ok){cached=read;deadline=d;}else if(r!=ChannelResult::Busy){cached={};deadline=0;}
  if(qpc>0&&qpc<deadline&&cached.focused)return cached;return std::nullopt;}};
int RecordedTapeReplaysWhatTheGatherConsumed(){
 auto* stream=new std::ostringstream;InputTapeGatherRecorder rec;CHECK(rec.Open(std::unique_ptr<std::ostream>(stream),{"weapon=XM8_sp_s"}));
 Gather gather;std::vector<std::optional<InputFrame>> consumed;std::int64_t qpc=Freq;std::uint64_t gen=0;
 struct Step {ChannelResult r;float squeeze;std::int64_t publishedAgo;std::uint64_t space;};
 const std::vector<Step> steps{{ChannelResult::Ok,0,Freq/200,1},{ChannelResult::Ok,0,Freq/200,1},{ChannelResult::Busy,0,0,1},{ChannelResult::Ok,.5f,Freq/200,1},
  {ChannelResult::Ok,.9f,Freq/5,1},{ChannelResult::Timeout,0,0,1},{ChannelResult::Ok,.9f,Freq/200,2},{ChannelResult::Ok,.9f,Freq/200,2},{ChannelResult::Ok,.2f,Freq/200,2}};
 for(const auto& st:steps){qpc+=Freq/60;const auto f=Frame(++gen,st.squeeze,st.space);const std::int64_t deadline=st.r==ChannelResult::Ok?qpc-st.publishedAgo+Freq/10:0;
  consumed.push_back(gather.See(st.r,f,deadline,qpc));rec.Observe(st.r,f,deadline,qpc,Freq);if(gen==4)CHECK(rec.Drain());}
 CHECK(rec.Finish()&&rec.Written()==steps.size()&&rec.Dropped()==0);
 runtime::InputTape tape;std::istringstream in(stream->str());std::string error;CHECK(tape.Load(in,error));
 CHECK(tape.Header().Value("source")==std::optional<std::string>("dll_gather_record"));
 for(std::size_t i=0;i<consumed.size();++i){const auto t=tape.Next();
  if(consumed[i]){CHECK(t.available);CHECK(t.frame.hands[0].squeeze==consumed[i]->hands[0].squeeze&&t.frame.hands[0].grip.position.y==consumed[i]->hands[0].grip.position.y);}
  else CHECK(!t.available); // stale (200 ms old) or cleared by Timeout
  if(i==6)CHECK(t.frame.spaceGeneration==2); // recorded space change replays as a new epoch
 }
 CHECK(consumed[2]&&consumed[6]); // Busy kept the cache; the epoch change itself was fresh input
 return 0;}
int OverflowKeepsTickAlignment(){
 auto* stream=new std::ostringstream;InputTapeGatherRecorder rec;CHECK(rec.Open(std::unique_ptr<std::ostream>(stream)));
 std::int64_t qpc=Freq;const unsigned total=InputTapeGatherRecorder::Capacity+10;
 for(unsigned i=0;i<total;++i){qpc+=Freq/90;rec.Observe(ChannelResult::Ok,Frame(i+1,i<InputTapeGatherRecorder::Capacity?0:.7f),qpc+Freq/10,qpc,Freq);}
 CHECK(rec.Dropped()==10);
 CHECK(rec.Drain());qpc+=Freq/90;rec.Observe(ChannelResult::Ok,Frame(total+1,.7f),qpc+Freq/10,qpc,Freq);CHECK(rec.Finish());
 runtime::InputTape tape;std::istringstream in(stream->str());std::string error;CHECK(tape.Load(in,error));
 CHECK(tape.TotalTicks()==total+1);
 for(unsigned i=0;i<InputTapeGatherRecorder::Capacity;++i)CHECK(tape.Next().available);
 for(unsigned i=0;i<10;++i)CHECK(!tape.Next().available);
 CHECK(tape.Next().frame.hands[0].squeeze==.7f);return 0;}
// No libc wide-string calls: the -fshort-wchar suites (NativeProbeConfig's
// 2-byte wchar_t) link glibc's 4-byte wmemset/wcsncpy, which wrote 1040 bytes
// into this 520-byte buffer (stack smashing) and misread the literals.
int ConfigGate(){char16_t path[InputTapePathChars]{};const auto set=[&](const char16_t* p){
    std::size_t n=0;for(;p[n]&&n+1<InputTapePathChars;++n)path[n]=p[n];for(;n<InputTapePathChars;++n)path[n]=0;};
 constexpr std::uint32_t stream=9u|0x800u|0x1000u|0x2000u,physical=stream|0x4000u|0x10000u|0x80000u|0x100000u|0x2000000u;
 CHECK(ValidInputTapeConfig(InputTapeMode::Disabled,0,0,path,InputTapePathChars));
 set(u"C:\\runs\\a.fvrtape");CHECK(!ValidInputTapeConfig(InputTapeMode::Disabled,0,0,path,InputTapePathChars)); // stray path
 CHECK(ValidInputTapeConfig(InputTapeMode::Replay,physical,3,path,InputTapePathChars));
 CHECK(ValidInputTapeConfig(InputTapeMode::Record,physical|0x400u,3,path,InputTapePathChars));
 CHECK(!ValidInputTapeConfig(InputTapeMode::Replay,physical|0x4000000u,3,path,InputTapePathChars)); // closed-loop probe
 CHECK(!ValidInputTapeConfig(InputTapeMode::Replay,physical,4,path,InputTapePathChars));             // magazine fixture session
 CHECK(!ValidInputTapeConfig(InputTapeMode::Replay,physical|0x1000000u,0,path,InputTapePathChars));
 CHECK(ValidInputTapeConfig(InputTapeMode::Record,physical|0x4000000u,4,path,InputTapePathChars));  // recording observes anything
 CHECK(!ValidInputTapeConfig(InputTapeMode::Replay,9u,0,path,InputTapePathChars));                   // no controller stream
 CHECK(!ValidInputTapeConfig(InputTapeMode(3),physical,0,path,InputTapePathChars));
 set(u"runs\\a.fvrtape");CHECK(!ValidInputTapeConfig(InputTapeMode::Replay,physical,0,path,InputTapePathChars));
 set(u"\\\\server\\share\\a.fvrtape");CHECK(ValidInputTapeConfig(InputTapeMode::Replay,physical,0,path,InputTapePathChars));
 path[InputTapePathChars-1]=u'x';CHECK(!ValidInputTapeConfig(InputTapeMode::Replay,physical,0,path,InputTapePathChars));return 0;}
}
int main(){if(ConfigGate()||ReplayHoldsForPreconditionThenPlays()||HoldTimesOutAndStops()||LeadingUnavailableUsesNeutralRecordedPoseWithoutSkippingTicks()||RecordedTapeReplaysWhatTheGatherConsumed()||OverflowKeepsTickAlignment())return 1;
 std::cout<<"BC2 gather tape source: precondition hold, timeout, recorder/gather equivalence and overflow alignment passed; native/headset unverified\n";}
