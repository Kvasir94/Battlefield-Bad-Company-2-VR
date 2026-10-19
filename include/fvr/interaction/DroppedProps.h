#pragma once
// Dropped magazine props (docs/BC2_RELOAD_PARITY.md M6, docs/INTERACTION-POLISH-20261008.md).
// KF2 VR DropMagazine / StartReleasedProp (IR:3229-3243): a released magazine
// leaves the hand with the hand's linear AND angular velocity, tumbles under
// gravity, bounces off the floor, slides to rest lying on a face and lingers
// for a bounded lifetime; a bounded number of cosmetic slots, the oldest
// recycled. Presentation only: a prop owns no ammunition, claim or native
// object, and nothing here reads or writes the game. Positions are world
// metres with +Y up; the adapter supplies the floor height (a play-space or
// ground estimate) and maps the pose for the renderer.
//
// Matrices are row-vector (point * matrix): rows 0..2 are the prop's local
// axes in world space, row 3 its origin. With zero half extents the prop is a
// point (the original M6 behaviour: the origin rests exactly on the floor and
// the release rotation is kept unless an angular velocity is supplied). With
// extents the prop is a box around `centerOffset` (prop-local): its lowest
// corner resolves against the floor with an impulse that also spins it, and
// at rest it lies on the face closest to the floor, its box sitting on the floor.
#include "fvr/math/StereoMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace fvr::interaction {
struct DroppedPropConfig {
    float gravity=9.81f,restitution=.25f,friction=.6f,restSpeed=.15f,maxSpeed=8.f;
    std::int64_t lifetimeNs=12000000000ll,maxStepNs=50000000;
    // Live props before the oldest is recycled (1..DroppedProps::Slots).
    unsigned maxLive=3;
    // Box model (all zero = point prop). Prop-local metres.
    math::Vec3 halfExtents{},centerOffset{};
    float maxAngularSpeed=25.f,restAngularSpeed=.6f,airAngularDamping=.05f;
    // A box prop that has not come to rest by then is settled where it is.
    std::int64_t settleAfterNs=4000000000ll,substepNs=4000000,restHoldNs=120000000;
};
// BC2 magazine profile: 30 s lifetime, four live props, XM8 magazine box.
// The box was measured (October 8 2026) from the jntWpn_6 rigid vertex set in
// the item (jntWpn_6 bone) frame; its +Y top matches the calibrated insertion
// point y=0.0879 (Bc2Xm8MagazineCalibration ItemFromInsertion). Three numbers,
// not game geometry; other rifle magazines are close enough for a resting pose.
inline DroppedPropConfig MagazineDropConfig()noexcept {
    DroppedPropConfig c;c.restitution=.3f;c.friction=.45f;c.restSpeed=.06f;
    c.lifetimeNs=30000000000ll;c.maxLive=4;
    c.halfExtents={.0178f,.0911f,.0512f};c.centerOffset={.0007f,-.0032f,.0051f};
    return c;
}
enum class DroppedPropEvent:std::uint8_t {None,Dropped,Landed,Expired,Recycled};
struct DroppedProp {
    std::uint64_t id=0,item=0;
    math::Matrix4 worldFromProp{}; // rotation integrated from the release (point props without spin keep it)
    math::Vec3 velocity{},angularVelocity{}; // centre of mass, world rad/s
    float floorY=0;
    std::int64_t droppedNs=0,landedNs=0,lastNs=0,slowSinceNs=0;
    unsigned bounces=0;
    bool resting=false,touched=false;
};
struct DroppedPropNotice {DroppedPropEvent event=DroppedPropEvent::None;std::uint64_t id=0,item=0;std::int64_t atNs=0;math::Vec3 position{};};
struct DroppedPropStats {std::uint64_t dropped=0,landed=0,expired=0,recycled=0,rejected=0,bounces=0,forcedSettles=0;};
class DroppedProps {
public:
    static constexpr unsigned Slots=6;
    explicit DroppedProps(DroppedPropConfig c={})noexcept:c_(Sanitize(c)){}
    void Configure(DroppedPropConfig c)noexcept{c_=Sanitize(c);}
    const DroppedPropConfig& Config()const noexcept{return c_;}
    // A released magazine. The oldest live prop is recycled when maxLive props
    // exist (TakeRecycled names it). Returns the new prop's id.
    std::optional<std::uint64_t> Drop(std::uint64_t item,const math::Matrix4& worldFromProp,math::Vec3 velocity,
        float floorY,std::int64_t now)noexcept {return Drop(item,worldFromProp,velocity,{},floorY,now);}
    std::optional<std::uint64_t> Drop(std::uint64_t item,const math::Matrix4& worldFromProp,math::Vec3 velocity,
        math::Vec3 angularVelocity,float floorY,std::int64_t now)noexcept {
        const auto& p=worldFromProp.values[3];
        if(now<=0||!Finite(p[0])||!Finite(p[1])||!Finite(p[2])||!Finite(floorY)||!Finite(velocity)||!Finite(angularVelocity)||!Basis(worldFromProp)){++stats_.rejected;return {};}
        velocity=Clamp(velocity,c_.maxSpeed);angularVelocity=Clamp(angularVelocity,c_.maxAngularSpeed);
        unsigned live=0,slot=Slots,oldest=Slots;
        for(unsigned n=0;n<Slots;++n){
            if(!props_[n]){if(slot==Slots)slot=n;continue;}
            ++live;if(oldest==Slots||props_[n]->droppedNs<props_[oldest]->droppedNs||
                (props_[n]->droppedNs==props_[oldest]->droppedNs&&props_[n]->id<props_[oldest]->id))oldest=n;
        }
        if(live>=c_.maxLive||slot==Slots){slot=oldest;recycled_=props_[slot]->id;++stats_.recycled;props_[slot].reset();}
        DroppedProp d;d.id=++next_;d.item=item;d.velocity=velocity;
        // A spin-free point prop keeps its release matrix bit for bit (M6).
        d.worldFromProp=Point()&&!(Length(angularVelocity)>0)?worldFromProp:Orthonormal(worldFromProp);
        d.angularVelocity=angularVelocity;d.floorY=floorY;d.droppedNs=d.lastNs=now;
        if(Point()){
            if(p[1]<=floorY){d.worldFromProp.values[3][1]=floorY;d.velocity={};d.angularVelocity={};d.resting=true;d.landedNs=now;++stats_.landed;}
        }else if(LowestY(d)<=floorY&&std::fabs(velocity.y)<c_.restSpeed){Settle(d);d.resting=true;d.landedNs=now;++stats_.landed;}
        props_[slot]=d;++stats_.dropped;return d.id;
    }
    // Integrates every live prop to now. Up to Slots notices (Landed/Expired).
    struct Step {std::array<DroppedPropNotice,Slots> notices{};unsigned count=0;};
    Step Update(std::int64_t now)noexcept {
        Step out;
        for(auto& slot:props_){
            if(!slot)continue;auto& d=*slot;
            if(now<d.lastNs){d.lastNs=now;continue;}
            if(now-d.droppedNs>=c_.lifetimeNs){out.notices[out.count++]={DroppedPropEvent::Expired,d.id,d.item,now,Position(d)};++stats_.expired;slot.reset();continue;}
            const bool wasResting=d.resting;
            const std::int64_t stepNs=Point()?c_.maxStepNs:std::min(c_.maxStepNs,c_.substepNs);
            for(std::int64_t t=d.lastNs;t<now&&!d.resting;){
                const auto step=std::min(stepNs,now-t);t+=step;
                if(Point())IntegratePoint(d,float(double(step)*1e-9));else IntegrateBox(d,float(double(step)*1e-9),t);
            }
            d.lastNs=now;
            if(d.resting&&!wasResting){d.landedNs=now;++stats_.landed;out.notices[out.count++]={DroppedPropEvent::Landed,d.id,d.item,now,Position(d)};}
        }
        return out;
    }
    void Clear()noexcept{for(auto& p:props_)p.reset();}
    const std::array<std::optional<DroppedProp>,Slots>& Props()const noexcept{return props_;}
    unsigned Live()const noexcept{unsigned n=0;for(const auto& p:props_)n+=p.has_value();return n;}
    std::optional<std::uint64_t> TakeRecycled()noexcept{auto r=recycled_;recycled_.reset();return r;}
    const DroppedPropStats& Stats()const noexcept{return stats_;}
    // Lowest world height of the prop's box (its origin for a point prop).
    float LowestY(const DroppedProp& d)const noexcept {
        float low=1e30f;ForCorners(d,[&](math::Vec3 q){low=std::min(low,q.y);});return low;
    }
private:
    static bool Finite(float v)noexcept{return std::isfinite(v);}
    static bool Finite(math::Vec3 v)noexcept{return Finite(v.x)&&Finite(v.y)&&Finite(v.z);}
    static float Length(math::Vec3 v)noexcept{return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
    static math::Vec3 Add(math::Vec3 a,math::Vec3 b)noexcept{return {a.x+b.x,a.y+b.y,a.z+b.z};}
    static math::Vec3 Sub(math::Vec3 a,math::Vec3 b)noexcept{return {a.x-b.x,a.y-b.y,a.z-b.z};}
    static math::Vec3 Scale(math::Vec3 a,float k)noexcept{return {a.x*k,a.y*k,a.z*k};}
    static float Dot(math::Vec3 a,math::Vec3 b)noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
    static math::Vec3 Cross(math::Vec3 a,math::Vec3 b)noexcept{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    static math::Vec3 Clamp(math::Vec3 v,float limit)noexcept{const float s=Length(v);return s>limit&&s>0?Scale(v,limit/s):v;}
    static math::Vec3 Row(const math::Matrix4& m,unsigned r)noexcept{return {m.values[r][0],m.values[r][1],m.values[r][2]};}
    static void SetRow(math::Matrix4& m,unsigned r,math::Vec3 v)noexcept{m.values[r][0]=v.x;m.values[r][1]=v.y;m.values[r][2]=v.z;}
    static bool Basis(const math::Matrix4& m)noexcept {
        for(unsigned r=0;r<3;++r){const auto a=Row(m,r);if(!Finite(a)||Length(a)<1e-4f)return false;}return true;
    }
    // Gram-Schmidt, keeping row 0's direction then row 1's plane (handedness kept from row 2).
    static math::Matrix4 Orthonormal(math::Matrix4 m)noexcept {
        auto x=Row(m,0),y=Row(m,1);const auto z0=Row(m,2);
        x=Scale(x,1/Length(x));y=Sub(y,Scale(x,Dot(x,y)));const float ly=Length(y);if(!(ly>1e-6f))return m;
        y=Scale(y,1/ly);auto z=Cross(x,y);if(Dot(z,z0)<0)z=Scale(z,-1);
        SetRow(m,0,x);SetRow(m,1,y);SetRow(m,2,z);return m;
    }
    bool Point()const noexcept{return !(c_.halfExtents.x>0||c_.halfExtents.y>0||c_.halfExtents.z>0);}
    static DroppedPropConfig Sanitize(DroppedPropConfig c)noexcept {
        c.maxLive=std::clamp(c.maxLive,1u,Slots);
        if(!(c.substepNs>0))c.substepNs=4000000;if(!(c.maxStepNs>0))c.maxStepNs=50000000;
        c.halfExtents={std::max(0.f,c.halfExtents.x),std::max(0.f,c.halfExtents.y),std::max(0.f,c.halfExtents.z)};
        return c;
    }
    math::Vec3 Com(const DroppedProp& d)const noexcept {
        const auto& m=d.worldFromProp;const auto& c=c_.centerOffset;
        return Add(Row(m,3),Add(Add(Scale(Row(m,0),c.x),Scale(Row(m,1),c.y)),Scale(Row(m,2),c.z)));
    }
    void SetCom(DroppedProp& d,math::Vec3 com)const noexcept {
        const auto& m=d.worldFromProp;const auto& c=c_.centerOffset;
        SetRow(d.worldFromProp,3,Sub(com,Add(Add(Scale(Row(m,0),c.x),Scale(Row(m,1),c.y)),Scale(Row(m,2),c.z))));
    }
    template<class F> void ForCorners(const DroppedProp& d,F&& f)const noexcept {
        const auto com=Com(d);const auto& h=c_.halfExtents;
        for(unsigned n=0;n<8;++n){
            const float sx=(n&1)?h.x:-h.x,sy=(n&2)?h.y:-h.y,sz=(n&4)?h.z:-h.z;
            f(Add(com,Add(Add(Scale(Row(d.worldFromProp,0),sx),Scale(Row(d.worldFromProp,1),sy)),Scale(Row(d.worldFromProp,2),sz))));
        }
    }
    static math::Vec3 Position(const DroppedProp& d)noexcept{return Row(d.worldFromProp,3);}
    // Box inverse inertia (unit mass) applied to a world vector.
    math::Vec3 InverseInertia(const DroppedProp& d,math::Vec3 v)const noexcept {
        const auto& h=c_.halfExtents;const float i[3]{(h.y*h.y+h.z*h.z)/3,(h.x*h.x+h.z*h.z)/3,(h.x*h.x+h.y*h.y)/3};
        math::Vec3 out{};
        for(unsigned a=0;a<3;++a){const auto axis=Row(d.worldFromProp,a);const float k=i[a]>1e-8f?Dot(axis,v)/i[a]:0.f;out=Add(out,Scale(axis,k));}
        return out;
    }
    void Rotate(DroppedProp& d,float dt)const noexcept {
        const auto w=d.angularVelocity;if(!(Length(w)>0))return;
        const auto com=Com(d);
        for(unsigned r=0;r<3;++r){const auto a=Row(d.worldFromProp,r);SetRow(d.worldFromProp,r,Add(a,Scale(Cross(w,a),dt)));}
        d.worldFromProp=Orthonormal(d.worldFromProp);SetCom(d,com);
    }
    void IntegratePoint(DroppedProp& d,float dt)noexcept {
        auto& p=d.worldFromProp.values[3];
        d.velocity.y-=c_.gravity*dt;
        if(Length(d.angularVelocity)>0){Rotate(d,dt);}
        p[0]+=d.velocity.x*dt;p[1]+=d.velocity.y*dt;p[2]+=d.velocity.z*dt;
        if(p[1]>d.floorY)return;
        p[1]=d.floorY;++d.bounces;++stats_.bounces;
        // Bounce with restitution; the floor's friction bleeds the slide and spin.
        d.velocity.y=-d.velocity.y*c_.restitution;
        d.velocity.x*=1-c_.friction;d.velocity.z*=1-c_.friction;d.angularVelocity=Scale(d.angularVelocity,1-c_.friction);
        const float s=Length(d.velocity);
        if(s<c_.restSpeed||d.velocity.y<c_.restSpeed){d.velocity={};d.angularVelocity={};d.resting=true;}
    }
    void IntegrateBox(DroppedProp& d,float dt,std::int64_t at)noexcept {
        d.velocity.y-=c_.gravity*dt;
        d.angularVelocity=Scale(d.angularVelocity,std::max(0.f,1-c_.airAngularDamping*dt));
        auto com=Add(Com(d),Scale(d.velocity,dt));SetCom(d,com);Rotate(d,dt);
        // Lowest corner against the floor.
        math::Vec3 contact{};float low=1e30f;
        ForCorners(d,[&](math::Vec3 q){if(q.y<low){low=q.y;contact=q;}});
        if(low<d.floorY){
            com=Com(d);com.y+=d.floorY-low;SetCom(d,com);
            // A face or edge on the floor contacts at the mean of its corners;
            // a single corner would add a spurious torque and rock it forever.
            math::Vec3 sum{};unsigned count=0;
            ForCorners(d,[&](math::Vec3 q){if(q.y<=d.floorY+.002f){sum=Add(sum,q);++count;}});
            if(count)contact=Scale(sum,1.f/float(count));contact.y=d.floorY;
            const math::Vec3 n{0,1,0};const auto r=Sub(contact,com);
            const auto pointVelocity=Add(d.velocity,Cross(d.angularVelocity,r));
            const float vn=Dot(pointVelocity,n);
            if(vn<0){
                const auto rn=Cross(r,n);
                const float k=1+Dot(n,Cross(InverseInertia(d,rn),r));
                const float e=-vn>.4f?c_.restitution:0.f; // slow contacts do not bounce (no jitter)
                const float j=-(1+e)*vn/k;
                d.velocity=Add(d.velocity,Scale(n,j));d.angularVelocity=Add(d.angularVelocity,InverseInertia(d,Scale(rn,j)));
                if(-vn>.4f){++d.bounces;++stats_.bounces;}
                // Coulomb friction against the sliding contact.
                const auto pv=Add(d.velocity,Cross(d.angularVelocity,r));
                auto vt=Sub(pv,Scale(n,Dot(pv,n)));const float st=Length(vt);
                if(st>1e-5f){
                    const auto t=Scale(vt,1/st);const auto rt=Cross(r,t);
                    const float kt=1+Dot(t,Cross(InverseInertia(d,rt),r));
                    const float jt=std::min(st/kt,c_.friction*j);
                    d.velocity=Sub(d.velocity,Scale(t,jt));d.angularVelocity=Sub(d.angularVelocity,InverseInertia(d,Scale(rt,jt)));
                }
            }
            d.touched=true;
            // Rolling resistance while in floor contact: a lying magazine stops.
            d.angularVelocity=Scale(d.angularVelocity,std::max(0.f,1-4.f*dt));
            d.velocity.x*=std::max(0.f,1-1.5f*dt);d.velocity.z*=std::max(0.f,1-1.5f*dt);
        }
        const bool onFloor=low<=d.floorY+.004f;
        const bool slow=Length(d.velocity)<c_.restSpeed&&Length(d.angularVelocity)<c_.restAngularSpeed;
        if(onFloor&&slow){if(!d.slowSinceNs)d.slowSinceNs=at;}else d.slowSinceNs=0;
        const bool settled=d.slowSinceNs&&at-d.slowSinceNs>=c_.restHoldNs;
        const bool timeout=at-d.droppedNs>=c_.settleAfterNs;
        if(settled||timeout){if(timeout&&!settled)++stats_.forcedSettles;Settle(d);d.resting=true;}
    }
    // Lie on the face whose normal points most nearly down; the box sits on the floor.
    void Settle(DroppedProp& d)const noexcept {
        const auto com=Com(d);unsigned axis=0;float best=-1;
        for(unsigned a=0;a<3;++a){const float v=std::fabs(Row(d.worldFromProp,a).y);if(v>best){best=v;axis=a;}}
        const auto a=Row(d.worldFromProp,axis);const math::Vec3 target{0,a.y>=0?1.f:-1.f,0};
        const auto c=Cross(a,target);const float s=Length(c),cs=Dot(a,target);
        if(s>1e-6f){
            const auto k=Scale(c,1/s);
            for(unsigned r=0;r<3;++r){const auto v=Row(d.worldFromProp,r); // Rodrigues
                SetRow(d.worldFromProp,r,Add(Add(Scale(v,cs),Scale(Cross(k,v),s)),Scale(k,Dot(k,v)*(1-cs))));}
            d.worldFromProp=Orthonormal(d.worldFromProp);
        }
        SetCom(d,com);
        const float lift=d.floorY-LowestY(d);auto moved=Com(d);moved.y+=lift;SetCom(d,moved);
        d.velocity={};d.angularVelocity={};
    }
    DroppedPropConfig c_;
    std::array<std::optional<DroppedProp>,Slots> props_{};
    std::optional<std::uint64_t> recycled_{};
    std::uint64_t next_=0;
    DroppedPropStats stats_{};
};
} // namespace fvr::interaction
