#include "fvr/ipc/ReloadAudioChannel.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <limits>
namespace fvr::ipc {
namespace {
struct Handle {HANDLE h=nullptr;~Handle(){if(h)CloseHandle(h);}};
struct alignas(8) Shared {
    std::uint32_t magic=0x31415552,version=1,bytes=1272,closed=0;
    std::array<std::uint8_t,16> session{};
    std::uint32_t hostPid=0,producerPid=0;
    std::uint64_t hostStarted=0,producerStarted=0;
    std::uint32_t head=0,count=0;
    std::uint64_t lastEvent=0;
    ReloadAudioOwner owner{};
    ReloadAudioOwner readyOwner{};
    std::array<ReloadAudioEvent,16> events{};
};
static_assert(sizeof(Shared)==1272);
std::uint64_t Started(HANDLE p){FILETIME a{},b{},c{},d{};return GetProcessTimes(p,&a,&b,&c,&d)?(std::uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime:0;}
bool Token(const std::wstring& t,std::array<std::uint8_t,16>& b){
    if(t.size()!=32)return false;bool any=false;
    for(unsigned i=0;i<16;++i){unsigned v=0;for(unsigned j=0;j<2;++j){const auto c=t[2*i+j];
        if(c>=L'0'&&c<=L'9')v=v*16+unsigned(c-L'0');else if(c>=L'a'&&c<=L'f')v=v*16+unsigned(c-L'a'+10);else return false;}
        b[i]=std::uint8_t(v);any|=v!=0;}return any;
}
}
struct ReloadAudioChannel::State {
    bool host=false,fault=false;Handle mapping,mutex,peer;Shared* data=nullptr;
    std::array<std::uint8_t,16> session{};std::int64_t frequency=0;
    ~State(){if(data)UnmapViewOfFile(data);}
    bool Header()const {return data->magic==0x31415552&&data->version==1&&data->bytes==sizeof(Shared)&&
        data->session==session&&data->closed<=1&&data->head<16&&data->count<=16&&data->hostPid&&data->hostStarted;}
    struct Lock {
        State& s;bool held=false;
        explicit Lock(State& state):s(state){if(s.fault||!s.mutex.h||!s.data)return;
            const auto r=WaitForSingleObject(s.mutex.h,0);held=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;
            if(held&&(r==WAIT_ABANDONED||!s.Header()))s.fault=true;
            else if(!held&&r!=WAIT_TIMEOUT)s.fault=true;}
        ~Lock(){if(held)ReleaseMutex(s.mutex.h);}explicit operator bool()const{return held&&!s.fault;}
    };
    bool Peer(){
        if(fault||data->closed)return false;const auto pid=host?data->producerPid:data->hostPid;
        const auto started=host?data->producerStarted:data->hostStarted;if(!pid)return false;
        if(!peer.h){peer.h=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if(!peer.h||!started||Started(peer.h)!=started){fault=true;return false;}}
        if(WaitForSingleObject(peer.h,0)!=WAIT_TIMEOUT){fault=true;return false;}return true;
    }
    std::int64_t Now()const {LARGE_INTEGER q{};if(!QueryPerformanceCounter(&q)||q.QuadPart<=0||frequency<=0)return 0;
        if(q.QuadPart/frequency>std::numeric_limits<std::int64_t>::max()/1000000000)return 0;
        return q.QuadPart/frequency*1000000000+(q.QuadPart%frequency)*1000000000/frequency;}
    void Pop(){data->events[data->head]={};data->head=(data->head+1)%16;--data->count;}
};
ReloadAudioChannel::ReloadAudioChannel()=default;ReloadAudioChannel::~ReloadAudioChannel(){Close();}
void ReloadAudioChannel::Close()noexcept {if(state_){{State::Lock lock(*state_);if(lock)state_->data->closed=1;}state_.reset();}}
bool ReloadAudioChannel::CreateHost(const std::wstring& t)noexcept{return Open(t,true);}
bool ReloadAudioChannel::ConnectProducer(const std::wstring& t)noexcept{return Open(t,false);}
bool ReloadAudioChannel::Open(const std::wstring& token,bool host)noexcept {
    Close();try{
        auto s=std::make_unique<State>();s->host=host;if(!Token(token,s->session))return false;
        const auto prefix=L"Local\\FrostbiteVR.Audio.v1."+token;
        LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0||f.QuadPart>1000000000)return false;s->frequency=f.QuadPart;
        if(host){
            s->mutex.h=CreateMutexW(nullptr,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
            s->mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),(prefix+L".map").c_str());if(!s->mapping.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        }else{
            s->mutex.h=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h)return false;
            s->mapping.h=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,(prefix+L".map").c_str());if(!s->mapping.h)return false;
        }
        s->data=static_cast<Shared*>(MapViewOfFile(s->mapping.h,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!s->data)return false;
        const auto started=Started(GetCurrentProcess());if(!started)return false;
        if(host){Shared initial{};initial.session=s->session;initial.hostPid=GetCurrentProcessId();initial.hostStarted=started;std::memcpy(s->data,&initial,sizeof(initial));}
        else {State::Lock lock(*s);if(!lock||s->data->producerPid||!s->Peer())return false;s->data->producerStarted=started;s->data->producerPid=GetCurrentProcessId();}
        state_=std::move(s);return true;
    }catch(...){Close();return false;}
}
bool ReloadAudioChannel::PublishOwner(std::uint64_t revision,std::uint64_t space,std::int64_t observedNs)noexcept {
    if(!state_||state_->host||(revision&&!space))return false;auto& s=*state_;State::Lock lock(s);if(!lock||!s.Peer())return false;
    const auto now=s.Now();if(revision&&(!ValidReloadAudioOwner({revision,space,observedNs},now)||observedNs<s.data->owner.observedNs))return false;
    if(s.data->owner.revision!=revision||s.data->owner.space!=space){s.data->head=0;s.data->count=0;s.data->readyOwner={};}
    s.data->owner={revision,space,revision?observedNs:now};return true;
}
bool ReloadAudioChannel::Publish(const ReloadAudioEvent& event)noexcept {
    if(!state_||state_->host)return false;auto& s=*state_;State::Lock lock(s);if(!lock||!s.Peer())return false;
    const auto now=s.Now();if(!ValidReloadAudio(event,now)||!ValidReloadAudioOwner(s.data->owner,now)
        ||event.ownerRevision!=s.data->owner.revision||event.space!=s.data->owner.space||event.id<=s.data->lastEvent)return false;
    while(s.data->count&&s.data->events[s.data->head].deadlineNs<=now)s.Pop();
    s.data->lastEvent=event.id; // A full queue drops this edge permanently.
    if(s.data->count==16)return false;
    s.data->events[(s.data->head+s.data->count)%16]=event;++s.data->count;return true;
}
bool ReloadAudioChannel::AcknowledgeReady(const ReloadAudioOwner& owner,bool ready)noexcept {
    if(!state_||!state_->host)return false;auto& s=*state_;State::Lock lock(s);if(!lock||!s.Peer())return false;
    if(!ready){s.data->readyOwner={};return true;}
    const auto& current=s.data->owner;
    if(!ValidReloadAudioOwner(owner,s.Now())||owner.revision!=current.revision||owner.space!=current.space||owner.observedNs!=current.observedNs)return false;
    s.data->readyOwner=owner;return true;
}
bool ReloadAudioChannel::Ready(std::uint64_t revision,std::uint64_t space,std::int64_t* deadlineNs)noexcept {
    if(deadlineNs)*deadlineNs=0;
    if(!state_||state_->host)return false;auto& s=*state_;State::Lock lock(s);if(!lock||!s.Peer())return false;
    const auto now=s.Now();const auto& owner=s.data->owner;const auto& ready=s.data->readyOwner;
    const bool valid=ValidReloadAudioOwner(owner,now)&&ValidReloadAudioOwner(ready,now)&&
        revision==owner.revision&&space==owner.space&&ready.revision==revision&&ready.space==space&&ready.observedNs<=owner.observedNs;
    if(valid&&deadlineNs)*deadlineNs=ready.observedNs+150000000;return valid;
}
bool ReloadAudioChannel::Poll(ReloadAudioOwner& owner,ReloadAudioEvent& event,bool* busy)noexcept {
    owner={};event={};if(busy)*busy=false;if(!state_||!state_->host)return false;auto& s=*state_;State::Lock lock(s);
    if(!lock){if(busy)*busy=!lock.held&&!s.fault;return false;}if(!s.Peer())return false;
    owner=s.data->owner;
    const auto now=s.Now();while(s.data->count){const auto p=s.data->events[s.data->head];s.Pop();if(ValidReloadAudio(p,now)){event=p;break;}}return true;
}
}
