#include "Bc2PhysicalTubeInteraction.h"
#include <limits>

namespace fvr::bc2 {
namespace {
using namespace interaction;
using namespace reload_insertion_detail;
bool SameInput(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
bool CurrentAuthority(const PhysicalWeaponIdentity& identity,std::uint64_t revision,std::uint32_t client)noexcept {
    const auto current=physicalWeaponRuntime::Read(client);
    return current&&!current->suspended&&current->identity==identity&&current->revision==revision;
}
PhysicalShellAuthority Authority(const physicalWeaponRuntime::View& view,const PhysicalReloadSample& s,std::uint64_t operation)noexcept {
    return {view.identity,view.revision,operation,s.nativeOwner.weapon,s.input.observedNs,s.input.deadlineNs,CurrentAuthority};
}
}
interaction::AmmoSupplyConfig Bc2PhysicalTubeInteraction::DefaultPouch()noexcept {
    return {InteractionHand::Left,0x424332414d4d4full,{0x424332504f5543ull,1},{-.23f,-.55f,.02f},.18f,200000000};
}
Bc2PhysicalTubeInteraction::Bc2PhysicalTubeInteraction(bool enabled,AmmoSupplyConfig pouch,PhysicalWeaponFamily family)noexcept
    :enabled_(enabled&&(family==PhysicalWeaponFamily::TubePump||family==PhysicalWeaponFamily::TubeAutomatic)),family_(family),supply_(pouch){}
void Bc2PhysicalTubeInteraction::Cancel(const HandInteractionSample& input,HandInteraction& hands,PhysicalReloadCancelReason,unsigned)noexcept {
    if(supply_.Held()||targets_)++cancelled_;
    supply_.Cancel(input,hands);insertion_.Reset();targets_.reset();settling_.reset();released_.reset();insertionProgress_=insertionAlignment_=0;insertionRawWrist_.reset();history_={};lastRaw_=0;
    // An accepted command belongs to the runtime and can still complete. Keep
    // its ID until a receipt; releasing a visual shell never refunds ammunition.
}
void Bc2PhysicalTubeInteraction::Feedback(PhysicalReloadResult& out,FeedbackKind kind,const HandInteractionSample& input,unsigned hand)noexcept {
    if(out.feedbackCount>=out.feedback.size()||nextFeedback_==UINT64_MAX||!input.focused||!input.tracked[0]||!input.tracked[1])return;
    const FeedbackEvent event{nextFeedback_+1,input.sequence,input.owner.space,input.observedNs,
        std::min(input.deadlineNs,input.observedNs+100000000),kind,hand};
    if(!ValidFeedback(event,input.nowNs))return;
    ++nextFeedback_;out.feedback[out.feedbackCount++]=event;
}
void Bc2PhysicalTubeInteraction::PumpRearFeedback(PhysicalReloadResult& out,const HandInteractionSample& input)noexcept {
    Feedback(out,FeedbackKind::PumpRear,input,0);Feedback(out,FeedbackKind::PumpGunKnock,input,1);
}
void Bc2PhysicalTubeInteraction::PumpRackFeedback(PhysicalReloadResult& out,const HandInteractionSample& input)noexcept {
    Feedback(out,FeedbackKind::PumpForward,input,0);Feedback(out,FeedbackKind::PumpForward,input,1);
}
void Bc2PhysicalTubeInteraction::FinishPending(PhysicalReloadResult& out,const HandInteractionSample& input)noexcept {
    if(!pending_)return;
    const auto receipt=physicalWeaponRuntime::Result(pending_);
    if(!receipt||receipt->status==physicalWeaponRuntime::CommandStatus::Pending)return;
    if(receipt->status==physicalWeaponRuntime::CommandStatus::Applied){
        ++completed_;
        if(settling_&&settling_->operation==pending_){
            const auto current=physicalWeaponRuntime::Read(settling_->nativeOwner.weapon);
            if(current&&!current->suspended&&current->identity==settling_->weapon)settling_->revision=current->revision;
            else settling_.reset();
        }
        if(pendingWeapon_==weapon_&&input.owner==handOwner_)Feedback(out,FeedbackKind::ReloadApplied,input);
    }else{
        ++rejected_;if(settling_&&settling_->operation==pending_)settling_.reset();
        if(pendingWeapon_==weapon_&&input.owner==handOwner_)Feedback(out,FeedbackKind::ReloadFailed,input);
    }
    pending_=0;pendingWeapon_={};
}
void Bc2PhysicalTubeInteraction::PublishSettling(const PhysicalReloadSample& s,const physicalWeaponRuntime::View& view,
    const HandClaim& gun,PhysicalReloadResult& out)noexcept {
    if(!settling_)return;
    const auto& tail=*settling_;
    if(tail.weapon!=view.identity||tail.nativeOwner!=s.nativeOwner||tail.handOwner!=s.input.owner||
       tail.revision!=view.revision||s.input.nowNs<tail.startedNs||s.input.nowNs>=tail.deadlineNs){settling_.reset();return;}
    ReloadPreview preview;preview.phase=ReloadPreviewPhase::Settling;preview.selectedMeshes=s.meshes;preview.weaponClaim=gun;
    preview.actionConstrained=true;preview.actionHandOwned=false;
    preview.actionProgress=float(double(s.input.nowNs-tail.startedNs)/double(tail.deadlineNs-tail.startedNs));
    preview.settlingStartedNs=tail.startedNs;preview.settlingDeadlineNs=tail.deadlineNs;
    preview.physicalAuthority=Authority(view,s,tail.operation);
    preview.physicalAuthority->deadlineNs=std::min(preview.physicalAuthority->deadlineNs,tail.deadlineNs);
    auto& target=preview.targets;
    target.identity={s.input.owner,s.weapon,{tail.operation,tail.weapon.weaponGeneration},s.trackingEpoch};
    target.weaponClaim=gun.token;target.inputSequence=s.input.sequence;target.observedNs=s.input.observedNs;
    target.deadlineNs=std::min({s.input.deadlineNs,gun.deadlineNs,preview.physicalAuthority->deadlineNs,tail.deadlineNs});
    try{out.tracking.preview=std::make_shared<const ReloadPreview>(std::move(preview));}catch(...){}
}
bool Bc2PhysicalTubeInteraction::AcceptInsertion(const ReloadInsertionResult& result,const ReloadInsertionSample& contact,
    const PhysicalReloadSample& s,const physicalWeaponRuntime::View& view,const HandClaim& gun,
    HandInteraction& hands,PhysicalReloadResult& out)noexcept {
    insertionProgress_=result.insertFraction;insertionAlignment_=result.alignment;insertionRawWrist_=result.rawHand;
    if(result.seat){
        // No reserve counter or fake native acknowledgement is touched here.
        // The server-thread ledger serializes InsertRound and debits one unit.
        const auto command=physicalWeaponRuntime::Submit(view.identity,PhysicalWeaponCommandKind::InsertRound);
        if(command){
            pending_=command;pendingWeapon_=view.identity;++submitted_;
            // Native 1P_Reload's shell continues from the finger release
            // at .4 s to its terminal .4333333 s pose. This short tail is
            // presentation only: the freed hand and ammo operation do not
            // wait for it, and a refused authority receipt cancels it.
            settling_=SettlingShell{view.identity,s.nativeOwner,s.input.owner,command,view.revision,
                s.input.nowNs,s.input.nowNs+33333333};
            Feedback(out,FeedbackKind::ReloadSeated,s.input);Feedback(out,FeedbackKind::ReloadGunSeat,s.input,1);
        }
        else{++rejected_;Feedback(out,FeedbackKind::ReloadFailed,s.input);}
        supply_.Cancel(s.input,hands);insertion_.Reset();targets_.reset();history_={};
        out.ammoOwnsHand=false;
        if(command)PublishSettling(s,view,gun,out);
        released_.reset();out.latchedLoadingHand=false;return true;
    }
    if(result.cancelled){supply_.Cancel(s.input,hands);released_.reset();targets_.reset();insertion_.Reset();out.ammoOwnsHand=false;out.latchedLoadingHand=false;return true;}
    if(result.handFreed){
        released_=ReleasedShell{contact,view.revision,s.input.nowNs,s.input.nowNs+250000000};
        released_->contact.itemReleased=true;
        // Retain the original insertion identity, not a live hand claim. The
        // player may immediately use this hand for the fore-end or another grip.
        supply_.Cancel(s.input,hands);history_={};out.settleStarted=true;
        out.ammoOwnsHand=false;out.latchedLoadingHand=false;
    }
    if(released_){PublishReleased(s,view,gun,out);return true;}
    return false;
}
void Bc2PhysicalTubeInteraction::PublishReleased(const PhysicalReloadSample& s,const physicalWeaponRuntime::View& view,
    const HandClaim& gun,PhysicalReloadResult& out)noexcept {
    if(!released_)return;
    const auto& flight=*released_;
    ReloadPreview preview;preview.phase=ReloadPreviewPhase::ReleasedInsert;preview.selectedMeshes=s.meshes;preview.weaponClaim=gun;
    preview.actionConstrained=true;preview.actionHandOwned=false;preview.actionProgress=insertionProgress_;
    preview.settlingStartedNs=flight.startedNs;preview.settlingDeadlineNs=flight.deadlineNs;
    preview.physicalAuthority=Authority(view,s,flight.contact.identity.item.generation);
    preview.physicalAuthority->deadlineNs=std::min(preview.physicalAuthority->deadlineNs,flight.deadlineNs);
    auto& target=preview.targets;
    target.identity={s.input.owner,s.weapon,{preview.physicalAuthority->operation,view.identity.weaponGeneration},s.trackingEpoch};
    target.weaponClaim=gun.token;target.inputSequence=s.input.sequence;target.observedNs=s.input.observedNs;
    target.deadlineNs=std::min({s.input.deadlineNs,gun.deadlineNs,preview.physicalAuthority->deadlineNs,s.input.observedNs+100000000});
    try{out.tracking.preview=std::make_shared<const ReloadPreview>(std::move(preview));}catch(...){}
}
const Bc2PhysicalTubeInteraction::Evidence* Bc2PhysicalTubeInteraction::Original(const PhysicalReloadSample& s)const noexcept {
    if(!s.raw.valid||s.raw.owner!=s.nativeOwner||s.raw.rigFingerprint!=SpasReloadRig||!s.originalHandEvidence||
        !SameInput(s.raw.inputEvidence,*s.originalHandEvidence)||s.raw.inputEvidence.deadlineNs<=s.input.nowNs)return nullptr;
    for(const auto& e:history_)if(e&&SameInput(e->input,s.raw.inputEvidence)&&e->input.owner==s.input.owner&&
        e->gun.token.item==s.weapon)return &*e;
    return nullptr;
}
PhysicalReloadResult Bc2PhysicalTubeInteraction::Tick(const PhysicalReloadSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    PhysicalReloadResult out;if(!enabled_)return out;
    FinishPending(out,s.input);
    out.latchedLoadingHand=LatchesLoadingHand();
    const auto cancel=[&](PhysicalReloadCancelReason reason){
        Cancel(s.input,hands,reason,s.cancelFlags);out.ammoOwnsHand=false;out.latchedLoadingHand=false;out.tracking.preview.reset();
    };
    if(s.input.nowNs<lastNow_){cancel(PhysicalReloadCancelReason::ApiOrClock);return out;}lastNow_=s.input.nowNs;
    out.tracking={true,s.nativeOwner,s.input,{}};
    const auto gun=hands.Current(InteractionHand::Right);
    const bool safe=ReloadTrackingFresh(out.tracking,s.input.nowNs)&&gun&&gun->token.owner==s.input.owner&&
        gun->token.kind==HandClaimKind::GunHold&&gun->token.item==s.weapon&&gun->inputSequence==s.input.sequence&&
        gun->deadlineNs>s.input.nowNs&&s.weapon.id==s.nativeOwner.weapon&&s.weapon.generation==s.input.owner.equipGeneration&&
        s.trackingEpoch==s.nativeOwner.space;
    if(!safe||s.cancel||s.asset!=SpasReloadAsset){cancel(!safe?PhysicalReloadCancelReason::UnsafeInput:
        s.cancel?PhysicalReloadCancelReason::RequestedInput:PhysicalReloadCancelReason::WrongAsset);out.tracking.enabled=false;return out;}
    if(!physicalWeaponRuntime::ConfigureWeapon(s.nativeOwner.weapon,family_)){cancel(PhysicalReloadCancelReason::MissingSource);return out;}
    const auto view=physicalWeaponRuntime::Read(s.nativeOwner.weapon);
    if(!view||view->suspended||!view->state.initialized||view->state.config.family!=family_){
        cancel(PhysicalReloadCancelReason::MissingSource);return out;}
    if(weapon_!=view->identity||nativeOwner_!=s.nativeOwner||handOwner_!=s.input.owner){
        cancel(PhysicalReloadCancelReason::OwnerChanged);weapon_=view->identity;nativeOwner_=s.nativeOwner;handOwner_=s.input.owner;
    }
    // Diagnostic counts are ledger observations, never fake native hold/idle receipts.
    Bc2AmmoReserveLease counts;counts.identity.owner=s.nativeOwner;counts.sequence=view->revision;
    counts.observedNs=s.input.observedNs;counts.deadlineNs=s.input.deadlineNs;
    counts.loaded=int(view->state.loaded);counts.reserve=int(view->state.looseReserve);counts.capacity=int(view->state.config.maxLoaded);
    diagnosticReserve_=counts;
    const bool mesh=s.meshes&&FindSelectedMesh(*s.meshes,s.nativeOwner,SelectedMeshKind::Spas12,s.input.nowNs);
    if(mesh){
        ReloadShellControl control;control.weaponClaim=*gun;control.selectedMeshes=s.meshes;control.physicalAuthority=Authority(*view,s,0);
        try{out.tracking.shellControl=std::make_shared<const ReloadShellControl>(std::move(control));}catch(...){}
        PublishSettling(s,*view,*gun,out);
    }else settling_.reset();
    // Submission is one-shot. While its authoritative receipt is outstanding
    // there is no second shell acquisition, even after cancellation/switching.
    if(released_){
        if(!mesh||view->revision!=released_->revision||s.input.nowNs>=released_->deadlineNs){cancel(PhysicalReloadCancelReason::MissingSource);return out;}
        auto contact=released_->contact;
        contact.weaponClaim=*gun;contact.sequence=contact.geometrySequence=s.input.sequence;
        contact.observedNs=s.input.observedNs;contact.nowNs=s.input.nowNs;
        contact.deadlineNs=std::min(s.input.deadlineNs,gun->deadlineNs);
        contact.focused=s.input.focused;contact.weaponTracked=s.input.tracked[1];contact.itemTracked=false;contact.held=false;
        const auto result=insertion_.Update(contact);
        AcceptInsertion(result,contact,s,*view,*gun,hands,out);
        return out;
    }
    if(pending_||settling_)return out;
    if(!view->state.looseReserve||view->state.feed>=view->state.config.feedCapacity||view->state.loaded>=view->state.config.maxLoaded){
        cancel(view->state.looseReserve?PhysicalReloadCancelReason::FullMagazine:PhysicalReloadCancelReason::EmptyReserve);return out;
    }
    const auto profile=SpasReloadInsertionProfile(feel_);
    AmmoSupplySource source{{s.input.owner,s.weapon,{profile.id,profile.revision},{view->identity.weapon,view->identity.weaponGeneration},s.trackingEpoch},
        ReloadInsertionFamily::SingleShell,view->state.looseReserve,1,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true};
    if(intent==UINT64_MAX){cancel(PhysicalReloadCancelReason::IntentExhausted);return out;}
    AmmoSupplySample supply{s.input,source,s.geometrySequence,s.trackingEpoch,s.bodyFromHand,s.gripPressed||out.latchedLoadingHand,++intent};
    const auto supplied=supply_.Update(supply,hands);
    if(supplied.hoverEntered)Feedback(out,FeedbackKind::ReloadHover,s.input);
    if(supplied.acquired){++acquired_;insertion_.Reset();targets_.reset();history_={};lastRaw_=0;Feedback(out,FeedbackKind::ReloadGrab,s.input);}
    if(!supply_.Held()){insertion_.Reset();targets_.reset();return out;}
    out.ammoOwnsHand=true;
    const auto& held=*supply_.Held();
    bool recorded=false;for(const auto& e:history_)if(e&&SameInput(e->input,s.input)){recorded=true;break;}
    if(!recorded){history_[historyNext_]=Evidence{s.input,held.claim,*gun};historyNext_=(historyNext_+1)%history_.size();}
    const auto original=Original(s);
    if(mesh&&original&&original->input.sequence>lastRaw_&&original->shell.token==held.claim.token&&original->gun.token==gun->token&&
        Rigid(s.raw.rawLeftWristWorldMeters)&&Rigid(s.raw.weaponWorldMeters)){
        ReloadInsertionSample contact;
        contact.identity={s.input.owner,s.weapon,held.item,s.trackingEpoch};contact.profile={profile.id,profile.revision};
        contact.itemClaim=original->shell;contact.weaponClaim=original->gun;
        contact.sequence=contact.geometrySequence=original->input.sequence;contact.observedNs=original->input.observedNs;
        contact.deadlineNs=std::min({original->input.deadlineNs,original->shell.deadlineNs,original->gun.deadlineNs});contact.nowNs=s.input.nowNs;
        contact.focused=s.input.focused;contact.itemTracked=s.input.tracked[0];contact.weaponTracked=s.input.tracked[1];
        contact.held=s.gripPressed;contact.eligible=true;
        contact.weaponFromHand=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));
        const auto result=insertion_.Update(contact);lastRaw_=original->input.sequence;
        if(result.captured)Feedback(out,FeedbackKind::ReloadCapture,s.input);
        out.settleStarted=result.handFreed;
        if(result.guidedItem){
            targets_=Bc2ReloadTargets{contact.identity,contact.itemClaim.token,contact.weaponClaim.token,contact.sequence,0,
                contact.observedNs,std::min(contact.deadlineNs,contact.observedNs+100000000),*result.guidedItem,Multiply(profile.itemFromHand,*result.guidedItem)};
        }else targets_.reset();
        if(AcceptInsertion(result,contact,s,*view,*gun,hands,out))return out;
    }
    if(mesh){
        ReloadPreview preview;preview.selectedMeshes=s.meshes;preview.shellClaim=held.claim;preview.weaponClaim=*gun;
        preview.physicalAuthority=Authority(*view,s,held.item.generation);
        if(targets_&&targets_->deadlineNs>s.input.nowNs){
            preview.phase=ReloadPreviewPhase::Guided;preview.targets=*targets_;
            preview.actionConstrained=true;preview.actionProgress=insertionProgress_;
            preview.actionAlignment=insertionAlignment_;preview.actionRawWristInWeaponMeters=insertionRawWrist_;
        }
        try{out.tracking.preview=std::make_shared<const ReloadPreview>(std::move(preview));}catch(...){}
    }
    return out;
}
PhysicalReloadProbeState Bc2PhysicalTubeInteraction::ProbeState(std::int64_t now)const noexcept {
    PhysicalReloadProbeState state;state.active=bool(supply_.Held())||bool(released_)||pending_;state.held=bool(supply_.Held());
    state.pending=pending_!=0;state.acquired=acquired_;state.submitted=submitted_;state.completed=completed_;
    if(diagnosticReserve_&&diagnosticReserve_->deadlineNs>now)state.reserve=diagnosticReserve_;
    if(const auto view=physicalWeaponRuntime::Read(nativeOwner_.weapon);view&&!view->suspended&&view->identity==weapon_){
        state.physicalState=view->state;
        // Publish one coherent diagnostic read. A server shot can land between
        // Tick and this observer; never combine its new ledger with old counts.
        Bc2AmmoReserveLease counts;counts.identity.owner=nativeOwner_;counts.sequence=view->revision;
        counts.observedNs=now;counts.deadlineNs=now+100000000;
        counts.loaded=int(view->state.loaded);counts.reserve=int(view->state.looseReserve);counts.capacity=int(view->state.config.maxLoaded);
        state.reserve=counts; // verified/allThree/native-hold flags stay false.
    }
    // nativeHolding, native cycle and native identity deliberately remain empty.
    return state;
}
void Bc2PhysicalTubeInteraction::Report(std::ostream& out)const {
    out<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"held\":"<<(supply_.Held()?"true":"false")
        <<",\"pending\":"<<pending_<<",\"acquired\":"<<acquired_<<",\"submitted\":"<<submitted_
        <<",\"completed\":"<<completed_<<",\"rejected\":"<<rejected_<<",\"cancelled\":"<<cancelled_<<'}';
}
}
