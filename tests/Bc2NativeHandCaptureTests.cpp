#include "Bc2NativeHandDrawCapture.h"
#include "Test.h"
#include <cstring>
#include <iostream>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000;
struct Fixture {
    Bc2ReloadProducerBinding binding;ReloadProducerView view{1,2,3,4,0};ReloadPackedSource source{};
    std::vector<std::byte> input,packed;
    explicit Fixture(bool opted=true){binding.Enable(true);binding.EnableHandsDiagnostic(opted);
        source.owner={10,11,12,13,14};source.rigPose=20;source.rigFingerprint=0xa7f219a1426216abull;
        source.inputGeneration=21;source.physicalEquipmentGeneration=22;source.observedNs=Now;source.deadlineNs=Now+100000000;
        source.boneCount=147;source.handsNamed=true;input.resize(147*64);packed.resize(147*48);
        for(unsigned b=0;b<147;++b)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c){const float x=float(b*12+r*3+c+.25);
            std::memcpy(input.data()+b*64+r*16+c*4,&x,4);std::memcpy(packed.data()+b*48+c*16+r*4,&x,4);}}
    void First(){binding.ObservePacked(30,true,source,100,Now+1,input,packed);}
    void Second(){binding.ObservePacked(30,false,source,200,Now+2,input,packed);}
    void Publish(){Bc2ReloadProducerBinding::Scope scope(binding,view,true,Now);First();Second();scope.Complete(true,Now+3);}
};
int DefaultOff(){Fixture f(false);{Bc2ReloadProducerBinding::Scope scope(f.binding,f.view,true,Now);
    CHECK(!f.binding.CurrentHandsView());f.First();f.Second();scope.Complete(true,Now+3);}CHECK(!f.binding.Read(f.view,f.source.owner,Now+4));
    Fixture on;CHECK(!on.binding.CurrentHandsView());{Bc2ReloadProducerBinding::Scope scope(on.binding,on.view,true,Now);
        CHECK(on.binding.CurrentHandsView()==on.view);on.First();on.Second();scope.Complete(true,Now+3);CHECK(!on.binding.CurrentHandsView());}
    CHECK(on.binding.Read(on.view,on.source.owner,Now+4));return 0;}
int OwnerAndRigAndHash(){for(unsigned n=0;n<6;++n){Fixture f;{Bc2ReloadProducerBinding::Scope scope(f.binding,f.view,true,Now);f.First();
        if(n==0)++f.source.owner.actor;if(n==1)++f.source.rigPose;if(n==2)++f.source.rigFingerprint;
        if(n==3){float value=123.f;std::memcpy(f.input.data(),&value,4);std::memcpy(f.packed.data(),&value,4);} // valid pack, different pair hash
        if(n==4)f.source.boneCount=146;if(n==5)f.source.physicalEquipmentGeneration=0;
        f.Second();scope.Complete(true,Now+3);}CHECK(!f.binding.Read(f.view,f.source.owner,Now+4));}return 0;}
int ExactFrozenTuple(){Fixture f;f.Publish();const auto source=f.binding.Read(f.view,f.source.owner,Now+4);CHECK(source);
    ReloadDrawFrameEvidence frame{f.view.world,f.view.request,f.view.view,f.view.nativeFrame,f.view.eye,Now+4,1,source->evidence};
    const auto frozen=nativeHandCapture::ExactOwner(frame);CHECK(frozen&&frozen->packedHash==source->packedHash);
    for(unsigned n=0;n<12;++n){auto wrong=frame;
        if(n==0)++wrong.request;if(n==1)++wrong.nativeFrame;if(n==2)wrong.producer.actor=0;
        if(n==3)wrong.producer.rigPose=0;if(n==4)wrong.producer.packedPaletteHash=0;if(n==5)++wrong.producer.rigFingerprint;
        if(n==6)wrong.producer.exactRequestAssociation=false;if(n==7)wrong.eye=2;if(n==8)wrong.producer.deadlineNs=Now+4;
        if(n==9)wrong.producer.observedNs=Now+5;if(n==10)wrong.producer.physicalEquipmentGeneration=0;if(n==11)wrong.producer.inputGeneration=0;
        CHECK(!nativeHandCapture::ExactOwner(wrong));}
    auto changed=*frozen;++changed.actor;CHECK(changed!=*frozen);changed=*frozen;++changed.rigPose;CHECK(changed!=*frozen);
    changed=*frozen;++changed.request;CHECK(changed!=*frozen);changed=*frozen;++changed.packedHash;CHECK(changed!=*frozen);
    auto secondEye=frame;secondEye.eye=1;secondEye.view=7;CHECK(nativeHandCapture::ExactOwner(secondEye)==frozen);
    auto key=f.view;++key.request;CHECK(!f.binding.Read(key,f.source.owner,Now+4));return 0;}
int BothExactEmptyEyes(){Fixture f;f.Publish();const auto source=f.binding.Read(f.view,f.source.owner,Now+4);CHECK(source);
    ReloadDrawFrameEvidence left{f.view.world,f.view.request,f.view.view,f.view.nativeFrame,0,Now+4,1,source->evidence};
    auto right=left;right.eye=1;right.view=7;
    nativeHandCapture::CandidateEyes eyes;eyes.owner=*nativeHandCapture::ExactOwner(left);
    CHECK(!eyes.RetryEmpty());CHECK(!eyes.Complete(left));CHECK(eyes.Admit(left));CHECK(!eyes.RetryEmpty());
    CHECK(eyes.Complete(left));CHECK(!eyes.RetryEmpty());CHECK(!eyes.Admit(left));CHECK(!eyes.Complete(left));
    for(unsigned n=0;n<7;++n){auto wrong=right;
        if(n==0)++wrong.producer.actor;if(n==1)++wrong.producer.rigPose;if(n==2)++wrong.producer.packedPaletteHash;
        if(n==3)++wrong.producer.inputGeneration;if(n==4)wrong.view=left.view;
        if(n==5){++wrong.request;++wrong.producer.request;}if(n==6)wrong.nowNs=wrong.producer.deadlineNs;
        CHECK(!eyes.Admit(wrong));CHECK(!eyes.Complete(wrong));CHECK(!eyes.RetryEmpty());}
    CHECK(eyes.Admit(right));auto wrongView=right;++wrongView.view;CHECK(!eyes.Complete(wrongView));
    CHECK(eyes.Complete(right));CHECK(eyes.RetryEmpty());
    // Any matching catalog count prevents retirement, even if its instance
    // transform or staged geometry is unsupported. It is not an empty frame.
    ++eyes.catalogCandidates;CHECK(!eyes.RetryEmpty());return 0;}
int RetrySelectionAnchor(){Fixture f;f.Publish();const auto source=f.binding.Read(f.view,f.source.owner,Now+4);CHECK(source);
    ReloadDrawFrameEvidence frame{f.view.world,f.view.request,f.view.view,f.view.nativeFrame,0,Now+4,1,source->evidence};
    const auto original=*nativeHandCapture::ExactOwner(frame);auto next=original;
    ++next.frame;++next.input;++next.packedHash;CHECK(nativeHandCapture::SameSelection(original,next));
    for(unsigned n=0;n<9;++n){auto wrong=next;
        if(n==0)++wrong.world;if(n==1)++wrong.request;if(n==2)++wrong.actor;if(n==3)++wrong.weak;
        if(n==4)++wrong.weapon;if(n==5)++wrong.owner;if(n==6)++wrong.equipment;if(n==7)++wrong.space;if(n==8)++wrong.rigPose;
        CHECK(!nativeHandCapture::SameSelection(original,wrong));}return 0;}
int InstanceContract(){CHECK(nativeHandCapture::InstanceTransformSupported(true,1,0));
    CHECK(!nativeHandCapture::InstanceTransformSupported(false,1,0));
    CHECK(!nativeHandCapture::InstanceTransformSupported(true,0,0));
    CHECK(!nativeHandCapture::InstanceTransformSupported(true,2,0));
    CHECK(!nativeHandCapture::InstanceTransformSupported(true,1,3));return 0;}
int BusyCandidateScope(){CHECK(!nativeHandCapture::CandidateBusyScope(0,7));
    CHECK(!nativeHandCapture::CandidateBusyScope(7,0));
    CHECK(!nativeHandCapture::CandidateBusyScope(7,8));
    CHECK(nativeHandCapture::CandidateBusyScope(7,7));return 0;}
int BilateralCandidateStart(){using Decision=nativeHandCapture::CandidateStartDecision;
    Fixture f;f.Publish();const auto source=f.binding.Read(f.view,f.source.owner,Now+4);CHECK(source);
    ReloadDrawFrameEvidence frame{f.view.world,f.view.request,f.view.view,f.view.nativeFrame,0,Now+4,1,source->evidence};
    const auto initial=*nativeHandCapture::ExactOwner(frame);auto next=initial;
    ++next.frame;++next.input;++next.packedHash;
    CHECK(nativeHandCapture::CandidateStart(initial,0,{},0)==Decision::Admit);
    CHECK(nativeHandCapture::CandidateStart(initial,1,{},0)==Decision::WrongEye);
    CHECK(nativeHandCapture::CandidateStart(next,0,initial,initial.frame)==Decision::Admit);
    CHECK(nativeHandCapture::CandidateStart(next,1,initial,initial.frame)==Decision::WrongEye);
    CHECK(nativeHandCapture::CandidateStart(initial,0,initial,initial.frame)==Decision::RetiredFrame);
    auto wrong=next;++wrong.actor;
    CHECK(nativeHandCapture::CandidateStart(wrong,0,initial,initial.frame)==Decision::SelectionMismatch);
    // Beginning at eye0 never substitutes an eye1 hash/owner for eye0 proof.
    nativeHandCapture::CandidateEyes eyes;eyes.owner=initial;CHECK(eyes.Admit(frame));CHECK(eyes.Complete(frame));
    auto right=frame;right.eye=1;right.view=7;++right.producer.packedPaletteHash;
    CHECK(!eyes.Admit(right));CHECK(!eyes.RetryEmpty());return 0;}
}
int MaterialObservationKey(){using namespace fvr::bc2;
    ReloadDrawFrameEvidence f;f.world=1;f.request=2;f.view=3;f.nativeFrame=4;f.eye=0;f.nowNs=5;
    CHECK(nativeHandCapture::MaterialEyeMatches(f,f));CHECK(!nativeHandCapture::ExactOwner(f));
    for(unsigned n=0;n<7;++n){auto wrong=f;
        if(n==0)++wrong.world;if(n==1)++wrong.request;if(n==2)++wrong.view;
        if(n==3)++wrong.nativeFrame;if(n==4)++wrong.eye;if(n==5)++wrong.nowNs;if(n==6)wrong.world=0;
        CHECK(!nativeHandCapture::MaterialEyeMatches(f,wrong));}
    f.eye=2;CHECK(!nativeHandCapture::MaterialEyeMatches(f,f));return 0;
}
int main(){if(MaterialObservationKey()||DefaultOff()||OwnerAndRigAndHash()||ExactFrozenTuple()||BothExactEmptyEyes()||RetrySelectionAnchor()||InstanceContract()||BusyCandidateScope()||BilateralCandidateStart())return 1;
    std::cout<<"native hand capture:9 cases passed\n";}
