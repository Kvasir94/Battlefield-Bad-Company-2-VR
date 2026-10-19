#pragma once
// Headset frame-delivery telemetry (docs/HEADSET-FRAME-DELIVERY-20261011.md).
// One compact "delivery" line per second in xr-events.jsonl that separates what the
// game produced from what the host submitted: fresh stereo pairs vs re-submitted
// (reused) frames, native game frames skipped between presented pairs, content age
// at xrEndFrame, the runtime's frame period vs what actually happened, and the GPU
// latency of the host's transfer/upload work. FVR_XR_DELIVERY_LOG=0 disables it.
// Cost: a few integer operations per XR frame, at most a few hundred stored
// samples per second, one sort per second, and two GPU timestamp queries per
// fresh pair read back without waiting.
// Include after <d3d11.h>/<wrl/client.h>.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ostream>
#include <vector>
namespace fvr::xr {
class DeliveryTelemetry {
public:
    using Clock=std::chrono::steady_clock;
    static bool EnabledFromEnvironment() noexcept {
        wchar_t value[8]{};const auto n=GetEnvironmentVariableW(L"FVR_XR_DELIVERY_LOG",value,8);
        return !(n==1&&value[0]==L'0');
    }
    void Enable(bool on) noexcept {enabled_=on;if(on){Reserve();windowStart_=Clock::now();}}
    bool Enabled() const noexcept {return enabled_;}

    // ---- Tracking samples: generation -> host time the pose was sampled. -----
    // Poses are sampled once per XR frame; a relatched pair carries a later
    // generation, so its age starts at the late-latched sample.
    void TrackingSampled(std::uint64_t generation,Clock::time_point at) noexcept {
        if(!enabled_)return;auto& slot=samples_[generation%samples_.size()];slot.generation=generation;slot.at=at;
    }
    bool SampleTime(std::uint64_t generation,Clock::time_point& at) const noexcept {
        const auto& slot=samples_[generation%samples_.size()];if(!generation||slot.generation!=generation)return false;at=slot.at;return true;
    }

    // ---- xrWaitFrame: the runtime's period vs what actually happened. --------
    void Waited(std::int64_t predictedDisplayTime,std::int64_t predictedPeriod,Clock::time_point returned) noexcept {
        if(!enabled_)return;
        if(lastWaitReturn_!=Clock::time_point{}){
            const auto us=Us(returned-lastWaitReturn_);w_.waitIntervalSumUs+=us;w_.waitIntervalMaxUs=(std::max)(w_.waitIntervalMaxUs,us);++w_.waitIntervals;
        }
        lastWaitReturn_=returned;
        if(predictedPeriod>0)w_.periodNs=predictedPeriod;
        if(lastPredicted_>0&&predictedDisplayTime>lastPredicted_){
            const auto delta=std::uint64_t(predictedDisplayTime-lastPredicted_);
            w_.displayDeltaSumNs+=delta;++w_.displayDeltas;
            // A display-time step of 1.5 periods or more: the runtime skipped at least one
            // display refresh for us (the host missed its slot or was throttled).
            if(predictedPeriod>0&&delta*2>=std::uint64_t(predictedPeriod)*3)w_.skippedRefreshes+=std::uint64_t((delta+std::uint64_t(predictedPeriod)/2)/std::uint64_t(predictedPeriod))-1;
        }
        lastPredicted_=predictedDisplayTime;
    }

    // ---- A fresh pair became the retained presentation (both eyes uploaded). --
    // frameId is the producer's native game frame id (one id per stereo pair: both
    // eyes are captured under the same lease). delivered is when the host took it.
    void FreshCommitted(std::uint64_t frameId,std::uint64_t trackingGeneration,Clock::time_point delivered) noexcept {
        if(!enabled_)return;
        retainedSampleValid_=SampleTime(trackingGeneration,retainedSample_);
        retainedDelivered_=delivered;retainedFresh_=true;
        if(retainedSampleValid_&&delivered>=retainedSample_)Push(sampleToDelivered_,Us(delivered-retainedSample_));
        if(lastFrameId_){
            if(frameId==lastFrameId_)++w_.repeatedFrameIds;
            else if(frameId>lastFrameId_){const auto gap=frameId-lastFrameId_;w_.frameIdAdvance+=gap;++w_.frameIdSteps;if(gap>1)w_.skippedGameFrames+=gap-1;w_.maxFrameIdGap=(std::max)(w_.maxFrameIdGap,gap);}
            else ++w_.frameIdRegressions;
        }
        lastFrameId_=frameId;
    }

    // ---- One xrEndFrame. ---------------------------------------------------------
    // submittedWorld: the projection layer was in the frame. fresh: this frame's layer
    // is a newly committed pair (first submission); otherwise a re-submission.
    void Ended(bool submittedWorld,bool fresh,Clock::time_point endCall) noexcept {
        if(!enabled_)return;
        ++w_.frames;
        if(!submittedWorld){++w_.noWorld;return;}
        if(fresh)++w_.fresh;else ++w_.reused;
        if(fresh&&retainedFresh_)Push(deliveredToEnd_,Us(endCall-retainedDelivered_));
        retainedFresh_=false;
        if(retainedSampleValid_&&endCall>=retainedSample_){
            const auto us=Us(endCall-retainedSample_);Push(ageAll_,us);if(fresh)Push(ageFresh_,us);
        }
    }
    void Failed() noexcept {if(enabled_)++w_.endFailures;}

    // ---- Host GPU latency of a fresh pair's copy + compose + swapchain upload. ---
    void GpuSpan(std::uint64_t us) noexcept {if(enabled_)Push(gpu_,us);}
    void GpuDropped() noexcept {if(enabled_)++w_.gpuDropped;}

    // Writes and resets the window once per second. Returns true when a line was written.
    bool MaybeWrite(std::ostream& out,std::uint64_t wallMs) {
        if(!enabled_)return false;
        const auto now=Clock::now();const auto windowUs=Us(now-windowStart_);if(windowUs<1000000)return false;
        const double seconds=double(windowUs)/1e6;
        out<<"{\"event\":\"delivery\",\"wall_ms\":"<<wallMs<<",\"window_ms\":"<<windowUs/1000
           <<",\"xr_frames\":"<<w_.frames<<",\"fresh\":"<<w_.fresh<<",\"reused\":"<<w_.reused<<",\"no_world\":"<<w_.noWorld<<",\"end_failures\":"<<w_.endFailures
           <<",\"fresh_per_s\":"<<Rate(w_.fresh,seconds)<<",\"reused_per_s\":"<<Rate(w_.reused,seconds)<<",\"xr_fps\":"<<Rate(w_.frames,seconds)
           <<",\"game_frames_per_pair\":"<<(w_.frameIdSteps?double(w_.frameIdAdvance)/double(w_.frameIdSteps):0.)
           <<",\"skipped_game_frames\":"<<w_.skippedGameFrames<<",\"max_game_frame_gap\":"<<w_.maxFrameIdGap
           <<",\"repeated_frame_ids\":"<<w_.repeatedFrameIds<<",\"frame_id_regressions\":"<<w_.frameIdRegressions;
        Stats(out,"age_at_end_us",ageAll_);Stats(out,"fresh_age_at_end_us",ageFresh_);
        Stats(out,"sample_to_delivered_us",sampleToDelivered_);Stats(out,"delivered_to_end_us",deliveredToEnd_);
        Stats(out,"host_gpu_us",gpu_);
        out<<",\"host_gpu_dropped\":"<<w_.gpuDropped
           <<",\"xr_period_us\":"<<w_.periodNs/1000
           <<",\"display_step_us\":"<<(w_.displayDeltas?w_.displayDeltaSumNs/w_.displayDeltas/1000:0)
           <<",\"skipped_refreshes\":"<<w_.skippedRefreshes
           <<",\"wait_interval_us\":{\"mean\":"<<(w_.waitIntervals?w_.waitIntervalSumUs/w_.waitIntervals:0)<<",\"max\":"<<w_.waitIntervalMaxUs<<"}}\n";
        w_={};ageAll_.clear();ageFresh_.clear();sampleToDelivered_.clear();deliveredToEnd_.clear();gpu_.clear();windowStart_=now;
        return true;
    }
private:
    struct Sample {std::uint64_t generation=0;Clock::time_point at{};};
    struct Window {
        std::uint64_t frames=0,fresh=0,reused=0,noWorld=0,endFailures=0;
        std::uint64_t frameIdAdvance=0,frameIdSteps=0,skippedGameFrames=0,maxFrameIdGap=0,repeatedFrameIds=0,frameIdRegressions=0;
        std::uint64_t gpuDropped=0;
        std::int64_t periodNs=0;std::uint64_t displayDeltaSumNs=0,displayDeltas=0,skippedRefreshes=0;
        std::uint64_t waitIntervalSumUs=0,waitIntervalMaxUs=0,waitIntervals=0;
    };
    static constexpr std::size_t MaxSamples=512; // > one second at any XR rate in use.
    static std::uint64_t Us(Clock::duration d) noexcept {const auto c=std::chrono::duration_cast<std::chrono::microseconds>(d).count();return c>0?std::uint64_t(c):0;}
    static double Rate(std::uint64_t n,double seconds) noexcept {return seconds>0?double(std::uint64_t(double(n)/seconds*10.+.5))/10.:0.;}
    static void Push(std::vector<std::uint32_t>& v,std::uint64_t us) {if(v.size()<MaxSamples)v.push_back(std::uint32_t((std::min)(us,std::uint64_t(0xffffffffu))));}
    static void Stats(std::ostream& out,const char* name,std::vector<std::uint32_t>& v) {
        out<<",\""<<name<<"\":{\"n\":"<<v.size();
        if(!v.empty()){
            std::uint64_t sum=0;std::uint32_t max=0;for(auto x:v){sum+=x;max=(std::max)(max,x);}
            const auto at=v.begin()+std::ptrdiff_t((v.size()*95)/100>=v.size()?v.size()-1:(v.size()*95)/100);
            std::nth_element(v.begin(),at,v.end());
            out<<",\"mean\":"<<sum/v.size()<<",\"p95\":"<<*at<<",\"max\":"<<max;
        }
        out<<'}';
    }
    void Reserve() {for(auto* v:{&ageAll_,&ageFresh_,&sampleToDelivered_,&deliveredToEnd_,&gpu_})v->reserve(MaxSamples);}
    bool enabled_=false;
    std::array<Sample,256> samples_{};
    Clock::time_point windowStart_{},lastWaitReturn_{},retainedSample_{},retainedDelivered_{};
    std::int64_t lastPredicted_=0;
    bool retainedSampleValid_=false,retainedFresh_=false;
    std::uint64_t lastFrameId_=0;
    Window w_{};
    std::vector<std::uint32_t> ageAll_,ageFresh_,sampleToDelivered_,deliveredToEnd_,gpu_;
};

// GPU timestamps around the host's own work on its immediate context, read back
// later without waiting (D3D11_ASYNC_GETDATA_DONOTFLUSH). The span is GPU wall time
// between the two markers, so it includes any time the GPU spent on other processes
// (the game) in between: it is transfer *latency* on a shared GPU, not exclusive cost.
class HostGpuTimer {
public:
    void Begin(ID3D11Device* device,ID3D11DeviceContext* context,DeliveryTelemetry& telemetry) noexcept {
        if(!telemetry.Enabled()||!context||open_)return;
        auto& s=ring_[next_];
        if(s.busy){telemetry.GpuDropped();return;} // Still unresolved: drop rather than wait.
        if(!s.disjoint){
            D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0},t{D3D11_QUERY_TIMESTAMP,0};
            if(FAILED(device->CreateQuery(&d,&s.disjoint))||FAILED(device->CreateQuery(&t,&s.start))||FAILED(device->CreateQuery(&t,&s.end))){s={};return;}
        }
        context->Begin(s.disjoint.Get());context->End(s.start.Get());open_=&s;
    }
    void End(ID3D11DeviceContext* context) noexcept {
        if(!open_)return;context->End(open_->end.Get());context->End(open_->disjoint.Get());open_->busy=true;open_->frames=0;open_=nullptr;
        next_=(next_+1)%ring_.size();
    }
    void Poll(ID3D11DeviceContext* context,DeliveryTelemetry& telemetry) noexcept {
        if(!context)return;
        for(auto& s:ring_){
            if(!s.busy)continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};UINT64 a=0,b=0;
            if(context->GetData(s.disjoint.Get(),&dj,sizeof(dj),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||
               context->GetData(s.start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||
               context->GetData(s.end.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK){
                if(++s.frames>240){s.busy=false;telemetry.GpuDropped();} // Never resolved: recycle.
                continue;
            }
            s.busy=false;
            if(!dj.Disjoint&&dj.Frequency&&b>=a)telemetry.GpuSpan((b-a)*1000000ull/dj.Frequency);else telemetry.GpuDropped();
        }
    }
private:
    struct Slot {Microsoft::WRL::ComPtr<ID3D11Query> disjoint,start,end;bool busy=false;unsigned frames=0;};
    std::array<Slot,8> ring_{};Slot* open_=nullptr;std::size_t next_=0;
};
}
