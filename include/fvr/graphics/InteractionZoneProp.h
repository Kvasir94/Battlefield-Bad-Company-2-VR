#pragma once
#include "fvr/graphics/D3D11RigidPropRenderer.h"
#include <cmath>
namespace fvr::graphics {
// Built-in presentation geometry: three thin unit-radius rings. No game asset
// cache is required; callers scale it to a small cue inside their real zone.
inline constexpr RigidPropGeometryKey InteractionZonePropKey{0x4656525a4f4e45ull,1,1};
inline RigidPropSectionUpload BuildInteractionZoneProp() {
    RigidPropSectionUpload out;out.color={1,.65f,.1f,1};
    out.mesh.sourceVertexSkinHash=out.mesh.sourcePositionHash=InteractionZonePropKey.asset;
    constexpr unsigned segments=32;constexpr float pi=3.14159265358979323846f;
    const auto vertex=[](unsigned plane,float angle,float radius){
        RigidPropVertex v;float p[3]{};p[(plane+1)%3]=std::cos(angle)*radius;p[(plane+2)%3]=std::sin(angle)*radius;
        v.x=p[0];v.y=p[1];v.z=p[2];v.ny=1;return v;};
    for(unsigned plane=0;plane<3;++plane)for(unsigned n=0;n<segments;++n){
        const float a=2*pi*n/segments,b=2*pi*(n+1)/segments;
        const auto ia=vertex(plane,a,.96f),oa=vertex(plane,a,1),ib=vertex(plane,b,.96f),ob=vertex(plane,b,1);
        for(const auto v:{ia,oa,ob,ia,ob,ib})out.mesh.vertices.push_back(v);
    }
    return out;
}
} // namespace fvr::graphics
