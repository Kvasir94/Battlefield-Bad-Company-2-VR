#include "Bc2DroppedFloor.h"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<__FILE__<<':'<<__LINE__<<": "<<#x<<std::endl;std::exit(1);}}while(0)
using namespace fvr::bc2;
namespace {
bool Near(float a,float b){return std::fabs(a-b)<1e-4f;}
// Run 211951: play-space floor 174.02, depth ground 173.71 under the mag.
int GroundWithinReachIsTheFloor(){
    DroppedFloor f;const DroppedFloorGround g{173.71f,2.f,-1.f};
    const auto c=f.Choose(0,0,174.02f,173.f,g);CHECK(c.source==DroppedFloorSource::Ground&&Near(c.y,173.71f));
    // Out of reach (another piece of ground): play space, uncorrected until an offset is seen.
    const auto far=f.Choose(10,0,174.02f,173.f,g);CHECK(far.source==DroppedFloorSource::PlaySpace&&Near(far.y,174.02f));
    return 0;
}
int OffsetCorrectsPlaySpaceWithoutAFreshSample(){
    DroppedFloor f;f.Observe(174.02f,DroppedFloorGround{173.71f,1,1});CHECK(f.Offset()&&Near(*f.Offset(),-.31f));
    // Up a staircase the play-space floor rises with the camera; the offset carries.
    const auto c=f.Choose(0,0,175.02f,174.f,std::nullopt);CHECK(c.source==DroppedFloorSource::Offset&&Near(c.y,174.71f));
    CHECK(f.Corrected(174.02f)&&Near(*f.Corrected(174.02f),173.71f));
    // No floor-relative space and no sample: the fixed fallback.
    const auto fixed=f.Choose(0,0,std::nullopt,173.f,std::nullopt);CHECK(fixed.source==DroppedFloorSource::Fixed&&Near(fixed.y,173.f));
    return 0;
}
int ImplausibleOffsetIsIgnored(){
    DroppedFloor f;f.Observe(174.f,DroppedFloorGround{170.f,0,0});CHECK(!f.Offset()&&f.Counts().rejected==1);
    f.Observe(std::nullopt,DroppedFloorGround{173.7f,0,0});CHECK(!f.Offset());
    CHECK(std::string_view(DroppedFloorLabel(true,DroppedFloorSource::Ground))=="eject_ground");
    return 0;
}
}
int main(){CHECK(GroundWithinReachIsTheFloor()==0);CHECK(OffsetCorrectsPlaySpaceWithoutAFreshSample()==0);CHECK(ImplausibleOffsetIsIgnored()==0);return 0;}
