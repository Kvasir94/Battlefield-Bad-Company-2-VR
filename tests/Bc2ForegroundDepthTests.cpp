#include "Bc2ForegroundDepth.h"
#include "Test.h"
using namespace fvr::bc2;
int main(){
    ForegroundDepthOverride o;std::uint8_t v=9;
    std::uint8_t flags[8]{1,0,1,1,0,1,1,1}; // native: foreground on, depth clear on
    CHECK(o.Step(0x1000,flags,true,v)==ForegroundDepthAction::Write&&v==0&&o.written&&o.original==1);flags[5]=v;
    CHECK(o.Step(0x1000,flags,true,v)==ForegroundDepthAction::None); // stays merged, no rewrite
    CHECK(o.Step(0x1000,flags,false,v)==ForegroundDepthAction::Restore&&v==1&&!o.written);flags[5]=v;
    CHECK(o.Step(0x1000,flags,false,v)==ForegroundDepthAction::None);
    // A native write of +0xbd while ours is live is kept.
    CHECK(o.Step(0x1000,flags,true,v)==ForegroundDepthAction::Write);flags[5]=1;
    CHECK(o.Step(0x1000,flags,false,v)==ForegroundDepthAction::None&&!o.written);
    // Replaced settings object: forget the old one, never restore into the new.
    flags[5]=1;CHECK(o.Step(0x1000,flags,true,v)==ForegroundDepthAction::Write);flags[5]=0;
    std::uint8_t other[8]{1,0,1,1,0,0,1,1};CHECK(o.Step(0x2000,other,false,v)==ForegroundDepthAction::None&&!o.written);
    // Natively merged already: nothing to own.
    CHECK(o.Step(0x2000,other,true,v)==ForegroundDepthAction::None&&!o.written);
    // Non-boolean bytes mean an unexpected layout.
    std::uint8_t bad[8]{1,0,0,0,0,7,0,0};CHECK(o.Step(0x3000,bad,true,v)==ForegroundDepthAction::Invalid);
    bad[0]=2;bad[5]=1;CHECK(o.Step(0x3000,bad,true,v)==ForegroundDepthAction::Invalid);
    CHECK(o.Step(0,flags,true,v)==ForegroundDepthAction::Invalid);
    // Only the +0xbd byte is reported; neighbouring blur/scope flags are untouched by contract.
    CHECK(kForegroundFlagsOffset+kForegroundDepthClear==0xbd);
    return 0;
}
