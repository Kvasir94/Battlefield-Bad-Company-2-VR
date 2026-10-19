#include "fvr/runtime/InputTape.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <istream>
#include <locale>
#include <ostream>
#include <sstream>
namespace fvr::runtime {
namespace {
using interaction::InputFrame;using interaction::ControllerState;
bool End(std::istream& s){s>>std::ws;return s.eof();}
bool ReadPose(std::istream& s,math::Pose& p){
    auto& a=p.position;auto& q=p.orientation;
    if(!(s>>a.x>>a.y>>a.z>>q.x>>q.y>>q.z>>q.w))return false;
    for(float v:{a.x,a.y,a.z,q.x,q.y,q.z,q.w})if(!std::isfinite(v))return false;
    return std::abs(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w-1.f)<.02f;
}
bool ReadHand(std::istream& s,ControllerState& h){
    unsigned flags=0,buttons=0;
    if(!(s>>flags>>buttons>>h.trigger>>h.squeeze>>h.stickX>>h.stickY)||flags>TapeHandFlags||buttons>TapeHandButtons)return false;
    if(!ReadPose(s,h.grip)||!ReadPose(s,h.aim))return false;
    h.gripTracked=(flags&1)!=0;h.aimTracked=(flags&2)!=0;h.active=(flags>>2)&interaction::Components;h.touchActive=(flags>>9)&interaction::TouchComponents;
    h.held=buttons&127;h.touched=(buttons>>7)&interaction::TouchComponents;
    // Placeholder poses for untracked hands are kept numeric, never interpreted.
    if(!h.gripTracked)h.grip={};if(!h.aimTracked)h.aim={};
    return true; // Ranges, held/active and touch subsets: interaction::ValidInput.
}
// Shortest text that reads back as the same float (classic locale).
void Number(std::string& out,float v){char buffer[32];const auto r=std::to_chars(buffer,buffer+sizeof(buffer),v);out.append(buffer,r.ptr);out+=' ';}
void Unsigned(std::string& out,std::uint64_t v){out+=std::to_string(v);out+=' ';}
void WritePose(std::string& out,const math::Pose& p,bool valid){const auto q=valid?p:math::Pose{};
    for(float v:{q.position.x,q.position.y,q.position.z,q.orientation.x,q.orientation.y,q.orientation.z,q.orientation.w})Number(out,v);}
void WriteHand(std::string& out,const ControllerState& h){
    Unsigned(out,(h.gripTracked?1u:0u)|(h.aimTracked?2u:0u)|((h.active&interaction::Components)<<2)|((h.touchActive&interaction::TouchComponents)<<9));
    Unsigned(out,(h.held&127u)|((h.touched&interaction::TouchComponents)<<7));
    for(float v:{h.trigger,h.squeeze,h.stickX,h.stickY})Number(out,v);
    WritePose(out,h.grip,h.gripTracked);WritePose(out,h.aim,h.aimTracked);
}
std::optional<TapeCondition> Condition(const std::string& token){
    static const std::pair<const char*,TapeOp> ops[]={{"<=",TapeOp::Le},{">=",TapeOp::Ge},{"!=",TapeOp::Ne},{"=",TapeOp::Eq},{"<",TapeOp::Lt},{">",TapeOp::Gt}};
    std::size_t best=std::string::npos;TapeOp op=TapeOp::Eq;std::size_t length=0;
    for(const auto& [text,value]:ops){const auto at=token.find(text);if(at!=std::string::npos&&(at<best||(at==best&&std::char_traits<char>::length(text)>length))){best=at;op=value;length=std::char_traits<char>::length(text);}}
    if(best==std::string::npos)return TapeCondition{token,TapeOp::Eq,"true"};
    if(!best||best+length>=token.size())return std::nullopt;
    return TapeCondition{token.substr(0,best),op,token.substr(best+length)};
}
std::string Lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
bool Compare(long long a,TapeOp op,long long b){
    switch(op){case TapeOp::Eq:return a==b;case TapeOp::Ne:return a!=b;case TapeOp::Lt:return a<b;case TapeOp::Le:return a<=b;case TapeOp::Gt:return a>b;case TapeOp::Ge:return a>=b;}
    return false;
}
math::Quaternion Multiply(const math::Quaternion& a,const math::Quaternion& b){
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
math::Vec3 Rotate(const math::Quaternion& q,const math::Vec3& v){
    const math::Quaternion p{v.x,v.y,v.z,0},c{-q.x,-q.y,-q.z,q.w};const auto r=Multiply(Multiply(q,p),c);return {r.x,r.y,r.z};
}
}
math::Pose ComposePose(const math::Pose& parent,const math::Pose& child)noexcept {
    const auto offset=Rotate(parent.orientation,child.position);
    return {{parent.position.x+offset.x,parent.position.y+offset.y,parent.position.z+offset.z},Multiply(parent.orientation,child.orientation)};
}
math::Pose InversePose(const math::Pose& p)noexcept {
    const math::Quaternion c{-p.orientation.x,-p.orientation.y,-p.orientation.z,p.orientation.w};
    const auto t=Rotate(c,p.position);return {{-t.x,-t.y,-t.z},c};
}
std::optional<std::string> TapeHeader::Value(const std::string& key)const{
    for(auto it=conditions.rbegin();it!=conditions.rend();++it)if(it->key==key&&it->op==TapeOp::Eq)return it->value;
    return std::nullopt;
}
bool TapePreconditionMet(const TapeHeader& header,const TapeLiveState& live)noexcept {
    try{
        for(const auto& c:header.conditions){
            if(c.key=="weapon"){
                if(live.weaponAsset.empty())continue; // unknown to this observer; checked from gun events
                const auto want=Lower(c.value),have=Lower(live.weaponAsset);
                const bool match=!want.empty()&&(have==want||have.rfind(want,0)==0);
                if(c.op==TapeOp::Eq?!match:c.op==TapeOp::Ne?match:true)return false;
                continue;
            }
            const long long* field=nullptr;long long loaded=live.loaded,reserve=live.reserve,capacity=live.capacity;
            if(c.key=="loaded")field=&loaded;else if(c.key=="reserve")field=&reserve;else if(c.key=="capacity")field=&capacity;
            if(!field)continue; // checkpoint/anchor/source/notes: provenance only
            long long rhs=0;
            if(c.value=="capacity")rhs=capacity;else if(c.value=="loaded")rhs=loaded;else if(c.value=="reserve")rhs=reserve;
            else{const auto* first=c.value.data();const auto r=std::from_chars(first,first+c.value.size(),rhs);if(r.ec!=std::errc{}||r.ptr!=first+c.value.size())return false;}
            if(!Compare(*field,c.op,rhs))return false;
        }
        return true;
    }catch(...){return false;}
}
bool InputTape::Fail(std::string& error,const std::string& message){samples_.clear();header_={};stopped_=true;total_=0;error=message;return false;}
bool InputTape::Load(std::istream& input,std::string& error){
    samples_.clear();header_={};cursor_=0;remaining_=0;tick_=0;total_=0;owner_=0;epoch_=0;haveEpoch_=false;stopped_=true;
    std::string line,magic;unsigned version=0;
    if(!std::getline(input,line))return Fail(error,"missing header");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    {std::istringstream head(line);head.imbue(std::locale::classic());
     if(!(head>>magic>>version>>header_.stepUs>>header_.seed)||magic!="FVR_BC2_INPUT"||version!=TapeVersion||
        header_.stepUs<TapeMinStepUs||header_.stepUs>TapeMaxStepUs||!End(head))return Fail(error,"invalid v1 header");}
    float worldUnits=1;unsigned lineNumber=1;
    while(std::getline(input,line)){
        ++lineNumber;if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.rfind("#!",0)==0){
            std::istringstream tokens(line.substr(2));tokens.imbue(std::locale::classic());std::string token;bool expect=false,first=true;
            while(tokens>>token){
                if(first&&token=="expect"){expect=true;first=false;continue;}first=false;
                if(expect){header_.expect.push_back(token);continue;}
                auto c=Condition(token);if(!c)return Fail(error,"invalid #! token at line "+std::to_string(lineNumber));
                if(c->key=="anchor"&&(c->op!=TapeOp::Eq||(c->value!="world"&&c->value!="weapon")))return Fail(error,"anchor must be world or weapon");
                if(c->key=="world_units_per_meter"){std::istringstream v(c->value);v.imbue(std::locale::classic());if(!(v>>worldUnits)||!End(v))return Fail(error,"invalid world_units_per_meter");}
                header_.conditions.push_back(std::move(*c));
            }
            continue;
        }
        if(line.empty()||line[0]=='#')continue;
        TapeSample s;unsigned focused=0,headValid=0,floor=0;auto& f=s.frame;
        std::istringstream row(line);row.imbue(std::locale::classic());
        if(!(row>>s.ticks>>s.ageMs>>s.epoch>>focused>>headValid>>floor)||!s.ticks||focused>1||headValid>1||floor>1||s.epoch>=0xffffffffull||
           !ReadPose(row,f.head)||!ReadPose(row,f.referenceHead)||!ReadHand(row,f.hands[0])||!ReadHand(row,f.hands[1])||!End(row))
            return Fail(error,"invalid record at line "+std::to_string(lineNumber));
        f.focused=focused!=0;f.headValid=headValid!=0;f.floorRelative=floor!=0;f.worldUnitsPerMeter=worldUnits;
        if(!f.headValid){f.head={};f.referenceHead={};}
        // Validate with the identity the gather will publish (generation/time are per tick).
        f.generation=1;f.spaceGeneration=s.epoch+1;f.predictedNs=1;
        if(!interaction::ValidInput(f))return Fail(error,"record violates InputFrame rules at line "+std::to_string(lineNumber));
        total_+=s.ticks;
        if(total_>TapeMaxTicks||samples_.size()>=TapeMaxTicks)return Fail(error,"tape exceeds 360000 ticks");
        samples_.push_back(s);
    }
    if(input.bad()||samples_.empty())return Fail(error,"empty or unreadable tape");
    if(header_.AnchorWeapon())for(auto& s:samples_){
        const auto& l=s.frame.hands[0];const auto& r=s.frame.hands[1];
        if(!l.gripTracked||!r.gripTracked)continue;
        const auto gun=InversePose(r.grip);s.anchored=true;s.leftGripInGun=ComposePose(gun,l.grip);s.leftAimInGun=ComposePose(gun,l.aimTracked?l.aim:l.grip);
    }
    stopped_=false;error.clear();return true;
}
TapeTick InputTape::Next(std::uint64_t owner,const math::Pose* liveGunGrip){
    // A respawn/checkpoint/vehicle owner change never silently restarts a tape.
    if(owner&&owner_&&owner!=owner_)stopped_=true;
    if(owner&&!owner_)owner_=owner;
    TapeTick out;out.tick=++tick_;
    if(!stopped_&&cursor_<samples_.size()){
        const auto& s=samples_[cursor_];
        if(!remaining_)remaining_=s.ticks;
        if(--remaining_==0)++cursor_;
        // A new EPOCH is a new spaceGeneration, exactly as the host publishes a
        // recenter; BC2 consumers own their space-transition handling, so the
        // sample stays available (unlike KF2, which blanked the first sample).
        epoch_=s.epoch;haveEpoch_=true;
        // An old sample is unavailable, never an observed physical release.
        if(s.ageMs<=TapeStaleMs&&s.frame.focused){
            out.available=true;out.frame=s.frame;
            if(s.anchored&&liveGunGrip){out.frame.hands[0].grip=ComposePose(*liveGunGrip,s.leftGripInGun);
                if(out.frame.hands[0].aimTracked)out.frame.hands[0].aim=ComposePose(*liveGunGrip,s.leftAimInGun);}
        }
    }
    out.finished=Finished();
    auto& f=out.frame;
    f.generation=out.tick;f.spaceGeneration=epoch_+1;f.predictedNs=std::int64_t(out.tick)*header_.stepUs*1000;
    if(!out.available){const auto units=f.worldUnitsPerMeter;f=InputFrame{};f.generation=out.tick;f.spaceGeneration=epoch_+1;
        f.predictedNs=std::int64_t(out.tick)*std::max(header_.stepUs,1u)*1000;f.worldUnitsPerMeter=units>0?units:1;}
    return out;
}
bool InputTapeWriter::Begin(std::ostream& out,unsigned stepUs,std::uint64_t seed,const std::vector<std::string>& headerLines){
    if(stepUs<TapeMinStepUs||stepUs>TapeMaxStepUs)return false;
    out_=&out;pending_.clear();pendingTicks_=0;ticks_=rows_=0;
    out<<"FVR_BC2_INPUT "<<TapeVersion<<' '<<stepUs<<' '<<seed<<'\n';
    for(const auto& line:headerLines){if(line.find('\n')!=std::string::npos)return false;out<<"#! "<<line<<'\n';}
    out<<"# TICKS AGE_MS EPOCH FOCUSED HEADVALID FLOOR head(7) referenceHead(7) | hand: FLAGS BUTTONS trigger squeeze stickX stickY grip(7) aim(7)\n";
    return bool(out);
}
bool InputTapeWriter::Append(const InputFrame& frame,unsigned ageMs,bool available,std::uint64_t epoch){
    if(!out_||ticks_>=TapeMaxTicks)return false;
    InputFrame f=frame;if(!available||!interaction::ValidInput(f)){const auto units=f.worldUnitsPerMeter;f=InputFrame{};f.worldUnitsPerMeter=units;available=false;}
    std::string row;row.reserve(512);
    // Ages over 100 ms are kept: they replay as stale, like the live gather saw them.
    Unsigned(row,available?ageMs:std::max(ageMs,TapeStaleMs+1));Unsigned(row,epoch);
    Unsigned(row,f.focused?1:0);Unsigned(row,f.headValid?1:0);Unsigned(row,f.floorRelative?1:0);
    WritePose(row,f.head,f.headValid);WritePose(row,f.referenceHead,f.headValid);WriteHand(row,f.hands[0]);WriteHand(row,f.hands[1]);
    row.pop_back();
    ++ticks_;
    if(pendingTicks_&&row==pending_){++pendingTicks_;return true;}
    if(!Flush())return false;
    pending_=std::move(row);pendingTicks_=1;return true;
}
bool InputTapeWriter::Flush(){
    if(!out_)return false;
    if(pendingTicks_){*out_<<pendingTicks_<<' '<<pending_<<'\n';++rows_;pendingTicks_=0;pending_.clear();}
    return bool(*out_);
}
bool InputTapeWriter::Finish(){const bool ok=Flush();if(out_)out_->flush();return ok&&out_&&bool(*out_);}
}
