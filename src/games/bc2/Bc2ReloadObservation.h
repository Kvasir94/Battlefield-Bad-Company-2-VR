#pragma once
#include "Bc2AmmoSupply.h"
#include "Bc2ReloadKeepAlive.h"
namespace fvr::bc2 {
enum class ReloadObservationResult:unsigned {Available,Deferred,Rejected,CohortGap};
struct ReloadReserveObservation {
 ReloadObservationResult result=ReloadObservationResult::Rejected;
 std::optional<Bc2AmmoReserveLease> lease;
 // Reserve reader stage that produced this result (the reserve_read_stages
 // counter index): 0 request mode off ... 9 accepted. Diagnostics only.
 unsigned stage=0;
};
// Stable names for the reserve reader stages, used by gun-event reason rows.
constexpr const char* ReloadReserveStageName(unsigned stage)noexcept {
 constexpr const char* names[]={"request_mode_off","entry","owner_lock","owner_missing","config",
  "boundary_or_capacity","cohort","expired_or_changed","sequence","accepted"};
 return stage<10?names[stage]:"unknown";
}
}
