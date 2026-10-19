#pragma once

namespace fvr::bc2 {
// Existing ReloadRack events: left/loading hand is bit 0, right/gun hand
// is bit 1. The physical stroke returns against both hands; a button bolt
// release belongs to the gun hand alone (KF2 ReleaseSlideLock). Completion
// is the consumer's successful chamber transition, never a button request
// or a handle-position sample. Event freshness remains the caller's job.
constexpr unsigned ReloadRackFeedbackHands(bool completed,bool buttonRelease)noexcept {
    return completed?(buttonRelease?2u:3u):0u;
}
}
