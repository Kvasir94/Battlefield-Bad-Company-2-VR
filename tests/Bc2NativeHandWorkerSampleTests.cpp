#include "Bc2NativeHandWorkerSample.h"
#include "Test.h"
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000;
struct Fixture {
    Bc2NativeHandWorkerSample recorder;RigWorkerIdentity worker{};ReloadPackedSource source{};
    std::vector<std::byte> input,packed;
    Fixture(){recorder.Enable(true);worker.owner={10,11,12,13,14};worker.world=20;worker.request=21;worker.view=22;worker.nativeFrame=99;
        source.owner=worker.owner;source.rigPose=30;source.rigFingerprint=0xa7f219a1426216abull;source.inputGeneration=31;
        source.physicalEquipmentGeneration=32;source.handsNamed=true;source.boneCount=147;source.observedNs=Now;source.deadlineNs=Now+100000000;
        input.resize(147*64);packed.resize(147*48);
        for(unsigned b=0;b<147;++b)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c){float value=float(b*12+r*3+c+.25);
            std::memcpy(input.data()+b*64+r*16+c*4,&value,4);std::memcpy(packed.data()+b*48+c*16+r*4,&value,4);}
    }
    void Begin(Bc2NativeHandWorkerSample::Scope& s){s.Begin(worker,RigWorkerStatus::NoExactView,Now);}
    void Pack(Bc2NativeHandWorkerSample::Scope& s,bool first){s.PackCalled(147);s.Observe(40,first,source,first?100:200,Now+(first?1:2),input,packed,true);}
    void Pair(Bc2NativeHandWorkerSample::Scope& s){Pack(s,true);Pack(s,false);}
    std::string Report(){std::ostringstream out;recorder.Report(out);return out.str();}
};
int ExplicitOptInAndStatus(){for(unsigned n=0;n<3;++n){Fixture f;if(n==0)f.recorder.Enable(false);
    Bc2NativeHandWorkerSample::Scope s(f.recorder);s.Begin(f.worker,n==1?RigWorkerStatus::Observed:n==2?RigWorkerStatus::WrongType:RigWorkerStatus::NoExactView,Now);
    CHECK(!s.NeedsRevalidation());CHECK(!Bc2NativeHandWorkerSample::Scope::Current());s.Complete(true,Now+3);CHECK(f.recorder.Statistics().started==0);}return 0;}
int ExactPairWithoutProducer(){Fixture f;{Bc2NativeHandWorkerSample::Scope s(f.recorder);f.Begin(s);CHECK(s.NeedsRevalidation());f.Pair(s);s.Complete(true,Now+3);CHECK(!s.NeedsRevalidation());}
    CHECK(!Bc2NativeHandWorkerSample::Scope::Current());CHECK(f.recorder.Statistics().pairs==1);const auto report=f.Report();
    CHECK(report.find("\"native_frame\":99")!=std::string::npos);CHECK(report.find("\"lease_relation\":0")!=std::string::npos);
    CHECK(report.find("\"producer_admitted\":false")!=std::string::npos);return 0;}
int PostOriginalIdentityAndDeadline(){for(unsigned n=0;n<4;++n){Fixture f;Bc2NativeHandWorkerSample::Scope s(f.recorder);f.Begin(s);f.Pair(s);
    if(n==3)f.recorder.Enable(false);s.Complete(n!=0,n==1?Now+200000001:n==2?Now+1:Now+3);
    CHECK(f.recorder.Statistics().pairs==0);CHECK(f.recorder.Statistics().postRejected==1);}return 0;}
int RejectSourceAndPairMismatch(){for(unsigned n=0;n<10;++n){Fixture f;Bc2NativeHandWorkerSample::Scope s(f.recorder);f.Begin(s);f.Pack(s,true);
    if(n==0)++f.source.owner.actor;if(n==1)++f.source.rigPose;if(n==2)++f.source.inputGeneration;if(n==3)f.source.handsNamed=false;
    if(n==4)f.source.boneCount=146;if(n==5)f.source.physicalEquipmentGeneration=0;if(n==6)++f.source.rigFingerprint;
    if(n==7){float value=123.f;std::memcpy(f.input.data(),&value,4);std::memcpy(f.packed.data(),&value,4);}
    if(n==8)f.packed[0]^=std::byte{1};if(n==9){float value=std::numeric_limits<float>::infinity();std::memcpy(f.input.data(),&value,4);std::memcpy(f.packed.data(),&value,4);}
    f.Pack(s,false);s.Complete(true,Now+3);CHECK(f.recorder.Statistics().pairs==0);CHECK(f.recorder.Statistics().invalidPack==1);}return 0;}
int SlotOrderAndSourcePreservation(){for(unsigned n=0;n<5;++n){Fixture f;Bc2NativeHandWorkerSample::Scope s(f.recorder);f.Begin(s);
    if(n!=0)f.Pack(s,true);s.Observe(n==1?41:40,false,f.source,n==2?100:200,Now+2,f.input,f.packed,n!=3);
    if(n==4)f.Pack(s,true);s.Complete(true,Now+3);CHECK(f.recorder.Statistics().pairs==0);CHECK(f.recorder.Statistics().invalidPack==1);}return 0;}
int NestedRejectedMasksParent(){Fixture f;Bc2NativeHandWorkerSample::Scope outer(f.recorder);f.Begin(outer);f.Pack(outer,true);
    {Bc2NativeHandWorkerSample::Scope nested(f.recorder);CHECK(!Bc2NativeHandWorkerSample::Scope::Current());nested.Complete(true,Now+2);}
    CHECK(Bc2NativeHandWorkerSample::Scope::Current()==&outer);f.Pack(outer,false);outer.Complete(true,Now+3);CHECK(f.recorder.Statistics().pairs==1);return 0;}
int NativeFrameNeverRelabelled(){for(unsigned n=0;n<4;++n){Fixture f;RigWorkerViewLease lease;
    const unsigned frame=n==0?98:n==1?100:99;lease.eyes={ReloadProducerView{20,21,22,frame,0},ReloadProducerView{20,21,23,frame,1}};
    lease.observedNs=Now-1;lease.deadlineNs=Now+20;if(n==3)++lease.eyes[1].request;
    {Bc2NativeHandWorkerSample::Scope s(f.recorder);s.Begin(f.worker,RigWorkerStatus::NoExactView,Now,lease);s.Complete(true,Now+3);}
    const auto report=f.Report();CHECK(report.find("\"native_frame\":99")!=std::string::npos);
    CHECK(report.find("\"lease_relation\":"+std::to_string(n==0?3:n==1?4:n==2?2:1))!=std::string::npos);
    CHECK(f.recorder.Statistics().pairs==0);CHECK(f.recorder.Statistics().noPack==1);}return 0;}
int CopiedLeaseViewClassification(){for(unsigned n=0;n<7;++n){Fixture f;
    RigWorkerViewLease lease{{ReloadProducerView{20,21,22,99,0},ReloadProducerView{20,21,23,99,1}},Now-1,Now+20};
    if(n==2||n==5||n==6)f.worker.view=23;
    if(n==3)f.worker.view=24;if(n==4)++lease.eyes[1].request;
    if(n==5)lease.deadlineNs=Now;if(n==6)lease.eyes[0].nativeFrame=lease.eyes[1].nativeFrame=100;
    {Bc2NativeHandWorkerSample::Scope s(f.recorder);
        s.Begin(f.worker,RigWorkerStatus::NoExactView,Now,n?std::optional<RigWorkerViewLease>{lease}:std::nullopt);
        s.Complete(true,Now+3);}
    const auto report=f.Report();const unsigned expected=n==0?0:n==3||n==4?1:n==1?2:3;
    CHECK(report.find("\"lease_view_class\":"+std::to_string(expected))!=std::string::npos);
    CHECK(report.find("\"lease_fresh\":"+std::string(n==0||n==5?"false":"true"))!=std::string::npos);
    CHECK(report.find("\"native_frame\":99")!=std::string::npos);
    CHECK(report.find("\"lease_relation\":"+std::to_string(n==0?0:n==3||n==4?1:n==6?4:2))!=std::string::npos);
    CHECK(report.find("\"producer_admitted\":false")!=std::string::npos);
    CHECK(f.recorder.Statistics().pairs==0);CHECK(f.recorder.Statistics().noPack==1);}return 0;}
int BoundedRecentRecordsAndIncomplete(){Fixture f;for(unsigned n=0;n<35;++n){Bc2NativeHandWorkerSample::Scope s(f.recorder);f.worker.nativeFrame=n;f.Begin(s);s.PackCalled(147);s.Complete(true,Now+3);}
    CHECK(f.recorder.Statistics().unpaired==35);CHECK(f.recorder.Statistics().evicted==3);
    {Bc2NativeHandWorkerSample::Scope incomplete(f.recorder);f.Begin(incomplete);f.Pair(incomplete);}CHECK(f.recorder.Statistics().pairs==0);CHECK(f.recorder.Statistics().abandoned==1);
    CHECK(f.Report().find("\"native_frame\":0,")==std::string::npos);return 0;}
}
int main(){for(auto test:{ExplicitOptInAndStatus,ExactPairWithoutProducer,PostOriginalIdentityAndDeadline,RejectSourceAndPairMismatch,
    SlotOrderAndSourcePreservation,NestedRejectedMasksParent,NativeFrameNeverRelabelled,CopiedLeaseViewClassification,BoundedRecentRecordsAndIncomplete})if(test())return 1;
    std::cout<<"9 native hand worker sample tests passed\n";return 0;}
