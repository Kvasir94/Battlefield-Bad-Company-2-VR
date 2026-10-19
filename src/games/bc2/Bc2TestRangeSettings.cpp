#include "Bc2TestRangeSettings.h"
#include <charconv>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
bool Encode(const RangeNativeField& f,std::string_view text,std::array<std::byte,4>& out){
    out={};
    switch(f.type){
    case RangeFieldType::Boolean:
        if(text=="true")out[0]=std::byte{1};else if(text!="false")return false;
        return true;
    case RangeFieldType::Uint32:{
        std::uint32_t v=0;const auto r=std::from_chars(text.data(),text.data()+text.size(),v);
        if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size()||text.empty())return false;
        std::memcpy(out.data(),&v,4);return true;
    }
    case RangeFieldType::Float32:{
        double d=0;const auto r=std::from_chars(text.data(),text.data()+text.size(),d);
        if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size()||text.empty()||!std::isfinite(d))return false;
        const float v=float(d);std::memcpy(out.data(),&v,4);return true;
    }
    default:return false;
    }
}
}
std::string_view RangeSettingStatusName(RangeSettingStatus s)noexcept{
    switch(s){
    case RangeSettingStatus::Planned:return "planned";
    case RangeSettingStatus::DeferredString:return "deferred_string";
    case RangeSettingStatus::Unmapped:return "unmapped";
    case RangeSettingStatus::Unresolved:return "unresolved";
    case RangeSettingStatus::InvalidValue:return "invalid_value";
    }
    return "unknown";
}
std::vector<RangeSettingWrite> PlanRangeSettingWrites(std::span<const runtime::RangeSetting> settings,const RangeSettingsInstances& live){
    std::vector<RangeSettingWrite> plan;
    for(const auto& s:settings){
        RangeSettingWrite w;w.key=s.key;
        const auto* f=FindRangeSettingField(s.key);
        if(!f){plan.push_back(std::move(w));continue;} // includes every set.* passthrough
        w.typeInfo=f->typeInfo;w.bytes=f->bytes;
        w.object=f->typeInfo==RangeServerSettingsType?live.server:f->typeInfo==RangeClientSettingsType?live.client:
            f->typeInfo==RangeGameSettingsType?live.game:0;
        if(f->type==RangeFieldType::String){w.status=RangeSettingStatus::DeferredString;plan.push_back(std::move(w));continue;}
        if(!w.object||std::uint64_t(w.object)+f->offset+f->bytes>UINT32_MAX){w.status=RangeSettingStatus::Unresolved;plan.push_back(std::move(w));continue;}
        w.address=w.object+f->offset;
        w.status=Encode(*f,s.value,w.value)?RangeSettingStatus::Planned:RangeSettingStatus::InvalidValue;
        plan.push_back(std::move(w));
    }
    return plan;
}
bool RangeSettingPlanWritable(std::span<const RangeSettingWrite> plan)noexcept{
    bool any=false;
    for(const auto& w:plan){
        if(w.status==RangeSettingStatus::Planned)any=true;
        else if(w.status!=RangeSettingStatus::DeferredString)return false;
    }
    return any;
}
bool RangeSettingsWriter::Apply(std::span<const RangeSettingWrite> plan,const RangeMemory& m){
    if(used_||!m.read||!m.write||!m.isType||!RangeSettingPlanWritable(plan))return false;
    used_=true;
    std::vector<Entry> staged;
    for(const auto& w:plan){
        if(w.status==RangeSettingStatus::DeferredString){++deferred_;continue;}
        if(!w.bytes||w.bytes>4||!m.isType(m.context,w.object,w.typeInfo))return false;
        for(const auto& e:staged)if(w.address<e.address+e.bytes&&e.address<w.address+w.bytes)return false;
        Entry e;e.address=w.address;e.bytes=w.bytes;e.written=w.value;
        if(!m.read(m.context,e.address,e.before.data(),e.bytes))return false;
        staged.push_back(e);
    }
    for(const auto& e:staged){
        std::array<std::byte,4> back{};
        if(!m.write(m.context,e.address,e.written.data(),e.bytes)||!m.read(m.context,e.address,back.data(),e.bytes)||
           std::memcmp(back.data(),e.written.data(),e.bytes)){
            applied_.push_back(e);Restore(m);return false; // include the possibly torn entry
        }
        applied_.push_back(e);
    }
    return true;
}
bool RangeSettingsWriter::Restore(const RangeMemory& m){
    bool exact=true;
    for(auto n=applied_.size();n-->0;){
        const auto& e=applied_[n];std::array<std::byte,4> now{};
        if(!m.read||!m.write||!m.read(m.context,e.address,now.data(),e.bytes)){exact=false;++conflicts_;continue;}
        if(!std::memcmp(now.data(),e.before.data(),e.bytes))continue; // already original
        if(std::memcmp(now.data(),e.written.data(),e.bytes)){exact=false;++conflicts_;continue;} // engine changed it since
        if(!m.write(m.context,e.address,e.before.data(),e.bytes)){exact=false;++conflicts_;}
    }
    applied_.clear();return exact;
}
}
