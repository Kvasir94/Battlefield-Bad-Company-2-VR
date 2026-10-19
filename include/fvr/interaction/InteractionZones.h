#pragma once
// Interaction zones, capture/release hysteresis and soft contact magnetism
// (docs/INTERACTION-POLISH-20261008.md). One table holds every grab range the
// hand interactions use, so ranges are tuned in one place and consistently.
//
// Values lean toward KF2 VR / AS2 generosity (KF2-reload-and-BC2-env-report.md,
// FrostbiteVR-vs-KF2VR-AS2-reload-assessment.md): AS2 pouch r 0.20 m, magwell
// capture 65 deg / keep 85 deg, two-hand engage 25 ms / release 100 ms, pickup
// blend 80 ms. Capture is where a fresh grip takes the part; release is where
// a held part lets go (always larger, so a grab never flickers at the edge).
// Magnetism: inside a held zone the visible hand snaps to the authored contact
// pose with a short eased blend, and on release eases back to the tracked hand.
// Policy and presentation only: no ammunition, claims, native state or input.
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace fvr::interaction {
enum class InteractionZoneKind : std::uint8_t {
    Support,        // fore-end / support grip (left hand on the gun)
    MagazineGrab,   // seated magazine taken out of the well
    MagazineInsert, // held magazine captured by the magwell rail
    ChargingHandle, // charging handle / bolt grab and its lateral breakaway
    Pouch,          // body ammunition pouch
    Holster,        // shoulder/chest body slots
    GroundPickup,   // world items (no BC2 runtime adapter yet; range only)
    Count
};
struct InteractionZone {
    float captureMeters=0,releaseMeters=0;
    float captureRadians=0,releaseRadians=0; // 0 = no angular gate
    float gripOn=.6f,gripOff=.35f;           // squeeze thresholds (hysteresis)
    std::int64_t magnetInNs=0,magnetOutNs=0; // visible hand snap blend in/out
    bool hoverCue=false;                     // haptic tick on entering the capture zone
};
struct InteractionZoneTable {
    std::array<InteractionZone,std::size_t(InteractionZoneKind::Count)> zones{};
    constexpr const InteractionZone& operator[](InteractionZoneKind k)const noexcept{return zones[std::size_t(k)];}
    constexpr InteractionZone& operator[](InteractionZoneKind k)noexcept{return zones[std::size_t(k)];}
};
// The tuning table. Previous hard-coded values are in the comments.
constexpr InteractionZoneTable DefaultInteractionZones()noexcept {
    InteractionZoneTable t;using K=InteractionZoneKind;
    //                    capture release  capRad  relRad  on   off   in(ns)    out(ns)    cue
    t[K::Support]       ={.20f,  .32f,    0,      0,      .6f, .35f, 60000000, 100000000, false}; // was .16/.28, on .7
    t[K::MagazineGrab]  ={.12f,  .18f,    0,      0,      .6f, .35f, 50000000, 100000000, true};  // was .09 grab, .15 pull breakaway
    t[K::MagazineInsert]={.08f,  .16f,    1.134464f,1.48353f,.6f,.35f,0,     0,         false}; // was .07/.15, 45/75 deg (AS2 65/85)
    t[K::ChargingHandle]={.10f,  .12f,    0,      0,      .6f, .35f, 40000000, 100000000, true};  // profile grab radius raised to at least this
    t[K::Pouch]         ={.20f,  .26f,    0,      0,      .6f, .35f, 80000000, 100000000, true};  // was .18 (AS2 belt r .20)
    t[K::Holster]       ={.20f,  .26f,    0,      0,      .6f, .35f, 80000000, 100000000, true};  // was .16 chest / .17 shoulders
    t[K::GroundPickup]  ={.25f,  .35f,    0,      0,      .6f, .35f, 80000000, 120000000, true};  // no runtime adapter yet
    return t;
}
// Uniform distance scale (angles and timing unchanged), clamped to 0.5..2.
constexpr InteractionZoneTable ScaleInteractionZones(InteractionZoneTable t,float scale)noexcept {
    if(!(scale>=.5f&&scale<=2.f))return t;
    for(auto& z:t.zones){z.captureMeters*=scale;z.releaseMeters*=scale;}
    return t;
}
constexpr bool ValidInteractionZone(const InteractionZone& z)noexcept {
    return z.captureMeters>0&&z.releaseMeters>z.captureMeters&&z.releaseMeters<=.7f&&
        z.captureRadians>=0&&(z.captureRadians==0||(z.releaseRadians>z.captureRadians&&z.releaseRadians<1.5707963f))&&
        z.gripOff>=0&&z.gripOn>z.gripOff&&z.gripOn<=1&&z.magnetInNs>=0&&z.magnetOutNs>=0&&
        z.magnetInNs<=500000000&&z.magnetOutNs<=500000000;
}
constexpr bool ValidInteractionZones(const InteractionZoneTable& t)noexcept {
    for(const auto& z:t.zones)if(!ValidInteractionZone(z))return false;return true;
}
static_assert(ValidInteractionZones(DefaultInteractionZones()));

// Distance (and optional angle) hysteresis for one zone. Capture needs the
// hand inside the capture radius/cone; once inside, it stays until it leaves
// the larger release radius/cone. Non-finite input leaves the zone.
class ZoneHysteresis {
public:
    bool Update(float distanceMeters,const InteractionZone& z,float angleRadians=0)noexcept {
        if(!std::isfinite(distanceMeters)||!std::isfinite(angleRadians)||distanceMeters<0){inside_=false;return false;}
        const bool angled=z.captureRadians>0;
        if(inside_)inside_=distanceMeters<=z.releaseMeters&&(!angled||angleRadians<=z.releaseRadians);
        else{inside_=distanceMeters<=z.captureMeters&&(!angled||angleRadians<=z.captureRadians);entered_+=inside_;}
        return inside_;
    }
    bool Inside()const noexcept{return inside_;}
    unsigned Entries()const noexcept{return entered_;}
    void Reset()noexcept{inside_=false;}
private:
    bool inside_=false;unsigned entered_=0;
};
// Squeeze hysteresis: on at gripOn, off at gripOff; armed only after a neutral.
class GripHysteresis {
public:
    struct Result {bool held=false,pressed=false,released=false;};
    Result Update(float squeeze,const InteractionZone& z)noexcept {
        Result r;if(!std::isfinite(squeeze)){r.released=held_;held_=false;armed_=false;return r;}
        if(held_){if(squeeze<=z.gripOff){held_=false;r.released=true;armed_=true;}}
        else if(squeeze<=z.gripOff)armed_=true;
        else if(armed_&&squeeze>=z.gripOn){held_=true;armed_=false;r.pressed=true;}
        r.held=held_;return r;
    }
    void Reset()noexcept{held_=armed_=false;}
private:
    bool held_=false,armed_=false;
};

namespace zone_detail {
struct Quat {float x=0,y=0,z=0,w=1;};
inline Quat FromBasis(const math::Matrix4& m,const std::array<float,3>& len)noexcept {
    float r[3][3];for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)r[i][j]=m.values[i][j]/len[i];
    Quat q;const float t=r[0][0]+r[1][1]+r[2][2];
    if(t>0){const float s=std::sqrt(t+1)*2;q={(r[1][2]-r[2][1])/s,(r[2][0]-r[0][2])/s,(r[0][1]-r[1][0])/s,s/4};}
    else if(r[0][0]>r[1][1]&&r[0][0]>r[2][2]){const float s=std::sqrt(1+r[0][0]-r[1][1]-r[2][2])*2;q={s/4,(r[0][1]+r[1][0])/s,(r[2][0]+r[0][2])/s,(r[1][2]-r[2][1])/s};}
    else if(r[1][1]>r[2][2]){const float s=std::sqrt(1+r[1][1]-r[0][0]-r[2][2])*2;q={(r[0][1]+r[1][0])/s,s/4,(r[1][2]+r[2][1])/s,(r[2][0]-r[0][2])/s};}
    else{const float s=std::sqrt(1+r[2][2]-r[0][0]-r[1][1])*2;q={(r[2][0]+r[0][2])/s,(r[1][2]+r[2][1])/s,s/4,(r[0][1]-r[1][0])/s};}
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};
}
inline void ToBasis(Quat q,math::Matrix4& m,const std::array<float,3>& len)noexcept {
    const float x=q.x,y=q.y,z=q.z,w=q.w;
    const float r[3][3]{{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w)},{2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w)},{2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)}};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)m.values[i][j]=r[i][j]*len[i];
}
inline Quat Slerp(Quat a,Quat b,float t)noexcept {
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(d<0){b={-b.x,-b.y,-b.z,-b.w};d=-d;}
    if(d>.9995f){Quat q{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
        const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};}
    const float th=std::acos(std::clamp(d,-1.f,1.f)),s=std::sin(th),ka=std::sin((1-t)*th)/s,kb=std::sin(t*th)/s;
    return {a.x*ka+b.x*kb,a.y*ka+b.y*kb,a.z*ka+b.z*kb,a.w*ka+b.w*kb};
}
}
// Rigid blend of two transforms (rows 0-2 basis, row 3 origin): position lerp,
// rotation slerp, per-row scale lerp. t is clamped to 0..1.
inline std::optional<math::Matrix4> BlendTransform(const math::Matrix4& a,const math::Matrix4& b,float t)noexcept {
    t=std::clamp(std::isfinite(t)?t:0.f,0.f,1.f);
    std::array<float,3> la{},lb{},l{};
    for(unsigned i=0;i<3;++i){
        la[i]=std::sqrt(a.values[i][0]*a.values[i][0]+a.values[i][1]*a.values[i][1]+a.values[i][2]*a.values[i][2]);
        lb[i]=std::sqrt(b.values[i][0]*b.values[i][0]+b.values[i][1]*b.values[i][1]+b.values[i][2]*b.values[i][2]);
        if(!(la[i]>1e-6f)||!(lb[i]>1e-6f)||!std::isfinite(la[i])||!std::isfinite(lb[i]))return {};
        l[i]=la[i]+(lb[i]-la[i])*t;
    }
    math::Matrix4 out=a;
    zone_detail::ToBasis(zone_detail::Slerp(zone_detail::FromBasis(a,la),zone_detail::FromBasis(b,lb),t),out,l);
    for(unsigned j=0;j<3;++j)out.values[3][j]=a.values[3][j]+(b.values[3][j]-a.values[3][j])*t;
    out.values[0][3]=out.values[1][3]=out.values[2][3]=0;out.values[3][3]=1;
    for(const auto& row:out.values)for(float v:row)if(!std::isfinite(v))return {};
    return out;
}
inline float SmoothStep(float t)noexcept{t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}

// Soft magnetism for one hand (docs/floating-hands-contacts.md contract).
// Each update supplies the tracked (free) hand pose and, while a contact is
// held, the authored contact pose relative to the held object's frame
// (contactLocal) with that frame's current world pose. Holding: the visible
// hand eases from the tracked hand onto the contact over magnetInNs. Release
// removes ownership at once (the caller already did); only the last visual
// correction decays, expressed in the TRACKED HAND's frame over magnetOutNs, so
// the returning hand never keeps following the gun. A new token re-blends from
// wherever the hand currently is. Gameplay must evaluate zones on the raw
// tracked wrist, never on this output (no self-sustaining contact).
struct ContactMagnetResult {math::Matrix4 target{};float weight=0;bool contact=false,blending=false;};
class ContactMagnet {
public:
    ContactMagnetResult Update(std::int64_t now,const math::Matrix4& freeHand,const std::optional<math::Matrix4>& contactLocal,
        const math::Matrix4& objectFrame,const InteractionZone& zone,std::uint64_t token=1)noexcept {
        ContactMagnetResult out;out.target=freeHand;
        if(now<=0||(last_&&now<last_)){Reset();return out;}
        const float dt=last_?float(double(now-last_)):0.f;last_=now;
        const auto inverseFree=InverseAnimatedTransform(freeHand);
        if(contactLocal&&token&&inverseFree){
            const auto contact=Multiply(*contactLocal,objectFrame);
            if(token!=token_){
                // Start from the visible hand: a pending release correction
                // becomes the starting offset of the new blend.
                token_=token;start_.reset();
                if(correction_&&weight_>0)start_=BlendTransform(freeHand,Multiply(*correction_,freeHand),SmoothStep(weight_));
                weight_=zone.magnetInNs>0?0.f:1.f;} // the first contact frame starts the blend (no stale dt)
            else weight_=zone.magnetInNs>0?std::min(1.f,weight_+dt/float(zone.magnetInNs)):1.f;
            out.weight=SmoothStep(weight_);out.contact=true;
            const auto from=start_?*start_:freeHand;
            if(out.weight>=1){out.target=contact;start_.reset();}
            else if(const auto blended=BlendTransform(from,contact,out.weight)){out.target=*blended;out.blending=true;}
            else{out.target=contact;out.weight=1;}
            correction_=Multiply(out.target,*inverseFree); // visible hand relative to the tracked hand
            return out;
        }
        if(token_){token_=0;start_.reset();weight_=correction_?1.f:0.f;}
        if(!correction_||!inverseFree){Reset();last_=now;return out;}
        weight_=zone.magnetOutNs>0?std::max(0.f,weight_-dt/float(zone.magnetOutNs)):0.f;
        if(weight_<=0){correction_.reset();return out;}
        out.weight=SmoothStep(weight_);
        if(const auto blended=BlendTransform(freeHand,Multiply(*correction_,freeHand),out.weight)){out.target=*blended;out.blending=true;}
        else{correction_.reset();out.weight=0;}
        return out;
    }
    void Reset()noexcept{correction_.reset();start_.reset();weight_=0;token_=0;last_=0;}
    bool Active()const noexcept{return token_!=0||correction_.has_value();}
private:
    std::optional<math::Matrix4> correction_,start_;
    float weight_=0;std::uint64_t token_=0;std::int64_t last_=0;
};
} // namespace fvr::interaction
