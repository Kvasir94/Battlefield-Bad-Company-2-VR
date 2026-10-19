#include "Test.h"
#include "fvr/math/ProjectionOverride.h"
#include <limits>
using namespace fvr::math;
int main(){
    std::array<float,16> right{1,0,0,0,0,1.111111f,0,0,.2f,0,-1.00005f,-1,0,0,-.1f,0};
    auto left=right;left[0]=.9f;left[5]=1.2f;left[8]=-.24f;left[9]=.17f;
    auto biased=right;biased[10]=-1.53968f;biased[14]=-11.562f;
    auto fixed=RetargetDepthProjection(biased,right,left);CHECK(fixed);
    // Verify clip-space X/Y/W at several depths, keeping the native biased Z.
    for(float z:{-8.f,-30.f,-400.f}){std::array<float,4> point{2.f,-1.f,z,1.f};
        for(unsigned col=0;col<4;++col){float actual=0,expected=0;
            for(unsigned row=0;row<4;++row){actual+=point[row]*(*fixed)[row*4+col];expected+=point[row]*(col==2?biased:left)[row*4+col];}
            CHECK(Near(actual,expected));}}
    CHECK((*fixed)[10]==biased[10]&&(*fixed)[14]==biased[14]);
    CHECK(RetargetDepthProjection(biased,right,right)==biased);
    auto unrelated=biased;unrelated[0]*=2;CHECK(!RetargetDepthProjection(unrelated,right,left));
    unrelated=biased;unrelated[11]=1;CHECK(!RetargetDepthProjection(unrelated,right,left));
    unrelated=biased;unrelated[14]=std::numeric_limits<float>::quiet_NaN();CHECK(!RetargetDepthProjection(unrelated,right,left));
    auto invalid=left;invalid[9]=std::numeric_limits<float>::infinity();CHECK(!RetargetDepthProjection(biased,right,invalid));
    auto weapon=right;weapon[0]*=3.3f;weapon[5]*=3.3f;weapon[10]=-1.5f;weapon[14]=-.2f;
    auto firstPerson=RetargetFirstPersonProjection(weapon,left);CHECK(firstPerson);
    CHECK((*firstPerson)[0]==left[0]&&(*firstPerson)[5]==left[5]&&(*firstPerson)[8]==left[8]&&(*firstPerson)[9]==left[9]);
    CHECK((*firstPerson)[10]==weapon[10]&&(*firstPerson)[14]==weapon[14]);
    auto bad=weapon;bad[15]=1;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[11]=1;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[3]=.1f;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[0]=0;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[10]=std::numeric_limits<float>::quiet_NaN();CHECK(!RetargetFirstPersonProjection(bad,left));
    // First-person world depth: the whole eye matrix, so gun clip Z equals world clip Z.
    auto full=FirstPersonEyeProjection(weapon,left);CHECK(full);CHECK(*full==left);
    for(float z:{-.05f,-.3f,-2.f}){float zl=0,ze=0,wl=0,we=0;std::array<float,4> point{.1f,-.05f,z,1.f};
        for(unsigned row=0;row<4;++row){zl+=point[row]*(*full)[row*4+2];ze+=point[row]*left[row*4+2];wl+=point[row]*(*full)[row*4+3];we+=point[row]*left[row*4+3];}
        CHECK(Near(zl/wl,ze/we));}
    CHECK(FirstPersonEyeProjection(weapon,right)==right);
    bad=weapon;bad[15]=1;CHECK(!FirstPersonEyeProjection(bad,left));
    bad=weapon;bad[0]=0;CHECK(!FirstPersonEyeProjection(bad,left));
    // General retarget against whole render blocks.
    std::array<float,kRenderBlockFloats> rightBlock{},leftBlock{};
    for(unsigned i=0;i<kRenderBlockFloats;++i){rightBlock[i]=float(i%7)*.5f;leftBlock[i]=rightBlock[i];}
    std::array<float,16> rightView{1,0,0,0,0,1,0,0,0,0,1,0,.032f,1.7f,4.f,1},leftView=rightView;leftView[12]=-.032f;
    for(unsigned i=0;i<16;++i){rightBlock[0x220/4+i]=rightView[i];leftBlock[0x220/4+i]=leftView[i];
        rightBlock[kRenderBlockProjection+i]=right[i];leftBlock[kRenderBlockProjection+i]=left[i];}
    // (a) exact block matrix copy: the projection and a non-projection matrix.
    CHECK(RetargetCameraBinding(right,rightBlock,leftBlock)==left);
    CHECK(RetargetCameraBinding(rightView,rightBlock,leftBlock)==leftView);
    // (b) depth-biased copy keeps its Z column.
    auto general=RetargetCameraBinding(biased,rightBlock,leftBlock);CHECK(general);
    CHECK((*general)[10]==biased[10]&&(*general)[14]==biased[14]&&(*general)[0]==left[0]&&(*general)[8]==left[8]&&(*general)[9]==left[9]);
    // Already-left and unrelated bindings are not retargeted to anything new.
    auto alreadyLeft=RetargetCameraBinding(left,rightBlock,leftBlock);CHECK(!alreadyLeft||*alreadyLeft==left);
    auto shadow=right;shadow[0]=.4f;shadow[5]=.4f;CHECK(!RetargetCameraBinding(shadow,rightBlock,leftBlock));
    unrelated=right;unrelated[3]=std::numeric_limits<float>::quiet_NaN();CHECK(!RetargetCameraBinding(unrelated,rightBlock,leftBlock));
    // A matrix shared by both eyes maps to itself (caller skips no-op writes).
    std::array<float,16> shared{};for(unsigned i=0;i<16;++i)shared[i]=rightBlock[i];CHECK(RetargetCameraBinding(shared,rightBlock,leftBlock)==shared);
    return 0;
}
