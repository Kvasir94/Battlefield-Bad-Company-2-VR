// Synthetic geometry only: no game animation data is used or committed.
#include "Bc2AuthoredContacts.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
using namespace fvr;
using namespace fvr::bc2;
namespace {
math::Matrix4 Pose(float x,float y,float z){math::Matrix4 m;for(unsigned i=0;i<4;++i)m.values[i][i]=1;m.values[3]={x,y,z,1};return m;}
std::string Row(const math::Matrix4& m){std::string s;for(unsigned n=0;n<16;++n){char b[32];std::snprintf(b,sizeof b," %.9g",double(m.values[n/4][n%4]));s+=b;}return s;}
}
int main(){
    // Path policy: kill switch, explicit path, DLL-adjacent default.
    CHECK(!ResolveAuthoredContactsPath("0","C:\\game"));
    CHECK(*ResolveAuthoredContactsPath("D:\\private\\c.txt","C:\\game")=="D:\\private\\c.txt");
    CHECK(*ResolveAuthoredContactsPath("","C:\\game")=="C:\\game\\authored-contacts-v1.txt");
    CHECK(!ResolveAuthoredContactsPath("",""));
    // Text load: valid and invalid.
    std::shared_ptr<const interaction::AuthoredContactLibrary> lib;
    const std::string text="fvr-authored-contacts 1\ncontact support left TEST_gun wpn_part a7f219a1426216ab "+std::string(64,'a')+" 0"+Row(Pose(0,.1f,0))+"\n";
    auto status=LoadAuthoredContactsText(text,lib);CHECK(status.state==AuthoredContactsState::Loaded&&status.contacts==1&&lib);
    std::shared_ptr<const interaction::AuthoredContactLibrary> bad;
    status=LoadAuthoredContactsText("garbage",bad);CHECK(status.state==AuthoredContactsState::Invalid&&!bad&&!status.error.empty());
    // Rig binding: the part must exist exactly once; the wrist is re-expressed
    // relative to the native weapon bone when the part is a different bone.
    RigSnapshot rig;rig.names={"root","jntWpn_1","wpn_part","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
    std::vector<math::Matrix4> native{Pose(0,0,0),Pose(1,0,0),Pose(1,0,.5f),Pose(0,1,0)};
    const auto* c=AuthoredContactFor("TEST_gun",interaction::AuthoredContactKind::Support,0,0xa7f219a1426216abull,rig,lib.get());CHECK(c);
    CHECK(!AuthoredContactFor("TEST_gun",interaction::AuthoredContactKind::Support,0,0x1234,rig,lib.get()));
    CHECK(!AuthoredContactFor("",interaction::AuthoredContactKind::Support,0,0xa7f219a1426216abull,rig,lib.get()));
    auto twice=rig;twice.names[3]="wpn_part";CHECK(!AuthoredContactFor("TEST_gun",interaction::AuthoredContactKind::Support,0,0xa7f219a1426216abull,twice,lib.get()));
    const auto local=AuthoredContactWeaponLocal(*c,rig,native,100.f);CHECK(local);
    // wrist world = (0,10,0) cm-scale offset on the part at (1,0,.5) -> relative to weapon at (1,0,0): (0,10,50).
    CHECK(std::fabs(local->values[3][0])<1e-4f&&std::fabs(local->values[3][1]-10)<1e-4f&&std::fabs(local->values[3][2]-.5f)<1e-4f);
    rig.weaponBone=2;const auto same=AuthoredContactWeaponLocal(*c,rig,native,100.f);CHECK(same&&std::fabs(same->values[3][1]-10)<1e-4f&&same->values[3][2]==0);
    CHECK(!AuthoredContactWeaponLocal(*c,rig,native,0.f));
    // Optional private check (skipped by default and in CI): parse a real
    // exported library named by FVR_TEST_AUTHORED_CONTACTS.
    if(const char* path=std::getenv("FVR_TEST_AUTHORED_CONTACTS");path&&*path){
        std::ifstream file(path,std::ios::binary);CHECK(file);std::ostringstream t;t<<file.rdbuf();
        std::shared_ptr<const interaction::AuthoredContactLibrary> real;const auto s=LoadAuthoredContactsText(t.str(),real);
        if(s.state!=AuthoredContactsState::Loaded)std::fprintf(stderr,"%s\n",s.error.c_str());
        CHECK(s.state==AuthoredContactsState::Loaded&&real->Size()>=1);
        for(const auto& contact:real->Contacts())std::printf("private contact kind %u hand %u %s/%s fingers %u\n",unsigned(contact.kind),contact.hand,contact.asset.c_str(),contact.part.c_str(),contact.fingerCount);
    }
    std::puts("Bc2AuthoredContacts: path policy and kill switch, strict load, exact skeleton/part binding and weapon-bone re-expression passed (synthetic data).");
    return 0;
}
