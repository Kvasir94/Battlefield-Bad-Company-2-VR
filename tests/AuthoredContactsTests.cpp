// Synthetic geometry only: no game animation data is used or committed.
#include "fvr/interaction/AuthoredContacts.h"
#include "fvr/interaction/RigPose.h"
#include "Test.h"
#include <cstdio>
#include <string>
using namespace fvr;
using namespace fvr::interaction;
namespace {
math::Matrix4 Pose(float x,float y,float z,float yaw=0,float roll=0){
    math::Matrix4 m;for(unsigned i=0;i<4;++i)m.values[i][i]=1;
    const float c=std::cos(yaw),s=std::sin(yaw);math::Matrix4 r;for(unsigned i=0;i<4;++i)r.values[i][i]=1;
    r.values[0]={c,0,-s,0};r.values[2]={s,0,c,0};
    math::Matrix4 q;for(unsigned i=0;i<4;++i)q.values[i][i]=1;const float cr=std::cos(roll),sr=std::sin(roll);
    q.values[1]={0,cr,sr,0};q.values[2]={0,-sr,cr,0};
    m=Multiply(q,r);m.values[3]={x,y,z,1};return m;}
std::string Row(const math::Matrix4& m){std::string s;for(unsigned n=0;n<16;++n){char b[32];std::snprintf(b,sizeof b," %.9g",double(m.values[n/4][n%4]));s+=b;}return s;}
const char* const Fingers[15]{"LThumb1","LThumb2","LThumb3","LIndex1","LIndex2","LIndex3","LMiddle1","LMiddle2","LMiddle3","LRing1","LRing2","LRing3","LPinky1","LPinky2","LPinky3"};
std::string Library(unsigned fingers=15,const std::string& extra=""){
    std::string t="fvr-authored-contacts 1\n# synthetic\ncontact support left TEST_gun wpn_root a7f2 "+std::string(64,'a')+" 0.0"+Row(Pose(.05f,-.05f,-.4f,.3f))+"\n";
    for(unsigned f=0;f<fingers;++f)t+=std::string("finger ")+Fingers[f]+Row(Pose(.02f*float(f%3+1),0,.01f*float(f/3),.1f*float(f%3)))+"\n";
    t+="contact magazine left TEST_gun wpn_mag a7f2 "+std::string(64,'b')+" 0.5"+Row(Pose(.07f,-.07f,0,1.f))+"\n";
    return t+extra;}
float Dist(const math::Matrix4& a,const math::Matrix4& b){float d=0;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)d=std::max(d,std::fabs(a.values[r][c]-b.values[r][c]));return d;}

int ParseAndFind(){
    std::string error;const auto lib=AuthoredContactLibrary::Parse(Library(),&error);CHECK(lib&&lib->Size()==2);
    const auto support=lib->Find("TEST_gun",AuthoredContactKind::Support,0,0xa7f2);
    CHECK(support.source==AuthoredContactSource::Authored&&support.contact->fingerCount==15&&support.contact->part=="wpn_root");
    const auto mag=lib->Find("TEST_gun",AuthoredContactKind::MagazineGrasp,0,0xa7f2);CHECK(mag.contact&&mag.contact->fingerCount==0&&Near(mag.contact->timeSeconds,.5f));
    // Fallback: other asset, hand, kind or skeleton.
    CHECK(!lib->Find("OTHER",AuthoredContactKind::Support,0,0xa7f2).contact&&!lib->Find("TEST_gun",AuthoredContactKind::Support,1,0xa7f2).contact);
    CHECK(!lib->Find("TEST_gun",AuthoredContactKind::Handle,0,0xa7f2).contact&&!lib->Find("TEST_gun",AuthoredContactKind::Support,0,0xa7f3).contact);
    CHECK(lib->Find("TEST_gun",AuthoredContactKind::Handle,0,0xa7f2).source==AuthoredContactSource::Fallback);
    auto spaced=Library();
    for(std::size_t at=0;(at=spaced.find("TEST_gun",at))!=std::string::npos;at+=10)spaced.replace(at,8,"TEST%20gun");
    const auto decoded=AuthoredContactLibrary::Parse(spaced);
    CHECK(decoded&&decoded->Find("TEST gun",AuthoredContactKind::Support,0,0xa7f2).contact);
    CHECK(!decoded->Find("TEST%20gun",AuthoredContactKind::Support,0,0xa7f2).contact);
    CHECK(authored_contact_detail::Asset("A%25B")==std::optional<std::string>("A%B"));
    for(const auto token:{"A%","A%2","A%GG","A%00","A%0a","A%7f"})CHECK(!authored_contact_detail::Asset(token));
    // Any malformed record rejects the whole library.
    CHECK(!AuthoredContactLibrary::Parse("not a library\n"));
    CHECK(!AuthoredContactLibrary::Parse(Library(14),&error)&&error.find("15 fingers")!=std::string::npos);
    CHECK(!AuthoredContactLibrary::Parse(Library(15,"contact support left TEST_gun wpn_root a7f2 "+std::string(64,'c').append(" 0")+Row(Pose(0,0,0))+"\n")));
    auto skewed=Pose(0,0,0);skewed.values[0][0]=2;
    CHECK(!AuthoredContactLibrary::Parse("fvr-authored-contacts 1\ncontact support left A p 1 "+std::string(64,'a')+" 0"+Row(skewed)+"\n"));
    CHECK(!AuthoredContactLibrary::Parse("fvr-authored-contacts 1\ncontact support left A p 1 "+std::string(64,'a')+" 0"+Row(Pose(3,0,0))+"\n"));
    CHECK(!AuthoredContactLibrary::Parse("fvr-authored-contacts 1\ncontact support middle A p 1 "+std::string(64,'a')+" 0"+Row(Pose(0,0,0))+"\n"));
    CHECK(!AuthoredContactLibrary::Parse("fvr-authored-contacts 1\nfinger X"+Row(Pose(0,0,0))+"\n"));
    CHECK(AuthoredContactLibrary::Parse("fvr-authored-contacts 1\r\n# crlf\r\n")->Size()==0);
    return 0;}
// Bound wrist = C * part. One rigid transform of the part (including an upside
// down weapon) moves the bound wrist with it; the local contact is unchanged.
int Equivariance(){
    const auto lib=*AuthoredContactLibrary::Parse(Library());const auto& c=*lib.Find("TEST_gun",AuthoredContactKind::Support,0,0xa7f2).contact;
    for(const auto& part:{Pose(0,0,0),Pose(1,2,3,.7f),Pose(-2,1,0,2.f,3.14159265f)}){
        const auto wrist=BindAuthoredWrist(c,part,1);
        const auto local=Multiply(wrist,*InverseRigid(part));CHECK(Dist(local,c.wristInPart)<1e-5f);}
    // World units scale translation only.
    const auto scaled=AuthoredWristLocal(c,100);CHECK(Near(scaled.values[3][2],-40.f,1e-3f)&&Near(scaled.values[0][0],c.wristInPart.values[0][0]));
    return 0;}
// Fingers replace the 15 joints on the final wrist and carry written tips.
int Fingers15(){
    const auto lib=*AuthoredContactLibrary::Parse(Library());const auto& c=*lib.Find("TEST_gun",AuthoredContactKind::Support,0,0xa7f2).contact;
    std::vector<std::string> names{"root","LHand"};std::vector<std::int32_t> parents{-1,0};
    for(unsigned f=0;f<15;++f){names.push_back(Fingers[f]);parents.push_back(f%3==0?1:std::int32_t(names.size()-2));}
    names.push_back("LIndexTip");parents.push_back(std::int32_t(2+5)); // child of LIndex3
    const auto wrist=Pose(.3f,1.2f,.1f,.5f,.2f);
    std::vector<BoneWrite> writes{{1,wrist}};
    for(std::uint32_t n=2;n<names.size();++n)writes.push_back({n,Pose(9,9,float(n))});
    const auto tipOld=writes.back().transform;const auto index3Old=writes[7-1+0].transform; // bone 7 = LIndex3
    CHECK(writes[6].index==7);
    CHECK(ApplyAuthoredFingers(parents,names,writes,c,wrist,1.f));
    for(unsigned f=0;f<15;++f){const auto& w=writes[1+f];CHECK(w.index==2+f&&Dist(w.transform,Multiply(c.fingerInWrist[f],wrist))<1e-5f);}
    // The tip keeps its previous local relation to LIndex3.
    const auto tipLocalOld=Multiply(tipOld,*InverseRigid(index3Old));
    CHECK(Dist(Multiply(writes.back().transform,*InverseRigid(writes[6].transform)),tipLocalOld)<1e-4f);
    // Missing finger bone: nothing changes.
    auto other=names;other[4]="Renamed";auto untouched=writes;
    CHECK(!ApplyAuthoredFingers(parents,other,untouched,c,Pose(5,5,5),1.f));
    for(std::size_t n=0;n<writes.size();++n)CHECK(Dist(untouched[n].transform,writes[n].transform)==0);
    // A wrist-only contact never touches fingers.
    const auto& mag=*lib.Find("TEST_gun",AuthoredContactKind::MagazineGrasp,0,0xa7f2).contact;CHECK(!ApplyAuthoredFingers(parents,names,untouched,mag,wrist,1.f));
    return 0;}
}
int main(){if(ParseAndFind()||Equivariance()||Fingers15())return 1;
    std::puts("AuthoredContacts: strict private-library parse, exact lookup with fallback, rigid equivariance incl. inversion, 15-finger application with carried tips passed (synthetic data).");return 0;}
