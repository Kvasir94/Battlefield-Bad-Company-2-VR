#pragma once
#include "fvr/interaction/MagazineFeedState.h"
#include <array>
#include <span>
#include <string_view>

// Per-weapon action kind and last-round hold-open for BC2 magazine guns, from
// the table in docs/BC2_PHYSICAL_RELOAD.md ("Per-weapon action defaults").
// Rows are keyed by the exact mesh resource basename in
// profiles/weapon-family-work-plan.json (".res" dropped, case-insensitive).
//
// These are REAL-FIREARM defaults. BC2's native has no lock state, and no row
// has been checked against the gun's decoded Granny empty-reload clip, so every
// row is unverified: it may drive VR-side chamber metadata (rack requirement,
// ammo display) but never native timing, ammunition or reload admission.
// G3, Thompson and M9-3 have no exact mesh in the work plan and are not listed.
namespace fvr::bc2 {
struct MagazineActionRow {
 std::string_view label,mesh;
 interaction::MagazineActionKind kind=interaction::MagazineActionKind::ClosedBolt;
 bool locksOpenOnEmpty=false;
 bool verified=false; // Checked against the BC2 empty-reload clip. None yet.
};
namespace magazine_action_detail {
using K=interaction::MagazineActionKind;
inline constexpr std::array<MagazineActionRow,25> Rows{{
 {"AEK-971","RU_rgl_AEK971_Mesh",K::ClosedBolt,false},
 {"XM8","US_rgl_XM8_Mesh",K::ClosedBolt,true},
 {"F2000","BU_rif_F2000_Mesh",K::ClosedBolt,true},
 {"AUG","MEC_rgl_SteyrAUG_Mesh",K::ClosedBolt,true},
 {"AN-94","BU_rif_AN94Abakan_Mesh",K::ClosedBolt,false},
 {"M416","UL_rif_HK416_Mesh",K::ClosedBolt,true},
 {"M16A2","BU_rif_M16A2_Mesh",K::ClosedBolt,true},
 {"9A-91","MEC_rif_9A91_Mesh",K::ClosedBolt,false},
 {"SCAR-L","UL_rif_FNSCARL_Mesh",K::ClosedBolt,true},
 {"XM8 Compact","US_rif_XM8c_Mesh",K::ClosedBolt,true},
 {"AKS-74U","RU_rif_AKS74u_Mesh",K::ClosedBolt,false},
 {"UZI","BU_smg_IMIUZI_Mesh",K::OpenBolt,false},
 {"PP-2000","BU_smg_PP2000_Mesh",K::ClosedBolt,false},
 {"UMP","BU_smg_UMP_Mesh",K::NotchLock,false},
 {"XM8 LMG","US_rif_XM8lmg_Mesh",K::ClosedBolt,true},
 {"MG36","BU_lmg_MG36_Mesh",K::ClosedBolt,true},
 {"Type 88 sniper","BU_sni_QBY88_Mesh",K::ClosedBolt,false},
 {"SVU","UL_sni_SVU_Mesh",K::ClosedBolt,false},
 {"VSS","BU_sni_VSSVintores_Mesh",K::ClosedBolt,false},
 {"M9","US_hg_M9Beretta_Mesh",K::ClosedBolt,true},
 {"M1911","US_M1911_Mesh",K::ClosedBolt,true},
 {"MP443","RU_hg_MP443Grach_Mesh",K::ClosedBolt,true},
 {"Saiga","RU_shg_Saiga12_Mesh",K::ClosedBolt,false},
 {"USAS","BU_shg_USAS-12_Mesh",K::ClosedBolt,true},
 {"M14 / Mk14 EBR","bu_rif_mk14ebr_mesh",K::ClosedBolt,true},
}};
constexpr bool SameName(std::string_view a,std::string_view b)noexcept {
 if(a.size()!=b.size())return false;
 for(std::size_t n=0;n<a.size();++n){
  const auto l=[](char c){return c>='A'&&c<='Z'?char(c-'A'+'a'):c;};
  if(l(a[n])!=l(b[n]))return false;
 }
 return true;
}
}
inline constexpr std::span<const MagazineActionRow> MagazineActionRows()noexcept {return magazine_action_detail::Rows;}
// Accepts a configured mesh path ("Objects/.../US_rgl_XM8_Mesh"), a basename,
// or a work-plan resource name ("US_rgl_XM8_Mesh.res").
constexpr const MagazineActionRow* FindMagazineAction(std::string_view mesh)noexcept {
 if(const auto slash=mesh.find_last_of("/\\");slash!=std::string_view::npos)mesh.remove_prefix(slash+1);
 if(mesh.size()>4&&magazine_action_detail::SameName(mesh.substr(mesh.size()-4),".res"))mesh.remove_suffix(4);
 if(mesh.empty())return nullptr;
 for(const auto& row:magazine_action_detail::Rows)if(magazine_action_detail::SameName(row.mesh,mesh))return &row;
 return nullptr;
}
} // namespace fvr::bc2
