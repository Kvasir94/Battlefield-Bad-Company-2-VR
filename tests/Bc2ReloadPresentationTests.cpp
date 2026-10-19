#include "Bc2ReloadPresentation.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b,float e=1e-4f){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],e))return false;return true;}
struct Fixture {
    RigSnapshot rig{};Bc2ReloadPresentationBinding binding{};Bc2ReloadTargets target{};Bc2ReloadPresentationObservation s{};
    Fixture(){
        rig.names={"root","jntWpn_1","jntWpn_7","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        for(const char* finger:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){
            rig.parents.push_back(j==1?3:std::int32_t(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+finger+std::to_string(j));}
        rig.names.push_back("Unrelated");rig.parents.push_back(0);
        const auto n=unsigned(rig.names.size());rig.identity.count=n;rig.identity.evaluatedMatrices=0x10000;rig.identity.soldier=1;
        rig.inverseBind.assign(n,Pose());rig.evaluatedWorld.assign(n,Pose());rig.nativeEvaluated.resize(n);
        for(auto& bytes:rig.nativeEvaluated){bytes.fill(std::byte{0x91});}
        binding=*bc2_reload_detail::DerivePresentationBinding(rig);
        target.identity={{1,2,3,4},{5,6},{7,8},9};target.inputSequence=10;target.nativeCycle=11;target.observedNs=1000000000ll;target.deadlineNs=1100000000ll;
        target.shellClaim={12,target.identity.owner,InteractionHand::Left,HandClaimKind::AmmoObject,target.identity.item,{20,1},0};
        target.weaponClaim={13,target.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,target.identity.weapon,{21,1},0};
        target.weaponFromShellCenterMeters=Pose(.1f,.2f,.3f);target.weaponFromLeftWristMeters=Multiply(SpasReloadInsertionProfile().itemFromHand,target.weaponFromShellCenterMeters);
        s.enabled=s.selectedMeshIdentityVerified=s.sectionOwnershipVerified=s.shellSectionVisible=true;s.assetName=SpasReloadAsset;s.meshPath=SpasReloadMesh;
        s.identity=target.identity;s.inputSequence=10;s.nativeCycle=11;s.nowNs=1000000000ll;s.placedWeaponWorld=Pose(2,3,4);
    }
    Bc2ReloadPresentationPlan Run(){return BuildBc2ReloadPresentation(rig,binding,s,target);}
};
int RealPalettePlanAndIsolation(){
    Fixture f;const auto original=f.rig.nativeEvaluated;auto p=f.Run();CHECK(p.reason==Bc2ReloadPresentationReason::None&&p.palette&&p.writes.size()==17&&p.palette->edits.size()==17);
    CHECK(f.rig.nativeEvaluated==original);
    CHECK(p.writes[0].index==f.binding.shell&&p.writes[1].index==f.binding.wrist);
    auto center=Pose(0,.00033545875f,.000945806466f);
    CHECK(Same(Multiply(center,p.writes[0].transform),Multiply(f.target.weaponFromShellCenterMeters,f.s.placedWeaponWorld)));
    CHECK(Same(p.writes[1].transform,Multiply(f.target.weaponFromLeftWristMeters,f.s.placedWeaponWorld)));
    for(const auto& e:p.palette->edits){
        CHECK(e.index!=0&&e.index!=1&&e.index!=f.rig.names.size()-1);
        CHECK(e.before==original[e.index]);for(unsigned row=0;row<4;++row)for(unsigned col=12;col<16;++col)CHECK(e.after[row*16+col]==std::byte{0x91});
    }
    // Synthetic rig exercises placement, but cannot pass production binding.
    CHECK(!BindBc2ReloadPresentation(f.rig,SpasReloadAsset,SpasReloadMesh));return 0;
}
int OverflowingSharedShellCenterRejectsSafely(){
    Fixture f;f.s.unitsPerMetre=std::numeric_limits<float>::max();
    f.target.weaponFromShellCenterMeters=Pose(2,2,2);
    const auto result=f.Run();CHECK(!result.palette&&result.writes.empty()&&result.reason==Bc2ReloadPresentationReason::InvalidGeometry);return 0;
}
int CapturedFingerPlacement(){
    Fixture f;f.s.placedWeaponWorld=Pose();
    // Independent source coordinates: captured native row58, measured before
    // any VR hand override. Detects inverted grasp or wrong shell origin.
    f.target.weaponFromShellCenterMeters.values[0]={0.7702783035f,0.4514477655f,-0.4504068798f,0.f};
    f.target.weaponFromShellCenterMeters.values[1]={-0.4221855129f,0.8903511923f,0.1703940782f,0.f};
    f.target.weaponFromShellCenterMeters.values[2]={0.4779442819f,0.05890444313f,0.8764128768f,0.f};
    f.target.weaponFromShellCenterMeters.values[3]={-0.134834118f,-0.3240808092f,-0.423486707f,1.f};
    // This receipt belongs to its original captured grasp, independently of
    // the currently selected native carry/input calibration.
    const math::Matrix4 capturedGrasp={{{{0.5216128812f,0.8433950571f,0.1288553864f,0.f},{0.8289782374f,-0.4652830701f,-0.3103332119f,0.f},{-0.2017792085f,0.2686923375f,-0.9418538283f,0.f},{-0.03649051857f,-0.1033049136f,0.07940265825f,1.f}}}};
    f.target.weaponFromLeftWristMeters=Multiply(capturedGrasp,f.target.weaponFromShellCenterMeters);
    const auto p=f.Run();CHECK(p.palette);
    const std::array<unsigned,2> slots{2,5}; // Thumb3, Index3 within15-finger mapping.
    const std::array<math::Vec3,2> expected{{{-.1040504638f,-.3297404193f,-.4173032679f},{-.1406201126f,-.3411608581f,-.4274161094f}}};
    for(unsigned k=0;k<2;++k){const auto& m=p.writes[2+slots[k]].transform;
        CHECK(Near(m.values[3][0],expected[k].x,.00006f));CHECK(Near(m.values[3][1],expected[k].y,.00006f));CHECK(Near(m.values[3][2],expected[k].z,.00006f));}
    return 0;
}
int UnitsAndSceneInvariance(){
    Fixture f;auto a=f.Run();CHECK(a.palette);
    auto scene=Pose(20,-30,40);scene.values[0]={0,0,-1,0};scene.values[2]={1,0,0,0};
    f.s.placedWeaponWorld=Multiply(f.s.placedWeaponWorld,scene);auto b=f.Run();CHECK(b.palette);
    for(unsigned n=0;n<a.writes.size();++n)CHECK(Same(Multiply(a.writes[n].transform,scene),b.writes[n].transform));
    Fixture scaled;scaled.s.unitsPerMetre=100;for(unsigned c=0;c<3;++c)scaled.s.placedWeaponWorld.values[3][c]*=100;
    auto d=scaled.Run();CHECK(d.palette);for(unsigned n=0;n<a.writes.size();++n){auto expected=a.writes[n].transform;for(unsigned c=0;c<3;++c)expected.values[3][c]*=100;CHECK(Same(expected,d.writes[n].transform,.001f));}
    return 0;
}
int VisibilityAndStaleRejection(){
    for(unsigned k=0;k<10;++k){Fixture f;
        if(k==0)f.s.enabled=false;if(k==1)f.s.selectedMeshIdentityVerified=false;if(k==2)f.s.sectionOwnershipVerified=false;
        if(k==3)f.s.shellSectionVisible=false;if(k==4)f.rig.nativeHiddenLeaves={f.binding.shell};
        if(k==5)f.s.nowNs=f.target.deadlineNs;if(k==6)++f.s.nativeCycle;if(k==7)++f.s.identity.owner.space;
        if(k==8)++f.s.inputSequence;if(k==9)f.s.assetName="XM8_sp_s";
        const auto p=f.Run();CHECK(!p.palette&&p.writes.empty());
        if(k==4)CHECK(p.reason==Bc2ReloadPresentationReason::NativeShellHidden);
    }return 0;
}
int TopologyAndBindingRejection(){
    for(unsigned k=0;k<7;++k){Fixture f;
        if(k==0)f.rig.parents[2]=0;if(k==1)f.rig.names[4]="MissingThumb";
        if(k==2)f.rig.parents[5]=3;if(k==3)f.rig.parents.back()=3;
        if(k==4)f.rig.parents[0]=0;if(k==5)f.binding.fingers[0]++;
        if(k==6)f.rig.inverseBind[4].values[3][0]=.1f;
        CHECK(!f.Run().palette);
    }
    Fixture f;f.rig.evaluatedWorld[f.binding.shell]={};CHECK(f.Run().reason==Bc2ReloadPresentationReason::InvalidGeometry);
    return 0;
}
int GuidedAxisAlignmentPreservesRollThroughPrivatePalette(){
    Fixture f;Bc2ReloadInteraction interaction{true};Bc2ReloadInteractionSample s;
    s.assetName=SpasReloadAsset;s.meshPath=SpasReloadMesh;s.rigFingerprint=SpasReloadRig;s.selectedMeshIdentityVerified=true;
    auto& i=s.insertion;i.identity=f.target.identity;i.profile={SpasReloadInsertionProfile().id,SpasReloadInsertionProfile().revision};
    i.itemClaim.token=f.target.shellClaim;i.weaponClaim.token=f.target.weaponClaim;i.held=i.eligible=i.focused=i.itemTracked=i.weaponTracked=true;
    s.weaponWorldMeters=f.s.placedWeaponWorld;
    const auto p=SpasReloadInsertionProfile();Bc2ReloadInteractionResult result;
    auto roll=Pose();roll.values[0][0]=roll.values[1][1]=std::cos(1.1f);roll.values[0][1]=std::sin(1.1f);roll.values[1][0]=-std::sin(1.1f);
    auto pitch=Pose();pitch.values[1][1]=pitch.values[2][2]=std::cos(.2f);pitch.values[1][2]=std::sin(.2f);pitch.values[2][1]=-std::sin(.2f);
    for(unsigned n=0;n<7;++n){auto raw=Multiply(roll,pitch);raw.values[3]=reload_insertion_detail::TravelPose(p,n?-.02f:-.05f).values[3];
        i.sequence=i.geometrySequence=10+n;i.observedNs=i.nowNs=1000000000ll+n*30000000ll;i.deadlineNs=i.nowNs+100000000;
        i.itemClaim.inputSequence=i.weaponClaim.inputSequence=i.sequence;i.itemClaim.deadlineNs=i.weaponClaim.deadlineNs=i.deadlineNs;
        s.native={i.identity.owner,i.identity.weapon,11,i.observedNs,i.deadlineNs,true};
        s.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(raw,Multiply(p.weaponFromEntry,s.weaponWorldMeters))));
        result=interaction.Update(s);
    }
    CHECK(result.targets&&result.insertion.alignment==1);
    f.target=*result.targets;f.s.inputSequence=i.sequence;f.s.nowNs=i.nowNs;
    const auto original=f.rig.nativeEvaluated;const auto native=f.Run();CHECK(native.palette&&native.writes.size()==17&&f.rig.nativeEvaluated==original);
    // Recover actual emitted shell-center transform from the private bone plan.
    const auto centerWorld=Multiply(Pose(0,.00033545875f,.000945806466f),native.writes[0].transform);
    const auto shellInWeapon=Multiply(centerWorld,*InverseRigid(f.s.placedWeaponWorld));
    const auto rail=Multiply(Multiply(p.itemFromInsertion,shellInWeapon),*InverseRigid(p.weaponFromEntry));
    roll.values[3]=reload_insertion_detail::TravelPose(p,-.02f).values[3];CHECK(Same(rail,roll));
    CHECK(Same(native.writes[1].transform,Multiply(Multiply(p.itemFromHand,shellInWeapon),f.s.placedWeaponWorld)));
    return 0;
}
int BottomStartAndTerminalThroughPrivatePalette(){
    Fixture f;const auto p=SpasReloadInsertionProfile();const auto original=f.rig.nativeEvaluated;
    for(float along:{0.f,p.travelMeters*.5f,p.travelMeters}){
        f.target.weaponFromShellCenterMeters=Multiply(*InverseRigid(p.itemFromInsertion),Multiply(reload_insertion_detail::TravelPose(p,along),p.weaponFromEntry));
        f.target.weaponFromLeftWristMeters=Multiply(p.itemFromHand,f.target.weaponFromShellCenterMeters);
        const auto output=f.Run();CHECK(output.palette&&f.rig.nativeEvaluated==original);
        const auto actualCenter=Multiply(Multiply(Pose(0,.00033545875f,.000945806466f),output.writes[0].transform),*InverseRigid(f.s.placedWeaponWorld));
        CHECK(Same(actualCenter,f.target.weaponFromShellCenterMeters));
        CHECK(Same(output.writes[1].transform,Multiply(Multiply(p.itemFromHand,actualCenter),f.s.placedWeaponWorld)));
        #ifndef FVR_BC2_NATIVE_RELOAD_CALIBRATION_HEADER
        // Historical fixed calibration. Exact native profiles may have their
        // own entry orientation; the scene/target invariants above still apply.
        CHECK(actualCenter.values[2][2]>.999f);
        if(along==0)CHECK(actualCenter.values[3][1]<-.085f);
        if(along==.05f)CHECK(Near(actualCenter.values[3][1],-.04518763864f,.000001f)&&Near(actualCenter.values[3][2],-.5035302898f,.000001f));
        #endif
    }return 0;
}
int ExactNativeShellTrajectoryAndCarry(){
    Fixture f;std::array<std::string_view,1> bones{"jntWpn_7"};
    std::array<math::Matrix4,1> entry{Pose(.1f)},seat{Pose(.2f)};
    std::array<NativeActionPoseFrame,2> frames{};
    for(unsigned n=0;n<2;++n){frames[n].progress=float(n);frames[n].timeSeconds=.1f*float(n);
        frames[n].partsInWeapon=n?std::span<const math::Matrix4>(seat):std::span<const math::Matrix4>(entry);
        frames[n].wristInWeapon=Pose(.15f+.1f*float(n));frames[n].fingersInWrist.fill(Pose(.01f+.02f*float(n)));}
    NativeActionPoseProfile insert{"SPAS12_sp",SpasReloadMesh,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",f.binding.fingerprint,NativeActionPoseKind::ReloadInsert,bones,frames};
    auto carry=insert;carry.action=NativeActionPoseKind::ReloadCarry;carry.frames=std::span(frames).first(1);
    auto settle=insert;settle.action=NativeActionPoseKind::ReloadSettle;
    const std::array profiles{insert,carry,settle};f.s.nativeActions=profiles;f.s.actionConstrained=true;f.s.actionProgress=.5f;
    const auto p=f.Run();CHECK(p.palette&&p.sampledNativeHand&&p.writes.size()==17);
    const auto wristFromShell=Multiply(p.writes[1].transform,*InverseRigid(p.writes[0].transform));
    CHECK(Same(wristFromShell,Pose(.05f))); // identical anchor correction on shell and native wrist
    CHECK(Same(Multiply(p.writes[2].transform,*InverseRigid(p.writes[1].transform)),Pose(.02f)));
    f.s.actionProgress=1;const auto terminal=f.Run();CHECK(terminal.palette);
    CHECK(Same(terminal.writes[0].transform,Multiply(seat[0],f.s.placedWeaponWorld)));
    // The virtual recognizer terminal never displaces the original shell path.
    CHECK(Same(terminal.writes[1].transform,Multiply(frames[1].wristInWeapon,f.s.placedWeaponWorld)));
    f.s.actionConstrained=false;const auto held=f.Run();CHECK(held.palette&&held.sampledNativeHand);
    CHECK(Same(held.writes[1].transform,Multiply(f.target.weaponFromLeftWristMeters,f.s.placedWeaponWorld)));
    CHECK(Same(Multiply(held.writes[0].transform,*InverseRigid(held.writes[1].transform)),Pose(-.05f)));
    // Captured stationary input must retain the original carry at the tracked
    // wrist. Partial physical alignment blends to native entry, with no clip
    // progress until alignment completes. Finger samples blend together too.
    f.s.actionConstrained=true;f.s.actionProgress=0;f.s.actionAlignment=0;
    f.s.actionRawWristInWeaponMeters=Pose(.45f);
    const auto captured=f.Run();CHECK(captured.palette&&captured.sampledNativeHand);
    CHECK(Same(captured.writes[1].transform,Multiply(Pose(.45f),f.s.placedWeaponWorld)));
    CHECK(Same(captured.writes[0].transform,Multiply(Pose(.4f),f.s.placedWeaponWorld)));
    f.s.actionAlignment=.5f;const auto aligning=f.Run();CHECK(aligning.palette);
    CHECK(Same(aligning.writes[1].transform,Multiply(Pose(.3f),f.s.placedWeaponWorld)));
    CHECK(Same(aligning.writes[0].transform,Multiply(Pose(.25f),f.s.placedWeaponWorld)));
    f.s.nowNs+=1000000;const auto stationary=f.Run();CHECK(stationary.palette);
    CHECK(Same(stationary.writes[1].transform,aligning.writes[1].transform));
    f.s.actionAlignment=1;const auto aligned=f.Run();CHECK(aligned.palette);
    CHECK(Same(aligned.writes[1].transform,Multiply(frames[0].wristInWeapon,f.s.placedWeaponWorld)));
    CHECK(Same(aligned.writes[0].transform,Multiply(entry[0],f.s.placedWeaponWorld)));
    f.s.actionAlignment=0;f.s.actionRawWristInWeaponMeters.reset();CHECK(!f.Run().palette);
    f.s.actionAlignment=1;
    // Accepted insertion releases the hand immediately; the original shell
    // alone finishes its short authored tail. No wrist target/claim is needed.
    f.s.actionConstrained=true;f.s.actionHandOwned=false;f.s.releasedShellVerified=true;f.s.carried=true;
    f.s.actionProgress=.5f;f.target.nativeCycle=f.s.nativeCycle=0;f.target.shellClaim={};
    f.target.weaponFromShellCenterMeters=f.target.weaponFromLeftWristMeters={};
    const auto tail=f.Run();CHECK(tail.palette&&!tail.sampledNativeHand&&tail.writes.size()==1&&tail.palette->edits.size()==1);
    CHECK(tail.writes[0].index==f.binding.shell&&Same(tail.writes[0].transform,Multiply(Pose(.15f),f.s.placedWeaponWorld)));
    f.s.releasedShellVerified=false;CHECK(!f.Run().palette);
    f.s.releasedShellVerified=true;f.s.nativeActions=std::span(profiles).first(2);CHECK(!f.Run().palette); // no approximate tail
    f.s.releasedInsertion=true;const auto inserting=f.Run();
    CHECK(inserting.palette&&!inserting.sampledNativeHand&&inserting.writes.size()==1&&inserting.writes[0].index==f.binding.shell);
    return 0;
}
}
int main(){if(OverflowingSharedShellCenterRejectsSafely()||RealPalettePlanAndIsolation()||CapturedFingerPlacement()||UnitsAndSceneInvariance()||VisibilityAndStaleRejection()||TopologyAndBindingRejection()||GuidedAxisAlignmentPreservesRollThroughPrivatePalette()||BottomStartAndTerminalThroughPrivatePalette()||ExactNativeShellTrajectoryAndCarry())return 1;std::cout<<"Bc2ReloadPresentation: 9 cases passed\n";}
