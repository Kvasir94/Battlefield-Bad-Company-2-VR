#include "Bc2ReplaySim.h"
#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2GunEventLog.h"
#include "Bc2PhysicalReload.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ReloadGrip.h"
#include <cstring>
#include <optional>
#include <ostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::interaction::reload_insertion_detail;
namespace fvr::sim {
namespace {
constexpr std::int64_t Ms=1000000;
math::Matrix4 Translation(float x=0,float y=0,float z=0){auto m=Identity();m.values[3]={x,y,z,1};return m;}
math::Matrix4 Matrix(const math::Pose& p){return *InverseRigid(*math::MakeLhViewFromOpenXRPose(p));}
// Same neutral frame the closed-loop probe test starts from.
InputFrame Neutral(std::uint64_t sequence,std::int64_t now){InputFrame in;in.generation=sequence;in.spaceGeneration=7;in.predictedNs=now;
 in.focused=in.headValid=true;for(auto& h:in.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 in.hands[0].grip.position={-.2f,-.4f,-.3f};in.hands[1].grip.position={0,-.25f,-.45f};return in;}
const char* PhaseName(DetachableMagazinePhase p){
 static const char* names[]={"Attached","PreparingRemoval","Pulling","RemovedHeld","WellEmpty","ReplacementHeld","Guided","AwaitingSeat","Complete","Cancelled","AwaitingOriginalReturn"};
 const auto i=unsigned(p);return i<sizeof(names)/sizeof(*names)?names[i]:"Unknown";}
const char* ReasonName(DetachableMagazineReason r){
 static const char* names[]={"none","invalid_config","invalid_input","stale_input","identity_changed","tracking_lost","unverified_native","native_cycle_changed","gun_claim_lost","need_neutral","outside_magazine","hand_unavailable","claim_lost","pose_jump","pull_abandoned","replacement_rejected","native_rejected","explicit","needs_reconciliation","counter_exhausted"};
 const auto i=unsigned(r);return i<sizeof(names)/sizeof(*names)?names[i]:"unknown";}
// Closed loop of tests/Bc2MagazinePhysicalProbeTests.cpp::Loop with an input source
// in place of Input(sequence) and gun-event emission from the mock native side.
struct Sim {
 std::int64_t now=1000*Ms,started=0,submittedAt=0,retiredAt=0;std::uint64_t sequence=0,cycle=0,intent=0;
 bool applied=false,gateTaken=false,ackTaken=false,frozen=false,session=false;unsigned blockedFireTicks=0;
 GunFvrTracker gunFvr;GunEventLog quiet; // never opened: Magazine() only advances mag_phase
 GunWeapon gunWeapon=MakeGunWeapon(Xm8MagazineAsset,0xa0000);
 unsigned starts=0,submits=0,cancels=0,transfers=0;int loaded=27,reserve=83,capacity=30,state=2;
 ReloadHoldIdentity native{};MagazinePhysicalApi api{};std::optional<ManualReloadRequest> unseat;
 std::optional<ReloadMagazineNativeRequest> request;
 std::optional<Bc2MagazinePhysicalReload> consumer;MagazinePackCounters packs{};
 HandInteraction hands;std::optional<HandClaim> gun;TrackedRig rig;MagazineRawContact raw{};
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 math::Matrix4 lastPouch=Translation();
 DetachableMagazinePhase lastPhase=DetachableMagazinePhase::Attached;
 const EventSink* sink=nullptr;std::uint64_t events=0;
 void Row(const char* src,const char* phase,const std::string& fields,bool copy){
  if(!sink||!*sink)return;std::ostringstream o;
  o<<"{\"v\":1,\"tick\":"<<sequence<<",\"qpc_us\":"<<now/1000<<",\"src\":\""<<src<<"\",\"weapon\":{\"asset\":\""<<Xm8MagazineAsset<<"\",\"id\":\"0xa0000\"},\"copy\":"
   <<(copy?"\"server\"":"null")<<",\"phase\":\""<<phase<<"\",\"sim\":true"<<fields<<"}";
  (*sink)(o.str());++events;}
 void Native(const char* phase,const std::string& fields=""){Row("native",phase,fields,true);}
 void Tracked(GunEvent e){if(!sink||!*sink)return;e.tick=sequence;e.qpcUs=now/1000;std::string line;AppendGunEventLine(line,e,true);
  if(!line.empty()&&line.back()=='\n')line.pop_back();(*sink)(line);++events;}
 void Fvr(const char* phase,const std::string& fields=""){Row("fvr",phase,fields+",\"mag_phase\":\""+PhaseName(lastPhase)+"\"",false);}
 void State(int to){if(to==state)return;Native("firing_state",",\"from\":"+std::to_string(state)+",\"to\":"+std::to_string(to));state=to;}
 void Freeze(bool on,const char* reason){if(on==frozen)return;frozen=on;Native(on?"freeze_start":"freeze_stop",on?"":std::string(",\"reason\":\"")+reason+"\"");}
 explicit Sim(const SimOptions& o,const EventSink* events):loaded(o.loaded),reserve(o.reserve),capacity(o.capacity),sink(events){
 native.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};native.firing={0x50000,0x60000,0x70000};
 native.serverPlayer=0x80000;native.serverSoldier=0x90000;native.serverItem=0xa0000;
 meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
 auto& m=meshes->states[0].meshes[0];m.kind=SelectedMeshKind::Xm8;m.address=0x120000;std::memcpy(m.assetPath.data(),Xm8MagazineMesh.data(),Xm8MagazineMesh.size());
 api.context=this;api.clock=[](void* p)noexcept{return static_cast<Sim*>(p)->now;};
 // Mock native: the transfer applies the reserved units 300 ms after submit.
 api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Sim*>(p);
  if(f.submittedAt&&!f.applied&&f.now-f.submittedAt>=300*Ms){const int lb=f.loaded,rb=f.reserve;
   f.loaded+=int(f.request->reservedUnits);f.reserve-=int(f.request->reservedUnits);f.applied=true;++f.transfers;
   f.Native("transfer",",\"loaded_before\":"+std::to_string(lb)+",\"reserve_before\":"+std::to_string(rb)+",\"loaded_after\":"+std::to_string(f.loaded)+",\"reserve_after\":"+std::to_string(f.reserve)+",\"path\":\"mock\"");}
  return Bc2AmmoReserveLease{f.native,100000+f.sequence,f.now,f.now+100*Ms,f.loaded,f.reserve,f.capacity,true,!f.started||(f.retiredAt&&f.now-f.retiredAt>=100*Ms)};};
 api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Sim*>(p)->native;};
 api.start=[](void* p,const ReloadCycleControl& c,const ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{auto& f=*static_cast<Sim*>(p);++f.starts;f.started=f.now;f.cycle=c.cycle;f.unseat=r;
  if(pulse.control!=c||pulse.endNs!=c.observedNs+100000000||pulse.endNs>c.deadlineNs)return MagazineCycleStartResult::NotStarted;
  f.retiredAt=0;f.gateTaken=false;f.ackTaken=false;f.applied=false;f.submittedAt=0;f.request.reset();f.State(10);return MagazineCycleStartResult::Started;};
 api.inspectStart=[](void*,const ReloadHoldIdentity&,std::uint64_t,const std::optional<ReloadMagazineStartupPulse>&)noexcept{return MagazineCycleStartResult::Unknown;};
 api.keep=[](void*,const ReloadCycleControl&)noexcept{return true;};
 api.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineLease>{auto& f=*static_cast<Sim*>(p);
  if(!f.started||f.now-f.started<150*Ms)return {};
  if(f.state==10){f.State(11);f.Freeze(true,"");} // native wait held by the physical reload
  return ReloadMagazineLease{id,cycle,f.sequence,f.now,f.now+100*Ms,f.loaded,f.reserve,f.capacity,true,!f.submittedAt};};
 api.gate=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineGateAcknowledgement>{auto& f=*static_cast<Sim*>(p);
  if(!f.started||f.now-f.started<150*Ms||f.gateTaken)return {};f.gateTaken=true;
  return ReloadMagazineGateAcknowledgement{{f.unseat->id,f.unseat->owner,ReloadOperation::UnseatMagazine,ReloadAcknowledgement::Applied},
   {id,cycle,f.sequence,f.now,f.now+100*Ms,f.loaded,f.reserve,f.capacity,true,true}};};
 api.submit=[](void* p,const ReloadMagazineNativeRequest& r)noexcept{auto& f=*static_cast<Sim*>(p);++f.submits;f.submittedAt=f.now;f.request=r;
  f.Fvr("seat",",\"cycle\":"+std::to_string(f.cycle)+",\"credits\":"+std::to_string(r.reservedUnits));
  f.Freeze(false,"release");f.State(12);return true;};
 api.ack=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineAckEvidence>{auto& f=*static_cast<Sim*>(p);
  if(!f.applied||f.ackTaken)return {};f.ackTaken=true;const auto& r=*f.request;
  f.Fvr("credit",",\"loaded\":"+std::to_string(f.loaded)+",\"reserve\":"+std::to_string(f.reserve)+",\"cycle\":"+std::to_string(cycle));
  return ReloadMagazineAckEvidence{{{r.request.id,r.request.owner,r.request.operation,ReloadAcknowledgement::Applied},id,cycle,f.sequence,900,
   r.heldLease.loaded,r.heldLease.reserve,f.loaded,f.reserve},f.now,f.now+100*Ms,true};};
 // The consumer releases the native hold through cancel on every outcome.
 api.cancel=[](void* p)noexcept{auto& f=*static_cast<Sim*>(p);++f.cancels;f.retiredAt=f.now;f.Freeze(false,f.applied?"release":"cancel");
  if(f.applied&&f.state==12)f.State(1);f.State(2);};
 api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadCycleRetirement>{auto& f=*static_cast<Sim*>(p);
  if(!f.retiredAt)return {};return ReloadCycleRetirement{id,cycle,1000+f.sequence,f.now,f.now+200*Ms,true};};
 consumer.emplace(true,api);
 }
 void Phase(const MagazinePhysicalProbeState& st){
  if(st.phase==lastPhase)return;const auto from=lastPhase;lastPhase=st.phase;
  using P=DetachableMagazinePhase;
  if(!session&&from==P::Attached&&st.phase!=P::Complete&&st.phase!=P::Cancelled){session=true;Fvr("begin",",\"hand\":\"left\",\"kind\":\"magazine\"");}
  switch(st.phase){
   case P::RemovedHeld:Fvr("unseat",",\"hand\":\"left\"");break;
   case P::WellEmpty:Fvr("release",",\"to\":\"pouch\"");break;
   case P::ReplacementHeld:Fvr("pouch_start",",\"hand\":\"left\"");break;
   case P::Guided:Fvr("capture");break;
   case P::Complete:Fvr("complete",",\"cycle\":"+std::to_string(st.cycle));if(session)Fvr("end",",\"reason\":\"complete\"");session=false;break;
   case P::Cancelled:Fvr("cancel",std::string(",\"reason\":\"")+ReasonName(st.reason)+"\"");if(session)Fvr("end",",\"reason\":\"cancel\"");session=false;break;
   case P::Attached:if(session&&st.originalReturns){Fvr("end",",\"reason\":\"original_return\"");session=false;}break;
   default:break;
  }
 }
 // One native gather with the given input (already prepared by tape or probe).
 void Gather(InputFrame in,bool available,bool cancel){
  const auto pouch=available?PhysicalReloadPouchPose(in):std::nullopt;if(pouch)lastPouch=*pouch;
  HandInteractionSample h{{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,5,17,7},sequence,now,now+100*Ms,now,available&&in.focused,
   {available&&in.hands[0].gripTracked,available&&in.hands[1].gripTracked},{in.hands[0].squeeze<=.35f,false}};
  // Same Slider settle latch as Bc2Gameplay::ObserveHandOwnership.
  LatchLoadingHand(h,consumer->LatchesLoadingHand());
  hands.Update(h);HandContactProof proof{{1002,1},sequence,h.deadlineNs,true};
  if(gun)gun=hands.Renew(h,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(h,{h.owner,InteractionHand::Right,HandClaimKind::GunHold,{0xb0000,17},proof,++intent,0}).claim;
  meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+200*Ms;
  MagazinePhysicalSample s;s.nativeOwner=native.owner;s.input=h;s.weapon={0xb0000,17};s.trackingEpoch=7;s.geometrySequence=sequence;
  s.family={{native.owner,s.weapon,0xd0000,0xc0000,2},now,now+100*Ms,true};
  s.bodyFromHand=lastPouch;s.gripPressed=ReloadGripActive(in.hands[0].squeeze,h,hands.Current(InteractionHand::Left));s.cancel=cancel;s.asset=Xm8MagazineAsset;s.meshes=meshes;s.raw=raw;
  if(raw.valid)s.originalHandEvidence=raw.inputEvidence;
  const auto result=consumer->Tick(s,hands,intent);
  // Haptic, settle, rack and chamber rows come from the live GunFvrTracker
  // builders, so sim and live rows share one shape.
  {const auto st=consumer->ProbeState(now);gunFvr.Magazine(quiet,gunWeapon,{st.phase,st.reason,st.cycle,st.originalReturns});}
  for(unsigned n=0;n<result.feedbackCount;++n)Tracked(gunFvr.HapticRow(gunWeapon,GunFeedbackKindName(result.feedback[n].kind),result.feedback[n].hand,true));
  if(result.settleStarted)Tracked(gunFvr.SettleRow(gunWeapon));
  if(result.racked)Tracked(gunFvr.RackRow(gunWeapon,true));
  {const auto c=consumer->Chamber();if(const auto row=gunFvr.ChamberRow(gunWeapon,{c.known,c.magazineOut,c.chamberLoaded,c.needsRack,c.loaded,c.displayAmmo}))Tracked(*row);}
  if(result.blockFire)++blockedFireTicks;
  // Mock renderer receipt (the fixture's own; production reads verified Pack counters).
  if(result.tracking.target&&MagazineTargetFresh(result.tracking,now)){
   ++packs.pairs;packs.copies+=2;const auto role=unsigned(result.tracking.target->role);++packs.rolePairs[role];packs.roleCopies[role]+=2;}
  Phase(consumer->ProbeState(now));
  const auto body=Translation(5,2,3),left=Multiply(Translation(-.2f,-.4f,.3f),body),right=Multiply(Translation(0,-.25f,.45f),body);
  std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
  const auto posed=available?rig.Update({1,2,3,4},in,body,left,right,right,arms):std::nullopt;
  if(posed){raw.valid=true;raw.owner=native.owner;raw.rigFingerprint=Xm8MagazineRig;raw.inputEvidence=h;
   raw.rawLeftWristWorldMeters=posed->left;raw.weaponWorldMeters=posed->weapon;raw.trackingBodyWorldMeters=body;
   raw.nativeMagazineAttached=!started||(applied&&now-submittedAt>600*Ms)||(retiredAt&&now-retiredAt>=100*Ms);
   auto attachment=Translation();attachment.values[1][1]=attachment.values[2][2]=-1; // anatomical wrist
   auto wrist=Multiply(attachment,Multiply(Matrix(in.hands[0].grip),body));wrist.values[3]=posed->left.values[3];raw.rawLeftWristWorldMeters=wrist;}
  if(sequence%30==0){
   Native("sample",",\"loaded\":"+std::to_string(loaded)+",\"reserve\":"+std::to_string(reserve)+",\"cur\":"+std::to_string(state)+",\"next\":"+std::to_string(state)+",\"shots\":0,\"transfers\":"+std::to_string(transfers)+",\"mag_phase\":\""+PhaseName(lastPhase)+"\",\"stale\":"+(available?"false":"true"));
   if(session)Fvr("sample",",\"hand\":\"left\"");
  }
 }
 void Advance(){now+=10*Ms;++sequence;}
 void Summarize(SimSummary& out){const auto st=consumer->ProbeState(now);
  out.ticks=sequence;out.events=events;out.starts=starts;out.submits=submits;out.cancels=cancels;out.transfers=transfers;out.completed=st.completed;
  out.originalReturns=st.originalReturns;out.loaded=loaded;out.reserve=reserve;out.finalPhase=PhaseName(st.phase);}
};
std::string Quote(const std::string& s){std::string o="\"";for(char c:s){if(c=='"'||c=='\\')o+='\\';if(static_cast<unsigned char>(c)>=32)o+=c;}return o+'"';}
}
bool RunTape(runtime::InputTape& tape,const SimOptions& options,const EventSink& events,SimSummary& out){
 out={};Sim f(options,&events);
 out.preconditionMet=runtime::TapePreconditionMet(tape.Header(),{std::string(Xm8MagazineAsset),options.loaded,options.reserve,options.capacity});
 f.Advance();
 if(!out.preconditionMet){f.Fvr("tape",",\"reason\":\"precondition\",\"met\":false");out.stopReason="precondition_unmet";f.Summarize(out);return false;}
 f.Fvr("tape",",\"reason\":\"precondition\",\"met\":true,\"loaded\":"+std::to_string(f.loaded)+",\"reserve\":"+std::to_string(f.reserve));
 f.Fvr("tape",",\"reason\":\"start\"");
 // The step is the tape's gather period; the mock native latencies stay in ns.
 const std::int64_t step=std::int64_t(tape.Header().stepUs)*1000;
 unsigned tail=0;bool endLogged=false;
 for(std::uint64_t n=0;n<options.maxTicks;++n){
  if(n){f.now+=step;++f.sequence;}
  auto t=tape.Next(1);
  InputFrame in=t.frame;in.generation=f.sequence;in.spaceGeneration=7+(t.frame.spaceGeneration-1);in.predictedNs=f.now;
  f.Gather(in,t.available,false);
  if(t.finished&&!endLogged){f.Fvr("tape",std::string(",\"reason\":\"")+(tape.Stopped()?"stop":"end")+"\"");endLogged=true;}
  if(t.finished&&++tail>options.tailTicks)break;
 }
 out.tapeFinished=tape.Finished();f.Summarize(out);out.stopReason=out.tapeFinished?"end":"max_ticks";return true;
}
bool GenerateXm8MagazineTape(std::ostream& stream,bool carry,SimSummary& out,const EventSink& events,bool sliderRelease){
 out={};SimOptions options;Sim f(options,&events);Bc2MagazinePhysicalProbe probe(true,false,carry);probe.ReleaseMidStroke(sliderRelease&&!carry);
 runtime::InputTapeWriter writer;
 if(!writer.Begin(stream,10000,carry?2:1,{
   "weapon=XM8_sp_s loaded<capacity reserve>=30 anchor=world source=synthetic_closed_loop_probe",
   std::string("notes=Bc2MagazinePhysicalProbe")+(carry?"_free_carry":"")+(sliderRelease&&!carry?"_slider_release_mid_stroke":"")+"_10ms_virtual_gathers",
   "expect transfers=1 credits=1 loaded=+fill reserve=-fill no-auto-reload no-fire-mag-out seat_to_transfer_ms<=1500"}))return false;
 for(unsigned n=0;n<3000&&!probe.CancelConsumer();++n){
  f.Advance();auto in=Neutral(f.sequence,f.now);
  probe.Prepare(in,f.native.owner,Xm8MagazineAsset,f.raw,f.consumer->ProbeState(f.now),f.now,f.now+100*Ms,f.now);
  if(!writer.Append(in,0))return false;
  f.Gather(in,true,probe.CancelConsumer());
  probe.Observe(f.consumer->ProbeState(f.now),f.packs,f.now);
 }
 std::ostringstream report;probe.Report(report);
 out.probeCompleted=report.str().find("\"actual_consumer_completed\":true")!=std::string::npos;
 f.Summarize(out);out.stopReason=out.probeCompleted?"probe_done":"probe_failed";
 return writer.Finish()&&out.probeCompleted;
}
void WriteSummary(std::ostream& o,const SimSummary& s){
 o<<"{\"schema\":\"fvr.bc2.replay_sim.v1\",\"sim\":true,\"native_model\":\"mock_fixed_latency\",\"headset_tested\":false"
  <<",\"precondition_met\":"<<(s.preconditionMet?"true":"false")<<",\"tape_finished\":"<<(s.tapeFinished?"true":"false")
  <<",\"stop_reason\":"<<Quote(s.stopReason)<<",\"ticks\":"<<s.ticks<<",\"events\":"<<s.events<<",\"starts\":"<<s.starts<<",\"submits\":"<<s.submits
  <<",\"native_cancels\":"<<s.cancels<<",\"transfers\":"<<s.transfers<<",\"completed\":"<<s.completed<<",\"original_returns\":"<<s.originalReturns
  <<",\"loaded\":"<<s.loaded<<",\"reserve\":"<<s.reserve<<",\"final_mag_phase\":"<<Quote(s.finalPhase)<<"}\n";
}
}
