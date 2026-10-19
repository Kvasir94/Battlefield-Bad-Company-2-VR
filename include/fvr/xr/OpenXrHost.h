#pragma once
#include "fvr/runtime/FrameProvider.h"
#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include "fvr/graphics/D3D11MirrorWindow.h"
#include "fvr/interaction/ControllerInput.h"
#include <filesystem>
#include <string>
#include <functional>
namespace fvr::ipc {class MenuChannel;class ReloadAudioChannel;}
namespace fvr::xr {
using IFrameProvider=runtime::IFrameProvider;
struct HostOptions {
    std::filesystem::path loaderPath;
    bool probeOnly=true;
    bool controllers=false,roomscale=false;
    float worldUnitsPerMeter=1;
    unsigned seconds=15; // Zero requires a stopRequested lifetime source.
    std::function<bool()> stopRequested;
    unsigned eyeWidth=0,eyeHeight=0; // Zero selects the runtime recommendation.
    std::filesystem::path readyPath; // Optional bounded-launcher readiness file.
    std::filesystem::path recordTape; // Optional host companion tape (docs/REPLAY_INPUT.md); new absolute path.
    IFrameProvider* provider=nullptr;
    ipc::MenuChannel* menu=nullptr;
    ipc::ReloadAudioChannel* reloadAudio=nullptr;
    std::filesystem::path reloadAudioDirectory;
    std::shared_ptr<const graphics::BodyPropCatalog> bodyProps;
    bool mirrorWindow=false; // Desktop window showing the composed left eye (what the headset receives).
    // Hybrid diagnostic: real OpenXR session, timing, views and submission, but
    // the game receives the magazine fixture's static input (identity head and
    // reference, fixed tracked hands) instead of the HMD head and controllers,
    // so an unattended headset can run the scripted reload at HMD frame timing.
    bool syntheticHands=false;
    bool ignorePresence=false; // Diagnostic: request frames while nobody wears the HMD.
};
struct HostReport {
    // Cumulative stage timings and fixed histograms survive normal shutdown;
    // the event stream checkpoints the same values once a second.
    std::array<std::uint64_t,9> stageTotalUs{},stageMaxUs{};
    // Queued, completed, timed out, other failures, retired, pending polls.
    std::array<std::uint64_t,6> asyncCopyEvidence{};
    bool asyncConsumerCopy=false;
    bool reloadAudioRequested=false,reloadAudioReady=false,reloadAudioExtension=false;
    std::string reloadAudioRoute="disabled";
    std::int32_t reloadAudioError=0;
    std::uint64_t reloadAudioPlayed=0,reloadAudioDropped=0,reloadAudioRejected=0,reloadAudioStops=0;
    // FVR_XR_RETAIN_ON_COPY_TIMEOUT (default on): a timed-out async copy keeps
    // its already-submitted pair retained instead of forcing no-world frames.
    bool retainOnCopyTimeout=false;
    // Timeouts that kept the pair retained; frames after a timeout (until the
    // next pair) that presented the world; and such frames with no world layer
    // although focused, tracked, rendering and menu-free.
    std::array<std::uint64_t,3> copyTimeoutPresentation{};
    // Acknowledged async copies by queue-to-observed-completion latency, an
    // upper bound set by poll cadence: <=2,5,11,17,20,25,33,50,>50 ms.
    std::array<std::uint64_t,9> copyLatencyBuckets{};
    // Telemetry only: eventual GPU completion of timed-out copies, <=20,25,33,
    // 50,100,250 ms, still incomplete/failed after 250 ms, not watched (slots
    // full, no event, or unresolved at shutdown).
    std::array<std::uint64_t,8> lateCopyBuckets{};
    std::array<std::uint64_t,10> controllerSampling{};
    // Metadata, space, future, age, open, copy Busy/Invalid/Unavailable/Faulted, other.
    std::array<std::uint64_t,11> pairRejectReasons{};
    // Host robustness: recoverable XR errors, swapchain wait timeouts, held/failed uploads,
    // EndFrame failures, retained-world frames while VISIBLE / during head loss, late copies.
    std::uint64_t xrRecoverableErrors=0,swapchainTimeouts=0,uploadHeld=0,uploadHeldFrames=0,uploadFailed=0,endFrameFailures=0,retainedVisibleFrames=0,retainedHeadLossFrames=0,lateCopies=0;
    std::int32_t lastXrError=0;
    bool sessionLost=false,retainVisible=false;
    std::int32_t lastPairTransferError=0;
    std::array<std::string,2> controllerCurrentProfiles{};
    std::array<std::int32_t,2> controllerProfileResults{};
    std::array<std::uint64_t,8> frameGapBuckets{},freshAgeBuckets{};
    std::array<std::uint64_t,4> gatedFrames{}; // !shouldRender, invalid head, unfocused, menu (can overlap)
    bool instanceCreated=false,systemAvailable=false,sessionCreated=false,okay=false;
    std::string runtimeName,systemName,error;
    runtime::PresentationRequirements requirements{};
    std::uint32_t minimumFeatureLevel=0;
    std::uint64_t waitedFrames=0,endedFrames=0,validTrackingFrames=0,submittedPairs=0,rejectedPairs=0;
    std::uint64_t presentedFrames=0,reusedFrames=0,blankFrames=0,providerMisses=0,maxProviderWaitUs=0,maxRetainedAgeNs=0;
    bool controllersEnabled=false,floorRelative=false,userPresenceSupported=false;
    std::uint64_t userPresenceEvents=0;
    unsigned controllerProfiles=0;
    std::uint64_t inputFrames=0,trackedHandFrames=0,recenters=0,automaticRecenters=0;
    interaction::RecenterEvidence recenterInput{};
    std::uint64_t menuFrames=0,menuErrors=0;
    graphics::BodyPropComposeStats bodyProps{};bool bodyPropsReady=false;
    graphics::MirrorWindowStats mirror{};
    std::uint64_t syntheticInputFrames=0;
    std::uint64_t feedbackApplied=0,feedbackRejected=0,feedbackErrors=0;
    std::uint64_t captureFeedbackApplied=0,receiptFeedbackApplied=0,seatedFeedbackApplied=0,lastFeedbackEvent=0;
    std::int32_t lastSessionState=0;
    std::uint64_t recordedTicks=0;bool recordFailed=false;
    // XrRenderViews: request views predicted (and fallbacks), overscanned samples,
    // late-latch pose samples published, current presentation lead.
    struct RenderViews {std::uint64_t predicted=0,predictionFallbacks=0,overscanned=0,posesPublished=0;std::int64_t leadNs=0;} renderViews{};
    float overscan=0;bool requestPrediction=false,latePose=false;
    // Depth layers (XR_KHR_composition_layer_depth; FVR_XR_DEPTH_SUBMIT=0 disables):
    // runtime has the extension, switch on, layers active. Setup failure step (see
    // InitializeDepth). Pairs whose depth was copied, missing, rejected or failed to
    // copy; depth uploads that did not fully land; frames submitted with depth.
    bool depthExtension=false,depthRequested=false,depthLayers=false;std::uint32_t depthSetupFailure=0;
    std::uint64_t depthPairs=0,depthMissing=0,depthRejected=0,depthCopyFailures=0,depthUploadFailures=0,depthFrames=0;
    // Display refresh (XR_FB_display_refresh_rate). FVR_XR_REFRESH_HZ: target Hz,
    // default 80, 0 leaves the runtime's own setting. Requested is the supported
    // rate nearest the target; current follows the runtime's change events.
    // result: last XrResult of the request (0 when nothing was requested).
    bool refreshExtension=false;float refreshTargetHz=0,refreshInitialHz=0,refreshRequestedHz=0,refreshCurrentHz=0;
    std::int32_t refreshResult=0;std::string refreshRates;
    // Pace-to-content (FVR_XR_PACE_TO_CONTENT, default on): before xrWaitFrame,
    // wait up to paceMaxWaitMs (FVR_XR_PACE_MAX_WAIT_MS, 0 = one display period)
    // for the game's next stereo pair instead of re-submitting the stale one at
    // the full refresh, so the runtime sees the real content rate and its own
    // frame synthesis (ASW / Motion Smoothing) can engage. Waits started, ended by
    // a ready pair, ended by the limit; total and worst wait.
    bool paceToContent=false;unsigned paceMaxWaitMs=0;
    std::uint64_t paceWaits=0,paceSatisfied=0,paceTimeouts=0,paceWaitTotalUs=0,paceWaitMaxUs=0;
};
// Uses the installed active runtime; never changes the registry or game settings.
HostReport RunOpenXrHost(const HostOptions&) noexcept;
}
