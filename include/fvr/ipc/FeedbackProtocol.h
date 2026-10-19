#pragma once
#include "fvr/interaction/Feedback.h"
#include <type_traits>
namespace fvr::ipc {
struct alignas(8) FeedbackPacket {
    std::uint32_t magic=0x31484646,version=1,bytes=64,reserved=0;
    std::uint64_t event=0,input=0,space=0;
    std::int64_t observedNs=0,deadlineNs=0;
    std::uint32_t kind=0,hand=0;
};
static_assert(sizeof(FeedbackPacket)==64&&offsetof(FeedbackPacket,event)==16&&offsetof(FeedbackPacket,kind)==56);
static_assert(std::is_trivially_copyable_v<FeedbackPacket>);
// Optional GunFire payload in the reserved word. Existing packets remain zero;
// older hosts reject weighted pulses rather than misreading their strength.
inline constexpr std::uint32_t RecoilPresent=1u<<16,RecoilSupport=1u<<17;
inline bool EncodeFeedback(const interaction::FeedbackEvent& e,std::int64_t now,FeedbackPacket& p)noexcept {
    p={};if(!interaction::ValidFeedback(e,now))return false;
    p.event=e.id;p.input=e.inputSequence;p.space=e.space;p.observedNs=e.observedNs;p.deadlineNs=e.deadlineNs;
    p.kind=std::uint32_t(e.kind);p.hand=e.hand;
    if(e.recoilWeight>=0.f)p.reserved=RecoilPresent|(e.recoilSupport?RecoilSupport:0u)|
        std::uint32_t(std::lround(double(e.recoilWeight)*65535.));
    return true;
}
inline bool DecodeFeedback(const FeedbackPacket& p,std::int64_t now,interaction::FeedbackEvent& e)noexcept {
    e={};if(p.magic!=0x31484646||p.version!=1||p.bytes!=64)return false;
    interaction::FeedbackEvent value{p.event,p.input,p.space,p.observedNs,p.deadlineNs,interaction::FeedbackKind(p.kind),p.hand};
    if(p.reserved){
        if(p.kind!=std::uint32_t(interaction::FeedbackKind::GunFire)||!(p.reserved&RecoilPresent)||
           (p.reserved&~(65535u|RecoilPresent|RecoilSupport)))return false;
        value.recoilWeight=float(p.reserved&65535u)/65535.f;value.recoilSupport=bool(p.reserved&RecoilSupport);
    }
    if(!interaction::ValidFeedback(value,now))return false;e=value;return true;
}
}
