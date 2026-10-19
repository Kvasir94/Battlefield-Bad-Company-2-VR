#pragma once
#include "fvr/ipc/ReloadAudioProtocol.h"
#include "fvr/math/StereoMath.h"
#include <filesystem>
#include <memory>
#include <string>
namespace fvr::xr {
struct ReloadAudioMixStats {std::uint64_t played=0,dropped=0,stops=0;std::int32_t error=0;};
class ReloadAudioMixer {
public:
    ReloadAudioMixer();~ReloadAudioMixer();
    bool Initialize(const std::filesystem::path& cues,const std::wstring& endpoint)noexcept;
    bool Play(const ipc::ReloadAudioEvent&,const math::Pose& head)noexcept;
    // Existing weapon-attached cues follow the current tracked emitter without
    // replaying or extending their PCM or owner lease.
    void UpdateHead(const math::Pose&,const math::Vec3* currentEmitter=nullptr)noexcept;
    void Stop()noexcept;
    const ReloadAudioMixStats& Statistics()const noexcept{return stats_;}
private:
    struct State;std::unique_ptr<State> state_;ReloadAudioMixStats stats_;
};
}
