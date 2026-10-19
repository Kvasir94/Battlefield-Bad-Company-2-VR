#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fvr::interaction {

// One ledger per owned weapon lifetime, retained across equip, hand, tracking
// and presentation changes. Pointer reuse requires a new weaponGeneration.
struct PhysicalWeaponIdentity {
    std::uint64_t actor=0,actorGeneration=0,weapon=0,weaponGeneration=0;
    bool operator==(const PhysicalWeaponIdentity&)const=default;
};
enum class PhysicalWeaponFamily : std::uint8_t {
    Unsupported,ClosedBoltMagazine,OpenBoltMagazine,NoActionMagazine,TubePump,TubeAutomatic
};
struct PhysicalWeaponConfig {
    PhysicalWeaponFamily family=PhysicalWeaponFamily::Unsupported;
    // Feed capacity excludes the chamber. maxLoaded is the engine's allowed
    // pooled total, so a native capacity of 30 need not silently become 30+1.
    std::uint32_t feedCapacity=0,maxLoaded=0;
    // Explicit pistol policy; existing families retain their behavior.
    bool locksOpenOnEmpty=false;
    bool operator==(const PhysicalWeaponConfig&)const=default;
};
enum class PhysicalMagazineLocation : std::uint8_t {Unused,Inserted,Held,Retained,Dropped};
struct PhysicalMagazine {
    std::uint64_t id=0;
    std::uint32_t rounds=0,capacity=0;
    PhysicalMagazineLocation location=PhysicalMagazineLocation::Unused;
};
enum class PhysicalWeaponCommandKind : std::uint8_t {
    AcquireMagazine,UnseatMagazine,SeatMagazine,RetainMagazine,DropMagazine,
    RecoverMagazine,UnloadMagazine,ForgetDroppedMagazine,InsertRound,CycleAction,
    RefillMagazine,AddReserve,ReconcileExternalAmmo,OpenAction,CloseAction,DrawMagazine
};
inline constexpr bool PhysicalMagazineMetadataCleanup(PhysicalWeaponCommandKind kind)noexcept {
    return kind==PhysicalWeaponCommandKind::RetainMagazine||kind==PhysicalWeaponCommandKind::DropMagazine;
}
struct PhysicalWeaponCommand {
    PhysicalWeaponIdentity identity{};
    // Monotonic within this weapon lifetime. A rejected command is consumed:
    // a delayed retry cannot become a different physical operation later.
    std::uint64_t id=0;
    PhysicalWeaponCommandKind kind=PhysicalWeaponCommandKind::AcquireMagazine;
    std::uint64_t magazine=0;
    // Acquire: requested rounds (zero = as many as fit); AddReserve: an exact
    // adapter-verified pickup. Other commands must use zero.
    std::uint32_t rounds=0;
    // Used only by ReconcileExternalAmmo, for an explicitly identified server
    // pickup/refill/correction, never for an unexplained native reload.
    std::uint32_t nativeLoaded=0,nativeReserve=0;
    bool operator==(const PhysicalWeaponCommand&)const=default;
};
enum class PhysicalWeaponResultCode : std::uint8_t {
    Applied,Duplicate,Uninitialized,IdentityMismatch,Invalid,Stale,IdCollision,
    Unsupported,Unavailable,Capacity,UnexpectedNativeState,FireBlocked,Exhausted
};
struct PhysicalWeaponResult {
    PhysicalWeaponResultCode code=PhysicalWeaponResultCode::Invalid;
    std::uint64_t magazine=0;
    std::uint32_t shots=0;
    std::uint64_t droppedMagazine=0;
    bool Applied()const noexcept{return code==PhysicalWeaponResultCode::Applied;}
};
struct PhysicalWeaponSnapshot {
    PhysicalWeaponIdentity identity{};
    PhysicalWeaponConfig config{};
    std::uint64_t insertedMagazine=0,commandWatermark=0,shotWatermark=0;
    std::uint32_t loaded=0,feed=0,looseReserve=0;
    std::uint8_t chamber=0;
    bool initialized=false,needsRack=false,pumpOwed=false,canFire=false;
    bool actionOpen=false,actionLocked=false;
    // Conservation: live rounds + spent + discarded == admitted. Dropped and
    // retained magazines remain live until explicitly forgotten/unloaded.
    std::uint64_t live=0,spent=0,discarded=0,admitted=0;
};

// Pure finite inventory/mechanics. No native pointers, input or animation.
// Adapter serializes all access on its authority update thread. Native loaded
// is chamber + installed feed; native reserve is looseReserve ONLY. Rounds in
// detached magazines are deliberately absent from those native projections.
// Publish after an Applied command; reconcile accepted shots BEFORE commands.
// A native write failure does not justify applying the command again: retry
// publication of Snapshot(), or stop firing until authority can be reconciled.
// Transactions may use a value copy: candidate=state; candidate.Apply(command);
// write/read back candidate's native projection; state=candidate only on success.
// Do not issue commands concurrently or let native firing race that transaction.
class PhysicalWeaponState {
public:
    static constexpr std::size_t MaxMagazines=32;
    static constexpr std::uint32_t MaxCapacity=4096;
    static constexpr std::uint32_t MaxReserve=1000000;

    static bool ValidConfig(const PhysicalWeaponConfig& c)noexcept {
        if(c.family<=PhysicalWeaponFamily::Unsupported||c.family>PhysicalWeaponFamily::TubeAutomatic||
           !c.feedCapacity||c.feedCapacity>MaxCapacity||!c.maxLoaded||c.maxLoaded>MaxCapacity)return false;
        const bool chamber=c.family==PhysicalWeaponFamily::ClosedBoltMagazine||
            c.family==PhysicalWeaponFamily::TubePump||c.family==PhysicalWeaponFamily::TubeAutomatic;
        return (!c.locksOpenOnEmpty||c.family==PhysicalWeaponFamily::ClosedBoltMagazine)&&
            c.maxLoaded<=c.feedCapacity+(chamber?1u:0u);
    }
    // Initial chamber/action readiness is supplied by the verified adapter;
    // positive pooled ammo alone is not evidence for a chamber after takeover.
    bool Initialize(PhysicalWeaponIdentity identity,PhysicalWeaponConfig config,
                    std::uint32_t loaded,std::uint32_t reserve,
                    bool chamberReady,bool actionReady)noexcept {
        if(initialized_||!ValidIdentity(identity)||!ValidConfig(config)||loaded>config.maxLoaded||reserve>MaxReserve)return false;
        const bool hasChamber=HasChamber(config.family);
        const auto chamber=hasChamber&&chamberReady&&loaded?1u:0u;
        if(loaded-chamber>config.feedCapacity||(!hasChamber&&chamberReady))return false;
        if(config.family==PhysicalWeaponFamily::TubePump&&!actionReady&&chamber)return false;
        identity_=identity;config_=config;initialized_=true;reserve_=reserve;chamber_=static_cast<std::uint8_t>(chamber);
        needsRack_=hasChamber?!chamber_:config.family==PhysicalWeaponFamily::OpenBoltMagazine&&!actionReady;
        pumpOwed_=config.family==PhysicalWeaponFamily::TubePump&&!actionReady;
        if(IsMagazine()) {
            magazines_[0]={nextMagazine_++,loaded-chamber,config.feedCapacity,PhysicalMagazineLocation::Inserted};
            inserted_=magazines_[0].id;
        }else tube_=loaded-chamber;
        admitted_=static_cast<std::uint64_t>(loaded)+reserve;
        return true;
    }
    PhysicalWeaponSnapshot Snapshot()const noexcept {
        PhysicalWeaponSnapshot s;
        s.identity=identity_;s.config=config_;s.insertedMagazine=inserted_;
        s.commandWatermark=lastCommand_.id;s.shotWatermark=lastShot_;
        s.feed=Feed();s.chamber=chamber_;s.loaded=s.feed+chamber_;s.looseReserve=reserve_;
        s.initialized=initialized_;s.needsRack=needsRack_;s.pumpOwed=pumpOwed_;s.canFire=CanFire();
        s.actionOpen=actionOpen_;s.actionLocked=actionLocked_;
        s.live=static_cast<std::uint64_t>(reserve_)+tube_+chamber_;
        for(const auto& m:magazines_)if(m.location!=PhysicalMagazineLocation::Unused)s.live+=m.rounds;
        s.spent=spent_;s.discarded=discarded_;s.admitted=admitted_;
        return s;
    }
    const std::array<PhysicalMagazine,MaxMagazines>& Magazines()const noexcept{return magazines_;}
    bool NativeReloadBusy()const noexcept{return nativeReloadActive_;}
    std::uint64_t NativeReloadToken()const noexcept{return nativeReloadToken_;}
    // An explicit native transaction may pool pocket magazines, never ground
    // magazines. Run on a value copy and publish its projection before letting
    // the engine transfer ammunition. A held object's cleanup must finish first.
    PhysicalWeaponResult BeginNativeReload(PhysicalWeaponIdentity identity,std::uint64_t token)noexcept {
        using R=PhysicalWeaponResultCode;using L=PhysicalMagazineLocation;
        if(!initialized_)return {R::Uninitialized};
        if(identity!=identity_)return {R::IdentityMismatch};
        if(!token)return {R::Invalid};
        if(nativeReloadActive_)return {token==nativeReloadToken_?R::Duplicate:R::Unavailable};
        if(token<=nativeReloadToken_)return {R::Stale};
        // A native completion cannot close an action still held by a hand.
        // A latched empty slide may enter the ordinary owned reload path.
        if(actionOpen_&&!actionLocked_)return {R::Unavailable};
        std::uint64_t pooled=reserve_;
        PhysicalMagazine* destination=nullptr;
        for(auto& m:magazines_) {
            if(m.location==L::Held)return {R::Unavailable};
            if(m.location==L::Retained)pooled+=m.rounds;
            if(!destination&&(m.location==L::Unused||m.location==L::Retained))destination=&m;
        }
        if(pooled>MaxReserve)return {R::Capacity};
        if(!pooled||Feed()+chamber_>=config_.maxLoaded)return {R::Unavailable};
        if(IsMagazine()&&!inserted_&&(!destination||nextMagazine_==std::numeric_limits<std::uint64_t>::max()))return {R::Capacity};
        for(auto& m:magazines_)if(m.location==L::Retained)m.rounds=0;
        reserve_=static_cast<std::uint32_t>(pooled);
        if(IsMagazine()&&!inserted_) {
            *destination={nextMagazine_++,0,config_.feedCapacity,L::Inserted};inserted_=destination->id;
        }
        nativeReloadToken_=token;nativeReloadActive_=true;
        nativeReloadTotal_=static_cast<std::uint64_t>(Feed())+chamber_+reserve_;
        nativeReloadLoaded_=Feed()+chamber_;nativeReloadReserve_=reserve_;
        return {R::Applied,inserted_};
    }
    // The adapter proves native completion/idle and exact ownership. Ordinary
    // native reload is a transfer, never a pickup: admitted/spent/discarded and
    // detached custody cannot change. An aborted, untransferred transaction
    // retains its previous chamber/action rather than inventing readiness.
    PhysicalWeaponResult FinishNativeReload(PhysicalWeaponIdentity identity,std::uint64_t token,
        std::uint32_t loaded,std::uint32_t reserve,bool nativeReady)noexcept {
        using R=PhysicalWeaponResultCode;
        if(!initialized_)return {R::Uninitialized};
        if(identity!=identity_)return {R::IdentityMismatch};
        if(!token||token!=nativeReloadToken_)return {R::Stale};
        if(!nativeReloadActive_)return {loaded==nativeReloadLoaded_&&reserve==nativeReloadReserve_&&nativeReady==nativeReloadReady_?R::Duplicate:R::IdCollision};
        if(loaded>config_.maxLoaded||reserve>MaxReserve||loaded<nativeReloadLoaded_||reserve>nativeReloadReserve_||
           static_cast<std::uint64_t>(loaded)+reserve!=nativeReloadTotal_)return {R::UnexpectedNativeState};
        const auto chamber=static_cast<std::uint8_t>(HasChamber(config_.family)?
            (nativeReady?(loaded?1u:0u):(loaded?chamber_:0u)):0u);
        if(loaded-chamber>config_.feedCapacity||(IsMagazine()&&!inserted_))return {R::UnexpectedNativeState};
        chamber_=chamber;SetFeed(loaded-chamber_);reserve_=reserve;
        if(nativeReady){needsRack_=HasChamber(config_.family)&&!chamber_;pumpOwed_=false;
            actionOpen_=actionLocked_=config_.locksOpenOnEmpty&&inserted_&&!loaded;}
        else if(HasChamber(config_.family)&&!chamber_)needsRack_=true;
        nativeReloadActive_=false;nativeReloadLoaded_=loaded;nativeReloadReserve_=reserve;nativeReloadReady_=nativeReady;
        return {R::Applied,inserted_};
    }
    // Convenience for a single serialized adapter owner; all command producers
    // must share this sequence, including external ammo corrections.
    std::uint64_t NextCommandId()const noexcept {
        return lastCommand_.id==std::numeric_limits<std::uint64_t>::max()?0:lastCommand_.id+1;
    }
    const PhysicalMagazine* Magazine(std::uint64_t id)const noexcept {
        if(id)for(const auto& m:magazines_)if(m.id==id&&m.location!=PhysicalMagazineLocation::Unused)return &m;
        return nullptr;
    }
    bool CanFire()const noexcept {
        if(!initialized_||pumpOwed_||nativeReloadActive_||actionOpen_)return false;
        if(HasChamber(config_.family))return chamber_!=0;
        return !needsRack_&&Feed()>0;
    }
    PhysicalWeaponResult Apply(const PhysicalWeaponCommand& c)noexcept {
        if(!initialized_)return {PhysicalWeaponResultCode::Uninitialized};
        if(c.identity!=identity_)return {PhysicalWeaponResultCode::IdentityMismatch};
        if(!c.id)return {PhysicalWeaponResultCode::Invalid};
        if(c.id<lastCommand_.id)return {PhysicalWeaponResultCode::Stale};
        if(c.id==lastCommand_.id) {
            if(c!=lastCommand_)return {PhysicalWeaponResultCode::IdCollision};
            auto r=lastResult_;if(r.Applied())r.code=PhysicalWeaponResultCode::Duplicate;return r;
        }
        lastCommand_=c;
        // Retain/drop change only a held resource's location. They cannot
        // interfere with the native loaded/reserve transfer transaction.
        lastResult_=nativeReloadActive_&&!PhysicalMagazineMetadataCleanup(c.kind)?
            PhysicalWeaponResult{PhysicalWeaponResultCode::Unavailable}:Execute(c);
        return lastResult_;
    }
    PhysicalWeaponResult ObserveExternalAmmo(PhysicalWeaponIdentity identity,
        std::uint64_t commandId,std::uint32_t nativeLoaded,std::uint32_t nativeReserve)noexcept {
        PhysicalWeaponCommand c;
        c.identity=identity;c.id=commandId;c.kind=PhysicalWeaponCommandKind::ReconcileExternalAmmo;
        c.nativeLoaded=nativeLoaded;c.nativeReserve=nativeReserve;return Apply(c);
    }
    bool Conserved()const noexcept {
        const auto s=Snapshot();return s.live+s.spent+s.discarded==s.admitted;
    }
    // Receipt ID is independent of command IDs. Only an authority observation
    // taken AFTER the adapter's last successful native publication is eligible.
    // No animation/native reload delta is imported as free ammunition. Pickup
    // additions must arrive as explicit AddReserve commands with their own ID.
    PhysicalWeaponResult ReconcileAcceptedShots(PhysicalWeaponIdentity identity,
        std::uint64_t receipt,std::uint32_t nativeLoaded,std::uint32_t nativeReserve)noexcept {
        if(!initialized_)return {PhysicalWeaponResultCode::Uninitialized};
        if(identity!=identity_)return {PhysicalWeaponResultCode::IdentityMismatch};
        if(nativeReloadActive_)return {PhysicalWeaponResultCode::FireBlocked};
        if(!receipt)return {PhysicalWeaponResultCode::Invalid};
        if(receipt<lastShot_)return {PhysicalWeaponResultCode::Stale};
        if(receipt==lastShot_) {
            if(nativeLoaded!=lastShotLoaded_||nativeReserve!=lastShotReserve_)return {PhysicalWeaponResultCode::IdCollision};
            auto r=lastShotResult_;if(r.Applied())r.code=PhysicalWeaponResultCode::Duplicate;return r;
        }
        lastShot_=receipt;lastShotLoaded_=nativeLoaded;lastShotReserve_=nativeReserve;
        const auto loaded=Feed()+chamber_;
        if(nativeReserve!=reserve_||nativeLoaded>loaded)
            return lastShotResult_={PhysicalWeaponResultCode::UnexpectedNativeState};
        const auto shots=loaded-nativeLoaded;
        if(shots==0)return lastShotResult_={PhysicalWeaponResultCode::Applied};
        if(!CanFire()||(config_.family==PhysicalWeaponFamily::TubePump&&shots!=1)||
           (IsMagazine()&&!inserted_&&shots!=1))return lastShotResult_={PhysicalWeaponResultCode::FireBlocked};
        if(HasChamber(config_.family)) {
            if(config_.family==PhysicalWeaponFamily::TubePump) {
                chamber_=0;pumpOwed_=true;needsRack_=true;
            }else {
                chamber_=static_cast<std::uint8_t>(nativeLoaded?1:0);
                SetFeed(nativeLoaded-chamber_);needsRack_=chamber_==0;
                if(config_.locksOpenOnEmpty&&inserted_&&!nativeLoaded)actionOpen_=actionLocked_=true;
            }
        }else SetFeed(nativeLoaded);
        spent_+=shots;
        return lastShotResult_={PhysicalWeaponResultCode::Applied,0,shots};
    }
private:
    static bool ValidIdentity(const PhysicalWeaponIdentity& i)noexcept {
        return i.actor&&i.actorGeneration&&i.weapon&&i.weaponGeneration;
    }
    static bool HasChamber(PhysicalWeaponFamily f)noexcept {
        return f==PhysicalWeaponFamily::ClosedBoltMagazine||f==PhysicalWeaponFamily::TubePump||f==PhysicalWeaponFamily::TubeAutomatic;
    }
    bool IsMagazine()const noexcept {
        return config_.family>=PhysicalWeaponFamily::ClosedBoltMagazine&&config_.family<=PhysicalWeaponFamily::NoActionMagazine;
    }
    PhysicalMagazine* MutableMagazine(std::uint64_t id)noexcept {
        if(id)for(auto& m:magazines_)if(m.id==id&&m.location!=PhysicalMagazineLocation::Unused)return &m;
        return nullptr;
    }
    std::uint32_t Feed()const noexcept {
        if(!IsMagazine())return tube_;
        const auto* m=Magazine(inserted_);return m?m->rounds:0;
    }
    void SetFeed(std::uint32_t rounds)noexcept {
        if(!IsMagazine())tube_=rounds;
        else if(auto* m=MutableMagazine(inserted_))m->rounds=rounds;
    }
    void OpenClosedBoltAction()noexcept {
        if(chamber_){++discarded_;chamber_=0;}
        actionOpen_=true;actionLocked_=false;needsRack_=true;
    }
    void CloseClosedBoltAction()noexcept {
        if(config_.locksOpenOnEmpty&&inserted_&&!Feed()){
            actionOpen_=actionLocked_=true;needsRack_=true;return;
        }
        if(Feed()){SetFeed(Feed()-1);chamber_=1;}
        actionOpen_=actionLocked_=false;needsRack_=!chamber_;
    }
    PhysicalWeaponResult Execute(const PhysicalWeaponCommand& c)noexcept {
        using K=PhysicalWeaponCommandKind;using L=PhysicalMagazineLocation;using R=PhysicalWeaponResultCode;
        if(c.kind>K::DrawMagazine||(c.rounds&&c.kind!=K::AcquireMagazine&&c.kind!=K::AddReserve)||
           ((c.nativeLoaded||c.nativeReserve)&&c.kind!=K::ReconcileExternalAmmo))return {R::Invalid};
        auto* m=MutableMagazine(c.magazine);
        switch(c.kind) {
        case K::DrawMagazine: {
            // One pouch gesture exchanges the installed magazine and supplies
            // its replacement. Validate the whole exchange before changing
            // custody, so a refused draw cannot strand a half-applied reload.
            if(!IsMagazine())return {R::Unsupported};
            if(c.magazine)return {R::Invalid};
            if(inserted_&&Feed()>=config_.feedCapacity)return {R::Unavailable};
            PhysicalMagazine* retained=nullptr,*slot=nullptr,*oldestDrop=nullptr;
            for(auto& item:magazines_) {
                if(item.location==L::Held)return {R::Unavailable};
                if(item.location==L::Retained&&item.rounds&&(!retained||item.rounds>retained->rounds))retained=&item;
                if(!slot&&(item.location==L::Unused||(item.location==L::Retained&&!item.rounds)))slot=&item;
                if(item.location==L::Dropped&&(!oldestDrop||item.id<oldestDrop->id))oldestDrop=&item;
            }
            // A normal pouch draw gives a full fresh magazine when reserve
            // permits it. With less loose ammo, top up a real retained one.
            const bool recover=retained&&(retained->rounds==config_.feedCapacity||
                reserve_<config_.feedCapacity||(!slot&&!oldestDrop));
            const bool fresh=!recover&&reserve_!=0;
            if(fresh&&!slot)slot=oldestDrop;
            if(fresh&&(!slot||nextMagazine_==std::numeric_limits<std::uint64_t>::max()))return {R::Capacity};
            if(!recover&&!fresh&&!inserted_)return {R::Unavailable};
            const auto dropped=inserted_;
            if(auto* installed=MutableMagazine(inserted_))installed->location=L::Dropped;
            inserted_=0;
            std::uint64_t offered=0;
            if(recover) {
                const auto transfer=std::min(reserve_,retained->capacity-retained->rounds);
                retained->rounds+=transfer;reserve_-=transfer;retained->location=L::Held;offered=retained->id;
            }else if(fresh) {
                if(slot->location==L::Dropped)discarded_+=slot->rounds;
                const auto rounds=std::min(reserve_,config_.feedCapacity);
                *slot={nextMagazine_++,rounds,config_.feedCapacity,L::Held};reserve_-=rounds;offered=slot->id;
            }
            return {R::Applied,offered,0,dropped};
        }
        case K::AcquireMagazine: {
            if(!IsMagazine())return {R::Unsupported};
            if(c.magazine)return {R::Invalid};
            if(!reserve_)return {R::Unavailable};
            const auto rounds=c.rounds?c.rounds:(reserve_<config_.feedCapacity?reserve_:config_.feedCapacity);
            if(rounds>reserve_||rounds>config_.feedCapacity)return {R::Capacity};
            if(nextMagazine_==std::numeric_limits<std::uint64_t>::max())return {R::Exhausted};
            for(auto& slot:magazines_)if(slot.location==L::Unused) {
                slot={nextMagazine_++,rounds,config_.feedCapacity,L::Held};reserve_-=rounds;return {R::Applied,slot.id};
            }
            return {R::Capacity};
        }
        case K::UnseatMagazine:
            if(!IsMagazine())return {R::Unsupported};
            if(!inserted_||!m||m->id!=inserted_)return {R::Unavailable};
            m->location=L::Held;inserted_=0;return {R::Applied,m->id};
        case K::SeatMagazine:
            if(!IsMagazine())return {R::Unsupported};
            if(inserted_||!m||m->location!=L::Held)return {R::Unavailable};
            if(m->rounds+chamber_>config_.maxLoaded)return {R::Capacity};
            m->location=L::Inserted;inserted_=m->id;
            if(HasChamber(config_.family)&&!chamber_)needsRack_=true;
            return {R::Applied,m->id};
        case K::RetainMagazine:case K::DropMagazine:
            if(!m||m->location!=L::Held)return {R::Unavailable};
            m->location=c.kind==K::RetainMagazine?L::Retained:L::Dropped;return {R::Applied,m->id};
        case K::RecoverMagazine:
            if(!m||(m->location!=L::Retained&&m->location!=L::Dropped))return {R::Unavailable};
            m->location=L::Held;return {R::Applied,m->id};
        case K::UnloadMagazine:
            if(!m||(m->location!=L::Held&&m->location!=L::Retained))return {R::Unavailable};
            if(m->rounds>MaxReserve-reserve_)return {R::Capacity};
            reserve_+=m->rounds;m->rounds=0;return {R::Applied,m->id};
        case K::ForgetDroppedMagazine:
            if(!m||m->location!=L::Dropped)return {R::Unavailable};
            discarded_+=m->rounds;*m={};return {R::Applied,c.magazine};
        case K::InsertRound:
            if(IsMagazine())return {R::Unsupported};
            if(c.magazine)return {R::Invalid};
            if(!reserve_)return {R::Unavailable};
            if(tube_>=config_.feedCapacity||tube_+chamber_>=config_.maxLoaded)return {R::Capacity};
            ++tube_;--reserve_;return {R::Applied};
        case K::CycleAction:
            if(c.magazine)return {R::Invalid};
            if(config_.family==PhysicalWeaponFamily::NoActionMagazine)return {R::Unsupported};
            // Pump input may outlive a shot or a selection change. Recheck
            // action debt here so a stale/repeated stroke cannot eject a live
            // shell or cycle an already cleared empty shotgun.
            if(config_.family==PhysicalWeaponFamily::TubePump&&
               !pumpOwed_&&!(needsRack_&&Feed()))return {R::Unavailable};
            if(config_.family==PhysicalWeaponFamily::OpenBoltMagazine) {
                needsRack_=false;return {R::Applied}; // Cocking before seating is valid.
            }
            if(config_.family==PhysicalWeaponFamily::ClosedBoltMagazine){
                OpenClosedBoltAction();CloseClosedBoltAction();return {R::Applied};
            }
            // A deliberate full cycle ejects a live chambered round. The
            // adapter may create a cosmetic cartridge from this total change.
            if(chamber_){++discarded_;chamber_=0;}
            if(Feed()){SetFeed(Feed()-1);chamber_=1;}
            needsRack_=!chamber_;pumpOwed_=false;return {R::Applied};
        case K::OpenAction:
            if(c.magazine)return {R::Invalid};
            if(config_.family!=PhysicalWeaponFamily::ClosedBoltMagazine)return {R::Unsupported};
            OpenClosedBoltAction();return {R::Applied};
        case K::CloseAction:
            if(c.magazine)return {R::Invalid};
            if(config_.family!=PhysicalWeaponFamily::ClosedBoltMagazine)return {R::Unsupported};
            if(!actionOpen_)return {R::Unavailable};
            CloseClosedBoltAction();return {R::Applied};
        case K::AddReserve:
            if(c.magazine||!c.rounds)return {R::Invalid};
            if(c.rounds>MaxReserve-reserve_||admitted_>std::numeric_limits<std::uint64_t>::max()-c.rounds)return {R::Capacity};
            reserve_+=c.rounds;admitted_+=c.rounds;return {R::Applied};
        case K::RefillMagazine: {
            if(!m||(m->location!=L::Held&&m->location!=L::Retained))return {R::Unavailable};
            const auto missing=m->capacity-m->rounds;
            const auto transfer=missing<reserve_?missing:reserve_;
            if(!transfer)return {R::Unavailable};
            m->rounds+=transfer;reserve_-=transfer;return {R::Applied,m->id};
        }
        case K::ReconcileExternalAmmo: {
            if(c.magazine||c.rounds||c.nativeLoaded>config_.maxLoaded||c.nativeReserve>MaxReserve)return {R::Invalid};
            // Preserve physical readiness and every detached magazine. A server
            // refill cannot seat an absent magazine or clear an owed action.
            const auto chamber=static_cast<std::uint8_t>(c.nativeLoaded?chamber_:0);
            if((IsMagazine()&&!inserted_&&c.nativeLoaded!=chamber)||c.nativeLoaded-chamber>config_.feedCapacity)return {R::UnexpectedNativeState};
            const auto oldLoaded=Feed()+chamber_;
            std::uint64_t added=0,removed=0;
            if(c.nativeLoaded>=oldLoaded)added+=c.nativeLoaded-oldLoaded;else removed+=oldLoaded-c.nativeLoaded;
            if(c.nativeReserve>=reserve_)added+=c.nativeReserve-reserve_;else removed+=reserve_-c.nativeReserve;
            if(admitted_>std::numeric_limits<std::uint64_t>::max()-added)return {R::Exhausted};
            chamber_=chamber;SetFeed(c.nativeLoaded-chamber_);reserve_=c.nativeReserve;
            if(HasChamber(config_.family)&&!chamber_)needsRack_=true;
            admitted_+=added;discarded_+=removed;return {R::Applied};
        }
        }
        return {R::Invalid};
    }
    PhysicalWeaponIdentity identity_{};PhysicalWeaponConfig config_{};
    std::array<PhysicalMagazine,MaxMagazines> magazines_{};
    std::uint64_t inserted_=0,nextMagazine_=1,lastShot_=0,admitted_=0,spent_=0,discarded_=0;
    std::uint32_t reserve_=0,tube_=0,lastShotLoaded_=0,lastShotReserve_=0;
    std::uint8_t chamber_=0;
    bool initialized_=false,needsRack_=false,pumpOwed_=false;
    bool actionOpen_=false,actionLocked_=false;
    bool nativeReloadActive_=false,nativeReloadReady_=false;
    std::uint64_t nativeReloadToken_=0,nativeReloadTotal_=0;
    std::uint32_t nativeReloadLoaded_=0,nativeReloadReserve_=0;
    PhysicalWeaponCommand lastCommand_{};PhysicalWeaponResult lastResult_{},lastShotResult_{};
};
} // namespace fvr::interaction
