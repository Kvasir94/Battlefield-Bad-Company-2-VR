#include "Bc2ReloadCommandBoard.h"
#include "Test.h"
#include <cstdio>
#include <thread>
#include <vector>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Ms=1000000;
ReloadCommandBoardState Board(){ReloadCommandBoardState b;b.owner.key=7;b.owner.firing={0x10000,0x20000,0x30000};
    b.copies[0]={ReloadCommandKind::Hold,3,0,0,100*Ms};b.copies[1]={ReloadCommandKind::Observe,3};
    b.copies[2]={ReloadCommandKind::AdvanceOnce,3,9,.8f,100*Ms};b.empty.block=true;return b;}
// Expiry polarity: Hold releases, AdvanceOnce drops, the empty block stays.
int ExpiryPolarity(){
    const auto b=Board();
    auto c=ResolveReloadCommand(b,0x10000,50*Ms);CHECK(c.branch==0&&c.kind==ReloadCommandKind::Hold&&c.blockEmpty);
    c=ResolveReloadCommand(b,0x10000,100*Ms);CHECK(c.kind==ReloadCommandKind::None&&c.blockEmpty);
    c=ResolveReloadCommand(b,0x30000,50*Ms);CHECK(c.kind==ReloadCommandKind::AdvanceOnce&&c.serial==9&&c.advanceSeconds==.8f);
    c=ResolveReloadCommand(b,0x30000,200*Ms);CHECK(c.kind==ReloadCommandKind::None&&c.blockEmpty);
    c=ResolveReloadCommand(b,0x20000,1000*Ms);CHECK(c.kind==ReloadCommandKind::Observe&&c.blockEmpty);
    c=ResolveReloadCommand(b,0x40000,50*Ms);CHECK(c.branch==3&&c.kind==ReloadCommandKind::None&&!c.blockEmpty); // not ours: native
    auto t=b;t.empty.tokenSerial=4;t.empty.tokenExpiresNs=60*Ms;
    CHECK(ResolveReloadCommand(t,0x10000,59*Ms).tokenOpen&&!ResolveReloadCommand(t,0x10000,60*Ms).tokenOpen);
    ReloadCommandBoardState none;CHECK(ResolveReloadCommand(none,0x10000,0).branch==3);return 0;}
// AdvanceOnce is consumed exactly once per copy and serial, under contention.
int ConsumeOnce(){
    ReloadCommandBoard board;CHECK(board.ConsumeAdvance(2,9)&&!board.ConsumeAdvance(2,9)&&!board.ConsumeAdvance(2,8));
    CHECK(board.ConsumeAdvance(0,9)&&board.ConsumeAdvance(2,10)&&!board.ConsumeAdvance(3,1)&&!board.ConsumeAdvance(1,0));
    std::atomic<unsigned> wins=0;std::vector<std::thread> threads;
    for(unsigned n=0;n<8;++n)threads.emplace_back([&]{for(std::uint64_t s=11;s<2011;++s)if(board.ConsumeAdvance(1,s))++wins;});
    for(auto& t:threads)t.join();CHECK(wins.load()==2000);return 0;}
// Seqlock: a reader never sees a torn board (all fields from one publish).
int NoTornReads(){
    ReloadCommandBoard board;std::atomic<bool> stop=false;std::atomic<unsigned> torn=0,reads=0;
    std::thread writer([&]{for(std::uint32_t n=1;n<200000;++n){auto b=Board();b.owner.key=n;b.owner.firing={n,n,n};b.copies[2].serial=n;board.Publish(b);}stop=true;});
    std::thread reader([&]{while(!stop){if(const auto b=board.Read()){++reads;const auto k=std::uint32_t(b->owner.key);
        if(b->owner.firing[0]!=k||b->owner.firing[2]!=k||b->copies[2].serial!=k)++torn;}}});
    writer.join();reader.join();CHECK(torn==0&&reads>0);
    const auto last=board.Read();CHECK(last&&last->owner.key==199999&&last->publication==199999);return 0;}
// Receipts drain in order per copy; an overrun loses only the oldest.
int ReceiptsInOrder(){
    ReloadCommandBoard board;for(unsigned n=0;n<10;++n){ReloadCommandReceipt r;r.branch=1;r.serial=n;board.PushReceipt(r);}
    std::array<ReloadCommandReceipt,16> out{};std::uint64_t cursor=0;unsigned lost=0;
    CHECK(board.Drain(1,out,cursor,&lost)==10&&out[0].serial==0&&out[9].serial==9&&cursor==10&&!lost);
    CHECK(board.Drain(0,out,cursor)==0);
    for(unsigned n=0;n<100;++n){ReloadCommandReceipt r;r.branch=1;r.serial=100+n;board.PushReceipt(r);}
    std::array<ReloadCommandReceipt,128> many{};const auto got=board.Drain(1,many,cursor,&lost);
    CHECK(got==ReloadCommandBoard::ReceiptSlots&&lost==100-ReloadCommandBoard::ReceiptSlots&&many[got-1].serial==199);return 0;}
}
int main(){if(ExpiryPolarity()||ConsumeOnce()||NoTornReads()||ReceiptsInOrder())return 1;
    std::puts("Reload command board: expiry polarity, consume-once, seqlock and receipts passed; hook integration pending.");return 0;}
