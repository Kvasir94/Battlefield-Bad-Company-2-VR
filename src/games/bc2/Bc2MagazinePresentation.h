#pragma once
#include "Bc2MagazineInteraction.h"
#include "Bc2ReloadPreview.h"
#include "Bc2MagazineDetachEvidence.h"
#include "Bc2NativeActionPose.h"
#include "Bc2BodyAmmoMaterial.h"
#include "fvr/interaction/PhysicalWeaponState.h"
namespace fvr::bc2 {
// Free replacement carry was measured relative to this exact sampled weapon.
// Reusing its local pose with a newer weapon would move the loading hand/prop.
// This is immutable presentation evidence, not a new native/input lease.
struct MagazineCarryFrame {
 interaction::HandInteractionOwner owner{};interaction::HandInteractionKey item{};
 interaction::HandClaimToken handClaim{},gunClaim{};
 std::uint64_t inputSequence=0,cycle=0;
 std::int64_t observedNs=0,deadlineNs=0;
 math::Matrix4 weaponWorldMeters{};
};
using MagazineReplacementFrame=MagazineCarryFrame;
struct MagazineTracking {
 MagazineFamilyEvidence family{};
 bool enabled=false;ReloadStateOwner owner{};interaction::HandInteractionSample inputEvidence{};
 std::shared_ptr<const SelectedMeshesSnapshot> selected;
 Bc2AmmoReserveLease reserve{};std::uint64_t cycle=0;
 std::optional<interaction::MagazinePropTarget> target;
 std::shared_ptr<const MagazineDetachAuthorization> detach;
 std::optional<MagazineReplacementFrame> replacementFrame;
 std::optional<MagazineCarryFrame> removalFrame;
 // Visual-only reuse of a previously committed Removed pose. Original
 // native/input/target leases stay unchanged; this current suppression may
 // neither generate contact nor acknowledge a return.
 std::optional<HolsterSuppressionReceipt> retainedVisualSuppression;
 // M6, presentation only: while the well is empty (role Hidden) the old
 // magazine that left the hand is drawn falling/resting at this raw world
 // pose (metres) instead of hidden. No claim, lease or native authority.
 std::optional<math::Matrix4> droppedWorldMeters;
 // Persistent physical authority is separate from the legacy native reload
 // lease. Recheck incarnation/revision at each render/Pack; keep pose deadlines.
 bool physicalAuthority=false;
 interaction::PhysicalWeaponIdentity authorityWeapon{};
 std::uint64_t authorityRevision=0;
 std::uint32_t authorityClientWeapon=0;
 bool (*authorityCurrent)(const interaction::PhysicalWeaponIdentity&,std::uint64_t,std::uint32_t)noexcept=nullptr;
 // Exact finite supply from this authority revision (loose reserve plus
 // retained magazines). Physical presentation has no legacy reserve lease.
 std::optional<interaction::AmmoSupplySource> bodySupply;
 std::optional<BodyAmmoMaterial> bodyMaterial;
 // Physical mechanism progress, measured before cosmetic hand placement.
 // false means free carry; true selects the native removal/insertion path.
 bool actionConstrained=false;
 float actionProgress=0;
 float actionAlignment=1;
 std::optional<math::Matrix4> actionRawWristInWeaponMeters;
};
bool MagazineTrackingFresh(const MagazineTracking&,std::int64_t now)noexcept;
bool MagazineTargetFresh(const MagazineTracking&,std::int64_t now)noexcept;
bool MagazineTargetRetained(const MagazineTracking& original,const MagazineTracking& current,std::int64_t now)noexcept;
struct MagazineRawContact:ReloadRawContact {bool nativeMagazineAttached=false;};
// Free carry only: retain the basis of the exact raw packet that produced the target.
std::optional<MagazineCarryFrame> MagazineRemovalFrame(const MagazineTracking&,const MagazineRawContact&)noexcept;
MagazineRawContact BuildMagazineRawContact(const MagazineTracking&,const RigSnapshot&,std::string_view asset,
 const math::Matrix4& rawLeftWrist,const math::Matrix4& placedWeapon,float unitsPerMetre,std::int64_t now,
 const math::Matrix4* trackingBody=nullptr);
struct MagazinePresentationBinding {
 std::uint64_t fingerprint=0;std::uint32_t weapon=0,magazine=0,wrist=0;
 std::array<std::uint32_t,15> fingers{};
 // Bones carried inside the magazine (F2000: the top-round chain jntWpn_17-19
 // under jntWpnwpnJnt_16), in parent-first order. They keep their native
 // relation to the magazine and hide with it.
 std::array<std::uint32_t,8> carried{};std::uint32_t carriedCount=0;
 const MagazineGeometryProfile* geometry=nullptr; // Immutable profile used to derive these exact roles.
 bool operator==(const MagazinePresentationBinding&)const=default;
};
namespace magazine_presentation_detail {
 // Structural fixture helper; production additionally requires captured hash.
 std::optional<MagazinePresentationBinding> Derive(const RigSnapshot&);
 // Explicit geometry variant for structural/offline calibration. It grants no native authority.
 std::optional<MagazinePresentationBinding> Derive(const RigSnapshot&,const MagazineGeometryProfile&);
}
std::optional<MagazinePresentationBinding> BindMagazinePresentation(const RigSnapshot&,std::string_view asset);
std::optional<MagazinePresentationBinding> BindMagazinePresentation(const RigSnapshot&,const MagazineEquipmentProfile&);
struct MagazinePresentationPlan {
 std::optional<MagazinePresentationBinding> binding;
 std::vector<interaction::BoneWrite> writes;
 std::optional<math::Matrix4> wristTarget;
 bool hideMagazine=false;
 bool sampledNativeHand=false;
 std::size_t partWriteCount=0;
};
// Root composes these writes with torso/weapon/right-hand, solving the left arm
// to EXACT wristTarget before the final immutable BuildRigPosePlan. No native
// animation write, hidden-bone exception, or ammunition authority is granted.
MagazinePresentationPlan BuildMagazinePresentation(const RigSnapshot&,const MagazinePresentationBinding&,
 const MagazineTracking&,const math::Matrix4& placedWeapon,float unitsPerMetre,std::int64_t now,
 std::span<const NativeActionPoseProfile> nativeActions={});
// A support claim owns the wrist/fingers, not the magazine's location. Keep
// magazine/carried geometry for every role, including a dropped Hidden target
// whose empty well is represented by moving the magazine rather than hiding it.
std::span<const interaction::BoneWrite> MagazinePresentationWrites(
 const MagazinePresentationPlan&,bool supportOwnsLeftHand)noexcept;
std::optional<std::vector<std::array<std::byte,64>>> HideMagazinePackedPalette(
 const RigSnapshot&,const MagazinePresentationBinding&,std::span<const std::array<std::byte,64>> ordinary);
// BEFORE EACH native Pack: original target/mesh/owner/cycle/claim deadlines must
// remain valid. coherentShotGuard is the adapter's existing exact-source proof.
ReloadPackedPaletteChoice SelectMagazinePackedPalette(const MagazineTracking& original,const MagazineTracking* current,
 std::int64_t now,std::span<const std::array<std::byte,64>> posed,
 std::span<const std::array<std::byte,64>> ordinary,bool coherentShotGuard)noexcept;
// Diagnostic only; never participates in palette selection or native authority.
enum class MagazineFallbackReason:unsigned {
 SourceReadFailed,SourceChanged,CurrentMissing,ShotIncoherent,OldExpired,
 CurrentExpired,RetentionMismatch,InvalidPalette,ShotMissing,Count
};
inline constexpr std::uint64_t MagazineFallbackBit(MagazineFallbackReason r)noexcept{return 1ull<<unsigned(r);}
inline constexpr std::array<const char*,unsigned(MagazineFallbackReason::Count)> MagazineFallbackNames{
 "source_read_failed","source_changed","current_missing","shot_incoherent","old_expired",
 "current_expired","retention_mismatch","invalid_palette","shot_missing"};
std::uint64_t ClassifyMagazinePaletteFallback(const MagazineTracking&,const MagazineTracking*,
 std::int64_t,std::span<const std::array<std::byte,64>>,bool coherentShotGuard)noexcept;
} // namespace fvr::bc2
