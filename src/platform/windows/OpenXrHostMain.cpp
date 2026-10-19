#include "fvr/xr/OpenXrHost.h"
#include "Bc2BodyAmmoGeometryCache.h"
#include "fvr/xr/DiagnosticScene.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/ipc/MenuChannel.h"
#include "fvr/ipc/ReloadAudioChannel.h"
#include "fvr/platform/windows/ProcessLifetime.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <fstream>
namespace {
template<class T>std::string Numbers(const T& values){std::string out="[";bool comma=false;for(auto v:values){if(comma)out+=',';out+=std::to_string(v);comma=true;}return out+']';}
std::string Json(const std::string& input){std::string out="\"";const char* hex="0123456789abcdef";for(unsigned char c:input){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
}
int wmain(int argc,wchar_t** argv){
    try {
        wchar_t module[32768]{};const auto length=GetModuleFileNameW(nullptr,module,32768);if(!length||length>=32768)throw std::runtime_error("Cannot locate host executable");
        fvr::platform::ProcessLifetime gameLifetime;unsigned gamePid=0;
        fvr::xr::DiagnosticScene diagnostic;fvr::ipc::RemoteFrameProvider remote;fvr::ipc::MenuChannel menu;fvr::ipc::ReloadAudioChannel reloadAudio;bool testScene=false,ipcMode=false;std::filesystem::path channelFile,stopFile,bodyPropsCache;fvr::xr::HostOptions options;options.loaderPath=std::filesystem::path(module).parent_path()/L"runtime/openxr/win64/openxr_loader.dll";
        options.reloadAudioDirectory=std::filesystem::path(module).parent_path()/L"assets/reload-audio";
        if(!std::filesystem::is_directory(options.reloadAudioDirectory))options.reloadAudioDirectory=std::filesystem::path(module).parent_path()/L"../../assets/reload-audio";
        {wchar_t audioDirectory[32768]{};const auto n=GetEnvironmentVariableW(L"FVR_BC2_RELOAD_AUDIO_DIR",audioDirectory,32768);if(n&&n<32768)options.reloadAudioDirectory=audioDirectory;}
        for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--probe")options.probeOnly=true;else if(arg==L"--session")options.probeOnly=false;else if(arg==L"--test-scene"){options.probeOnly=false;testScene=true;options.provider=&diagnostic;}
            else if(arg==L"--ipc"){options.probeOnly=false;ipcMode=true;options.provider=&remote;}
            else if(arg==L"--controllers")options.controllers=true;
            else if(arg==L"--synthetic-hands")options.syntheticHands=true;
            else if(arg==L"--ignore-presence")options.ignorePresence=true;
            else if(arg==L"--mirror-window")options.mirrorWindow=true;
            else if(arg==L"--roomscale")options.roomscale=true;
            else if(arg==L"--world-units-per-meter"&&i+1<argc)options.worldUnitsPerMeter=std::stof(argv[++i]);
            else if(arg==L"--frame-budget"&&i+1<argc){if(!remote.SetBudget(std::stoul(argv[++i])))throw std::runtime_error("Frame budget must be 1..50 ms");}
            else if(arg==L"--request-lifetime"&&i+1<argc){if(!remote.SetRequestLifetime(std::stoul(argv[++i])))throw std::runtime_error("Request lifetime must be 1..200 ms");}
            else if(arg==L"--eye-width"&&i+1<argc){options.eyeWidth=std::stoul(argv[++i]);if(!options.eyeWidth||options.eyeWidth>16384)throw std::runtime_error("Eye width must be 1..16384");}
            else if(arg==L"--eye-height"&&i+1<argc){options.eyeHeight=std::stoul(argv[++i]);if(!options.eyeHeight||options.eyeHeight>16384)throw std::runtime_error("Eye height must be 1..16384");}
            else if(arg==L"--body-props-cache"&&i+1<argc)bodyPropsCache=std::filesystem::absolute(argv[++i]);
            else if(arg==L"--channel-file"&&i+1<argc)channelFile=std::filesystem::absolute(argv[++i]);
            else if(arg==L"--ready-file"&&i+1<argc)options.readyPath=std::filesystem::absolute(argv[++i]);
            else if(arg==L"--record-tape"&&i+1<argc){options.recordTape=std::filesystem::absolute(argv[++i]);if(std::filesystem::exists(options.recordTape))throw std::runtime_error("Recorded tape requires a new path");}
            else if(arg==L"--stop-file"&&i+1<argc)stopFile=std::filesystem::absolute(argv[++i]);
            else if(arg==L"--until-game-exit"&&i+1<argc){gamePid=std::stoul(argv[++i]);if(!gamePid)throw std::runtime_error("A game PID is required");}
            else if(arg==L"--seconds"&&i+1<argc){options.seconds=std::stoul(argv[++i]);if(!options.seconds||options.seconds>300)throw std::runtime_error("Seconds must be 1..300");}
            else if(arg==L"--loader"&&i+1<argc)options.loaderPath=std::filesystem::absolute(argv[++i]);
            else throw std::runtime_error("Usage: BC2XrHost [--probe | --session | --test-scene | --ipc] [--controllers --roomscale --world-units-per-meter 1] [--seconds 15 | --until-game-exit PID] [--loader path] [--frame-budget 8 --request-lifetime 150] [--eye-width N --eye-height N] [--channel-file path --ready-file path --stop-file path] [--record-tape new-path]");}
        if(gamePid){if(!ipcMode||!gameLifetime.Open(gamePid,L"BFBC2Game.exe"))throw std::runtime_error("Cannot watch the BC2 game lifetime");options.seconds=0;options.stopRequested=[&]{return gameLifetime.Ended();};}
        if(!stopFile.empty()){
            if(!gamePid||std::filesystem::exists(stopFile))throw std::runtime_error("Stop file requires a continuous game session and a new path");
            options.stopRequested=[&]{std::error_code error;return gameLifetime.Ended()||std::filesystem::is_regular_file(stopFile,error);};
        }
        if((options.eyeWidth==0)!=(options.eyeHeight==0))throw std::runtime_error("Set both eye dimensions");
        if(!channelFile.empty()&&(!ipcMode||std::filesystem::exists(channelFile)))throw std::runtime_error("Channel output requires IPC mode and a new file");
        // Pipeline depth 4 (default): up to four requests in flight with one always
        // queued untaken, so every game frame starts with a request and renders
        // stereo. FVR_XR_PIPELINE_DEPTH=1|2|3 selects fewer (2 = the previous
        // default). The game reads the same variable for its producer overlap.
        {wchar_t value[8]{};const auto chars=GetEnvironmentVariableW(L"FVR_XR_PIPELINE_DEPTH",value,8);
            const unsigned depth=chars==1&&value[0]>=L'1'&&value[0]<=L'4'?unsigned(value[0]-L'0'):fvr::ipc::FrameSlotCount;remote.SetPipelineDepth(depth);}
        if(ipcMode){if(testScene||options.probeOnly)throw std::runtime_error("IPC mode cannot be combined with probe/test-scene");
            if(!remote.Create())throw std::runtime_error("Cannot create frame channel");if(!menu.CreateHost(remote.Token()))throw std::runtime_error("Cannot create menu channel");options.menu=&menu;if(reloadAudio.CreateHost(remote.Token()))options.reloadAudio=&reloadAudio;std::wcerr<<L"frame_channel="<<remote.Token()<<L'\n';if(!channelFile.empty()){std::wofstream output(channelFile);output<<remote.Token();output.close();if(!output)throw std::runtime_error("Cannot write channel token");}}
        fvr::bc2::BodyAmmoCacheResult propCache;
        // IPC reload zone cues also work without an installed-asset mesh cache.
        if(ipcMode)options.bodyProps=std::make_shared<fvr::graphics::BodyPropCatalog>();
        if(ipcMode&&!bodyPropsCache.empty()){
            propCache=fvr::bc2::LoadBodyAmmoGeometryCache(bodyPropsCache);
            if(propCache.catalog){auto catalog=std::make_shared<fvr::graphics::BodyPropCatalog>();
                for(const auto& asset:*propCache.catalog)catalog->push_back({asset->key,asset->sections});options.bodyProps=std::move(catalog);}
        }
        const auto report=fvr::xr::RunOpenXrHost(options);
        const auto& delivery=remote.Statistics();
        std::cerr<<"body_props_cache="<<fvr::bc2::BodyAmmoCacheStatusName(propCache.status)<<" parts="<<(propCache.catalog?propCache.catalog->size():0)<<" ready="<<report.bodyPropsReady
            <<" pairs="<<report.bodyProps.pairs<<" instances="<<report.bodyProps.instances<<" expired="<<report.bodyProps.expired<<" missing="<<report.bodyProps.missing<<" invalid="<<report.bodyProps.invalid<<" failures="<<report.bodyProps.failures<<'\n';
        std::cout<<std::boolalpha<<"{\n  \"state\":"<<Json(report.okay?(options.probeOnly?"runtime_ready":"session_observed"):"runtime_unavailable")
            <<",\n  \"runtime\":"<<Json(report.runtimeName)<<",\n  \"system\":"<<Json(report.systemName)<<",\n  \"error\":"<<Json(report.error)
            <<",\n  \"instance_created\":"<<report.instanceCreated<<",\n  \"system_available\":"<<report.systemAvailable<<",\n  \"session_created\":"<<report.sessionCreated
            <<",\n  \"adapter_luid_low\":"<<report.requirements.adapterLow<<",\n  \"adapter_luid_high\":"<<report.requirements.adapterHigh
            <<",\n  \"body_props\":{\"cache\":"<<Json(fvr::bc2::BodyAmmoCacheStatusName(propCache.status))<<",\"ready\":"<<report.bodyPropsReady
            <<",\"frames\":"<<report.bodyProps.frames<<",\"pairs\":"<<report.bodyProps.pairs<<",\"instances\":"<<report.bodyProps.instances
            <<",\"expired\":"<<report.bodyProps.expired<<",\"missing\":"<<report.bodyProps.missing<<",\"invalid\":"<<report.bodyProps.invalid<<",\"failures\":"<<report.bodyProps.failures<<",\"scene_depth\":false}"
            <<",\n  \"reload_audio\":{\"requested\":"<<report.reloadAudioRequested<<",\"ready\":"<<report.reloadAudioReady<<",\"extension\":"<<report.reloadAudioExtension
            <<",\"route\":"<<Json(report.reloadAudioRoute)<<",\"error\":"<<report.reloadAudioError<<",\"played\":"<<report.reloadAudioPlayed<<",\"dropped\":"<<report.reloadAudioDropped<<",\"rejected\":"<<report.reloadAudioRejected<<",\"stops\":"<<report.reloadAudioStops<<'}'
            <<",\n  \"synthetic_input_frames\":"<<report.syntheticInputFrames<<",\n  \"ignore_presence\":"<<options.ignorePresence
            <<",\n  \"mirror_window\":{\"requested\":"<<options.mirrorWindow<<",\"created\":"<<report.mirror.created<<",\"presented\":"<<report.mirror.presented<<",\"busy\":"<<report.mirror.busy<<",\"skipped\":"<<report.mirror.skipped<<",\"failures\":"<<report.mirror.failures<<",\"closed\":"<<report.mirror.closed<<"}"
            <<",\n  \"minimum_feature_level\":"<<report.minimumFeatureLevel<<",\n  \"eye_width\":"<<report.requirements.width<<",\n  \"eye_height\":"<<report.requirements.height
            <<",\n  \"format\":"<<report.requirements.format<<",\n  \"waited_frames\":"<<report.waitedFrames<<",\n  \"ended_frames\":"<<report.endedFrames
            <<",\n  \"valid_tracking_frames\":"<<report.validTrackingFrames<<",\n  \"submitted_pairs\":"<<report.submittedPairs<<",\n  \"rejected_pairs\":"<<report.rejectedPairs
            <<",\n  \"presented_frames\":"<<report.presentedFrames<<",\n  \"reused_frames\":"<<report.reusedFrames<<",\n  \"blank_frames\":"<<report.blankFrames
            <<",\n  \"menu_frames\":"<<report.menuFrames<<",\n  \"menu_errors\":"<<report.menuErrors
            <<",\n  \"provider_misses\":"<<report.providerMisses<<",\n  \"max_provider_wait_us\":"<<report.maxProviderWaitUs<<",\n  \"max_retained_age_ns\":"<<report.maxRetainedAgeNs
            <<",\n  \"stage_total_us\":"<<Numbers(report.stageTotalUs)<<",\n  \"stage_max_us\":"<<Numbers(report.stageMaxUs)
            <<",\n  \"frame_gap_buckets\":"<<Numbers(report.frameGapBuckets)<<",\n  \"fresh_age_buckets\":"<<Numbers(report.freshAgeBuckets)<<",\n  \"gated_frames\":"<<Numbers(report.gatedFrames)
            <<",\n  \"async_request_lifetime_ms\":"<<remote.RequestLifetimeMs()<<",\n  \"async_requests\":"<<delivery.requested<<",\n  \"async_timeouts\":"<<delivery.timeouts
            <<",\n  \"async_completed\":"<<delivery.completed<<",\n  \"async_mean_latency_us\":"<<(delivery.completed?delivery.totalLatencyUs/delivery.completed:0)
            <<",\n  \"async_max_latency_us\":"<<delivery.maxLatencyUs<<",\n  \"async_completed_after_50ms\":"<<delivery.completedAfter50Ms
            <<",\n  \"render_views\":{\"request_prediction\":"<<report.requestPrediction<<",\"overscan\":"<<report.overscan<<",\"late_pose_lane\":"<<report.latePose
            <<",\"predicted\":"<<report.renderViews.predicted<<",\"prediction_fallbacks\":"<<report.renderViews.predictionFallbacks<<",\"overscanned\":"<<report.renderViews.overscanned
            <<",\"poses_published\":"<<report.renderViews.posesPublished<<",\"lead_ns\":"<<report.renderViews.leadNs
            <<",\"relatched_pairs\":"<<delivery.relatched<<",\"relatch_rejected\":"<<delivery.relatchRejected<<'}'
            <<",\n  \"display_refresh\":{\"extension\":"<<report.refreshExtension<<",\"target_hz\":"<<report.refreshTargetHz<<",\"initial_hz\":"<<report.refreshInitialHz
            <<",\"requested_hz\":"<<report.refreshRequestedHz<<",\"current_hz\":"<<report.refreshCurrentHz<<",\"result\":"<<report.refreshResult<<",\"rates\":"<<Json(report.refreshRates)<<'}'
            <<",\n  \"pace_to_content\":{\"enabled\":"<<report.paceToContent<<",\"max_wait_ms\":"<<report.paceMaxWaitMs<<",\"waits\":"<<report.paceWaits
            <<",\"satisfied\":"<<report.paceSatisfied<<",\"timeouts\":"<<report.paceTimeouts<<",\"wait_total_us\":"<<report.paceWaitTotalUs<<",\"wait_max_us\":"<<report.paceWaitMaxUs<<'}'
            <<",\n  \"pipeline\":{\"depth\":"<<remote.PipelineDepth()<<",\"overlapped_requests\":"<<delivery.overlapped<<",\"queued_while_delivered\":"<<delivery.queuedWhileDelivered<<'}'
            <<",\n  \"depth_layers\":{\"extension\":"<<report.depthExtension<<",\"requested\":"<<report.depthRequested<<",\"active\":"<<report.depthLayers
            <<",\"setup_failure\":"<<report.depthSetupFailure<<",\"pairs\":"<<report.depthPairs<<",\"missing\":"<<report.depthMissing<<",\"rejected\":"<<report.depthRejected
            <<",\"copy_failures\":"<<report.depthCopyFailures<<",\"upload_failures\":"<<report.depthUploadFailures<<",\"frames\":"<<report.depthFrames<<'}'
            <<",\n  \"controllers_enabled\":"<<report.controllersEnabled<<",\n  \"controller_profiles\":"<<report.controllerProfiles
            <<",\n  \"controller_sampling\":"<<Numbers(report.controllerSampling)
            <<",\n  \"pair_reject_reasons\":"<<Numbers(report.pairRejectReasons)<<",\n  \"last_pair_transfer_error\":"<<report.lastPairTransferError
            <<",\n  \"async_consumer_copy\":"<<report.asyncConsumerCopy<<",\n  \"async_copy\":"<<Numbers(report.asyncCopyEvidence)
            <<",\n  \"retain_on_copy_timeout\":"<<report.retainOnCopyTimeout<<",\n  \"copy_timeout_presentation\":"<<Numbers(report.copyTimeoutPresentation)
            <<",\n  \"copy_latency_buckets\":"<<Numbers(report.copyLatencyBuckets)<<",\n  \"late_copy_buckets\":"<<Numbers(report.lateCopyBuckets)
            <<",\n  \"controller_profile_results\":"<<Numbers(report.controllerProfileResults)
            <<",\n  \"controller_current_profiles\":["<<Json(report.controllerCurrentProfiles[0])<<","<<Json(report.controllerCurrentProfiles[1])<<"]"
            <<",\n  \"feedback_applied\":"<<report.feedbackApplied<<",\n  \"feedback_rejected\":"<<report.feedbackRejected<<",\n  \"feedback_errors\":"<<report.feedbackErrors
            <<",\n  \"capture_feedback_applied\":"<<report.captureFeedbackApplied<<",\n  \"receipt_feedback_applied\":"<<report.receiptFeedbackApplied<<",\n  \"seated_feedback_applied\":"<<report.seatedFeedbackApplied<<",\n  \"last_feedback_event\":"<<report.lastFeedbackEvent
            <<",\n  \"recorded_tape_ticks\":"<<report.recordedTicks<<",\n  \"recorded_tape_failed\":"<<report.recordFailed<<",\n  \"input_frames\":"<<report.inputFrames<<",\n  \"tracked_hand_frames\":"<<report.trackedHandFrames<<",\n  \"recenters\":"<<report.recenters<<",\n  \"automatic_recenters\":"<<report.automaticRecenters<<",\n  \"user_presence_supported\":"<<report.userPresenceSupported<<",\n  \"user_presence_events\":"<<report.userPresenceEvents
            <<",\n  \"recenter_input\":{\"samples\":"<<report.recenterInput.samples<<",\"buttons_active\":"<<report.recenterInput.buttonsActive
            <<",\"left_held\":"<<report.recenterInput.leftHeld<<",\"right_held\":"<<report.recenterInput.rightHeld<<",\"both_held\":"<<report.recenterInput.bothHeld
            <<",\"both_held_without_hand_pose\":"<<report.recenterInput.bothHeldWithoutHandPose<<",\"starts\":"<<report.recenterInput.starts
            <<",\"completed\":"<<report.recenterInput.completed<<",\"interruptions\":"<<report.recenterInput.interruptions<<",\"max_hold_ns\":"<<report.recenterInput.maxHoldNs<<'}'
            <<",\n  \"roomscale_reference\":"<<options.roomscale<<",\n  \"floor_relative\":"<<report.floorRelative<<",\n  \"world_units_per_meter\":"<<options.worldUnitsPerMeter
            <<",\n  \"last_session_state\":"<<report.lastSessionState<<",\n  \"diagnostic_scene\":"<<testScene<<",\n  \"diagnostic_error\":"<<Json(diagnostic.Error())<<",\n  \"ipc_mode\":"<<ipcMode<<",\n  \"ipc_pairs_received\":"<<(ipcMode&&report.submittedPairs>0)<<",\n  \"headset_visuals_verified\":false\n}\n";
        return report.okay?0:2;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
