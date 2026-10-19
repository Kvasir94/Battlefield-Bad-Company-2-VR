#pragma once
#include "fvr/runtime/PresentationPolicy.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/Feedback.h"
#include "fvr/graphics/BodyPropFrame.h"
namespace fvr::runtime {
// Local C++ contract shared by native producers, IPC and XR presentation.
// Not an exported ABI: serialize explicitly at process/module boundaries.
class IFrameProvider {
public:
    virtual ~IFrameProvider()=default;
    // Called on the frame thread. Use a bounded wait; false means no fresh pair.
    // The presenter may retain its own last complete images with their render poses.
    virtual bool TryGetPair(const PresentationRequirements&,const TrackingFrame&,
        graphics::TextureDescriptor&,graphics::PairTicket&) noexcept=0;
    // Asynchronous sources may complete an earlier request. Its ORIGINAL tracking
    // is returned explicitly and must match the ticket; current is never substituted.
    virtual bool TryGetCompletedPair(const PresentationRequirements& r,const TrackingFrame& current,
        TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& t) noexcept {
        rendered=current;return TryGetPair(r,current,d,t);
    }
    // Optional render-only metadata for the EXACT delivered image pair.
    virtual bool ReadBodyProps(const graphics::PairTicket&,graphics::BodyPropFrame& out)noexcept {out={};return false;}
    // Optional scene depth delivered with the EXACT pair (XR depth layers).
    virtual bool ReadDepth(const graphics::PairTicket&,graphics::DepthAttachment& out)noexcept {out={};return false;}
    // The presenter can submit depth: ask the source to attach it to future pairs.
    virtual void RequestDepth(bool) noexcept {}
    virtual void UpdateInput(const interaction::InputFrame&) noexcept {}
    // Newest predicted view sample for a producer late latch (optional lane).
    virtual void PublishPose(const PresentationRequirements&,const TrackingFrame&) noexcept {}
    // Refresh a prospective successor before acknowledging an older GPU lease.
    // Must never relabel the delivered pair's original tracking.
    virtual void PrepareFrame(const PresentationRequirements&,const TrackingFrame&) noexcept {}
    virtual bool TakeFeedback(interaction::FeedbackEvent& event)noexcept {event={};return false;}
    // Pace-to-content (host): whether this source paces its pairs at all; whether a
    // completed pair could be taken right now; a bounded wait for the producer's
    // next publication signal; and keeping one request queued while the presenter
    // waits. Local sources never make the presenter wait.
    virtual bool PacesContent() noexcept {return false;}
    virtual bool CompletedPairReady() noexcept {return true;}
    virtual bool WaitForPublication(unsigned) noexcept {return false;}
    virtual void KeepRequestQueued(const PresentationRequirements&,const TrackingFrame&) noexcept {}
    virtual void Suspend() noexcept {}
    // False means the pair was not consumed; reset/reclaim that texture stream.
    // True reports GPU consumption, not that xrEndFrame succeeded or was seen.
    virtual void PairConsumed(const graphics::PairTicket&,bool consumed) noexcept=0;
};
}
