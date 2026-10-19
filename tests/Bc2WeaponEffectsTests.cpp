#include "Test.h"
#include "Bc2WeaponEffects.h"
#include "fvr/interaction/WeaponProfile.h"
#include <bit>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
using namespace fvr;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
// Row-vector rotations (v' = v * M), translation in row 3.
math::Matrix4 RotX(float d){const float r=d*0.017453292519943295f,c=std::cos(r),s=std::sin(r);auto m=Identity();
    m.values[1][1]=c;m.values[1][2]=s;m.values[2][1]=-s;m.values[2][2]=c;return m;}
math::Matrix4 RotY(float d){const float r=d*0.017453292519943295f,c=std::cos(r),s=std::sin(r);auto m=Identity();
    m.values[0][0]=c;m.values[0][2]=-s;m.values[2][0]=s;m.values[2][2]=c;return m;}
math::Matrix4 RotZ(float d){const float r=d*0.017453292519943295f,c=std::cos(r),s=std::sin(r);auto m=Identity();
    m.values[0][0]=c;m.values[0][1]=s;m.values[1][0]=-s;m.values[1][1]=c;return m;}
math::Matrix4 Translate(math::Matrix4 m,float x,float y,float z){m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;}
math::Matrix4 Mul(const math::Matrix4& a,const math::Matrix4& b){return interaction::Multiply(a,b);}
math::Vec3 Point(math::Vec3 p,const math::Matrix4& m){
    return {p.x*m.values[0][0]+p.y*m.values[1][0]+p.z*m.values[2][0]+m.values[3][0],
            p.x*m.values[0][1]+p.y*m.values[1][1]+p.z*m.values[2][1]+m.values[3][1],
            p.x*m.values[0][2]+p.y*m.values[1][2]+p.z*m.values[2][2]+m.values[3][2]};
}
float Distance(math::Vec3 a,math::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
bool SameAffine(const math::Matrix4& a,const math::Matrix4& b,float e=1e-4f){
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)if(!Near(a.values[r][c],b.values[r][c],e))return false;return true;
}
// The native effect compose (0x7f90b0) ends by swapping rows 1/2 (Y-up effect
// basis to the weapon's Z-forward); any rigid relation is preserved exactly.
math::Matrix4 NativeEffectLocal(math::Vec3 offset,float pitchDegrees){
    auto local=RotX(pitchDegrees);
    const auto row1=local.values[1],row2=local.values[2];
    local.values[1]=row2;for(unsigned c=0;c<3;++c)local.values[2][c]=-row1[c];
    return Translate(local,offset.x,offset.y,offset.z);
}
struct Call {std::uint32_t manager=0,handle=0;math::Matrix4 world{};};
}
int main(){
    using namespace fvr::bc2;
    // Kill switch and window parsing.
    {auto c=ParseEffectAnchorConfig(nullptr,nullptr);CHECK(c.enabled&&c.windowNs==400000000);
     c=ParseEffectAnchorConfig("0",nullptr);CHECK(!c.enabled);
     c=ParseEffectAnchorConfig("1","250");CHECK(c.enabled&&c.windowNs==250000000);
     c=ParseEffectAnchorConfig("00","3000");CHECK(c.enabled&&c.windowNs==3000000000ll);
     c=ParseEffectAnchorConfig("","3001");CHECK(c.enabled&&c.windowNs==400000000);
     c=ParseEffectAnchorConfig(nullptr,"12x");CHECK(c.windowNs==400000000);
     c=ParseEffectAnchorConfig(nullptr,"0");CHECK(c.windowNs==0);}
    // Native LinearTransform padding (0xcdcdcdcd) never enters the affine.
    {std::array<float,16> raw{};const auto pad=std::bit_cast<float>(0xcdcdcdcdu);
     const auto root=Translate(Mul(RotY(30),RotX(-10)),1,2,3);
     for(unsigned r=0;r<4;++r){for(unsigned c=0;c<3;++c)raw[r*4+c]=root.values[r][c];raw[r*4+3]=pad;}
     const auto m=NativeAffine(raw.data());CHECK(SameAffine(m,root));CHECK(m.values[0][3]==0&&m.values[3][3]==1);
     CHECK(interaction::InverseAnimatedTransform(m));}

    // Spawn at the rifle's authored offset (0,0.05,0.9) on the VR root, then
    // move/roll the gun. A world-fixed spawn drifts; the anchor keeps the
    // engine's own root relation exactly.
    const EffectAnchorOwner owner{0x1000,0x2000,0x3000};
    const math::Vec3 flash{0,.05f,.9f};
    const auto local=NativeEffectLocal(flash,10);
    const auto root0=Translate(Mul(RotZ(5),RotY(20)),10,1.6f,-4);
    const auto spawn=Mul(local,root0);
    CHECK(Distance(Point({},spawn),Point(flash,root0))<1e-4f);
    WeaponEffectAnchor anchor;
    CHECK(anchor.Add(owner,0xaaaa,7,root0,spawn,1000)==WeaponEffectAnchor::Track::Stored);
    CHECK(anchor.Live()==1&&anchor.Stored()==1);
    const auto root1=Translate(Mul(Mul(RotZ(35),RotX(-15)),RotY(45)),10.1f,1.55f,-3.9f); // hand rolled/turned/moved
    const float drift=Distance(Point({},spawn),Point(flash,root1));
    CHECK(drift>.2f); // what the user saw: the flash stays where it spawned
    std::vector<Call> calls;
    const auto record=[&](std::uint32_t m,std::uint32_t h,const math::Matrix4& w){calls.push_back({m,h,w});};
    CHECK(anchor.Reanchor(owner,root1,1000+100000000,400000000,record)==1);
    CHECK(calls.size()==1&&calls[0].manager==0xaaaa&&calls[0].handle==7);
    CHECK(SameAffine(calls[0].world,Mul(local,root1)));
    CHECK(Distance(Point({},calls[0].world),Point(flash,root1))<1e-4f);
    // Orientation follows too: the effect basis relative to the root is unchanged.
    const auto relative=Mul(calls[0].world,*interaction::InverseAnimatedTransform(root1));
    CHECK(SameAffine(relative,local));
    // Window: an expired effect is never moved again; the setter is not called.
    calls.clear();
    CHECK(anchor.Reanchor(owner,root0,1000+400000000,400000000,record)==0);
    CHECK(calls.empty()&&anchor.Live()==0&&anchor.Expired()==1);
    // A zero window (anchoring effectively off) applies nothing.
    CHECK(anchor.Add(owner,0xaaaa,8,root0,spawn,5000)==WeaponEffectAnchor::Track::Stored);
    CHECK(anchor.Reanchor(owner,root1,5000,0,record)==0&&calls.empty());
    // Owner change: tracked handles of the previous owner are dropped, not moved.
    CHECK(anchor.Add(owner,0xaaaa,9,root0,spawn,6000)==WeaponEffectAnchor::Track::Stored);
    const EffectAnchorOwner other{0x1000,0x2000,0x3004};
    CHECK(anchor.Reanchor(other,root1,6001,400000000,record)==0&&calls.empty()&&anchor.Live()==0);
    // Invalid inputs.
    CHECK(anchor.Add({},0xaaaa,1,root0,spawn,0)==WeaponEffectAnchor::Track::InvalidOwner);
    CHECK(anchor.Add(owner,0,1,root0,spawn,0)==WeaponEffectAnchor::Track::InvalidHandle);
    CHECK(anchor.Add(owner,0xaaaa,0,root0,spawn,0)==WeaponEffectAnchor::Track::InvalidHandle);
    auto scaled=root0;scaled.values[0][0]*=3;
    CHECK(anchor.Add(owner,0xaaaa,1,scaled,spawn,0)==WeaponEffectAnchor::Track::InvalidRoot);
    auto nan=spawn;nan.values[3][1]=std::nanf("");
    CHECK(anchor.Add(owner,0xaaaa,1,root0,nan,0)==WeaponEffectAnchor::Track::InvalidSpawn);
    CHECK(anchor.Reanchor(owner,scaled,1,400000000,record)==0&&calls.empty());
    // Same handle re-added replaces; a full table evicts the oldest entry.
    {WeaponEffectAnchor a;
     for(std::uint32_t h=1;h<=WeaponEffectAnchor::Capacity;++h)CHECK(a.Add(owner,0xbbbb,h,root0,spawn,h)==WeaponEffectAnchor::Track::Stored);
     CHECK(a.Live()==WeaponEffectAnchor::Capacity&&a.Evicted()==0);
     CHECK(a.Add(owner,0xbbbb,5,root0,spawn,100)==WeaponEffectAnchor::Track::Stored&&a.Live()==WeaponEffectAnchor::Capacity);
     CHECK(a.Add(owner,0xbbbb,99,root0,spawn,101)==WeaponEffectAnchor::Track::Stored&&a.Evicted()==1);
     std::vector<std::uint32_t> handles;
     a.Reanchor(owner,root1,102,400000000,[&](std::uint32_t,std::uint32_t h,const math::Matrix4&){handles.push_back(h);});
     CHECK(handles.size()==WeaponEffectAnchor::Capacity);
     CHECK(std::find(handles.begin(),handles.end(),1u)==handles.end()); // oldest evicted
     CHECK(std::find(handles.begin(),handles.end(),99u)!=handles.end());}

    // Basis deltas: profile-axis placement (-Z barrel, +Y up) matches the aim it
    // was built from; an inherited native cant shows up as roll, yaw as forward.
    {const auto aim=Mul(Mul(RotX(-12),RotY(37)),Identity());
     const auto placed=interaction::ModelAimFrame({0,0,-1},{0,1,0},aim);CHECK(placed);
     auto d=MeasureBasisDelta(*placed,{0,0,-1},{0,1,0},aim,{0,0,1},{0,1,0});
     CHECK(d.valid&&Near(d.forwardDegrees,0,.01f)&&Near(d.rollDegrees,0,.01f));
     // Cant: roll the model 30 degrees about its own barrel.
     const auto canted=Mul(RotZ(30),*placed);
     d=MeasureBasisDelta(canted,{0,0,-1},{0,1,0},aim,{0,0,1},{0,1,0});
     CHECK(d.valid&&Near(d.forwardDegrees,0,.01f)&&Near(std::abs(d.rollDegrees),30,.01f));
     const auto cantedOther=Mul(RotZ(-30),*placed);
     const auto e=MeasureBasisDelta(cantedOther,{0,0,-1},{0,1,0},aim,{0,0,1},{0,1,0});
     CHECK(Near(e.rollDegrees,-d.rollDegrees,.01f));
     // Barrel yawed 8 degrees off the aim: forward error, no roll.
     const auto yawed=Mul(RotY(8),*placed);
     d=MeasureBasisDelta(yawed,{0,0,-1},{0,1,0},aim,{0,0,1},{0,1,0});
     CHECK(Near(d.forwardDegrees,8,.01f)&&Near(d.rollDegrees,0,.05f));
     // Degenerate axes are rejected.
     CHECK(!MeasureBasisDelta(*placed,{0,0,0},{0,1,0},aim,{0,0,1},{0,1,0}).valid);}

    // Telemetry store: bounded, try-lock producer, oldest overwritten, JSON drain.
    {WeaponEffectTelemetry t;
     WeaponEffectRecord r;r.kind=WeaponEffectRecordKind::Spawn;r.tick=44677;r.weapon=0x7878ce0;r.a=0xaaaa;r.b=7;
     r.flags=weapon_effect_flag::RootWritten|weapon_effect_flag::Tracked;SetRecordAsset(r,"40mmgl\"x");r.m[0]=root0;r.m[1]=spawn;r.matrices=3;
     CHECK(t.Push(r));
     WeaponEffectRecord p;p.kind=WeaponEffectRecordKind::Placement;p.source=WeaponOrientationSource::NativeAttachment;p.tick=43967;p.weapon=0x7878ce0;
     p.delta={true,1.5f,-31.25f};SetRecordAsset(p,"40mmgl");p.m[1]=root1;p.matrices=2;CHECK(t.Push(p));
     WeaponEffectRecord a;a.kind=WeaponEffectRecordKind::Admission;a.flags=weapon_effect_flag::CandidateProfile|weapon_effect_flag::VisualMeshes;SetRecordAsset(a,"40mmgl");CHECK(t.Push(a));
     std::ostringstream out;t.Write(out,EffectAnchorConfig{},true);const auto json=out.str();
     CHECK(json.find("\"anchor_installed\":true")!=std::string::npos);
     CHECK(json.find("\"kind\":\"spawn\"")!=std::string::npos&&json.find("\"weapon\":\"0x7878ce0\"")!=std::string::npos);
     CHECK(json.find("\"asset\":\"40mmglx\"")!=std::string::npos); // quotes never reach the JSON
     CHECK(json.find("\"root_written\":true")!=std::string::npos&&json.find("\"effect_root\":[")!=std::string::npos&&json.find("\"spawn\":[")!=std::string::npos);
     CHECK(json.find("\"source\":\"native_attachment\"")!=std::string::npos&&json.find("\"roll_deg\":-31.25")!=std::string::npos);
     CHECK(json.find("\"placed_weapon\":[")!=std::string::npos&&json.find("\"native_weapon\":[")==std::string::npos);
     CHECK(json.find("\"admitted\":false")!=std::string::npos&&json.find("\"visual_meshes\":true")!=std::string::npos);
     CHECK(t.Pending()==0);
     for(unsigned n=0;n<WeaponEffectTelemetry::Capacity+5;++n){r.tick=n;CHECK(t.Push(r));}
     CHECK(t.Overwritten()==5&&t.Pending()==WeaponEffectTelemetry::Capacity);
     const auto rows=t.Take(10);CHECK(rows.size()==10&&rows.front().tick==5);
     std::ostringstream again;t.Write(again,EffectAnchorConfig{false,0},false,4);
     CHECK(again.str().find("\"anchor_enabled\":false")!=std::string::npos&&t.Pending()==WeaponEffectTelemetry::Capacity-14);}
    std::puts("Bc2WeaponEffectsTests passed");
    return 0;
}
