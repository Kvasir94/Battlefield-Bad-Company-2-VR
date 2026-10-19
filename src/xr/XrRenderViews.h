#pragma once
// Render-view sampling for the stereo request: display-time prediction, FOV
// overscan and the late-latch pose lane. Kept apart from OpenXrHost::Frame so the
// host's frame steps can change independently. Include after
// <openxr/openxr_platform.h> (XR_NO_PROTOTYPES, function pointers passed in).
#include "fvr/runtime/LateLatch.h"
#include "fvr/runtime/FrameProvider.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <cwchar>
namespace fvr::xr {
struct RenderViewPolicy {
    // FVR_XR_PREDICT_REQUEST=0: locate request views at this XR frame's display
    // time (old behaviour) instead of when the pair will actually be shown.
    bool predict=true;
    // FVR_XR_OVERSCAN=<0..0.25>, default 0.08: FOV tangents rendered and submitted
    // this much wider, so rotational reprojection has image at the edges. 0 = off.
    float overscan=.08f;
    // FVR_BC2_LATE_LATCH=0 (honoured by host and game): no pose lane samples.
    bool latePose=true;
    // BC2 native frustum (0x965bf0) clamps the half-angle near 1 rad; overscan
    // never widens a tangent past tan(0.98) so the stereo culling cone stays valid.
    static constexpr float OverscanLimitTangent=1.4910171f;
    static RenderViewPolicy FromEnvironment() noexcept {
        RenderViewPolicy p;wchar_t value[32]{};
        const auto off=[&](const wchar_t* name){const auto n=GetEnvironmentVariableW(name,value,32);return n==1&&value[0]==L'0';};
        p.predict=!off(L"FVR_XR_PREDICT_REQUEST");p.latePose=!off(L"FVR_BC2_LATE_LATCH");
        const auto n=GetEnvironmentVariableW(L"FVR_XR_OVERSCAN",value,32);
        if(n>0&&n<32){wchar_t* end=nullptr;const double parsed=std::wcstod(value,&end);
            if(end&&*end==0&&std::isfinite(parsed)&&parsed>=0&&parsed<=.25)p.overscan=float(parsed);}
        return p;
    }
};
struct RenderViewStats {
    std::uint64_t predicted=0,predictionFallbacks=0,overscanned=0,posesPublished=0;
    std::int64_t leadNs=0;
};
class RenderViewSampler {
public:
    void Configure(const RenderViewPolicy& policy) noexcept {policy_=policy;lead_.Reset();}
    const RenderViewPolicy& Policy() const noexcept {return policy_;}
    const RenderViewStats& Stats() const noexcept {return stats_;}
    // First presentation of a fresh pair: its age from sample to display.
    void ObservePresentation(std::int64_t ageNs) noexcept {lead_.Observe(ageNs);stats_.leadNs=lead_.LeadNs();}
    void ResetLead() noexcept {lead_.Reset();stats_.leadNs=0;}
    // `tracking` holds valid views located at frameTime. Replace its head/eyes/FOV
    // with views predicted for the pair's expected display time and widen the FOV.
    // predictedNs stays the XR frame key (ordering/age); generation is unchanged.
    void Refine(PFN_xrLocateViews locateViews,PFN_xrLocateSpace locateSpace,XrSession session,XrSpace local,XrSpace head,
        XrTime frameTime,runtime::TrackingFrame& tracking) noexcept {
        const auto lead=lead_.LeadNs();
        if(policy_.predict&&lead>0&&locateViews&&locateSpace){
            std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewState state{XR_TYPE_VIEW_STATE};
            XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate.displayTime=frameTime+lead;locate.space=local;std::uint32_t count=0;
            XrSpaceLocation located{XR_TYPE_SPACE_LOCATION};
            constexpr auto validViews=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            constexpr auto validHead=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            bool okay=XR_SUCCEEDED(locateViews(session,&locate,&state,2,&count,views.data()))&&count==2&&(state.viewStateFlags&validViews)==validViews&&
                XR_SUCCEEDED(locateSpace(head,local,frameTime+lead,&located))&&(located.locationFlags&validHead)==validHead;
            runtime::TrackingFrame next=tracking;
            if(okay){
                next.head=PoseOf(located.pose);
                for(unsigned eye=0;eye<2;++eye){next.eyes[eye]=PoseOf(views[eye].pose);const auto& f=views[eye].fov;
                    next.fov[eye]={std::tan(f.angleLeft),std::tan(f.angleRight),std::tan(f.angleUp),std::tan(f.angleDown)};
                    okay&=bool(math::MakeRelativePose(next.referenceHead,next.eyes[eye]))&&bool(math::MakeLhProjectionFromFovTangents(next.fov[eye],.05f,100.f));}
                okay&=bool(math::MakeRelativePose(next.referenceHead,next.head));
            }
            if(okay){tracking=next;++stats_.predicted;}else ++stats_.predictionFallbacks;
        }
        if(policy_.overscan>0){
            for(auto& fov:tracking.fov)fov=runtime::OverscanFov(fov,policy_.overscan,RenderViewPolicy::OverscanLimitTangent);
            ++stats_.overscanned;
        }
    }
    void Publish(runtime::IFrameProvider* provider,const runtime::PresentationRequirements& requirements,const runtime::TrackingFrame& tracking) noexcept {
        if(!policy_.latePose||!provider)return;provider->PublishPose(requirements,tracking);++stats_.posesPublished;
    }
private:
    static math::Pose PoseOf(const XrPosef& p) noexcept {return {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}};}
    RenderViewPolicy policy_{};runtime::PresentationLead lead_;RenderViewStats stats_{};
};
}
