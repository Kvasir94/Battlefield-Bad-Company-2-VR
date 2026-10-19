#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
namespace fvr::ipc {
enum class ReloadAudioRole : std::uint32_t {MagazineOut,MagazineIn,HandleBack,HandleForward,ShellIn,PumpBack,PumpForward,Count};
struct alignas(8) ReloadAudioEvent {
    std::uint64_t id=0,inputSequence=0,ownerRevision=0,space=0;
    std::int64_t observedNs=0,deadlineNs=0;
    ReloadAudioRole role{};std::uint32_t reserved=0;
    float emitterX=0,emitterY=0,emitterZ=0,gain=1;
};
struct alignas(8) ReloadAudioOwner {std::uint64_t revision=0,space=0;std::int64_t observedNs=0;};
static_assert(sizeof(ReloadAudioEvent)==72&&sizeof(ReloadAudioOwner)==24&&std::is_trivially_copyable_v<ReloadAudioEvent>);
inline bool ValidReloadAudio(const ReloadAudioEvent& e,std::int64_t now)noexcept {
    return e.id&&e.inputSequence&&e.ownerRevision&&e.space&&!e.reserved&&unsigned(e.role)<unsigned(ReloadAudioRole::Count)
        &&e.observedNs>0&&e.observedNs<=now&&e.deadlineNs>now&&e.deadlineNs-e.observedNs<=100000000
        &&std::isfinite(e.emitterX)&&std::isfinite(e.emitterY)&&std::isfinite(e.emitterZ)
        &&std::abs(e.emitterX)<10000&&std::abs(e.emitterY)<10000&&std::abs(e.emitterZ)<10000
        &&std::isfinite(e.gain)&&e.gain>=0&&e.gain<=1;
}
inline bool ValidReloadAudioOwner(const ReloadAudioOwner& o,std::int64_t now)noexcept {
    return o.revision&&o.space&&o.observedNs>0&&o.observedNs<=now&&now-o.observedNs<=150000000;
}
class ReloadAudioOwnerSnapshot {
public:
    ReloadAudioOwner Observe(bool available,bool busy,const ReloadAudioOwner& owner)noexcept {
        if(available)owner_=owner;else if(!busy)owner_={};
        return owner_; // Contention preserves the ORIGINAL observation timestamp.
    }
    const ReloadAudioOwner& Current()const noexcept{return owner_;}
private:ReloadAudioOwner owner_{};
};
class ReloadAudioPolicy {
public:
    // True means stop all current voices before accepting more events.
    bool Update(const ReloadAudioOwner& owner,std::uint64_t space,std::uint64_t input,std::int64_t now,bool active)noexcept {
        const bool safe=active&&ValidReloadAudioOwner(owner,now)&&owner.space==space&&input>=input_&&now>=now_;
        const bool changed=owner.revision!=owner_||space!=space_;
        if(!safe)barrier_=input>barrier_?input:barrier_;
        if(changed)sequences_={};
        owner_=owner.revision;space_=space;input_=input;now_=now;active_=safe;
        return !safe||changed;
    }
    bool Accept(const ReloadAudioEvent& e)noexcept {
        if(!e.id||e.id<=seen_)return false;seen_=e.id;
        if(!active_||!ValidReloadAudio(e,now_)||e.ownerRevision!=owner_||e.space!=space_
            ||e.inputSequence<=barrier_||e.inputSequence>input_)return false;
        auto& last=sequences_[unsigned(e.role)];if(e.inputSequence<=last)return false;last=e.inputSequence;return true;
    }
private:
    std::uint64_t owner_=0,space_=0,input_=0,seen_=0,barrier_=0;
    std::int64_t now_=0;bool active_=false;
    std::array<std::uint64_t,unsigned(ReloadAudioRole::Count)> sequences_{};
};
}
