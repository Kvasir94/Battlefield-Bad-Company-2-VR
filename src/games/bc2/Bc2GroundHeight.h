#pragma once
// Depth-derived ground height (FVR_BC2_GROUND_PROBE=1). NativeProbe reads the scene
// depth bound at the post-eye boundary back asynchronously, unprojects the ground a
// little ahead of the player with that frame's native view/projection, and publishes
// the result here. Native world is Y up; the canonical body-prop space negates Z only,
// so `y` is the same in both. Run 211951: the dropped mag rested at y 174.02 while
// the depth floor under it was 173.71.
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>

namespace fvr::bc2::ground {

struct Sample {
    float y=0,x=0,z=0;        // native world point on the ground
    float distance=0;          // horizontal distance from the camera that saw it
    std::int64_t observedNs=0; // QPC ns of the frame whose depth was read
    std::uint64_t frame=0;
};

// Candidate ground points: straight ahead (yaw only) at these distances, this far
// below the eye. The nearest one that lands on screen and unprojects to plausible
// ground wins. SP_02 eye height over the depth floor: 175.285 - 173.71 = 1.575 m.
inline constexpr std::array<float,6> CandidateDistances{.75f,1.f,1.5f,2.f,3.f,4.5f};
inline constexpr float AssumedEyeHeight=1.6f;

struct Pixel {float ndcX=0,ndcY=0;unsigned x=0,y=0;};

// Row-vector matrices (position*matrix), native right-handed view: clip.w = -viewZ.
inline std::optional<Pixel> ProjectToPixel(const math::Matrix4& view,const math::Matrix4& projection,
    float x,float y,float z,unsigned width,unsigned height)noexcept {
    const math::Matrix4 vp=interaction::Multiply(view,projection);const auto& m=vp.values;
    const float cx=x*m[0][0]+y*m[1][0]+z*m[2][0]+m[3][0],cy=x*m[0][1]+y*m[1][1]+z*m[2][1]+m[3][1],cw=x*m[0][3]+y*m[1][3]+z*m[2][3]+m[3][3];
    if(!(cw>.05f)||!width||!height)return {};
    const float nx=cx/cw,ny=cy/cw;if(!(std::abs(nx)<.95f)||!(std::abs(ny)<.95f))return {};
    Pixel p{nx,ny,unsigned((nx*.5f+.5f)*float(width)),unsigned((1-(ny*.5f+.5f))*float(height))};
    if(p.x>=width||p.y>=height)return {};
    return p;
}

// Native standard-Z depth (0 near, 1 far) at an NDC position back to native world.
inline std::optional<std::array<float,3>> Unproject(const math::Matrix4& view,const math::Matrix4& projection,
    float ndcX,float ndcY,float depth)noexcept {
    const auto& p=projection.values;
    if(!(depth>0)||!(depth<.99999f)||!(std::abs(p[0][0])>1e-4f)||!(std::abs(p[1][1])>1e-4f))return {};
    const float den=-depth-p[2][2];if(!(std::abs(den)>1e-7f))return {};
    const float vz=p[3][2]/den;if(!(vz<0))return {};  // in front of a right-handed camera
    const float vx=(ndcX*-vz-vz*p[2][0])/p[0][0],vy=(ndcY*-vz-vz*p[2][1])/p[1][1];
    const auto inv=interaction::InverseRigid(view);if(!inv)return {};const auto& i=inv->values;
    const std::array<float,3> w{vx*i[0][0]+vy*i[1][0]+vz*i[2][0]+i[3][0],vx*i[0][1]+vy*i[1][1]+vz*i[2][1]+i[3][1],vx*i[0][2]+vy*i[1][2]+vz*i[2][2]+i[3][2]};
    for(const float f:w)if(!std::isfinite(f))return {};
    return w;
}

// depthAt(x, y) returns the normalized depth at a pixel, or a negative value.
template<class DepthAt>
std::optional<Sample> GroundFromDepth(const math::Matrix4& view,const math::Matrix4& projection,unsigned width,unsigned height,DepthAt&& depthAt)noexcept {
    const auto inv=interaction::InverseRigid(view);if(!inv)return {};const auto& i=inv->values;
    const float camX=i[3][0],camY=i[3][1],camZ=i[3][2];
    float fx=-i[2][0],fz=-i[2][2];const float len=std::hypot(fx,fz);if(!(len>.05f))return {}; // looking straight up or down
    fx/=len;fz/=len;
    for(const float d:CandidateDistances){
        const float x=camX+fx*d,z=camZ+fz*d,y=camY-AssumedEyeHeight;
        const auto pixel=ProjectToPixel(view,projection,x,y,z,width,height);if(!pixel)continue;
        const float depth=depthAt(pixel->x,pixel->y);
        const auto w=Unproject(view,projection,pixel->ndcX,pixel->ndcY,depth);if(!w)continue;
        const float below=camY-(*w)[1],off=std::hypot((*w)[0]-x,(*w)[2]-z);
        // Ground, not a wall or a prop at chest height, and near the column we aimed at.
        if(below<.8f||below>3.f||off>(std::max)(.5f,d*.6f))continue;
        return Sample{(*w)[1],(*w)[0],(*w)[2],std::hypot((*w)[0]-camX,(*w)[2]-camZ),0,0};
    }
    return {};
}

// One writer (the render thread at the post-eye boundary), any number of readers.
struct Board {
    std::atomic<std::uint32_t> sequence{0};
    std::atomic<float> y{0},x{0},z{0},distance{0};
    std::atomic<std::int64_t> observedNs{0};std::atomic<std::uint64_t> frame{0};
};
inline Board board;

inline void Publish(const Sample& s)noexcept {
    board.sequence.fetch_add(1,std::memory_order_acq_rel);
    board.y.store(s.y,std::memory_order_relaxed);board.x.store(s.x,std::memory_order_relaxed);board.z.store(s.z,std::memory_order_relaxed);
    board.distance.store(s.distance,std::memory_order_relaxed);board.observedNs.store(s.observedNs,std::memory_order_relaxed);board.frame.store(s.frame,std::memory_order_relaxed);
    board.sequence.fetch_add(1,std::memory_order_release);
}

// The latest sample no older than maxAgeNs at `now` (QPC ns), or nothing.
inline std::optional<Sample> Latest(std::int64_t now,std::int64_t maxAgeNs)noexcept {
    for(int attempt=0;attempt<8;++attempt){
        const auto before=board.sequence.load(std::memory_order_acquire);if(before&1u)continue;
        Sample s{board.y.load(std::memory_order_relaxed),board.x.load(std::memory_order_relaxed),board.z.load(std::memory_order_relaxed),
            board.distance.load(std::memory_order_relaxed),board.observedNs.load(std::memory_order_relaxed),board.frame.load(std::memory_order_relaxed)};
        std::atomic_thread_fence(std::memory_order_acquire);
        if(board.sequence.load(std::memory_order_relaxed)!=before)continue;
        if(!before||!s.observedNs||now-s.observedNs<0||now-s.observedNs>maxAgeNs)return {};
        return s;
    }
    return {};
}

} // namespace fvr::bc2::ground
