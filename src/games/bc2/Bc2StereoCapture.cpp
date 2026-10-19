#include "Bc2StereoCapture.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace fvr::bc2::stereoCapture {
namespace {
std::uint32_t Bits(float f)noexcept{std::uint32_t r=0;std::memcpy(&r,&f,4);return r;}
float Float(std::uint32_t u)noexcept{float r=0;std::memcpy(&r,&u,4);return r;}
void Real(std::ostream& o,float f){if(std::isfinite(f))o<<f;else o<<"null";}
std::uint32_t Dword(const std::byte* bytes,unsigned offset)noexcept{std::uint32_t v=0;std::memcpy(&v,bytes+offset,4);return v;}
void Hex(std::ostream& o,const std::byte* bytes,std::size_t n){
    static constexpr char digits[]="0123456789abcdef";
    for(std::size_t i=0;i<n;++i){const auto b=unsigned(bytes[i]);o<<digits[b>>4]<<digits[b&15];}
}
void WriteKey(std::ostream& o,const Key& k){
    o<<"\"lease_channel\":"<<k.leaseChannel<<",\"lease_request\":"<<k.leaseRequest<<",\"tracking_generation\":"<<k.trackingGeneration
     <<",\"native_world\":"<<k.world<<",\"native_request\":"<<k.request<<",\"native_frame\":"<<k.nativeFrame;
}
unsigned Number(const char* text,unsigned fallback,unsigned low,unsigned high)noexcept{
    if(!text||!*text)return fallback;unsigned value=0;
    for(const char* p=text;*p;++p){if(*p<'0'||*p>'9'||value>100000)return fallback;value=value*10+unsigned(*p-'0');}
    return std::clamp(value,low,high);
}
std::string ArmFolder(unsigned index){std::ostringstream s;s<<"arm-"<<std::setw(2)<<std::setfill('0')<<index;return s.str();}
std::string AutoDetail(std::uint64_t d){
    std::ostringstream s;s<<"{\"view_offset\":"<<((d>>16)&0xffff)<<",\"old_byte\":"<<((d>>8)&0xff)<<",\"new_byte\":"<<(d&0xff)<<'}';return s.str();
}
}

const char* Name(Point p)noexcept{constexpr const char* n[]={"before_visibility","before_draw","after_draw"};return unsigned(p)<3?n[unsigned(p)]:"?";}
const char* Name(ArmSource s)noexcept{constexpr const char* n[]={"none","startup","trigger_file","auto_effect_field_change"};return unsigned(s)<4?n[unsigned(s)]:"?";}
const char* Name(State s)noexcept{constexpr const char* n[]={"disabled","idle","capturing","draining","ready","writing"};return unsigned(s)<6?n[unsigned(s)]:"?";}
const char* Name(ArmStatus s)noexcept{constexpr const char* n[]={"none","complete","exhausted","session_ended"};return unsigned(s)<4?n[unsigned(s)]:"?";}
const char* Name(DrawKind k)noexcept{constexpr const char* n[]={"draw","draw_indexed","draw_instanced","draw_indexed_instanced"};return unsigned(k)<4?n[unsigned(k)]:"?";}
const char* Name(Category c)noexcept{constexpr const char* n[]={"viewport","vs","ps","gs","rtv","dsv","ps_srv","depth_stencil","blend","vs_cb","ps_cb"};
    return unsigned(c)<unsigned(Category::Count)?n[unsigned(c)]:"?";}
const char* Name(ColorStatus s)noexcept{constexpr const char* n[]={"none","pending","complete","failed","unsupported","inflight_cap","timeout","unresolved"};return unsigned(s)<8?n[unsigned(s)]:"?";}

Config ParseConfig(const char* capture,const char* autoArms,const char* maxArms)noexcept {
    Config c{};
    if(!capture||(capture[0]!='1'&&capture[0]!='2')||capture[1])return c; // unset, 0 or unknown: off
    c.enabled=true;c.armAtStartup=capture[0]=='1';
    c.autoArms=Number(autoArms,DefaultAutoArms,0,MaxAutoArms);
    c.maxArms=Number(maxArms,DefaultMaxArms,1,HardMaxArms);
    return c;
}

std::uint64_t Fnv1a(const void* data,std::size_t n)noexcept{
    std::uint64_t h=14695981039346656037ULL;const auto* p=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ULL;}return h;
}

void EyeTransitions::Reset()noexcept{last_={};hasLast_=false;draws_=0;sequence_=0;kinds_={};head_.clear();tailNext_=0;tailCount_=0;}
void EyeTransitions::Reserve(){head_.reserve(HeadTransitions);tail_.resize(TailTransitions);}
void EyeTransitions::Emit(Category category,unsigned slot,std::initializer_list<std::uint32_t> values,DrawKind kind,bool initial)noexcept{
    Transition row{};row.sequence=++sequence_;row.draw=draws_;row.category=std::uint8_t(category);row.slot=std::uint8_t(slot);
    row.kind=std::uint8_t(kind);row.initial=initial?1:0;
    unsigned i=0;for(const auto v:values){if(i<row.values.size())row.values[i++]=v;}
    if(head_.size()<HeadTransitions&&head_.capacity()>=HeadTransitions){head_.push_back(row);return;}
    if(tail_.empty())return; // not reserved: counted as dropped
    tail_[tailNext_]=row;tailNext_=(tailNext_+1)%TailTransitions;if(tailCount_<TailTransitions)++tailCount_;
}
void EyeTransitions::Observe(const PipelineState& s,DrawKind kind)noexcept{
    ++draws_;if(unsigned(kind)<kinds_.size())++kinds_[unsigned(kind)];
    const bool initial=!hasLast_;const auto& p=last_;
    const auto surface=[&](Category c,unsigned slot,const Surface& v){
        Emit(c,slot,{v.view,v.resource,v.width,v.height,v.format,v.samples,v.mip,v.dimension},kind,initial);};
    for(unsigned i=0;i<s.viewports.size();++i){
        const bool now=i<s.viewportCount,before=i<p.viewportCount;
        const bool changed=initial?(now||i==0):(now!=before||(now&&std::memcmp(&s.viewports[i],&p.viewports[i],sizeof(Viewport))));
        if(changed){const auto& v=s.viewports[i];
            Emit(Category::Viewport,i,{s.viewportCount,Bits(v.x),Bits(v.y),Bits(v.width),Bits(v.height),Bits(v.minDepth),Bits(v.maxDepth)},kind,initial);}
    }
    if(initial||s.vs!=p.vs)Emit(Category::VertexShader,0,{s.vs},kind,initial);
    if(initial||s.ps!=p.ps)Emit(Category::PixelShader,0,{s.ps},kind,initial);
    if(initial||s.gs!=p.gs)Emit(Category::GeometryShader,0,{s.gs},kind,initial);
    for(unsigned i=0;i<s.rtv.size();++i)if(initial?(i==0||s.rtv[i].view):!(s.rtv[i]==p.rtv[i]))surface(Category::RenderTarget,i,s.rtv[i]);
    if(initial||!(s.dsv==p.dsv))surface(Category::DepthStencilView,0,s.dsv);
    for(unsigned i=0;i<s.psSrv.size();++i)if(initial?s.psSrv[i].view!=0:!(s.psSrv[i]==p.psSrv[i]))surface(Category::PixelResource,i,s.psSrv[i]);
    if(initial||s.depthState!=p.depthState||s.stencilRef!=p.stencilRef||s.depthBits!=p.depthBits)
        Emit(Category::DepthStencilState,0,{s.depthState,s.stencilRef,s.depthBits},kind,initial);
    bool blendFactor=false;for(unsigned i=0;i<4;++i)blendFactor|=Bits(s.blendFactor[i])!=Bits(p.blendFactor[i]);
    if(initial||blendFactor||s.blendState!=p.blendState||s.blendBits!=p.blendBits||s.sampleMask!=p.sampleMask||s.writeMask!=p.writeMask)
        Emit(Category::BlendState,0,{s.blendState,s.blendBits,s.sampleMask,s.writeMask,Bits(s.blendFactor[0]),Bits(s.blendFactor[1]),Bits(s.blendFactor[2]),Bits(s.blendFactor[3])},kind,initial);
    for(unsigned i=0;i<s.vsCb.size();++i)if(initial?s.vsCb[i]!=0:(s.vsCb[i]!=p.vsCb[i]||s.vsCbBytes[i]!=p.vsCbBytes[i]))Emit(Category::VertexConstants,i,{s.vsCb[i],s.vsCbBytes[i]},kind,initial);
    for(unsigned i=0;i<s.psCb.size();++i)if(initial?s.psCb[i]!=0:(s.psCb[i]!=p.psCb[i]||s.psCbBytes[i]!=p.psCbBytes[i]))Emit(Category::PixelConstants,i,{s.psCb[i],s.psCbBytes[i]},kind,initial);
    last_=s;hasLast_=true;
}

void WriteTransition(std::ostream& o,const Key& k,unsigned eye,const Transition& t){
    const auto& v=t.values;const auto c=Category(t.category);
    o<<"{\"lease_request\":"<<k.leaseRequest<<",\"native_frame\":"<<k.nativeFrame<<",\"eye\":"<<eye<<",\"seq\":"<<t.sequence<<",\"draw\":"<<t.draw
     <<",\"kind\":\""<<Name(DrawKind(t.kind))<<"\",\"initial\":"<<unsigned(t.initial)<<",\"cat\":\""<<Name(c)<<"\",\"slot\":"<<unsigned(t.slot);
    switch(c){
    case Category::Viewport:
        o<<",\"count\":"<<v[0]<<",\"x\":";Real(o,Float(v[1]));o<<",\"y\":";Real(o,Float(v[2]));o<<",\"w\":";Real(o,Float(v[3]));
        o<<",\"h\":";Real(o,Float(v[4]));o<<",\"min_depth\":";Real(o,Float(v[5]));o<<",\"max_depth\":";Real(o,Float(v[6]));break;
    case Category::VertexShader:case Category::PixelShader:case Category::GeometryShader:o<<",\"shader\":"<<v[0];break;
    case Category::RenderTarget:case Category::DepthStencilView:case Category::PixelResource:
        o<<",\"view\":"<<v[0]<<",\"resource\":"<<v[1]<<",\"width\":"<<v[2]<<",\"height\":"<<v[3]<<",\"format\":"<<v[4]
         <<",\"samples\":"<<v[5]<<",\"mip\":"<<v[6]<<",\"dimension\":"<<v[7];break;
    case Category::DepthStencilState:
        o<<",\"state\":"<<v[0]<<",\"stencil_ref\":"<<v[1]<<",\"depth_enable\":"<<(v[2]&1)<<",\"depth_write\":"<<((v[2]>>1)&1)
         <<",\"depth_func\":"<<((v[2]>>2)&15)<<",\"stencil_enable\":"<<((v[2]>>6)&1);break;
    case Category::BlendState:
        o<<",\"state\":"<<v[0]<<",\"rt0_enable\":"<<(v[1]&1)<<",\"alpha_to_coverage\":"<<((v[1]>>1)&1)<<",\"independent\":"<<((v[1]>>2)&1)
         <<",\"src\":"<<((v[1]>>3)&31)<<",\"dst\":"<<((v[1]>>8)&31)<<",\"op\":"<<((v[1]>>13)&7)<<",\"src_alpha\":"<<((v[1]>>16)&31)
         <<",\"dst_alpha\":"<<((v[1]>>21)&31)<<",\"op_alpha\":"<<((v[1]>>26)&7)<<",\"sample_mask\":"<<v[2]<<",\"write_mask\":"<<v[3]<<",\"factor\":[";
        for(unsigned i=0;i<4;++i){if(i)o<<',';Real(o,Float(v[4+i]));}o<<']';break;
    case Category::VertexConstants:case Category::PixelConstants:o<<",\"buffer\":"<<v[0]<<",\"bytes\":"<<v[1];break;
    default:break;
    }
    o<<'}';
}

Session::Session()=default;
Session::~Session()=default;
bool Session::Configure(const Config& c){
    config_=c;enabled_=c.enabled;
    if(!enabled_){state_.store(State::Disabled,std::memory_order_release);return true;}
    pairs_=std::make_unique<std::array<PairCapture,MaxAttemptsPerArm>>();
    for(auto& pair:*pairs_)for(auto& eye:pair.eyes)eye.transitions.Reserve();
    autoRemaining_.store(c.autoArms,std::memory_order_release);
    state_.store(State::Idle,std::memory_order_release);
    if(c.armAtStartup)RequestArm(ArmSource::Startup);
    return true;
}
bool Session::RequestArm(ArmSource source,std::uint64_t detail)noexcept{
    if(!enabled_||source==ArmSource::None)return false;
    if(pending_.load(std::memory_order_acquire))return true; // coalesced into the pending arm
    if(armsStarted_.load(std::memory_order_acquire)>=config_.maxArms){rejectedArms_.fetch_add(1,std::memory_order_relaxed);return false;}
    pendingDetail_.store(detail,std::memory_order_relaxed);
    unsigned expected=0;pending_.compare_exchange_strong(expected,1u+unsigned(source),std::memory_order_acq_rel);
    return true;
}
void Session::StartArm(ArmSource source,std::uint64_t detail,std::int64_t nowNs)noexcept{
    for(auto& pair:*pairs_){pair.key={};pair.open.store(false,std::memory_order_relaxed);pair.published=pair.complete=false;
        pair.outcomeBits=pair.sequence=0;pair.beginNs=pair.finishNs=0;
        for(auto& eye:pair.eyes){eye.view=0;eye.drawn=false;for(auto& point:eye.points)point.valid=false,point.view=0,point.readFailures=0,point.nowNs=0;
            eye.transitions.Reset();eye.color.status=ColorStatus::None;eye.color.pixels.clear();eye.color.error=0;eye.color.polls=0;}}
    attempts_.store(0,std::memory_order_relaxed);consecutive_.store(0,std::memory_order_relaxed);status_=ArmStatus::None;
    armSource_=source;armDetail_=detail;armNs_=nowNs;endNs_=0;current_.store(-1,std::memory_order_relaxed);
    armsStarted_.fetch_add(1,std::memory_order_acq_rel);
    state_.store(State::Capturing,std::memory_order_release);
}
bool Session::BeginPair(const Key& key,std::int64_t nowNs)noexcept{
    if(!enabled_)return false;
    auto state=state_.load(std::memory_order_acquire);
    if(state==State::Idle){
        const auto pending=pending_.exchange(0,std::memory_order_acq_rel);if(!pending)return false;
        StartArm(ArmSource(pending-1),pendingDetail_.load(std::memory_order_relaxed),nowNs);state=State::Capturing;
    }
    if(state!=State::Capturing)return false;
    if(const auto cur=current_.load(std::memory_order_acquire);cur>=0){
        auto& old=(*pairs_)[unsigned(cur)];
        if(old.open.load(std::memory_order_acquire))ClosePair(old,Superseded,nowNs);
        if(state_.load(std::memory_order_acquire)!=State::Capturing)return false;
    }
    const auto attempt=attempts_.load(std::memory_order_relaxed);if(attempt>=MaxAttemptsPerArm)return false;
    auto& pair=(*pairs_)[attempt];pair.key=key;pair.beginNs=nowNs;pair.published=pair.complete=false;pair.outcomeBits=pair.sequence=0;
    pair.open.store(true,std::memory_order_release);current_.store(int(attempt),std::memory_order_release);
    return true;
}
EyeCapture* Session::Eye(const Key& key,unsigned eye)noexcept{
    if(!enabled_||eye>1||state_.load(std::memory_order_acquire)!=State::Capturing)return nullptr;
    const auto cur=current_.load(std::memory_order_acquire);if(cur<0)return nullptr;
    auto& pair=(*pairs_)[unsigned(cur)];
    if(!pair.open.load(std::memory_order_acquire)||!(pair.key==key))return nullptr;
    return &pair.eyes[eye];
}
void Session::CaptureView(const Key& key,unsigned eye,Point point,std::uint32_t view,const std::array<std::uint32_t,CameraBlocks>& offsets,ReadFn read,std::int64_t nowNs)noexcept{
    auto* e=Eye(key,eye);if(!e||!read||unsigned(point)>=Points)return;
    auto& s=e->points[unsigned(point)];s.view=view;s.nowNs=nowNs;s.cameraOffsets=offsets;s.readFailures=0;e->view=view;
    // Raw copies only: +0x1540/+0x1548 are kept as bytes, never followed.
    if(!read(std::uintptr_t(view)+EffectBegin,s.effect.data(),EffectBytes)){s.effect={};++s.readFailures;}
    for(unsigned b=0;b<CameraBlocks;++b)if(!offsets[b]||!read(std::uintptr_t(view)+offsets[b],s.cameras[b].data(),CameraBytes)){s.cameras[b]={};++s.readFailures;}
    s.valid=s.readFailures==0;
}
void Session::ClosePair(PairCapture& pair,std::uint32_t bits,std::int64_t nowNs)noexcept{
    pair.open.store(false,std::memory_order_release);pair.outcomeBits=bits;pair.finishNs=nowNs;pair.published=(bits&Published)!=0;
    bool views=true;for(const auto& eye:pair.eyes){views&=eye.drawn;for(const auto& point:eye.points)views&=point.valid;}
    pair.complete=pair.published&&views;
    if(pair.complete)pair.sequence=consecutive_.fetch_add(1,std::memory_order_relaxed)+1;
    else {pair.sequence=0;consecutive_.store(0,std::memory_order_relaxed);}
    const auto attempts=attempts_.fetch_add(1,std::memory_order_acq_rel)+1;current_.store(-1,std::memory_order_release);
    if(bits&SessionEnd)return;
    if(consecutive_.load(std::memory_order_relaxed)>=PairsPerArm){status_=ArmStatus::Complete;endNs_=nowNs;state_.store(State::Draining,std::memory_order_release);}
    else if(attempts>=MaxAttemptsPerArm){status_=ArmStatus::Exhausted;endNs_=nowNs;state_.store(State::Draining,std::memory_order_release);}
}
void Session::FinishPair(std::uint64_t leaseRequest,std::uint32_t bits,std::int64_t nowNs)noexcept{
    if(!enabled_||state_.load(std::memory_order_acquire)!=State::Capturing)return;
    const auto cur=current_.load(std::memory_order_acquire);if(cur<0)return;
    auto& pair=(*pairs_)[unsigned(cur)];
    if(!pair.open.load(std::memory_order_acquire)||pair.key.leaseRequest!=leaseRequest)return;
    ClosePair(pair,bits,nowNs);
}
bool Session::TryMarkReady(bool colorsResolved)noexcept{
    auto expected=State::Draining;
    return enabled_&&colorsResolved&&state_.compare_exchange_strong(expected,State::Ready,std::memory_order_acq_rel);
}
bool Session::ObserveAutoSignal(const std::array<std::byte,AutoSignalBytes>& bytes)noexcept{
    if(!enabled_)return false;
    if(!autoSeen_){autoLast_=bytes;autoSeen_=true;autoStable_=0;return false;}
    if(bytes==autoLast_){if(autoStable_<UINT_MAX)++autoStable_;return false;}
    unsigned at=0;while(at<AutoSignalBytes&&bytes[at]==autoLast_[at])++at;
    const unsigned offset=at<AutoBlockBytes?AutoBlockBegin+at:AutoTailBegin+(at-AutoBlockBytes);
    autoChanges_.fetch_add(1,std::memory_order_relaxed);bool armed=false;
    if(autoStable_>=AutoStableFrames&&autoRemaining_.load(std::memory_order_acquire)&&
       state_.load(std::memory_order_acquire)==State::Idle&&!pending_.load(std::memory_order_acquire)){
        const std::uint64_t detail=(std::uint64_t(offset)<<16)|(std::uint64_t(autoLast_[at])<<8)|std::uint64_t(bytes[at]);
        if(RequestArm(ArmSource::Auto,detail)){autoRemaining_.fetch_sub(1,std::memory_order_acq_rel);autoArmed_.fetch_add(1,std::memory_order_relaxed);armed=true;}
    }
    autoLast_=bytes;autoStable_=0;return armed;
}
bool Session::WriteReady(const std::filesystem::path& root){
    auto expected=State::Ready;
    if(!enabled_||!state_.compare_exchange_strong(expected,State::Writing,std::memory_order_acq_rel))return false;
    bool okay=false;try{okay=WriteArm(root);}catch(...){okay=false;}
    state_.store(State::Idle,std::memory_order_release);return okay;
}
bool Session::Finalize(const std::filesystem::path& root,bool quiesced){
    if(!enabled_)return false;
    const auto state=state_.load(std::memory_order_acquire);
    if(!quiesced||state==State::Idle||state==State::Writing){try{WriteManifest(root);}catch(...){}return false;}
    if(state==State::Capturing){
        if(const auto cur=current_.load(std::memory_order_acquire);cur>=0){auto& pair=(*pairs_)[unsigned(cur)];if(pair.open.load())ClosePair(pair,SessionEnd,0);}
        status_=ArmStatus::SessionEnded;
    }
    for(auto& pair:*pairs_)for(auto& eye:pair.eyes)if(eye.color.status==ColorStatus::Pending)eye.color.status=ColorStatus::Unresolved;
    state_.store(State::Ready,std::memory_order_release);
    return WriteReady(root);
}
Telemetry Session::ReadTelemetry()const noexcept{
    Telemetry t;t.state=state_.load(std::memory_order_acquire);t.armsStarted=armsStarted_.load();t.armsWritten=armsWritten_.load();
    t.pending=pending_.load()?1:0;t.attempts=attempts_.load();t.consecutive=consecutive_.load();t.autoRemaining=autoRemaining_.load();
    t.autoSignalChanges=autoChanges_.load();t.autoArmed=autoArmed_.load();t.rejectedArms=rejectedArms_.load();t.bytesWritten=bytesWritten_.load();return t;
}
void Session::WriteTelemetry(std::ostream& o)const{
    const auto t=ReadTelemetry();
    o<<"{\"state\":\""<<Name(t.state)<<"\",\"arms_started\":"<<t.armsStarted<<",\"arms_written\":"<<t.armsWritten<<",\"pending\":"<<t.pending
     <<",\"attempts\":"<<t.attempts<<",\"consecutive\":"<<t.consecutive<<",\"auto_remaining\":"<<t.autoRemaining<<",\"auto_signal_changes\":"<<t.autoSignalChanges
     <<",\"auto_armed\":"<<t.autoArmed<<",\"rejected_arms\":"<<t.rejectedArms<<",\"bytes_written\":"<<t.bytesWritten<<'}';
}

bool Session::WriteArm(const std::filesystem::path& root){
    const unsigned index=armsStarted_.load(std::memory_order_acquire);
    WrittenArm arm;arm.index=index;arm.folder=ArmFolder(index);arm.status=Name(status_);arm.source=Name(armSource_);arm.detail=armDetail_;
    const auto folder=root/arm.folder;std::error_code ec;std::filesystem::create_directories(folder,ec);if(ec)return false;
    constexpr std::uint64_t recordsReserve=2ull<<20; // records.jsonl and manifest headroom
    std::uint64_t used=0;const std::uint64_t limit=ArmByteBudget-recordsReserve;
    const unsigned attempts=attempts_.load(std::memory_order_acquire);arm.attempts=attempts;
    const auto put=[&](const std::string& name,const void* data,std::size_t bytes)->bool{
        if(used+bytes>limit){arm.skipped.push_back(name);return false;}
        std::ofstream out(folder/name,std::ios::binary);out.write(static_cast<const char*>(data),std::streamsize(bytes));out.close();
        if(!out){arm.skipped.push_back(name+" (write failed)");return false;}
        used+=bytes;arm.files.push_back({name,bytes,Fnv1a(data,bytes)});return true;
    };
    // 1. views.bin: fixed records ((attempt*2+eye)*3+point)*ViewRecordBytes; zero if invalid.
    std::vector<std::byte> views(std::size_t(attempts)*2*Points*ViewRecordBytes);
    for(unsigned a=0;a<attempts;++a)for(unsigned e=0;e<2;++e)for(unsigned p=0;p<Points;++p){
        const auto& s=(*pairs_)[a].eyes[e].points[p];if(!s.valid&&!s.view)continue;
        auto* at=views.data()+((std::size_t(a)*2+e)*Points+p)*ViewRecordBytes;std::memcpy(at,s.effect.data(),EffectBytes);
        for(unsigned b=0;b<CameraBlocks;++b)std::memcpy(at+EffectBytes+b*CameraBytes,s.cameras[b].data(),CameraBytes);
    }
    const bool viewsWritten=!views.empty()&&put("views.bin",views.data(),views.size());
    // 2. transitions.jsonl, truncated with an explicit marker at the budget.
    {std::ostringstream t;t<<std::setprecision(9);bool truncated=false;
        for(unsigned a=0;a<attempts&&!truncated;++a)for(unsigned e=0;e<2&&!truncated;++e){
            const auto& eye=(*pairs_)[a].eyes[e];const auto& key=(*pairs_)[a].key;std::uint32_t previous=0;
            eye.transitions.ForEach([&](const Transition& row){
                if(truncated)return;
                // Middle rows dropped between the head and tail windows are explicit.
                if(row.sequence!=previous+1)t<<"{\"lease_request\":"<<key.leaseRequest<<",\"native_frame\":"<<key.nativeFrame<<",\"eye\":"<<e
                    <<",\"gap_after_seq\":"<<previous<<",\"rows_dropped\":"<<(row.sequence-previous-1)<<"}\n";
                previous=row.sequence;WriteTransition(t,key,e,row);t<<'\n';
                if(used+std::uint64_t(t.tellp())+recordsReserve/2>limit){truncated=true;t<<"{\"truncated_at_budget\":true}\n";}
            });
        }
        const auto text=t.str();
        if(!text.empty())put("transitions.jsonl",text.data(),text.size());
    }
    // 3. reduced eye colors: complete pairs first, then others.
    std::array<std::array<std::string,2>,MaxAttemptsPerArm> colorFiles{};
    for(unsigned pass=0;pass<2;++pass)for(unsigned a=0;a<attempts;++a){
        if(((*pairs_)[a].complete?0u:1u)!=pass)continue;
        for(unsigned e=0;e<2;++e){const auto& c=(*pairs_)[a].eyes[e].color;if(c.status!=ColorStatus::Complete||c.pixels.empty())continue;
            const auto name="color-a"+std::to_string(a)+"-e"+std::to_string(e)+".raw";
            if(put(name,c.pixels.data(),c.pixels.size()))colorFiles[a][e]=name;}
    }
    // 4. records.jsonl
    std::ostringstream r;r<<std::setprecision(9);
    unsigned complete=0;for(unsigned a=0;a<attempts;++a)complete+=(*pairs_)[a].complete?1:0;arm.complete=complete;
    r<<"{\"type\":\"arm\",\"arm\":"<<index<<",\"source\":\""<<arm.source<<"\",\"detail\":";
    if(armSource_==ArmSource::Auto)r<<AutoDetail(armDetail_);else r<<armDetail_;
    r<<",\"status\":\""<<arm.status<<"\",\"armed_ns\":"<<armNs_<<",\"end_ns\":"<<endNs_<<",\"attempts\":"<<attempts<<",\"complete_pairs\":"<<complete
     <<",\"pairs_required\":"<<PairsPerArm<<",\"views_bin\":"<<(viewsWritten?"true":"false")<<",\"view_record_bytes\":"<<ViewRecordBytes<<"}\n";
    // Final run: the last PairsPerArm attempts when the arm completed.
    const unsigned finalBegin=status_==ArmStatus::Complete&&attempts>=PairsPerArm?attempts-PairsPerArm:UINT_MAX;
    std::uint32_t previousFrame=0;
    for(unsigned a=0;a<attempts;++a){const auto& pair=(*pairs_)[a];const auto b=pair.outcomeBits;
        r<<"{\"type\":\"pair\",\"attempt\":"<<a<<',';WriteKey(r,pair.key);
        r<<",\"published\":"<<(pair.published?"true":"false")<<",\"complete\":"<<(pair.complete?"true":"false")<<",\"consecutive_index\":"<<pair.sequence
         <<",\"in_final_run\":"<<(a>=finalBegin?"true":"false")<<",\"outcome_bits\":"<<b<<",\"outcome\":{\"complete_pair\":"<<((b&CompletePair)?1:0)
         <<",\"restored\":"<<((b&Restored)?1:0)<<",\"captures_okay\":"<<((b&CapturesOkay)?1:0)<<",\"visibility_both\":"<<((b&VisibilityBoth)?1:0)
         <<",\"published\":"<<((b&Published)?1:0)<<",\"superseded\":"<<((b&Superseded)?1:0)<<",\"session_end\":"<<((b&SessionEnd)?1:0)<<'}'
         <<",\"begin_ns\":"<<pair.beginNs<<",\"finish_ns\":"<<pair.finishNs<<",\"native_frame_delta\":";
        if(a)r<<std::int64_t(pair.key.nativeFrame)-std::int64_t(previousFrame);else r<<"null";r<<"}\n";previousFrame=pair.key.nativeFrame;
        for(unsigned e=0;e<2;++e){const auto& eye=pair.eyes[e];
            for(unsigned p=0;p<Points;++p){const auto& s=eye.points[p];
                r<<"{\"type\":\"view\",\"attempt\":"<<a<<',';WriteKey(r,pair.key);
                r<<",\"eye\":"<<e<<",\"point\":\""<<Name(Point(p))<<"\",\"view\":"<<s.view<<",\"valid\":"<<(s.valid?"true":"false")<<",\"read_failures\":"<<s.readFailures
                 <<",\"now_ns\":"<<s.nowNs<<",\"views_bin_offset\":"<<((std::uint64_t(a)*2+e)*Points+p)*ViewRecordBytes<<",\"camera_offsets\":[";
                for(unsigned i=0;i<CameraBlocks;++i)r<<(i?",":"")<<s.cameraOffsets[i];
                r<<"],\"camera_fnv1a64\":[";for(unsigned i=0;i<CameraBlocks;++i)r<<(i?",":"")<<'"'<<std::hex<<Fnv1a(s.cameras[i].data(),CameraBytes)<<std::dec<<'"';
                const auto* x=s.effect.data();const auto at=[&](unsigned offset){return Dword(x,offset-EffectBegin);};
                r<<"],\"effect_begin\":"<<EffectBegin<<",\"effect_hex\":\"";Hex(r,x,EffectBytes);
                r<<"\",\"fields\":{\"u32_1530\":"<<at(0x1530)<<",\"raw_ptr_1540\":"<<at(PointerFieldA)<<",\"raw_ptr_1548\":"<<at(PointerFieldB)
                 <<",\"block_15d0\":["<<at(0x15d0)<<','<<at(0x15d4)<<','<<at(0x15e0)<<','<<at(0x15f0)<<','<<at(0x15f4)<<"],\"flag_15f5\":"<<unsigned(x[0x15f5-EffectBegin])
                 <<",\"u32_1640\":"<<at(0x1640)<<",\"u32_1644\":"<<at(0x1644)<<",\"f32_1640\":";Real(r,Float(at(0x1640)));r<<",\"f32_1644\":";Real(r,Float(at(0x1644)));r<<"}}\n";
            }
            const auto& c=eye.color;const auto& tr=eye.transitions;
            r<<"{\"type\":\"eye\",\"attempt\":"<<a<<',';WriteKey(r,pair.key);
            r<<",\"eye\":"<<e<<",\"view\":"<<eye.view<<",\"drawn\":"<<(eye.drawn?"true":"false")<<",\"draws\":"<<tr.Draws()<<",\"draw_kinds\":{";
            for(unsigned k=0;k<4;++k)r<<(k?",":"")<<'"'<<Name(DrawKind(k))<<"\":"<<tr.KindCount(DrawKind(k));
            r<<"},\"transition_rows\":"<<tr.Rows()<<",\"transition_rows_dropped\":"<<tr.Dropped()<<",\"color\":{\"status\":\""<<Name(c.status)<<"\",\"error\":"<<c.error
             <<",\"file\":";if(colorFiles[a][e].empty())r<<"null";else r<<'"'<<colorFiles[a][e]<<'"';
            r<<",\"source_width\":"<<c.sourceWidth<<",\"source_height\":"<<c.sourceHeight<<",\"source_format\":"<<c.sourceFormat<<",\"source_samples\":"<<c.sourceSamples
             <<",\"mip\":"<<c.mip<<",\"width\":"<<c.width<<",\"height\":"<<c.height<<",\"format\":"<<c.format<<",\"bytes_per_pixel\":"<<c.bytesPerPixel<<",\"polls\":"<<c.polls<<"}}\n";
        }
    }
    {const auto text=r.str();std::ofstream out(folder/"records.jsonl",std::ios::binary);out.write(text.data(),std::streamsize(text.size()));out.close();
        if(out){used+=text.size();arm.files.insert(arm.files.begin(),{"records.jsonl",text.size(),Fnv1a(text.data(),text.size())});}
        else arm.skipped.push_back("records.jsonl (write failed)");}
    arm.bytes=used;
    written_.push_back(std::move(arm));
    bytesWritten_.fetch_add(used,std::memory_order_relaxed);armsWritten_.fetch_add(1,std::memory_order_acq_rel);
    WriteManifest(root);
    return true;
}
void Session::WriteManifest(const std::filesystem::path& root)const{
    std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return;
    std::ostringstream m;const auto t=ReadTelemetry();
    m<<"{\"schema\":1,\"feature\":\"bc2_stereo_capture\",\"diagnostic_only\":true,\"native_writes\":false,"
     <<"\"config\":{\"env\":\"FVR_BC2_STEREO_CAPTURE\",\"arm_at_startup\":"<<(config_.armAtStartup?"true":"false")<<",\"auto_arms\":"<<config_.autoArms<<",\"max_arms\":"<<config_.maxArms<<"},"
     <<"\"bounds\":{\"pairs_per_arm\":"<<PairsPerArm<<",\"max_attempts_per_arm\":"<<MaxAttemptsPerArm<<",\"points\":[\"before_visibility\",\"before_draw\",\"after_draw\"]"
     <<",\"effect_range\":["<<EffectBegin<<','<<EffectEnd<<"],\"camera_blocks\":"<<CameraBlocks<<",\"camera_bytes\":"<<CameraBytes<<",\"view_record_bytes\":"<<ViewRecordBytes
     <<",\"head_transitions_per_eye\":"<<HeadTransitions<<",\"tail_transitions_per_eye\":"<<TailTransitions<<",\"color_max_dimension\":"<<ColorMaxDimension
     <<",\"color_in_flight_cap\":"<<MaxColorInFlight<<",\"color_poll_limit\":"<<ColorPollLimit<<",\"arm_byte_budget\":"<<ArmByteBudget
     <<",\"session_byte_bound\":"<<ArmByteBudget*config_.maxArms<<",\"hard_session_byte_bound\":"<<ArmByteBudget*HardMaxArms<<"},"
     <<"\"trigger\":{\"file\":\"stereo-capture.request\",\"location\":\"native trace report folder (parent of this folder)\",\"consumed_as\":\"stereo-capture/request-<n>.txt\"},"
     <<"\"auto_signal\":{\"view_ranges\":[["<<AutoBlockBegin<<','<<AutoBlockBegin+AutoBlockBytes<<"],["<<AutoTailBegin<<','<<AutoTailBegin+AutoTailBytes<<"]],\"stable_frames\":"<<AutoStableFrames
     <<",\"semantics\":\"unproven proxy: owned main-view effect bytes changed after being stable; not an explosion detector\"},"
     <<"\"layout\":{\"views_bin\":\"record ((attempt*2+eye)*3+point)*view_record_bytes: effect bytes [effect_range) then camera blocks in camera_offsets order; zero when not captured\","
     <<"\"color_raw\":\"tightly packed rows, top-down, width*bytes_per_pixel per row, DXGI format in records.jsonl eye row (BGRA formats are not swizzled)\","
     <<"\"pointer_fields\":\"view+0x1540 and view+0x1548 are raw values; never dereferenced\"},"
     <<"\"caveats\":[\"transitions are state differences observed at Draw/DrawIndexed/DrawInstanced/DrawIndexedInstanced only; Dispatch, DrawAuto and indirect draws are not observed\","
     <<"\"state captured: viewports 0..3, VS/PS/GS, RTV 0..3, DSV, PS SRV 0..15, depth-stencil, blend, VS/PS constant buffers 0..7 (bindings, not contents)\","
     <<"\"pairs are consecutive tracked transactions; leases never begun (no host request) are not attempts; see native_frame_delta\","
     <<"\"timings are not measurements: capture adds per-draw Get* calls during captured eyes\"],"
     <<"\"telemetry\":";WriteTelemetry(m);
    m<<",\"arms\":[";
    for(std::size_t i=0;i<written_.size();++i){const auto& a=written_[i];
        m<<(i?",":"")<<"{\"arm\":"<<a.index<<",\"folder\":\""<<a.folder<<"\",\"source\":\""<<a.source<<"\",\"status\":\""<<a.status<<"\",\"attempts\":"<<a.attempts
         <<",\"complete_pairs\":"<<a.complete<<",\"bytes\":"<<a.bytes<<",\"files\":[";
        for(std::size_t f=0;f<a.files.size();++f){const auto& file=a.files[f];
            m<<(f?",":"")<<"{\"name\":\""<<file.name<<"\",\"bytes\":"<<file.bytes<<",\"fnv1a64\":\""<<std::hex<<file.hash<<std::dec<<"\"}";}
        m<<"],\"skipped_at_budget\":[";for(std::size_t s=0;s<a.skipped.size();++s)m<<(s?",":"")<<'"'<<a.skipped[s]<<'"';m<<"]}";
    }
    m<<"],\"total_bytes\":"<<t.bytesWritten<<",\"pending_unstarted_arm\":"<<(t.pending?"true":"false")<<"}\n";
    const auto text=m.str();const auto temp=root/"manifest.json.tmp";
    {std::ofstream out(temp,std::ios::binary);out.write(text.data(),std::streamsize(text.size()));}
    std::filesystem::rename(temp,root/"manifest.json",ec);
}
}
