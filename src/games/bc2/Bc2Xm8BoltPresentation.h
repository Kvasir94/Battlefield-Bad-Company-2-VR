#pragma once
#include "Bc2Rig.h"
#include "Bc2Xm8BoltAsset.h"
#include <atomic>

// Presentation-only seams for the XM8 bolt: rig binding, the carrier travel
// transform, the jntWpn_4 card-collapse decision and the both-eye draw health
// latch. No native call, hook or memory write is made here; the private
// palette edit reuses CollapseWeightedPalette (Bc2WeaponVisibility).
namespace fvr::bc2 {
struct Xm8BoltRigBinding {
    std::uint64_t fingerprint=0;
    std::uint32_t weapon=0,carrier=0,handle=0,card=0;
    bool operator==(const Xm8BoltRigBinding&)const=default;
};
// Exact ske01 fingerprint, unique names, jntWpn_1 as the rig weapon bone, the
// carrier/handle chain under it and the card as a leaf of jntWpn_1.
std::optional<Xm8BoltRigBinding> BindXm8BoltRig(const RigSnapshot&);
// Test seam: identical topology checks against an explicit fingerprint.
std::optional<Xm8BoltRigBinding> BindXm8BoltRigForFingerprint(const RigSnapshot&,std::uint64_t fingerprint);

// Canonical weapon-local carrier frame at an exact travel along the asset axis:
// identity rotation, translation (rest + axis * travel) * unitsPerMetre.
std::optional<math::Matrix4> Xm8BoltCarrierFromWeapon(const Xm8BoltAsset&,float travelMetres,float unitsPerMetre)noexcept;
// Axial travel (metres) of a posed carrier relative to the asset rest frame;
// lateral drift beyond 5 mm or a rotated carrier is rejected.
std::optional<float> Xm8BoltObservedTravel(const Xm8BoltAsset&,const math::Matrix4& carrierFromWeapon,float unitsPerMetre)noexcept;
// Rigid-prop draw transform for the asset's metre vertices from the posed
// carrier world (canonical world, native units): basis rows scaled by units.
std::optional<math::Matrix4> Xm8BoltPartWorld(const math::Matrix4& carrierWorld,float unitsPerMetre)noexcept;

enum class Xm8BoltCardReason : unsigned {Collapse,Disabled,NoAsset,OtherWeapon,NoRig,GunNative,RendererUnproven,Count};
struct Xm8BoltCardInputs {
    bool enabled=false,assetLoaded=false,xm8=false,rigBound=false;
    bool gunRetargeted=false;   // The weapon subtree is in the private palette (tracked right hand).
    bool rendererHealthy=false; // Both eyes of a recent frame drew the bolt (Xm8BoltDrawHealth).
};
// One decision per published palette. The colour and depth (ZOnly) passes
// consume that same palette, and both eyes pack it, so they cannot disagree.
Xm8BoltCardReason DecideXm8BoltCard(const Xm8BoltCardInputs&)noexcept;
const char* Xm8BoltCardReasonName(Xm8BoltCardReason)noexcept;
// Zeroes the card's 12 consumed skin scalars (all its vertices collapse to a
// point) in a copy of the palette; every other entry and all padding is kept.
std::optional<std::vector<std::array<std::byte,64>>> CollapseXm8BoltCard(
    std::span<const std::array<std::byte,64>> posed,const Xm8BoltRigBinding&);

// Both-eye draw latch. Record() runs on the graphics thread only; Healthy()
// may be read from any thread. Any failed eye drops the latch at once; a frame
// whose two eyes both drew renews it for `windowNs`.
class Xm8BoltDrawHealth {
public:
    static constexpr std::int64_t DefaultWindowNs=250000000;
    explicit Xm8BoltDrawHealth(std::int64_t windowNs=DefaultWindowNs)noexcept:window_(windowNs){}
    void Record(std::uint64_t frame,unsigned eye,bool drawn,std::int64_t now)noexcept {
        if(!drawn||eye>1||!frame||now<=0){healthyUntil_.store(0,std::memory_order_release);frame_=0;mask_=0;return;}
        if(frame!=frame_){frame_=frame;mask_=0;}
        mask_|=1u<<eye;
        if(mask_==3u)healthyUntil_.store(now+window_,std::memory_order_release);
    }
    void Reset()noexcept {healthyUntil_.store(0,std::memory_order_release);frame_=0;mask_=0;}
    bool Healthy(std::int64_t now)const noexcept {return now>0&&now<healthyUntil_.load(std::memory_order_acquire);}
private:
    std::int64_t window_=DefaultWindowNs;std::atomic<std::int64_t> healthyUntil_{0};
    std::uint64_t frame_=0;unsigned mask_=0;
};
} // namespace fvr::bc2
