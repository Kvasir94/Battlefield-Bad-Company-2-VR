#include "Test.h"
#include "Bc2NativeRack.h"
#ifdef FVR_BC2_NATIVE_RACK_PHASE_HEADER
#include FVR_BC2_NATIVE_RACK_PHASE_HEADER
#endif
using namespace fvr::bc2;
namespace {
fvr::math::Matrix4 Pose(float x,float angle=0){fvr::math::Matrix4 m{};
 m.values[0][0]=m.values[1][1]=std::cos(angle);m.values[0][1]=std::sin(angle);m.values[1][0]=-std::sin(angle);
 m.values[2][2]=m.values[3][3]=1;m.values[3][0]=x;return m;}
struct Fixture {
 std::array<std::string_view,1> bones{"handle"};
 std::array<std::array<std::array<fvr::math::Matrix4,1>,3>,4> parts;
 std::array<std::array<NativeActionPoseFrame,3>,4> frames;
 std::array<NativeActionPoseProfile,4> profiles;
 Fixture(){
  constexpr float ends[5]={-.05f,0,.1f,.2f,0};
  for(unsigned p=0;p<4;++p){for(unsigned i=0;i<3;++i){const float t=float(i)*.5f;
   parts[p][i][0]=Pose(ends[p]+t*(ends[p+1]-ends[p]));auto& f=frames[p][i];
   f.timeSeconds=float(p)*.2f+float(i)*.1f;f.progress=t;f.wristInWeapon=parts[p][i][0];
   f.fingersInWrist.fill(Pose(0));f.partsInWeapon=parts[p][i];}
   profiles[p]={"gun","mesh","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",7,NativeActionPoseKind::Rack,bones,frames[p]};
  }
 }
 NativeRackSample Sample(unsigned n,float x,bool grip=true){return {{1,2,3},n,std::int64_t(n)*10000000,std::int64_t(n)*10000000,Pose(x),true,true,grip};}
};
int FullStroke(){Fixture f;NativeRack rack({f.profiles});CHECK(rack.Valid());
 CHECK(!rack.Update(f.Sample(1,0,false)).ownsHand);
 auto r=rack.Update(f.Sample(2,0));CHECK(r.grabbed&&r.ownsHand&&r.phase==NativeRackPhase::Prep);
 r=rack.Update(f.Sample(3,.05f));CHECK(r.progress>.49f&&r.progress<.51f);
 r=rack.Update(f.Sample(4,.1f));CHECK(r.phase==NativeRackPhase::Pull&&!r.rearStop);
 r=rack.Update(f.Sample(5,.15f));CHECK(r.ownsHand&&!r.rearStop);
 r=rack.Update(f.Sample(6,.2f));CHECK(r.phase==NativeRackPhase::Rear&&r.rearStop);
 CHECK(!rack.Update(f.Sample(6,.2f)).rearStop);
 CHECK(!rack.Update(f.Sample(7,.2f)).rearStop);
 // Release wins even if the packet sequence repeats. The hand is immediately free.
 r=rack.Update(f.Sample(7,.2f,false));CHECK(!r.ownsHand&&r.ownsPart&&r.phase==NativeRackPhase::Return);
 auto later=f.Sample(8,.2f,false);later.nowNs+=100000000;
 r=rack.Update(later);CHECK(r.progress>.5f&&r.progress<.6f);
 later=f.Sample(30,0,false);r=rack.Update(later);CHECK(!r.ownsHand&&!r.ownsPart);
 return 0;}
int Boundaries(){Fixture f;NativeRack rack({f.profiles});rack.Update(f.Sample(1,0,false));rack.Update(f.Sample(2,0));
 auto bad=f.Sample(2,0);bad.identity.weapon=9;auto r=rack.Update(bad);CHECK(r.cancelled&&!r.ownsPart);
 rack.Update(f.Sample(3,0,false));rack.Update(f.Sample(4,0));bad=f.Sample(4,0);bad.focused=false;
 CHECK(rack.Update(bad).cancelled);
 rack.Update(f.Sample(5,0,false));rack.Update(f.Sample(6,0));bad=f.Sample(25,.1f);
 CHECK(rack.Update(bad).cancelled); // tracked gap cannot advance the phase
 rack.Update(f.Sample(26,0,false));rack.Update(f.Sample(27,0));bad=f.Sample(28,0);bad.wristInWeapon=Pose(0,1.0f);
 CHECK(rack.Update(bad).cancelled); // implausible angular step
 return 0;}
int EarlyRelease(){Fixture f;NativeRack rack({f.profiles});rack.Update(f.Sample(1,0,false));rack.Update(f.Sample(2,0));
 rack.Update(f.Sample(3,.05f));auto r=rack.Update(f.Sample(4,.05f,false));
 CHECK(r.phase==NativeRackPhase::CancelPrep&&!r.ownsHand&&r.ownsPart&&r.progress>.49f);
 auto later=f.Sample(15,0,false);r=rack.Update(later);CHECK(!r.ownsPart&&!r.rearStop);
 return 0;}
int HeldForward(){Fixture f;NativeRack rack({f.profiles});rack.Update(f.Sample(1,0,false));rack.Update(f.Sample(2,0));
 rack.Update(f.Sample(3,.1f));rack.Update(f.Sample(4,.2f));
 auto r=rack.Update(f.Sample(5,.17f));CHECK(r.phase==NativeRackPhase::Rear&&r.ownsHand&&!r.rearStop);
 CHECK(std::abs(r.progress-.7f)<.0001f);
 const auto retained=rack.Heartbeat({1,2,3},true,true,true,200000000);
 CHECK(retained.ownsHand&&retained.phase==NativeRackPhase::Rear&&retained.progress==r.progress);
 // Missing geometry and expired leases cannot manufacture the forward stroke.
 auto resumed=f.Sample(21,.17f);r=rack.Update(resumed);
 CHECK(r.phase==NativeRackPhase::Rear&&r.ownsHand&&std::abs(r.progress-.7f)<.0001f);
 r=rack.Update(f.Sample(22,.136f));CHECK(r.phase==NativeRackPhase::Rear&&r.ownsHand);
 CHECK(std::abs(r.progress-.36f)<.0001f); // >35% axial pull still owns the hand
 r=rack.Update(f.Sample(23,.134f));CHECK(r.phase==NativeRackPhase::Return&&!r.ownsHand&&r.ownsPart);
 const auto part=EvaluateNativeActionPose(*r.pose,r.progress,false);CHECK(part);
 CHECK(std::abs(part->partsInWeapon[0].values[3][0]-.134f)<.00001f); // no restart at the rear
 CHECK(r.progress>.3f&&!r.closed&&!r.rearStop);
 auto done=f.Sample(45,.134f);r=rack.Update(done);CHECK(r.closed&&!r.ownsPart);
 CHECK(!rack.Update(f.Sample(46,.134f)).closed); // one closure, held grip cannot reacquire
 return 0;}
int ReleaseAfterHeldForward(){Fixture f;NativeRack rack({f.profiles});rack.Update(f.Sample(1,0,false));rack.Update(f.Sample(2,0));
 rack.Update(f.Sample(3,.1f));rack.Update(f.Sample(4,.2f));
 auto held=rack.Update(f.Sample(5,.17f));CHECK(held.ownsHand&&held.phase==NativeRackPhase::Rear);
 const auto before=EvaluateNativeActionPose(*held.pose,held.progress,false);CHECK(before);
 const auto released=rack.Update(f.Sample(6,.17f,false));CHECK(!released.ownsHand&&released.phase==NativeRackPhase::Return);
 const auto after=EvaluateNativeActionPose(*released.pose,released.progress,false);CHECK(after);
 CHECK(std::abs(after->partsInWeapon[0].values[3][0]-before->partsInWeapon[0].values[3][0])<.00001f);
 CHECK(released.progress>.1f); // joins the original return where the part actually is
 return 0;}
int TwistAndContinuity(){Fixture f;
 for(unsigned i=0;i<3;++i)f.frames[1][i].wristInWeapon=Pose(0,float(i)*.2f);
 const auto twist=ProjectNativeRackPose(f.frames[1],Pose(0,.3f),0);
 CHECK(twist&&std::abs(twist->progress-.75f)<.001f); // rotation advances a stationary handle
 f.profiles[2].clipSha256="cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
 CHECK(!NativeRack({f.profiles}).Valid());
 return 0;}
int TwistAllowsElapsedTrackedMotion(){
 Fixture f;
 for(unsigned i=0;i<3;++i)f.frames[1][i].wristInWeapon=Pose(0,float(i)*.5f);
 NativeRack rack({f.profiles});CHECK(rack.Valid());
 rack.Update(f.Sample(1,0,false));CHECK(rack.Update(f.Sample(2,0)).grabbed);
 auto twist=f.Sample(10,0);twist.wristInWeapon=Pose(0,.8f);
 const auto r=rack.Update(twist);
 // 46 degrees over an 80 ms native frame is a real wrist twist, rather than
 // the old fixed per-frame angular cliff. The same jump over 10 ms rejects.
 CHECK(!r.cancelled&&r.ownsHand&&r.phase==NativeRackPhase::Prep&&std::abs(r.progress-.8f)<.001f);
 NativeRack fast({f.profiles});fast.Update(f.Sample(1,0,false));fast.Update(f.Sample(2,0));
 twist=f.Sample(3,0);twist.wristInWeapon=Pose(0,.8f);CHECK(fast.Update(twist).cancelled);
 return 0;
}
int FastCursorAndSnapshot(){
 Fixture f;std::array<NativeActionPoseFrame,71> dense{};
 for(unsigned i=0;i<dense.size();++i){dense[i]=f.frames[1][0];dense[i].progress=float(i)/70;dense[i].wristInWeapon=Pose(float(i)*.001f);}
 const auto fast=ProjectNativeRackPose(dense,Pose(.035f),0,12,.05f,.08);
 CHECK(fast&&std::abs(fast->progress-.5f)<.0001f); // crossed 35 original samples in one valid gesture
 NativeRack rack({f.profiles});CHECK(rack.Valid());
 f.frames[1][0].wristInWeapon=Pose(1);f.profiles[1].assetName={}; // cannot mutate the live snapshot
 rack.Update(f.Sample(1,0,false));CHECK(rack.Update(f.Sample(2,0)).grabbed);
 auto rollback=f.Sample(1,.05f);CHECK(rack.Update(rollback).cancelled);
 rack.Update(f.Sample(3,0,false));rack.Update(f.Sample(4,0));auto zero=f.Sample(5,0);zero.observedNs=0;
 CHECK(rack.Update(zero).cancelled);
 return 0;
}
#ifdef FVR_BC2_NATIVE_RACK_PHASE_HEADER
int OriginalNativeRoundtrip(){
 const auto& phases=fvr::bc2::generated::xm8_rack_proposal::NativeActionPoses;
 NativeRack rack({phases});CHECK(rack.Valid());NativeRackSample s;
 s.identity={1,2,3};s.focused=s.tracked=true;s.sequence=1;s.observedNs=s.nowNs=10000000;
 s.wristInWeapon=phases[1].frames.front().wristInWeapon;rack.Update(s);
 s.grip=true;++s.sequence;s.observedNs=s.nowNs+=10000000;CHECK(rack.Update(s).grabbed);
 for(unsigned phase=1;phase<=2;++phase)for(std::size_t i=1;i<phases[phase].frames.size();++i){
  s.wristInWeapon=phases[phase].frames[i].wristInWeapon;++s.sequence;s.observedNs=s.nowNs+=10000000;
  const auto r=rack.Update(s);CHECK(!r.cancelled&&r.ownsHand);
  if(i+1<phases[phase].frames.size())CHECK(std::abs(r.progress-phases[phase].frames[i].progress)<.0001f);
  else CHECK(phase==1?r.phase==NativeRackPhase::Pull:r.rearStop);
 }
 return 0;
}
int OriginalNativeHeldForward(){
 const auto& phases=fvr::bc2::generated::xm8_rack_proposal::NativeActionPoses;
 const auto& pull=phases[2];NativeRack rack({phases});CHECK(rack.Valid());NativeRackSample s;
 s.identity={1,2,3};s.focused=s.tracked=true;s.sequence=1;s.observedNs=s.nowNs=10000000;
 s.wristInWeapon=phases[1].frames.front().wristInWeapon;rack.Update(s);
 const auto send=[&](const fvr::math::Matrix4& wrist){s.wristInWeapon=wrist;++s.sequence;s.observedNs=s.nowNs+=10000000;return rack.Update(s);};
 s.grip=true;CHECK(send(s.wristInWeapon).grabbed);
 for(unsigned phase=1;phase<=2;++phase)for(std::size_t i=1;i<phases[phase].frames.size();++i)
  CHECK(!send(phases[phase].frames[i].wristInWeapon).cancelled);
 unsigned bound=0;bool released=false;NativeRackResult r;
 for(std::size_t i=pull.frames.size()-1;i>0;){--i;r=send(pull.frames[i].wristInWeapon);
  CHECK(!r.cancelled&&!r.rearStop&&!r.closed);
  if(r.ownsHand){++bound;CHECK(r.phase==NativeRackPhase::Rear&&std::abs(r.progress-pull.frames[i].progress)<.0001f);
   const auto sampled=EvaluateNativeActionPose(*r.pose,r.progress,false);CHECK(sampled);
   for(unsigned finger=0;finger<15;++finger)for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
    CHECK(std::abs(sampled->fingersInWrist[finger].values[row][col]-pull.frames[i].fingersInWrist[finger].values[row][col])<.00001f);
  }else{
   CHECK(r.phase==NativeRackPhase::Return&&r.progress>.4f);
   const auto before=EvaluateNativeActionPose(pull,pull.frames[i].progress,false);
   const auto after=EvaluateNativeActionPose(*r.pose,r.progress,false);CHECK(before&&after);
   const auto delta=native_rack_detail::Sub(native_rack_detail::Position(before->partsInWeapon[0]),native_rack_detail::Position(after->partsInWeapon[0]));
   const auto angle=native_rack_detail::RotationLog(before->partsInWeapon[0],after->partsInWeapon[0]);
   // Original Pull and Return differ slightly in handle rotation. The join
   // keeps the nearest original Return sample, with <1mm / <1 degree mismatch.
   CHECK(std::sqrt(native_rack_detail::Dot(delta,delta))<.001);
   CHECK(std::sqrt(native_rack_detail::Dot(angle,angle))<.0174533);
   released=true;break;
  }
 }
 CHECK(bound>=3&&released);
 unsigned closed=0;
 for(unsigned i=0;i<60;++i){r=send(pull.frames.back().wristInWeapon);closed+=r.closed;CHECK(!r.ownsHand&&!r.rearStop);}
 CHECK(closed==1);return 0;
}
int OriginalNativeFastForward(){
 const auto& phases=fvr::bc2::generated::xm8_rack_proposal::NativeActionPoses;
 // A fast return may cross the 35% point between samples. Join the actual
 // observed original part pose rather than clamping it back to that threshold.
 for(unsigned target:{0u,2u,7u}){
  NativeRack rack({phases});NativeRackSample s;
  s.identity={1,2,3};s.focused=s.tracked=true;s.sequence=1;s.observedNs=s.nowNs=10000000;
  s.wristInWeapon=phases[1].frames.front().wristInWeapon;rack.Update(s);
  const auto send=[&](const fvr::math::Matrix4& wrist){s.wristInWeapon=wrist;++s.sequence;s.observedNs=s.nowNs+=10000000;return rack.Update(s);};
  s.grip=true;CHECK(send(s.wristInWeapon).grabbed);
  for(unsigned phase=1;phase<=2;++phase)for(std::size_t i=1;i<phases[phase].frames.size();++i)
   CHECK(!send(phases[phase].frames[i].wristInWeapon).cancelled);
  const auto& source=phases[2].frames[target];const auto r=send(source.wristInWeapon);
  CHECK(r.phase==NativeRackPhase::Return&&!r.ownsHand&&!r.cancelled&&!r.closed);
  const auto after=EvaluateNativeActionPose(*r.pose,r.progress,false);CHECK(after);
  const auto delta=native_rack_detail::Sub(native_rack_detail::Position(source.partsInWeapon[0]),native_rack_detail::Position(after->partsInWeapon[0]));
  const auto angle=native_rack_detail::RotationLog(source.partsInWeapon[0],after->partsInWeapon[0]);
  // The original curves are not identical: even a full observed overshoot
  // joins within 3mm / 2 degrees, instead of restarting ~9cm rearward.
  CHECK(std::sqrt(native_rack_detail::Dot(delta,delta))<.003);
  CHECK(std::sqrt(native_rack_detail::Dot(angle,angle))<.0349066);
 }
 return 0;
}
#endif
}
int main(){if(FullStroke()||Boundaries()||EarlyRelease()||HeldForward()||ReleaseAfterHeldForward()||TwistAndContinuity()||TwistAllowsElapsedTrackedMotion()||FastCursorAndSnapshot())return 1;
#ifdef FVR_BC2_NATIVE_RACK_PHASE_HEADER
 if(OriginalNativeRoundtrip()||OriginalNativeHeldForward()||OriginalNativeFastForward())return 1;
#endif
 return 0;}
