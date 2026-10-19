#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace fvr::bc2 {
// Reflection layout facts used by future test-range operations, transcribed from
// the static TypeInfo dump of BFBC2Game.exe build 795745 (exe sha256 c603f686...,
// layout-identical to the repo-pinned 3911fcc8...; bc2/tools/typeinfo-dump,
// profiles/typeinfo/795745/bc2_types.csv, method in docs/BC2-TYPEINFO-DUMP.md).
//
// UNVERIFIED for runtime use. An offset here is a reflected layout fact; what a
// write does to the engine (and when it is read) is inference until a live
// read-back receipt exists. Nothing in this file is read or written by the probe:
// a RangeHooks binding may use an entry only through an admitted
// RangeOperationCapability, and must re-check `typeInfo` on the live object
// (ReloadStateMemory::type) before touching it. Strings are Frostbite `String`
// (0xc bytes, engine-allocated): writing one needs the engine's own assignment.
enum class RangeFieldType:std::uint8_t {Boolean,Uint32,Int32,Float32,String,Ref,Array};
struct RangeNativeField {
    std::string_view owner,field; // reflected class and field names
    std::uint32_t typeInfo,offset,bytes;
    RangeFieldType type;
    std::string_view setting;     // `Class.Field` key produced by runtime::RangeSettings, or empty
};
// Settings singletons (coordinator PC notes 2026-10-08, same build). Registration
// stubs and the dotted registry are static facts; the live instance addresses
// are not recorded and must be resolved at run time.
inline constexpr std::uint32_t RangeServerSettingsRegister=0x534600,RangeClientSettingsRegister=0x534680,
    RangeGameSettingsRegister=0x534700,RangeNetworkSettingsRegister=0x534780;
inline constexpr std::uint32_t RangeServerSettingsType=0x01bf3b3c,RangeClientSettingsType=0x01bf3b74,
    RangeGameSettingsType=0x01bf3bc0,RangeNetworkSettingsType=0x01bf3bf8;
inline constexpr std::uint32_t RangeSettingsLookup=0x4f4600,RangeSettingsRegistryPointer=0x155cfdc;
inline constexpr std::uint32_t RangeServerSettingsBytes=0xcc,RangeClientSettingsBytes=0xfc,RangeGameSettingsBytes=0xa4;
// MenuIdEnum values for ready/cinematic detection.
inline constexpr std::int32_t RangeMenuPlaying=4,RangeMenuLetterBox=13,RangeMenuMovie=14,RangeMenuPlayingNoMinimap=19;
inline constexpr std::array<RangeNativeField,49> RangeNativeFields{{
    // (a) level selection and start-up
    {"ServerSettings","Level",0x01bf3b3c,0x7c,0xc,RangeFieldType::String,""},
    {"ClientSettings","Level",0x01bf3b74,0x5c,0xc,RangeFieldType::String,"Client.Level"},
    {"ClientSettings","SpawnPoint",0x01bf3b74,0x50,0xc,RangeFieldType::String,"Client.SpawnPoint"},
    {"ServerSettings","SavePoint",0x01bf3b3c,0x70,0xc,RangeFieldType::String,"Server.SavePoint"},
    {"ServerSettings","LoadSavePoint",0x01bf3b3c,0xb8,1,RangeFieldType::Boolean,"Server.LoadSavePoint"},
    {"ServerSettings","IsAiEnabled",0x01bf3b3c,0xc5,1,RangeFieldType::Boolean,"Server.IsAiEnabled"},
    {"ServerSettings","IsFastStartupEnabled",0x01bf3b3c,0x9f,1,RangeFieldType::Boolean,""},
    {"ClientSettings","IsFastStartupEnabled",0x01bf3b74,0xf0,1,RangeFieldType::Boolean,"Client.IsFastStartupEnabled"},
    {"GameSettings","FastStartupEnable",0x01bf3bc0,0x98,1,RangeFieldType::Boolean,""},
    {"ClientSettings","IsMultiplayer",0x01bf3b74,0xd5,1,RangeFieldType::Boolean,""},
    {"MissionEntityData","NextLevelToRun",0x01bfe69c,0x8c,0xc,RangeFieldType::String,""},
    // (b) front end, movies, cutscenes, level start gates
    {"ClientSettings","DisableMovies",0x01bf3b74,0xf1,1,RangeFieldType::Boolean,"Client.DisableMovies"},
    {"ClientSettings","DisableHudMovies",0x01bf3b74,0xf2,1,RangeFieldType::Boolean,""},
    {"ClientSettings","DisableMenuFlow",0x01bf3b74,0xf9,1,RangeFieldType::Boolean,""},
    {"ClientSettings","LoadMenu",0x01bf3b74,0xfa,1,RangeFieldType::Boolean,"Client.LoadMenu"},
    {"ClientSettings","LimitedMainMenu",0x01bf3b74,0xba,1,RangeFieldType::Boolean,""},
    {"ClientSettings","BypassMatchmaking",0x01bf3b74,0xea,1,RangeFieldType::Boolean,""},
    {"ClientSettings","PauseGameOnStartUp",0x01bf3b74,0xd2,1,RangeFieldType::Boolean,""},
    {"ServerSettings","DisableCutscenes",0x01bf3b3c,0xb5,1,RangeFieldType::Boolean,"Server.DisableCutscenes"},
    {"ServerSettings","DisableCheckpoints",0x01bf3b3c,0x8e,1,RangeFieldType::Boolean,"Server.DisableCheckpoints"},
    {"LevelDescription","StartDelayTime",0x01bfd688,0x24,4,RangeFieldType::Uint32,""},           // LevelData+0x84+0x24 = 0xa8
    {"LevelDescription","WaitForButtonBeforeStarting",0x01bfd688,0x168,1,RangeFieldType::Boolean,""}, // LevelData+0x1ec
    {"LevelData","StartLevelFadedToBlack",0x01bfd738,0x3b0,1,RangeFieldType::Boolean,""},
    {"CutsceneEntityData","IsSkippable",0x01c011a4,0xcd,1,RangeFieldType::Boolean,""},
    // (c) loadout, unlocks, test aids
    {"ServerSettings","UnrestrictedUnlocks",0x01bf3b3c,0xbe,1,RangeFieldType::Boolean,"Server.UnrestrictedUnlocks"},
    {"ClientSettings","UnrestrictedUnlocks",0x01bf3b74,0xc8,1,RangeFieldType::Boolean,"Client.UnrestrictedUnlocks"},
    {"ClientSettings","Kit",0x01bf3b74,0x18,4,RangeFieldType::Uint32,"Client.Kit"},
    {"ClientSettings","Team",0x01bf3b74,0x1c,4,RangeFieldType::Uint32,"Client.Team"},
    {"ServerSettings","InstantSpawn",0x01bf3b3c,0x93,1,RangeFieldType::Boolean,"Server.InstantSpawn"},
    {"GameSettings","UseCustomKit",0x01bf3bc0,0x91,1,RangeFieldType::Boolean,""},
    {"ServerSettings","IsGodMode",0x01bf3b3c,0x95,1,RangeFieldType::Boolean,"Server.IsGodMode"},
    {"ServerSettings","HasUnlimitedAmmo",0x01bf3b3c,0xc0,1,RangeFieldType::Boolean,"Server.HasUnlimitedAmmo"},
    {"ServerSettings","HasUnlimitedMags",0x01bf3b3c,0xc9,1,RangeFieldType::Boolean,"Server.HasUnlimitedMags"},
    {"GameSettings","ForceDeltaTime",0x01bf3bc0,0x58,4,RangeFieldType::Float32,"Game.ForceDeltaTime"},
    {"ClientSettings","UseScriptedInput",0x01bf3b74,0xc9,1,RangeFieldType::Boolean,"Client.UseScriptedInput"},
    // Kit patch route: SoldierAsset(ObjectBlueprint).Object -> SoldierEntityData.Weapons[] -> SoldierWeaponAsset.Object -> SoldierWeaponData
    {"ObjectBlueprint","Object",0x01c11778,0x50,4,RangeFieldType::Ref,""},
    {"SoldierEntityData","WeaponSwitching",0x01c00d3c,0x180,4,RangeFieldType::Ref,""},
    {"SoldierEntityData","WeaponUnlocks",0x01c00d3c,0x1a0,8,RangeFieldType::Array,""},
    {"SoldierEntityData","Weapons",0x01c00d3c,0x1b0,8,RangeFieldType::Array,""},
    {"SoldierWeaponUnlockData","WeaponSlot",0x01c00bc0,0x0,4,RangeFieldType::Uint32,""},
    {"SoldierWeaponUnlockData","Weapon",0x01c00bc0,0x4,4,RangeFieldType::Ref,""},
    {"SoldierWeaponData","WeaponAssetName",0x01bfca60,0x40,0xc,RangeFieldType::String,""},
    // Pickup route: repoint an existing WeaponPickupEntityData entry (e.g. "SPAS12 and Grenade")
    {"WeaponPickupEntityData","Weapons",0x01c00f9c,0xa4,8,RangeFieldType::Array,""},
    {"WeaponPickupData","LinkedToWeaponSlot",0x01c00f88,0x0,4,RangeFieldType::Int32,""},
    {"WeaponPickupData","MinAmmo",0x01c00f88,0x4,4,RangeFieldType::Uint32,""},
    {"WeaponPickupData","MaxAmmo",0x01c00f88,0x8,4,RangeFieldType::Uint32,""},
    {"WeaponPickupData","Weapon",0x01c00f88,0xc,4,RangeFieldType::Ref,""},
    {"WeaponPickupData","WeaponSlot",0x01c00f88,0x10,4,RangeFieldType::Uint32,""},
    {"WeaponPickupData","AltWeaponSlot",0x01c00f88,0x14,4,RangeFieldType::Int32,""},
}};
static_assert([]{for(const auto& f:RangeNativeFields)if(f.owner.empty()||f.field.empty()||!f.typeInfo||!f.bytes)return false;return true;}(),
    "every RangeNativeFields entry must be populated");
// The entry a RangeSettings() key maps to, or nullptr (case-sensitive exact key).
constexpr const RangeNativeField* FindRangeSettingField(std::string_view key)noexcept{
    for(const auto& f:RangeNativeFields)if(!f.setting.empty()&&f.setting==key)return &f;
    return nullptr;
}
}
