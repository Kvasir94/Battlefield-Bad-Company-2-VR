#include "Test.h"
#include "Bc2NativeActionPose.h"
#include "Bc2NativeRackPose.h"
#include "Bc2ReloadPresentation.h"
#include <limits>
#ifdef FVR_BC2_NATIVE_ACTION_POSE_HEADER
#include FVR_BC2_NATIVE_ACTION_POSE_HEADER
#endif
using namespace fvr::bc2;
namespace {
fvr::math::Matrix4 At(float x=0){fvr::math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3][0]=x;return m;}
struct Fixture {
    std::array<std::string_view,1> names{"handle"};
    std::array<fvr::math::Matrix4,1> closed{At(.1f)},open{At(.2f)};
    std::array<NativeActionPoseFrame,4> frames{};
    NativeActionPoseProfile profile;
    Fixture(){
        for(unsigned n=0;n<4;++n){auto& f=frames[n];f.timeSeconds=float(n)*.1f;f.progress=float(n%2);f.returning=n>=2;
            f.wristInWeapon=At(float(n)*.01f);f.fingersInWrist.fill(At(.01f));f.partsInWeapon=n%2?std::span<const fvr::math::Matrix4>(open):std::span<const fvr::math::Matrix4>(closed);}
        profile={"gun","mesh","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",7,NativeActionPoseKind::Rack,names,frames};
    }
};
int Selection(){
    Fixture f;CHECK(ValidNativeActionPose(f.profile));
    const std::array catalog{f.profile};
    CHECK(FindNativeActionPose(catalog,"gun","mesh",7,NativeActionPoseKind::Rack));
    CHECK(!FindNativeActionPose(catalog,"gun","wrong",7,NativeActionPoseKind::Rack));
    CHECK(!FindNativeActionPose(catalog,"gun","mesh",8,NativeActionPoseKind::Rack));
    CHECK(!FindNativeActionPose(catalog,"gun","mesh",7,NativeActionPoseKind::Reload));
    const std::array duplicate{f.profile,f.profile};CHECK(!FindNativeActionPose(duplicate,"gun","mesh",7,NativeActionPoseKind::Rack));
    CHECK(SampleNativeActionPose(f.profile,.4f,false)==&f.frames[0]);
    CHECK(SampleNativeActionPose(f.profile,.6f,false)==&f.frames[1]);
    CHECK(SampleNativeActionPose(f.profile,.4f,true)==&f.frames[2]); // same progress, original return hand
    CHECK(!SampleNativeActionPose(f.profile,-.1f,false));
    CHECK(!SampleNativeActionPose(f.profile,std::numeric_limits<float>::quiet_NaN(),false));
    f.profile.frames=std::span(f.frames).first(2);CHECK(!SampleNativeActionPose(f.profile,.5f,true));
    f.profile.frames=f.frames;f.frames[2].fingersInWrist[3]={};CHECK(!ValidNativeActionPose(f.profile));
    return 0;
}
int Provenance(){
    Fixture f;f.profile.source=NativeActionPoseSource::EffectiveNativeCapture;
    CHECK(!ValidNativeActionPose(f.profile)); // A clip hash is not capture evidence.
    f.profile.captureSha256=f.profile.clipSha256;f.profile.clipSha256={};
    CHECK(ValidNativeActionPose(f.profile));
    f.profile.source=NativeActionPoseSource::AuthoredClip;CHECK(!ValidNativeActionPose(f.profile));
    f.profile.source=static_cast<NativeActionPoseSource>(255);CHECK(!ValidNativeActionPose(f.profile));
    return 0;
}
int Placement(){
    Fixture f;RigSnapshot rig;rig.names={"root","weapon","handle"};rig.parents={-1,0,1};rig.weaponBone=1;
    const auto wrist=NativeActionWrist(f.frames[1],At(10),100);CHECK(wrist&&std::abs(wrist->values[3][0]-11.f)<1e-5f);
    const auto part=NativeActionPartWrites(f.profile,f.frames[1],rig,At(10),100);CHECK(part&&part->size()==1);
    CHECK((*part)[0].index==2&&std::abs((*part)[0].transform.values[3][0]-30.f)<1e-5f);
    rig.parents[2]=0;CHECK(!NativeActionPartWrites(f.profile,f.frames[1],rig,At(),1)); // outside this weapon
    rig.parents[2]=1;rig.names.push_back("handle");rig.parents.push_back(1);
    CHECK(!NativeActionPartWrites(f.profile,f.frames[1],rig,At(),1)); // ambiguous name
    CHECK(!NativeActionWrist(f.frames[1],At(),0));
    return 0;
}
int InterpolatedTrajectory(){
    Fixture f;
    // Original endpoints rotate 90 degrees while translating; interpolating
    // matrix entries would shrink the basis. Wrist, fingers and part use .25.
    auto turn=At(.04f);turn.values[0]={0,1,0,0};turn.values[1]={-1,0,0,0};
    f.frames[1].wristInWeapon=turn;f.frames[1].fingersInWrist[0]=turn;
    f.open[0]=turn;
    auto p=EvaluateNativeActionPose(f.profile,.25f,false);CHECK(p);
    auto view=p->View();const float angle=.25f*1.57079632679f;
    CHECK(std::abs(view.wristInWeapon.values[0][0]-std::cos(angle))<1e-5f);
    CHECK(std::abs(view.wristInWeapon.values[0][1]-std::sin(angle))<1e-5f);
    CHECK(std::abs(view.wristInWeapon.values[3][0]-.01f)<1e-6f);
    CHECK(std::abs(view.fingersInWrist[0].values[3][0]-.0175f)<1e-6f);
    CHECK(std::abs(view.partsInWeapon[0].values[3][0]-.085f)<1e-6f);
    CHECK(std::abs(view.timeSeconds-.025f)<1e-6f);
    CHECK(EvaluateNativeActionPose(f.profile,0,false)->wristInWeapon.values==f.frames[0].wristInWeapon.values);
    CHECK(EvaluateNativeActionPose(f.profile,1,false)->wristInWeapon.values==f.frames[1].wristInWeapon.values);
    auto moved=std::move(p);CHECK(std::abs(moved->View().partsInWeapon[0].values[3][0]-.085f)<1e-6f);
    f.profile.frames=std::span(f.frames).first(2);CHECK(!EvaluateNativeActionPose(f.profile,.5f,true));
    CHECK(!EvaluateNativeActionPose(f.profile,std::numeric_limits<float>::quiet_NaN(),false));
    f.frames[0].progress=.1f;CHECK(!EvaluateNativeActionPose(f.profile,0,false)); // never extrapolate a partial path
    return 0;
}
int AnchoredPath(){
    Fixture f;auto p=EvaluateNativeActionPose(f.profile,.5f,false);CHECK(p);
    CHECK(AnchorNativeActionPose(*p,f.profile,0,1,At(.5f)));
    CHECK(std::abs(p->partsInWeapon[0].values[3][0]-.45f)<1e-6f);
    CHECK(std::abs(p->wristInWeapon.values[3][0]-.305f)<1e-6f);
    CHECK(p->fingersInWrist[0].values==f.frames[0].fingersInWrist[0].values);
    CHECK(!AnchorNativeActionPose(*p,f.profile,1,1,At()));
    return 0;
}
int SharedNativeTurnaround(){
    Fixture f;f.profile.action=NativeActionPoseKind::Pump;
    // As generated from the original clip, the progress-1 peak belongs only
    // to the outbound branch; the first return frame has already moved ahead.
    f.frames[2].progress=.98f;f.frames[3].progress=0;
    f.frames[1].fingersInWrist[0]=At(.03f);
    const auto peak=EvaluateNativeActionPose(f.profile,1,true);CHECK(peak);
    CHECK(peak->returning&&peak->timeSeconds==f.frames[1].timeSeconds);
    CHECK(peak->wristInWeapon.values==f.frames[1].wristInWeapon.values);
    CHECK(peak->fingersInWrist[0].values==f.frames[1].fingersInWrist[0].values);
    CHECK(peak->partsInWeapon[0].values==f.frames[1].partsInWeapon[0].values);
    const auto nearby=EvaluateNativeActionPose(f.profile,.99f,true);CHECK(nearby);
    CHECK(std::abs(nearby->wristInWeapon.values[3][0]-.015f)<1e-6f);
    CHECK(std::abs(nearby->fingersInWrist[0].values[3][0]-.02f)<1e-6f);
    CHECK(std::abs(nearby->partsInWeapon[0].values[3][0]-.15f)<1e-6f);
    CHECK(std::abs(nearby->timeSeconds-.15f)<1e-6f);
    f.profile.action=NativeActionPoseKind::Rack;CHECK(EvaluateNativeActionPose(f.profile,1,true));
    f.profile.action=NativeActionPoseKind::Reload;CHECK(!EvaluateNativeActionPose(f.profile,1,true));
    f.profile.action=NativeActionPoseKind::Pump;f.frames[1].progress=.99f;
    CHECK(!EvaluateNativeActionPose(f.profile,1,true)); // partial paths still cannot extrapolate
    f.frames[1].progress=1;f.frames[1].timeSeconds=.3f;
    CHECK(!EvaluateNativeActionPose(f.profile,1,true)); // never borrow a later unrelated peak
    f.profile.frames=std::span(f.frames).first(2);
    CHECK(!EvaluateNativeActionPose(f.profile,1,true)); // no return trajectory
    return 0;
}
int CosmeticHandTransition(){
    NativeActionHandTransition blend;NativeActionHandPose free,authored;
    free.wrist=At();free.fingersInWrist.fill(At(.01f));authored=free;authored.wrist=At(.2f);authored.fingersInWrist[0]=At(.02f);
    const auto first=blend.Update(1000000000,free,&authored,1,1);CHECK(first&&first->wrist.values==free.wrist.values);
    const auto held=blend.Update(1200000000,free,&authored,1,1);CHECK(held&&std::abs(held->wrist.values[3][0]-.2f)<1e-6f);
    CHECK(std::abs(held->fingersInWrist[0].values[3][0]-.02f)<1e-6f);
    CHECK(blend.Update(1200000000,free,&authored,1,1)->wrist.values==held->wrist.values); // second eye cannot advance
    CHECK(blend.Update(1210000000,free,nullptr,0,1));
    free.wrist=At(1);authored.wrist=At(50); // weapon movement has no input after release
    const auto released=blend.Update(1270000000,free,nullptr,0,1);CHECK(released&&std::abs(released->wrist.values[3][0]-1.1f)<1e-5f);
    const auto reacquired=blend.Update(1270000000,free,&authored,2,1);CHECK(reacquired&&reacquired->wrist.values==released->wrist.values);
    blend.Reset();CHECK(!blend.Active());CHECK(!blend.Update(1400000000,free,nullptr,0,1));
    return 0;
}
std::uint64_t rackRevision=9;
bool RackAuthority(const fvr::interaction::PhysicalWeaponIdentity& id,std::uint64_t revision,std::uint32_t client)noexcept {
    return id==fvr::interaction::PhysicalWeaponIdentity{1,2,3,4}&&revision==rackRevision&&client==0x40000;
}
int NativeRackFullMatricesAndReleasedPartOnly(){
    using namespace fvr::interaction;constexpr std::int64_t now=1000000000;RigSnapshot rig;
    rig.names={"root","jntWpn_1","jntWpn_17","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;rig.left.wrist=3;
    for(const char* digit:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned joint=1;joint<=3;++joint){
        rig.parents.push_back(joint==1?3:int(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+digit+std::to_string(joint));}
    rig.names.push_back("RightHand");rig.parents.push_back(0);rig.right.wrist=unsigned(rig.names.size()-1);
    rig.inverseBind.assign(rig.names.size(),At());rig.evaluatedWorld.assign(rig.names.size(),At());
    rig.identity.soldier=1;rig.identity.weak=2;rig.identity.count=unsigned(rig.names.size());rig.identity.evaluatedMatrices=0x10000;
    rig.nativeEvaluated.resize(rig.names.size());for(auto& row:rig.nativeEvaluated)row.fill(std::byte{0x91});
    NativeRackPresentation rack;rack.assetName=Xm8MagazineAsset;rack.meshPath=Xm8MagazineMesh;
    rack.clipSha256=std::string(64,'a');rack.skeletonSha256=std::string(64,'b');rack.partBones={"jntWpn_17"};
    rack.rigFingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);rack.actionToken=42;rack.trackingEpoch=9;
    rack.phase=NativeRackPhase::Pull;rack.ownsHand=true;rack.owner={0x10000,1,2,0x40000,5,7,9};
    rack.authorityWeapon={1,2,3,4};rack.authorityRevision=rackRevision=9;rack.authorityClientWeapon=0x40000;rack.authorityCurrent=RackAuthority;
    rack.inputEvidence={{(std::uint64_t(2)<<32)|1,5,17,9},11,now,now+100000000,now,true,{true,true}};
    rack.progressEvidence=rack.inputEvidence;
    rack.weapon={0x40000,17};rack.gunClaim={1,rack.inputEvidence.owner,InteractionHand::Right,HandClaimKind::GunHold,rack.weapon,{71,1},0};
    rack.handClaim={2,rack.inputEvidence.owner,InteractionHand::Left,HandClaimKind::Mechanism,rack.weapon,{72,1},1};
    rack.gunDeadlineNs=rack.handDeadlineNs=rack.inputEvidence.deadlineNs;
    rack.pose.progress=.5f;rack.pose.timeSeconds=.25f;rack.pose.wristInWeapon=At(.5f);rack.pose.fingersInWrist.fill(At(.01f));
    auto handle=At(.2f);handle.values[0]={0,1,0,0};handle.values[1]={-1,0,0,0};rack.pose.partsInWeapon={handle};
    const auto held=BuildNativeRackPose(rack,rig,At(10),100,now);CHECK(held&&held->wristTarget&&held->writes.size()==17);
    CHECK(held->writes[0].index==2&&std::abs(held->writes[0].transform.values[3][0]-30)<1e-5f);
    CHECK(held->writes[0].transform.values[0][1]==1&&held->writes[0].transform.values[1][0]==-1); // full native handle rotation
    CHECK(std::abs(held->wristTarget->values[3][0]-60)<1e-5f);
    for(const auto& w:held->writes)CHECK(w.index!=rig.right.wrist); // primary hand is never overridden
    const auto heldPalette=BuildRigPosePlan(rig,held->writes);CHECK(heldPalette);
    for(const auto& e:heldPalette->edits)CHECK(e.index!=rig.right.wrist&&e.before==rig.nativeEvaluated[e.index]);
    const auto old=rack;rack.phase=NativeRackPhase::Return;rack.ownsHand=false;rack.handClaim={};rack.handDeadlineNs=0;
    const auto returning=BuildNativeRackPose(rack,rig,At(10),100,now);CHECK(returning&&!returning->wristTarget&&returning->writes.size()==1);
    CHECK(returning->writes[0].transform.values==held->writes[0].transform.values);
    std::vector<std::array<std::byte,64>> posed(3),base(3);posed[2].fill(std::byte{0x10});base[2].fill(std::byte{0x20});
    CHECK(!SelectNativeRackPackedPalette(old,&old,now,posed,base,true).fallback);
    CHECK(SelectNativeRackPackedPalette(old,&rack,now+1,posed,base,true).bytes.data()==base.data()); // release between native copies
    auto renewed=old;renewed.inputEvidence.observedNs+=50000000;renewed.inputEvidence.deadlineNs+=50000000;++renewed.inputEvidence.sequence;
    renewed.gunDeadlineNs=renewed.handDeadlineNs=renewed.inputEvidence.deadlineNs;
    CHECK(SelectNativeRackPackedPalette(old,&renewed,now+100000000,posed,base,true).fallback); // never renew original palette deadline
    CHECK(SelectNativeRackPackedPalette(old,nullptr,now,posed,base,true).fallback);
    ++rackRevision;CHECK(!BuildNativeRackPose(rack,rig,At(),1,now));CHECK(SelectNativeRackPackedPalette(old,&old,now,posed,base,true).fallback);
    return 0;
}
fvr::math::Matrix4 AtXyz(float x,float y,float z){auto m=At(x);m.values[3][1]=y;m.values[3][2]=z;return m;}
int Donor(){
    CHECK(!FindNativeRackGraspDonor("XM8C"));CHECK(!FindNativeRackGraspDonor("XM8 LMG"));
    const auto* d=FindNativeRackGraspDonor("XM8_sp_s");CHECK(d&&d->handleBone=="jntWpn_17");
    // Synthetic donor: carrier fixed, handle closed at the recipient rest, pulled 0.1 m.
    std::array<std::string_view,2> names{"jntWpnwpnJnt_16","jntWpn_17"};
    std::array<std::array<fvr::math::Matrix4,2>,3> parts{};
    const float rest[3]{d->restHandleMeters[0],d->restHandleMeters[1],d->restHandleMeters[2]};
    for(unsigned n=0;n<3;++n){parts[n][0]=AtXyz(0,rest[1],-.3831f);parts[n][1]=AtXyz(rest[0],rest[1],rest[2]+(n==1?.1f:0.f));}
    std::array<NativeActionPoseFrame,3> frames{};
    for(unsigned n=0;n<3;++n){auto& f=frames[n];f.timeSeconds=float(n)*.05f;f.progress=n==1?1.f:0.f;f.returning=n==2;
        f.wristInWeapon=AtXyz(.05f,rest[1]-.01f,rest[2]+.08f+(n==1?.1f:0.f));f.fingersInWrist.fill(At(.01f));f.partsInWeapon=parts[n];}
    NativeActionPoseProfile p{"XM8 LMG",d->donorMesh,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",7,NativeActionPoseKind::Rack,names,frames};
    CHECK(NativeRackGraspDonorPart(p,*d)==1);
    // Wrist on the handle: same handle-relative offset at any handle placement.
    const auto handleWorld=AtXyz(1,2,3);
    const auto wrist=NativeActionWristOnPart(frames[1],1,handleWorld,1);
    CHECK(wrist&&std::abs(wrist->values[3][0]-1.05f)<1e-5f&&std::abs(wrist->values[3][1]-1.99f)<1e-5f&&std::abs(wrist->values[3][2]-3.08f)<1e-5f);
    const auto scaled=NativeActionWristOnPart(frames[1],1,handleWorld,10);
    CHECK(scaled&&std::abs(scaled->values[3][0]-1.5f)<1e-4f&&std::abs(scaled->values[3][2]-3.8f)<1e-4f);
    CHECK(!NativeActionWristOnPart(frames[1],2,handleWorld,1));
    CHECK(!NativeActionWristOnPart(frames[1],1,handleWorld,0));
    // Rejections: wrong action, wrong donor asset, handle away from recipient rest, missing part.
    auto wrong=p;wrong.action=NativeActionPoseKind::Reload;CHECK(NativeRackGraspDonorPart(wrong,*d)<0);
    wrong=p;wrong.assetName="XM8C";CHECK(NativeRackGraspDonorPart(wrong,*d)<0);
    for(unsigned n=0;n<3;++n)if(n!=1)parts[n][1].values[3][2]+=.005f;
    CHECK(NativeRackGraspDonorPart(p,*d)<0);
    for(unsigned n=0;n<3;++n)if(n!=1)parts[n][1].values[3][2]-=.005f;
    CHECK(NativeRackGraspDonorPart(p,*d)==1);
    std::array<std::string_view,2> other{"jntWpnwpnJnt_16","jntWpn_18"};p.partBones=other;CHECK(NativeRackGraspDonorPart(p,*d)<0);
    return 0;
}
#ifdef FVR_BC2_NATIVE_ACTION_POSE_HEADER
int OriginalPumpTurnaround(){
    const NativeActionPoseProfile* pump=nullptr;
    for(const auto& p:generated::NativeActionPoses)if(p.assetName=="SPAS12_sp"&&p.action==NativeActionPoseKind::Pump){CHECK(!pump);pump=&p;}
    CHECK(pump);
    const auto outbound=EvaluateNativeActionPose(*pump,1,false),returning=EvaluateNativeActionPose(*pump,1,true);
    CHECK(outbound&&returning&&outbound->timeSeconds==returning->timeSeconds);
    CHECK(outbound->wristInWeapon.values==returning->wristInWeapon.values);
    for(unsigned n=0;n<15;++n)CHECK(outbound->fingersInWrist[n].values==returning->fingersInWrist[n].values);
    CHECK(outbound->partsInWeapon.size()==returning->partsInWeapon.size());
    for(std::size_t n=0;n<outbound->partsInWeapon.size();++n)
        CHECK(outbound->partsInWeapon[n].values==returning->partsInWeapon[n].values);
    // Tracking at and around the rear stop must never drop to a support-pose
    // fallback, including the short native interval before the first return.
    for(unsigned n=0;n<=1000;++n)CHECK(EvaluateNativeActionPose(*pump,float(n)/1000.f,true));
    return 0;
}
int OriginalReloadWindowEndpoints(){
    struct Window {std::string_view asset;float entry,seat,carry;};
    constexpr std::array windows{Window{"XM8_sp_s",1.516666667f,1.575f,1.5f},Window{"SPAS12_sp",.35f,.4f,.25f}};
    for(const auto& window:windows){
        const NativeActionPoseProfile* insert=nullptr;
        for(const auto& profile:generated::NativeActionPoses)
            if(profile.assetName==window.asset&&profile.action==NativeActionPoseKind::ReloadInsert){CHECK(!insert);insert=&profile;}
        CHECK(insert);
        const auto* carry=FindNativeActionPose(generated::NativeActionPoses,insert->assetName,insert->meshPath,insert->rigFingerprint,NativeActionPoseKind::ReloadCarry);
        CHECK(carry&&carry->frames.size()==1&&std::abs(carry->frames[0].timeSeconds-window.carry)<2e-6f);
        for(float progress:{0.f,1.f}){
            const auto evaluated=EvaluateNativeActionPose(*insert,progress,false);
            CHECK(evaluated&&std::abs(evaluated->timeSeconds-(progress==0?window.entry:window.seat))<2e-6f);
            const auto* original=SampleNativeActionPose(*insert,progress,false);
            CHECK(original&&original->progress==progress);
            CHECK(evaluated->wristInWeapon.values==original->wristInWeapon.values);
            for(unsigned n=0;n<15;++n)CHECK(evaluated->fingersInWrist[n].values==original->fingersInWrist[n].values);
            CHECK(evaluated->partsInWeapon.size()==original->partsInWeapon.size());
            for(std::size_t n=0;n<evaluated->partsInWeapon.size();++n)CHECK(evaluated->partsInWeapon[n].values==original->partsInWeapon[n].values);
        }
        // The first alignment pose is the gun's ORIGINAL carry grasp, not a
        // rigid insertion-frame attachment or an approximation from another gun.
        auto aligned=EvaluateNativeActionPose(*insert,0.f,false);CHECK(aligned);
        const auto& free=carry->frames[0];
        CHECK(AlignNativeReloadPose(*aligned,*insert,carry,free.wristInWeapon,0.f));
        CHECK(aligned->wristInWeapon.values==free.wristInWeapon.values);
        for(unsigned n=0;n<15;++n)CHECK(aligned->fingersInWrist[n].values==free.fingersInWrist[n].values);
    }
    return 0;
}
#endif
}
int main(){CHECK(!Selection());CHECK(!Placement());CHECK(!Provenance());CHECK(!Donor());CHECK(!InterpolatedTrajectory());CHECK(!AnchoredPath());CHECK(!SharedNativeTurnaround());CHECK(!CosmeticHandTransition());CHECK(!NativeRackFullMatricesAndReleasedPartOnly());
#ifdef FVR_BC2_NATIVE_ACTION_POSE_HEADER
    CHECK(!generated::NativeActionPoses.empty());
    CHECK(!OriginalReloadWindowEndpoints());
    CHECK(!OriginalPumpTurnaround());
    for(const auto& profile:generated::NativeActionPoses){
        CHECK(ValidNativeActionPose(profile));
        CHECK(FindNativeActionPose(generated::NativeActionPoses,profile.assetName,profile.meshPath,profile.rigFingerprint,profile.action)==&profile);
        for(const auto& frame:profile.frames){
            CHECK(SampleNativeActionPose(profile,frame.progress,frame.returning));
            CHECK(NativeActionWrist(frame,At(),1));
        }
    }
#endif
    return 0;}
