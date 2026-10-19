#pragma once
#include "Bc2Rig.h"
#include "Bc2SightContact.h"
#include "fvr/graphics/RigidPropFrame.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fvr::bc2 {
// An independent mesh on the ORIGINAL native skeleton. No arm geometry,
// inverse-bind redesign, wrist offsets, procedural fingers or gameplay state.
// Private asset readers supply measured native attributes, never game bytes in git.
struct NativeHandInfluence {std::uint32_t skeletonBone=0;float weight=0;};
struct NativeHandPoint {
    std::array<float,3> nativePositionMeters{};
    std::array<NativeHandInfluence,4> influences{};
};
struct NativeHandCorner {
    unsigned point=0;
    std::array<float,3> nativeNormal{};
    std::array<float,2> uv{};
};
struct NativeHandTriangle {std::array<NativeHandCorner,3> corners{};bool wristCap=false;};
struct NativeHandAssetInput {
    std::string archiveSha256,exportSha256,material,diffuseResource,diffuseSha256;
    std::uint64_t rigFingerprint=0;
    // Archive/export coordinates: right +X, up +Y, back +Z, metres.
    // UV/normal and material bindings require an asset-reader receipt. The
    // existing export's CandidateNativeUV fields do NOT satisfy these flags.
    bool cornerAttributesVerified=false,materialBindingVerified=false;
    std::array<std::vector<NativeHandPoint>,2> points;
    std::array<std::vector<NativeHandTriangle>,2> triangles;
};
struct NativeHandVertex {
    std::array<float,3> position{},normal{};
    std::array<float,2> uv{};
    std::array<std::uint32_t,4> bones{}; // Our 32-entry palette, NOT game section IDs.
    std::array<float,4> weights{};
};
static_assert(sizeof(NativeHandVertex)==64);
struct NativeHandGeometry {
    NativeHandAssetInput provenance; // Retained private provenance, not runtime traces.
    std::array<std::uint32_t,32> skeletonBones{}; // wrist +15 fingers, per side
    std::vector<NativeHandVertex> vertices; // per-corner seams preserved
    std::array<unsigned,2> first{},count{},capTriangles{};
};
namespace native_hand_detail {
inline bool Digest(std::string_view s)noexcept {
    return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
inline std::optional<std::array<std::uint32_t,32>> Bones(const RigSnapshot& rig){
    if(!rig.identity.count||rig.identity.count>1024||rig.names.size()!=rig.identity.count||
       rig.parents.size()!=rig.identity.count||rig.inverseBind.size()!=rig.identity.count)return {};
    // Reject cycles and malformed ancestry, including unrelated branches.
    for(unsigned n=0;n<rig.parents.size();++n){auto at=std::int32_t(n);unsigned steps=0;
        while(at!=-1){if(at<0||std::size_t(at)>=rig.parents.size()||++steps>rig.parents.size())return {};at=rig.parents[at];}}
    const auto named=[&](const std::string& name)->std::optional<unsigned>{
        const auto at=std::find(rig.names.begin(),rig.names.end(),name);
        if(at==rig.names.end()||std::find(at+1,rig.names.end(),name)!=rig.names.end())return {};
        return unsigned(at-rig.names.begin());};
    std::array<std::uint32_t,32> out{};
    for(unsigned side=0;side<2;++side){const std::string wrist=side?"RightHand":"LeftHand";
        const auto root=named(wrist);if(!root)return {};out[side*16]=*root;unsigned slot=side*16+1;
        for(const char* finger:{"Thumb","Index","Middle","Ring","Pinky"}){auto parent=*root;
            for(unsigned joint=1;joint<=3;++joint){const auto bone=named(wrist+finger+std::to_string(joint));
                if(!bone||rig.parents[*bone]!=std::int32_t(parent)||
                   !interaction::InverseAnimatedTransform(rig.inverseBind[*bone]))return {};
                out[slot++]=*bone;parent=*bone;}}
        if(!interaction::InverseAnimatedTransform(rig.inverseBind[*root]))return {};
    }
    return out;
}
inline math::Matrix4 CanonicalSkin(const std::array<std::byte,64>& bytes)noexcept {
    // Exactly the established Bc2Rig.cpp palette convention. SIMD padding can
    // contain metadata: only xyz columns are scalar data; synthesize column4.
    math::Matrix4 m{};for(unsigned row=0;row<4;++row){std::memcpy(m.values[row].data(),bytes.data()+row*16,12);m.values[row][3]=row==3?1.f:0.f;}
    for(unsigned i=0;i<4;++i){m.values[2][i]=-m.values[2][i];m.values[i][2]=-m.values[i][2];}return m;
}
}
inline std::optional<NativeHandGeometry> BuildNativeHandGeometry(const NativeHandAssetInput& asset,const RigSnapshot& rig){
    using namespace native_hand_detail;
    const auto bones=Bones(rig);
    if(!bones||!asset.rigFingerprint||asset.rigFingerprint!=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)||
       !Digest(asset.archiveSha256)||!Digest(asset.exportSha256)||!Digest(asset.diffuseSha256)||
       asset.material.empty()||asset.material.size()>512||asset.diffuseResource.empty()||asset.diffuseResource.size()>512||
       !asset.cornerAttributesVerified||!asset.materialBindingVerified)return {};
    NativeHandGeometry out;out.provenance=asset;out.skeletonBones=*bones;
    for(unsigned side=0;side<2;++side){const auto& points=asset.points[side];const auto& triangles=asset.triangles[side];
        if(points.empty()||points.size()>65536||triangles.empty()||triangles.size()>16384)return {};
        out.first[side]=unsigned(out.vertices.size());
        std::vector<NativeHandVertex> base(points.size());
        for(unsigned n=0;n<points.size();++n){const auto& p=points[n];auto& v=base[n];
            for(unsigned axis=0;axis<3;++axis){if(!std::isfinite(p.nativePositionMeters[axis])||std::abs(p.nativePositionMeters[axis])>10)return {};
                v.position[axis]=p.nativePositionMeters[axis]*(axis==2?-1.f:1.f);}
            double sum=0;for(unsigned influence=0;influence<4;++influence){const auto& w=p.influences[influence];
                if(!std::isfinite(w.weight)||w.weight<0||w.weight>1)return {};if(!w.weight)continue;
                const auto begin=out.skeletonBones.begin()+side*16,end=begin+16;
                const auto bone=std::find(begin,end,w.skeletonBone);if(bone==end)return {}; // arm/opposite hand/weapon reject
                for(unsigned prior=0;prior<influence;++prior)if(p.influences[prior].weight&&p.influences[prior].skeletonBone==w.skeletonBone)return {};
                v.bones[influence]=unsigned(bone-out.skeletonBones.begin());v.weights[influence]=w.weight;sum+=w.weight;}
            if(std::abs(sum-1)>1e-5)return {}; // preserve weights, never fold arm weights at runtime
        }
        std::map<std::pair<unsigned,unsigned>,std::pair<unsigned,int>> edges;double nativeVolume6=0;
        for(const auto& triangle:triangles){
            for(const auto& corner:triangle.corners)if(corner.point>=points.size())return {};
            for(unsigned k=0;k<3;++k){const auto a=triangle.corners[k].point,b=triangle.corners[(k+1)%3].point;
                if(a==b)return {};auto& edge=edges[{std::min(a,b),std::max(a,b)}];++edge.first;edge.second+=a<b?1:-1;}
            const auto& a=base[triangle.corners[0].point].position;const auto& b=base[triangle.corners[1].point].position;
            const auto& c=base[triangle.corners[2].point].position;
            // Canonical reflection negates signed volume. Source winding must
            // enclose a positive native volume, as the private export audits do.
            nativeVolume6-=double(a[0])*(double(b[1])*c[2]-double(b[2])*c[1])+
                double(a[1])*(double(b[2])*c[0]-double(b[0])*c[2])+double(a[2])*(double(b[0])*c[1]-double(b[1])*c[0]);
            const double x=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]),
                y=(b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]),
                z=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
            if(x*x+y*y+z*z<1e-20)return {};
            // RH -> LH reflection also reverses winding. Preserve original
            // corner normal/UV seams; do not rebuild glove normals from faces.
            for(unsigned k:{0u,2u,1u}){const auto& corner=triangle.corners[k];auto v=base[corner.point];
                double length=0;for(unsigned axis=0;axis<3;++axis){const auto n=corner.nativeNormal[axis];
                    if(!std::isfinite(n))return {};length+=double(n)*n;v.normal[axis]=n*(axis==2?-1.f:1.f);}
                if(std::abs(length-1)>1e-3)return {};
                for(unsigned axis=0;axis<2;++axis){if(!std::isfinite(corner.uv[axis])||std::abs(corner.uv[axis])>16)return {};v.uv[axis]=corner.uv[axis];}
                if(triangle.wristCap)for(unsigned influence=0;influence<4;++influence)
                    if(v.weights[influence]&&v.bones[influence]!=side*16)return {};
                out.vertices.push_back(v);}
            if(triangle.wristCap)++out.capTriangles[side];
        }
        // Closed cuffs and outward, consistently oriented surfaces. Counts or a
        // cap tag alone cannot make an open sleeve into independent geometry.
        for(const auto& [key,edge]:edges)if(edge.first!=2||edge.second!=0)return {};
        if(!out.capTriangles[side]||!std::isfinite(nativeVolume6)||nativeVolume6<=1e-15)return {};
        out.count[side]=unsigned(out.vertices.size())-out.first[side];
    }
    // Keep identities/receipts, not a second full copy of private mesh input.
    for(unsigned side=0;side<2;++side){out.provenance.points[side].clear();out.provenance.triangles[side].clear();}
    return out;
}
// Animation/input provenance is deliberately independent of a render eye or
// native render-frame number. A copied pose is selected once for a stereo pair.
struct NativeHandPoseSource {
    std::uint64_t actor=0,weak=0,actorGeneration=0,weapon=0,equipmentGeneration=0,space=0,input=0,serial=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const NativeHandPoseSource&)const=default;
};
inline bool NativeHandSourceFresh(const NativeHandPoseSource& s,std::int64_t now)noexcept {
    return s.actor&&s.weak&&s.actorGeneration&&s.weapon&&s.equipmentGeneration&&s.space&&s.input&&s.serial&&
        s.observedNs>0&&s.observedNs<=now&&now<s.deadlineNs&&s.deadlineNs-s.observedNs<=250000000;
}
inline bool NativeHandSameOwner(const NativeHandPoseSource& a,const NativeHandPoseSource& b)noexcept {
    return a.actor==b.actor&&a.weak==b.weak&&a.actorGeneration==b.actorGeneration&&a.weapon==b.weapon&&
        a.equipmentGeneration==b.equipmentGeneration&&a.space==b.space;
}
struct NativeHandPose {
    NativeHandPoseSource source{};
    std::uint64_t rigFingerprint=0;
    std::array<std::uint32_t,32> skeletonBones{};
    std::array<math::Matrix4,32> skin{};
    std::array<bool,2> tracked{};
};
struct NativeHandPoseLayout {
    RigIdentity rig{};std::uint64_t fingerprint=0;std::array<std::uint32_t,32> bones{};
};
inline std::optional<NativeHandPoseLayout> NativeHandLayout(const RigSnapshot& rig){
    const auto bones=native_hand_detail::Bones(rig);if(!bones)return {};
    const auto fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);if(!fingerprint)return {};
    return NativeHandPoseLayout{rig.identity,fingerprint,*bones};
}
// Pass the immutable FINAL private palette after contact/reload/finger layers.
// A preceding native animation snapshot is not equivalent to the packed pose.
// No shoulder reach, fresh input, authored contact or ownership is generated here.
inline std::optional<NativeHandPose> BuildNativeHandPose(const NativeHandPoseLayout& layout,
    std::span<const std::array<std::byte,64>> finalNativeSkin,const NativeHandPoseSource& source,
    std::array<bool,2> tracked,std::int64_t now){
    if(!NativeHandSourceFresh(source,now)||(!tracked[0]&&!tracked[1])||finalNativeSkin.size()!=layout.rig.count||
       source.actor!=layout.rig.soldier||source.weak!=layout.rig.weak||!layout.fingerprint)return {};
    NativeHandPose out;out.source=source;out.rigFingerprint=layout.fingerprint;out.skeletonBones=layout.bones;out.tracked=tracked;
    for(unsigned n=0;n<32;++n){if(layout.bones[n]>=finalNativeSkin.size())return {};
        out.skin[n]=native_hand_detail::CanonicalSkin(finalNativeSkin[layout.bones[n]]);
        // Proximal arm collapse is neither needed nor accepted as replacement
        // geometry. A hidden/degenerate hand cannot authorize a separate draw.
        if(!interaction::InverseAnimatedTransform(out.skin[n]))return {};}
    return out;
}
inline std::optional<NativeHandPose> BuildNativeHandPose(const NativeHandGeometry& mesh,const RigSnapshot& rig,
    std::span<const std::array<std::byte,64>> finalNativeSkin,const NativeHandPoseSource& source,
    std::array<bool,2> tracked,std::int64_t now){
    const auto layout=NativeHandLayout(rig);if(!layout)return {};
    auto pose=BuildNativeHandPose(*layout,finalNativeSkin,source,tracked,now);
    if(!pose||pose->rigFingerprint!=mesh.provenance.rigFingerprint||pose->skeletonBones!=mesh.skeletonBones)return {};
    return pose;
}
inline bool NativeHandPoseForEye(const NativeHandPose& pose,const graphics::RigidPropEye& eye,std::int64_t now)noexcept {
    const auto& s=pose.source;
    return NativeHandSourceFresh(s,now)&&graphics::RigidPropEyeFresh(eye,now)&&pose.rigFingerprint&&
        (pose.tracked[0]||pose.tracked[1])&&s.actor==eye.actor&&s.actorGeneration==eye.actorGeneration&&
        s.weapon==eye.weapon&&s.equipmentGeneration==eye.equipmentGeneration&&s.space==eye.space&&s.input<=eye.input;
}
// Graphics-thread owned. One owned value, not one animation producer per eye.
// Current owner/tracking must still be validated by the adapter before each draw.
class NativeHandStereoPose {
public:
    const NativeHandPose* Select(const NativeHandPose& candidate,const graphics::RigidPropEye& eye,std::int64_t now)noexcept {
        if(!NativeHandPoseForEye(candidate,eye,now))return nullptr;
        const bool samePair=pose_&&world_==eye.world&&request_==eye.request&&frame_==eye.frame;
        if(!samePair){
            // Do not resurrect an older pair from a delayed eye callback.
            if(pose_&&world_==eye.world&&request_==eye.request&&frame_>=eye.frame)return nullptr;
            pose_=candidate;world_=eye.world;request_=eye.request;frame_=eye.frame;
        }
        if(!NativeHandSameOwner(pose_->source,candidate.source)||pose_->tracked!=candidate.tracked||
           pose_->rigFingerprint!=candidate.rigFingerprint||pose_->skeletonBones!=candidate.skeletonBones||
           candidate.source.input<pose_->source.input||!NativeHandPoseForEye(*pose_,eye,now))return nullptr;
        return &*pose_;
    }
    void Reset()noexcept{pose_.reset();world_=request_=frame_=0;}
private:
    std::optional<NativeHandPose> pose_;std::uint64_t world_=0,request_=0,frame_=0;
};
// Reference CPU deformation for focused source tests. Production uses the same
// four ORIGINAL native weights in the dedicated vertex shader.
inline math::Vec3 SkinNativeHandPoint(const NativeHandVertex& vertex,const NativeHandPose& pose)noexcept {
    math::Vec3 out{};std::array<float*,3> dst{&out.x,&out.y,&out.z};
    for(unsigned i=0;i<4;++i)if(vertex.weights[i]&&vertex.bones[i]<32)
        for(unsigned c=0;c<3;++c){const auto& m=pose.skin[vertex.bones[i]];double value=m.values[3][c];
            for(unsigned r=0;r<3;++r)value+=double(vertex.position[r])*m.values[r][c];*dst[c]+=float(value*vertex.weights[i]);}
    return out;
}
} // namespace fvr::bc2
