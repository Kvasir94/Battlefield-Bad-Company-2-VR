#pragma once
// Reload sound on the physical event (docs/BC2_RELOAD_PARITY.md F3). BC2
// maps each animation to one SoundAsset, which may contain a layered graph
// rather than one wave (docs/re/BC2_DISASM_WEAPON_SOUNDS.md section 4 and
// docs/BC2_RELOAD_FEEDBACK_MATRIX.md). During a physical reload the clip
// starts when the native reload begins, so its magazine-out/in sounds play on
// the stock clock while the hand is elsewhere.
//
// This gate learns the clip that starts right after a physical reload begin,
// withholds that one start, and lets the adapter replay it on the animation
// thread when the hand actually pulls the magazine (unseat), then stops it
// after a bounded time so the map's per-owner "already active" record never
// blocks the next reload. A cancel drops pending work and stops a replay on
// its owning listener's next dispatch. Everything runs through the
// game's own start/stop functions with their original arguments.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
namespace fvr::bc2 {
using AnimGuid=std::array<std::uint8_t,16>;
struct ReloadSoundCall {void* listener=nullptr;AnimGuid guid{};int callback=0;std::uint32_t soldier=0;};
struct ReloadSoundStats {std::uint64_t learned=0,withheld=0,replayed=0,stopped=0,dropped=0,expired=0,passedEarly=0;};
// The learn window after Begin, the replay timeout and the stop delay. The
// reload clip starts with the native reload; the SPAS pump clip starts about
// 0.35 s after the shot (runs 092813/093006: anim 1bf49c2d), after the fire
// clip, and the pump may wait up to the 8 s hold timeout.
struct ReloadSoundTiming {std::int64_t learnFromNs=0,learnUntilNs=500000000,replayTimeoutNs=5000000000ll,stopAfterNs=4000000000ll;};
class ReloadSoundGate {
public:
    static constexpr std::int64_t LearnWindowNs=500000000,ReplayTimeoutNs=5000000000ll,StopAfterNs=4000000000ll;
    ReloadSoundGate()noexcept=default;
    explicit ReloadSoundGate(ReloadSoundTiming t)noexcept:t_(t){}
    // Gameplay thread: identity comes from the admitted player's native pair.
    // On ownership change never call through a retained listener pointer. The
    // game owns cleanup of the old entity's sounds; a new pair learns afresh.
    void Weapon(std::uint32_t id,std::uint32_t soldier)noexcept{std::lock_guard l(m_);
        if(id==weapon_&&soldier==soldier_)return;
        if(pending_)++stats_.dropped;
        weapon_=id;soldier_=soldier;known_=pending_=playing_=stopDue_=early_=false;
        armedNs_=dueNs_=pendingNs_=playingNs_=0;listener_=nullptr;pendingCall_={};playingCall_={};}
    void Begin(std::int64_t now)noexcept{std::lock_guard l(m_);
        if(pending_){pending_=false;++stats_.dropped;}dueNs_=0;
        armedNs_=weapon_&&soldier_&&now>0?now:0;early_=false;}
    // The physical event. Before the clip has even started (an early pump
    // stroke) the clip is let through natively when it comes.
    void Trigger(std::int64_t now)noexcept{std::lock_guard l(m_);if(pending_)dueNs_=now;else if(armedNs_)early_=true;}
    void Cancel()noexcept{std::lock_guard l(m_);if(pending_){pending_=false;++stats_.dropped;}
        stopDue_=playing_;armedNs_=0;dueNs_=0;early_=false;}
    // Animation thread, from the clip-start hook. true: call the original now.
    bool OnStart(const ReloadSoundCall& call,bool played,std::int64_t now)noexcept {
        std::lock_guard l(m_);
        if(!played)return true;
        // A played callback can belong to another soldier, or the other
        // animation view. Neither may teach or consume this player's gate.
        if(!weapon_||!soldier_||call.soldier!=soldier_||!call.listener||
           (known_&&call.listener!=listener_))return true;
        const bool armed=armedNs_&&now>=armedNs_+t_.learnFromNs&&now-armedNs_<=t_.learnUntilNs;
        if(armed&&!known_){known_=true;guid_=call.guid;listener_=call.listener;++stats_.learned;}
        if(armed&&known_&&call.guid==guid_&&early_){armedNs_=0;early_=false;++stats_.passedEarly;return true;}
        if(armed&&known_&&call.guid==guid_&&!pending_){pending_=true;pendingCall_=call;pendingNs_=now;armedNs_=0;dueNs_=0;++stats_.withheld;return false;}
        return true;
    }
    // Animation thread, every clip start/stop callback: what to run now.
    struct Work {bool replay=false,stop=false;ReloadSoundCall call{},stopCall{};};
    Work Poll(std::int64_t now,void* listener,std::uint32_t soldier)noexcept {
        std::lock_guard l(m_);Work w;
        // The caller has freshly read listener+4 and proved dispatch ownership.
        // Other callbacks cannot drain, expire or stop this listener's work.
        if(!listener||listener!=listener_||!soldier_||soldier!=soldier_)return w;
        if(playing_&&stopDue_){w.stop=true;w.stopCall=playingCall_;playing_=stopDue_=false;++stats_.stopped;}
        if(pending_&&(dueNs_||now-pendingNs_>t_.replayTimeoutNs)){
            if(!dueNs_){++stats_.expired;pending_=false;return w;} // never pulled: stay silent
            // A previous replay still playing (a second pump within 4 s) is
            // stopped first, so the map's per-owner record is released.
            if(playing_){w.stop=true;w.stopCall=playingCall_;++stats_.stopped;}
            w.replay=true;w.call=pendingCall_;pending_=false;playing_=true;playingCall_=pendingCall_;playingNs_=now;++stats_.replayed;return w;}
        if(playing_&&now-playingNs_>t_.stopAfterNs){w.stop=true;w.stopCall=playingCall_;playing_=false;++stats_.stopped;}
        return w;
    }
    // Listener dispatch, every call: the listener a replay or stop is owed to,
    // so the dispatch hook runs Poll only for the ant that owns it.
    void* WorkListener()const noexcept{std::lock_guard l(m_);return pending_?pendingCall_.listener:playing_?playingCall_.listener:nullptr;}
    ReloadSoundStats Stats()const noexcept{std::lock_guard l(m_);return stats_;}
    bool Known()const noexcept{std::lock_guard l(m_);return known_;}
private:
    ReloadSoundTiming t_{};
    mutable std::mutex m_;
    std::int64_t armedNs_=0,dueNs_=0,pendingNs_=0,playingNs_=0;
    bool known_=false,pending_=false,playing_=false,stopDue_=false,early_=false;
    AnimGuid guid_{};std::uint32_t weapon_=0,soldier_=0;void* listener_=nullptr;
    ReloadSoundCall pendingCall_{},playingCall_{};ReloadSoundStats stats_{};
};
}
