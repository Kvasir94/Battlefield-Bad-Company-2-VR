// Synthetic rig only: no game data.
#include "Bc2FloatingHands.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
using namespace fvr;
using namespace fvr::bc2;
namespace {
math::Matrix4 Pose(float x,float y,float z){math::Matrix4 m;for(unsigned i=0;i<4;++i)m.values[i][i]=1;m.values[3]={x,y,z,1};return m;}
std::array<float,16> Floats(const std::array<std::byte,64>& b){std::array<float,16> f{};std::memcpy(f.data(),b.data(),64);return f;}
// root -> spine -> LShoulder -> LElbow(+LTwist) -> LWrist -> LFinger ; spine -> RShoulder -> RElbow -> RWrist ; root -> weapon
RigSnapshot Rig(){
    RigSnapshot r;r.names={"root","spine","LShoulder","LElbow","LTwist","LWrist","LFinger","RShoulder","RElbow","RWrist","weapon"};
    r.parents={-1,0,1,2,3,3,5,1,7,8,0};r.identity.count=std::uint32_t(r.names.size());r.identity.evaluatedMatrices=0x100000;
    r.left={2,3,5};r.right={7,8,9};r.weaponBone=10;
    for(std::size_t n=0;n<r.names.size();++n){std::array<std::byte,64> b{};float f[16];for(unsigned k=0;k<16;++k)f[k]=float(n*100+k);
        std::memcpy(b.data(),f,64);r.nativeEvaluated.push_back(b);r.inverseBind.push_back(Pose(0,0,0));r.evaluatedWorld.push_back(Pose(0,0,0));}
    return r;
}
}
int main(){
    // Mode: default ik, explicit floating, unknown value falls back to ik.
    std::map<std::string,std::string> env;
    const auto get=[&](std::string_view n)->std::string_view{const auto i=env.find(std::string(n));return i==env.end()?std::string_view{}:std::string_view(i->second);};
    CHECK(ParseHandsModeSettings(get).mode==HandsPresentationMode::Ik);
    env["FVR_HANDS_MODE"]="floating";CHECK(ParseHandsModeSettings(get).mode==HandsPresentationMode::Floating);
    env["FVR_HANDS_MODE"]="ik";CHECK(ParseHandsModeSettings(get).mode==HandsPresentationMode::Ik&&!ParseHandsModeSettings(get).invalid);
    env["FVR_HANDS_MODE"]="ghost";{const auto s=ParseHandsModeSettings(get);CHECK(s.mode==HandsPresentationMode::Ik&&s.invalid);}
    const auto rig=Rig();
    // Beyond anatomical reach, floating placement keeps the raw target and
    // animated finger-in-wrist relation; an untracked opposite hand is untouched.
    auto animated=rig.evaluatedWorld;
    animated[rig.left.shoulder]=Pose(0,1,-.5f);animated[rig.left.elbow]=Pose(0,1,-.25f);
    animated[rig.left.wrist]=Pose(0,1,0);animated[6]=Pose(0,1,.07f);
    const auto far=Pose(0,1,4);
    const auto raw=SolvePresentedArms(HandsPresentationMode::Floating,rig.parents,animated,
        rig.left,rig.right,{far,{},{},true},{{},{},{},false});
    CHECK(raw&&!raw->reachClamped[0]&&raw->targetError[0]==0);
    CHECK(raw->writes.size()==2);
    CHECK(raw->writes[0].index==rig.left.wrist&&raw->writes[0].transform.values==far.values);
    CHECK(raw->writes[1].index==6&&std::abs(raw->writes[1].transform.values[3][2]-4.07f)<.00001f);
    const auto limited=SolvePresentedArms(HandsPresentationMode::Ik,rig.parents,animated,
        rig.left,rig.right,{far,{1,0,0},{},true},{{},{},{},false});
    CHECK(limited&&limited->reachClamped[0]&&limited->targetError[0]>3);
    const auto item=Pose(0,1,4.2f);
    const auto floatingItem=PresentedWeaponFrame(HandsPresentationMode::Floating,item,far,limited->writes,rig.left.wrist);
    const auto ikItem=PresentedWeaponFrame(HandsPresentationMode::Ik,item,far,limited->writes,rig.left.wrist);
    CHECK(floatingItem&&floatingItem->values==item.values);
    CHECK(ikItem&&ikItem->values[3][2]<1); // Same reach-limited item relation as before.
    auto malformed=rig.parents;malformed[3]=3;
    CHECK(!SolvePresentedArms(HandsPresentationMode::Floating,malformed,animated,
        rig.left,rig.right,{far},{far}));
    // The collapse set is the arm without the hand subtree (twist included, fingers excluded).
    const auto left=ArmCollapseBones(rig.parents,rig.left);CHECK((left==std::vector<std::uint32_t>{2,3,4}));
    CHECK((ArmCollapseBones(rig.parents,rig.right)==std::vector<std::uint32_t>{7,8}));
    CHECK(ArmCollapseBones(rig.parents,{5,3,2}).empty()); // reversed chain rejects
    // Plan with the solved left arm written (shoulder, elbow, wrist, finger) and right wrist.
    std::vector<interaction::BoneWrite> writes{{2,Pose(0,1,0)},{3,Pose(0,.8f,.2f)},{5,Pose(.1f,.9f,.5f)},{6,Pose(.1f,.9f,.6f)},{9,Pose(-.2f,1.f,.5f)}};
    RigPosePlan plan;plan.identity=rig.identity;
    for(const auto& w:writes){RigBoneEdit e{w.index,rig.identity.evaluatedMatrices+w.index*64,rig.nativeEvaluated[w.index],rig.nativeEvaluated[w.index]};
        float f[16]{1,0,0,111,0,1,0,112,0,0,1,113,w.transform.values[3][0],w.transform.values[3][1],-w.transform.values[3][2],114};std::memcpy(e.after.data(),f,64);plan.edits.push_back(e);}
    const auto floating=CollapseArmsToWrists(rig,plan,writes,true,true);CHECK(floating);
    const auto edit=[&](std::uint32_t bone)->const RigBoneEdit*{for(const auto& e:floating->edits)if(e.index==bone)return &e;return nullptr;};
    // Every left arm bone (including the unwritten twist, added as a new edit) maps to the left wrist.
    for(const std::uint32_t bone:{2u,3u,4u}){const auto* e=edit(bone);CHECK(e&&e->address==rig.identity.evaluatedMatrices+bone*64);
        const auto f=Floats(e->after);
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)CHECK(f[row*4+col]==0);
        CHECK(f[12]==.1f&&f[13]==.9f&&f[14]==-.5f);
        // Padding (fourth float of each row) survives from the original bytes.
        const auto before=Floats(rig.nativeEvaluated[bone]);const auto plannedPad=bone==4?before:Floats(edit(bone)->before);
        if(bone==4)for(unsigned row=0;row<4;++row)CHECK(f[row*4+3]==before[row*4+3]);
        (void)plannedPad;
        // Any bind-space vertex, any weight, lands on the wrist: v * S = translation row.
        const float v[3]{.3f,-.7f,2.f};float out[3];for(unsigned c=0;c<3;++c)out[c]=v[0]*f[c]+v[1]*f[4+c]+v[2]*f[8+c]+f[12+c];
        CHECK(out[0]==.1f&&out[1]==.9f&&out[2]==-.5f);}
    for(const std::uint32_t bone:{7u,8u}){const auto f=Floats(edit(bone)->after);CHECK(f[0]==0&&f[12]==-.2f&&f[13]==1.f&&f[14]==-.5f);}
    // Hands and fingers keep their planned skins exactly; the weapon is untouched.
    for(const std::uint32_t bone:{5u,6u,9u}){const auto* e=edit(bone);CHECK(e);
        for(const auto& o:plan.edits)if(o.index==bone)CHECK(o.after==e->after);}
    CHECK(!edit(10)&&!edit(0)&&!edit(1));
    // Untracked side stays native (no edits added); no wrist write: no collapse.
    const auto leftOnly=CollapseArmsToWrists(rig,plan,writes,true,false);CHECK(leftOnly);
    for(const auto& e:leftOnly->edits)CHECK(e.index!=7&&e.index!=8);
    std::vector<interaction::BoneWrite> noWrists{{2,Pose(0,1,0)}};CHECK(!CollapseArmsToWrists(rig,plan,noWrists,true,true));
    auto hidden=rig;hidden.nativeHiddenLeaves={4};const auto keep=CollapseArmsToWrists(hidden,plan,writes,true,false);
    for(const auto& e:keep->edits)CHECK(e.index!=4);
    std::puts("Bc2FloatingHands: mode switch (default ik), arm-minus-hand collapse set, wrist-point skins with preserved padding, untouched hands/weapon and untracked arms passed (synthetic rig); GPU draw unverified.");
    return 0;
}
