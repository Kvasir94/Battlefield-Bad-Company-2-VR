#include "../src/games/bc2/Bc2ReloadFeedback.h"
#include "fvr/ipc/FeedbackProtocol.h"
#include "Test.h"
#include <cstdio>

using namespace fvr::interaction;
namespace {
constexpr std::int64_t Now=1000000000;
InputFrame Input(){InputFrame i;i.generation=20;i.spaceGeneration=7;i.predictedNs=Now;
    i.focused=i.headValid=true;i.hands[0].gripTracked=i.hands[1].gripTracked=true;return i;}
int CompletedActionRoutesThroughExistingWire(){
    for(bool button:{false,true}){
        CHECK(fvr::bc2::ReloadRackFeedbackHands(false,button)==0);
        const auto mask=fvr::bc2::ReloadRackFeedbackHands(true,button);
        CHECK(mask==(button?2u:3u));
        HapticFeedbackPolicy policy;auto input=Input();unsigned delivered=0;
        for(unsigned hand=0;hand<2;++hand)if(mask&(1u<<hand)){
            const auto at=Now+hand*2;
            FeedbackEvent e{hand+1,19,7,Now-1000000,Now+99000000,FeedbackKind::ReloadRack,hand};
            fvr::ipc::FeedbackPacket packet;CHECK(fvr::ipc::EncodeFeedback(e,at,packet));
            FeedbackEvent decoded;CHECK(fvr::ipc::DecodeFeedback(packet,at,decoded));
            const auto result=policy.Update(input,at,&decoded);
            CHECK(result.pulse&&result.pulse->hand==hand&&result.pulse->eventId==e.id);
            CHECK(result.pulse->kind==FeedbackKind::ReloadRack&&Near(result.pulse->amplitude,1.f)&&result.pulse->durationNs==100000000);
            CHECK(!policy.Update(input,at+1,&decoded).pulse);++delivered;
        }
        CHECK(delivered==(button?1u:2u));
    }
    return 0;
}
int CompletionDoesNotBypassDeliverySafety(){
    for(unsigned fault=0;fault<4;++fault){
        HapticFeedbackPolicy policy;auto input=Input();
        FeedbackEvent e{1,19,7,Now-1000000,Now+99000000,FeedbackKind::ReloadRack,1};
        if(fault==0)input.focused=false;
        if(fault==1)input.hands[1].gripTracked=false;
        if(fault==2)e.deadlineNs=Now;
        if(fault==3)++e.space;
        CHECK(!policy.Update(input,Now,&e).pulse);
        input=Input();e.deadlineNs=Now+99000000;e.space=7;
        CHECK(!policy.Update(input,Now+1,&e).pulse); // rejected ID stays consumed
    }
    return 0;
}
}
int main(){if(CompletedActionRoutesThroughExistingWire()||CompletionDoesNotBypassDeliverySafety())return 1;
    std::puts("BC2 reload feedback: completion hand routing, existing wire, dedup and safety passed; headset unverified.");return 0;}
