#pragma once
// Developer input tape for the native gather (docs/REPLAY_INPUT.md). Portable
// core; NativeProbe.cpp supplies QPC and installs Read/Observe as the
// gameplay::InputReader. Neither class touches consumer, firing or ammo state.
#include "fvr/ipc/FrameChannel.h"
#include "fvr/runtime/InputTape.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace fvr::bc2 {
inline constexpr std::int64_t TapePreconditionTimeoutSeconds=20;
// Replay: one tape tick per GatherHook readInput call. Supplies neutral input
// from the first valid recorded pose until the `#!` start conditions match a
// fresh native observation, for at most 20 s. Holding consumes no tape ticks.
class InputTapeGatherSource {
public:
    struct Stats {
        std::uint64_t holdGathers=0,holdFrames=0,ticks=0,available=0,unavailable=0,idleAfterEnd=0;
        bool loaded=false,preconditionMet=false,preconditionTimedOut=false,finished=false;
        std::int64_t holdStartQpc=0,startQpc=0;runtime::TapeLiveState matched{};
    };
    // At Install only, never from a hook. A failed load refuses installation.
    bool Load(std::istream&,std::string& error);
    // live: fresh native weapon/loaded/reserve/capacity, or nullopt while unknown.
    // An empty weaponAsset skips only the weapon condition (verified offline from gun events).
    ipc::ChannelResult Read(interaction::InputFrame&,std::int64_t& deadline,std::int64_t qpc,std::int64_t frequency,
        const std::optional<runtime::TapeLiveState>& live)noexcept;
    bool Holding()const noexcept{return holding_;}
    const Stats& Statistics()const noexcept{return stats_;}
    const runtime::TapeHeader& Header()const noexcept{return tape_.Header();}
    void Report(std::ostream&)const;
private:
    runtime::InputTape tape_;Stats stats_{};bool holding_=true,stopped_=false,noPrecondition_=false,afterEndIdle_=false;
    std::optional<interaction::InputFrame> neutralHold_;
};
// Live recording of exactly what the gather consumed. Observe runs on the game
// thread (inside the reader wrapper, mirroring Bc2Gameplay.cpp GatherHook's
// cache + 100 ms freshness decision) and only copies into an SPSC ring.
// Drain runs on the telemetry thread and run-length encodes into the tape.
class InputTapeGatherRecorder {
public:
    static constexpr unsigned Capacity=4096;
    explicit InputTapeGatherRecorder(unsigned stepUs=11111)noexcept:stepUs_(stepUs){}
    bool Open(std::unique_ptr<std::ostream>,std::vector<std::string> headerLines={});
    // Mirrors: Ok -> cache+deadline; Busy -> keep cache; other -> clear. fresh = qpc < deadline.
    void Observe(ipc::ChannelResult,const interaction::InputFrame& read,std::int64_t deadline,std::int64_t qpc,std::int64_t frequency)noexcept;
    bool Drain();   // telemetry thread
    bool Finish();  // after hooks are disabled
    std::uint64_t Observed()const noexcept{return observed_.load(std::memory_order_relaxed);}
    std::uint64_t Dropped()const noexcept{return dropped_.load(std::memory_order_relaxed);}
    std::uint64_t Written()const noexcept{return writer_.Ticks();}
    bool Failed()const noexcept{return failed_;}
private:
    struct Entry {interaction::InputFrame frame{};std::uint32_t ageMs=0,gapBefore=0;std::uint64_t epoch=0;bool available=false;};
    std::unique_ptr<std::ostream> out_;std::vector<std::string> header_;runtime::InputTapeWriter writer_;unsigned stepUs_;bool begun_=false,failed_=false;
    std::unique_ptr<std::array<Entry,Capacity>> ring_=std::make_unique<std::array<Entry,Capacity>>();
    std::atomic<std::uint32_t> head_{0},tail_{0};std::atomic<std::uint64_t> observed_{0},dropped_{0};
    // Game-thread only.
    interaction::InputFrame cached_{};std::int64_t cachedDeadline_=0;std::uint64_t lastSpace_=0,epoch_=0;std::uint32_t pendingGap_=0;bool haveSpace_=false;
};
}
