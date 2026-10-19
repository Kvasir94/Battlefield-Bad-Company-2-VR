#define _CRT_SECURE_NO_WARNINGS
#include "Test.h"
#include "Bc2Xm8BoltAsset.h"
#include "Bc2Xm8BoltCycle.h"
#include "Bc2Xm8BoltPresentation.h"
#include "Bc2SightContact.h"
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
using namespace fvr;
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
math::Matrix4 At(float x,float y,float z){auto m=Identity();m.values[3]={x,y,z,1};return m;}
std::span<const std::byte> Bytes(const std::string& s){return {reinterpret_cast<const std::byte*>(s.data()),s.size()};}

// ---- Synthetic ske01-shaped rig and a closed tetrahedron part ----
const std::vector<std::string> Names{"grannyRootBone","jntWpn_1","jntWpnwpnJnt_16","jntWpn_17","jntWpn_4"};
const std::vector<std::int32_t> Parents{-1,0,1,2,1};
// Native row-vector inverse binds (translation in the last row).
std::array<float,16> NativeInverse(unsigned bone){
    std::array<float,16> m{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    if(bone>=1){m[13]=-1.f;m[14]=.1f;} // all jntWpn share one bind world (0,1,-0.1)
    return m;
}
std::uint64_t SyntheticFingerprint(){
    std::vector<math::Matrix4> inverse;
    for(unsigned n=0;n<Names.size();++n){const auto f=NativeInverse(n);math::Matrix4 m{};for(unsigned k=0;k<16;++k)m.values[k/4][k%4]=f[k];
        for(unsigned row=0;row<4;++row)m.values[row][3]=row==3?1.f:0.f;
        for(unsigned i=0;i<4;++i){m.values[2][i]=-m.values[2][i];m.values[i][2]=-m.values[i][2];}inverse.push_back(m);}
    return SightRigFingerprint(Names,Parents,inverse);
}
struct Tetra {std::array<std::array<float,3>,4> p{{{0,1.0f,-.2f},{.02f,1.0f,-.1f},{-.02f,1.0f,-.1f},{0,1.03f,-.12f}}};
    std::vector<std::array<unsigned,3>> tris{{0,1,2},{0,3,1},{0,2,3},{1,3,2}};};
std::array<double,3> Normal(const Tetra& t,const std::array<unsigned,3>& f){
    const auto& a=t.p[f[0]];const auto& b=t.p[f[1]];const auto& c=t.p[f[2]];
    const double u[3]{b[0]-a[0],b[1]-a[1],b[2]-a[2]},v[3]{c[0]-a[0],c[1]-a[1],c[2]-a[2]};
    std::array<double,3> n{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
    const double l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);for(auto& x:n)x/=l;return n;
}
struct FixtureOptions {bool twoWeights=false,dropTriangle=false,flipNormal=false,badSchema=false;};
std::string RenderJson(const FixtureOptions& o={},std::uint64_t fingerprint=SyntheticFingerprint()){
    Tetra t;if(o.dropTriangle)t.tris.back()=t.tris.front(); // open: one face missing, one doubled
    std::ostringstream s;s.precision(9);
    s<<"{\"schema\":\""<<(o.badSchema?"other":"bc2.private.xm8_bolt_render_input.v1")<<"\",\"rig\":{\"names\":[";
    for(unsigned n=0;n<Names.size();++n)s<<(n?",":"")<<'"'<<Names[n]<<'"';s<<"],\"parents\":[";
    for(unsigned n=0;n<Parents.size();++n)s<<(n?",":"")<<Parents[n];s<<"],\"inverse_bind\":[";
    for(unsigned n=0;n<Names.size();++n){s<<(n?",":"")<<'[';const auto m=NativeInverse(n);for(unsigned k=0;k<16;++k)s<<(k?",":"")<<m[k];s<<']';}
    char fp[40]{};std::snprintf(fp,sizeof(fp),"fnv1a64:%016llx",static_cast<unsigned long long>(fingerprint));
    s<<"],\"rig_fingerprint\":\""<<fp<<"\"},\"parts\":[{\"name\":\"bolt\",\"skin_joint\":\"jntWpnwpnJnt_16\",\"joint_rest_relative_to_jntWpn_1_canonical\":[0,0.07,-0.38],\"vertices\":[";
    for(unsigned n=0;n<4;++n){s<<(n?",":"")<<"{\"position\":["<<t.p[n][0]<<','<<t.p[n][1]<<','<<t.p[n][2]<<"],\"weights\":{\"jntWpnwpnJnt_16\":"<<(o.twoWeights&&n==2?"0.5,\"jntWpn_1\":0.5":"1.0")<<"}}";}
    s<<"],\"triangles\":[";
    for(unsigned n=0;n<t.tris.size();++n){const auto& f=t.tris[n];auto normal=Normal(t,f);if(o.flipNormal&&n==0)for(auto& x:normal)x=-x;
        s<<(n?",":"")<<"{\"corners\":[{\"vertex\":"<<f[0]<<"},{\"vertex\":"<<f[1]<<"},{\"vertex\":"<<f[2]<<"}],\"geometric_normal\":["<<normal[0]<<','<<normal[1]<<','<<normal[2]<<"]}";}
    s<<"]}],\"travel\":{\"axis_canonical\":[0,0,1],\"stroke_m\":0.119885,\"handle_jntWpn_17_rest_canonical\":[0,0.069722,-0.496687],\"carrier_joint_rest_canonical\":[0,0.07,-0.38]},"
       "\"hide_native_parts_while_drawn\":[\"jntWpn_4\"],\"validation\":\"C:\\\\x\\\\xm8-bolt-v1-validation.json\"}";
    return s.str();
}
struct ReceiptOptions {bool open=false;bool wrongSha=false;unsigned triangles=4;double stroke=.119885;};
std::string ReceiptJson(const std::string& render,const ReceiptOptions& o={}){
    const auto sha=o.wrongSha?std::string(64,'0'):Xm8BoltSha256Hex(Bytes(render));
    char fp[40]{};std::snprintf(fp,sizeof(fp),"fnv1a64:%016llx",static_cast<unsigned long long>(SyntheticFingerprint()));
    std::ostringstream s;
    s<<"{\"schema\":\"bc2.private.xm8_bolt.validation.v1\",\"rig\":{\"fingerprint\":\""<<fp<<"\"},\"topology\":{\"vertices\":4,\"triangles\":"<<o.triangles
     <<",\"all_triangles\":true,\"closed_manifold\":"<<(o.open?"false":"true")<<",\"boundary_edges\":0,\"non_manifold_edges\":0,\"degenerate_triangles\":0,"
       "\"connected_shells\":1,\"outward_normals\":true},\"skin\":{\"joint\":\"jntWpnwpnJnt_16\",\"all_rigid_single_joint\":true},"
       "\"travel\":{\"axis_canonical\":[0,0,1],\"stroke_m\":"<<o.stroke<<"},\"export\":{\"sha256\":\""<<sha<<"\",\"vertices\":4,\"triangles\":"<<o.triangles<<"}}";
    return s.str();
}
Xm8BoltAssetResult Parse(const std::string& render,const std::string& receipt){return ParseXm8BoltAssetForRig(Bytes(render),receipt,SyntheticFingerprint());}

int Sha256Vectors(){
    CHECK(Xm8BoltSha256Hex({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Xm8BoltSha256Hex(Bytes("abc"))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Xm8BoltSha256Hex(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    return 0;
}
int AssetParsesAndConverts(){
    const auto render=RenderJson();const auto r=Parse(render,ReceiptJson(render));
    CHECK(r.status==Xm8BoltAssetStatus::Loaded&&r.asset);const auto& a=*r.asset;
    CHECK(a.triangles==4&&a.vertices==4&&a.mesh.vertices.size()==12);
    CHECK(Near(a.stroke,.119885f,1e-7f)&&a.axis==(std::array<float,3>{0,0,1}));
    CHECK(Near(a.handleRest[2],-.496687f,1e-6f)&&Near(a.carrierRest[1],.07f,1e-6f));
    CHECK(graphics::ValidRigidPropKey(a.key)&&a.sha256==Xm8BoltSha256Hex(Bytes(render)));
    // Archive (0,1,-0.2) -> canonical (0,1,0.2) -> carrier-local by the canonical
    // inverse bind (translation (0,-1,-0.1)) -> (0,0,0.1): first corner of tri 0.
    CHECK(Near(a.mesh.vertices[0].x,0)&&Near(a.mesh.vertices[0].y,0)&&Near(a.mesh.vertices[0].z,.1f));
    // Reflection reverses the corner order: stored corners are (0,2,1).
    CHECK(Near(a.mesh.vertices[1].x,-.02f)&&Near(a.mesh.vertices[2].x,.02f));
    // Converted normals point away from the converted centroid (outward kept).
    std::array<double,3> centroid{};for(const auto& v:a.mesh.vertices){centroid[0]+=v.x/12;centroid[1]+=v.y/12;centroid[2]+=v.z/12;}
    for(unsigned t=0;t<4;++t){const auto& v=a.mesh.vertices[t*3];const double d=(v.x-centroid[0])*v.nx+(v.y-centroid[1])*v.ny+(v.z-centroid[2])*v.nz;CHECK(d>0);}
    return 0;
}
int AssetFailsSoft(){
    const auto good=RenderJson();
    CHECK(Parse(good,ReceiptJson(good,{.wrongSha=true})).status==Xm8BoltAssetStatus::ValidationMismatch);
    CHECK(Parse(good,ReceiptJson(good,{.open=true})).status==Xm8BoltAssetStatus::Validation);
    CHECK(Parse(good,ReceiptJson(good,{.triangles=5})).status==Xm8BoltAssetStatus::ValidationMismatch);
    CHECK(Parse(good,ReceiptJson(good,{.stroke=.12})).status==Xm8BoltAssetStatus::ValidationMismatch);
    const auto open=RenderJson({.dropTriangle=true});
    CHECK(Parse(open,ReceiptJson(open)).status==Xm8BoltAssetStatus::Geometry);
    const auto blended=RenderJson({.twoWeights=true});CHECK(Parse(blended,ReceiptJson(blended)).status==Xm8BoltAssetStatus::Part);
    const auto flipped=RenderJson({.flipNormal=true});CHECK(Parse(flipped,ReceiptJson(flipped)).status==Xm8BoltAssetStatus::Geometry);
    const auto schema=RenderJson({.badSchema=true});CHECK(Parse(schema,ReceiptJson(schema)).status==Xm8BoltAssetStatus::Schema);
    CHECK(Parse(good.substr(0,good.size()-1),ReceiptJson(good)).status==Xm8BoltAssetStatus::Json);
    // The production entry point requires the real ske01 fingerprint.
    CHECK(ParseXm8BoltAsset(Bytes(good),ReceiptJson(good)).status==Xm8BoltAssetStatus::Rig);
    const auto declaredWrong=RenderJson({},SyntheticFingerprint()^1);CHECK(Parse(declaredWrong,ReceiptJson(declaredWrong)).status==Xm8BoltAssetStatus::Rig);
    // Missing files fail soft with a status, never an exception.
    CHECK(LoadXm8BoltAsset("Z:/definitely/missing/xm8-bolt-v1-render-input.json").status==Xm8BoltAssetStatus::NotFound);
    return 0;
}
int PathAndSwitch(){
    const std::array<std::filesystem::path,2> anchors{"D:/repo/reports/native-xr-session-1","D:/repo/build/x86"};
    const auto expected=std::filesystem::path("D:/repo")/std::filesystem::path(std::string(Xm8BoltDefaultAsset));
    const auto exists=[&](const std::filesystem::path& p){return p==expected;};
    CHECK(ResolveXm8BoltAssetPath("",anchors,exists)==expected);
    CHECK(ResolveXm8BoltAssetPath("E:/custom.json",anchors,exists)==std::filesystem::path("E:/custom.json"));
    CHECK(!ResolveXm8BoltAssetPath("",anchors,[](const std::filesystem::path&){return false;}));
    CHECK(Xm8BoltSwitchEnabled(std::nullopt)&&Xm8BoltSwitchEnabled(std::string_view("1"))&&!Xm8BoltSwitchEnabled(std::string_view("0")));
    return 0;
}
Xm8BoltAsset SyntheticAsset(){const auto render=RenderJson();return *Parse(render,ReceiptJson(render)).asset;}
int TravelTransform(){
    const auto a=SyntheticAsset();
    const auto rest=Xm8BoltCarrierFromWeapon(a,0,1);CHECK(rest&&Near(rest->values[3][2],-.38f)&&Near(rest->values[3][1],.07f));
    const auto rear=Xm8BoltCarrierFromWeapon(a,a.stroke,1);CHECK(rear&&Near(rear->values[3][2],-.38f+.119885f));
    CHECK(!Xm8BoltCarrierFromWeapon(a,a.stroke+.001f,1)&&!Xm8BoltCarrierFromWeapon(a,-.001f,1));
    const auto scaled=Xm8BoltCarrierFromWeapon(a,.05f,100);CHECK(scaled&&Near(scaled->values[3][2],(-.38f+.05f)*100,1e-3f));
    const auto observed=Xm8BoltObservedTravel(a,*scaled,100);CHECK(observed&&Near(*observed,.05f,1e-5f));
    auto lateral=*rest;lateral.values[3][0]+=.01f;CHECK(!Xm8BoltObservedTravel(a,lateral,1));
    // The draw transform scales the basis for metre vertices; native units.
    const auto world=Xm8BoltPartWorld(At(1,2,3),100);CHECK(world&&Near(world->values[0][0],100)&&Near(world->values[3][2],3));
    // Vertex at carrier-local +Z goes rear with travel: the bolt follows the carrier rigidly.
    const auto part=Xm8BoltPartWorld(*rear,1);CHECK(part&&Near(part->values[3][2]-rest->values[3][2],a.stroke,1e-6f));
    return 0;
}
RigSnapshot SyntheticRig(){
    RigSnapshot rig;rig.names=Names;rig.parents=Parents;rig.weaponBone=1;rig.identity.count=unsigned(Names.size());
    for(unsigned n=0;n<Names.size();++n){const auto f=NativeInverse(n);math::Matrix4 m{};for(unsigned k=0;k<16;++k)m.values[k/4][k%4]=f[k];
        for(unsigned row=0;row<4;++row)m.values[row][3]=row==3?1.f:0.f;
        for(unsigned i=0;i<4;++i){m.values[2][i]=-m.values[2][i];m.values[i][2]=-m.values[i][2];}rig.inverseBind.push_back(m);}
    return rig;
}
int RigBindingAndCard(){
    auto rig=SyntheticRig();const auto binding=BindXm8BoltRigForFingerprint(rig,SyntheticFingerprint());
    CHECK(binding&&binding->weapon==1&&binding->carrier==2&&binding->handle==3&&binding->card==4);
    CHECK(!BindXm8BoltRig(rig)); // Not the real ske01.
    auto wrongWeapon=rig;wrongWeapon.weaponBone=0;CHECK(!BindXm8BoltRigForFingerprint(wrongWeapon,SyntheticFingerprint()));
    auto hidden=rig;hidden.nativeHiddenLeaves={4};CHECK(!BindXm8BoltRigForFingerprint(hidden,SyntheticFingerprint()));
    // Decision: every prerequisite is required, one answer per palette.
    Xm8BoltCardInputs in{true,true,true,true,true,true};CHECK(DecideXm8BoltCard(in)==Xm8BoltCardReason::Collapse);
    auto x=in;x.enabled=false;CHECK(DecideXm8BoltCard(x)==Xm8BoltCardReason::Disabled);
    x=in;x.assetLoaded=false;CHECK(DecideXm8BoltCard(x)==Xm8BoltCardReason::NoAsset);
    x=in;x.xm8=false;CHECK(DecideXm8BoltCard(x)==Xm8BoltCardReason::OtherWeapon);
    x=in;x.rigBound=false;CHECK(DecideXm8BoltCard(x)==Xm8BoltCardReason::NoRig);
    x=in;x.gunRetargeted=false;CHECK(DecideXm8BoltCard(x)==Xm8BoltCardReason::GunNative);
    x=in;x.rendererHealthy=false;CHECK(DecideXm8BoltCard(x)==Xm8BoltCardReason::RendererUnproven);
    // Collapse zeroes only the card's 12 consumed scalars; padding and others intact.
    std::vector<std::array<std::byte,64>> palette(Names.size());
    for(unsigned n=0;n<palette.size();++n)for(unsigned row=0;row<4;++row){const float v[4]{1.f+n,2.f,3.f,4.f};std::memcpy(palette[n].data()+row*16,v,12);
        palette[n][row*16+12]=std::byte(0xab);}
    const auto collapsed=CollapseXm8BoltCard(palette,*binding);CHECK(collapsed&&collapsed->size()==palette.size());
    for(unsigned n=0;n<palette.size();++n)for(unsigned row=0;row<4;++row){
        for(unsigned b=0;b<12;++b)CHECK((*collapsed)[n][row*16+b]==(n==4?std::byte{0}:palette[n][row*16+b]));
        for(unsigned b=12;b<16;++b)CHECK((*collapsed)[n][row*16+b]==palette[n][row*16+b]);}
    return 0;
}
int DrawHealthLatch(){
    Xm8BoltDrawHealth h(100);CHECK(!h.Healthy(10));
    h.Record(5,0,true,10);CHECK(!h.Healthy(11)); // one eye is not proof
    h.Record(6,1,true,12);CHECK(!h.Healthy(13)); // different frame
    h.Record(6,0,true,14);CHECK(h.Healthy(15)&&h.Healthy(113)&&!h.Healthy(114));
    h.Record(7,0,true,20);h.Record(7,1,false,21);CHECK(!h.Healthy(22)); // any failed eye drops it at once
    h.Record(8,0,true,30);h.Record(8,1,true,31);CHECK(h.Healthy(32));h.Reset();CHECK(!h.Healthy(33));
    return 0;
}
// ---- Cycle gating through the real shared arbiter ----
struct CycleFixture {
    Xm8BoltAsset asset=SyntheticAsset();Bc2Xm8BoltCycle cycle{asset};HandInteraction hands;std::uint64_t intent=0;
    HandInteractionSample input{{1,2,3,4},0,0,0,0,true,{true,true},{false,false}};HandInteractionKey weapon{77,3};
    std::int64_t now=1000000000;float lateral=0;
    Xm8BoltCycleResult last;
    void Next(bool grip){++input.sequence;now+=10000000;input.observedNs=now;input.nowNs=now;input.deadlineNs=now+100000000;input.released[0]=!grip;
        hands.Update(input);
        const auto gun=hands.Current(InteractionHand::Right);const HandContactProof proof{{1,3},input.sequence,input.deadlineNs,true};
        if(gun)hands.Renew(input,gun->token,proof);else hands.Acquire(input,{input.owner,InteractionHand::Right,HandClaimKind::GunHold,weapon,proof,++intent,0});}
    Xm8BoltCycleResult Step(float travel,bool grip,bool magazine=false,bool xm8=true){
        Next(grip);Xm8BoltCycleSample s;s.input=input;s.evidence=input;s.weapon=weapon;s.xm8=xm8;s.gripPressed=grip;s.magazineOwnsLeft=magazine;
        auto hand=At(asset.handleRest[0]+lateral,asset.handleRest[1],asset.handleRest[2]+travel);s.weaponFromHand=hand;
        return last=cycle.Tick(s,hands,intent);
    }
    void Stroke(){for(float t:{.0f,.02f,.04f,.06f,.08f,.1f,.115f,.119f,.119f,.119f})Step(t,true);
        for(float t:{.1f,.08f,.06f,.04f,.02f,.005f,.0f,.0f,.0f})Step(t,true);}
};
int CycleProfileContract(){
    CycleFixture f;CHECK(f.cycle.Valid());const auto& p=f.cycle.Profile();
    CHECK(p.family==WeaponCycleFamily::Pump&&p.axis==(std::array<float,3>{0,0,1})&&Near(p.stroke,.119885f,1e-7f));
    CHECK(Near(p.closedContact.values[3][1],.069722f,1e-6f)&&Near(p.closedContact.values[3][2],-.496687f,1e-6f)&&p.unlockRadians==0);
    return 0;
}
int CycleHoverGrabStrokeComplete(){
    CycleFixture f;f.Step(0,false);CHECK(f.last.phase==WeaponCyclePhase::AwaitingGrip&&!f.last.blocksFire&&!f.last.ownsLeft);
    CHECK(!f.hands.Current(InteractionHand::Left));
    f.Step(0,true);CHECK(f.last.grabbed&&f.last.phase==WeaponCyclePhase::Rear&&f.last.blocksFire&&f.last.ownsLeft);
    const auto left=f.hands.Current(InteractionHand::Left);CHECK(left&&left->token.kind==HandClaimKind::Mechanism&&left->token.contact==Xm8BoltContactKey);
    CHECK(left->token.prerequisiteClaim==f.hands.Current(InteractionHand::Right)->token.id);
    f.Step(.05f,true);CHECK(Near(f.last.travel,.05f,1e-5f)&&f.last.blocksFire); // presentation follows the hand
    bool complete=false;
    for(float t:{.07f,.09f,.11f,.115f,.119f,.119f,.119f})f.Step(t,true);CHECK(f.last.phase==WeaponCyclePhase::Forward);
    for(float t:{.1f,.08f,.06f,.04f,.02f,.005f,.0f,.0f,.0f}){f.Step(t,true);complete|=f.last.presentationComplete;}
    CHECK(complete&&!f.last.blocksFire&&!f.last.ownsLeft&&!f.hands.Current(InteractionHand::Left));
    CHECK(f.cycle.Stats().completions==1);
    // A held grip cannot start another stroke; a release then hover re-arms.
    f.Step(0,true);CHECK(!f.last.ownsLeft);f.Step(0,false);f.Step(0,true);CHECK(f.last.grabbed);
    return 0;
}
int CycleExclusiveWithMagazineAndSupport(){
    {CycleFixture f;f.Step(0,false);f.Step(0,true,true);CHECK(!f.last.ownsLeft&&!f.last.blocksFire&&!f.hands.Current(InteractionHand::Left));}
    {CycleFixture f;f.Step(0,false);f.Step(0,true);f.Step(.03f,true);CHECK(f.last.ownsLeft);
     f.Step(.04f,true,true);CHECK(f.last.cancelled&&!f.last.ownsLeft&&!f.last.blocksFire&&!f.hands.Current(InteractionHand::Left));
     CHECK(f.cycle.Stats().yielded==1);}
    {// Left already supporting the gun while squeezing: no hover, no steal, no fire block.
     CycleFixture f;f.Next(true);const auto gun=f.hands.Current(InteractionHand::Right);
     CHECK(f.hands.Acquire(f.input,{f.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.weapon,{{2,3},f.input.sequence,f.input.deadlineNs,true},++f.intent,gun->token.id}).accepted);
     f.Step(0,true);CHECK(!f.last.ownsLeft&&!f.last.blocksFire&&f.last.phase!=WeaponCyclePhase::AwaitingGrip);
     const auto left=f.hands.Current(InteractionHand::Left);CHECK(left&&left->token.kind==HandClaimKind::WeaponSupport);}
    {// Another lane takes the hand between hover and squeeze: the attempt is refused, its claim kept.
     CycleFixture f;f.Step(0,false);CHECK(f.last.phase==WeaponCyclePhase::AwaitingGrip);
     f.Next(true);const auto gun=f.hands.Current(InteractionHand::Right);
     CHECK(f.hands.Acquire(f.input,{f.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.weapon,{{2,3},f.input.sequence,f.input.deadlineNs,true},++f.intent,gun->token.id}).accepted);
     f.Step(0,true);CHECK(!f.last.ownsLeft&&!f.last.blocksFire&&f.cycle.Stats().claimRejected==1);
     const auto left=f.hands.Current(InteractionHand::Left);CHECK(left&&left->token.kind==HandClaimKind::WeaponSupport);}
    return 0;
}
int CycleRequiresFreshGripAndCancels(){
    {CycleFixture f;f.Step(0,true);f.Step(0,true);CHECK(!f.last.ownsLeft);} // arrives already squeezing: no grab
    {CycleFixture f;f.lateral=.2f;f.Step(0,false);f.Step(0,true);CHECK(!f.last.ownsLeft);} // outside the contact radius
    {CycleFixture f;f.Step(0,false);f.Step(0,true);f.Step(.03f,true);f.Step(.03f,false); // release: arbiter drops, stroke cancels
     CHECK(!f.last.ownsLeft&&!f.last.blocksFire&&!f.hands.Current(InteractionHand::Left));}
    {CycleFixture f;f.Step(0,false);f.Step(0,true);f.Step(.03f,true);f.lateral=.05f;f.Step(.04f,true); // off the rail
     CHECK(f.last.cancelled&&!f.hands.Current(InteractionHand::Left));}
    {CycleFixture f;f.Step(0,false);f.Step(0,true);f.Step(.03f,true,false,false); // weapon switched away
     CHECK(f.last.cancelled&&!f.hands.Current(InteractionHand::Left));}
    return 0;
}
int RealAssetWhenProvided(){
    const char* path=std::getenv("FVR_XM8_BOLT_ASSET");
    if(!path||!*path){std::puts("real XM8 bolt asset: skipped (FVR_XM8_BOLT_ASSET unset)");return 0;}
    const auto r=LoadXm8BoltAsset(path);
    if(r.status!=Xm8BoltAssetStatus::Loaded)std::fprintf(stderr,"asset status %s: %s\n",Xm8BoltAssetStatusName(r.status),r.detail.c_str());
    CHECK(r.status==Xm8BoltAssetStatus::Loaded&&r.asset);const auto& a=*r.asset;
    CHECK(a.triangles==100&&a.vertices==52&&a.mesh.vertices.size()==300&&Near(a.stroke,.119885f,1e-7f));
    CHECK(Near(a.carrierRest[1],.069722f,1e-5f)&&Near(a.carrierRest[2],-.38314f,1e-5f));
    // Carrier-local bounds from the receipt's canonical forward bounds minus the rest frame.
    float lo[3]{1,1,1},hi[3]{-1,-1,-1};
    for(const auto& v:a.mesh.vertices){const float p[3]{v.x,v.y,v.z};for(unsigned k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}}
    CHECK(Near(lo[0],-.0103f,2e-4f)&&Near(hi[0],.0103f,2e-4f));
    CHECK(Near(lo[1],.0267676f-.069722f,2e-4f)&&Near(hi[1],.0646676f-.069722f,2e-4f));
    CHECK(Near(lo[2],-.498642f+.38314f,2e-4f)&&Near(hi[2],-.283642f+.38314f,2e-4f));
    std::printf("real XM8 bolt asset: loaded %u triangles, sha256 %s\n",a.triangles,a.sha256.c_str());
    return 0;
}
} // namespace
int main(){
    if(Sha256Vectors()||AssetParsesAndConverts()||AssetFailsSoft()||PathAndSwitch()||TravelTransform()||RigBindingAndCard()||DrawHealthLatch()||
       CycleProfileContract()||CycleHoverGrabStrokeComplete()||CycleExclusiveWithMagazineAndSupport()||CycleRequiresFreshGripAndCancels()||RealAssetWhenProvided())return 1;
    std::puts("12 XM8 bolt groups passed: asset parse/validation, travel transform, card collapse decision, draw latch, cycle gating (presentation only).");
}
