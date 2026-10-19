#pragma once
#include "fvr/interaction/ChargingHandleRack.h"
#include "fvr/interaction/ReloadInsertion.h"
#include <span>
#include <string_view>

namespace fvr::bc2 {
struct PistolSlidePart {
    std::string_view bone{};
    math::Matrix4 closedInWeaponMeters{};
};
struct PistolMechanismProfile {
    std::string_view asset{};
    std::uint64_t rigFingerprint=0;
    interaction::ChargingHandleProfile handle{};
    std::span<const PistolSlidePart> parts{};
};
}
#ifdef FVR_BC2_PISTOL_MECHANISM_HEADER
#include FVR_BC2_PISTOL_MECHANISM_HEADER
#endif
namespace fvr::bc2 {
inline const PistolMechanismProfile* FindPistolMechanism(std::string_view asset)noexcept {
#ifdef FVR_BC2_PISTOL_MECHANISM_HEADER
    const PistolMechanismProfile* found=nullptr;
    for(const auto& p:generated::PistolMechanisms)if(p.asset==asset){
        if(found||!p.rigFingerprint||!p.handle.pistolSlide||!interaction::ValidChargingHandleProfile(p.handle)||
           p.parts.empty()||p.parts.size()>32)return nullptr;
        for(std::size_t i=0;i<p.parts.size();++i){
            if(p.parts[i].bone.empty()||!interaction::reload_insertion_detail::Rigid(p.parts[i].closedInWeaponMeters))return nullptr;
            for(std::size_t j=0;j<i;++j)if(p.parts[i].bone==p.parts[j].bone)return nullptr;
        }
        found=&p;
    }
    return found;
#else
    (void)asset;return nullptr;
#endif
}
}
