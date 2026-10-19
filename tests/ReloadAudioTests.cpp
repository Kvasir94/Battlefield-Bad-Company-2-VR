#include "fvr/ipc/ReloadAudioChannel.h"
#include <Windows.h>
#include "Test.h"
#include <limits>
#include <string>
using namespace fvr::ipc;
static std::int64_t Now(){LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);return q.QuadPart/f.QuadPart*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
int main(){
    ReloadAudioOwnerSnapshot snapshot;const ReloadAudioOwner original{9,3,100};
    CHECK(snapshot.Observe(true,false,original).observedNs==100);
    CHECK(snapshot.Observe(false,true,{}).observedNs==100);
    CHECK(ValidReloadAudioOwner(snapshot.Current(),150000100));
    CHECK(!ValidReloadAudioOwner(snapshot.Current(),150000101));
    CHECK(!snapshot.Observe(true,false,{}).revision); // authoritative cancellation
    snapshot.Observe(true,false,original);CHECK(!snapshot.Observe(false,false,{}).revision); // disconnect
    ReloadAudioPolicy policy;const auto now=Now();ReloadAudioOwner owner{10,3,now};
    ReloadAudioEvent e{1,100,10,3,now,now+100000000,ReloadAudioRole::MagazineOut,0,.2f,1.f,-.3f,1.f};
    CHECK(ValidReloadAudio(e,now));CHECK(policy.Update(owner,3,100,now,true));CHECK(policy.Accept(e));
    CHECK(!policy.Accept(e));++e.id;CHECK(!policy.Accept(e)); // different callback ID, same physical input/role
    ++e.id;e.role=ReloadAudioRole::MagazineIn;CHECK(policy.Accept(e));
    ++e.id;e.ownerRevision=9;CHECK(!policy.Accept(e));e.ownerRevision=10;
    ++e.id;e.space=2;CHECK(!policy.Accept(e));e.space=3;
    ++e.id;e.deadlineNs=now;CHECK(!policy.Accept(e));e.deadlineNs=now+100000000;
    ++e.id;e.emitterX=std::numeric_limits<float>::quiet_NaN();CHECK(!policy.Accept(e));e.emitterX=.2f;
    CHECK(policy.Update(owner,3,101,now,false));++e.id;e.inputSequence=101;CHECK(!policy.Accept(e));
    policy.Update(owner,3,102,now,true);++e.id;CHECK(!policy.Accept(e)); // pre-focus queue blocked
    ++e.id;e.inputSequence=102;CHECK(policy.Accept(e));
    CHECK(policy.Update(owner,3,103,now+150000001,true));++e.id;e.inputSequence=103;CHECK(!policy.Accept(e));
    owner={11,3,now+150000001};policy.Update(owner,3,104,now+150000001,true);
    ++e.id;e.ownerRevision=11;e.inputSequence=104;e.observedNs=now+150000001;e.deadlineNs=e.observedNs+50000000;CHECK(policy.Accept(e));

    wchar_t token[33]{};swprintf_s(token,L"%016llx%016llx",static_cast<unsigned long long>(GetCurrentProcessId()),static_cast<unsigned long long>(GetTickCount64()+1));
    ReloadAudioChannel host,producer;CHECK(host.CreateHost(token));CHECK(producer.ConnectProducer(token));
    const auto current=Now();CHECK(!producer.PublishOwner(10,3,current-200000000));CHECK(producer.PublishOwner(10,3,current));
    e={1,100,10,3,current,current+100000000,ReloadAudioRole::ShellIn,0,.1f,1.f,-.3f,1};
    CHECK(producer.Publish(e));CHECK(!producer.Publish(e));
    ReloadAudioOwner gotOwner;ReloadAudioEvent got;
    CHECK(host.Poll(gotOwner,got)&&got.id==1&&gotOwner.revision==10);
    std::int64_t readyDeadline=0;
    CHECK(!producer.Ready(10,3,&readyDeadline)&&!readyDeadline);
    CHECK(host.AcknowledgeReady(gotOwner,true));CHECK(producer.Ready(10,3,&readyDeadline));
    CHECK(readyDeadline==current+150000000);
    CHECK(!producer.Ready(10,4));CHECK(!producer.AcknowledgeReady(gotOwner,true));
    CHECK(producer.PublishOwner(10,3,Now()));CHECK(producer.Ready(10,3,&readyDeadline));
    CHECK(readyDeadline==current+150000000); // a producer heartbeat cannot renew host readiness
    CHECK(!host.AcknowledgeReady({10,3,current-1},true));
    CHECK(host.AcknowledgeReady({},false));CHECK(!producer.Ready(10,3));
    for(unsigned i=0;i<16;++i){e.id=2+i;++e.inputSequence;CHECK(producer.Publish(e));}
    e.id=18;CHECK(!producer.Publish(e)); // full, bounded queue
    CHECK(host.Poll(gotOwner,got)&&got.id==2);CHECK(!producer.Publish(e)); // dropped edge never retries
    CHECK(producer.PublishOwner(11,3,Now()));CHECK(host.Poll(gotOwner,got)&&!got.id&&gotOwner.revision==11);
    CHECK(!producer.Ready(11,3));CHECK(host.AcknowledgeReady(gotOwner,true));CHECK(producer.Ready(11,3));
    e.id=19;CHECK(!producer.Publish(e)); // old-owner event after switch
    CHECK(producer.PublishOwner(0,0,0));CHECK(host.Poll(gotOwner,got)&&!gotOwner.revision);
    CHECK(!producer.Ready(11,3));
    const auto mutexName=std::wstring(L"Local\\FrostbiteVR.Audio.v1.")+token+L".mutex";
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName.c_str());CHECK(mutex);
    // Mutex contention exercised on another thread (Windows mutexes recurse on one thread).
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    struct Locks{HANDLE mutex,ready,release;} locks{mutex,ready,release};
    HANDLE thread=CreateThread(nullptr,0,[](void* p)->DWORD{auto& l=*static_cast<Locks*>(p);WaitForSingleObject(l.mutex,INFINITE);SetEvent(l.ready);WaitForSingleObject(l.release,INFINITE);ReleaseMutex(l.mutex);return 0;},&locks,0,nullptr);
    CHECK(thread);CHECK(WaitForSingleObject(ready,1000)==WAIT_OBJECT_0);bool busy=false;CHECK(!host.Poll(gotOwner,got,&busy)&&busy);SetEvent(release);CHECK(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0);
    CloseHandle(thread);CloseHandle(ready);CloseHandle(release);CloseHandle(mutex);
    producer.Close();CHECK(!host.Poll(gotOwner,got));host.Close();
}
