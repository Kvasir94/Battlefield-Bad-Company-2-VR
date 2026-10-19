#include "Bc2InputTapeSource.h"
#include <istream>
#include <locale>
#include <ostream>
#include <sstream>
namespace fvr::bc2 {
bool InputTapeGatherSource::Load(std::istream& in,std::string& error){
    stats_={};holding_=true;stopped_=false;neutralHold_.reset();
    if(!tape_.Load(in,error)){stopped_=true;return false;}
    for(const auto& sample:tape_.Samples()){
        if(sample.ageMs>runtime::TapeStaleMs||!sample.frame.focused||!sample.frame.headValid||
            !interaction::ValidInput(sample.frame))continue;
        neutralHold_=sample.frame;
        for(auto& hand:neutralHold_->hands){
            hand.held=0;hand.trigger=hand.squeeze=hand.stickX=hand.stickY=0;
        }
        break;
    }
    noPrecondition_=tape_.Header().Value("precondition")==std::optional<std::string>("none");
    afterEndIdle_=tape_.Header().Value("after_end")==std::optional<std::string>("idle");
    stats_.loaded=true;return true;
}
ipc::ChannelResult InputTapeGatherSource::Read(interaction::InputFrame& out,std::int64_t& deadline,std::int64_t qpc,std::int64_t frequency,
    const std::optional<runtime::TapeLiveState>& live)noexcept {
    if(stopped_||!stats_.loaded||frequency<=0||qpc<=0){stats_.finished=true;return ipc::ChannelResult::Closed;}
    if(holding_){
        if(!stats_.holdStartQpc)stats_.holdStartQpc=qpc;
        ++stats_.holdGathers;
        // `#! precondition=none` starts without a reserve lease (diagnostic tapes
        // for weapons whose lease is not published yet); nothing is matched.
        if(noPrecondition_){holding_=false;stats_.preconditionMet=true;stats_.startQpc=qpc;}
        else if(live&&runtime::TapePreconditionMet(tape_.Header(),*live)){holding_=false;stats_.preconditionMet=true;stats_.matched=*live;stats_.startQpc=qpc;}
        else{
            if(qpc-stats_.holdStartQpc>TapePreconditionTimeoutSeconds*frequency){stats_.preconditionTimedOut=true;stopped_=true;stats_.finished=true;return ipc::ChannelResult::Closed;}
            // Fresh neutral recorded poses let gameplay publish native state
            // even when the original tape begins with unavailable input.
            if(neutralHold_){
                ++stats_.holdFrames;out=*neutralHold_;deadline=qpc+frequency/10;return ipc::ChannelResult::Ok;
            }
            return ipc::ChannelResult::Timeout;
        }
    }
    const auto tick=tape_.Next();++stats_.ticks;stats_.finished=tick.finished;
    // `#! after_end=idle`: after the last tick keep feeding neutral recorded
    // poses so the owner stays live for timed tests (e.g. an 8 s pump
    // timeout). Default stays the KF2 rule: unavailable forever.
    if(!tick.available&&tick.finished&&afterEndIdle_){
        if(neutralHold_){++stats_.idleAfterEnd;out=*neutralHold_;deadline=qpc+frequency/10;return ipc::ChannelResult::Ok;}
    }
    if(!tick.available){++stats_.unavailable;return ipc::ChannelResult::Timeout;}
    ++stats_.available;out=tick.frame;deadline=qpc+frequency/10; // BC2 channel freshness window
    return ipc::ChannelResult::Ok;
}
void InputTapeGatherSource::Report(std::ostream& o)const{
    o<<"{\"loaded\":"<<(stats_.loaded?"true":"false")<<",\"precondition_met\":"<<(stats_.preconditionMet?"true":"false")
     <<",\"precondition_timed_out\":"<<(stats_.preconditionTimedOut?"true":"false")<<",\"hold_gathers\":"<<stats_.holdGathers<<",\"hold_frames\":"<<stats_.holdFrames
     <<",\"idle_after_end\":"<<stats_.idleAfterEnd<<",\"ticks\":"<<stats_.ticks<<",\"tape_ticks\":"<<tape_.TotalTicks()<<",\"available\":"<<stats_.available<<",\"unavailable\":"<<stats_.unavailable
     <<",\"finished\":"<<(stats_.finished?"true":"false")<<",\"matched\":{\"loaded\":"<<stats_.matched.loaded<<",\"reserve\":"<<stats_.matched.reserve
     <<",\"capacity\":"<<stats_.matched.capacity<<"},\"weapon_checked_natively\":"<<(stats_.matched.weaponAsset.empty()?"false":"true")
     <<",\"step_us\":"<<tape_.Header().stepUs<<",\"seed\":"<<tape_.Header().seed<<'}';
}
bool InputTapeGatherRecorder::Open(std::unique_ptr<std::ostream> out,std::vector<std::string> headerLines){
    if(!out||!*out)return false;out_=std::move(out);header_=std::move(headerLines);begun_=failed_=false;return true;
}
void InputTapeGatherRecorder::Observe(ipc::ChannelResult result,const interaction::InputFrame& read,std::int64_t deadline,std::int64_t qpc,std::int64_t frequency)noexcept {
    observed_.fetch_add(1,std::memory_order_relaxed);
    if(result==ipc::ChannelResult::Ok){cached_=read;cachedDeadline_=deadline;}
    else if(result!=ipc::ChannelResult::Busy){cached_={};cachedDeadline_=0;}
    const bool fresh=qpc>0&&qpc<cachedDeadline_&&frequency>0;
    Entry e;e.available=fresh&&cached_.focused;
    if(fresh){e.frame=cached_;const auto published=cachedDeadline_-frequency/10;e.ageMs=std::uint32_t(qpc>published?(qpc-published)*1000/frequency:0);}
    else{e.ageMs=runtime::TapeStaleMs+1;e.frame.worldUnitsPerMeter=cached_.worldUnitsPerMeter>0?cached_.worldUnitsPerMeter:1;}
    if(e.available){if(!haveSpace_){lastSpace_=e.frame.spaceGeneration;haveSpace_=true;}else if(e.frame.spaceGeneration!=lastSpace_){lastSpace_=e.frame.spaceGeneration;++epoch_;}}
    e.epoch=epoch_;
    const auto tail=tail_.load(std::memory_order_relaxed),head=head_.load(std::memory_order_acquire);
    if(tail-head>=Capacity){++pendingGap_;dropped_.fetch_add(1,std::memory_order_relaxed);return;}
    e.gapBefore=pendingGap_;pendingGap_=0;
    (*ring_)[tail%Capacity]=e;tail_.store(tail+1,std::memory_order_release);
}
bool InputTapeGatherRecorder::Drain(){
    if(!out_||failed_)return false;
    auto head=head_.load(std::memory_order_relaxed);const auto tail=tail_.load(std::memory_order_acquire);
    for(;head!=tail;++head){
        const auto& e=(*ring_)[head%Capacity];
        if(!begun_){
            auto lines=header_;std::ostringstream units;units.imbue(std::locale::classic());units<<"world_units_per_meter="<<e.frame.worldUnitsPerMeter<<" source=dll_gather_record";
            lines.insert(lines.begin(),units.str());
            if(!writer_.Begin(*out_,stepUs_,0,lines)){failed_=true;break;}begun_=true;
        }
        // Ring overflow keeps tick alignment: the lost gathers replay as unavailable.
        for(std::uint32_t gap=0;gap<e.gapBefore;++gap)if(!writer_.Append({},runtime::TapeStaleMs+1,false,e.epoch)){failed_=true;break;}
        if(failed_||!writer_.Append(e.frame,e.ageMs,e.available,e.epoch)){failed_=true;break;}
    }
    head_.store(head,std::memory_order_release);
    return !failed_;
}
bool InputTapeGatherRecorder::Finish(){const bool drained=Drain();return drained&&begun_&&writer_.Finish();}
}
