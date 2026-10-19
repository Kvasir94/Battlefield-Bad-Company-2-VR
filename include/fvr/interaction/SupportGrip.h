#pragma once
#include "ControllerInput.h"
namespace fvr::interaction {
struct SupportGripOwner {std::uint64_t actor=0,generation=0,equipped=0;bool operator==(const SupportGripOwner&)const=default;};
struct SupportGripContact {
 bool valid=false;float distanceMeters=0;
 // Calibrated minus raw wrist separation, in upright reference-space metres.
 math::Vec3 wristOffsetMeters{};
};
enum class SupportRelease : std::uint8_t {None,Tracking,Contact,Action,Identity,Button,Distance,Separation,Crossed};
// Ranges (metres) and squeeze hysteresis. Defaults are the original policy
// values; BC2 supplies InteractionZoneKind::Support from its zone table.
struct SupportGripConfig {float captureMeters=.16f,releaseMeters=.28f,squeezeOn=.7f,squeezeOff=.35f;};
inline bool ValidSupportGripConfig(const SupportGripConfig& c)noexcept {
 return c.captureMeters>0&&c.releaseMeters>c.captureMeters&&c.releaseMeters<=.7f&&c.squeezeOff>=0&&c.squeezeOn>c.squeezeOff&&c.squeezeOn<=1;}
struct SupportGripResult {InputFrame input{};bool holding=false,engaged=false,released=false;float correctionRadians=0;std::uint64_t token=0;SupportRelease reason=SupportRelease::None;};
// Only the gun-hand orientation changes. Positions and off-hand pose stay free.
class SupportGrip {
public:
 SupportGrip()noexcept=default;
 explicit SupportGrip(SupportGripConfig c)noexcept:config_(ValidSupportGripConfig(c)?c:SupportGripConfig{}){}
 const SupportGripConfig& Config()const noexcept{return config_;}
 bool Holding()const noexcept{return holding_;}
 // A transient hand owner prevents acquisition but may observe a fresh release.
 // Hard cancellation/tracking or identity loss still requires neutral rearming.
 SupportGripResult Update(const SupportGripOwner&,const InputFrame&,const SupportGripContact&,bool cancel=false,bool handBusy=false,bool consumeBusySqueeze=true) noexcept;
 void Reset()noexcept {const auto next=nextToken_;const auto config=config_;*this={};nextToken_=next;config_=config;}
 // AS2 support release slerp (F1): after an ordinary release (button,
 // distance, separation, hand busy, action) the two-hand correction fades
 // out over ReleaseNs instead of snapping the gun back to the raw aim.
 // Tracking/identity loss still drops it at once.
 static constexpr std::int64_t ReleaseNs=100000000;
private:
 SupportGripResult UpdateCore(const SupportGripOwner&,const InputFrame&,const SupportGripContact&,bool cancel,bool handBusy,bool consumeBusySqueeze) noexcept;
 SupportGripConfig config_{};
 math::Quaternion lastDelta_{};std::int64_t releaseNs_=0;
 SupportGripOwner owner_{};std::uint64_t space_=0,generation_=0;std::int64_t time_=0;
 std::uint64_t nextToken_=0,token_=0;
 float units_=0;bool armed_=false,holding_=false;math::Vec3 localDirection_{},heldWristOffset_{};
};
}
