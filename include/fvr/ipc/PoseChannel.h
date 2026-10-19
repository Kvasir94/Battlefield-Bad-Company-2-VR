#pragma once
#include "fvr/ipc/FrameProtocol.h"
#include <memory>
#include <string>
namespace fvr::ipc {
// Optional session-token lane carrying the host's newest predicted view sample
// (one per XR frame) for a producer-side late latch. Single writer, lock-free
// sequence-checked reads: the game never waits on it. A sample expires 100 ms
// after publication, so a stalled or exited host can never be latched.
class PoseChannel {
public:
    PoseChannel();~PoseChannel();
    PoseChannel(const PoseChannel&)=delete;PoseChannel& operator=(const PoseChannel&)=delete;
    bool CreateHost(const std::wstring& token)noexcept;
    bool ConnectProducer(const std::wstring& token)noexcept;
    bool Publish(const runtime::PresentationRequirements&,const runtime::TrackingFrame&)noexcept;
    bool Read(runtime::TrackingFrame&)noexcept;
    std::uint64_t Published()const noexcept{return published_;}
    void Close()noexcept;
private:
    struct State;std::unique_ptr<State> state_;std::uint64_t published_=0;
    bool Open(const std::wstring&,bool)noexcept;
};
}
