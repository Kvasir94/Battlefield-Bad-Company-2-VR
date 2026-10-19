#pragma once
#include "fvr/interaction/HandInteraction.h"

namespace fvr::bc2 {
enum class ReloadButtonEvent { None, ShortRelease, LongHold };
enum class ReloadInputLane { Unavailable, Native, Physical };
struct ReloadButtonDispatch {bool nativePulse=false,physicalShort=false,physicalAutomatic=false;};
constexpr ReloadButtonDispatch DispatchReloadButton(ReloadInputLane lane,ReloadButtonEvent event)noexcept {
    return {lane==ReloadInputLane::Native&&event==ReloadButtonEvent::LongHold,
        lane==ReloadInputLane::Physical&&event==ReloadButtonEvent::ShortRelease,
        lane==ReloadInputLane::Physical&&event==ReloadButtonEvent::LongHold};
}
// Require a neutral packet after ownership/lane changes. An old held button
// must not become a fresh reload on a newly selected or newly admitted item.
class ReloadInputRouting {
public:
    bool Update(const interaction::HandInteractionOwner& owner,std::uint64_t incarnation,
        ReloadInputLane lane,bool held)noexcept {
        const bool changed=owner!=owner_||incarnation!=incarnation_||lane!=lane_;
        if(changed){owner_=owner;incarnation_=incarnation;lane_=lane;neutral_=false;}
        pressEdge_=held&&!prevHeld_;prevHeld_=held;
        if(!held)neutral_=true;
        return changed;
    }
    bool NativeAllowed()const noexcept{return lane_==ReloadInputLane::Native&&neutral_;}
    // Instant native reload: one pulse on the press edge, only after a neutral
    // packet on this owner/incarnation/lane (a held button never replays).
    bool NativePressEdge()const noexcept{return NativeAllowed()&&pressEdge_;}
    void Reset()noexcept{*this={};}
private:
    interaction::HandInteractionOwner owner_{};
    std::uint64_t incarnation_=0;
    ReloadInputLane lane_=ReloadInputLane::Unavailable;
    bool neutral_=false,prevHeld_=false,pressEdge_=false;
};
// Unique tracked packets measure the hold; native gathers cannot extend it.
class ReloadButtonGesture {
public:
    ReloadButtonEvent Update(const interaction::HandInteractionOwner& owner,
        std::uint64_t sequence,std::int64_t timeNs,bool eligible,bool held)noexcept {
        if(!eligible||!sequence||timeNs<=0||owner!=owner_||timeNs<last_||sequence<sequence_||
            (last_&&timeNs-last_>150000000)){
            Reset();owner_=owner;sequence_=sequence;last_=timeNs;
            return ReloadButtonEvent::None;
        }
        if(sequence==sequence_||timeNs==last_)return ReloadButtonEvent::None;
        sequence_=sequence;last_=timeNs;
        if(!held){
            const bool shortRelease=since_&&!consumed_&&timeNs-since_<600000000;
            const bool longRelease=since_&&!consumed_&&!shortRelease;
            armed_=true;since_=0;consumed_=false;
            return shortRelease?ReloadButtonEvent::ShortRelease:longRelease?ReloadButtonEvent::LongHold:ReloadButtonEvent::None;
        }
        if(!armed_)return ReloadButtonEvent::None;
        if(!since_)since_=timeNs;
        if(!consumed_&&timeNs-since_>=600000000){consumed_=true;return ReloadButtonEvent::LongHold;}
        return ReloadButtonEvent::None;
    }
    void Reset()noexcept {*this={};}
private:
    interaction::HandInteractionOwner owner_{};
    std::uint64_t sequence_=0;
    std::int64_t last_=0,since_=0;
    bool armed_=false,consumed_=false;
};
}
