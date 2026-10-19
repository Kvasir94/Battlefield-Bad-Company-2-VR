#pragma once
#include "fvr/runtime/PresentationPolicy.h"
#include <optional>

namespace fvr::runtime {
// What the host knows about the frame being submitted, independent of whether
// head tracking produced a valid TrackingFrame for it (a lost head leaves the
// TrackingFrame empty; the display time and space generation are still known).
struct RetentionContext {
    std::int64_t predictedNs=0;
    std::uint64_t spaceGeneration=0;
    bool shouldRender=true; // xrWaitFrame shouldRender; false means nothing is shown.
    bool focused=false;     // Session FOCUSED with the user present: input and gameplay are live.
    bool headValid=false;   // Head and both views located and tracked this frame.
};
// Metadata for the last complete pair in presenter-owned images. The source
// ticket is acknowledged exactly once; repeated presentation never reconsumes it.
// Keep the original render poses/FOV: the compositor reprojects these images.
//
// The retained world layer keeps being submitted through everything that does not
// invalidate its pixels or pose basis:
//  * session VISIBLE (dashboard/overlay open, not focused): up to VisibleMaxAgeNs,
//    only input and gameplay are gated (FVR_XR_RETAIN_VISIBLE=0 restores the reset);
//  * a brief head-tracking loss while focused: up to HeadLossGraceNs.
// It resets only on a space-generation change, a requirements change, a gated
// (non-rendering) frame, a non-monotonic display time, or when its age passes the
// bound for the current state; the host also resets it on session end/presence loss.
class RetainedPresentation {
public:
    static constexpr std::int64_t MaxAgeNs=250000000;          // Focused: pose age bound.
    static constexpr std::int64_t HeadLossGraceNs=100000000;   // Focused, head not tracked.
    static constexpr std::int64_t VisibleMaxAgeNs=30000000000; // Session VISIBLE: frozen world behind a dashboard.
    void Reset() noexcept { saved_.reset(); headLostNs_=0; }
    // false (FVR_XR_RETAIN_VISIBLE=0): the pre-existing behaviour, reset as soon as the
    // session is not focused or the head is not tracked.
    void SetRetainThroughGaps(bool enabled) noexcept { retainGaps_=enabled; }
    bool Commit(const PresentationRequirements& requirements,const TrackingFrame& rendered,
                const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket) noexcept {
        if(!PairMatchesFrame(requirements,rendered,descriptor,ticket)||!ValidViews(rendered))return false;
        requirements_=requirements;saved_=rendered;headLostNs_=0;return true;
    }
    const TrackingFrame* Select(const PresentationRequirements& requirements,const RetentionContext& current) noexcept {
        if(!saved_)return nullptr;
        const auto& old=*saved_;
        // A discontinuity permanently invalidates the retained pair. Regaining
        // focus/tracking must not resurrect an image from before that transition.
        if(!current.shouldRender||current.spaceGeneration!=old.spaceGeneration||current.predictedNs<old.predictedNs||
           requirements.width!=requirements_.width||requirements.height!=requirements_.height||
           requirements.format!=requirements_.format||requirements.adapterLow!=requirements_.adapterLow||
           requirements.adapterHigh!=requirements_.adapterHigh){Reset();return nullptr;}
        const auto age=current.predictedNs-old.predictedNs;
        if(current.focused){
            if(age>MaxAgeNs){Reset();return nullptr;}
            if(current.headValid)headLostNs_=0;
            else if(!retainGaps_){Reset();return nullptr;}
            else {
                if(!headLostNs_)headLostNs_=current.predictedNs;
                // Past the grace the layer is withheld, not discarded: the age
                // bound above still ends it, and a regained head resumes it.
                if(current.predictedNs-headLostNs_>HeadLossGraceNs)return nullptr;
            }
        }else{
            if(!retainGaps_||age>VisibleMaxAgeNs){Reset();return nullptr;}
            headLostNs_=0;
        }
        return &*saved_;
    }
private:
    static bool ValidViews(const TrackingFrame& tracking) noexcept {
        if(!tracking.generation||!tracking.spaceGeneration||tracking.predictedNs<=0)return false;
        for(unsigned eye=0;eye<2;++eye)
            if(!math::MakeLhViewFromOpenXRPose(tracking.eyes[eye])||
               !math::MakeLhProjectionFromFovTangents(tracking.fov[eye],.05f,100.f))return false;
        return true;
    }
    PresentationRequirements requirements_{};
    std::optional<TrackingFrame> saved_;
    std::int64_t headLostNs_=0; // Display time at which the current head loss began.
    bool retainGaps_=true;
};
}
