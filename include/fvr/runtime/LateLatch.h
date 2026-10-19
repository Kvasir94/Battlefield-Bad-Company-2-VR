#pragma once
#include "fvr/runtime/FrameCoordinator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace fvr::runtime {
// Late latch: the producer may replace a request's head/eye sample with a newer
// one published by the same host, at the last native point where the camera is
// written. The pair is then labelled with the sample actually rendered, so the
// presenter submits exactly the rendered poses. Never a different space,
// reference, world scale, or an older/non-monotonic sample.
inline constexpr std::int64_t MaxRelatchAdvanceNs=150000000;
inline bool CanRelatch(const TrackingFrame& requested,const TrackingFrame& candidate) noexcept {
    if(!requested.focused||!requested.headValid||!candidate.focused||!candidate.headValid)return false;
    if(candidate.spaceGeneration!=requested.spaceGeneration||candidate.generation<=requested.generation)return false;
    if(candidate.predictedNs<requested.predictedNs||candidate.predictedNs-requested.predictedNs>MaxRelatchAdvanceNs)return false;
    if(candidate.worldUnitsPerMeter!=requested.worldUnitsPerMeter)return false;
    if(std::memcmp(&candidate.referenceHead,&requested.referenceHead,sizeof(candidate.referenceHead)))return false;
    for(unsigned eye=0;eye<2;++eye)if(!math::MakeLhProjectionFromFovTangents(candidate.fov[eye],.05f,100.f))return false;
    return true;
}
// Widen each FOV tangent by `amount` (0.08 = 8%) so rotational reprojection has
// rendered margin. A tangent already beyond `limitTangent` is left unchanged and
// a widened one is capped there (BC2's native frustum clamps the half-angle).
inline math::FovTangents OverscanFov(const math::FovTangents& fov,float amount,float limitTangent) noexcept {
    if(!std::isfinite(amount)||amount<=0||!std::isfinite(limitTangent)||limitTangent<=0)return fov;
    const auto widen=[&](float t){const float a=std::abs(t),w=a*(1+amount);
        if(!std::isfinite(w)||a>=limitTangent)return t;return std::copysign((std::min)(w,limitTangent),t);};
    math::FovTangents out{widen(fov.left),widen(fov.right),widen(fov.up),widen(fov.down)};
    if(!math::MakeLhProjectionFromFovTangents(out,.05f,100.f))return fov;
    return out;
}
// Observed request-to-first-presentation age, in the XR display-time domain.
// The lead is how far past the request frame the views should be predicted so
// they match the display time when the pair is actually shown.
class PresentationLead {
public:
    static constexpr std::int64_t MaxLeadNs=50000000;
    void Observe(std::int64_t ageNs) noexcept {
        if(ageNs<0||ageNs>250000000)return;
        // Exponential average, 1/8 weight: follows rate changes within ~10 pairs.
        averageNs_=samples_?averageNs_+(ageNs-averageNs_)/8:ageNs;if(samples_<1000000)++samples_;
    }
    void Reset() noexcept {averageNs_=0;samples_=0;}
    std::int64_t LeadNs() const noexcept {return samples_<4?0:std::clamp<std::int64_t>(averageNs_,0,MaxLeadNs);}
    std::uint64_t Samples() const noexcept {return samples_;}
private:
    std::int64_t averageNs_=0;std::uint64_t samples_=0;
};
}
