#pragma once
#include "Bc2MagazineGeometryProfile.h"
#include "Bc2MagazineNativeProfile.h"
#include "Bc2MagazineActionCatalog.h"
#include "fvr/interaction/ChargingHandleRack.h"
#include "Bc2PistolMechanismProfile.h"
#include "Bc2MagazineMechanismProfile.h"

namespace fvr::bc2 {
// Immutable compiled registration joins independent native and geometry data.
// Experimental authored/design estimates stay distinct from measured calibration.
// Neither kind infers native ammunition or ownership evidence from a gun class.
struct MagazineEquipmentProfile {
 NativeMagazineProfileId nativeId=NativeMagazineProfileId::ScopedXm8;
 const MagazineNativeProfile* native=nullptr;
 const MagazineGeometryProfile* geometry=nullptr;
 bool geometryVerified=false;
 bool experimentalGeometry=false; // Explicit generated build; never measured/headset verified.
 // VR-side chamber metadata only (MagazineFeedState). Real-firearm defaults
 // from Bc2MagazineActionCatalog.h; actionVerified stays false until the row
 // is checked against BC2's Granny empty-reload clip. Never gates Ready().
 interaction::MagazineActionKind actionKind=interaction::MagazineActionKind::ClosedBolt;
 bool locksOpenOnEmpty=false,actionVerified=false;
 // Charging-handle grab point and stroke. Needs live mapping against the
 // native rig per weapon; none is admitted yet, so the rack stays disabled.
 const interaction::ChargingHandleProfile* chargingHandle=nullptr;
 const MagazineMechanismProfile* mechanism=nullptr;
 bool Ready()const noexcept {
  return native&&native->Reviewed()&&(geometryVerified||experimentalGeometry)&&geometry&&geometry->rigFingerprint&&
   MagazineGeometryMatchesConfiguration(*geometry,*native)&&!geometry->mesh.empty()&&
   geometry->interaction.insertion.id&&geometry->interaction.insertion.revision;
 }
};
// C2 charging handles, weapon-local metres in the magazine geometry frame
// (canonical, relative to jntWpn_1, +Z toward the butt). Closed grab point is
// the handle part's centroid at the static HandsIkPose; the stroke is the
// authored fire-cycle travel of that part along +Z (no shipped clip racks a
// handle by hand). Grab/tolerance/release are design values for the grasp
// point (wrist through the magazine grasp offset). Evidence:
// docs/2142-TRANSFER.md, "Charging-handle rack".
// XM8: jntWpn_17 (child of the jntWpnwpnJnt_16 carrier), 1P_FireStart.res
// sha 8271c742..., peak 0.1208 m, axis residual 6e-7 m.
inline constexpr interaction::ChargingHandleProfile Xm8ChargingHandle{0x584d384348ull,1,{-.00088f,.0697f,-.5115f},{0,0,1},
 .12f,.06f,.015f,.08f,interaction::MotionGuardConfig::Rack()};
// F2000: jntWpn_6 knob (left side), 1P_FireStart.res sha 2bad662c... 0.1008 m,
// returned by 1P_FireRelease.res sha 3bae931c..., residual 1e-8 m.
inline constexpr interaction::ChargingHandleProfile F2000ChargingHandle{0x4632304b48ull,1,{.0331f,.0669f,-.5095f},{0,0,1},
 .10f,.06f,.012f,.08f,interaction::MotionGuardConfig::Rack()};
// The rig parts that move with the handle (drawn at closed + travel along
// +Z). XM8: the jntWpnwpnJnt_16 carrier and its jntWpn_17 handle; F2000: the
// jntWpn_6 handle.
struct ChargingHandleParts {std::array<std::string_view,2> bones{};unsigned count=0;float strokeMeters=0;};
inline const ChargingHandleParts* FindChargingHandleParts(std::string_view asset)noexcept {
 static constexpr ChargingHandleParts xm8{{"jntWpnwpnJnt_16","jntWpn_17"},2,.12f},f2000{{"jntWpn_6",""},1,.10f};
 if(asset=="XM8_sp_s")return &xm8;
 if(asset=="F2000_sp")return &f2000;
 return nullptr;
}
inline const interaction::ChargingHandleProfile* FindChargingHandle(std::string_view asset)noexcept {
 if(const auto* pistol=FindPistolMechanism(asset))return &pistol->handle;
 if(asset=="XM8_sp_s")return &Xm8ChargingHandle;
 if(asset=="F2000_sp")return &F2000ChargingHandle;
 return nullptr; // AEK971_sp: no handle motion in any shipped clip.
}
// Fills the per-weapon action columns from the catalog row for the geometry's
// configured mesh. Unknown meshes keep the ClosedBolt/no-lock defaults.
inline MagazineEquipmentProfile WithMagazineAction(MagazineEquipmentProfile p)noexcept {
 if(const auto* row=p.geometry?FindMagazineAction(p.geometry->mesh):nullptr){
  p.actionKind=row->kind;p.locksOpenOnEmpty=row->locksOpenOnEmpty;p.actionVerified=row->verified;}
 if(p.geometry)p.chargingHandle=FindChargingHandle(p.geometry->asset);
 if(p.geometry)if(const auto* mechanism=FindMagazineMechanism(p.geometry->asset,p.geometry->mesh,p.geometry->rigFingerprint)){
  if(!p.chargingHandle){p.mechanism=mechanism;p.chargingHandle=&mechanism->handle;}
 }
 if(p.geometry)if(const auto* pistol=FindPistolMechanism(p.geometry->asset)){
  if(pistol->rigFingerprint==p.geometry->rigFingerprint){p.actionKind=interaction::MagazineActionKind::ClosedBolt;p.locksOpenOnEmpty=true;}
  else p.chargingHandle=nullptr;
 }
 return p;
}
inline const MagazineEquipmentProfile& Xm8MagazineEquipment()noexcept {
 static const MagazineEquipmentProfile profile=WithMagazineAction({NativeMagazineProfileId::ScopedXm8,
  ResolveMagazineNativeProfile(NativeMagazineProfileId::ScopedXm8),&Xm8MagazineGeometry(),true});
 return profile;
}
inline const MagazineEquipmentProfile* FindMagazineEquipment(NativeMagazineProfileId id)noexcept {
 if(id==NativeMagazineProfileId::ScopedXm8)return &Xm8MagazineEquipment();
 // One shared immutable join for every registered native family. Without the
 // optional generated header, missing geometry remains unavailable as before.
 static const auto profiles=[] {
  std::array<MagazineEquipmentProfile,MaxGeneratedMagazineProfiles+3> result{};
  const auto registeredProfiles=RegisteredMagazineNativeProfiles();
  for(std::size_t n=0;n<registeredProfiles.size();++n){const auto& registered=registeredProfiles[n];
   const auto geometry=ResolveMagazineNativeProfile(registered.id)?
    FindMagazineGeometry(*registered.profile):nullptr;
   result[n]=WithMagazineAction({registered.id,registered.profile,geometry,false,geometry!=nullptr});
  }
  return result;
 }();
 const auto registered=ResolveMagazineNativeProfile(id);
 if(!registered)return nullptr;
 for(const auto& profile:profiles)if(profile.nativeId==id&&profile.native==registered)return &profile;
 return nullptr;
}
inline const MagazineEquipmentProfile* FindMagazineEquipment(std::string_view asset)noexcept {
 const auto registered=FindMagazineNativeProfile(asset);
 return registered?FindMagazineEquipment(registered->id):nullptr;
}
inline const MagazineEquipmentProfile* FindMagazineEquipment(const ReloadObservedConfig& config)noexcept {
 const auto registered=FindMagazineNativeProfile(config);
 return registered?FindMagazineEquipment(registered->id):nullptr;
}
// Per-weapon support by native asset name, with the reason a weapon is not
// supported. An unsupported weapon keeps the stock native reload; it never
// fails a session. Adding a weapon is data: a reviewed native descriptor
// (Bc2MagazineNativeProfile / generated registry) plus a geometry row
// (Bc2MagazineGeometryProfile / generated experimental header).
enum class MagazineSupportStatus:std::uint8_t {Supported,NoNativeProfile,NativeNotReviewed,NoGeometry,NotReady};
struct MagazineSupport {MagazineSupportStatus status=MagazineSupportStatus::NoNativeProfile;const MagazineEquipmentProfile* profile=nullptr;};
inline MagazineSupport MagazineSupportFor(std::string_view asset)noexcept {
 const auto registered=FindMagazineNativeProfile(asset);
 if(!registered||!registered->profile)return {MagazineSupportStatus::NoNativeProfile,nullptr};
 if(!registered->profile->Reviewed())return {MagazineSupportStatus::NativeNotReviewed,nullptr};
 const auto* profile=FindMagazineEquipment(registered->id);
 if(!profile||!profile->geometry)return {MagazineSupportStatus::NoGeometry,profile};
 if(!profile->Ready())return {MagazineSupportStatus::NotReady,profile};
 return {MagazineSupportStatus::Supported,profile};
}
inline const char* MagazineSupportName(MagazineSupportStatus s)noexcept {
 switch(s){case MagazineSupportStatus::Supported:return "supported";case MagazineSupportStatus::NoNativeProfile:return "no_native_profile";
 case MagazineSupportStatus::NativeNotReviewed:return "native_not_reviewed";case MagazineSupportStatus::NoGeometry:return "no_geometry";
 case MagazineSupportStatus::NotReady:return "not_ready";}return "unknown";
}
} // namespace fvr::bc2
