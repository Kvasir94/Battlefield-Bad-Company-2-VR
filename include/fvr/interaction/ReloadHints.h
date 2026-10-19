#pragma once
// Reload tips (docs/BC2_RELOAD_PARITY.md F5). AS2 part glow (AS2-R:107-137)
// and KF2 VRInteractiveReload hints (UpdateHints/UpdateParts): when the
// player stalls on a reload step, the part to act on glows. Each part has
// its own clock: it starts glowing once its step has been pending for
// DelayNs, then pulses up to PeakAlpha with a RiseNs ramp and back.
// Presentation only: no claim, no native state, no gameplay effect.
#include <array>
#include <cstdint>

namespace fvr::interaction {
enum class ReloadHintPart:std::uint8_t {Magazine,Pouch,Magwell,Handle,Pump,Count};
struct ReloadHintInput {
    // Which step is pending right now (several may be; each keeps its clock).
    bool pullMagazine=false;   // empty gun, magazine seated, hand free
    bool takeFromPouch=false;  // well empty, hand free
    bool seatMagazine=false;   // replacement held, not yet seated
    bool rackHandle=false;     // chamber needs a rack
    bool workPump=false;       // a pump is owed
    std::int64_t nowNs=0;
};
struct ReloadHintOutput {
    std::array<float,std::size_t(ReloadHintPart::Count)> alpha{}; // 0..PeakAlpha
    std::uint32_t lit=0;    // bit per part with alpha > 0
    std::uint32_t changed=0;// bit per part whose lit state changed this update
};
class ReloadHints {
public:
    static constexpr std::int64_t DelayNs=750000000,RiseNs=300000000;
    static constexpr float PeakAlpha=.6f;
    ReloadHintOutput Update(const ReloadHintInput& in)noexcept {
        ReloadHintOutput out;
        const std::array<bool,std::size_t(ReloadHintPart::Count)> pending{in.pullMagazine,in.takeFromPouch,in.seatMagazine,in.rackHandle,in.workPump};
        for(std::size_t n=0;n<pending.size();++n){
            if(!pending[n]||in.nowNs<=0){since_[n]=0;}
            else if(!since_[n]||in.nowNs<since_[n])since_[n]=in.nowNs;
            float a=0;
            if(since_[n]){const auto age=in.nowNs-since_[n];
                if(age>=DelayNs){
                    // Triangle pulse: rise to the peak over RiseNs, fall over RiseNs.
                    const auto phase=(age-DelayNs)%(2*RiseNs);
                    a=PeakAlpha*float(double(phase<RiseNs?phase:2*RiseNs-phase)/double(RiseNs));}}
            out.alpha[n]=a;
            const bool lit=a>0||(since_[n]&&in.nowNs-since_[n]>=DelayNs);
            if(lit)out.lit|=1u<<n;
            if(lit!=bool(lastLit_&(1u<<n)))out.changed|=1u<<n;
        }
        lastLit_=out.lit;return out;
    }
    void Reset()noexcept{since_={};lastLit_=0;}
private:
    std::array<std::int64_t,std::size_t(ReloadHintPart::Count)> since_{};
    std::uint32_t lastLit_=0;
};
} // namespace fvr::interaction
