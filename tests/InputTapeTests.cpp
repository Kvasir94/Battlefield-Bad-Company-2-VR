#include "fvr/runtime/InputTape.h"
#include "Test.h"
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::runtime;using namespace fvr::interaction;
namespace {
// Active trigger/squeeze/stick/primary (components 1|2|4|8) tracked both poses: flags 3|(15<<2)=63.
std::string Hand(float trigger,unsigned flags=63,unsigned buttons=0,float x=.2f){
 std::ostringstream s;s<<flags<<' '<<buttons<<' '<<trigger<<" 0 0 0  "<<x<<" -0.25 -0.4 0 0 0 1  "<<x<<" -0.25 -0.4 0 0 0 1 ";return s.str();}
std::string Row(unsigned ticks,float left,float right,unsigned age=0,unsigned leftFlags=63,unsigned epoch=0){
 return std::to_string(ticks)+" "+std::to_string(age)+" "+std::to_string(epoch)+" 1 1 0  0 1.7 0 0 0 0 1  0 1.7 0 0 0 0 1  "+Hand(left,leftFlags,0,-.2f)+Hand(right)+"\n";}
bool Load(InputTape& t,const std::string& rows,const std::string& header=""){std::istringstream s("FVR_BC2_INPUT 1 10000 42\n"+header+rows);std::string error;
 const bool ok=t.Load(s,error);if(!ok&&std::getenv("FVR_TAPE_VERBOSE"))std::cerr<<error<<'\n';return ok;}
int SequenceClockAndRunLength(){InputTape t;CHECK(Load(t,Row(1,0,0)+Row(3,1,0)+Row(1,0,1)+Row(1,1,0)+Row(1,0,1)));
 CHECK(t.TotalTicks()==7&&t.Header().seed==42&&t.Header().stepUs==10000);
 const float left[]{0,1,1,1,0,1,0},right[]{0,0,0,0,1,0,1};
 for(unsigned i=0;i<7;++i){const auto f=t.Next(9);
  CHECK(f.available&&f.tick==i+1&&f.frame.generation==i+1&&f.frame.predictedNs==std::int64_t(i+1)*10000000);
  CHECK(f.frame.hands[0].trigger==left[i]&&f.frame.hands[1].trigger==right[i]);
  CHECK(f.frame.hands[0].active&Trigger&&ValidInput(f.frame));}
 CHECK(t.Finished());
 // EOF publishes unavailable input forever: cancellation, never a release.
 for(unsigned i=0;i<3;++i){const auto f=t.Next(9);CHECK(!f.available&&f.finished&&!f.frame.focused&&!f.frame.hands[0].active&&!f.frame.hands[1].gripTracked&&ValidInput(f.frame));}
 return 0;}
int StaleEpochAndAvailability(){InputTape t;
 CHECK(Load(t,Row(1,1,1,101)+Row(1,1,1,100)+Row(1,0,1,0,0)+Row(2,0,0)));
 auto f=t.Next();CHECK(!f.available&&!f.finished); // >100 ms: BC2's IPC deadline
 f=t.Next();CHECK(f.available);                     // exactly 100 ms is still fresh
 f=t.Next();CHECK(f.available&&!f.frame.hands[0].gripTracked&&!f.frame.hands[0].active&&f.frame.hands[1].gripTracked);
 f=t.Next();CHECK(f.available&&f.frame.hands[0].active&Trigger&&f.frame.hands[0].trigger==0);
 CHECK(Load(t,Row(1,0,0)+Row(2,1,1,0,63,3)));
 f=t.Next();CHECK(f.available&&f.frame.spaceGeneration==1);
 f=t.Next();CHECK(f.available&&f.frame.spaceGeneration==4&&f.frame.hands[0].trigger==1); // new epoch = new spaceGeneration
 t.Next();f=t.Next();CHECK(!f.available&&f.frame.spaceGeneration==4);return 0;}  // EOF keeps the last space
int OwnerBindingStopsPermanently(){InputTape t;CHECK(Load(t,Row(5,1,1)));
 CHECK(t.Next(7).available);CHECK(!t.Next(8).available&&t.Stopped());CHECK(!t.Next(7).available);
 CHECK(Load(t,Row(5,1,1)));CHECK(t.Next(7).available&&!t.Stopped());return 0;}
int ParserRejects(){InputTape t;
 CHECK(!Load(t,Row(0,0,0)));CHECK(!Load(t,Row(1,2,0)));CHECK(!Load(t,Row(1,0,0)+"garbage\n"));
 CHECK(!t.Next().available); // a failed parse cannot play a partial tape
 CHECK(!Load(t,Row(1,0,0,0,2048)));
 CHECK(!Load(t,Row(1,1,0,0,3)));            // trigger value while Trigger inactive
 auto bad=Row(1,0,0);bad.replace(bad.find("0 1.7 0 0 0 0 1"),15,"0 1.7 0 0 0 0 0");CHECK(!Load(t,bad));
 CHECK(!Load(t,Row(360001,0,0)));
 CHECK(!Load(t,"1 0 0 0 1 0  0 1.7 0 0 0 0 1  0 1.7 0 0 0 0 1  "+Hand(0)+Hand(0)+"\n")); // unfocused but tracked
 CHECK(!Load(t,""));
 CHECK(!Load(t,Row(1,0,0),"#! anchor=hips\n"));CHECK(!Load(t,Row(1,0,0),"#! =3\n"));
 std::istringstream v2("FVR_BC2_INPUT 2 10000 0\n"+Row(1,0,0));std::string error;CHECK(!t.Load(v2,error)&&!error.empty());
 std::istringstream slow("FVR_BC2_INPUT 1 200000 0\n"+Row(1,0,0));CHECK(!t.Load(slow,error));
 std::istringstream crlf("FVR_BC2_INPUT 1 11111 0\r\n"+Row(2,0,1).insert(Row(2,0,1).size()-1,"\r"));CHECK(t.Load(crlf,error));return 0;}
int HeaderConditions(){InputTape t;
 CHECK(Load(t,Row(1,0,0),"#! weapon=XM8 loaded<capacity reserve>=30 checkpoint=sp_02 anchor=weapon\n#! expect transfers=1 no-auto-reload seat_to_transfer_ms<=1500\n"));
 const auto& h=t.Header();CHECK(h.AnchorWeapon()&&h.Value("checkpoint")==std::optional<std::string>("sp_02"));
 CHECK(h.expect.size()==3&&h.expect[2]=="seat_to_transfer_ms<=1500");
 CHECK(TapePreconditionMet(h,{"XM8_sp_s",22,191,30}));
 CHECK(!TapePreconditionMet(h,{"XM8_sp_s",30,191,30}));  // full magazine: loaded<capacity fails
 CHECK(!TapePreconditionMet(h,{"XM8_sp_s",22,29,30}));
 CHECK(!TapePreconditionMet(h,{"SPAS12",2,40,8}));
 CHECK(TapePreconditionMet(h,{"",22,191,30})&&!TapePreconditionMet(h,{"",30,191,30})); // weapon unobservable: ammo still gates
 CHECK(Load(t,Row(1,0,0),"#! weapon=SPAS loaded<=6 reserve!=0\n"));
 CHECK(TapePreconditionMet(t.Header(),{"spas12_sp",6,24,8})&&!TapePreconditionMet(t.Header(),{"spas12_sp",6,0,8}));
 CHECK(Load(t,Row(1,0,0),"#! loaded=abc\n"));CHECK(!TapePreconditionMet(t.Header(),{"x",1,1,1}));return 0;}
int AnchorRecomposesSupportHand(){InputTape t;CHECK(Load(t,Row(2,0,0),"#! anchor=weapon\n"));
 const math::Pose live{{1,2,3},{0,.7071068f,0,.7071068f}}; // gun hand moved and yawed 90 degrees
 auto f=t.Next(0,&live);CHECK(f.available);
 // Tape: left (-0.2,-0.25,-0.4), right (0.2,-0.25,-0.4): left is 0.4 m along -X of the gun grip.
 const auto& l=f.frame.hands[0].grip.position;CHECK(Near(l.x,1)&&Near(l.y,2)&&Near(l.z,3.4f,.001f));
 f=t.Next();CHECK(Near(f.frame.hands[0].grip.position.x,-.2f));       // no live grip: tape pose
 const math::Pose p{{.1f,.2f,.3f},{.1825742f,.3651484f,.5477226f,.7302967f}};
 const auto round=ComposePose(InversePose(p),p);CHECK(Near(round.position.x,0)&&Near(round.orientation.w,1));return 0;}
int WriterRoundTripsBitExact(){
 InputFrame a{};a.generation=1;a.spaceGeneration=1;a.predictedNs=1;a.focused=a.headValid=true;a.worldUnitsPerMeter=1;
 a.head.position={.0123456789f,1.7f,-.3f};a.referenceHead.position={0,1.6f,0};
 for(auto& h:a.hands){h.gripTracked=h.aimTracked=true;h.active=Components;h.touchActive=TouchComponents;h.touched=IndexTouch;}
 a.hands[0].grip={{-.21f,-.4f,-.3f},{.1825742f,.3651484f,.5477226f,.7302967f}};a.hands[0].squeeze=.6123f;a.hands[1].held=Primary|Secondary;a.hands[1].trigger=1;
 auto b=a;b.hands[0].squeeze=.9f;
 std::ostringstream out;InputTapeWriter w;CHECK(w.Begin(out,11111,7,{"weapon=XM8_sp_s loaded<capacity","expect transfers=1"}));
 for(int i=0;i<3;++i)CHECK(w.Append(a,4));CHECK(w.Append(b,4));CHECK(w.Append(b,250,false));CHECK(w.Append(b,4));CHECK(w.Finish());
 CHECK(w.Ticks()==6&&w.Rows()==4);
 InputTape t;std::istringstream in(out.str());std::string error;const bool loaded=t.Load(in,error);if(!loaded)std::cerr<<error<<"\n"<<out.str();CHECK(loaded);CHECK(t.TotalTicks()==6&&t.Samples().size()==4);
 CHECK(t.Header().expect.size()==1&&t.Header().conditions.size()==2);
 for(int i=0;i<3;++i){const auto f=t.Next();CHECK(f.available);const auto& h=f.frame.hands[0];
  CHECK(h.squeeze==a.hands[0].squeeze&&h.grip.orientation.x==a.hands[0].grip.orientation.x&&f.frame.head.position.x==a.head.position.x);
  CHECK(f.frame.hands[1].held==(Primary|Secondary)&&h.touched==IndexTouch&&h.touchActive==TouchComponents);}
 CHECK(t.Next().frame.hands[0].squeeze==.9f);CHECK(!t.Next().available);CHECK(t.Next().available&&t.Finished());return 0;}
}
int main(){if(SequenceClockAndRunLength()||StaleEpochAndAvailability()||OwnerBindingStopsPermanently()||ParserRejects()||HeaderConditions()||AnchorRecomposesSupportHand()||WriterRoundTripsBitExact())return 1;
 std::cout<<"InputTape: sequence, run-length, stale/epoch, owner, EOF, header, anchor and writer round-trip passed\n";}
