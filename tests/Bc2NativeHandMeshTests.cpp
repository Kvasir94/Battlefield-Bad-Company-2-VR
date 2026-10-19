// Synthetic geometry/poses only. No BC2 assets or authored game samples.
#include "Bc2NativeHandMesh.h"
#include "Bc2NativeHandRenderer.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
using namespace fvr;using namespace fvr::bc2;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
math::Matrix4 Pose(float angle,float x,float y,float z){auto m=Identity();const auto c=std::cos(angle),s=std::sin(angle);
    m.values[0][0]=c;m.values[0][2]=s;m.values[2][0]=-s;m.values[2][2]=c;m.values[3]={x,y,z,1};return m;}
std::array<std::byte,64> Native(const math::Matrix4& canonical){auto m=canonical;
    for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}
    std::array<std::byte,64> b{};std::memcpy(b.data(),&m,64);
    // Padding deliberately is non-float data, including NaNs. Never read it.
    for(unsigned row=0;row<4;++row){const std::uint32_t pad=0x7fffffff;std::memcpy(b.data()+row*16+12,&pad,4);}return b;}
RigSnapshot Rig(){RigSnapshot r;r.names={"root"};r.parents={-1};
    for(const char* side:{"Left","Right"}){const auto arm=unsigned(r.names.size());r.names.push_back(std::string(side)+"ForeArm");r.parents.push_back(0);
        const auto wrist=unsigned(r.names.size());r.names.push_back(std::string(side)+"Hand");r.parents.push_back(int(arm));
        for(const char* finger:{"Thumb","Index","Middle","Ring","Pinky"}){auto parent=wrist;
            for(unsigned joint=1;joint<=3;++joint){const auto n=unsigned(r.names.size());r.names.push_back(std::string(side)+"Hand"+finger+std::to_string(joint));r.parents.push_back(int(parent));parent=n;}}}
    r.identity.count=unsigned(r.names.size());r.identity.soldier=0x100000;r.identity.weak=0x100100;r.inverseBind.assign(r.names.size(),Identity());return r;}
NativeHandAssetInput Asset(const RigSnapshot& r){NativeHandAssetInput a;
    a.archiveSha256=std::string(64,'a');a.exportSha256=std::string(64,'b');a.diffuseSha256=std::string(64,'c');
    a.material="synthetic-material";a.diffuseResource="synthetic-diffuse";a.rigFingerprint=SightRigFingerprint(r.names,r.parents,r.inverseBind);
    a.cornerAttributesVerified=a.materialBindingVerified=true;const auto bones=*native_hand_detail::Bones(r);
    for(unsigned side=0;side<2;++side){const float x=side?.4f:-.4f;
        for(const auto& p:std::array<std::array<float,3>,4>{{{x,1,0},{x+.04f,1,0},{x,1,.04f},{x,1.04f,0}}}){
            NativeHandPoint point;point.nativePositionMeters=p;point.influences[0]={bones[side*16],1};a.points[side].push_back(point);}
        a.points[side][3].influences={NativeHandInfluence{bones[side*16],.25f},NativeHandInfluence{bones[side*16+1],.75f},NativeHandInfluence{},NativeHandInfluence{}};
        // Closed tetrahedron. First triangle is a wrist-only cap.
        for(const auto indices:std::array<std::array<unsigned,3>,4>{{{0,1,2},{0,3,1},{1,3,2},{2,3,0}}}){
            NativeHandTriangle t;t.wristCap=a.triangles[side].empty();for(unsigned k=0;k<3;++k)
                t.corners[k]={indices[k],{0,1,0},{float(k)*.25f,float(side)*.5f}};a.triangles[side].push_back(t);}}
    return a;}
graphics::RigidPropEye Eye(const RigSnapshot& r){return {1,2,3,4,r.identity.soldier,6,7,8,9,10,0,100,1000};}
NativeHandPoseSource Source(const RigSnapshot& r){return {r.identity.soldier,r.identity.weak,6,7,8,9,10,11,100,1000};}
void Near(math::Vec3 a,math::Vec3 b){CHECK(std::abs(a.x-b.x)<1e-5f&&std::abs(a.y-b.y)<1e-5f&&std::abs(a.z-b.z)<1e-5f);}
math::Vec3 Transform(math::Vec3 p,const math::Matrix4& m){math::Vec3 o{};float* v[]{&o.x,&o.y,&o.z};for(unsigned c=0;c<3;++c)*v[c]=p.x*m.values[0][c]+p.y*m.values[1][c]+p.z*m.values[2][c]+m.values[3][c];return o;}
}
int main(){const auto rig=Rig();const auto asset=Asset(rig);const auto mesh=BuildNativeHandGeometry(asset,rig);CHECK(mesh);
    NativeHandTexture texture;texture.resource="synthetic-diffuse";texture.sha256=std::string(64,'a');
    texture.width=4;texture.height=2;texture.rgba.resize(4*2*4);
    texture.lowerMips={{2,1,std::string(64,'b'),std::vector<std::byte>(8)},
                       {1,1,std::string(64,'c'),std::vector<std::byte>(4)}};
    CHECK(NativeHandTextureShapeValid(texture));
    for(unsigned failure=0;failure<7;++failure){auto badTexture=texture;
        if(failure==0)badTexture.lowerMips.pop_back();
        if(failure==1)badTexture.lowerMips[0].width=4;
        if(failure==2)badTexture.lowerMips[1].rgba.pop_back();
        if(failure==3)badTexture.lowerMips.push_back(badTexture.lowerMips.back());
        if(failure==4)badTexture.lowerMips[0].sha256="candidate";
        if(failure==5)badTexture.rgba.pop_back();
        if(failure==6)badTexture.width=8192;
        CHECK(!NativeHandTextureShapeValid(badTexture));}
    CHECK(mesh->vertices.size()==24&&mesh->first[1]==12&&mesh->count[0]==12&&mesh->count[1]==12);
    CHECK(mesh->capTriangles[0]==1&&mesh->capTriangles[1]==1);
    // Reflection and winding reverse together; preserve exact corner seams.
    CHECK(mesh->vertices[1].position[2]==-.04f&&mesh->vertices[1].uv==asset.triangles[0][0].corners[2].uv);
    CHECK(mesh->vertices[2].uv==asset.triangles[0][0].corners[1].uv);
    // Bilateral wrists and ALL 15 fingers retain exact source palette matrices.
    std::vector<std::array<std::byte,64>> palette(rig.identity.count,Native(Identity()));
    for(unsigned n=0;n<32;++n)palette[mesh->skeletonBones[n]]=Native(Pose(float(n)*.03f,float(n)*.01f,.2f,.3f));
    const auto inputBefore=palette;auto owner=Source(rig);const auto pose=BuildNativeHandPose(*mesh,rig,palette,owner,{true,true},200);CHECK(pose);
    CHECK(palette==inputBefore);
    for(unsigned n=0;n<32;++n){const auto expected=Pose(float(n)*.03f,float(n)*.01f,.2f,.3f);
        CHECK(pose->skin[n].values==expected.values);}
    // Mixed native weights really deform the original geometry. They are not a
    // rigid wrist proxy: changing the thumb matrix moves its weighted point.
    const auto finger=mesh->vertices[5];CHECK(finger.weights[0]==.25f&&finger.weights[1]==.75f);
    const auto point=SkinNativeHandPoint(finger,*pose);auto changed=*pose;changed.skin[finger.bones[1]].values[3][0]+=.1f;
    const auto moved=SkinNativeHandPoint(finger,changed);CHECK(std::abs(moved.x-point.x-.075f)<1e-6f);
    // Common player/world transform, including 180-degree inversion, preserves
    // every same-frame wrist/finger/contact relationship and weighted point.
    for(float angle:{.7f,3.14159265358979323846f}){const auto world=Pose(angle,1,-2,.5f);auto transformed=*pose;
        for(unsigned n=0;n<32;++n)transformed.skin[n]=interaction::Multiply(pose->skin[n],world);
        for(const auto& v:mesh->vertices)Near(SkinNativeHandPoint(v,transformed),Transform(SkinNativeHandPoint(v,*pose),world));
        for(unsigned side=0;side<2;++side){const auto oldInverse=interaction::InverseAnimatedTransform(pose->skin[side*16]);
            const auto newInverse=interaction::InverseAnimatedTransform(transformed.skin[side*16]);CHECK(oldInverse&&newInverse);
            for(unsigned fingerIndex=1;fingerIndex<16;++fingerIndex){const auto old=interaction::Multiply(pose->skin[side*16+fingerIndex],*oldInverse);
                const auto fresh=interaction::Multiply(transformed.skin[side*16+fingerIndex],*newInverse);
                for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)CHECK(std::abs(old.values[r][c]-fresh.values[r][c])<1e-5f);}}}
    // Current owner/epoch/deadline are caller-owned and are never invented.
    auto wrong=owner;wrong.actor++;CHECK(!BuildNativeHandPose(*mesh,rig,palette,wrong,{true,true},200));
    CHECK(!BuildNativeHandPose(*mesh,rig,palette,owner,{true,true},1000));CHECK(!BuildNativeHandPose(*mesh,rig,palette,owner,{false,false},200));
    auto collapsed=palette;collapsed[mesh->skeletonBones[0]]={};CHECK(!BuildNativeHandPose(*mesh,rig,collapsed,owner,{true,true},200));
    auto otherRig=rig;otherRig.inverseBind[2].values[3][0]=.1f;CHECK(!BuildNativeHandPose(*mesh,otherRig,palette,owner,{true,true},200));
    // Freeze final post-fallback bytes once for both views. Newer animation
    // arriving between eyes cannot produce stereo-disagreeing fingers/wrists.
    auto eye0=Eye(rig),eye1=eye0;eye1.eye=1;eye1.view=30;
    NativeHandStereoPose pair;const auto* first=pair.Select(*pose,eye0,200);CHECK(first);
    const auto firstSkin=first->skin;auto newer=*pose;newer.source.serial++;newer.source.input++;
    newer.skin[1].values[3][0]+=.25f;eye1.input++;
    const auto* second=pair.Select(newer,eye1,201);CHECK(second);
    for(unsigned n=0;n<32;++n)CHECK(second->skin[n].values==firstSkin[n].values);
    CHECK(second->source.input==pose->source.input&&second->source.serial==pose->source.serial);
    // Eye projections remain independent; only the animation data is shared.
    auto projection=Identity();projection.values[2][3]=1;
    const auto clip0=graphics::RigidPropClipTransform(Identity(),Pose(0,-.03f,0,0),projection);
    const auto clip1=graphics::RigidPropClipTransform(Identity(),Pose(0,.03f,0,0),projection);
    CHECK(clip0&&clip1&&clip0->values!=clip1->values);
    auto nextEye=eye0;nextEye.frame++;nextEye.input++;
    CHECK(pair.Select(newer,nextEye,202)->skin[1].values==newer.skin[1].values);
    CHECK(!pair.Select(*pose,eye0,203)); // delayed previous frame
    for(unsigned scenario=0;scenario<8;++scenario){NativeHandStereoPose frozen;CHECK(frozen.Select(*pose,eye0,200));
        auto candidate=newer;auto eye=eye1;
        if(scenario==0){candidate.source.equipmentGeneration++;eye.equipmentGeneration++;}
        if(scenario==1){candidate.source.actorGeneration++;eye.actorGeneration++;}
        if(scenario==2){candidate.source.space++;eye.space++;}
        if(scenario==3)candidate.tracked[0]=false;
        if(scenario==4)candidate.source.weak++;
        if(scenario==5)candidate.skeletonBones[0]++;
        if(scenario==6){candidate.source.observedNs=999;candidate.source.deadlineNs=2000;eye.observedNs=999;eye.deadlineNs=2000;}
        if(scenario==7)candidate.source.input=9;
        CHECK(!frozen.Select(candidate,eye,scenario==6?1001:201));}
    // The producer owns its copied final palette; later source mutation cannot
    // change a retained pose, and no native-frame relabelling is involved.
    auto finalPalette=palette;finalPalette[mesh->skeletonBones[1]]=Native(Pose(.4f,2,3,4));
    const auto final=BuildNativeHandPose(*NativeHandLayout(rig),finalPalette,owner,{true,true},200);CHECK(final);
    finalPalette[mesh->skeletonBones[1]]=Native(Identity());CHECK(final->skin[1].values==Pose(.4f,2,3,4).values);
    // No guessed materials/caps, proximal weights, cross-hand weights, silent
    // normalization, duplicate influences, degenerate triangles or bad topology.
    auto bad=asset;bad.cornerAttributesVerified=false;CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.materialBindingVerified=false;CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.diffuseSha256="candidate";CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.points[0][3].influences[1].skeletonBone=1;CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.points[0][3].influences[1].skeletonBone=mesh->skeletonBones[16];CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.points[0][3].influences[1].weight=.6f;CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.points[0][3].influences[1].skeletonBone=mesh->skeletonBones[0];CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.points[0][0].influences[0].skeletonBone=mesh->skeletonBones[1];CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;for(auto& t:bad.triangles[0])t.wristCap=false;CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.triangles[1][0].corners[0].point=100000;CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.triangles[1][0].corners[1].point=bad.triangles[1][0].corners[0].point;CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.triangles[1].pop_back();CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;for(auto& t:bad.triangles[0])std::swap(t.corners[1],t.corners[2]);CHECK(!BuildNativeHandGeometry(bad,rig));
    bad=asset;bad.triangles[1][0].corners[1].uv[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!BuildNativeHandGeometry(bad,rig));
    auto cyclic=rig;cyclic.parents[1]=1;CHECK(!BuildNativeHandGeometry(asset,cyclic));
    std::puts("Bc2NativeHandMesh: bilateral native skinning, 15-finger palette preservation, seams/winding, inversion equivariance and identity/asset rejection passed (synthetic only).");
}
