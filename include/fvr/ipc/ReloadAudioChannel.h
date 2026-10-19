#pragma once
#include "ReloadAudioProtocol.h"
#include <memory>
#include <string>
namespace fvr::ipc {
class ReloadAudioChannel {
public:
    ReloadAudioChannel();~ReloadAudioChannel();
    ReloadAudioChannel(const ReloadAudioChannel&)=delete;ReloadAudioChannel& operator=(const ReloadAudioChannel&)=delete;
    bool CreateHost(const std::wstring& token)noexcept;
    bool ConnectProducer(const std::wstring& token)noexcept;
    bool PublishOwner(std::uint64_t revision,std::uint64_t space,std::int64_t observedNs)noexcept;
    bool Publish(const ReloadAudioEvent&)noexcept;
    // Host acknowledges its live mixer for exactly the observed owner lease.
    bool AcknowledgeReady(const ReloadAudioOwner&,bool ready)noexcept;
    bool Ready(std::uint64_t revision,std::uint64_t space,std::int64_t* deadlineNs=nullptr)noexcept;
    // Atomic owner snapshot + one event; id=0 means queue empty. False means
    // unavailable/disconnected. A true busy output permits retaining only the
    // last successful ORIGINAL lease; it never renews its observation time.
    bool Poll(ReloadAudioOwner&,ReloadAudioEvent&,bool* busy=nullptr)noexcept;
    void Close()noexcept;
private:
    bool Open(const std::wstring&,bool)noexcept;
    struct State;std::unique_ptr<State> state_;
};
}
