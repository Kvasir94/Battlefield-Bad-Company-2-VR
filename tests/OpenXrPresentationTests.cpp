#include "Test.h"
#include "fvr/xr/OpenXrHost.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/platform/windows/ProcessLifetime.h"
#include <Windows.h>
#include <wrl/client.h>
#include <vector>
#include <iostream>
#include <limits>
using Microsoft::WRL::ComPtr;
using namespace fvr;
struct Delayed final:runtime::IFrameProvider {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;std::array<ComPtr<ID3D11Texture2D>,2> images;
    graphics::D3D11PairProducer producer;runtime::TrackingFrame pending{};unsigned consumed=0,discarded=0;std::uint64_t epoch=0;
    bool faultInjected=false,invalidPoseInjected=false,injectFaults=true;std::string error;
    graphics::TextureSharing sharing=graphics::TextureSharing::NamedKeyed;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void Suspend()noexcept override{pending={};}
    bool TryGetCompletedPair(const runtime::PresentationRequirements& req,const runtime::TrackingFrame& current,runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept override{
        try {
            const unsigned frame=unsigned((current.predictedNs-1000000000)/11000000);
            if(frame>=40&&frame<=67){pending={};return false;}
            if(!pending.generation||pending.spaceGeneration!=current.spaceGeneration){pending=current;return false;}
            if(frame%4!=0)return false;
            rendered=pending;pending={};
            if(!device){D3D_FEATURE_LEVEL level{};if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return false;
                D3D11_TEXTURE2D_DESC desc{};desc.Width=req.width;desc.Height=req.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT(req.format);
                for(auto& image:images)if(FAILED(device->CreateTexture2D(&desc,nullptr,&image)))return false;
            }
            if(!graphics::Valid(producer.Descriptor()))if(producer.Create(device.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch,sharing)!=graphics::TransferResult::Ok)return false;
            const unsigned source=unsigned((rendered.predictedNs-1000000000)/11000000);
            for(unsigned eye=0;eye<2;++eye){std::vector<std::uint32_t> pixels(std::size_t(req.width)*req.height,0xff5a0000u|((eye+1)<<8)|source);context->UpdateSubresource(images[eye].Get(),0,nullptr,pixels.data(),req.width*4,0);}
            const std::array<graphics::TextureSlice,2> slices{{{images[0].Get(),0},{images[1].Get(),0}}};
            auto published=producer.Publish(slices,{source,rendered.spaceGeneration,rendered.generation,rendered.predictedNs},ticket);
            // This fixture completes producer GPU work before exposing its pair.
            // Production pumps this phase across native frames instead.
            const auto deadline=GetTickCount64()+100;
            while(sharing==graphics::TextureSharing::LegacyFenced&&published==graphics::TransferResult::Busy&&GetTickCount64()<deadline){
                SwitchToThread();published=producer.Publish(slices,{source,rendered.spaceGeneration,rendered.generation,rendered.predictedNs},ticket);
            }
            if(published!=graphics::TransferResult::Ok)return false;
            d=producer.Descriptor();if(injectFaults&&frame==20){ticket.sequence+=100;faultInjected=true;}
            if(injectFaults&&frame==24){rendered.eyes[0].position.x=std::numeric_limits<float>::quiet_NaN();invalidPoseInjected=true;}
            return true;
        }catch(...){error="fixture failed";return false;}
    }
    void PairConsumed(const graphics::PairTicket& ticket,bool okay)noexcept override{if(okay)++consumed;else {++discarded;if(!producer.Discard(ticket))producer.Reset();}producer.Acknowledge(ticket,okay);}
};
struct InputObserver final:runtime::IFrameProvider {
    unsigned samples=0,tracked=0,released=0,invalid=0;bool floor=false;
    bool sawUntracked=false,sawInactive=false,sawBadStick=false,sawValidPose=false;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void PairConsumed(const graphics::PairTicket&,bool)noexcept override{}
    void UpdateInput(const interaction::InputFrame& f)noexcept override {
        ++samples;if(!interaction::ValidInput(f))++invalid;if(f.floorRelative)floor=true;
        const unsigned n=unsigned((f.predictedNs-1000000000)/11000000);
        if(!f.focused){++released;for(const auto& h:f.hands)if(h.active||h.held||h.gripTracked||h.aimTracked)++invalid;return;}
        if(f.worldUnitsPerMeter!=2.5f||f.referenceHead.orientation.x!=0||f.referenceHead.orientation.z!=0)++invalid;
        if(n==8||n==9){sawUntracked=true;if(f.hands[0].gripTracked||f.hands[1].aimTracked)++invalid;}
        if(n==12){sawInactive=true;if(f.hands[1].trigger!=0||(f.hands[1].active&interaction::Trigger))++invalid;}
        if(n==13){sawBadStick=true;if(f.hands[0].stickX!=0||(f.hands[0].active&interaction::Stick))++invalid;}
        if(f.hands[0].gripTracked&&f.hands[1].aimTracked){++tracked;
            if(f.hands[0].grip.position.x!=-.25f||f.hands[1].aim.position.x!=.25f||f.hands[0].grip.position.z!=-.4f||f.hands[1].aim.position.z!=-.6f)++invalid;
            else sawValidPose=true;
        }
    }
};
struct ColdControllerObserver final:runtime::IFrameProvider {
    std::array<unsigned,4> packets{},requests{};
    unsigned invalid=0,focusedSamples=0,pressedSamples=0,released=0;
    std::uint64_t lastGeneration=0;std::int64_t lastTime=0;
    static unsigned Phase(unsigned frame){return frame<20?0u:frame<40?1u:frame<60?2u:3u;}
    static bool Neutral(const interaction::ControllerState& h){return !h.held&&!h.touched&&h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;}
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    bool TryGetCompletedPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame& current,runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override {
        const unsigned frame=unsigned((current.predictedNs-1000000000)/11000000);
        ++requests[Phase(frame)];
        if(!current.focused||!current.headValid||current.predictedNs!=lastTime)++invalid;
        return false; // Observe actual world requests; no game or native renderer.
    }
    void PairConsumed(const graphics::PairTicket&,bool)noexcept override{}
    void UpdateInput(const interaction::InputFrame& f)noexcept override {
        if(!interaction::ValidInput(f)||f.generation<=lastGeneration||f.predictedNs<lastTime)++invalid;
        lastGeneration=f.generation;
        if(!f.focused){++released;for(const auto& h:f.hands)if(h.active||h.held||h.gripTracked||h.aimTracked)++invalid;return;}
        if(!f.headValid||f.predictedNs<=lastTime)++invalid;
        lastTime=f.predictedNs;++focusedSamples;
        const unsigned frame=unsigned((f.predictedNs-1000000000)/11000000),phase=Phase(frame);
        ++packets[phase];
        for(const auto& h:f.hands){
            if(phase==0||phase==2){
                if(h.active||h.touchActive||h.gripTracked||h.aimTracked||!Neutral(h)||h.grip.position.x!=0||h.aim.position.x!=0)++invalid;
            }else{
                if(!h.gripTracked||!h.aimTracked||h.active!=interaction::Components)++invalid;
                if(frame>=30&&frame<40){
                    if(h.trigger!=.8f||h.squeeze!=.8f||h.stickX!=.3f||h.held!=interaction::Secondary)++invalid;
                    ++pressedSamples;
                }else if(!Neutral(h))++invalid; // Wake/recovery must not replay preceding holds.
            }
        }
    }
};
// Late GPU completion: selected pairs read as incomplete past the 20 ms copy
// budget (SteamVR 2026-10-09: 130 such timeouts while EndFrame blocked ~12 ms).
// Picks avoid the fixture's own gating (31-33 unfocused, 40-67 no pairs, 70/75).
namespace late_copy {
std::uint64_t lastSource=0;unsigned seen=0;std::vector<std::uint64_t> picked;
bool Hook(const graphics::PairTicket& ticket){
    if(ticket.frameId!=lastSource){lastSource=ticket.frameId;
        const bool window=ticket.frameId<=16||(ticket.frameId>=80&&ticket.frameId<=92);
        if(window&&seen++%2==0)picked.push_back(ticket.frameId);}
    return !picked.empty()&&picked.back()==ticket.frameId;
}
}
int wmain(int argc,wchar_t** argv){SetEnvironmentVariableW(L"FVR_XR_PREDICT_REQUEST",L"0");SetEnvironmentVariableW(L"FVR_XR_OVERSCAN",L"0");SetEnvironmentVariableW(L"FVR_BC2_LATE_LATCH",L"0"); /* fake runtime verifies exact submitted poses: R4 prediction/overscan/relabel off */ if(argc==2&&std::wstring(argv[1])==L"--lifetime-child"){Sleep(1000);return 0;}CHECK(argc==2);const auto path=std::filesystem::absolute(argv[1]);HMODULE runtime=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(runtime);
    Delayed provider;xr::HostOptions options;options.loaderPath=path;options.probeOnly=false;options.seconds=3;options.provider=&provider;
    const auto report=xr::RunOpenXrHost(options);if(!report.okay)std::cerr<<report.error<<'\n';CHECK(report.okay);
    const auto errors=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrErrors"));
    const auto reuses=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrReuses"));
    const auto layers=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrLayers"));CHECK(errors&&reuses&&layers);
    std::cout<<"frames="<<report.endedFrames<<" fresh="<<report.submittedPairs<<" reused="<<report.reusedFrames<<" blank="<<report.blankFrames<<" rejected="<<report.rejectedPairs<<" errors="<<errors()<<'\n';
    CHECK(errors()==0);CHECK(report.endedFrames==100);CHECK(report.reusedFrames>35);CHECK(reuses()>35);CHECK(report.presentedFrames==layers());
    CHECK(report.presentedFrames==report.submittedPairs+report.reusedFrames);CHECK(report.endedFrames==report.presentedFrames+report.blankFrames);
    CHECK(report.submittedPairs==provider.consumed);CHECK(provider.faultInjected&&provider.invalidPoseInjected&&provider.discarded==2&&report.rejectedPairs==2);
    CHECK(report.pairRejectReasons[0]==1&&report.pairRejectReasons[5]==1); // Invalid pose and wrong keyed-mutex sequence.
    std::uint64_t rejectedTotal=0;for(auto count:report.pairRejectReasons)rejectedTotal+=count;
    CHECK(rejectedTotal==report.rejectedPairs);
    CHECK(report.maxRetainedAgeNs<=250000000&&report.maxRetainedAgeNs>100000000);CHECK(provider.error.empty());
    // Exercise actual LegacyFenced host copies, original-pose stereo readback,
    // and exact producer acknowledgements with async enabled and its kill switch.
    wchar_t savedAsync[64]{};const auto savedLength=GetEnvironmentVariableW(L"FVR_XR_ASYNC_CONSUMER_COPY",savedAsync,64);
    CHECK(savedLength<64);
    for(bool asynchronous:{true,false}){
        CHECK(SetEnvironmentVariableW(L"FVR_XR_ASYNC_CONSUMER_COPY",asynchronous?L"1":L"0"));
        Delayed legacy;legacy.sharing=graphics::TextureSharing::LegacyFenced;legacy.injectFaults=false;
        options.provider=&legacy;const auto r=xr::RunOpenXrHost(options);
        if(!r.okay)std::cerr<<r.error<<'\n';
        CHECK(r.okay&&errors()==0&&legacy.error.empty());CHECK(r.asyncConsumerCopy==asynchronous);
        CHECK(r.submittedPairs>5&&r.reusedFrames>0&&r.rejectedPairs==0&&legacy.discarded==0);
        CHECK(r.submittedPairs==legacy.consumed&&r.maxRetainedAgeNs<=250000000);
        CHECK(r.asyncCopyEvidence[0]==(asynchronous?r.submittedPairs:0));
        CHECK(r.asyncCopyEvidence[1]==r.asyncCopyEvidence[0]);
        CHECK(r.asyncCopyEvidence[2]==0&&r.asyncCopyEvidence[3]==0&&r.asyncCopyEvidence[4]==0);
    }
    // A copy that is only late must not turn into no-world frames. Default keeps
    // the submitted pair retained; FVR_XR_RETAIN_ON_COPY_TIMEOUT=0 reproduces the
    // c2083ba reset. Both still retire the generation with a negative ack.
    CHECK(SetEnvironmentVariableW(L"FVR_XR_ASYNC_CONSUMER_COPY",L"1"));
    wchar_t savedRetain[64]{};const auto savedRetainLength=GetEnvironmentVariableW(L"FVR_XR_RETAIN_ON_COPY_TIMEOUT",savedRetain,64);
    CHECK(savedRetainLength<64);
    for(bool retain:{true,false}){
        CHECK(SetEnvironmentVariableW(L"FVR_XR_RETAIN_ON_COPY_TIMEOUT",retain?nullptr:L"0"));
        late_copy::lastSource=0;late_copy::seen=0;late_copy::picked.clear();graphics::SetCopyLateTestHook(late_copy::Hook);
        Delayed legacy;legacy.sharing=graphics::TextureSharing::LegacyFenced;legacy.injectFaults=false;
        options.provider=&legacy;const auto r=xr::RunOpenXrHost(options);graphics::SetCopyLateTestHook(nullptr);
        if(!r.okay)std::cerr<<r.error<<'\n';
        const auto timeouts=r.asyncCopyEvidence[2];std::uint64_t lateSeen=0,lateDone=0;
        for(unsigned n=0;n<8;++n){lateSeen+=r.lateCopyBuckets[n];if(n<6)lateDone+=r.lateCopyBuckets[n];}
        std::cout<<"late-copy retain="<<retain<<" timeouts="<<timeouts<<" fresh="<<r.submittedPairs<<" reused="<<r.reusedFrames<<" blank="<<r.blankFrames
            <<" after_timeout=["<<r.copyTimeoutPresentation[0]<<','<<r.copyTimeoutPresentation[1]<<','<<r.copyTimeoutPresentation[2]<<"] late_done="<<lateDone<<'\n';
        CHECK(r.okay&&errors()==0&&legacy.error.empty()&&r.asyncConsumerCopy&&r.retainOnCopyTimeout==retain);
        CHECK(timeouts>=2&&timeouts==late_copy::picked.size()&&r.asyncCopyEvidence[3]==0&&r.rejectedPairs==0);
        CHECK(legacy.discarded==timeouts&&legacy.consumed==r.asyncCopyEvidence[1]); // Late copies still retire their generation.
        CHECK(r.asyncCopyEvidence[0]==r.asyncCopyEvidence[1]+timeouts);
        // The old reset can even suppress the timed-out pair's own first frame.
        CHECK(retain?r.asyncCopyEvidence[0]==r.submittedPairs:r.asyncCopyEvidence[0]>r.submittedPairs);
        CHECK(lateSeen==timeouts&&lateDone>=1); // The abandoned GPU events really complete afterwards.
        if(retain)CHECK(r.copyTimeoutPresentation[0]==timeouts&&r.copyTimeoutPresentation[1]>=timeouts&&r.copyTimeoutPresentation[2]==0);
        else CHECK(r.copyTimeoutPresentation[0]==0&&r.copyTimeoutPresentation[1]==0&&r.copyTimeoutPresentation[2]>=timeouts);
        CHECK(r.maxRetainedAgeNs<=250000000);
    }
    CHECK(SetEnvironmentVariableW(L"FVR_XR_RETAIN_ON_COPY_TIMEOUT",savedRetainLength?savedRetain:nullptr));
    std::cout<<"Late async copy keeps the submitted pair presented and retires its generation; kill switch restores reset\n";
    CHECK(SetEnvironmentVariableW(L"FVR_XR_ASYNC_CONSUMER_COPY",savedLength?savedAsync:nullptr));
    // Unlimited mode must have a stop source, and must exit when it signals.
    xr::HostOptions continuous;continuous.loaderPath=path;continuous.probeOnly=false;continuous.seconds=0;
    const auto invalid=xr::RunOpenXrHost(continuous);CHECK(!invalid.okay&&!invalid.instanceCreated);
    unsigned polls=0;continuous.stopRequested=[&]{return ++polls>=10;};
    const auto stopped=xr::RunOpenXrHost(continuous);CHECK(stopped.okay&&stopped.endedFrames>0&&stopped.endedFrames<20);CHECK(errors()==0);
    wchar_t executable[32768]{};CHECK(GetModuleFileNameW(nullptr,executable,32768));
    const auto filename=std::filesystem::path(executable).filename().wstring();
    platform::ProcessLifetime wrong;CHECK(!wrong.Open(GetCurrentProcessId(),L"not-the-process.exe"));
    platform::ProcessLifetime own;CHECK(own.Open(GetCurrentProcessId(),filename.c_str())&&!own.Ended());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    auto command=std::wstring(L"\"")+executable+L"\" --lifetime-child";
    CHECK(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child));
    platform::ProcessLifetime lifetime;CHECK(lifetime.Open(child.dwProcessId,filename.c_str())&&!lifetime.Ended());
    CHECK(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0&&lifetime.Ended());CloseHandle(child.hThread);CloseHandle(child.hProcess);
    std::cout<<"Continuous stop callback and exact-process lifetime checks passed\n";
    const auto stage=reinterpret_cast<void(*)(bool)>(GetProcAddress(runtime,"FakeXrStageAvailable"));CHECK(stage);
    for(bool floor:{true,false}){
        stage(floor);InputObserver input;xr::HostOptions control;control.loaderPath=path;control.probeOnly=false;control.seconds=3;control.provider=&input;
        control.controllers=control.roomscale=true;control.worldUnitsPerMeter=2.5f;
        const auto observed=xr::RunOpenXrHost(control);if(!observed.okay)std::cerr<<observed.error<<'\n';
        CHECK(observed.okay&&observed.controllersEnabled&&observed.controllerProfiles==3&&observed.floorRelative==floor);
        CHECK(input.invalid==0&&input.samples==101&&input.released>=5&&input.tracked>80&&input.floor==floor);
        CHECK(input.sawUntracked&&input.sawInactive&&input.sawBadStick&&input.sawValidPose&&errors()==0);
    }
    std::cout<<"OpenXR controller lifecycle, predicted poses, loss handling, scale and STAGE/LOCAL fallback passed\n";
    const auto scenario=reinterpret_cast<void(*)(unsigned)>(GetProcAddress(runtime,"FakeXrRecoveryScenario"));
    const auto controllerCalls=reinterpret_cast<unsigned(*)(unsigned,unsigned)>(GetProcAddress(runtime,"FakeXrControllerLifecycleCalls"));
    CHECK(scenario&&controllerCalls);scenario(7);stage(true);
    ColdControllerObserver cold;xr::HostOptions coldOptions;coldOptions.loaderPath=path;coldOptions.probeOnly=false;
    coldOptions.seconds=3;coldOptions.controllers=true;coldOptions.provider=&cold;
    coldOptions.stopRequested=[&]{return cold.focusedSamples>=100;};
    const auto coldReport=xr::RunOpenXrHost(coldOptions);if(!coldReport.okay)std::cerr<<coldReport.error<<'\n';
    scenario(0); // Other scenarios retain their existing behavior.
    CHECK(coldReport.okay&&coldReport.controllersEnabled&&errors()==0&&cold.invalid==0);
    std::cout<<"Cold-controller samples="<<cold.focusedSamples<<" released="<<cold.released<<" pressed="<<cold.pressedSamples<<'\n';
    // RequestExitSession is asynchronous: the host completes one final frame
    // before observing the fake runtime's EXITING event.
    CHECK(cold.focusedSamples==101&&cold.released==1&&cold.pressedSamples==20);
    for(unsigned phase=0;phase<4;++phase){
        CHECK(cold.packets[phase]>=19&&cold.requests[phase]==cold.packets[phase]);
        CHECK(controllerCalls(phase,0)==4*cold.packets[phase]); // Both pose actions on both hands.
        CHECK(controllerCalls(phase,1)==(phase==0?0:4*cold.packets[phase])); // Cold poses skipped; lost active poses located.
    }
    CHECK(coldReport.providerMisses==101&&coldReport.validTrackingFrames==101&&coldReport.inputFrames==101);
    const std::array<std::uint64_t,10> expectedSampling{101,0,82,82,82,82,62,62,62,62};
    CHECK(coldReport.controllerSampling==expectedSampling);
    for(unsigned hand=0;hand<2;++hand){
        CHECK(coldReport.controllerProfileResults[hand]==0);
        CHECK(coldReport.controllerCurrentProfiles[hand]=="/interaction_profiles/oculus/touch_controller");
    }
    std::cout<<"Cold controllers, wake, tracking loss and neutral recovery preserve focused head world requests; fake runtime only\n";
    FreeLibrary(runtime);return 0;
}
