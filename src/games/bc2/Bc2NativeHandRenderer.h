#pragma once
#include "Bc2NativeHandMesh.h"
#include "Bc2BodyAmmoRenderer.h"
#include <memory>

namespace fvr::bc2 {
struct NativeHandTextureMip {
    unsigned width=0,height=0;std::string sha256;std::vector<std::byte> rgba;
};
struct NativeHandTexture {
    std::string resource,sha256; // sha256 of the decoded RGBA bytes below
    unsigned width=0,height=0;
    std::vector<std::byte> rgba; // decoded private native diffuse, RGBA8 sRGB
    std::vector<NativeHandTextureMip> lowerMips; // complete authored chain, through 1x1
};
// Preserve the authored mip chain instead of sampling an unfiltered top level.
// Digest verification is performed over each level at GPU upload. Dimensions
// and lengths are checked first, without generating or guessing asset data.
inline bool NativeHandTextureShapeValid(const NativeHandTexture& texture)noexcept {
    if(texture.resource.empty()||!native_hand_detail::Digest(texture.sha256)||!texture.width||!texture.height||
       texture.width>4096||texture.height>4096||texture.lowerMips.size()>12||
       texture.rgba.size()!=std::size_t(texture.width)*texture.height*4)return false;
    auto width=texture.width,height=texture.height;
    for(const auto& mip:texture.lowerMips){
        if(width==1&&height==1)return false;
        width=std::max(1u,width/2);height=std::max(1u,height/2);
        if(mip.width!=width||mip.height!=height||!native_hand_detail::Digest(mip.sha256)||
           mip.rgba.size()!=std::size_t(width)*height*4)return false;
    }
    return width==1&&height==1;
}
// Real four-weight native-skeleton geometry draw, with a mod-owned shader/layout.
// It does not need the game's 35/44-entry section GPU mapping. It does require
// verified original corner UV/normal/material inputs and an exact final pose.
// Same graphics thread owns Initialize/Upload/Draw/destruction. No hooks, loader,
// arm suppression or gameplay state are installed by constructing this backend.
class Bc2NativeHandRenderer final {
public:
    Bc2NativeHandRenderer();~Bc2NativeHandRenderer();
    Bc2NativeHandRenderer(const Bc2NativeHandRenderer&)=delete;
    Bc2NativeHandRenderer& operator=(const Bc2NativeHandRenderer&)=delete;
    bool Initialize(ID3D11DeviceContext*);
    bool Upload(std::shared_ptr<const NativeHandGeometry>,const NativeHandTexture&);
    // Draw only after the owned eye's original draw/query scopes ended. Boundary
    // must identify the actual borrowed color/depth resources and canonical eye.
    // Opaque capped hands test AND write the verified scene depth. This supplies
    // self-occlusion and a matching depth result for later XR composition. The
    // adapter must remove the exact original arm/glove color and depth geometry
    // as one replacement decision; color-only replacement is not supported.
    // Only tracked sides are submitted.
    // The first valid eye selects one copied pose for BOTH eyes of the pair.
    // Animation provenance is not relabelled to either native eye/frame. Caller
    // revalidates live owner/equipment/tracking before each draw.
    bool Draw(const NativeHandPose&,const graphics::RigidPropEye& current,
        std::int64_t now,const BodyAmmoBoundaryProof&);
    bool Ready()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace fvr::bc2
