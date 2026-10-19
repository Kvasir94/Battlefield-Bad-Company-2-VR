#include "fvr/ipc/FrameChannel.h"
#include "fvr/ipc/FeedbackChannel.h"
#include "fvr/ipc/PoseChannel.h"
#include <Windows.h>
#include <objbase.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace fvr::ipc {
namespace {
struct Handle {
    HANDLE h=nullptr;
    ~Handle(){if(h)CloseHandle(h);}
    void Set(HANDLE value){if(h)CloseHandle(h);h=value;}
};
// v9: four request slots (pipeline depth up to 4; v8 had two), per-slot body
// props, and an optional per-slot DepthAttachment. The DLL and the host must come
// from the same build: the object names (Control.v6) and the header version both
// changed, so a v8 peer cannot open or validate this block.
constexpr unsigned SlotCount=FrameSlotCount;
struct alignas(8) Shared {
    std::uint32_t magic=0x31434646,version=9,bytes=13728,reserved0=0;
    std::array<std::uint8_t,16> session{};
    std::uint32_t hostPid=0,producerPid=0;
    std::uint64_t hostStarted=0,producerStarted=0;
    std::array<FrameSlot,SlotCount> slots{};
    std::array<graphics::BodyPropFrame,SlotCount> props{};
    InputPacket input{};
    std::array<std::uint64_t,5> reserved{};
};
static_assert(SlotCount==4 && sizeof(Shared)==13728 && offsetof(Shared,slots)==56 && offsetof(Shared,props)==3480 && offsetof(Shared,input)==13400);
std::int64_t Now(){LARGE_INTEGER n{};return QueryPerformanceCounter(&n)?n.QuadPart:0;}
std::uint64_t Started(HANDLE process){FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;return (std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
bool TokenBytes(const std::wstring& token,std::array<std::uint8_t,16>& bytes){
    if(token.size()!=32)return false;bool any=false;
    for(unsigned i=0;i<16;++i){unsigned value=0;for(unsigned j=0;j<2;++j){const auto c=token[2*i+j];if(c>=L'0'&&c<=L'9')value=value*16+unsigned(c-L'0');else if(c>=L'a'&&c<=L'f')value=value*16+unsigned(c-L'a'+10);else return false;}bytes[i]=std::uint8_t(value);any|=value!=0;}return any;
}
std::wstring NewToken(){GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create IPC session");const auto* p=reinterpret_cast<const unsigned char*>(&guid);std::wstring token;for(unsigned i=0;i<16;++i){token+=L"0123456789abcdef"[p[i]>>4];token+=L"0123456789abcdef"[p[i]&15];}return token;}
}
struct FrameChannel::State {
    FeedbackChannel feedback;
    PoseChannel pose;
    bool host=false,fault=false,pendingAck=false,ackConsumed=false,wantDepth=false;
    unsigned depth=1;
    // Host: request id per slot (0 = none). Producer: taken request id per slot.
    std::array<std::uint64_t,SlotCount> active{};
    std::uint64_t next=0,deliveredId=0,ackId=0;
    std::uint64_t rendering=0; // Producer: taken and not yet published or skipped.
    unsigned deliveredSlot=0,ackSlot=0;
    graphics::PairTicket delivered{};graphics::BodyPropFrame deliveredProps{};graphics::DepthAttachment deliveredDepth{};
    Handle mapping,mutex,response,peer;
    Shared* shared=nullptr;
    std::array<std::uint8_t,16> session{};
    std::int64_t frequency=0;
    ~State(){if(shared)UnmapViewOfFile(shared);}
    int SlotOf(std::uint64_t id)const {if(!id)return -1;for(unsigned i=0;i<SlotCount;++i)if(active[i]==id)return int(i);return -1;}
    unsigned Count()const {unsigned n=0;for(auto id:active)n+=id?1u:0u;return n;}
    std::uint64_t Oldest(std::uint64_t except=0)const {std::uint64_t best=0;for(auto id:active)if(id&&id!=except&&(!best||id<best))best=id;return best;}
    void CloseSlots(){for(auto& slot:shared->slots){slot.queued={};slot.phase=Phase::Closed;}}
    bool Closed()const {return shared->slots[0].phase==Phase::Closed;}
    bool Header()const {
        if(shared->magic!=0x31434646||shared->version!=9||shared->bytes!=sizeof(Shared)||shared->session!=session||shared->reserved0||!shared->hostPid||!shared->hostStarted)return false;
        for(auto v:shared->reserved)if(v)return false;
        for(const auto& slot:shared->slots){
            if(slot.queued.requestId){
                runtime::PresentationRequirements requirements{};runtime::TrackingFrame tracking{};
                if(slot.phase!=Phase::Complete||slot.queued.requestId<=slot.request.requestId||
                   !Decode(slot.queued,requirements,tracking))return false;
            }
            if(std::uint32_t(slot.phase)>std::uint32_t(Phase::Closed)||slot.reserved||slot.consumed>1||slot.depth.present>1)return false;
        }
        return true;
    }
    struct Lock {
        State& state;bool locked=false;
        explicit Lock(State& s,DWORD timeout=0):state(s){
            if(s.fault||!s.mutex.h||!s.shared)return;
            const auto result=WaitForSingleObject(s.mutex.h,timeout);
            if(result==WAIT_OBJECT_0){locked=true;if(!s.Header()){s.fault=true;s.CloseSlots();}}
            else if(result==WAIT_ABANDONED){locked=true;s.fault=true;s.CloseSlots();}
            else if(result!=WAIT_TIMEOUT)s.fault=true;
        }
        explicit operator bool()const{return locked&&!state.fault;}
        ~Lock(){if(locked)ReleaseMutex(state.mutex.h);}
    };
    bool Peer(){ // called under mutex, caches a handle to prevent PID-reuse confusion
        if(fault||Closed())return false;
        const auto pid=host?shared->producerPid:shared->hostPid;
        const auto started=host?shared->producerStarted:shared->hostStarted;
        if(!pid)return false;
        if(!peer.h){peer.h=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if(!peer.h||!started||Started(peer.h)!=started){fault=true;CloseSlots();return false;}}
        if(WaitForSingleObject(peer.h,0)!=WAIT_TIMEOUT){fault=true;CloseSlots();return false;}return true;
    }
    void FlushFeedback(){
        if(!pendingAck)return;
        ipc::Acknowledge(shared->slots[ackSlot],ackId,ackConsumed);
        if(active[ackSlot]==ackId)active[ackSlot]=0;
        pendingAck=false;ackId=0;deliveredId=0;
    }
    void Delivered(unsigned slot,std::uint64_t id,const graphics::PairTicket& t,const graphics::DepthAttachment& attachment){
        delivered=t;deliveredId=id;deliveredSlot=slot;deliveredProps=shared->props[slot];deliveredDepth=attachment;
    }
};
FrameChannel::FrameChannel()=default;
FrameChannel::~FrameChannel(){Close();}
void FrameChannel::Close()noexcept {
    if(state_){auto& s=*state_;{State::Lock lock(s,20);if(lock){s.FlushFeedback();s.CloseSlots();SetEvent(s.response.h);}}state_.reset();}token_.clear();
}
const std::wstring& FrameChannel::Token()const noexcept{return token_;}
bool FrameChannel::CreateHost()noexcept {
    // Host settings survive a channel re-creation.
    const unsigned depth=state_?state_->depth:1;const bool wantDepth=state_&&state_->wantDepth;
    Close();try {
        auto s=std::make_unique<State>();s->host=true;s->depth=depth;s->wantDepth=wantDepth;const auto token=NewToken();if(!TokenBytes(token,s->session))return false;
        const auto prefix=L"Local\\FrostbiteVR.Control.v6."+token;
        s->mutex.h=CreateMutexW(nullptr,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        s->response.h=CreateEventW(nullptr,FALSE,FALSE,(prefix+L".response").c_str());if(!s->response.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        s->mapping.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),(prefix+L".map").c_str());if(!s->mapping.h||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        s->shared=static_cast<Shared*>(MapViewOfFile(s->mapping.h,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!s->shared)return false;
        LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return false;s->frequency=frequency.QuadPart;
        auto initial=std::make_unique<Shared>();initial->session=s->session;initial->hostPid=GetCurrentProcessId();initial->hostStarted=Started(GetCurrentProcess());if(!initial->hostStarted)return false;
        std::memcpy(s->shared,initial.get(),sizeof(Shared));
        s->feedback.CreateHost(token); // Optional side lane cannot break image/input transport.
        s->pose.CreateHost(token); // Optional late-latch lane, same rule.
        token_=token;state_=std::move(s);return true;
    }catch(...){Close();return false;}
}
bool FrameChannel::ConnectProducer(const std::wstring& token)noexcept {
    Close();try {
        auto s=std::make_unique<State>();if(!TokenBytes(token,s->session))return false;const auto prefix=L"Local\\FrostbiteVR.Control.v6."+token;
        s->mutex.h=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(prefix+L".mutex").c_str());if(!s->mutex.h)return false;
        s->response.h=OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,(prefix+L".response").c_str());if(!s->response.h)return false;
        s->mapping.h=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,(prefix+L".map").c_str());if(!s->mapping.h)return false;
        s->shared=static_cast<Shared*>(MapViewOfFile(s->mapping.h,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!s->shared)return false;
        const auto started=Started(GetCurrentProcess());if(!started)return false;
        {State::Lock lock(*s,20);if(!lock||s->shared->producerPid||!s->Peer())return false;
            s->shared->producerStarted=started;s->shared->producerPid=GetCurrentProcessId();}
        s->feedback.ConnectProducer(token);
        s->pose.ConnectProducer(token);
        token_=token;state_=std::move(s);return true;
    }catch(...){Close();return false;}
}
bool FrameChannel::Connected()noexcept {if(!state_)return false;auto& s=*state_;State::Lock lock(s);return lock&&s.Peer();}
bool FrameChannel::PublishFeedback(const interaction::FeedbackEvent& e)noexcept {return state_&&!state_->host&&state_->feedback.Publish(e);}
bool FrameChannel::TakeFeedback(interaction::FeedbackEvent& e)noexcept {e={};return state_&&state_->host&&state_->feedback.Take(e);}
void FrameChannel::SetPipelineDepth(unsigned depth)noexcept {if(state_&&state_->host)state_->depth=(std::clamp)(depth,1u,SlotCount);}
unsigned FrameChannel::PipelineDepth()const noexcept {return state_?state_->depth:1;}
void FrameChannel::SetDepthRequested(bool wanted)noexcept {if(state_&&state_->host)state_->wantDepth=wanted;}
ChannelResult FrameChannel::RequestPair(const runtime::PresentationRequirements& requirements,const runtime::TrackingFrame& tracking,unsigned budgetMs,graphics::TextureDescriptor& d,graphics::PairTicket& t)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;
    if(!budgetMs||budgetMs>50)return ChannelResult::Invalid;
    // Blocking rendezvous: single-slot (slot 0) exchange only.
    auto& s=*state_;std::int64_t deadline=0;auto& slot=s.shared->slots[0];
    {State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
        if(!s.Peer())return s.fault||s.Closed()?ChannelResult::Closed:ChannelResult::Busy;
        s.FlushFeedback();
        for(unsigned i=1;i<SlotCount;++i)if(s.active[i])return ChannelResult::Busy;
        if(s.active[0]){if(slot.phase==Phase::Delivered)return ChannelResult::Busy;Cancel(slot,s.active[0]);s.active[0]=0;}
        if(slot.phase!=Phase::Idle)return ChannelResult::Busy;
        const auto now=Now();const auto ticks=(s.frequency/1000)*budgetMs+(s.frequency%1000)*budgetMs/1000;
        if(now<=0||ticks<=0||now>INT64_MAX-ticks||s.next==UINT64_MAX)return ChannelResult::Invalid;
        deadline=now+ticks;FrameRequest request{};
        if(!Encode(requirements,tracking,s.next+1,deadline,request))return ChannelResult::Invalid;
        if(s.wantDepth)request.flags|=FrameDepthRequested;
        if(!Begin(slot,request,now))return ChannelResult::Invalid;
        s.active[0]=++s.next;ResetEvent(s.response.h);
    }
    while(true){
        bool retryLock=false;
        // Host only: join the mutex wait queue briefly instead of repeatedly
        // losing zero-time polls to a producer on the same scheduler cadence.
        // Every producer operation still uses a zero-time try-lock.
        {State::Lock lock(s,1);if(lock){
            if(!s.Peer())return ChannelResult::Closed;
            graphics::DepthAttachment depth{};
            if(Deliver(slot,s.active[0],Now(),d,t,&depth)){s.Delivered(0,s.active[0],t,depth);return ChannelResult::Ok;}
            const auto phase=slot.phase;
            if(phase==Phase::Idle||phase==Phase::Complete||phase==Phase::Cancelled){s.active[0]=0;return ChannelResult::Timeout;}
        }else if(s.fault)return ChannelResult::Closed;else retryLock=true;}
        const auto remaining=deadline-Now();if(remaining<=0)break;
        auto ms=static_cast<DWORD>((std::min)(std::int64_t(budgetMs),(remaining*1000+s.frequency-1)/s.frequency));
        // An auto-reset response event may wake us while Publish still owns
        // the mutex. Its signal is then consumed: do not sleep until deadline
        // waiting for a second signal that the completed pair will never send.
        if(retryLock)ms=(std::min)(ms,DWORD{1});
        HANDLE waits[]={s.response.h,s.peer.h};const auto result=WaitForMultipleObjects(2,waits,FALSE,ms);
        if(result==WAIT_OBJECT_0+1||result==WAIT_FAILED){s.fault=true;return ChannelResult::Closed;}
    }
    {State::Lock lock(s);if(lock){Cancel(slot,s.active[0]);s.active[0]=0;}}
    return ChannelResult::Timeout;
}
ChannelResult FrameChannel::BeginRequest(const runtime::PresentationRequirements& requirements,const runtime::TrackingFrame& tracking,unsigned lifetimeMs,std::uint64_t* id)noexcept {
    if(id)*id=0;
    if(!state_||!state_->host)return ChannelResult::Closed;
    if(!lifetimeMs||lifetimeMs>200)return ChannelResult::Invalid;
    auto& s=*state_;State::Lock lock(s);
    if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer())return s.fault||s.Closed()?ChannelResult::Closed:ChannelResult::Busy;
    s.FlushFeedback();
    unsigned chosen=SlotCount;
    if(s.depth<=1){
        const auto& slot=s.shared->slots[0];
        if(s.Count()||(slot.phase!=Phase::Idle&&slot.phase!=Phase::Complete)||slot.queued.requestId)return ChannelResult::Busy;
        chosen=0;
    }else{
        // Never stack untaken requests: the producer always renders the newest
        // sample, and a second request only overlaps a frame it already owns.
        for(unsigned i=0;i<SlotCount;++i)if(s.active[i]){
            const auto& slot=s.shared->slots[i];
            if(slot.queued.requestId==s.active[i]||(slot.request.requestId==s.active[i]&&slot.phase==Phase::Requested))return ChannelResult::Busy;
        }
        // Depth 2 keeps its original rule (no request while a pair is delivered).
        // Depth 3+ fills any other free slot so a request is queued before the
        // game's next frame even while the host is still copying a pair.
        if(s.Count()>=s.depth||(s.depth<=2&&s.deliveredId))return ChannelResult::Busy;
        for(unsigned i=0;i<SlotCount&&chosen==SlotCount;++i)if(!s.active[i]&&s.shared->slots[i].phase==Phase::Idle)chosen=i;
        // A completed slot not yet reaped by the producer accepts a queued successor.
        for(unsigned i=0;i<SlotCount&&chosen==SlotCount;++i){const auto& slot=s.shared->slots[i];
            if(!s.active[i]&&slot.phase==Phase::Complete&&!slot.queued.requestId)chosen=i;}
        if(chosen==SlotCount)return ChannelResult::Busy;
    }
    const auto now=Now(),ticks=(s.frequency/1000)*lifetimeMs+(s.frequency%1000)*lifetimeMs/1000;
    if(now<=0||ticks<=0||now>INT64_MAX-ticks||s.next==UINT64_MAX)return ChannelResult::Invalid;
    FrameRequest request{};
    if(!Encode(requirements,tracking,s.next+1,now+ticks,request))return ChannelResult::Invalid;
    if(s.wantDepth)request.flags|=FrameDepthRequested;
    if(!Begin(s.shared->slots[chosen],request,now))return ChannelResult::Invalid;
    s.active[chosen]=++s.next;ResetEvent(s.response.h);if(id)*id=s.active[chosen];return ChannelResult::Ok;
}
ChannelResult FrameChannel::PollRequest(graphics::TextureDescriptor& d,graphics::PairTicket& t,runtime::TrackingFrame* rendered)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;
    return PollRequestId(state_->Oldest(),d,t,rendered);
}
ChannelResult FrameChannel::PollRequestId(std::uint64_t id,graphics::TextureDescriptor& d,graphics::PairTicket& t,runtime::TrackingFrame* rendered)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer())return ChannelResult::Closed;
    const int index=s.SlotOf(id);if(index<0)return ChannelResult::Invalid;
    auto& slot=s.shared->slots[index];
    const auto now=Now();
    if(slot.queued.requestId==id){
        if(now>=slot.queued.deadlineQpc){Cancel(slot,id);s.active[index]=0;return ChannelResult::Timeout;}
        return ChannelResult::Busy; // Old producer outcome must be reaped first.
    }
    // One delivered pair at a time: its feedback names the delivered slot.
    if(s.deliveredId&&s.deliveredId!=id)return ChannelResult::Busy;
    graphics::DepthAttachment depth{};
    if(Deliver(slot,id,now,d,t,&depth)){
        // Deliver has decoded this exact request; an empty result is rejected by the caller.
        if(rendered&&!RenderedTracking(slot,*rendered))*rendered={};
        s.Delivered(unsigned(index),id,t,depth);return ChannelResult::Ok;}
    const auto phase=slot.phase;
    // Delivered images belong to the consumer even after their request deadline.
    if(phase==Phase::Delivered)return ChannelResult::Busy;
    if(now>=slot.request.deadlineQpc||phase==Phase::Idle||phase==Phase::Complete||phase==Phase::Cancelled){
        Cancel(slot,id);s.active[index]=0;return ChannelResult::Timeout;
    }
    return ChannelResult::Busy;
}
bool FrameChannel::CompletedReady()noexcept {
    if(!state_||!state_->host)return false;auto& s=*state_;
    const auto id=s.Oldest();const int index=s.SlotOf(id);if(index<0||(s.deliveredId&&s.deliveredId!=id))return false;
    State::Lock lock(s);if(!lock)return false;
    const auto& slot=s.shared->slots[index];
    return slot.request.requestId==id&&slot.phase==Phase::Ready&&slot.queued.requestId!=id;
}
bool FrameChannel::WaitPublication(unsigned ms)noexcept {
    if(!state_||!state_->host||!state_->response.h)return false;auto& s=*state_;
    // Auto-reset: a signal consumed here was only a wake-up hint; every decision
    // is made again under the mutex by the caller. A peer exit also wakes us.
    HANDLE waits[]={s.response.h,s.peer.h};
    const auto result=WaitForMultipleObjects(s.peer.h?2:1,waits,FALSE,ms);
    return result==WAIT_OBJECT_0;
}
ChannelResult FrameChannel::CancelRequest()noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    s.FlushFeedback();bool busy=false;
    for(unsigned i=0;i<SlotCount;++i){
        if(!s.active[i])continue;
        auto& slot=s.shared->slots[i];
        if(slot.phase==Phase::Delivered&&slot.request.requestId==s.active[i]){busy=true;continue;}
        Cancel(slot,s.active[i]);s.active[i]=0;
    }
    return busy?ChannelResult::Busy:ChannelResult::Ok;
}
ChannelResult FrameChannel::Feedback(const graphics::PairTicket& ticket,bool consumed)noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;
    if(!s.deliveredId||s.SlotOf(s.deliveredId)!=int(s.deliveredSlot)||s.pendingAck||std::memcmp(&s.delivered,&ticket,sizeof(ticket)))return ChannelResult::Invalid;
    s.pendingAck=true;s.ackConsumed=consumed;s.ackId=s.deliveredId;s.ackSlot=s.deliveredSlot;return FlushFeedback();
}
ChannelResult FrameChannel::FlushFeedback()noexcept {
    if(!state_||!state_->host)return ChannelResult::Closed;auto& s=*state_;
    if(!s.pendingAck)return ChannelResult::Ok;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer())return ChannelResult::Closed;s.FlushFeedback();return ChannelResult::Ok;
}
ChannelResult FrameChannel::TryTake(FrameRequest& out)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;if(s.rendering||s.Count()>=SlotCount)return ChannelResult::Busy;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer())return ChannelResult::Closed;
    // The oldest requested slot that this producer does not already hold.
    unsigned chosen=SlotCount;
    for(unsigned i=0;i<SlotCount;++i){const auto& slot=s.shared->slots[i];
        if(s.active[i]||slot.phase!=Phase::Requested)continue;
        if(chosen==SlotCount||slot.request.requestId<s.shared->slots[chosen].request.requestId)chosen=i;}
    if(chosen==SlotCount||!Take(s.shared->slots[chosen],Now(),out))return ChannelResult::Busy;
    s.active[chosen]=out.requestId;s.rendering=out.requestId;return ChannelResult::Ok;
}
bool FrameChannel::ReadBodyProps(const graphics::PairTicket& ticket,graphics::BodyPropFrame& out)noexcept {
    out={};if(!state_||!state_->host)return false;const auto& s=*state_;
    if(!s.deliveredId||s.pendingAck||std::memcmp(&ticket,&s.delivered,sizeof(ticket))||!graphics::BodyPropFrameMatches(s.deliveredProps,ticket))return false;
    out=s.deliveredProps;return true;
}
bool FrameChannel::ReadDepth(const graphics::PairTicket& ticket,graphics::DepthAttachment& out)noexcept {
    out={};if(!state_||!state_->host)return false;const auto& s=*state_;
    if(!s.deliveredId||s.pendingAck||!s.deliveredDepth.present||std::memcmp(&ticket,&s.delivered,sizeof(ticket)))return false;
    out=s.deliveredDepth;return true;
}
ChannelResult FrameChannel::Publish(const graphics::TextureDescriptor& d,const graphics::PairTicket& t,const graphics::BodyPropFrame* props,const runtime::TrackingFrame* rendered,const graphics::DepthAttachment* depth)noexcept {
    return PublishId(state_?state_->rendering:0,d,t,props,rendered,depth);
}
ChannelResult FrameChannel::DeferRendering(std::uint64_t id)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;
    if(!id||s.rendering!=id||s.SlotOf(id)<0)return ChannelResult::Invalid;
    s.rendering=0;return ChannelResult::Ok;
}
ChannelResult FrameChannel::PublishId(std::uint64_t id,const graphics::TextureDescriptor& d,const graphics::PairTicket& t,const graphics::BodyPropFrame* props,const runtime::TrackingFrame* rendered,const graphics::DepthAttachment* depth)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;const int index=s.SlotOf(id);if(!id||index<0)return ChannelResult::Invalid;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer())return ChannelResult::Closed;
    auto& slot=s.shared->slots[index];
    if(ipc::Publish(slot,id,Now(),d,t,rendered,depth)){
        s.shared->props[index]=props&&graphics::BodyPropFrameMatches(*props,t)?*props:graphics::BodyPropFrame{};
        if(s.rendering==id)s.rendering=0;SetEvent(s.response.h);return ChannelResult::Ok;}
    const bool expired=slot.phase==Phase::Idle;ipc::Skip(slot,id);s.active[index]=0;if(s.rendering==id)s.rendering=0;SetEvent(s.response.h);
    return expired?ChannelResult::Timeout:ChannelResult::Invalid;
}
ChannelResult FrameChannel::Skip()noexcept {return SkipId(state_?state_->rendering:0);}
ChannelResult FrameChannel::SkipId(std::uint64_t id)noexcept {
    if(!state_||state_->host)return ChannelResult::Closed;auto& s=*state_;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    const int index=s.SlotOf(id);
    if(index>=0){ipc::Skip(s.shared->slots[index],id);s.active[index]=0;}
    if(s.rendering==id)s.rendering=0;SetEvent(s.response.h);return ChannelResult::Ok;
}
Outcome FrameChannel::PollOutcome()noexcept {
    if(!state_||state_->host)return Outcome::Discarded;
    return PollOutcome(state_->Oldest(state_->rendering));
}
Outcome FrameChannel::PollOutcome(std::uint64_t id)noexcept {
    if(!state_||state_->host)return Outcome::Discarded;auto& s=*state_;
    const int index=id&&id!=s.rendering?s.SlotOf(id):-1;
    if(index<0)return Outcome::Discarded;
    State::Lock lock(s);if(!lock)return s.fault?Outcome::Discarded:Outcome::Pending;
    if(!s.Peer()){s.active[index]=0;return Outcome::Discarded;}
    const auto outcome=Reap(s.shared->slots[index],id,Now());if(outcome!=Outcome::Pending)s.active[index]=0;return outcome;
}
ChannelResult FrameChannel::PublishInput(const interaction::InputFrame& input) noexcept {
    if(!state_||!state_->host)return ChannelResult::Invalid;auto& s=*state_;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(s.Closed())return ChannelResult::Closed;
    // Publishing before producer attachment is allowed; attachment checks lifetime.
    if(input.generation<=s.shared->input.generation)return ChannelResult::Invalid;
    const auto now=Now();InputPacket packet{};
    if(now<=0||!EncodeInput(input,now+s.frequency/10,packet))return ChannelResult::Invalid;
    s.shared->input=packet;return ChannelResult::Ok;
}
ChannelResult FrameChannel::ReadInput(interaction::InputFrame& out,std::int64_t* deadlineQpc) noexcept {
    if(deadlineQpc)*deadlineQpc=0;out={};if(!state_||state_->host)return ChannelResult::Invalid;auto& s=*state_;
    State::Lock lock(s);if(!lock)return s.fault?ChannelResult::Closed:ChannelResult::Busy;
    if(!s.Peer())return ChannelResult::Closed;
    if(!s.shared->input.generation)return ChannelResult::Busy;
    const auto now=Now();if(now<=0||now>=s.shared->input.deadlineQpc)return ChannelResult::Timeout;
    if(!DecodeInput(s.shared->input,out))return ChannelResult::Invalid;
    if(deadlineQpc)*deadlineQpc=s.shared->input.deadlineQpc;
    return ChannelResult::Ok;
}

bool FrameChannel::PublishPose(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& t)noexcept {return state_&&state_->host&&state_->pose.Publish(r,t);}
bool FrameChannel::ReadPose(runtime::TrackingFrame& out)noexcept {out={};return state_&&!state_->host&&state_->pose.Read(out);}
}
