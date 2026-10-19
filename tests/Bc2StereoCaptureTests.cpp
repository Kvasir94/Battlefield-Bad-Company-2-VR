#include "Bc2StereoCapture.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace fvr::bc2::stereoCapture;
namespace fs=std::filesystem;
namespace {
void Check(bool ok,const char* what){if(!ok){std::cerr<<"stereo capture assertion failed: "<<what<<'\n';std::abort();}}
// Two fake owned views. Each holds a recognizable pattern and, at +0x1540 and
// +0x1548, "pointers" into a sentinel range the reader must never be asked for.
constexpr std::uintptr_t MainView=0x100000,EyeView=0x200000,ViewSize=0x2000,Sentinel=0x900000;
constexpr std::array<std::uint32_t,CameraBlocks> Offsets{0x100,0x560,0x9c0,0xe20};
std::vector<std::byte> mainBytes(ViewSize),eyeBytes(ViewSize);
std::vector<std::uintptr_t> reads;bool failReads=false;
void Fill(std::vector<std::byte>& v,unsigned seed){
    for(std::size_t i=0;i<v.size();++i)v[i]=std::byte((i*31+seed)&0xff);
    const std::uint32_t a=std::uint32_t(Sentinel+seed),b=std::uint32_t(Sentinel+0x100+seed);
    std::memcpy(v.data()+PointerFieldA,&a,4);std::memcpy(v.data()+PointerFieldB,&b,4);
}
bool FakeRead(std::uintptr_t address,void* out,std::size_t bytes)noexcept{
    reads.push_back(address);if(failReads)return false;
    for(const auto [base,data]:{std::pair{MainView,&mainBytes},std::pair{EyeView,&eyeBytes}})
        if(address>=base&&address+bytes<=base+ViewSize){std::memcpy(out,data->data()+(address-base),bytes);return true;}
    return false;
}
Key PairKey(unsigned n){Key k;k.leaseChannel=7;k.leaseRequest=100+n;k.trackingGeneration=500+n;k.world=0x4000;k.request=0x5000;k.nativeFrame=1000+2*n;return k;}
PipelineState StateA(){PipelineState s;s.viewportCount=1;s.viewports[0]={0,0,1920,1080,0,1};s.vs=11;s.ps=12;
    s.rtv[0]={21,22,1920,1080,28,1,0,4};s.dsv={31,32,1920,1080,45,1,0,3};s.psSrv[0]={41,42,1920,1080,28,1,0,4};
    s.depthState=51;s.depthBits=7;s.blendState=0;s.writeMask=15;s.sampleMask=~0u;s.vsCb[0]=61;s.vsCbBytes[0]=256;s.psCb[1]=62;s.psCbBytes[1]=64;return s;}
PipelineState StateB(){auto s=StateA();s.ps=13;s.rtv[0]={23,24,1920,1080,28,1,0,4};s.psSrv[0]={43,22,1920,1080,28,1,0,4};s.blendState=71;s.blendBits=0x1234;return s;}
PipelineState Quarter(){auto s=StateB();s.viewports[0]={0,0,480,270,0,1};s.rtv[0]={25,26,480,270,28,1,0,4};return s;}
// Simulates one tracked pair the way NativeProbe drives the session.
void RunPair(Session& s,unsigned n,bool published,bool withColor=true){
    const auto key=PairKey(n);Check(s.BeginPair(key,int64_t(n)*10),"begin pair");
    for(unsigned eye=0;eye<2;++eye)s.CaptureView(key,eye,Point::BeforeVisibility,unsigned(eye?EyeView:MainView),Offsets,FakeRead,1);
    for(unsigned eye=0;eye<2;++eye){
        s.CaptureView(key,eye,Point::BeforeDraw,unsigned(eye?EyeView:MainView),Offsets,FakeRead,2);
        auto* e=s.Eye(key,eye);Check(e!=nullptr,"eye capture");
        e->transitions.Observe(StateA(),DrawKind::DrawIndexed);e->transitions.Observe(StateB(),DrawKind::Draw);
        e->transitions.Observe(StateA(),DrawKind::DrawIndexed);e->transitions.Observe(Quarter(),DrawKind::Draw);
        e->drawn=true;
        s.CaptureView(key,eye,Point::AfterDraw,unsigned(eye?EyeView:MainView),Offsets,FakeRead,3);
        if(withColor){e->color.status=ColorStatus::Complete;e->color.width=4;e->color.height=2;e->color.bytesPerPixel=4;e->color.format=87;e->color.pixels.assign(32,std::uint8_t(eye+1));}
    }
    s.FinishPair(key.leaseRequest,published?(CompletePair|Restored|CapturesOkay|VisibilityBoth|Published):(CompletePair|Restored),int64_t(n)*10+5);
}
std::string ReadText(const fs::path& p){std::ifstream in(p,std::ios::binary);std::stringstream s;s<<in.rdbuf();return s.str();}
unsigned Count(const std::string& text,const std::string& needle){unsigned n=0;for(auto at=text.find(needle);at!=std::string::npos;at=text.find(needle,at+1))++n;return n;}
fs::path TempRoot(){
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    auto root=fs::temp_directory_path()/("fvr-stereo-capture-test-"+std::to_string(stamp));fs::create_directories(root);return root;
}

void ConfigParsing(){
    Check(!ParseConfig(nullptr,nullptr,nullptr).enabled,"unset is off");
    Check(!ParseConfig("0",nullptr,nullptr).enabled,"0 is off");
    Check(!ParseConfig("yes",nullptr,nullptr).enabled&&!ParseConfig("10",nullptr,nullptr).enabled,"unknown is off");
    const auto one=ParseConfig("1",nullptr,nullptr);
    Check(one.enabled&&one.armAtStartup&&one.autoArms==DefaultAutoArms&&one.maxArms==DefaultMaxArms,"1 defaults");
    const auto two=ParseConfig("2","0","3");Check(two.enabled&&!two.armAtStartup&&two.autoArms==0&&two.maxArms==3,"2 explicit");
    Check(ParseConfig("1","9","99").autoArms==MaxAutoArms&&ParseConfig("1","9","99").maxArms==HardMaxArms,"clamped high");
    Check(ParseConfig("1",nullptr,"0").maxArms==1&&ParseConfig("1","x",nullptr).autoArms==DefaultAutoArms,"clamped low / invalid default");
}
void Disabled(){
    Session s;Check(s.Configure({}),"configure off");
    Check(!s.Enabled()&&s.CurrentState()==State::Disabled,"disabled state");
    Check(!s.RequestArm(ArmSource::File)&&!s.BeginPair(PairKey(0),0)&&!s.Eye(PairKey(0),0),"inert when disabled");
}
void Transitions(){
    EyeTransitions t;t.Reserve();
    t.Observe(StateA(),DrawKind::DrawIndexed);const auto initial=t.Rows();
    // viewport, vs, ps, gs, rtv0, dsv, srv0, depth, blend, vs cb0, ps cb1
    Check(initial==11,"initial rows");
    t.Observe(StateA(),DrawKind::DrawIndexed);Check(t.Rows()==initial,"identical draw adds nothing");
    t.Observe(StateB(),DrawKind::Draw);const auto toB=t.Rows()-initial;Check(toB==4,"ps, rtv0, srv0, blend changed");
    t.Observe(StateA(),DrawKind::Draw);Check(t.Rows()-initial-toB==4,"repeated transition back is kept (no dedupe)");
    t.Observe(Quarter(),DrawKind::Draw);
    bool quarterViewport=false,quarterTarget=false;
    t.ForEach([&](const Transition& r){
        if(Category(r.category)==Category::Viewport&&!r.initial){float w=0;std::memcpy(&w,&r.values[3],4);quarterViewport|=w==480.f;}
        if(Category(r.category)==Category::RenderTarget&&r.values[2]==480)quarterTarget=true;});
    Check(quarterViewport&&quarterTarget,"reduced-resolution pass not filtered");
    Check(t.Draws()==5&&t.KindCount(DrawKind::Draw)==3&&t.KindCount(DrawKind::DrawIndexed)==2,"draw counts");
    // Overflow keeps the head and the most recent tail.
    t.Reset();for(unsigned i=0;i<6000;++i)t.Observe(i&1?StateB():StateA(),DrawKind::Draw);
    unsigned rows=0;std::uint32_t last=0,first=0;bool ordered=true;
    t.ForEach([&](const Transition& r){if(!rows)first=r.sequence;ordered&=r.sequence>last;last=r.sequence;++rows;});
    Check(rows==HeadTransitions+TailTransitions&&first==1&&last==t.Rows()&&ordered,"head+tail window ordered");
    Check(t.Dropped()==t.Rows()-rows,"dropped count explicit");
    std::ostringstream o;Transition r;r.category=std::uint8_t(Category::Viewport);float w=960;std::memcpy(&r.values[3],&w,4);WriteTransition(o,PairKey(1),1,r);
    Check(o.str().find("\"cat\":\"viewport\"")!=std::string::npos&&o.str().find("\"w\":960")!=std::string::npos&&o.str().find("\"lease_request\":101")!=std::string::npos,"transition row");
}
void ArmRearmAndSerialize(){
    Fill(mainBytes,1);Fill(eyeBytes,2);reads.clear();
    Session s;Config c;c.enabled=true;c.armAtStartup=true;c.autoArms=0;c.maxArms=3;Check(s.Configure(c),"configure");
    const auto root=TempRoot();s.WriteManifest(root);Check(fs::exists(root/"manifest.json"),"startup manifest");
    Check(s.CurrentState()==State::Idle,"idle until first pair");
    Check(!s.Eye(PairKey(0),0),"no capture before a pair begins");
    for(unsigned n=0;n<4;++n){RunPair(s,n,true);if(n<3)Check(s.CurrentState()==State::Capturing,"capturing");}
    Check(s.CurrentState()==State::Draining&&s.Status()==ArmStatus::Complete&&s.Attempts()==4,"four consecutive pairs complete the arm");
    Check(!s.BeginPair(PairKey(9),0),"no new pair while draining");
    Check(!s.TryMarkReady(false)&&s.CurrentState()==State::Draining,"waits for color readbacks");
    Check(s.TryMarkReady(true)&&s.CurrentState()==State::Ready,"ready");
    for(unsigned n=0;n<4;++n){const auto& p=s.Pair(n);Check(p.complete&&p.sequence==n+1&&p.key==PairKey(n),"pair keys and sequence");
        for(const auto& e:p.eyes)for(const auto& v:e.points)Check(v.valid,"all points valid");}
    // Read-only proof: every read is inside the effect range or a camera block, never at the stored pointers.
    for(const auto at:reads){Check(at<Sentinel||at>=Sentinel+0x1000,"pointer fields never dereferenced");
        const auto base=at>=EyeView?EyeView:MainView;const auto off=at-base;bool known=off==EffectBegin;for(const auto o:Offsets)known|=off==o;Check(known,"only effect/camera reads");}
    Check(s.WriteReady(root)&&s.CurrentState()==State::Idle,"written and idle");
    const auto arm=root/"arm-01";
    Check(fs::file_size(arm/"views.bin")==std::uintmax_t(4)*2*Points*ViewRecordBytes,"views.bin size");
    {std::ifstream in(arm/"views.bin",std::ios::binary);std::vector<char> rec(ViewRecordBytes);in.seekg(std::streamoff(((1*2+1)*Points+2)*ViewRecordBytes));in.read(rec.data(),ViewRecordBytes);
        Check(!std::memcmp(rec.data(),eyeBytes.data()+EffectBegin,EffectBytes)&&!std::memcmp(rec.data()+EffectBytes+CameraBytes,eyeBytes.data()+Offsets[1],CameraBytes),"raw effect/camera bytes");}
    const auto records=ReadText(arm/"records.jsonl");
    Check(Count(records,"\"type\":\"arm\"")==1&&Count(records,"\"type\":\"pair\"")==4&&Count(records,"\"type\":\"view\"")==24&&Count(records,"\"type\":\"eye\"")==8,"record counts");
    Check(Count(records,"\"in_final_run\":true")==4&&records.find("\"source\":\"startup\"")!=std::string::npos,"final run and source");
    Check(records.find("\"raw_ptr_1540\":"+std::to_string(Sentinel+1))!=std::string::npos&&records.find("\"raw_ptr_1548\":"+std::to_string(Sentinel+0x100+2))!=std::string::npos,"raw pointer values recorded");
    Check(records.find("\"lease_request\":103")!=std::string::npos&&records.find("\"native_frame\":1006")!=std::string::npos&&records.find("\"tracking_generation\":503")!=std::string::npos,"exact keys");
    Check(fs::exists(arm/"color-a0-e0.raw")&&fs::file_size(arm/"color-a3-e1.raw")==32,"color dumps");
    const auto transitions=ReadText(arm/"transitions.jsonl");Check(Count(transitions,"\n")==4*2*(11+4+4+5),"every transition row written");
    auto manifest=ReadText(root/"manifest.json");Check(manifest.find("\"arm\":1")!=std::string::npos&&manifest.find("\"session_byte_bound\":"+std::to_string(ArmByteBudget*3))!=std::string::npos,"manifest");

    // Rearm at runtime; an incomplete pair restarts the consecutive run.
    Check(s.RequestArm(ArmSource::File,1)&&s.RequestArm(ArmSource::File,2),"rearm accepted and coalesced");
    RunPair(s,10,true);RunPair(s,11,false);Check(s.Pair(1).sequence==0&&!s.Pair(1).complete,"unpublished pair aborts");
    for(unsigned n=12;n<16;++n)RunPair(s,n,true);
    Check(s.CurrentState()==State::Draining&&s.Attempts()==6&&s.Pair(5).sequence==4,"restarted run completes");
    Check(s.TryMarkReady(true)&&s.WriteReady(root),"second arm written");
    const auto second=ReadText(root/"arm-02"/"records.jsonl");
    Check(Count(second,"\"in_final_run\":true")==4&&second.find("\"source\":\"trigger_file\"")!=std::string::npos,"second arm metadata");
    Check(s.ReadTelemetry().pending==0,"coalesced request consumed once");

    // Exhaustion and supersession.
    Check(s.RequestArm(ArmSource::File,3),"third arm");
    const auto first=PairKey(20);Check(s.BeginPair(first,0)&&s.BeginPair(PairKey(21),0),"superseding begin");
    Check(s.Pair(0).outcomeBits==Superseded&&!s.Pair(0).complete,"superseded pair recorded");
    s.FinishPair(first.leaseRequest,Published,0);Check(s.Attempts()==1,"stale finish ignored");
    Check(!s.Eye(first,0)&&s.Eye(PairKey(21),0),"exact key required");
    s.FinishPair(PairKey(21).leaseRequest,0,0);
    for(unsigned n=22;s.CurrentState()==State::Capturing;++n)RunPair(s,n,false,false);
    Check(s.Status()==ArmStatus::Exhausted&&s.Attempts()==MaxAttemptsPerArm,"bounded attempts");
    Check(s.TryMarkReady(true)&&s.WriteReady(root),"exhausted arm written");
    Check(!s.RequestArm(ArmSource::File,4)&&s.ReadTelemetry().rejectedArms==1,"max arms enforced");
    Check(s.Finalize(root,true)==false&&fs::exists(root/"manifest.json"),"idle finalize writes manifest only");
    manifest=ReadText(root/"manifest.json");Check(Count(manifest,"\"folder\":\"arm-0")==3,"three arms in manifest");
    fs::remove_all(root);
}
void AutoSignal(){
    Session s;Config c;c.enabled=true;c.autoArms=1;Check(s.Configure(c),"configure auto");
    std::array<std::byte,AutoSignalBytes> v{};
    // A signal that never settles never arms.
    for(unsigned i=0;i<500;++i){v[0]=std::byte(i&0xff);Check(!s.ObserveAutoSignal(v),"unstable signal never arms");}
    for(unsigned i=0;i<AutoStableFrames;++i)Check(!s.ObserveAutoSignal(v),"stable");
    v[0x25]=std::byte(1);Check(s.ObserveAutoSignal(v),"change after stability arms");
    Check(s.BeginPair(PairKey(0),0)&&s.CurrentState()==State::Capturing,"auto arm starts on next pair");
    for(unsigned i=0;i<AutoStableFrames+5;++i)s.ObserveAutoSignal(v);v[0x25]=std::byte(0);
    Check(!s.ObserveAutoSignal(v)&&s.ReadTelemetry().autoArmed==1,"auto arms bounded");
    // Session end mid-capture writes a partial, labeled arm.
    const auto root=TempRoot();
    Check(s.Finalize(root,true),"partial arm written at shutdown");
    const auto records=ReadText(root/"arm-01"/"records.jsonl");
    Check(records.find("\"status\":\"session_ended\"")!=std::string::npos&&records.find("\"source\":\"auto_effect_field_change\"")!=std::string::npos&&
          records.find("\"view_offset\":"+std::to_string(AutoBlockBegin+0x25))!=std::string::npos,"partial arm labeled");
    Session t;Check(t.Configure(c),"configure");Check(t.RequestArm(ArmSource::File)&&t.BeginPair(PairKey(0),0),"capturing");
    Check(!t.Finalize(root,false)&&t.CurrentState()==State::Capturing,"no write while callbacks may run");
    fs::remove_all(root);
}
void ByteBound(){
    // Worst case: every attempt carries full transitions and maximum-size colors.
    Fill(mainBytes,3);Fill(eyeBytes,4);
    Session s;Config c;c.enabled=true;c.armAtStartup=true;Check(s.Configure(c),"configure bound");
    for(unsigned n=0;n<MaxAttemptsPerArm;++n){
        const auto key=PairKey(n);Check(s.BeginPair(key,0),"begin");
        for(unsigned eye=0;eye<2;++eye){auto* e=s.Eye(key,eye);
            for(unsigned i=0;i<HeadTransitions+TailTransitions+500;++i)e->transitions.Observe(i&1?StateB():Quarter(),DrawKind::Draw);
            e->color.status=ColorStatus::Complete;e->color.width=e->color.height=ColorMaxDimension;e->color.bytesPerPixel=4;
            e->color.pixels.assign(std::size_t(ColorMaxDimension)*ColorMaxDimension*4,std::uint8_t(n));}
        s.FinishPair(key.leaseRequest,0,0);
    }
    Check(s.TryMarkReady(true),"ready");const auto root=TempRoot();Check(s.WriteReady(root),"written");
    std::uintmax_t total=0;for(const auto& f:fs::directory_iterator(root/"arm-01"))total+=fs::file_size(f.path());
    Check(total<=ArmByteBudget,"arm stays within its byte budget");
    const auto manifest=ReadText(root/"manifest.json");Check(manifest.find("skipped_at_budget\":[\"")!=std::string::npos||ReadText(root/"arm-01"/"transitions.jsonl").find("truncated_at_budget")!=std::string::npos,"budget enforcement recorded");
    fs::remove_all(root);
}
void ConcurrentRequests(){
    Session s;Config c;c.enabled=true;c.maxArms=16;Check(s.Configure(c),"configure");
    std::vector<std::thread> threads;for(unsigned i=0;i<8;++i)threads.emplace_back([&]{for(unsigned k=0;k<1000;++k)s.RequestArm(ArmSource::File,k);});
    for(auto& t:threads)t.join();
    Check(s.ReadTelemetry().pending==1&&s.BeginPair(PairKey(0),0)&&s.ReadTelemetry().armsStarted==1&&s.ReadTelemetry().pending==0,"one coalesced arm");
}
}
int main(){
    ConfigParsing();Disabled();Transitions();ArmRearmAndSerialize();AutoSignal();ByteBound();ConcurrentRequests();
    std::cout<<"Bc2StereoCapture tests passed\n";return 0;
}
