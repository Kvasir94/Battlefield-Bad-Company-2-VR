#include "Test.h"
#include "Bc2NativeRecoilTransfer.h"
#include "Bc2GripAim.h"
#include "fvr/engine/FrostbiteCamera.h"
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>
using namespace fvr::bc2;
namespace math=fvr::math;
namespace interaction=fvr::interaction;
namespace engine=fvr::engine;
namespace {
struct Fixture {
    static constexpr unsigned image=0x400000,weapon=0x16000,soldier=0x11000,
        firingA=0x20000,firingB=0x21000,swayA=0x22000,swayB=0x23000;
    std::unordered_map<unsigned,std::byte> bytes;
    NativeRecoilContext context{};ReloadStateBinding binding{};
    bool current=true,changeBranch=false;unsigned flagReads=0;
    template<class T>void Put(unsigned at,const T& value){
        const auto* p=reinterpret_cast<const std::byte*>(&value);
        for(std::size_t n=0;n<sizeof(value);++n)bytes[at+unsigned(n)]=p[n];
    }
    Fixture(){
        context.owner={0x10000,soldier,0x12000,weapon,7,8,9};
        context.equipment.weapon=weapon;context.equipment.data=0x17000;
        context.input=10;context.rigEquipmentGeneration=11;context.deadlineQpc=1000;
        binding.firingVtableRva=0x8000;
        Put<std::uint8_t>(soldier+0x114,1);Put<std::uint8_t>(soldier+0x475,0x10);
        Put(weapon+0x3c,firingA);Put(weapon+0x40,firingB);
        Put(firingA,image+binding.firingVtableRva);Put(firingB,image+binding.firingVtableRva);
        Put(firingA+0x14,swayA);Put(firingB+0x14,swayB);
        Put(swayA,image+NativeRecoilBinding::swayVtableRva);Put(swayB,image+NativeRecoilBinding::swayVtableRva);
        Put(image+NativeRecoilBinding::swayVtableRva+0x5c,image+NativeRecoilBinding::tickRva);
        Put(image+NativeRecoilBinding::swayVtableRva+0x68,image+NativeRecoilBinding::cameraRva);
        std::array<float,16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        Put(swayA+0x40,matrix);Put(swayB+0x40,matrix);
        Put(swayA+0x2c,std::array<float,2>{-.05f,.01f});
        Put(swayB+0x2c,std::array<float,2>{-.03f,-.01f});
    }
    NativeRecoilReadResult Read(){
        const NativeRecoilMemory memory{this,[](void* ctx,unsigned at,void* dst,std::size_t size){
            auto& f=*static_cast<Fixture*>(ctx);
            if(at==soldier+0x114&&++f.flagReads==2&&f.changeBranch)f.Put<std::uint8_t>(soldier+0x114,0x11);
            auto* out=static_cast<std::byte*>(dst);
            for(std::size_t n=0;n<size;++n){const auto p=f.bytes.find(at+unsigned(n));if(p==f.bytes.end())return false;out[n]=p->second;}
            return true;
        },[](void* ctx,const NativeRecoilContext& c){const auto& f=*static_cast<Fixture*>(ctx);return f.current&&c==f.context;}};
        return ReadNativeRecoilObservation(memory,binding,image,context);
    }
};
NativeRecoilObservation Row(const NativeRecoilSample& s,NativeRecoilSeam seam,std::int64_t qpc){
    NativeRecoilObservation row;row.sample=s;row.context=s.context;row.copy=s.copy;
    row.status=NativeRecoilReadStatus::Observed;row.seam=seam;row.qpc=qpc;return row;
}
int SelectedCopy(){
    Fixture f;const auto before=f.bytes;const auto a=f.Read();
    CHECK(a.sample&&a.copy.branch==NativeRecoilBranch::FiringA&&a.copy.sway==f.swayA);
    CHECK(a.sample->rawRadians[0]==-.05f&&f.bytes==before);
    f.Put<std::uint8_t>(f.soldier+0x114,0x11);const auto b=f.Read();
    CHECK(b.sample&&b.copy.branch==NativeRecoilBranch::FiringB&&b.copy.firing==f.firingB&&b.copy.sway==f.swayB);
    f.Put<std::uint8_t>(f.soldier+0x114,0);const auto alternate=f.Read();
    CHECK(!alternate.sample&&alternate.status==NativeRecoilReadStatus::UnsupportedAlternate&&alternate.copy.branch==NativeRecoilBranch::Alternate);
    return 0;
}
int InvalidOwnershipAndState(){
    Fixture missing;missing.context.owner.equipGeneration=0;CHECK(missing.Read().status==NativeRecoilReadStatus::InvalidContext);
    Fixture owner;owner.current=false;CHECK(owner.Read().status==NativeRecoilReadStatus::OwnerMismatch);
    Fixture firing;firing.Put(firing.firingA,0x400123u);CHECK(firing.Read().status==NativeRecoilReadStatus::InvalidFiring);
    Fixture sway;sway.Put(sway.swayA,0x400123u);CHECK(sway.Read().status==NativeRecoilReadStatus::ReadFailure);
    Fixture slot;slot.Put(slot.image+NativeRecoilBinding::swayVtableRva+0x5c,0x400123u);CHECK(slot.Read().status==NativeRecoilReadStatus::InvalidSway);
    Fixture changed;changed.changeBranch=true;CHECK(changed.Read().status==NativeRecoilReadStatus::ChangedDuringRead);
    Fixture state;state.Put(state.swayA+0x40,std::numeric_limits<float>::quiet_NaN());CHECK(state.Read().status==NativeRecoilReadStatus::InvalidState);
    Fixture padding;padding.Put(padding.swayA+0x4c,std::numeric_limits<float>::quiet_NaN());CHECK(padding.Read().sample);
    return 0;
}
int NativeTickAndPublication(){
    Fixture f;const auto sample=f.Read();CHECK(sample.sample);
    NativeRecoilObservationHistory<5> history;
    history.Record(Row(*sample.sample,NativeRecoilSeam::PostTick,100),50);
    history.Record(Row(*sample.sample,NativeRecoilSeam::PostTick,101),50);
    auto publication=Row(*sample.sample,NativeRecoilSeam::BeforePublication,110);
    publication.context.input=11;publication.sample->context=publication.context;
    history.Record(publication,50);
    std::vector<NativeRecoilObservation> rows;history.ForEach([&](const auto& r){rows.push_back(r);});
    CHECK(rows.size()==3&&history.Ticks()==2&&rows[0].serial==1&&rows[1].serial==2);
    CHECK(rows[2].matchedTick&&rows[2].cameraMatrixMatchesTick&&rows[2].serial==2);
    CHECK(rows[2].qpc==110&&rows[2].tickQpc==101&&rows[2].tickInputContext==10);
    CHECK(rows[1].sample->rawRadians==rows[0].sample->rawRadians); // absolute, no summed impulses
    auto camera=publication;camera.seam=NativeRecoilSeam::CameraRead;camera.qpc=111;camera.sample->camera[0]=.99f;
    history.Record(camera,50);rows.clear();history.ForEach([&](const auto& r){rows.push_back(r);});
    CHECK(rows.back().matchedTick&&!rows.back().cameraMatrixMatchesTick); // intervening native state remains visible
    publication.qpc=200;history.Record(publication,50);
    rows.clear();history.ForEach([&](const auto& r){rows.push_back(r);});CHECK(!rows.back().matchedTick);
    history.Record(Row(*sample.sample,NativeRecoilSeam::PostTick,201),50);
    CHECK(history.Count()==5&&history.Total()==6&&history.Overwritten()==1);
    CHECK(!history.transferImplemented&&!history.sameFrameAuthority);
    return 0;
}
int EpochInvalidation(){
    Fixture f;const auto s=f.Read();CHECK(s.sample);
    for(unsigned change=0;change<6;++change){
        NativeRecoilObservationHistory<4> history;history.Record(Row(*s.sample,NativeRecoilSeam::PostTick,100),50);
        auto bad=Row(*s.sample,NativeRecoilSeam::BeforePublication,101);
        switch(change){
        case 0:++bad.context.owner.actorGeneration;break;
        case 1:++bad.context.owner.equipGeneration;break;
        case 2:++bad.context.owner.space;break;
        case 3:++bad.context.equipment.persistence;break;
        case 4:++bad.context.rigEquipmentGeneration;break;
        case 5:bad.copy.branch=NativeRecoilBranch::FiringB;bad.copy.sway=f.swayB;break;
        }
        bad.sample->context=bad.context;bad.sample->copy=bad.copy;history.Record(bad,50);
        history.Record(Row(*s.sample,NativeRecoilSeam::BeforePublication,102),50);
        bool resumed=false;history.ForEach([&](const auto& r){if(r.seam==NativeRecoilSeam::BeforePublication&&r.matchedTick)resumed=true;});
        CHECK(!resumed); // changing back cannot revive an old recoil lease
    }
    NativeRecoilObservationHistory<3> history;history.Record(Row(*s.sample,NativeRecoilSeam::PostTick,100),50);
    history.Invalidate();history.Record(Row(*s.sample,NativeRecoilSeam::BeforePublication,101),50);
    bool matched=false;history.ForEach([&](const auto& r){matched|=r.matchedTick;});CHECK(!matched);
    return 0;
}
int BoundaryOrdering(){
    Fixture f;const auto s=f.Read();CHECK(s.sample);
    NativeRecoilObservationHistory<8> history;
    history.Record(Row(*s.sample,NativeRecoilSeam::PostTick,100),50);
    auto before=Row(*s.sample,NativeRecoilSeam::BeforeVrBaseline,101);
    before.boundary.input=11;before.boundary.inputDeadlineQpc=300;
    before.boundary.anglesValid=true;before.boundary.requestedAim={.2f,.1f};
    before.boundary.aimerAim={.25f,.15f};before.boundary.simulationAim=before.boundary.aimerAim;
    history.Record(before,50);
    auto after=before;after.seam=NativeRecoilSeam::AfterVrBaseline;after.qpc=102;
    after.boundary.aimerAim=after.boundary.simulationAim=after.boundary.requestedAim;
    history.Record(after,50);
    auto shot=Row(*s.sample,NativeRecoilSeam::ClientShotBase,103);
    shot.boundary.shotSwayValid=true;shot.boundary.shotSway=f.swayA;
    shot.boundary.shotTokenValid=true;shot.boundary.shotToken=42;
    shot.boundary.rigGeneration=9;shot.boundary.rigDeadlineQpc=250;
    shot.boundary.shotBaseValid=true;shot.boundary.nativeShotBase=s.sample->camera;
    history.Record(shot,50);
    shot.seam=NativeRecoilSeam::ServerShotBase;shot.qpc=104;shot.boundary.shotSway=f.swayB;
    history.Record(shot,50);
    std::vector<NativeRecoilObservation> rows;history.ForEach([&](const auto& r){rows.push_back(r);});
    CHECK(history.Ticks()==1&&rows.size()==5);
    CHECK(rows[1].boundary.input==11&&rows[1].context.input==10&&rows[1].tickInputContext==10);
    CHECK(rows[1].boundary.aimerAim[0]==.25f&&rows[2].boundary.aimerAim[0]==.2f);
    CHECK(rows[1].matchedTick&&rows[2].matchedTick&&rows[2].tickQpc==100);
    CHECK(rows[3].shotUsesSelectedSway&&!rows[4].shotUsesSelectedSway);
    CHECK(rows[4].matchedTick); // selected camera tick observation is not shot ownership
    CHECK(rows[3].boundary.rigGeneration==9&&rows[3].boundary.rigDeadlineQpc==250);
    CHECK(rows[3].boundary.nativeShotBase==s.sample->camera&&rows[4].boundary.shotToken==42);
    CHECK(!history.transferImplemented&&!history.sameFrameAuthority);
    return 0;
}
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
math::Matrix4 AxisRotation(unsigned axis,float radians){
    auto m=Identity();const unsigned a=(axis+1)%3,b=(axis+2)%3;
    m.values[a][a]=m.values[b][b]=std::cos(radians);
    m.values[a][b]=std::sin(radians);m.values[b][a]=-std::sin(radians);return m;
}
std::array<float,16> NativeColumns(const math::Matrix4& m){
    std::array<float,16> out{};for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)out[4*row+column]=m.values[row][column];return out;
}
bool Close(const math::Matrix4& a,const math::Matrix4& b,float tolerance=2e-5f){
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)
        if(std::abs(a.values[row][column]-b.values[row][column])>tolerance)return false;
    return true;
}
int PoseLease(){
    Fixture f;const auto read=f.Read();CHECK(read.sample);
    NativeRecoilPoseSample pose{*read.sample,1,100,150};
    const auto valid=[&](const NativeRecoilPoseSample& p,std::int64_t now=110){return NativeRecoilPoseCurrent(p,
        f.soldier,0x12000,f.weapon,7,11,f.context.equipment,9,now);};
    CHECK(valid(pose));CHECK(!valid(pose,99));CHECK(valid(pose,150));CHECK(!valid(pose,151));
    auto changed=pose;changed.native.context.input=11;CHECK(valid(changed)); // rotation outlives its input label
    changed=pose;changed.serial=0;CHECK(!valid(changed));
    changed=pose;changed.deadlineQpc=1001;CHECK(!valid(changed)); // copying cannot renew native lifetime
    changed=pose;changed.native.context.owner.actorGeneration++;CHECK(!valid(changed));
    changed=pose;changed.native.context.owner.space++;CHECK(!valid(changed));
    changed=pose;changed.native.context.rigEquipmentGeneration++;CHECK(!valid(changed));
    changed=pose;changed.native.context.equipment.persistence++;CHECK(!valid(changed));
    changed=pose;changed.native.copy.cameraEnabled=false;CHECK(!valid(changed));
    changed=pose;changed.native.copy.branch=NativeRecoilBranch::Alternate;CHECK(!valid(changed));
    changed=pose;changed.native.copy.branch=NativeRecoilBranch::FiringB;CHECK(valid(changed));
    return 0;
}
int NativeCameraConvention(){
    const auto native=interaction::Multiply(interaction::Multiply(AxisRotation(0,.21f),AxisRotation(1,-.14f)),AxisRotation(2,.08f));
    const auto columns=NativeColumns(native);const auto rotation=NativeRecoilRotation(columns);CHECK(rotation);
    const auto canonical=engine::CanonicalCamera({native,.1f,100.f,1.f});CHECK(canonical);
    auto cameraToAim=Identity();cameraToAim.values[0][0]=cameraToAim.values[2][2]=-1;
    CHECK(Close(*rotation,interaction::Multiply(interaction::Multiply(cameraToAim,canonical->camera),cameraToAim)));
    // A camera coordinate conversion alone omits native camera/aim headings.
    CHECK(!Close(*rotation,canonical->camera));
    auto padded=columns;for(unsigned n=0;n<4;++n)padded[4*n+3]=std::numeric_limits<float>::quiet_NaN();
    padded[12]=1000;padded[13]=-2000;padded[14]=3000;
    const auto ignored=NativeRecoilRotation(padded);CHECK(ignored&&Close(*ignored,*rotation));
    auto bad=columns;bad[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!NativeRecoilRotation(bad));
    bad=NativeColumns(Identity());bad[0]=-1;CHECK(!NativeRecoilRotation(bad)); // reflection is not rotation
    bad=columns;bad[0]*=2;CHECK(!NativeRecoilRotation(bad));
    return 0;
}
int CurrentPhysicalBore(){
    auto boreInModel=Identity();boreInModel.values[0][0]=boreInModel.values[2][2]=-1;
    auto weaponInWrist=AxisRotation(0,.3f);weaponInWrist.values[3]={.1f,.2f,.3f,1};
    const auto wristToGrip=AxisRotation(2,.4f);
    const auto relation=GripBoreRelation(boreInModel,weaponInWrist,wristToGrip);CHECK(relation);
    CHECK(relation->values[3][0]==0&&relation->values[3][1]==0&&relation->values[3][2]==0);
    math::Pose reference{},grip{};reference.orientation.w=grip.orientation.w=1;
    grip.position={9,8,7};
    const auto first=CurrentGripBore(reference,grip,*relation);CHECK(first);
    // A new CURRENT grip orientation, without a new calibration publication.
    grip.orientation={0,std::sin(.35f),0,std::cos(.35f)};
    const auto next=CurrentGripBore(reference,grip,*relation);CHECK(next&&!Close(*first,*next));
    const auto currentView=math::MakeLhViewFromOpenXRPose(grip);CHECK(currentView);
    const auto currentGrip=interaction::InverseRigid(*currentView);CHECK(currentGrip);
    const auto currentWrist=interaction::Multiply(wristToGrip,*currentGrip);
    const auto currentWeapon=interaction::Multiply(weaponInWrist,currentWrist);
    const auto drawnBore=interaction::Multiply(boreInModel,currentWeapon);
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)CHECK(std::abs(next->values[r][c]-drawnBore.values[r][c])<2e-5f);
    const auto angles=GripBoreAngles(*next);CHECK(angles);
    CHECK(std::abs(std::sin(angles->pitch)-drawnBore.values[2][1])<2e-5f);
    // Identity relation agrees with the existing XR direction convention.
    const auto direct=CurrentGripBore(reference,grip,Identity());CHECK(direct);
    const auto a=GripBoreAngles(*direct),b=interaction::ControllerAim(reference,grip);CHECK(a&&b);
    CHECK(std::abs(a->yaw-b->yaw)<2e-5f&&std::abs(a->pitch-b->pitch)<2e-5f);
    auto invalid=*relation;invalid.values[0][0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!CurrentGripBore(reference,grip,invalid)&&!GripBoreAngles(invalid));
    return 0;
}
int NativeMuzzleRise(){
    // 0x6d60e0 stores upward recoil as negative pitch; 0x6d5db0 builds
    // Rx(pitch)*Ry(yaw). Check bore motion, not another copy of the conversion.
    constexpr float rise=.13f,yaw=.07f;
    const auto native=NativeColumns(interaction::Multiply(AxisRotation(0,-rise),AxisRotation(1,yaw)));
    for(const auto& aim:{Identity(),interaction::Multiply(AxisRotation(2,.6f),AxisRotation(1,1.2f))}){
        auto wrist=aim;wrist.values[3]={2,3,4,1};
        const auto delta=NativeRecoilGripDelta(native,aim,wrist);CHECK(delta);
        auto moved=interaction::Multiply(aim,*delta);
        float up=0,right=0,forward=0;
        for(unsigned n=0;n<3;++n){
            up+=moved.values[2][n]*aim.values[1][n];
            right+=moved.values[2][n]*aim.values[0][n];
            forward+=moved.values[2][n]*aim.values[2][n];
        }
        CHECK(std::abs(up-std::sin(rise))<2e-5f); // rises in the hand's up axis
        CHECK(std::abs(right+std::cos(rise)*std::sin(yaw))<2e-5f); // native yaw retained
        CHECK(std::abs(forward-std::cos(rise)*std::cos(yaw))<2e-5f);
        const auto movedWrist=interaction::Multiply(wrist,*delta);
        for(unsigned n=0;n<3;++n)CHECK(std::abs(movedWrist.values[3][n]-wrist.values[3][n])<2e-5f);
        const auto kick=NativeRecoilKickDelta(moved,.02f,1.f);CHECK(kick);
        float backwards=0;for(unsigned n=0;n<3;++n)backwards+=kick->values[3][n]*moved.values[2][n];
        CHECK(std::abs(backwards+.02f)<2e-5f); // kick follows the raised bore backwards
    }
    const auto roll=NativeRecoilRotation(NativeColumns(AxisRotation(2,.08f)));CHECK(roll);
    CHECK(roll->values[0][1]<0&&roll->values[1][0]>0); // complete heading change, not pitch-only patch
    return 0;
}
int AbsoluteGripTransfer(){
    const auto camera=NativeColumns(interaction::Multiply(AxisRotation(0,.2f),AxisRotation(1,-.09f)));
    auto aim=interaction::Multiply(AxisRotation(2,.7f),AxisRotation(1,1.2f));aim.values[3]={9,8,7,1};
    auto wrist=interaction::Multiply(AxisRotation(0,-.1f),AxisRotation(2,.15f));wrist.values[3]={2,3,4,1};
    auto attachment=AxisRotation(1,.4f);attachment.values[3]={.12f,-.08f,.45f,1};
    const auto weapon=interaction::Multiply(attachment,wrist);const auto baseline=weapon;
    const auto delta=NativeRecoilGripDelta(camera,aim,wrist);CHECK(delta);
    const auto movedWrist=interaction::Multiply(wrist,*delta),movedWeapon=interaction::Multiply(weapon,*delta);
    for(unsigned n=0;n<3;++n)CHECK(std::abs(movedWrist.values[3][n]-wrist.values[3][n])<2e-5f);
    const auto inverseWrist=interaction::InverseAnimatedTransform(movedWrist);CHECK(inverseWrist);
    CHECK(Close(interaction::Multiply(movedWeapon,*inverseWrist),attachment)); // wrist/item attachment retained
    CHECK(Close(weapon,baseline)); // original native/root/ballistic baseline remains available
    const auto again=NativeRecoilGripDelta(camera,aim,wrist);CHECK(again);
    CHECK(Close(interaction::Multiply(weapon,*again),movedWeapon)); // same absolute sample does not accumulate
    auto zero=NativeRecoilGripDelta(NativeColumns(Identity()),aim,wrist);CHECK(zero);
    CHECK(Close(interaction::Multiply(weapon,*zero),weapon)); // native recovery returns directly to baseline
    auto rotationAim=aim;rotationAim.values[3]={0,0,0,1};
    const auto inverseAim=interaction::InverseAnimatedTransform(rotationAim);CHECK(inverseAim);
    auto worldRotation=*delta;worldRotation.values[3]={0,0,0,1};
    const auto localRotation=interaction::Multiply(interaction::Multiply(rotationAim,worldRotation),*inverseAim);
    const auto expected=NativeRecoilRotation(camera);CHECK(expected&&Close(localRotation,*expected)); // yaw AND roll conjugation
    auto badAim=aim;badAim.values[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!NativeRecoilGripDelta(camera,badAim,wrist));
    auto badWrist=wrist;badWrist.values[3][0]=std::numeric_limits<float>::infinity();CHECK(!NativeRecoilGripDelta(camera,aim,badWrist));
    return 0;
}
int KickEnvelope(){
    Fixture f;constexpr std::int64_t frequency=1000000;
    constexpr float low=50.f*360.f/65536.f,high=1200.f*360.f/65536.f;
    const auto light=NativeRecoilStrengthWeight(low),heavy=NativeRecoilStrengthWeight(high);
    CHECK(light&&*light==0&&heavy&&*heavy==1);
    CHECK(!NativeRecoilStrengthWeight(0)&&!NativeRecoilStrengthWeight(-1)&&!NativeRecoilStrengthWeight(std::numeric_limits<float>::quiet_NaN()));
    const auto middle=NativeRecoilStrengthWeight(std::sqrt(low*high));CHECK(middle&&std::abs(*middle-.5f)<1e-6f);
    NativeRecoilKickEnvelope envelope;CHECK(envelope.Shot(f.context,1,1000000,high,false,frequency));
    auto sample=envelope.Sample();CHECK(sample&&std::abs(sample->peakMeters-.04f)<1e-6f&&sample->recoverySeconds==.6f);
    const auto attack=EvaluateNativeRecoilKick(*sample,1022500,frequency);CHECK(attack&&std::abs(*attack-.02f)<1e-6f);
    const auto peak=EvaluateNativeRecoilKick(*sample,1045000,frequency);CHECK(peak&&std::abs(*peak-.04f)<1e-6f);
    const auto recovered=EvaluateNativeRecoilKick(*sample,1645000,frequency);
    CHECK(recovered&&std::abs(*recovered-.04f*std::exp(-2.3f))<1e-6f);
    CHECK(!EvaluateNativeRecoilKick(*sample,999999,frequency));
    CHECK(envelope.Shot(f.context,2,1045000,high,false,frequency));sample=envelope.Sample();
    CHECK(sample&&sample->sequence==2&&std::abs(sample->fromMeters-.04f)<1e-6f&&sample->peakMeters==.05f);
    CHECK(!envelope.Shot(f.context,2,1060000,high,false,frequency));CHECK(envelope.Sample()->shotQpc==1045000);
    CHECK(!envelope.Shot(f.context,3,1030000,high,false,frequency));
    const auto end=NativeRecoilKickDeadline(*sample,frequency);CHECK(end>sample->shotQpc);
    const auto zero=EvaluateNativeRecoilKick(*sample,end,frequency);CHECK(zero&&*zero==0);
    CHECK(NativeRecoilKickCurrent(*sample,f.soldier,0x12000,f.weapon,7,11,f.context.equipment,9,1050000,frequency));
    CHECK(!NativeRecoilKickCurrent(*sample,f.soldier,0x12000,f.weapon,7,11,f.context.equipment,9,end,frequency));
    // Input lifetime does not truncate the authored recovery; owner/space do.
    CHECK(!NativeRecoilKickCurrent(*sample,f.soldier,0x12000,f.weapon,7,11,f.context.equipment,10,1050000,frequency));
    auto changed=f.context;changed.owner.space++;
    CHECK(envelope.Shot(changed,4,1050000,low,true,frequency));sample=envelope.Sample();
    CHECK(sample&&sample->fromMeters==0&&std::abs(sample->peakMeters-.004f)<1e-6f&&sample->recoverySeconds==.15f);
    envelope.Reset();CHECK(!envelope.Sample());
    return 0;
}
int KickBoreAndAttachment(){
    const auto aim=interaction::Multiply(AxisRotation(2,.4f),AxisRotation(1,1.1f));
    const auto delta=NativeRecoilKickDelta(aim,.04f,100.f);CHECK(delta);
    auto wrist=AxisRotation(0,.2f);wrist.values[3]={20,30,40,1};
    auto attachment=AxisRotation(1,-.3f);attachment.values[3]={2,-1,5,1};
    const auto weapon=interaction::Multiply(attachment,wrist);
    const auto kickedWrist=interaction::Multiply(wrist,*delta),kickedWeapon=interaction::Multiply(weapon,*delta);
    for(unsigned n=0;n<3;++n){
        CHECK(std::abs(kickedWrist.values[3][n]-wrist.values[3][n]+aim.values[2][n]*4.f)<1e-5f);
        for(unsigned row=0;row<3;++row)CHECK(kickedWrist.values[row][n]==wrist.values[row][n]);
    }
    const auto inverse=interaction::InverseAnimatedTransform(kickedWrist);CHECK(inverse);
    CHECK(Close(interaction::Multiply(kickedWeapon,*inverse),attachment));
    CHECK(!NativeRecoilKickDelta(aim,.051f,100.f)&&!NativeRecoilKickDelta(aim,.01f,0));
    CHECK(Close(weapon,interaction::Multiply(attachment,wrist))); // native baseline retained
    return 0;
}
int MuzzleUpOnly(){
    // Spring overshoot / wide heading: native POSITIVE pitch is a dive.
    constexpr float dive=.05f,yaw=.06f;
    const auto down=NativeColumns(interaction::Multiply(AxisRotation(0,dive),AxisRotation(1,yaw)));
    const auto up=NativeColumns(interaction::Multiply(AxisRotation(0,-.1f),AxisRotation(1,yaw)));
    const auto aim=interaction::Multiply(AxisRotation(2,.5f),AxisRotation(1,.9f));
    auto wrist=aim;wrist.values[3]={1,2,3,1};
    const auto bore=[&](const math::Matrix4& delta,float& u,float& r){
        const auto moved=interaction::Multiply(aim,delta);u=r=0;
        for(unsigned n=0;n<3;++n){u+=moved.values[2][n]*aim.values[1][n];r+=moved.values[2][n]*aim.values[0][n];}
    };
    float u=0,r=0,rawRight=0;bool rectified=true;
    const auto raw=NativeRecoilGripDelta(down,aim,wrist,false,&rectified);CHECK(raw&&!rectified);
    bore(*raw,u,rawRight);CHECK(u<-.04f); // raw native descent is a dive
    const auto clamped=NativeRecoilGripDelta(down,aim,wrist,true,&rectified);CHECK(clamped&&rectified);
    bore(*clamped,u,r);CHECK(std::abs(u)<2e-5f); // no dive
    CHECK(r*rawRight>0&&std::abs(r-rawRight)<.01f); // native heading retained
    const auto movedWrist=interaction::Multiply(wrist,*clamped);
    for(unsigned n=0;n<3;++n)CHECK(std::abs(movedWrist.values[3][n]-wrist.values[3][n])<2e-5f); // same pivot
    const auto rising=NativeRecoilGripDelta(up,aim,wrist,true,&rectified);CHECK(rising&&!rectified);
    const auto rawRising=NativeRecoilGripDelta(up,aim,wrist);CHECK(rawRising&&Close(*rising,*rawRising)); // rise untouched
    const auto identity=NativeRecoilMuzzleUp(Identity(),&rectified);CHECK(identity&&!rectified&&Close(*identity,Identity()));
    auto bad=Identity();bad.values[0][0]=2;CHECK(!NativeRecoilMuzzleUp(bad));
    return 0;
}
int MuzzleBoreFromDrawnBarrel(){
    // Unverified model axes: the drawn gun's model +Z points BACKWARDS.
    const auto drawn=interaction::Multiply(AxisRotation(1,3.14159265f),interaction::Multiply(AxisRotation(2,.3f),AxisRotation(1,.8f)));
    auto weapon=drawn;weapon.values[3]={5,6,7,1};
    auto muzzleLocal=Identity();muzzleLocal.values[3]={0,.04f,-.85f,1}; // Flash ahead along model -Z
    const auto muzzle=interaction::Multiply(muzzleLocal,weapon);
    auto upReference=interaction::Multiply(AxisRotation(2,.3f),AxisRotation(1,.8f));
    const auto frame=MuzzleBoreFrame(weapon,muzzle,upReference,.05f);CHECK(frame);
    math::Vec3 f{muzzle.values[3][0]-5,muzzle.values[3][1]-6,muzzle.values[3][2]-7};
    const float length=std::hypot(f.x,f.y,f.z);
    CHECK(std::abs(frame->values[2][0]-f.x/length)<2e-5f&&std::abs(frame->values[2][1]-f.y/length)<2e-5f&&std::abs(frame->values[2][2]-f.z/length)<2e-5f);
    float upDot=0;for(unsigned n=0;n<3;++n)upDot+=frame->values[1][n]*upReference.values[1][n];CHECK(upDot>.99f);
    // Native rise lifts the drawn barrel in this frame; through the reversed
    // model frame the same sample dives.
    const auto native=NativeColumns(AxisRotation(0,-.1f));
    auto wrist=weapon;
    const auto good=NativeRecoilGripDelta(native,*frame,wrist);CHECK(good);
    auto reversed=weapon;reversed.values[3]={0,0,0,1};
    const auto bad=NativeRecoilGripDelta(native,reversed,wrist);CHECK(bad);
    const auto rise=[&](const math::Matrix4& delta){
        const auto a=interaction::Multiply(muzzle,delta);return a.values[3][1]-muzzle.values[3][1];};
    CHECK(rise(*good)>.05f);CHECK(rise(*bad)<-.05f);
    CHECK(!MuzzleBoreFrame(weapon,weapon,upReference,.05f)); // Flash at root
    auto straightUp=Identity();straightUp.values[3]={5,7,7,1};
    CHECK(!MuzzleBoreFrame(weapon,straightUp,Identity(),.05f)); // barrel parallel to reference up
    return 0;
}
}
int main(){CHECK(!SelectedCopy());CHECK(!InvalidOwnershipAndState());CHECK(!NativeTickAndPublication());CHECK(!EpochInvalidation());CHECK(!BoundaryOrdering());CHECK(!PoseLease());CHECK(!NativeCameraConvention());CHECK(!CurrentPhysicalBore());CHECK(!NativeMuzzleRise());CHECK(!AbsoluteGripTransfer());CHECK(!KickEnvelope());CHECK(!KickBoreAndAttachment());CHECK(!MuzzleUpOnly());CHECK(!MuzzleBoreFromDrawnBarrel());return 0;}
