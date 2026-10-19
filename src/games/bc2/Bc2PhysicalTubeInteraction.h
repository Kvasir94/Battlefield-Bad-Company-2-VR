#pragma once
#include "Bc2PhysicalReload.h"
#include "Bc2PhysicalWeaponRuntime.h"

namespace fvr::bc2 {
// Transient hand/geometry controller. The runtime's per-weapon ledger owns all
// ammunition and survives this object's cancellation or weapon deselection.
class Bc2PhysicalTubeInteraction {
public:
    static interaction::AmmoSupplyConfig DefaultPouch()noexcept;
    explicit Bc2PhysicalTubeInteraction(bool enabled=true,interaction::AmmoSupplyConfig pouch=DefaultPouch(),
        interaction::PhysicalWeaponFamily family=interaction::PhysicalWeaponFamily::TubePump)noexcept;
    PhysicalReloadResult Tick(const PhysicalReloadSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&,
        PhysicalReloadCancelReason=PhysicalReloadCancelReason::External,unsigned sourceFlags=0)noexcept;
    PhysicalReloadProbeState ProbeState(std::int64_t now)const noexcept;
    bool LatchesLoadingHand()const noexcept{return enabled_&&supply_.Held()&&(insertion_.Settling()||insertion_.FinishesOnRelease());}
    bool BlocksEquipment()const noexcept{return supply_.Held().has_value();}
    bool SetShellFeel(SpasShellFeel feel)noexcept {
        if(supply_.Held()||pending_||!insertion_.Reconfigure(SpasReloadInsertionProfile(feel)))return false;
        feel_=feel;return true;
    }
    SpasShellFeel ShellFeel()const noexcept{return feel_;}
    void PumpRearFeedback(PhysicalReloadResult&,const interaction::HandInteractionSample&)noexcept;
    void PumpRackFeedback(PhysicalReloadResult&,const interaction::HandInteractionSample&)noexcept;
    void Report(std::ostream&)const;
private:
    struct Evidence {interaction::HandInteractionSample input{};interaction::HandClaim shell{},gun{};};
    const Evidence* Original(const PhysicalReloadSample&)const noexcept;
    void Feedback(PhysicalReloadResult&,interaction::FeedbackKind,const interaction::HandInteractionSample&,unsigned hand=0)noexcept;
    void FinishPending(PhysicalReloadResult&,const interaction::HandInteractionSample&)noexcept;
    void PublishSettling(const PhysicalReloadSample&,const physicalWeaponRuntime::View&,
        const interaction::HandClaim&,PhysicalReloadResult&)noexcept;
    bool AcceptInsertion(const interaction::ReloadInsertionResult&,const interaction::ReloadInsertionSample&,
        const PhysicalReloadSample&,const physicalWeaponRuntime::View&,const interaction::HandClaim&,
        interaction::HandInteraction&,PhysicalReloadResult&)noexcept;
    void PublishReleased(const PhysicalReloadSample&,const physicalWeaponRuntime::View&,
        const interaction::HandClaim&,PhysicalReloadResult&)noexcept;
    bool enabled_=true;
    interaction::PhysicalWeaponFamily family_=interaction::PhysicalWeaponFamily::TubePump;
    SpasShellFeel feel_=SpasShellFeel::As2;
    interaction::AmmoSupply supply_;
    interaction::ReloadInsertion insertion_{SpasReloadInsertionProfile(SpasShellFeel::As2)};
    interaction::PhysicalWeaponIdentity weapon_{},pendingWeapon_{};
    ReloadStateOwner nativeOwner_{};
    interaction::HandInteractionOwner handOwner_{};
    std::array<std::optional<Evidence>,32> history_{};
    std::size_t historyNext_=0;
    std::optional<Bc2ReloadTargets> targets_;
    float insertionProgress_=0;
    float insertionAlignment_=0;
    std::optional<math::Matrix4> insertionRawWrist_;
    struct SettlingShell {
        interaction::PhysicalWeaponIdentity weapon{};
        ReloadStateOwner nativeOwner{};
        interaction::HandInteractionOwner handOwner{};
        std::uint64_t operation=0,revision=0;
        std::int64_t startedNs=0,deadlineNs=0;
    };
    std::optional<SettlingShell> settling_;
    struct ReleasedShell {
        interaction::ReloadInsertionSample contact{};
        std::uint64_t revision=0;
        std::int64_t startedNs=0,deadlineNs=0;
    };
    std::optional<ReleasedShell> released_;
    std::optional<Bc2AmmoReserveLease> diagnosticReserve_;
    std::uint64_t pending_=0,lastRaw_=0,nextFeedback_=0;
    std::int64_t lastNow_=0;
    unsigned acquired_=0,submitted_=0,completed_=0,rejected_=0,cancelled_=0;
};
}
