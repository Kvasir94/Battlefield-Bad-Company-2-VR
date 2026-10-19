#pragma once
#include "Bc2ReloadConfigDescriptor.h"
#include <cmath>
#include <optional>

namespace fvr::bc2 {
// Compatibility constants retain their values. New immutable registrations use
// digest-derived keys; adding a descriptor never adds another named enum case.
enum class NativeMagazineProfileId : std::uint64_t {ScopedXm8=0,AuthoredAek=1};
enum class MagazineIdentityRoute : std::uint8_t { LinkedLauncherAlias, SelectedCarriedItem };
enum class MagazineCycleAdmission : std::uint8_t { Candidate, ReviewedReload11Transfer12, ReviewedPhysicalAuthority };
// Native reload policy data only. Geometry, hand contacts and render receipts
// are independent capabilities. Authored timing does not prove this cycle.
struct MagazineNativeProfile {
 ReloadConfigDescriptor configuration{};
 MagazineIdentityRoute identityRoute=MagazineIdentityRoute::SelectedCarriedItem;
 MagazineCycleAdmission cycleAdmission=MagazineCycleAdmission::Candidate;
 std::int64_t completionDeadlineNs=0;
 constexpr bool Reviewed()const noexcept {
  return configuration.admission==ReloadDescriptorAdmission::ReviewedNative&&
   !configuration.assetName.empty()&&!configuration.assetPath.empty()&&
   !configuration.timing.empty()&&configuration.timing.size()<=6&&
   (cycleAdmission==MagazineCycleAdmission::ReviewedReload11Transfer12||
    (cycleAdmission==MagazineCycleAdmission::ReviewedPhysicalAuthority&&configuration.values.fireLogicType==0))&&
   configuration.values.reloadType==1&&
   completionDeadlineNs>0&&completionDeadlineNs<=10000000000ll&&
   configuration.values.reloadTime>0&&configuration.values.reloadTime<=10&&
   configuration.values.reloadThreshold>0&&configuration.values.reloadThreshold<=1;
 }
 bool Matches(const ReloadObservedConfig& c)const noexcept {
  return Reviewed()&&MatchesReloadDescriptor(c,configuration);
 }
 bool ReadTiming(const ReloadStateMemory& memory,const ReloadObservedConfig& c)const noexcept {
  return Reviewed()&&ReadReloadDescriptorTiming(memory,c,configuration);
 }
 float HoldCeiling()const noexcept {return configuration.values.reloadTime*configuration.values.reloadThreshold;}
 float TailCeiling()const noexcept {return configuration.values.reloadTime*(1.f-configuration.values.reloadThreshold);}
 bool Wait(const ReloadFiringObservation& b)const noexcept {
  return Reviewed()&&cycleAdmission==MagazineCycleAdmission::ReviewedReload11Transfer12&&b.currentState==11&&b.nextState==12&&std::isfinite(b.phaseTimer)&&
   b.phaseTimer>0&&b.phaseTimer<=HoldCeiling();
 }
 bool Tail(const ReloadFiringObservation& b)const noexcept {
  return Reviewed()&&cycleAdmission==MagazineCycleAdmission::ReviewedReload11Transfer12&&b.currentState==12&&b.nextState==1&&b.phaseTimer>=0&&b.phaseTimer<=TailCeiling();
 }
};
inline constexpr MagazineNativeProfile Xm8MagazineNativeProfile{
 Xm8ReloadDescriptor,MagazineIdentityRoute::LinkedLauncherAlias,
 MagazineCycleAdmission::ReviewedReload11Transfer12,3500000000ll};
// Exact authored AEK chain supplies all values. The executable-wide symbol
// translation is shared with XM8 (automatic=2, magazine=1, Fire=8, Reload=29).
// Reviewed against the same executable-wide AutomaticFire/rtMagazine code,
// exact firing vtable, transfer and abort implementation as scoped XM8. This
// does not register geometry or assert a headset/native AEK test took place.
// All current configuration, context, owner and three-branch receipts still gate
// each operation; a name alone grants no native authority.
inline constexpr std::array<ReloadTimingWord,6> AekReloadTiming{{
 {0x10,std::bit_cast<std::uint32_t>(.75f)},{0x14,0},
 {0x18,std::bit_cast<std::uint32_t>(3.2f)},{0x20,1},{0x24,2},{0x2c,0}}};
inline constexpr MagazineNativeProfile AekMagazineNativeProfile{
 {"AEK971_sp","Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971",
  {2,1,8,29,30,4,0,3.2f,.75f,0,0,0,false,false},AekReloadTiming,ReloadDescriptorAdmission::ReviewedNative},
 // LinkedLauncherAlias admits both hand identities: the AEK carried alone
 // (native item, no route check) and the SP_02 AEK carried with its GP30
 // underslung launcher (SP_rgl_GP30, native 40mmgl), where the hand holds the
 // launcher pair's alias like the scoped XM8 with its GL.
 MagazineIdentityRoute::LinkedLauncherAlias,MagazineCycleAdmission::ReviewedReload11Transfer12,3900000000ll};
// F2000_sp: every value from the shipped authored chain SP_rif_F2000.dbx
// (sha256 62d1514a...) -> BU_rif_F2000_Firing.dbx (sha256 84329145...,
// FiringFunction 82546e7b-...) in sp_common/level-00.fbrb, via
// bc2_weapon_config_pipeline -> build_manual_reload_catalog ->
// bc2_magazine_descriptor_manifest (no deferred reasons,
// same_reviewed_dispatch_shape true; descriptor digest a83a3c5288dee2d4...).
// Same reviewed dispatch shape as XM8/AEK: fltAutomaticFire, rtMagazine,
// EiaFire/EiaReload, zero bolt, rlWeaponSwitchCancelsUnfinishedReload. Only
// ReloadTime differs (3.1 s). Admitted on that shared review like AEK; no
// live F2000 reload has run yet (docs/BC2_RELOAD_LEDGER-20261008.md).
inline constexpr std::array<ReloadTimingWord,6> F2000ReloadTiming{{
 {0x10,std::bit_cast<std::uint32_t>(.75f)},{0x14,0},
 {0x18,std::bit_cast<std::uint32_t>(3.1f)},{0x20,1},{0x24,2},{0x2c,0}}};
inline constexpr MagazineNativeProfile F2000MagazineNativeProfile{
 {"F2000_sp","Objects/Weapons/Handheld/BU_rif_F2000/SP_rif_F2000",
  {2,1,8,29,30,4,0,3.1f,.75f,0,0,0,false,false},F2000ReloadTiming,ReloadDescriptorAdmission::ReviewedNative},
 MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,3800000000ll};
// Digest-derived key (registry convention) for the F2000 descriptor.
inline constexpr NativeMagazineProfileId AuthoredF2000MagazineId=static_cast<NativeMagazineProfileId>(0xa83a3c5288dee2d4ull);

// Immutable compiled lookup. Optional rows remain disabled unless the exact
// generated record explicitly carries reviewed family admission and enablement.
struct MagazineNativeRegistration {
 NativeMagazineProfileId id{};
 const MagazineNativeProfile* profile=nullptr;
 std::string_view descriptorDigest{};
 bool enabled=false;
};
inline constexpr std::size_t MaxGeneratedMagazineProfiles=256;
std::optional<NativeMagazineProfileId> MagazineProfileKey(std::string_view digest)noexcept;
// Pure generated-row validation. Reject all colliding IDs/digests or ambiguous
// exact name/path rows, including disabled conflicts. No first-match fallback.
const MagazineNativeRegistration* SelectGeneratedMagazineRegistration(
 std::span<const MagazineNativeRegistration>,NativeMagazineProfileId)noexcept;
std::span<const MagazineNativeRegistration> RegisteredMagazineNativeProfiles()noexcept;
// Lookup is explicit and lazy; including native profile types must not create
// a runtime registry dependency in IPC-only consumers.
const MagazineNativeProfile* ResolveMagazineNativeProfile(NativeMagazineProfileId)noexcept;
const MagazineNativeRegistration* FindMagazineNativeProfile(std::string_view asset)noexcept;
const MagazineNativeRegistration* FindMagazineNativeProfile(const ReloadObservedConfig&)noexcept;
} // namespace fvr::bc2
