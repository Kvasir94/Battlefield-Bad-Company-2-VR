#include "Bc2PhysicalWeaponInteraction.h"
#include "Bc2BodyAmmo.h"
#include "Bc2Xm8MagazineCalibration.h"
#include "Bc2PhysicalTubeInteraction.h"
#include "fvr/interaction/SupportGrip.h"
#include "Test.h"
#include <cstring>
#include <vector>
#include <cstdlib>
#ifdef FVR_BC2_NATIVE_RACK_PHASE_HEADER
#include FVR_BC2_NATIVE_RACK_PHASE_HEADER
#endif
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
using namespace fvr::interaction::reload_insertion_detail;
namespace {
PhysicalWeaponState ledger;
PhysicalWeaponState secondLedger;
bool firstSuspended=false,firstNativeBusy=false;
PhysicalWeaponState* RuntimeLedger(std::uint32_t client){
 return client==0x40000?&ledger:client==0x40004&&secondLedger.Snapshot().initialized?&secondLedger:nullptr;
}
PhysicalWeaponState* CommandLedger(const PhysicalWeaponIdentity& id){
 for(auto* state:{&ledger,&secondLedger})if(state->Snapshot().initialized&&state->Snapshot().identity==id)return state;
 return nullptr;
}
std::vector<PhysicalWeaponCommand> queued;
std::vector<physicalWeaponRuntime::Receipt> receipts;
std::uint64_t serial=0,revision=1;
unsigned submitFailures=0;
// Every command ever submitted, by kind.
std::vector<PhysicalWeaponCommandKind> submitted;
bool holdReceipts=false; // leave submitted commands in flight (no authority update this gather)
bool projectionPending=false;
constexpr PhysicalWeaponIdentity IdentityKey{0x90000,12,0xa0000,14};
void Commit(){for(auto command:queued){const auto id=command.id;auto* state=CommandLedger(command.identity);
 command.id=state?state->NextCommandId():0;
 const auto r=state&&(!firstSuspended||state!=&ledger||PhysicalMagazineMetadataCleanup(command.kind))?
  state->Apply(command):PhysicalWeaponResult{};receipts[id-1]={id,r.Applied()?physicalWeaponRuntime::CommandStatus::Applied:
 physicalWeaponRuntime::CommandStatus::Rejected,r.magazine,r.droppedMagazine};++revision;}queued.clear();}
void ExpireQueued(){for(const auto& command:queued)
 receipts[command.id-1]={command.id,physicalWeaponRuntime::CommandStatus::Rejected,0};queued.clear();}
auto Pose(float x=0,float y=0,float z=0){auto m=Identity();m.values[3]={x,y,z,1};return m;}
struct Fixture {
 std::int64_t now=1000000000;std::uint64_t intent=0;
 MagazinePhysicalSample sample{};HandInteraction hands;std::optional<HandClaim> gun;
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 MagazineEquipmentProfile profile=Xm8MagazineEquipment();
 bool acquireSupport=false;std::optional<HandClaim> support;
 Bc2PhysicalWeaponInteraction frontend;MagazinePhysicalResult result;
 Fixture(bool grabRemoval=true){ledger={};secondLedger={};firstSuspended=firstNativeBusy=false;queued.clear();receipts.clear();serial=0;revision=1;submitFailures=0;holdReceipts=false;projectionPending=false;submitted.clear();
  if(grabRemoval)frontend.EnableGrabRemoval();
  ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,60,true,true);
  sample.nativeOwner={0x10000,0x20000,0x30000,0x40000,5,3,7};sample.weapon={0xb0000,17};sample.trackingEpoch=7;
  sample.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},1,now,now+100000000,now,true,{true,true},{true,false}};
  sample.asset=Xm8MagazineAsset;sample.meshes=meshes;sample.bodyFromHand=Pose(2);
  meshes->owner=sample.nativeOwner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=SelectedMeshKind::Xm8;mesh.address=0x120000;
  std::memcpy(mesh.assetPath.data(),Xm8MagazineMesh.data(),Xm8MagazineMesh.size());
  sample.raw.valid=true;sample.raw.owner=sample.nativeOwner;sample.raw.rigFingerprint=Xm8MagazineRig;
  sample.raw.weaponWorldMeters=Identity();
 }
 void Send(bool grip=false,float z=.1f,bool eject=false,bool currentRaw=false){const auto previous=sample.input;const auto previousHand=sample.raw.rawLeftWristWorldMeters;
  if(!holdReceipts)Commit();now+=20000000;++sample.input.sequence;
  auto& in=sample.input;in.observedNs=in.nowNs=now;in.deadlineNs=now+100000000;in.released[0]=!grip;
  LatchLoadingHand(in,frontend.LatchesLoadingHand());
  sample.gripPressed=grip;sample.ejectPressed=eject;sample.geometrySequence=in.sequence;
  sample.family={{sample.nativeOwner,sample.weapon,0xd0000,0xc0000,2},now,in.deadlineNs,true};
  sample.family.binding.profile=&profile;
  meshes->sequence=in.sequence;meshes->observedNs=now;meshes->deadlineNs=in.deadlineNs;
  const auto p=Xm8MagazineConfig().insertion;
  sample.raw.inputEvidence=previous;sample.originalHandEvidence=previous;
  sample.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,z),p.weaponFromEntry)));
  const auto nextHand=sample.raw.rawLeftWristWorldMeters;sample.raw.rawLeftWristWorldMeters=previousHand;
  if(currentRaw){sample.raw.inputEvidence=in;sample.originalHandEvidence=in;}
  hands.Update(in);HandContactProof proof{{1002,1},in.sequence,in.deadlineNs,true};
  if(gun)gun=hands.Renew(in,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(in,{in.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.weapon,proof,++intent,0}).claim;
  if(acquireSupport){acquireSupport=false;support=hands.Acquire(in,{in.owner,InteractionHand::Left,
   HandClaimKind::WeaponSupport,sample.weapon,{{77,1},in.sequence,in.deadlineNs,true},++intent,gun->token.id}).claim;}
  result=frontend.Tick(sample,hands,intent);
  sample.raw.rawLeftWristWorldMeters=nextHand;
 }
 bool Pull(){Send();Send(true);for(float z=.09f;z>-.031f;z-=.01f)Send(true,z);
  Send(true,-.03f);return frontend.ProbeState(now).phase==DetachableMagazinePhase::RemovedHeld;}
 // Short B with the magazine seated and nothing in hand: eject it to the floor.
 void Eject(){Send();Send(false,.1f,true);Send();Send();}
 // Take a magazine from the pouch and slide it in until the slider latches the hand.
 void Draw(){const auto pouch=Bc2MagazinePhysicalReload::DefaultPouch().pouchCenterMeters;
  sample.bodyFromHand=Pose(pouch[0],pouch[1],pouch[2]);Send();Send(true,-.1f);Send(true,-.1f);
  for(float z=-.09f;z<.061f;z+=.01f)Send(true,z);}
 void Settle(unsigned n=20){for(unsigned k=0;k<n;++k)Send(false,.06f);}
 void Handle(bool grip,float travel){const auto* p=FindMagazineEquipment(sample.asset);const auto& h=*p->chargingHandle;
  auto hand=Pose();for(unsigned i=0;i<3;++i)hand.values[3][i]=h.handleMeters[i]+h.pullDirection[i]*travel;
  sample.raw.rawLeftWristWorldMeters=Multiply(p->geometry->interaction.insertion.itemFromHand,hand);Send(grip);}
 unsigned Count(PhysicalWeaponCommandKind kind)const{unsigned n=0;for(const auto& c:queued)n+=c.kind==kind;return n;}
};
}
namespace fvr::bc2::physicalWeaponRuntime {
bool Enabled()noexcept{return true;}bool Installed()noexcept{return true;}
bool ConfigureWeapon(std::uint32_t client,PhysicalWeaponFamily family,bool,std::optional<std::int32_t>)noexcept{
 const auto* state=RuntimeLedger(client);return state&&family==state->Snapshot().config.family;}
std::optional<View> Read(std::uint32_t client)noexcept {const auto* state=RuntimeLedger(client);if(!state)return {};
 View v;v.identity=state->Snapshot().identity;v.state=state->Snapshot();v.magazines=state->Magazines();v.revision=revision;v.native.clientWeapon=client;
 v.suspended=state==&ledger&&firstSuspended;
 v.native.ammoAddress=0xe0000;v.readiness=ReadWeaponReadiness(v.state,v.suspended,
  projectionPending||(state==&ledger&&firstNativeBusy));return v;}
std::optional<PlayerInventorySnapshot> Inventory()noexcept {
 const auto identity=ledger.Snapshot().identity;PlayerInventorySnapshot inventory;
 inventory.anchor.actorGeneration=identity.actorGeneration;inventory.serverSoldier=std::uint32_t(identity.actor);
 inventory.count=1;inventory.slots[0].owned=true;inventory.slots[0].item=std::uint32_t(identity.weapon);
 inventory.slots[0].clientWeapon=0x40000;return inventory;}
std::uint64_t Submit(const PhysicalWeaponIdentity& id,PhysicalWeaponCommandKind kind,std::uint64_t magazine,int rounds)noexcept {
 const auto* state=CommandLedger(id);if(!state||(state==&ledger&&firstSuspended&&!PhysicalMagazineMetadataCleanup(kind)))return 0;
 if(state==&ledger&&firstNativeBusy&&!(firstSuspended&&PhysicalMagazineMetadataCleanup(kind)))return 0;
 if(submitFailures){--submitFailures;return 0;}
 submitted.push_back(kind);
 const auto n=++serial;queued.push_back({id,n,kind,magazine,unsigned(rounds)});
 receipts.push_back({n,CommandStatus::Pending,0});return n;}
std::optional<Receipt> Result(std::uint64_t id)noexcept {return id&&id<=receipts.size()?std::optional{receipts[id-1]}:std::nullopt;}
}
namespace {
// Exercise the actual SPAS frontend against the same queued runtime authority
// used by the magazine cases. Renderer geometry is deliberately one input
// packet behind, matching Gameplay's original-evidence pairing.
struct TubeFixture {
 Fixture runtime{false};Bc2PhysicalTubeInteraction frontend;
 PhysicalReloadSample sample{};HandInteraction hands;std::optional<HandClaim> gun,support;
 PhysicalReloadResult result;std::int64_t now=1000000000;std::uint64_t intent=0;
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 TubeFixture(){
  ledger={};ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::TubePump,7,8},1,5,true,true);
  sample.nativeOwner=runtime.sample.nativeOwner;sample.weapon={sample.nativeOwner.weapon,17};sample.trackingEpoch=7;
  sample.input=runtime.sample.input;sample.asset=SpasReloadAsset;sample.meshes=meshes;
  const auto pouch=Bc2PhysicalTubeInteraction::DefaultPouch().pouchCenterMeters;
  sample.bodyFromHand=Pose(pouch[0],pouch[1],pouch[2]);
  meshes->owner=sample.nativeOwner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=SelectedMeshKind::Spas12;mesh.address=0x120000;
  std::memcpy(mesh.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());
  sample.raw.valid=true;sample.raw.owner=sample.nativeOwner;sample.raw.rigFingerprint=SpasReloadRig;
  sample.raw.weaponWorldMeters=Identity();sample.raw.rawLeftWristWorldMeters=GuideHand(0);
 }
 static math::Matrix4 GuideHand(float along){const auto p=SpasReloadInsertionProfile(SpasShellFeel::As2);
  return Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,along),p.weaponFromEntry)));}
 void Send(bool grip,float along=0){
  const auto previous=sample.input;const auto previousWrist=sample.raw.rawLeftWristWorldMeters;
  if(!holdReceipts)Commit();now+=20000000;++sample.input.sequence;
  auto& in=sample.input;in.observedNs=in.nowNs=now;in.deadlineNs=now+100000000;in.released[0]=!grip;
  LatchLoadingHand(in,frontend.LatchesLoadingHand());sample.gripPressed=grip;sample.geometrySequence=in.sequence;
  meshes->owner=sample.nativeOwner;meshes->sequence=in.sequence;meshes->observedNs=now;meshes->deadlineNs=in.deadlineNs;
  sample.raw.inputEvidence=previous;sample.originalHandEvidence=previous;sample.raw.owner=sample.nativeOwner;
  sample.raw.rawLeftWristWorldMeters=previousWrist;
  hands.Update(in);const HandContactProof proof{{1002,1},in.sequence,in.deadlineNs,true};
  if(gun)gun=hands.Renew(in,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(in,{in.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.weapon,proof,++intent,0}).claim;
  if(support)support=hands.Renew(in,support->token,{{77,1},in.sequence,in.deadlineNs,true}).claim;
  result=frontend.Tick(sample,hands,intent);sample.raw.rawLeftWristWorldMeters=GuideHand(along);
 }
 bool BeginFlight(){
  Send(false);Send(true);Send(true);
  const auto p=SpasReloadInsertionProfile(SpasShellFeel::As2);
  for(unsigned n=1;n<=12&&!result.settleStarted;++n)Send(true,p.travelMeters*.05f*n);
  return result.settleStarted&&result.tracking.preview&&result.tracking.preview->phase==ReloadPreviewPhase::ReleasedInsert;
 }
 bool GrabForeEnd(){
  if(!gun)return false;const auto& in=sample.input;
  support=hands.Acquire(in,{in.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,sample.weapon,
   {{77,1},in.sequence,in.deadlineNs,true},++intent,gun->token.id}).claim;
  return support.has_value();
 }
};
int ReleasedSpasInsertionFreesActualClaimAndCommitsOnce(){
 TubeFixture f;holdReceipts=true;const auto before=ledger.Snapshot();CHECK(f.BeginFlight());
 CHECK(!f.result.ammoOwnsHand&&!f.result.latchedLoadingHand&&!f.frontend.LatchesLoadingHand()&&!f.frontend.BlocksEquipment());
 CHECK(!f.hands.Current(InteractionHand::Left)&&queued.empty()&&submitted.empty());
 CHECK(f.result.tracking.preview&&!f.result.tracking.preview->actionHandOwned&&!f.result.tracking.preview->shellClaim.token.id);
 CHECK(ReloadPreviewFresh(*f.result.tracking.preview,f.result.tracking,f.now));
 const auto gun=f.gun->token;CHECK(f.GrabForeEnd());const auto support=f.support->token;
 // A fore-end claim (the pump's real support ownership) may be acquired and
 // renewed while the detached shell advances. Raw offhand movement no longer
 // drives the shell, and neither a fake hand lease nor early ammo debit occurs.
 unsigned steps=0;
 while(queued.empty()&&steps++<20){
  f.Send(true,-.2f);CHECK(!f.result.ammoOwnsHand&&!f.frontend.LatchesLoadingHand());
  CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==support);
  CHECK(f.hands.Current(InteractionHand::Right)&&f.hands.Current(InteractionHand::Right)->token==gun);
  CHECK(ledger.Snapshot().loaded==before.loaded&&ledger.Snapshot().looseReserve==before.looseReserve);
  if(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::ReleasedInsert){
   CHECK(f.result.tracking.preview->actionProgress<1&&queued.empty()&&submitted.empty());
   CHECK(ReloadPreviewFresh(*f.result.tracking.preview,f.result.tracking,f.now));
  }
 }
 CHECK(queued.size()==1&&queued[0].kind==PhysicalWeaponCommandKind::InsertRound);
 CHECK(submitted.size()==1&&submitted[0]==PhysicalWeaponCommandKind::InsertRound);
 CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Settling);
 for(unsigned n=0;n<3;++n)f.Send(true,-.2f);
 CHECK(queued.size()==1&&submitted.size()==1);Commit();holdReceipts=false;f.Send(true,-.2f);
 CHECK(ledger.Snapshot().loaded==before.loaded+1&&ledger.Snapshot().looseReserve==before.looseReserve-1&&ledger.Conserved());
 CHECK(f.frontend.ProbeState(f.now).completed==1&&submitted.size()==1&&!f.result.ammoOwnsHand);
 CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==support);
 return 0;
}
int ReleasedSpasInsertionRejectsOwnerRevisionAndCancellation(){
 for(unsigned fault=0;fault<3;++fault){TubeFixture f;holdReceipts=true;CHECK(f.BeginFlight());
  const auto before=ledger.Snapshot();
  if(fault==0)++f.sample.nativeOwner.equipGeneration;
  if(fault==1)++revision;
  if(fault==2)f.sample.cancel=true;
  f.Send(true,-.2f);CHECK(!f.result.tracking.preview&&!f.result.ammoOwnsHand&&!f.hands.Current(InteractionHand::Left));
  // The rejected flight cannot resurrect after the original settle deadline.
  for(unsigned n=0;n<20;++n)f.Send(false,-.2f);
  CHECK(queued.empty()&&submitted.empty()&&ledger.Snapshot().loaded==before.loaded&&
        ledger.Snapshot().looseReserve==before.looseReserve&&ledger.Conserved());
 }
 return 0;
}
int EmptyWellSupportKeepsMagazineGeometryAndChamber(){
 Fixture f;f.Eject();
 CHECK(!ledger.Snapshot().insertedMagazine&&ledger.Snapshot().chamber==1&&ledger.CanFire());
 CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Hidden);
 const auto commands=serial,authority=revision;
 SupportGrip support;InputFrame input{};input.spaceGeneration=f.sample.input.owner.space;
 input.focused=input.headValid=true;input.hands[0].grip.position.z=-.4f;
 for(auto& hand:input.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
 const SupportGripOwner owner{f.sample.input.owner.actor,f.sample.input.owner.actorGeneration,f.sample.weapon.id};
 const SupportGripContact contact{true,0,{}};
 input.generation=f.sample.input.sequence;input.predictedNs=f.now;
 CHECK(!support.Update(owner,input,contact,MagazineBlocksSupport(f.result,f.now)).holding);
 f.Send(true,.7f);input.generation=f.sample.input.sequence;input.predictedNs=f.now;input.hands[0].squeeze=1;
 const auto supported=support.Update(owner,input,contact,MagazineBlocksSupport(f.result,f.now));CHECK(supported.holding);
 const HandContactProof proof{{77,1},f.sample.input.sequence,f.sample.input.deadlineNs,true};
 const auto claim=f.hands.Acquire(f.sample.input,{f.sample.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
  f.sample.weapon,proof,++f.intent,f.gun->token.id}).claim;CHECK(claim);
 f.Send(true,.7f);const auto current=f.hands.Current(InteractionHand::Left);CHECK(current&&current->token==claim->token);
 CHECK(serial==commands&&revision==authority&&queued.empty()&&!f.result.blockFire);
 CHECK(f.result.chamber.magazineOut&&f.result.chamber.chamberLoaded&&ledger.Snapshot().loaded==1&&ledger.Conserved());

 // Native evaluation still places a magazine in the well. Private packing
 // must relocate that exact bone and its carried top-round chain even while
 // support owns the left wrist/fingers; it must never mutate native evaluation.
 RigSnapshot rig;rig.names={"root","jntWpn_1","jntWpn_6","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
 for(const char* digit:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){
  rig.parents.push_back(j==1?3:int(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+digit+std::to_string(j));}
 rig.names.push_back("round");rig.parents.push_back(2);rig.names.push_back("roundTip");rig.parents.push_back(int(rig.names.size()-2));
 rig.names.push_back("unrelated");rig.parents.push_back(0);
 rig.identity.soldier=f.sample.nativeOwner.soldier;rig.identity.weak=f.sample.nativeOwner.weak;
 rig.identity.count=unsigned(rig.names.size());rig.identity.evaluatedMatrices=0x10000;
 rig.inverseBind.assign(rig.names.size(),Pose());rig.evaluatedWorld.assign(rig.names.size(),Pose());
 rig.evaluatedWorld[2]=Xm8MagazineGeometry().attachedItem;
 rig.evaluatedWorld[rig.names.size()-3]=Multiply(Pose(0,.05f),rig.evaluatedWorld[2]);
 rig.evaluatedWorld[rig.names.size()-2]=Multiply(Pose(0,.1f),rig.evaluatedWorld[2]);
 rig.nativeEvaluated.resize(rig.names.size());for(auto& bytes:rig.nativeEvaluated)bytes.fill(std::byte{0x91});
 const auto binding=magazine_presentation_detail::Derive(rig);CHECK(binding&&binding->carriedCount==2);
 const auto native=rig.nativeEvaluated;auto tracking=f.result.tracking;CHECK(MagazineTargetFresh(tracking,f.now));
 const auto hidden=BuildMagazinePresentation(rig,*binding,tracking,Pose(2,3,4),1,f.now);
 CHECK(hidden.binding&&hidden.hideMagazine&&MagazinePresentationWrites(hidden,true).empty());
 tracking.droppedWorldMeters=Pose(3,-1,2);
 const auto dropped=BuildMagazinePresentation(rig,*binding,tracking,Pose(2,3,4),1,f.now);
 CHECK(dropped.binding&&!dropped.hideMagazine&&!dropped.wristTarget);
 const auto geometry=MagazinePresentationWrites(dropped,supported.holding);CHECK(geometry.size()==3);
 CHECK(geometry[0].index==binding->magazine&&geometry[0].transform.values==tracking.droppedWorldMeters->values);
 CHECK(geometry[1].index==binding->carried[0]&&geometry[2].index==binding->carried[1]);
 CHECK(Near(geometry[2].transform.values[3][1],-.9f));
 std::vector<BoneWrite> writes{{binding->weapon,Pose(2,3,4)},{binding->wrist,Pose(2,3,3.6f)}};
 for(const auto bone:binding->fingers)writes.push_back({bone,Pose(2,3,3.6f)});
 const auto supportOnly=BuildRigPosePlan(rig,writes);CHECK(supportOnly);
 writes.insert(writes.end(),geometry.begin(),geometry.end());const auto packed=BuildRigPosePlan(rig,writes);CHECK(packed);
 auto ordinary=native;for(const auto& edit:supportOnly->edits)ordinary[edit.index]=edit.after;
 auto posed=native;for(const auto& edit:packed->edits)posed[edit.index]=edit.after;
 CHECK(posed[binding->magazine]!=ordinary[binding->magazine]&&posed.back()==native.back());
 CHECK(posed[binding->wrist]==ordinary[binding->wrist]);
 for(const auto bone:binding->fingers)CHECK(posed[bone]==ordinary[bone]);
 for(unsigned eye=0;eye<2;++eye){const auto choice=SelectMagazinePackedPalette(tracking,&tracking,f.now,posed,ordinary,true);
  CHECK(!choice.fallback&&choice.bytes[binding->magazine]==posed[binding->magazine]);}
 CHECK(rig.nativeEvaluated==native&&serial==commands&&revision==authority&&ledger.Conserved());

 // Original deadlines and selected-copy identity still gate both eye copies.
 CHECK(SelectMagazinePackedPalette(tracking,&tracking,tracking.target->deadlineNs,posed,ordinary,true).fallback);
 CHECK(SelectMagazinePackedPalette(tracking,&tracking,f.now,posed,ordinary,false).fallback);
 auto wrong=tracking;auto changed=std::make_shared<SelectedMeshesSnapshot>(*tracking.selected);
 ++changed->soleConfiguredArray;wrong.selected=changed;
 CHECK(SelectMagazinePackedPalette(tracking,&wrong,f.now,posed,ordinary,true).fallback);
 wrong=tracking;++wrong.owner.equipGeneration;CHECK(!BuildMagazinePresentation(rig,*binding,wrong,Pose(),1,f.now).binding);

 // A supported mag-out gun still spends its one chambered round. That authority
 // revision retires the previous private palette without seating/refilling.
 CHECK(ledger.ReconcileAcceptedShots(IdentityKey,1,0,60).Applied());++revision;f.Send(true,.7f);
 CHECK(!ledger.Snapshot().insertedMagazine&&!ledger.CanFire()&&ledger.Snapshot().spent==1&&ledger.Conserved());
 CHECK(f.result.blockFire&&f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Hidden);
 CHECK(SelectMagazinePackedPalette(tracking,&f.result.tracking,f.now,posed,ordinary,true).fallback);
 CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Dropped&&ledger.Magazine(1)->rounds==10);
 // A new incarnation at the same addresses cannot retain that magazine pose.
 auto replacement=IdentityKey;++replacement.weaponGeneration;ledger={};
 CHECK(ledger.Initialize(replacement,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,60,true,true));++revision;
 CHECK(!BuildMagazinePresentation(rig,*binding,tracking,Pose(),1,f.now).binding);
 // Support must also preserve a genuinely seated magazine. A held target
 // gives the magazine its own geometry while support retains wrist/fingers.
 {Fixture seated;seated.Send();const auto p=BuildMagazinePresentation(rig,*binding,seated.result.tracking,Pose(2,3,4),1,seated.now);
  CHECK(p.binding&&!p.hideMagazine&&!p.wristTarget&&MagazinePresentationWrites(p,true).size()==3);
  CHECK(p.writes[0].transform.values==Multiply(Xm8MagazineGeometry().attachedItem,Pose(2,3,4)).values);}
 {Fixture held;CHECK(held.Pull());const auto p=BuildMagazinePresentation(rig,*binding,held.result.tracking,Pose(),1,held.now);
  CHECK(p.binding&&p.wristTarget&&MagazinePresentationWrites(p,false).size()==19);
  CHECK(MagazinePresentationWrites(p,true).size()==3);}
 return 0;
}
}
int main(){
#ifdef FVR_BC2_NATIVE_RACK_PHASE_HEADER
 // Exact private source curves exercise the real frontend, claims and delayed
 // authority queue. The remaining legacy-coordinate cases use its kill switch.
 _putenv_s("FVR_BC2_NATIVE_XM8_RACK","1");
 {
  Fixture f(false);const auto& phases=generated::xm8_rack_proposal::NativeActionPoses;
  const auto send=[&](bool grip,const math::Matrix4& wrist){f.sample.raw.rawLeftWristWorldMeters=wrist;f.Send(grip);};
  const auto start=phases[1].frames.front().wristInWeapon;
  send(false,start);send(false,start);send(true,start);send(true,start);
  CHECK(f.result.rackOwnsLeft&&f.result.nativeRack&&f.result.nativeRack->ownsHand);
  for(unsigned phase=1;phase<=2;++phase)for(std::size_t i=1;i<phases[phase].frames.size();++i)
   send(true,phases[phase].frames[i].wristInWeapon);
  const auto rear=phases[2].frames.back().wristInWeapon;
  CHECK(f.result.nativeRackRearStop&&f.result.rackOwnsLeft&&f.result.blockFire&&f.Count(PhysicalWeaponCommandKind::CycleAction)==0);
  const auto token=f.hands.Current(InteractionHand::Left)->token;
  // Readiness/revision changes cannot drop a held mechanism at the rear.
  ++revision;send(true,rear);send(true,rear);
  CHECK(f.result.rackOwnsLeft&&f.hands.Current(InteractionHand::Left)->token==token);
  const auto geometrySequence=f.result.nativeRack->progressEvidence.sequence;
  f.sample.raw.valid=false;
  for(unsigned missing=0;missing<8;++missing){send(true,rear);
   CHECK(f.result.rackOwnsLeft&&f.result.nativeRack&&f.hands.Current(InteractionHand::Left)->token==token);
   CHECK(f.result.nativeRack->progressEvidence.sequence==geometrySequence&&f.result.nativeRack->pose.progress==1);}
  // A combat export hitch can exceed the arbiter's 100 ms lease while the
  // next actual input remains focused/tracked and the same squeeze stays down.
  // Rebind both expired claims without changing the sampled action/progress.
  const auto action=f.result.nativeRack->actionToken;const auto oldGun=f.gun->token;
  f.now+=130000000;send(true,rear);
  CHECK(f.result.rackOwnsLeft&&f.result.nativeRack&&f.result.blockFire);
  CHECK(f.gun->token!=oldGun&&f.hands.Current(InteractionHand::Left)->token!=token);
  CHECK(f.hands.Current(InteractionHand::Left)->token.prerequisiteClaim==f.gun->token.id);
  CHECK(f.result.nativeRack->actionToken==action&&f.result.nativeRack->progressEvidence.sequence==geometrySequence);
  CHECK(f.result.nativeRack->pose.progress==1&&f.Count(PhysicalWeaponCommandKind::CycleAction)==0);
  send(false,rear);CHECK(!f.result.rackOwnsLeft&&!f.hands.Current(InteractionHand::Left));
  CHECK(f.result.nativeRack&&!f.result.nativeRack->ownsHand&&f.result.blockFire&&!f.frontend.BlocksEquipment());
  bool forward=false,closed=false;
  // The original released return also survives an expired GunHold. Its hand
  // stays free, and only completing the return can submit the chamber cycle.
  const auto releasedGun=f.gun->token;f.now+=130000000;send(false,rear);
  forward|=f.result.nativeRackForwardStarted;closed|=f.result.nativeRackClosed;
  CHECK(f.gun->token!=releasedGun&&!f.hands.Current(InteractionHand::Left));
  CHECK(f.result.nativeRack||f.result.nativeRackClosed);
  if(f.result.nativeRack){CHECK(f.result.nativeRack->actionToken==action&&!f.result.nativeRack->ownsHand&&f.result.blockFire);
   CHECK(f.result.nativeRack->progressEvidence.sequence==geometrySequence);}
  for(unsigned i=0;i<25;++i){send(false,rear);forward|=f.result.nativeRackForwardStarted;closed|=f.result.nativeRackClosed;}
  CHECK(forward&&closed&&ledger.Conserved());
  unsigned cycles=0;for(auto kind:submitted)cycles+=kind==PhysicalWeaponCommandKind::CycleAction;CHECK(cycles==1);
 }
 // Riding the original handle forward keeps the hand until KF2's <=35%
 // axial home threshold. Released partial returns join the same native tail.
 for(bool releasePartway:{false,true}){
  Fixture f(false);const auto& phases=generated::xm8_rack_proposal::NativeActionPoses;
  const auto send=[&](bool grip,const math::Matrix4& wrist){f.sample.raw.rawLeftWristWorldMeters=wrist;f.Send(grip);};
  const auto start=phases[1].frames.front().wristInWeapon;
  send(false,start);send(false,start);send(true,start);send(true,start);
  for(unsigned phase=1;phase<=2;++phase)for(std::size_t i=1;i<phases[phase].frames.size();++i)
   send(true,phases[phase].frames[i].wristInWeapon);
  CHECK(f.result.nativeRackRearStop&&f.result.rackOwnsLeft);
  const auto action=f.result.nativeRack->actionToken;
  const auto token=f.hands.Current(InteractionHand::Left)->token;
  const auto& pull=phases[2];unsigned forwardEvents=0,closedEvents=0,heldForward=0;
  bool freed=false;
  for(std::size_t i=pull.frames.size()-1;i>0;){--i;send(true,pull.frames[i].wristInWeapon);
   forwardEvents+=f.result.nativeRackForwardStarted;closedEvents+=f.result.nativeRackClosed;
   CHECK(f.result.nativeRack&&!f.result.nativeRackRearStop&&f.result.blockFire);
   if(f.result.rackOwnsLeft){++heldForward;
    CHECK(f.hands.Current(InteractionHand::Left)->token==token&&f.result.nativeRack->actionToken==action);
    CHECK(f.result.nativeRack->phase==NativeRackPhase::Rear&&f.frontend.BlocksEquipment());
    if(releasePartway&&heldForward==2){send(false,pull.frames[i].wristInWeapon);
     forwardEvents+=f.result.nativeRackForwardStarted;closedEvents+=f.result.nativeRackClosed;
     freed=true;break;}
   }else{freed=true;break;}
  }
  CHECK(freed&&heldForward>0&&f.result.nativeRack&&!f.result.nativeRack->ownsHand);
  CHECK(f.result.nativeRack->phase==NativeRackPhase::Return&&f.result.nativeRack->pose.progress>0);
  CHECK(!f.hands.Current(InteractionHand::Left)&&!f.frontend.BlocksEquipment());
  // No observed geometry is needed for the admitted original part-only tail.
  // Failed submissions and a delayed receipt must still submit one cycle.
  f.sample.raw.valid=false;submitFailures=2;holdReceipts=true;
  for(unsigned i=0;i<40;++i){send(!releasePartway,pull.frames.back().wristInWeapon);
   forwardEvents+=f.result.nativeRackForwardStarted;closedEvents+=f.result.nativeRackClosed;
   CHECK(!f.result.rackOwnsLeft&&!f.hands.Current(InteractionHand::Left));}
  unsigned cycles=0;for(const auto kind:submitted)cycles+=kind==PhysicalWeaponCommandKind::CycleAction;
  CHECK(forwardEvents==1&&closedEvents==1&&cycles==1&&f.Count(PhysicalWeaponCommandKind::CycleAction)==1);
  holdReceipts=false;send(!releasePartway,pull.frames.back().wristInWeapon);
  CHECK(f.result.racked&&ledger.Conserved());
 }
 // Releasing a partial original stroke owns the moving part until its native
 // cancellation tail closes. A ready chamber cannot fire through that tail.
 {
  Fixture f(false);const auto& phases=generated::xm8_rack_proposal::NativeActionPoses;
  const auto send=[&](bool grip,const math::Matrix4& wrist){f.sample.raw.rawLeftWristWorldMeters=wrist;f.Send(grip);};
  const auto start=phases[1].frames.front().wristInWeapon;
  send(false,start);send(false,start);send(true,start);send(true,start);
  const auto middle=phases[1].frames.size()/2;
  for(std::size_t i=1;i<=middle;++i)send(true,phases[1].frames[i].wristInWeapon);
  CHECK(f.result.rackOwnsLeft&&f.result.nativeRack&&f.result.nativeRack->pose.progress>0);
  // A renderer can also publish fresh geometry immediately after a longer
  // lease hitch. Rebase that actual sample; never infer stroke progress across
  // the unobserved interval or cancel merely on MotionGuard's old timestamp.
  const auto progress=f.result.nativeRack->pose.progress;
  const auto evidence=f.result.nativeRack->progressEvidence.sequence;
  const auto action=f.result.nativeRack->actionToken;
  f.now+=180000000;f.sample.raw.rawLeftWristWorldMeters=phases[1].frames[middle+1].wristInWeapon;
  f.Send(true,.1f,false,true);
  CHECK(f.result.rackOwnsLeft&&f.result.nativeRack&&f.result.nativeRack->actionToken==action);
  CHECK(f.result.nativeRack->pose.progress==progress&&f.result.nativeRack->progressEvidence.sequence==evidence);
  f.sample.raw.valid=false;send(false,start);
  CHECK(f.result.nativeRack&&f.result.nativeRack->phase==NativeRackPhase::CancelPrep&&!f.result.rackOwnsLeft);
  CHECK(f.result.blockFire&&!f.frontend.BlocksEquipment()&&ledger.CanFire());
  for(unsigned i=0;i<50;++i)send(false,start);
  CHECK(!f.result.blockFire&&submitted.empty()&&ledger.Conserved());
 }
 // Genuine identity/tracking loss, an early explicit release, or another
 // current hand owner cancels the grip. Recovery must never steal or recapture
 // that held squeeze, even if fresh geometry later returns.
 for(unsigned scenario=0;scenario<16;++scenario){
  const bool returning=scenario>=9;const unsigned fault=returning?scenario-9:scenario;
  Fixture f(false);const auto& phases=generated::xm8_rack_proposal::NativeActionPoses;
  const auto send=[&](bool grip,const math::Matrix4& wrist){f.sample.raw.rawLeftWristWorldMeters=wrist;f.Send(grip);};
  const auto start=phases[1].frames.front().wristInWeapon;
  send(false,start);send(false,start);send(true,start);send(true,start);
  for(unsigned phase=1;phase<=2;++phase)for(std::size_t i=1;i<phases[phase].frames.size();++i)
   send(true,phases[phase].frames[i].wristInWeapon);
  CHECK(f.result.rackOwnsLeft&&f.result.nativeRackRearStop);
  const auto rear=phases[2].frames.back().wristInWeapon;
  if(returning){send(false,rear);CHECK(f.result.nativeRack&&!f.result.nativeRack->ownsHand);}
  if(fault==0)++f.sample.input.owner.actorGeneration;
  if(fault==1)++f.sample.input.owner.equipGeneration;
  if(fault==2)++f.sample.input.owner.space;
  if(fault==3)f.sample.input.tracked[0]=false;
  if(fault==4)f.sample.input.focused=false;
  if(fault==5)++f.sample.trackingEpoch;
  if(fault==6)++f.sample.nativeOwner.weak;
  if(fault==7)CHECK(f.hands.Release(f.sample.input,f.hands.Current(InteractionHand::Left)->token).accepted);
  if(fault==8){f.now+=130000000;f.acquireSupport=true;}
  f.sample.raw.valid=false;send(!returning,rear);
  CHECK(!f.result.rackOwnsLeft&&!f.result.nativeRack&&submitted.empty());
  if(fault==8)CHECK(f.support&&f.hands.Current(InteractionHand::Left)->token==f.support->token);
  f.sample.input.focused=true;f.sample.input.tracked[0]=true;f.sample.raw.valid=true;
  f.sample.raw.owner=f.sample.nativeOwner;f.meshes->owner=f.sample.nativeOwner;
  for(unsigned i=0;i<25;++i){send(true,rear);CHECK(!f.result.rackOwnsLeft&&!f.result.nativeRack);}
  CHECK(submitted.empty()&&ledger.Conserved());
 }
 _putenv_s("FVR_BC2_NATIVE_XM8_RACK","0");
#endif
 CHECK(!ReleasedSpasInsertionFreesActualClaimAndCommitsOnce());
 CHECK(!ReleasedSpasInsertionRejectsOwnerRevisionAndCancellation());
 // A pouch squeeze starts a tactical reload while the old magazine is still
 // installed, without a button or a receiver grab. That same squeeze owns
 // the replacement, and only the committed authority result drops the old one.
 {Fixture f(false);const auto pouch=Bc2MagazinePhysicalReload::DefaultPouch().pouchCenterMeters;
  f.sample.bodyFromHand=Pose(pouch[0],pouch[1],pouch[2]);f.Send();f.Send(true,-.1f);
  CHECK(queued.size()==1&&queued[0].kind==PhysicalWeaponCommandKind::DrawMagazine);
  CHECK(ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==11&&f.frontend.ProbeState(f.now).ownsHand);
  f.Send(true,-.1f);CHECK(f.result.physicalDrop&&f.result.physicalDropFromWell);
  CHECK(!ledger.Snapshot().insertedMagazine&&ledger.Snapshot().chamber==1&&ledger.Snapshot().looseReserve==30);
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Dropped&&ledger.Magazine(1)->rounds==10);
  CHECK(ledger.Magazine(2)->location==PhysicalMagazineLocation::Held&&ledger.Magazine(2)->rounds==30);
  f.Send(true,-.1f);CHECK(!f.result.physicalDrop);
  for(float z=-.09f;z<.061f;z+=.01f)f.Send(true,z);
  f.Settle();CHECK(ledger.Snapshot().loaded==31&&ledger.CanFire()&&ledger.Conserved());
 }
 // Losing the hand before the draw receipt retains the real replacement;
 // recovery cannot mint ammunition or leave a permanent hand obligation.
 {Fixture f(false);const auto pouch=Bc2MagazinePhysicalReload::DefaultPouch().pouchCenterMeters;
  f.sample.bodyFromHand=Pose(pouch[0],pouch[1],pouch[2]);f.Send();f.Send(true,-.1f);
  f.frontend.Cancel(f.sample.input,f.hands);f.Send(false,-.1f);f.Send();
  CHECK(!f.frontend.BlocksEquipment()&&!f.frontend.ProbeState(f.now).ownsHand);
  CHECK(ledger.Magazine(2)->location==PhysicalMagazineLocation::Retained&&ledger.Magazine(2)->rounds==30);
  CHECK(ledger.Snapshot().chamber==1&&ledger.Snapshot().looseReserve==30&&ledger.Conserved());
 }
 // Render-only body offers use the physical ledger, whose presentation has
 // no legacy reserve lease. Both loose reserve and retained magazines are
 // finite offers; an authority change retires the old offer immediately.
 {Fixture f(false);f.frontend.EnableBodyAmmo();f.Send();CHECK(f.result.bodyAmmo&&f.result.tracking.bodySupply);
  BodyAmmoTracking source{*f.result.bodyAmmo,f.result.tracking,{}};CHECK(BodyAmmoFresh(source,f.now));
  CHECK(source.visual.source==*source.magazine->bodySupply&&source.visual.source.reserveUnits==60);
  InputFrame input{};input.generation=source.visual.input.sequence;input.spaceGeneration=source.visual.input.owner.space;
  input.predictedNs=f.now;input.focused=input.headValid=true;input.referenceHead.orientation.w=input.head.orientation.w=1;
  for(auto& hand:input.hands){hand.gripTracked=hand.aimTracked=true;hand.grip.orientation.w=hand.aim.orientation.w=1;}
  const auto pose=BuildBodyAmmoPose(source,input,Pose(2,3,4),f.now);CHECK(pose);
  const auto anchor=AmmoSupplyAnchorWorld(input,Pose(2,3,4),source.visual);CHECK(anchor);
  const auto grasp=Multiply(Xm8MagazineEquipment().geometry->interaction.insertion.itemFromHand,pose->partWorld);
  for(unsigned n=0;n<3;++n)CHECK(Near(grasp.values[3][n],anchor->values[3][n]));
  CHECK(!BodyAmmoFresh(source,source.visual.input.deadlineNs));
  auto wrong=source;++wrong.visual.source.objectUnits;CHECK(!BodyAmmoFresh(wrong,f.now));
  wrong=source;++wrong.visual.input.owner.space;CHECK(!BodyAmmoFresh(wrong,f.now));
  ++revision;CHECK(!BodyAmmoFresh(source,f.now));CHECK(!BuildBodyAmmoPose(source,input,Pose(),f.now));
  f.Eject();f.Draw();CHECK(!f.result.bodyAmmo&&!f.result.tracking.bodySupply);
 }
 {Fixture f;ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,0,true,true));
  f.frontend.EnableBodyAmmo();f.Send();CHECK(!f.result.bodyAmmo);
  CHECK(f.Pull());f.frontend.Cancel(f.sample.input,f.hands);Commit();f.Send();f.Send();
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Retained&&ledger.Snapshot().looseReserve==0);
  CHECK(f.result.bodyAmmo&&f.result.bodyAmmo->source.reserveUnits==10&&f.result.bodyAmmo->source.objectUnits==10);
  const BodyAmmoTracking source{*f.result.bodyAmmo,f.result.tracking,{}};CHECK(BodyAmmoFresh(source,f.now));
  CHECK(ledger.Conserved());
 }
 // A removal also works while the ignored receiver/support grip stays held.
 {Fixture f(false);f.Send();for(unsigned n=0;n<6;++n)f.Send(true,.1f);
  f.Send(true,.1f,true);f.Send(true,.1f);f.Send(true,.1f);
  CHECK(!ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==1&&ledger.Magazine(1)->location==PhysicalMagazineLocation::Dropped&&ledger.Conserved());}
 // Default receiver grip cannot claim or unseat the magazine. A still ejects,
 // and physical pouch pickup/insertion still completes a tactical reload.
 {Fixture f(false);CHECK(!f.Pull());CHECK(queued.empty()&&submitted.empty()&&!f.frontend.ProbeState(f.now).ownsHand);
  CHECK(ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==11&&ledger.Conserved());
  CHECK(f.frontend.HoverCues().magazineHover==0);
  f.Eject();CHECK(!ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==1);
  f.Draw();f.Settle();CHECK(ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==31&&ledger.CanFire()&&ledger.Conserved());}
 // The same default also preserves empty-magazine insertion and A chamber
 // fallback. Receiver-grab policy does not participate in action commands.
 {Fixture f(false);ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},0,60,false,true));
  f.Eject();f.Draw();f.Settle();CHECK(ledger.Snapshot().needsRack&&!ledger.CanFire());
  f.Send(false,.06f,true);f.Send();CHECK(ledger.Snapshot().chamber==1&&ledger.CanFire()&&ledger.Conserved());}
 CHECK(EmptyWellSupportKeepsMagazineGeometryAndChamber()==0);
 {Fixture f;CHECK(f.Pull());const auto p=f.frontend.ProbeState(f.now);
  CHECK(p.ownsHand&&ledger.Snapshot().loaded==1&&ledger.CanFire());
  // Outgoing motion must never capture its own insertion guide.
  for(unsigned n=0;n<6;++n){f.Send(true,-.03f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);}
  unsigned drops=0;f.Send(false,-.03f);drops+=f.result.physicalDrop;f.Send(false,-.03f);drops+=f.result.physicalDrop;
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Dropped&&ledger.Magazine(1)->rounds==10);
  CHECK(ledger.Conserved()&&!f.frontend.ProbeState(f.now).ownsHand);
  // The applied drop receipt is reported to the presentation exactly once.
  for(unsigned n=0;n<4;++n){f.Send(false,-.03f);drops+=f.result.physicalDrop;}
  CHECK(drops==1);
 }
 // Zone table: a grab 10.5 cm out along the rail misses the old 9 cm radius but
 // is inside the 12 cm magazine zone; entering the zone with an open hand
 // gives exactly one hover tick, and staying inside does not repeat it.
 {Fixture legacy;for(unsigned n=0;n<2;++n)legacy.Send(false,-.005f);legacy.Send(true,-.005f);legacy.Send(true,-.005f);
  CHECK(!legacy.frontend.ProbeState(legacy.now).ownsHand&&legacy.frontend.HoverCues().magazineHover==0);}
 {Fixture f;f.frontend.ConfigureZones(DefaultInteractionZones());unsigned hovers=0;
  for(unsigned n=0;n<4;++n){f.Send(false,-.005f);for(unsigned k=0;k<f.result.feedbackCount;++k)hovers+=f.result.feedback[k].kind==FeedbackKind::ReloadHover;}
  CHECK(hovers==1&&f.frontend.HoverCues().magazineHover==1);
  f.Send(true,-.005f);f.Send(true,-.005f);CHECK(f.frontend.ProbeState(f.now).ownsHand);}
 // A cancel between the drop submit and its receipt still presents the drop:
 // the ledger holds the magazine as Dropped (it used to vanish).
 {Fixture f;CHECK(f.Pull());f.Send(false,-.03f);CHECK(queued.size()==1&&queued[0].kind==PhysicalWeaponCommandKind::DropMagazine);
  f.frontend.Cancel(f.sample.input,f.hands);f.Send(false,-.03f);
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Dropped&&f.result.physicalDrop&&!f.result.physicalDropFromWell);
 }
 {Fixture f;CHECK(f.Pull());f.frontend.Cancel(f.sample.input,f.hands);Commit();
  for(unsigned n=0;n<3;++n)f.frontend.Cancel(f.sample.input,f.hands);
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Retained);
 CHECK(ledger.Snapshot().loaded==1&&ledger.Snapshot().looseReserve==60&&ledger.Conserved());
 }
 {Fixture f;CHECK(f.Pull());f.frontend.Cancel(f.sample.input,f.hands);
  CHECK(queued.size()==1&&queued[0].kind==PhysicalWeaponCommandKind::RetainMagazine);
  const auto expired=queued[0].id;ExpireQueued();f.now+=600000000;f.Send();
  // A 500 ms runtime expiry is a rejection, not an applied cleanup. Keep the
  // original incarnation and magazine until a fresh retain receipt succeeds.
  CHECK(receipts[expired-1].status==physicalWeaponRuntime::CommandStatus::Rejected);
  CHECK(queued.size()==1&&queued[0].id!=expired&&queued[0].identity==IdentityKey&&queued[0].magazine==1);
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Held&&ledger.Magazine(1)->rounds==10);
  f.Send();CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Retained&&ledger.Conserved());
  const auto pouch=Bc2MagazinePhysicalReload::DefaultPouch().pouchCenterMeters;
  f.sample.bodyFromHand=Pose(pouch[0],pouch[1],pouch[2]);f.Send();f.Send(true,-.1f);f.Send(true,-.1f);
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Retained&&ledger.Magazine(1)->rounds==10);
  CHECK(ledger.Magazine(2)->location==PhysicalMagazineLocation::Held&&ledger.Magazine(2)->rounds==30);
  CHECK(f.frontend.ProbeState(f.now).ownsHand&&ledger.Snapshot().looseReserve==30&&ledger.Conserved());
 }
 {Fixture f;CHECK(f.Pull());submitFailures=1;f.frontend.Cancel(f.sample.input,f.hands);
  CHECK(queued.empty()&&ledger.Magazine(1)->location==PhysicalMagazineLocation::Held);
  f.Send();CHECK(queued.size()==1&&queued[0].identity==IdentityKey);
  f.Send();CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Retained&&ledger.Conserved());
 }
 // A suspended old weapon can finish its metadata-only held-magazine cleanup.
 // Its unknown native ammunition stays quarantined; another healthy gun can eject.
 {Fixture f;CHECK(f.Pull());firstSuspended=firstNativeBusy=true;
  const auto before=ledger.Snapshot();f.frontend.Cancel(f.sample.input,f.hands);
  CHECK(queued.size()==1&&queued[0].kind==PhysicalWeaponCommandKind::RetainMagazine);
  auto second=IdentityKey;second.weapon+=4;++second.weaponGeneration;
  CHECK(secondLedger.Initialize(second,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,60,true,true));
  f.sample.nativeOwner.weapon=0x40004;++f.sample.nativeOwner.equipGeneration;
  ++f.sample.input.owner.equipGeneration;f.sample.weapon={0xb0004,f.sample.input.owner.equipGeneration};
  f.meshes->owner=f.sample.raw.owner=f.sample.nativeOwner;
  f.Send();CHECK(!f.frontend.BlocksEquipment());
  const auto after=ledger.Snapshot();CHECK(after.loaded==before.loaded&&after.looseReserve==before.looseReserve&&after.chamber==before.chamber);
  CHECK(after.live==before.live&&after.spent==before.spent&&after.discarded==before.discarded&&ledger.Conserved());
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Retained);
  CHECK(firstSuspended&&firstNativeBusy&&!physicalWeaponRuntime::Read(0x40000)->readiness.physicalCanFire);
  CHECK(!f.result.blockFire&&secondLedger.CanFire());
  f.Send(false,.1f,true);f.Send();f.Send();
  CHECK(!secondLedger.Snapshot().insertedMagazine&&secondLedger.Snapshot().loaded==1&&secondLedger.CanFire()&&secondLedger.Conserved());
  CHECK(ledger.Snapshot().loaded==before.loaded&&ledger.Snapshot().looseReserve==before.looseReserve);
 }
 {Fixture f;CHECK(f.Pull());f.frontend.Cancel(f.sample.input,f.hands);ExpireQueued();
  // Reusing the same client/item addresses must never retarget the old cleanup
  // to a replacement's magazine 1. Its weapon generation is different.
  auto replacement=IdentityKey;++replacement.weaponGeneration;ledger={};
  CHECK(ledger.Initialize(replacement,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,60,true,true));
  const auto before=serial;f.sample.cancel=true;f.Send();f.Send();
  CHECK(serial==before&&queued.empty()&&!f.frontend.ProbeState(f.now).pending);
  CHECK(ledger.Magazine(1)->location==PhysicalMagazineLocation::Inserted&&ledger.Snapshot().loaded==11&&ledger.Conserved());
 }
 {Fixture f;f.Send();f.Send(false,.1f,true);f.Send();f.Send();
  CHECK(!ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==1);
  const auto pouch=Bc2MagazinePhysicalReload::DefaultPouch().pouchCenterMeters;
  f.sample.bodyFromHand=Pose(pouch[0],pouch[1],pouch[2]);f.Send();f.Send(true,-.1f);f.Send(true,-.1f);
  CHECK(f.frontend.ProbeState(f.now).acquired==1&&ledger.Snapshot().looseReserve==30);
  for(float z=-.09f;z<.061f;z+=.01f)f.Send(true,z);
  // Renderer geometry is one packet behind input; releasing after the
  // insertion threshold must keep the claim until the slider seats.
  CHECK(f.frontend.LatchesLoadingHand());
  for(unsigned n=0;n<20;++n)f.Send(false,.06f);
  CHECK(f.frontend.ProbeState(f.now).completed==1&&ledger.Snapshot().loaded==31&&ledger.Conserved());
  const auto commands=serial;for(unsigned n=0;n<10;++n)f.Send(false,.06f);
  CHECK(serial==commands&&ledger.Snapshot().loaded==31);
 }
 // --- Short B after a physical insertion (docs/RELOAD-SHORT-B-AFTER-INSERT-20261009.md) ---
 using K=PhysicalWeaponCommandKind;
 const auto ejects=[]{unsigned n=0;for(const auto k:submitted)n+=k==K::UnseatMagazine;return n;};
 const auto racks=[]{unsigned n=0;for(const auto k:submitted)n+=k==K::CycleAction;return n;};
 // Pure decision table and environment switch.
 {ShortReloadInput in;in.pressed=true;in.inserted=true;in.loaded=31;
  CHECK(DecideShortReload(in,true)==ShortReloadAction::Eject&&DecideShortReload(in,false)==ShortReloadAction::Eject);
  in.afterInsert=true;CHECK(DecideShortReload(in,true)==ShortReloadAction::Confirm&&DecideShortReload(in,false)==ShortReloadAction::Eject);
  in.needsRack=true;in.loaded=30;CHECK(DecideShortReload(in,true)==ShortReloadAction::Rack&&DecideShortReload(in,false)==ShortReloadAction::Rack);
  in.seatInFlight=true;CHECK(DecideShortReload(in,true)==ShortReloadAction::Latch&&DecideShortReload(in,false)==ShortReloadAction::None);
  ShortReloadInput latched;latched.latched=true;latched.inserted=true;latched.loaded=31;
  CHECK(DecideShortReload(latched,true)==ShortReloadAction::None); // a seat that did not land never ejects
  latched.busy=true;latched.pressed=true;CHECK(DecideShortReload(latched,true)==ShortReloadAction::None);
  ShortReloadInput idle;CHECK(DecideShortReload(idle,true)==ShortReloadAction::None);
  const auto env=[](const char* value){return [value](std::string_view){return std::string_view(value);};};
  CHECK(ParseShortReloadRackAfterInsert(env(""))&&ParseShortReloadRackAfterInsert(env("1"))&&!ParseShortReloadRackAfterInsert(env("0")));}
 // Playtest 2026-10-09 (XM8, seat 02:38:41, B 02:38:43): tactical reload with
 // a round kept in the chamber, insert, short B. It used to eject the new
 // magazine; now it confirms, and the magazine stays seated and ready.
 {Fixture f;f.Eject();CHECK(!ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==1&&ejects()==1);
  f.Draw();f.Settle();
  CHECK(ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==31&&ledger.Snapshot().chamber==1&&!ledger.Snapshot().needsRack);
  const auto seated=ledger.Snapshot().insertedMagazine;
  f.Send(false,.06f,true);bool tick=false;
  for(unsigned k=0;k<f.result.feedbackCount;++k)tick|=f.result.feedback[k].kind==FeedbackKind::ReloadReady&&f.result.feedback[k].hand==1;
  f.Send();f.Send();
  CHECK(tick&&ejects()==1&&racks()==0&&ledger.Snapshot().insertedMagazine==seated&&ledger.Snapshot().loaded==31&&ledger.CanFire());
  CHECK(f.frontend.ShortReload().confirms==1&&f.frontend.ShortReload().ejects==1);
  // Repeated presses inside the window do not bleed rounds or eject.
  for(unsigned n=0;n<3;++n){f.Send(false,.06f,true);f.Send();}
  CHECK(ejects()==1&&ledger.Snapshot().loaded==31&&ledger.Snapshot().discarded==0&&ledger.Conserved());
  // Once the reload is idle (window over), short B is the deliberate eject again.
  f.now+=ShortReloadAfterInsertNs;f.Send(false,.06f,true);f.Send();f.Send();
  CHECK(ejects()==2&&!ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==1&&ledger.Conserved());
 }
 // A settled seat can subsequently be empty or have native projection work
 // outstanding. Short B acknowledges the seat but must not promise READY.
 for(const bool empty:{false,true}){Fixture f;f.Eject();f.Draw();f.Settle();
  if(empty)CHECK(ledger.ObserveExternalAmmo(IdentityKey,ledger.NextCommandId(),0,ledger.Snapshot().looseReserve).Applied());
  else projectionPending=true;
  const auto commands=serial;
  f.Send(false,.06f,true);bool hover=false,ready=false;
  for(unsigned k=0;k<f.result.feedbackCount;++k){
   hover|=f.result.feedback[k].kind==FeedbackKind::ReloadHover&&f.result.feedback[k].hand==1;
   ready|=f.result.feedback[k].kind==FeedbackKind::ReloadReady;
  }
  CHECK(hover&&!ready&&serial==commands&&ledger.Snapshot().insertedMagazine&&ledger.Conserved());
 }
 // A shot after the insertion ends the window: the next short B ejects.
 {Fixture f;f.Eject();f.Draw();f.Settle();CHECK(ledger.Snapshot().loaded==31);
  CHECK(ledger.ReconcileAcceptedShots(IdentityKey,1,30,ledger.Snapshot().looseReserve).Applied());++revision;
  f.Send(false,.06f,true);f.Send();f.Send();CHECK(ejects()==2&&!ledger.Snapshot().insertedMagazine);
 }
 // Empty chamber: dry gun, eject the empty magazine, insert a full one, short
 // B racks (CycleAction) and the weapon can fire. A second B confirms only.
 {Fixture f;ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},0,60,false,true));
  CHECK(ledger.Snapshot().needsRack&&!ledger.CanFire());
  f.Eject();CHECK(!ledger.Snapshot().insertedMagazine&&ejects()==1);
  f.Draw();f.Settle();CHECK(ledger.Snapshot().loaded==30&&ledger.Snapshot().needsRack&&!ledger.Snapshot().chamber);
  f.Send(false,.06f,true);f.Send();f.Send();
  CHECK(racks()==1&&ejects()==1&&ledger.Snapshot().chamber==1&&!ledger.Snapshot().needsRack&&ledger.Snapshot().loaded==30&&ledger.CanFire());
  // The applied rack keeps the window open: a second B is a confirmation.
  f.Send(false,.06f,true);f.Send();f.Send();
  CHECK(racks()==1&&ejects()==1&&ledger.Snapshot().insertedMagazine&&ledger.Snapshot().loaded==30&&f.frontend.ShortReload().confirms==1);
  CHECK(f.frontend.ShortReload().racks==1&&ledger.Conserved());
 }
 // Button origin belongs to the command, not the submit frame. Delayed
 // completion must never masquerade as a physical rack for native audio.
 {Fixture f;ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},10,60,false,true));
  f.Send();holdReceipts=true;f.Send(false,.1f,true);
  CHECK(f.Count(K::CycleAction)==1&&!f.result.racked&&!f.result.boltReleased);
  f.Send();CHECK(!f.result.racked&&!f.result.boltReleased);
  holdReceipts=false;f.Send();CHECK(f.result.racked&&f.result.boltReleased);
  unsigned hands=0;for(unsigned n=0;n<f.result.feedbackCount;++n)if(f.result.feedback[n].kind==FeedbackKind::ReloadRack)hands|=1u<<f.result.feedback[n].hand;
  CHECK(hands==2&&ledger.CanFire());f.Send();CHECK(!f.result.racked&&!f.result.boltReleased);
 }
 // Rejected, cancelled and changed-owner action receipts cannot emit an
 // audio/haptic completion, even when the command reached native authority.
 for(unsigned fault=0;fault<3;++fault){Fixture f;ledger={};
  CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},10,60,false,true));
  f.Send();holdReceipts=true;f.Send(false,.1f,true);CHECK(f.Count(K::CycleAction)==1);
  if(fault==0)ExpireQueued();else {Commit();if(fault==1)f.frontend.Cancel(f.sample.input,f.hands);else ++f.sample.input.owner.actorGeneration;}
  holdReceipts=false;f.Send();CHECK(!f.result.racked&&!f.result.boltReleased);
  for(unsigned n=0;n<f.result.feedbackCount;++n)CHECK(f.result.feedback[n].kind!=FeedbackKind::ReloadRack);
 }
 // Ready open-bolt weapons permit an optional handle gesture. Holding it
 // suppresses fire; cocking changes no ammunition and never forces a reload rack.
 {Fixture f;f.profile.actionKind=MagazineActionKind::OpenBolt;ledger={};
  CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::OpenBoltMagazine,30,30},11,60,false,true));
  const auto before=ledger.Snapshot();CHECK(!before.needsRack&&ledger.CanFire());
  f.Handle(false,0);f.Handle(true,0);CHECK(f.result.rackOwnsLeft&&f.result.blockFire);
  holdReceipts=true;
  const auto stroke=f.profile.chargingHandle->strokeMeters;
  for(unsigned n=1;n<=12;++n)f.Handle(true,stroke*n/12);
  holdReceipts=false;f.Handle(false,stroke);CHECK(f.result.racked);
  const auto after=ledger.Snapshot();CHECK(after.loaded==before.loaded&&after.feed==before.feed&&after.looseReserve==before.looseReserve);
  CHECK(after.spent==before.spent&&after.discarded==before.discarded&&!after.needsRack&&ledger.CanFire()&&ledger.Conserved());
 }
 // Support transfers atomically to an intentional rack using the original
 // renderer contact. A stale contact drops ownership and requires a new grab.
 {Fixture f;ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},10,60,false,true));
  f.Handle(false,0);f.acquireSupport=true;f.Handle(true,0);CHECK(f.support&&f.result.rackOwnsLeft);
  const auto mechanism=f.hands.Current(InteractionHand::Left);CHECK(mechanism&&mechanism->token.kind==HandClaimKind::Mechanism);
  CHECK(mechanism->token.id!=f.support->token.id&&mechanism->token.prerequisiteClaim==f.gun->token.id);
  f.sample.raw.valid=false;f.Handle(true,.06f);CHECK(!f.result.rackOwnsLeft&&!f.hands.Current(InteractionHand::Left)&&queued.empty());
  f.sample.raw.valid=true;f.Handle(true,.12f);CHECK(queued.empty());
 }
 // A physical handle returns to Idle at its rear stop. The later receipt
 // still belongs to the hand gesture and provides both-hand completion.
 {Fixture f;ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},10,60,false,true));
  f.Handle(false,0);f.Handle(true,0);holdReceipts=true;
  const auto stroke=FindMagazineEquipment(f.sample.asset)->chargingHandle->strokeMeters;
  for(unsigned n=1;n<=12;++n)f.Handle(true,stroke*n/12);
  CHECK(f.Count(K::CycleAction)==1&&!f.result.racked);f.Handle(false,stroke);
  holdReceipts=false;f.Handle(false,stroke);CHECK(f.result.racked&&!f.result.boltReleased);
  unsigned hands=0;for(unsigned n=0;n<f.result.feedbackCount;++n)if(f.result.feedback[n].kind==FeedbackKind::ReloadRack)hands|=1u<<f.result.feedback[n].hand;
  CHECK(hands==3&&ledger.CanFire()&&ledger.Conserved());
 }
 // B pressed while the slider is still settling: latched, then resolved by the
 // seat receipt into the rack. Nothing is ejected.
 {Fixture f;ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},0,60,false,true));
  f.Eject();f.Draw();CHECK(f.frontend.LatchesLoadingHand());
  f.Send(false,.06f,true);CHECK(f.frontend.ShortReload().latched==1);
  f.Settle();
  CHECK(ejects()==1&&racks()==1&&ledger.Snapshot().chamber==1&&ledger.CanFire()&&f.frontend.ShortReload().latchResolved==1);
 }
 // B pressed while the SeatMagazine receipt is in flight (the authority has
 // not updated yet): it waits for the receipt, then racks.
 {Fixture f;ledger={};CHECK(ledger.Initialize(IdentityKey,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},0,60,false,true));
  f.Eject();f.Draw();
  unsigned n=0;while(!f.Count(K::SeatMagazine)&&n++<40){holdReceipts=true;f.Send(false,.06f);}
  CHECK(f.Count(K::SeatMagazine)==1&&f.frontend.ProbeState(f.now).pending);
  f.Send(false,.06f,true);f.Send(false,.06f);
  CHECK(f.frontend.ShortReload().latched==1&&racks()==0&&ejects()==1&&!ledger.Snapshot().insertedMagazine);
  holdReceipts=false;f.Settle(4);
  CHECK(racks()==1&&ejects()==1&&ledger.Snapshot().insertedMagazine&&ledger.Snapshot().chamber==1&&ledger.CanFire());
 }
 // The seat receipt is rejected (authority expiry): the latched press is
 // dropped after the grace window. It never becomes an eject.
 {Fixture f;f.Eject();f.Draw();
  unsigned n=0;while(!f.Count(K::SeatMagazine)&&n++<40){holdReceipts=true;f.Send(false,.06f);}
  f.Send(false,.06f,true);CHECK(f.frontend.ShortReload().latched==1);
  ExpireQueued();holdReceipts=false;for(unsigned k=0;k<60;++k)f.Send(false,.06f);
  CHECK(ejects()==1&&racks()==0&&f.frontend.ShortReload().latchDropped==1&&!ledger.Snapshot().insertedMagazine&&ledger.Conserved());
 }
 // Kill switch FVR_BC2_SHORT_B_RACK_AFTER_INSERT=0: the aca6ffd behaviour, a
 // short B right after a tactical insertion ejects the new magazine.
 {Fixture f;f.frontend.EnableShortRackAfterInsert(false);f.Eject();f.Draw();f.Settle();
  CHECK(ledger.Snapshot().loaded==31&&ledger.Snapshot().insertedMagazine);
  f.Send(false,.06f,true);f.Send();f.Send();
  CHECK(ejects()==2&&!ledger.Snapshot().insertedMagazine&&f.frontend.ShortReload().confirms==0);
 }
 return 0;
}
