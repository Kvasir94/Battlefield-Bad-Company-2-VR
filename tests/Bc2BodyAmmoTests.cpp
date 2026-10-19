#include "Bc2BodyAmmo.h"
#include "Bc2MagazineConsumerFixture.h"
using namespace magazine_consumer_fixture;
namespace {
BodyAmmoTracking Source(Fixture& f){
    f.Send();BodyAmmoTracking t;t.magazine=f.result.tracking;
    const auto owners=BindMagazineOwners(f.s.input.owner,f.s.weapon,f.reserve,0,f.now,f.s.family);
    t.visual.enabled=true;t.visual.source=*MagazineSupply(*owners,f.reserve,f.s.trackingEpoch,f.now);
    t.visual.input=f.s.input;t.visual.gun=*f.gun;
    t.visual.contact=*ChestAmmoSupply().alternateContact;t.visual.frame=SupplyAnchorFrame::RecenteredBody;
    return t;
}
InputFrame Input(const Fixture& f){InputFrame i;i.generation=f.s.input.sequence;i.spaceGeneration=f.s.input.owner.space;
    i.predictedNs=f.now;i.focused=i.headValid=true;i.referenceHead.orientation.w=i.head.orientation.w=1;
    i.referenceHead.position=i.head.position={0,1.7f,0};
    for(auto& hand:i.hands){hand.gripTracked=hand.aimTracked=true;hand.grip.orientation.w=hand.aim.orientation.w=1;}
    return i;
}
int SharedProfileBodyPose(){
    unsigned profiles=0;
    for(const auto asset:{std::string_view("XM8_sp_s"),std::string_view("AEK971_sp")}){
        const auto* p=FindMagazineEquipment(asset);if(!p||!p->Ready())continue;++profiles;
        for(float units:{1.f,100.f}){
            Fixture f(true,*p);auto source=Source(f);auto input=Input(f);input.worldUnitsPerMeter=units;
            input.referenceHead.orientation={0,std::sin(.3f),0,std::cos(.3f)};
            input.head.orientation={0,std::sin(.9f),0,std::cos(.9f)};input.head.position={.15f,1.3f,-.18f};
            const auto body=Pose(20,-10,30);const auto pose=BuildBodyAmmoPose(source,input,body,f.now);CHECK(pose);
            const auto contact=AmmoSupplyAnchorWorld(input,body,source.visual);CHECK(contact);
            // Extracted magazine is in part-local metres. Its actual grasp
            // lands on the body's configured pickup point at either scale.
            const auto grasp=Multiply(p->geometry->interaction.insertion.itemFromHand,pose->partWorld);
            for(unsigned n=0;n<3;++n)CHECK(Near(grasp.values[3][n],contact->values[3][n],.0002f));
            CHECK(pose->asset==asset&&pose->mesh==p->geometry->mesh&&pose->part==p->geometry->bones.magazine);
            // Looking sideways changes no recentered-body anchor; moving the
            // weapon has no input to this independent body prop at all.
            input.head.orientation={0,std::sin(-.4f),0,std::cos(-.4f)};
            const auto turned=BuildBodyAmmoPose(source,input,body,f.now);CHECK(turned&&Same(pose->partWorld,turned->partWorld));
        }
    }
    CHECK(profiles>=1);
#ifdef FVR_EXPECT_GENERATED_MAGAZINE
    CHECK(profiles==2);
#endif
    std::cout<<"body supply profiles checked: "<<profiles<<" (native calls mocked)\n";return 0;
}
int ExactSourceAndOriginalExpiry(){
    Fixture f;const auto source=Source(f);CHECK(BodyAmmoFresh(source,f.now));CHECK(BodyAmmoRetained(source,source,f.now));
    for(unsigned fault=0;fault<12;++fault){auto t=source;
        if(fault==0)++t.visual.source.objectUnits;
        if(fault==1)++t.visual.source.reserveUnits;
        if(fault==2)++t.visual.source.sequence;
        if(fault==3)++t.visual.source.identity.pool.id;
        if(fault==4)t.visual.occupied=true;
        if(fault==5)++t.visual.input.sequence;
        if(fault==6)++t.visual.input.deadlineNs;
        if(fault==7)++t.magazine->owner.equipGeneration;
        if(fault==8)t.magazine->family.verified=false;
        if(fault==9)t.shell=BeltAmmoTracking{};
        if(fault==10)t.magazine.reset();
        if(fault==11)t.visual.input.released[1]=true;
        CHECK(!BodyAmmoFresh(t,f.now));CHECK(!BodyAmmoRetained(source,t,f.now));
    }
    CHECK(!BodyAmmoFresh(source,source.visual.input.deadlineNs));
    auto next=source;next.visual.contact.centerMeters[0]+=.1f;CHECK(!BodyAmmoRetained(source,next,f.now));
    next=source;++next.visual.gun.token.id;CHECK(!BodyAmmoRetained(source,next,f.now));
    auto meshes=std::make_shared<SelectedMeshesSnapshot>(*source.magazine->selected);
    ++meshes->deadlineNs;next=source;next.magazine->selected=meshes;CHECK(!BodyAmmoRetained(source,next,f.now));
    return 0;
}
int NoWrongSpaceOrHeldReplacement(){
    Fixture f;auto source=Source(f);auto input=Input(f);CHECK(BuildBodyAmmoPose(source,input,Pose(),f.now));
    ++input.spaceGeneration;CHECK(!BuildBodyAmmoPose(source,input,Pose(),f.now));input=Input(f);
    ++input.generation;CHECK(!BuildBodyAmmoPose(source,input,Pose(),f.now));input=Input(f);
    input.headValid=false;CHECK(!BuildBodyAmmoPose(source,input,Pose(),f.now));input=Input(f);
    source.magazine->target=MagazinePropTarget{};source.magazine->target->role=MagazinePropRole::Replacement;
    CHECK(!BodyAmmoFresh(source,f.now));source.magazine->target->role=MagazinePropRole::Removed;
    CHECK(!BodyAmmoFresh(source,f.now));return 0;
}
std::optional<BodyAmmoMaterial> currentMaterial;
std::uint64_t currentRevision=0;
bool currentOwned=true;
bool PhysicalCurrent(const PhysicalWeaponIdentity& id,std::uint64_t revision,std::uint32_t client)noexcept {
    return currentOwned&&currentMaterial&&id==currentMaterial->weapon&&client==currentMaterial->clientWeapon&&revision==currentRevision;
}
std::uint64_t MaterialCurrent(const BodyAmmoMaterial& original)noexcept {
    return currentOwned&&currentMaterial&&original==*currentMaterial?currentRevision:0;
}
int ShotsRetainOnlyUnchangedBodyMaterial(){
    Fixture f;auto source=Source(f);auto& m=*source.magazine;
    PhysicalWeaponState ledger;const PhysicalWeaponIdentity identity{1,5,9,17};
    CHECK(ledger.Initialize(identity,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},27,83,true,true));
    source.visual.source.objectUnits=30; // Physical body offer is one full-capacity refill, independent of installed rounds.
    currentMaterial=BodyAmmoMaterialFor(identity,m.owner.weapon,source.visual.source.identity.pool,ledger.Snapshot(),ledger.Magazines());
    CHECK(currentMaterial&&BodyAmmoMaterialOffers(*currentMaterial,source.visual.source));
    currentOwned=true;currentRevision=1;
    m.physicalAuthority=true;m.authorityWeapon=identity;m.authorityClientWeapon=m.owner.weapon;
    m.authorityRevision=currentRevision;m.authorityCurrent=PhysicalCurrent;m.bodySupply=source.visual.source;m.bodyMaterial=currentMaterial;
    source.materialCurrentRevision=MaterialCurrent;
    CHECK(BodyAmmoFresh(source,f.now));CHECK(MagazineTrackingFresh(m,f.now));
    const auto originalMaterial=*currentMaterial;
    CHECK(ledger.ReconcileAcceptedShots(identity,1,26,83).Applied());++currentRevision;
    currentMaterial=BodyAmmoMaterialFor(identity,m.owner.weapon,source.visual.source.identity.pool,ledger.Snapshot(),ledger.Magazines());
    CHECK(currentMaterial&&*currentMaterial==originalMaterial);
    CHECK(!MagazineTrackingFresh(m,f.now)); // Actual magazine/action authority remains strict.
    auto current=source;current.magazine->authorityRevision=currentRevision;
    CHECK(BodyAmmoFresh(source,f.now));CHECK(BodyAmmoRetained(source,current,f.now));
    CHECK(BuildBodyAmmoPose(source,Input(f),Pose(),f.now));
    CHECK(!BodyAmmoFresh(source,source.visual.input.deadlineNs)); // No renewal of old geometry/input.
    for(unsigned fault=0;fault<7;++fault){currentMaterial=originalMaterial;
        if(fault==0)++currentMaterial->looseReserve;
        if(fault==1)++currentMaterial->feedCapacity;
        if(fault==2)++currentMaterial->pool.id;
        if(fault==3)++currentMaterial->weapon.weaponGeneration;
        if(fault==4)++currentMaterial->clientWeapon;
        if(fault==5)currentMaterial->retained[0]={91,10,30};
        if(fault==6)currentOwned=false;
        CHECK(!BodyAmmoFresh(source,f.now));CHECK(!BodyAmmoRetained(source,current,f.now));currentOwned=true;
    }
    // Equal aggregate offer sizes cannot hide changed retained-object identity.
    auto magazines=ledger.Magazines();magazines[1]={71,10,30,PhysicalMagazineLocation::Retained};
    const auto retained=BodyAmmoMaterialFor(identity,m.owner.weapon,originalMaterial.pool,ledger.Snapshot(),magazines);CHECK(retained);
    ++magazines[1].id;const auto replaced=BodyAmmoMaterialFor(identity,m.owner.weapon,originalMaterial.pool,ledger.Snapshot(),magazines);
    CHECK(replaced&&*retained!=*replaced);
    auto tampered=source;currentMaterial=originalMaterial;++tampered.visual.source.objectUnits;tampered.magazine->bodySupply=tampered.visual.source;
    CHECK(!BodyAmmoFresh(tampered,f.now));currentMaterial.reset();return 0;
}
}
int main(){if(SharedProfileBodyPose()||ExactSourceAndOriginalExpiry()||NoWrongSpaceOrHeldReplacement()||ShotsRetainOnlyUnchangedBodyMaterial())return 1;
    std::cout<<"BodyAmmo: 4 CPU groups passed; no independent native draw or headset acceptance\n";}
