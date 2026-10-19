#pragma once
#include "fvr/graphics/RigidPropFrame.h"
#include "fvr/graphics/RigidPropGeometry.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// XM8 VR bolt carrier group: authored mesh (reports/xm8-bolt-intake) loaded at
// runtime. The JSON is game-derived (native rig binds) and is never committed;
// only this loader is. Nothing here touches the game: it parses, converts the
// part into jntWpnwpnJnt_16-local canonical metres for the rigid-prop renderer
// and cross-checks the asset lane's validation receipt.
namespace fvr::bc2 {
inline constexpr std::string_view Xm8BoltWeaponAsset="XM8_sp_s";
inline constexpr std::uint64_t Xm8BoltRigFingerprint=0xa7f219a1426216abull; // ske01, SightRigFingerprint
inline constexpr std::string_view Xm8BoltWeaponBone="jntWpn_1";
inline constexpr std::string_view Xm8BoltCarrierBone="jntWpnwpnJnt_16";
inline constexpr std::string_view Xm8BoltHandleBone="jntWpn_17";
inline constexpr std::string_view Xm8BoltCardBone="jntWpn_4";
inline constexpr std::string_view Xm8BoltRenderSchema="bc2.private.xm8_bolt_render_input.v1";
inline constexpr std::string_view Xm8BoltValidationSchema="bc2.private.xm8_bolt.validation.v1";
inline constexpr std::string_view Xm8BoltDefaultAsset="reports/xm8-bolt-intake/xm8-bolt-v1-render-input.json";

enum class Xm8BoltAssetStatus : unsigned {
    Loaded,Disabled,NotFound,Read,Json,Schema,Rig,Part,Geometry,Validation,ValidationMismatch,Exception,Count
};
const char* Xm8BoltAssetStatusName(Xm8BoltAssetStatus)noexcept;

struct Xm8BoltAsset {
    // Unindexed triangle list in jntWpnwpnJnt_16-local CANONICAL metres
    // (native bind space, Z reflected; corners reordered with the reflection).
    graphics::RigidPropMesh mesh;
    graphics::RigidPropGeometryKey key{};
    // Canonical weapon-local (jntWpn_1 frame, rear +Z) metres, from the asset.
    std::array<float,3> carrierRest{},handleRest{},axis{};
    float stroke=0;
    std::vector<std::string> hideWhileDrawn; // Native parts the asset occludes (jntWpn_4).
    std::string sha256,partName;
    unsigned vertices=0,triangles=0;
};
struct Xm8BoltAssetResult {
    Xm8BoltAssetStatus status=Xm8BoltAssetStatus::NotFound;
    std::shared_ptr<const Xm8BoltAsset> asset;
    std::string detail; // First failing check, for telemetry.
    std::filesystem::path path,validationPath;
};

// Portable parse of the two JSON documents (render input + validation receipt).
// `renderBytes` is hashed (SHA-256) and must match the receipt's export hash.
Xm8BoltAssetResult ParseXm8BoltAsset(std::span<const std::byte> renderBytes,std::string_view validationJson);
// Test seam: the same parse against another rig fingerprint (synthetic rigs).
Xm8BoltAssetResult ParseXm8BoltAssetForRig(std::span<const std::byte> renderBytes,std::string_view validationJson,std::uint64_t rigFingerprint);
// Reads <path> and the validation receipt it names (resolved next to <path>).
Xm8BoltAssetResult LoadXm8BoltAsset(const std::filesystem::path&)noexcept;

// FVR_XM8_BOLT_ASSET wins when non-empty. Otherwise each anchor and up to five
// of its parents are searched for the repository-relative default asset.
using Xm8BoltExists=std::function<bool(const std::filesystem::path&)>;
std::optional<std::filesystem::path> ResolveXm8BoltAssetPath(std::string_view environment,
    std::span<const std::filesystem::path> anchors,const Xm8BoltExists& exists);
// FVR_XM8_BOLT=0 disables the feature; anything else (or unset) enables it.
bool Xm8BoltSwitchEnabled(std::optional<std::string_view> environment)noexcept;

std::string Xm8BoltSha256Hex(std::span<const std::byte>);
} // namespace fvr::bc2
