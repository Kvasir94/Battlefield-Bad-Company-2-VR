#pragma once
#include "fvr/interaction/PhysicalWeaponState.h"
#include "fvr/interaction/AmmoSupplyVisual.h"
#include <optional>
#include <span>
namespace fvr::bc2 {
// Only ammunition offered at the body contact. Installed rounds/chamber/action
// are deliberately excluded: firing them does not change the reserve prop.
struct BodyAmmoMaterial {
 struct Retained {std::uint64_t id=0;std::uint32_t rounds=0,capacity=0;bool operator==(const Retained&)const=default;};
 interaction::PhysicalWeaponIdentity weapon{};
 std::uint32_t clientWeapon=0,looseReserve=0,feedCapacity=0;
 interaction::HandInteractionKey pool{};
 std::array<Retained,interaction::PhysicalWeaponState::MaxMagazines> retained{};
 bool operator==(const BodyAmmoMaterial&)const=default;
};
inline std::optional<BodyAmmoMaterial> BodyAmmoMaterialFor(const interaction::PhysicalWeaponIdentity& identity,
 std::uint32_t client,interaction::HandInteractionKey pool,const interaction::PhysicalWeaponSnapshot& state,
 std::span<const interaction::PhysicalMagazine> magazines)noexcept {
 using namespace interaction;
 if(!identity.actor||!identity.actorGeneration||!identity.weapon||!identity.weaponGeneration||
  !client||!pool.id||!pool.generation||!state.initialized||
  state.identity!=identity||!state.config.feedCapacity||magazines.size()>PhysicalWeaponState::MaxMagazines||
  (state.config.family!=PhysicalWeaponFamily::ClosedBoltMagazine&&state.config.family!=PhysicalWeaponFamily::OpenBoltMagazine&&
   state.config.family!=PhysicalWeaponFamily::NoActionMagazine))return {};
 BodyAmmoMaterial out;out.weapon=identity;out.clientWeapon=client;out.pool=pool;
 out.looseReserve=state.looseReserve;out.feedCapacity=state.config.feedCapacity;unsigned n=0;
 for(const auto& m:magazines)if(m.location==PhysicalMagazineLocation::Retained){
  if(!m.id||m.rounds>m.capacity||m.capacity!=out.feedCapacity)return {};
  out.retained[n++]={m.id,m.rounds,m.capacity};
 }
 std::sort(out.retained.begin(),out.retained.begin()+n,[](const auto& a,const auto& b){return a.id<b.id;});
 for(unsigned i=1;i<n;++i)if(out.retained[i-1].id==out.retained[i].id)return {};
 return out;
}
inline bool BodyAmmoMaterialOffers(const BodyAmmoMaterial& m,const interaction::AmmoSupplySource& source)noexcept {
 std::uint64_t available=m.looseReserve;std::uint32_t best=0;
 for(const auto& r:m.retained){available+=r.rounds;best=std::max(best,r.rounds);}
 return source.family==interaction::ReloadInsertionFamily::Magazine&&source.identity.pool==m.pool&&
  source.reserveUnits==available&&source.objectUnits==std::min<std::uint64_t>(m.feedCapacity,std::uint64_t(m.looseReserve)+best);
}
}
