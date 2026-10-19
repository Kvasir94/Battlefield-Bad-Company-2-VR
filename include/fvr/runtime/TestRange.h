#pragma once
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace fvr::runtime {
// Developer test range, version 1 (docs/BC2_TEST_RANGE.md). A range describes
// where a replay or headset session starts and which weapons the player holds:
// it never carries input, expectations or ammo authority. The same text is read
// by tools/bc2_test_range.py (launch arguments, run preparation) and by the
// in-game executor (fvr::bc2::TestRange). Both must stay byte-for-byte aligned;
// tests/fixtures/test-range holds the shared golden outputs.
//
//   FVR_BC2_RANGE 1
//   # comment
//   name=xm8-spas
//   level=Levels/SP_005
//   weapons=XM8_sp_s,SPAS12_sp
//   skip_intro=true
//
// Engine switches (level, menu, movies, AI, god mode, checkpoints, unlocks,
// fixed delta, scripted input, passthrough set.*) become Frostbite settings
// pairs `Class.Field value` (RangeSettings), delivered by the selected route.
// They are emitted only when the key is present: an absent key leaves the
// game's own default (ClientSettings.Level defaults to Levels/SP_01).
// Loadout and placement keys (weapons, select, ammo, teleport, clear_inventory,
// skip_cinematics) are executed in-process and only through admitted operations.
inline constexpr unsigned RangeVersion=1,RangeMaxWeapons=8,RangeMaxSettings=32,RangeMaxLineBytes=512;
inline constexpr unsigned RangeDefaultTimeoutS=180,RangeDefaultReadyTimeoutS=90,RangeMaxTimeoutS=3600;
inline constexpr int RangeMaxRounds=100000;
// How the level is loaded and the settings pairs reach the engine (PC static
// RE, build 795745). The command line and Win32Game.cfg are a dead end: argv
// values are only looked up by exact non-dotted names (0x4f4cf0), so
// `-Client.Level ...` is ignored. SP starts only through the menu command newSP
// (UI command 132 -> 0x7600f0), which loads whatever Client.Level holds.
// Menu (primary, route B): the PC launcher (bc2/test/range,
//   tools/testrange/Start-BC2TestRange.ps1) injects ~3 s after launch and
//   applies BC2TestRange.cfg through SettingsManager::Set (0x4f4bd0); newSP
//   then loads Client.Level (LoadLevel).
// Override (route A): NOT VIABLE on retail. It needs a suspended or early
//   inject, and injecting ~100 ms after launch makes BC2 exit silently. Kept
//   parseable so old ranges fail at Begin (unavailable) rather than at parse.
// Manual: no native operation; a person or launcher starts the level and the
//   executor only waits for it (fallback, works on today's build).
enum class RangeLevelRoute:std::uint8_t {Menu,Override,Manual};
enum class RangeAmmoMode:std::uint8_t {Native,Full};
// loaded/reserve -1 = leave native; full = the weapon's own capacity and reserve.
struct RangeAmmoOverride {std::string asset;bool full=false;int loaded=-1,reserve=-1;bool operator==(const RangeAmmoOverride&)const=default;};
struct RangeSetting {std::string key,value;bool operator==(const RangeSetting&)const=default;}; // key `Class.Field`, emitted as `-Class.Field value`
// text keeps the validated source spelling so C++ and Python format it identically.
struct RangeTeleport {float x=0,y=0,z=0,yaw=0;bool hasYaw=false;std::string text;bool operator==(const RangeTeleport&)const=default;};
struct RangeSpec {
    std::string name,level,spawn,savepoint,notes;
    RangeLevelRoute route=RangeLevelRoute::Menu;
    std::vector<std::string> weapons; // exact native SoldierWeaponData asset names, first is the default selection
    std::vector<int> weaponSlots;     // parallel to weapons: WeaponSwitchingEnum value, -1 = mapping chooses
    std::string select;               // empty = weapons.front()
    bool clearInventory=true;         // remove carried weapons not listed in `weapons`
    bool skipCinematics=false;        // in-process skip when a cutscene is observed (native, gated)
    RangeAmmoMode ammo=RangeAmmoMode::Native;
    std::vector<RangeAmmoOverride> ammoOverrides;
    std::optional<RangeTeleport> teleport;
    // Engine switches; nullopt = not emitted.
    std::optional<bool> menu,skipIntro,fastStartup,cutscenes,ai,god,checkpoints,unlocks,instantSpawn,unlimitedAmmo,unlimitedMags,scriptedInput;
    std::optional<unsigned> kit,team;
    std::string fixedDt; // validated decimal text, emitted verbatim; empty = not emitted
    std::vector<RangeSetting> settings;
    unsigned timeoutS=RangeDefaultTimeoutS,readyTimeoutS=RangeDefaultReadyTimeoutS;
    bool operator==(const RangeSpec&)const=default;
    const std::string& Selected()const noexcept{return select.empty()&&!weapons.empty()?weapons.front():select;}
};
// WeaponSwitchingEnum (typeinfo dump 795745): wsPrimary1=0 ... wsHandGrenade=8.
// Range files spell the slot without the `ws` prefix: `weapons=XM8_sp_s@Primary1`.
inline constexpr std::string_view RangeSlotNames[]={"Primary1","Primary2","GrenadeLauncher1","GrenadeLauncher2",
    "StaticGadget","DynamicGadget1","DynamicGadget2","MeleeWeapon","HandGrenade"};
// Shipped single-player level ids (Dist/win32/levels/<id>, PC 2026-10-08).
// A range level is `Levels/<id>`; the id compares case-insensitively.
inline constexpr std::string_view RangeLevelIds[]={"sp_01","sp_02","sp_03","sp_04","sp_003_b","sp_004_b",
    "sp_005","sp_005_b","sp_006","sp_007","sp_008","sp_008_b","sp_009"};
// Field validators shared with the executor and the tape `#! range.*` bridge.
bool ValidRangeAsset(std::string_view)noexcept;
bool ValidRangeLevel(std::string_view)noexcept; // `Levels/<id>` with a RangeLevelIds id
bool ValidRangeIdentifier(std::string_view)noexcept; // spawn, savepoint, name
bool ValidRangeSettingKey(std::string_view)noexcept; // Class.Field
bool ValidRangeSettingValue(std::string_view)noexcept;
// Applies one `key=value`. Rejects unknown keys and duplicates of keys seen in
// `seen` (pass the same vector for one document). Used by the file parser and by
// tape `#! range.key=value` overrides (overrides use a fresh `seen`).
bool ApplyRangeKey(RangeSpec&,std::string_view key,std::string_view value,std::vector<std::string>& seen,std::string& error);
// Whole-spec rules: required level and weapons, select/ammo assets present,
// timeouts ordered, no passthrough switch duplicating a typed one.
bool ValidateRange(const RangeSpec&,std::string& error);
// Parses and validates a complete document. On failure `error` names the line.
bool ParseRange(std::string_view text,RangeSpec&,std::string& error);
bool ParseRange(std::istream&,RangeSpec&,std::string& error);
// Engine settings pairs in a fixed order, e.g. {"Client.Level","Levels/sp_02"}.
// Route-independent text; route=manual means nobody writes them.
std::vector<RangeSetting> RangeSettings(const RangeSpec&);
// Canonical document text (fixed key order, `\n` endings); parse(format(x)) == x.
std::string FormatRange(const RangeSpec&);
}
