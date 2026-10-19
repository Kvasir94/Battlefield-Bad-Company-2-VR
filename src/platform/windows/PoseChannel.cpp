#include "fvr/ipc/PoseChannel.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstring>
namespace fvr::ipc {
namespace {
struct Handle {HANDLE h=nullptr;~Handle(){if(h)CloseHandle(h);}};
struct alignas(8) Shared {
    std::uint32_t magic=0x31535046,version=1,bytes=288,closed=0; // FPS1
    std::array<std::uint8_t,16> session{};
    std::uint32_t hostPid=0,sequence=0; // sequence: odd while the host writes.
    FrameRequest sample{};
};
static_assert(sizeof(Shared)==288&&offsetof(Shared,sample)==40&&offsetof(Shared,sequence)%4==0);
bool Token(const std::wstring& t,std::array<std::uint8_t,16>& b){
    if(t.size()!=32)return false;bool any=false;
    for(unsigned i=0;i<16;++i){unsigned v=0;for(unsigned j=0;j<2;++j){const auto c=t[2*i+j];
        if(c>=L'0'&&c<=L'9')v=v*16+unsigned(c-L'0');else if(c>=L'a'&&c<=L'f')v=v*16+unsigned(c-L'a'+10);else return false;}
        b[i]=std::uint8_t(v);any|=v!=0;}return any;
}
std::int64_t Now(){LARGE_INTEGER n{};return QueryPerformanceCounter(&n)?n.QuadPart:0;}
}
struct PoseChannel::State {
    bool host=false;Handle mapping;Shared* data=nullptr;std::array<std::uint8_t,16> session{};std::int64_t frequency=0;std::uint64_t next=0;
    ~State(){if(data)UnmapViewOfFile(data);}
    bool Header()const{return data->magic==0x31535046&&data->version==1&&data->bytes==sizeof(Shared)&&data->session==session&&!data->closed;}
    std::atomic_ref<std::uint32_t> Sequence(){return std::atomic_ref<std::uint32_t>(data->sequence);}
};
PoseChannel::PoseChannel()=default;PoseChannel::~PoseChannel(){Close();}
void PoseChannel::Close()noexcept {
    if(state_&&state_->host&&state_->data){auto sequence=state_->Sequence();sequence.fetch_add(1,std::memory_order_acq_rel);state_->data->closed=1;sequence.fetch_add(1,std::memory_order_release);}
    state_.reset();
}
bool PoseChannel::CreateHost(const std::wstring& t)noexcept{return Open(t,true);}
bool PoseChannel::ConnectProducer(const std::wstring& t)noexcept{return Open(t,false);}
bool PoseChannel::Open(const std::wstring& token,bool host)noexcept {
    Close();try{
        auto s=std::make_unique<State>();s->host=host;if(!Token(token,s->session))return false;
        LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return false;s->frequency=f.QuadPart;
        const auto name=L"Local\\FrostbiteVR.Pose.v1."+token+L".map";
        if(host){s->mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str());if(!s->mapping.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;}
        else{s->mapping.h=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!s->mapping.h)return false;}
        s->data=static_cast<Shared*>(MapViewOfFile(s->mapping.h,host?FILE_MAP_READ|FILE_MAP_WRITE:FILE_MAP_READ,0,0,sizeof(Shared)));if(!s->data)return false;
        if(host){Shared initial{};initial.session=s->session;initial.hostPid=GetCurrentProcessId();std::memcpy(s->data,&initial,sizeof(initial));}
        else if(!s->Header())return false;
        state_=std::move(s);return true;
    }catch(...){state_.reset();return false;}
}
bool PoseChannel::Publish(const runtime::PresentationRequirements& requirements,const runtime::TrackingFrame& tracking)noexcept {
    if(!state_||!state_->host)return false;auto& s=*state_;
    const auto now=Now();if(now<=0||s.next==UINT64_MAX)return false;
    FrameRequest sample{};if(!Encode(requirements,tracking,s.next+1,now+s.frequency/10,sample))return false;
    auto sequence=s.Sequence();sequence.fetch_add(1,std::memory_order_acq_rel);
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&s.data->sample,&sample,sizeof(sample));
    sequence.fetch_add(1,std::memory_order_release);++s.next;++published_;return true;
}
bool PoseChannel::Read(runtime::TrackingFrame& out)noexcept {
    if(!state_||state_->host)return false;auto& s=*state_;
    auto sequence=s.Sequence();
    const auto before=sequence.load(std::memory_order_acquire);if(before&1)return false;
    FrameRequest sample{};std::memcpy(&sample,const_cast<const FrameRequest*>(&s.data->sample),sizeof(sample));
    std::atomic_thread_fence(std::memory_order_acquire);
    if(sequence.load(std::memory_order_relaxed)!=before||!s.Header())return false;
    const auto now=Now();if(now<=0||now>=sample.deadlineQpc)return false;
    runtime::PresentationRequirements requirements{};runtime::TrackingFrame tracking{};
    if(!Decode(sample,requirements,tracking))return false;out=tracking;return true;
}
}
