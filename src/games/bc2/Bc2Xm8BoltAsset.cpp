#include "Bc2Xm8BoltAsset.h"
#include "Bc2SightContact.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <utility>

namespace fvr::bc2 {
namespace {
// ---- Minimal bounded JSON DOM (objects, arrays, strings, numbers, literals) ----
struct Json {
    enum class Type : unsigned char {Null,Bool,Number,String,Array,Object} type=Type::Null;
    bool boolean=false;double number=0;std::string text;
    std::vector<Json> items;std::vector<std::pair<std::string,Json>> fields;
    const Json* operator[](std::string_view key)const noexcept {
        if(type!=Type::Object)return nullptr;
        for(const auto& [name,value]:fields)if(name==key)return &value;return nullptr;
    }
};
class JsonReader {
public:
    explicit JsonReader(std::string_view s):s_(s){}
    std::optional<Json> Document(){Json out;Space();if(!Value(out,0))return {};Space();if(at_!=s_.size())return {};return out;}
private:
    void Space()noexcept{while(at_<s_.size()&&(s_[at_]==' '||s_[at_]=='\n'||s_[at_]=='\r'||s_[at_]=='\t'))++at_;}
    bool Literal(std::string_view word)noexcept{if(s_.substr(at_,word.size())!=word)return false;at_+=word.size();return true;}
    bool Value(Json& out,unsigned depth){
        if(depth>64||at_>=s_.size())return false;const char c=s_[at_];
        if(c=='{'){out.type=Json::Type::Object;++at_;Space();if(at_<s_.size()&&s_[at_]=='}'){++at_;return true;}
            for(;;){Space();std::string key;if(!String(key))return false;Space();if(at_>=s_.size()||s_[at_]!=':')return false;++at_;Space();
                Json value;if(!Value(value,depth+1))return false;out.fields.emplace_back(std::move(key),std::move(value));Space();
                if(at_<s_.size()&&s_[at_]==','){++at_;continue;}if(at_<s_.size()&&s_[at_]=='}'){++at_;return true;}return false;}}
        if(c=='['){out.type=Json::Type::Array;++at_;Space();if(at_<s_.size()&&s_[at_]==']'){++at_;return true;}
            for(;;){Space();Json value;if(!Value(value,depth+1))return false;out.items.push_back(std::move(value));Space();
                if(at_<s_.size()&&s_[at_]==','){++at_;continue;}if(at_<s_.size()&&s_[at_]==']'){++at_;return true;}return false;}}
        if(c=='"'){out.type=Json::Type::String;return String(out.text);}
        if(Literal("true")){out.type=Json::Type::Bool;out.boolean=true;return true;}
        if(Literal("false")){out.type=Json::Type::Bool;return true;}
        if(Literal("null"))return true;
        return Number(out);
    }
    bool String(std::string& out){
        if(at_>=s_.size()||s_[at_]!='"')return false;++at_;
        while(at_<s_.size()){const char c=s_[at_++];if(c=='"')return true;
            if(static_cast<unsigned char>(c)<0x20)return false;
            if(c!='\\'){out.push_back(c);continue;}
            if(at_>=s_.size())return false;const char e=s_[at_++];
            switch(e){case '"':case '\\':case '/':out.push_back(e);break;case 'b':out.push_back('\b');break;case 'f':out.push_back('\f');break;
                case 'n':out.push_back('\n');break;case 'r':out.push_back('\r');break;case 't':out.push_back('\t');break;
                case 'u':{if(at_+4>s_.size())return false;unsigned code=0;
                    for(unsigned n=0;n<4;++n){const char h=s_[at_++];code<<=4;
                        if(h>='0'&&h<='9')code|=unsigned(h-'0');else if(h>='a'&&h<='f')code|=unsigned(h-'a'+10);else if(h>='A'&&h<='F')code|=unsigned(h-'A'+10);else return false;}
                    // Identifiers here are ASCII; keep other code points as UTF-8.
                    if(code<0x80)out.push_back(char(code));else if(code<0x800){out.push_back(char(0xc0|(code>>6)));out.push_back(char(0x80|(code&63)));}
                    else {out.push_back(char(0xe0|(code>>12)));out.push_back(char(0x80|((code>>6)&63)));out.push_back(char(0x80|(code&63)));}
                    break;}
                default:return false;}
        }
        return false;
    }
    bool Number(Json& out){
        const auto begin=at_;
        while(at_<s_.size()&&(std::strchr("+-0123456789.eE",s_[at_])!=nullptr)&&s_[at_]!='\0')++at_;
        if(begin==at_||at_-begin>64)return false;
        char buffer[72]{};std::memcpy(buffer,s_.data()+begin,at_-begin);char* end=nullptr;
        out.number=std::strtod(buffer,&end);if(end!=buffer+(at_-begin)||!std::isfinite(out.number))return false;
        out.type=Json::Type::Number;return true;
    }
    std::string_view s_;std::size_t at_=0;
};
const Json* Path(const Json& root,std::initializer_list<std::string_view> keys)noexcept {
    const Json* at=&root;for(auto key:keys){at=(*at)[key];if(!at)return nullptr;}return at;
}
std::optional<double> Number(const Json* j)noexcept {if(!j||j->type!=Json::Type::Number)return {};return j->number;}
std::optional<bool> Bool(const Json* j)noexcept {if(!j||j->type!=Json::Type::Bool)return {};return j->boolean;}
const std::string* Text(const Json* j)noexcept {return j&&j->type==Json::Type::String?&j->text:nullptr;}
std::optional<std::array<float,3>> Vec3(const Json* j)noexcept {
    if(!j||j->type!=Json::Type::Array||j->items.size()!=3)return {};std::array<float,3> v{};
    for(unsigned n=0;n<3;++n){const auto x=Number(&j->items[n]);if(!x||std::abs(*x)>100)return {};v[n]=float(*x);}return v;
}
bool Near(const std::array<float,3>& a,const std::array<float,3>& b,float e)noexcept {
    for(unsigned n=0;n<3;++n)if(!(std::abs(a[n]-b[n])<=e))return false;return true;
}

// ---- SHA-256 (FIPS 180-4) ----
struct Sha256 {
    std::array<std::uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<unsigned char,64> block{};std::size_t used=0;std::uint64_t bits=0;
    static std::uint32_t R(std::uint32_t x,unsigned n)noexcept{return (x>>n)|(x<<(32-n));}
    void Compress()noexcept {
        static constexpr std::array<std::uint32_t,64> k{
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
            0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
            0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
            0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::array<std::uint32_t,64> w{};
        for(unsigned n=0;n<16;++n)w[n]=std::uint32_t(block[n*4])<<24|std::uint32_t(block[n*4+1])<<16|std::uint32_t(block[n*4+2])<<8|block[n*4+3];
        for(unsigned n=16;n<64;++n){const auto s0=R(w[n-15],7)^R(w[n-15],18)^(w[n-15]>>3),s1=R(w[n-2],17)^R(w[n-2],19)^(w[n-2]>>10);w[n]=w[n-16]+s0+w[n-7]+s1;}
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],i=h[7];
        for(unsigned n=0;n<64;++n){const auto t1=i+(R(e,6)^R(e,11)^R(e,25))+((e&f)^(~e&g))+k[n]+w[n],t2=(R(a,2)^R(a,13)^R(a,22))+((a&b)^(a&c)^(b&c));
            i=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=i;
    }
    void Add(unsigned char byte)noexcept{block[used++]=byte;if(used==64){Compress();used=0;}}
    std::string Finish(){
        const auto length=bits;Add(0x80);while(used!=56)Add(0);for(int n=7;n>=0;--n)Add((unsigned char)(length>>(n*8)));
        std::string out;char hex[9]{};for(auto v:h){std::snprintf(hex,sizeof(hex),"%08x",v);out+=hex;}return out;
    }
};
std::uint64_t Fnv(std::string_view s)noexcept {std::uint64_t h=14695981039346656037ull;for(unsigned char c:s)h=(h^c)*1099511628211ull;return h;}
Xm8BoltAssetResult Fail(Xm8BoltAssetStatus status,std::string detail){Xm8BoltAssetResult r;r.status=status;r.detail=std::move(detail);return r;}
math::Matrix4 Canonical(const Json& flat){
    math::Matrix4 m{};for(unsigned n=0;n<16;++n)m.values[n/4][n%4]=float(flat.items[n].number);
    // Exactly Bc2Rig.cpp's conversion, so the fingerprint is bit-comparable.
    for(unsigned row=0;row<4;++row)m.values[row][3]=row==3?1.f:0.f;
    for(unsigned i=0;i<4;++i){m.values[2][i]=-m.values[2][i];m.values[i][2]=-m.values[i][2];}
    return m;
}
bool Flat16(const Json& j)noexcept {
    if(j.type!=Json::Type::Array||j.items.size()!=16)return false;
    for(const auto& x:j.items)if(x.type!=Json::Type::Number||std::abs(x.number)>1000)return false;return true;
}
} // namespace

const char* Xm8BoltAssetStatusName(Xm8BoltAssetStatus s)noexcept {
    static constexpr const char* names[]{"loaded","disabled","not_found","read","json","schema","rig","part","geometry","validation","validation_mismatch","exception"};
    return unsigned(s)<unsigned(Xm8BoltAssetStatus::Count)?names[unsigned(s)]:"unknown";
}
std::string Xm8BoltSha256Hex(std::span<const std::byte> bytes){
    Sha256 sha;for(auto b:bytes)sha.Add(std::to_integer<unsigned char>(b));sha.bits=std::uint64_t(bytes.size())*8;return sha.Finish();
}
bool Xm8BoltSwitchEnabled(std::optional<std::string_view> value)noexcept {return !value||*value!="0";}

Xm8BoltAssetResult ParseXm8BoltAsset(std::span<const std::byte> renderBytes,std::string_view validationJson){
    return ParseXm8BoltAssetForRig(renderBytes,validationJson,Xm8BoltRigFingerprint);
}
Xm8BoltAssetResult ParseXm8BoltAssetForRig(std::span<const std::byte> renderBytes,std::string_view validationJson,std::uint64_t rigFingerprint){
    using S=Xm8BoltAssetStatus;
    if(renderBytes.empty()||renderBytes.size()>16*1024*1024||validationJson.empty()||validationJson.size()>1024*1024)return Fail(S::Read,"size");
    const std::string_view renderText(reinterpret_cast<const char*>(renderBytes.data()),renderBytes.size());
    const auto render=JsonReader(renderText).Document();if(!render||render->type!=Json::Type::Object)return Fail(S::Json,"render_input");
    const auto receipt=JsonReader(validationJson).Document();if(!receipt||receipt->type!=Json::Type::Object)return Fail(S::Json,"validation");
    const auto* schema=Text((*render)["schema"]);if(!schema||*schema!=Xm8BoltRenderSchema)return Fail(S::Schema,"render_schema");
    const auto* receiptSchema=Text((*receipt)["schema"]);if(!receiptSchema||*receiptSchema!=Xm8BoltValidationSchema)return Fail(S::Schema,"validation_schema");

    // Rig: exact ske01 names/parents/canonical inverse binds (SightRigFingerprint).
    const auto* names=Path(*render,{"rig","names"});const auto* parents=Path(*render,{"rig","parents"});const auto* binds=Path(*render,{"rig","inverse_bind"});
    if(!names||!parents||!binds||names->type!=Json::Type::Array||parents->type!=Json::Type::Array||binds->type!=Json::Type::Array||
       names->items.empty()||names->items.size()>1024||parents->items.size()!=names->items.size()||binds->items.size()!=names->items.size())return Fail(S::Rig,"rig_arrays");
    std::vector<std::string> boneNames;std::vector<std::int32_t> boneParents;std::vector<math::Matrix4> inverseBind;
    for(std::size_t n=0;n<names->items.size();++n){
        const auto* name=Text(&names->items[n]);const auto parent=Number(&parents->items[n]);
        if(!name||!parent||*parent<-1||*parent>=double(names->items.size())||*parent!=std::floor(*parent)||!Flat16(binds->items[n]))return Fail(S::Rig,"rig_entry");
        boneNames.push_back(*name);boneParents.push_back(std::int32_t(*parent));inverseBind.push_back(Canonical(binds->items[n]));
    }
    if(!rigFingerprint||SightRigFingerprint(boneNames,boneParents,inverseBind)!=rigFingerprint)return Fail(S::Rig,"rig_fingerprint");
    const auto bone=[&](std::string_view name)->std::optional<std::size_t>{
        const auto at=std::find(boneNames.begin(),boneNames.end(),name);
        if(at==boneNames.end()||std::find(at+1,boneNames.end(),name)!=boneNames.end())return {};return std::size_t(at-boneNames.begin());};
    const auto weapon=bone(Xm8BoltWeaponBone),carrier=bone(Xm8BoltCarrierBone),handle=bone(Xm8BoltHandleBone),card=bone(Xm8BoltCardBone);
    if(!weapon||!carrier||!handle||!card||boneParents[*carrier]!=std::int32_t(*weapon)||boneParents[*handle]!=std::int32_t(*carrier)||
       boneParents[*card]!=std::int32_t(*weapon))return Fail(S::Rig,"rig_topology");
    const auto* declared=Text(Path(*render,{"rig","rig_fingerprint"}));
    {char expected[40]{};std::snprintf(expected,sizeof(expected),"fnv1a64:%016llx",static_cast<unsigned long long>(rigFingerprint));
     if(!declared||*declared!=expected)return Fail(S::Rig,"declared_fingerprint");}

    // One rigid part on jntWpnwpnJnt_16.
    const auto* parts=(*render)["parts"];if(!parts||parts->type!=Json::Type::Array||parts->items.size()!=1)return Fail(S::Part,"part_count");
    const auto& part=parts->items[0];
    const auto* skin=Text(part["skin_joint"]);if(!skin||*skin!=Xm8BoltCarrierBone)return Fail(S::Part,"skin_joint");
    const auto* vertices=part["vertices"];const auto* triangles=part["triangles"];
    if(!vertices||!triangles||vertices->type!=Json::Type::Array||triangles->type!=Json::Type::Array||vertices->items.size()<4||vertices->items.size()>20000||
       triangles->items.size()<4||triangles->items.size()>32768)return Fail(S::Part,"part_arrays");
    const auto rest=Vec3(part["joint_rest_relative_to_jntWpn_1_canonical"]);if(!rest)return Fail(S::Part,"joint_rest");
    const auto& inverse=inverseBind[*carrier];
    std::vector<std::array<float,3>> points;points.reserve(vertices->items.size());
    for(const auto& v:vertices->items){
        const auto position=Vec3(v["position"]);const auto* weights=v["weights"];
        if(!position||!weights||weights->type!=Json::Type::Object||weights->fields.size()!=1||weights->fields[0].first!=Xm8BoltCarrierBone)return Fail(S::Part,"vertex_weights");
        const auto weight=Number(&weights->fields[0].second);if(!weight||std::abs(*weight-1)>1e-6)return Fail(S::Part,"vertex_weight");
        // Native archive point -> canonical (reflect Z) -> carrier-local by the
        // canonical inverse bind (ExtractRigidProp's NativeRightHanded rule).
        const std::array<float,3> c{(*position)[0],(*position)[1],-(*position)[2]};std::array<float,3> local{};
        for(unsigned col=0;col<3;++col){double x=inverse.values[3][col];for(unsigned row=0;row<3;++row)x+=double(c[row])*inverse.values[row][col];local[col]=float(x);}
        for(auto x:local)if(!std::isfinite(x)||std::abs(x)>1.f)return Fail(S::Geometry,"vertex_range");
        points.push_back(local);
    }
    auto asset=std::make_shared<Xm8BoltAsset>();asset->vertices=unsigned(points.size());asset->triangles=unsigned(triangles->items.size());
    std::map<std::pair<unsigned,unsigned>,int> edges; // undirected edge -> signed use count (manifold + orientation)
    for(const auto& t:triangles->items){
        const auto* corners=t["corners"];if(!corners||corners->type!=Json::Type::Array||corners->items.size()!=3)return Fail(S::Part,"corners");
        std::array<unsigned,3> index{};
        for(unsigned k=0;k<3;++k){const auto i=Number(corners->items[k]["vertex"]);
            if(!i||*i<0||*i>=double(points.size())||*i!=std::floor(*i))return Fail(S::Part,"corner_index");index[k]=unsigned(*i);}
        if(index[0]==index[1]||index[1]==index[2]||index[0]==index[2])return Fail(S::Geometry,"repeated_corner");
        for(unsigned k=0;k<3;++k){const unsigned a=index[k],b=index[(k+1)%3];auto& use=edges[{std::min(a,b),std::max(a,b)}];use+=a<b?1:-1;}
        // Reflection reverses handedness: reverse the corner order as well.
        const std::array<std::array<float,3>,3> p{points[index[0]],points[index[2]],points[index[1]]};
        const std::array<double,3> u{p[1][0]-p[0][0],p[1][1]-p[0][1],p[1][2]-p[0][2]},w{p[2][0]-p[0][0],p[2][1]-p[0][1],p[2][2]-p[0][2]};
        std::array<double,3> normal{u[1]*w[2]-u[2]*w[1],u[2]*w[0]-u[0]*w[2],u[0]*w[1]-u[1]*w[0]};
        const double length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        if(!std::isfinite(length)||length<1e-12)return Fail(S::Geometry,"degenerate");
        for(auto& x:normal)x/=length;
        // The asset's archive normal, reflected, must agree with the converted winding.
        if(const auto declaredNormal=Vec3(t["geometric_normal"])){
            const double dot=normal[0]*(*declaredNormal)[0]+normal[1]*(*declaredNormal)[1]-normal[2]*(*declaredNormal)[2];
            if(dot<.99)return Fail(S::Geometry,"normal_winding");
        }else return Fail(S::Part,"geometric_normal");
        for(const auto& v:p)asset->mesh.vertices.push_back({v[0],v[1],v[2],float(normal[0]),float(normal[1]),float(normal[2])});
    }
    for(const auto& [edge,use]:edges)if(use!=0)return Fail(S::Geometry,"not_closed_oriented");
    // Every undirected edge must be shared by exactly two oppositely wound faces.
    {std::map<std::pair<unsigned,unsigned>,unsigned> count;
     for(const auto& t:triangles->items){std::array<unsigned,3> i{};for(unsigned k=0;k<3;++k)i[k]=unsigned(Number(t["corners"]->items[k]["vertex"]).value_or(0));
         for(unsigned k=0;k<3;++k)++count[{std::min(i[k],i[(k+1)%3]),std::max(i[k],i[(k+1)%3])}];}
     for(const auto& [edge,n]:count)if(n!=2)return Fail(S::Geometry,"non_manifold_edge");}

    // Travel contract.
    const auto axis=Vec3(Path(*render,{"travel","axis_canonical"}));const auto stroke=Number(Path(*render,{"travel","stroke_m"}));
    const auto handleRest=Vec3(Path(*render,{"travel","handle_jntWpn_17_rest_canonical"}));
    const auto carrierRest=Vec3(Path(*render,{"travel","carrier_joint_rest_canonical"}));
    if(!axis||!stroke||!handleRest||!carrierRest||!(*stroke>.01&&*stroke<.5)||!Near(*axis,{0,0,1},1e-6f)||!Near(*carrierRest,*rest,1e-4f))
        return Fail(S::Part,"travel");
    const auto* hide=(*render)["hide_native_parts_while_drawn"];
    if(!hide||hide->type!=Json::Type::Array)return Fail(S::Part,"hide_parts");
    for(const auto& h:hide->items){const auto* name=Text(&h);if(!name)return Fail(S::Part,"hide_parts");asset->hideWhileDrawn.push_back(*name);}
    if(std::find(asset->hideWhileDrawn.begin(),asset->hideWhileDrawn.end(),Xm8BoltCardBone)==asset->hideWhileDrawn.end())return Fail(S::Part,"hide_card");

    // Validation receipt (background Blender readback of the saved asset).
    asset->sha256=Xm8BoltSha256Hex(renderBytes);
    const auto flag=[&](std::initializer_list<std::string_view> path){return Bool(Path(*receipt,path)).value_or(false);};
    const auto count=[&](std::initializer_list<std::string_view> path){return Number(Path(*receipt,path));};
    if(!flag({"topology","all_triangles"})||!flag({"topology","closed_manifold"})||!flag({"topology","outward_normals"})||
       count({"topology","boundary_edges"})!=0.||count({"topology","non_manifold_edges"})!=0.||count({"topology","degenerate_triangles"})!=0.||
       count({"topology","connected_shells"})!=1.||!flag({"skin","all_rigid_single_joint"}))return Fail(S::Validation,"topology_flags");
    const auto* receiptJoint=Text(Path(*receipt,{"skin","joint"}));const auto* receiptRig=Text(Path(*receipt,{"rig","fingerprint"}));
    if(!receiptJoint||*receiptJoint!=Xm8BoltCarrierBone||!receiptRig||*receiptRig!=*declared)return Fail(S::Validation,"skin_rig");
    if(count({"topology","vertices"})!=double(asset->vertices)||count({"topology","triangles"})!=double(asset->triangles)||
       count({"export","vertices"})!=double(asset->vertices)||count({"export","triangles"})!=double(asset->triangles))return Fail(S::ValidationMismatch,"counts");
    const auto* exportSha=Text(Path(*receipt,{"export","sha256"}));if(!exportSha||*exportSha!=asset->sha256)return Fail(S::ValidationMismatch,"export_sha256");
    const auto receiptAxis=Vec3(Path(*receipt,{"travel","axis_canonical"}));const auto receiptStroke=count({"travel","stroke_m"});
    if(!receiptAxis||!Near(*receiptAxis,*axis,1e-6f)||!receiptStroke||std::abs(*receiptStroke-*stroke)>1e-7)return Fail(S::ValidationMismatch,"travel");

    asset->carrierRest=*rest;asset->handleRest=*handleRest;asset->axis=*axis;asset->stroke=float(*stroke);
    if(const auto* n=Text(part["name"]))asset->partName=*n;
    asset->mesh.sourcePositionHash=Fnv(asset->sha256);asset->mesh.sourceVertexSkinHash=Fnv(Xm8BoltCarrierBone)^asset->mesh.sourcePositionHash;
    asset->key={Fnv(Xm8BoltWeaponAsset),Fnv(Xm8BoltCarrierBone),Fnv(asset->sha256)};
    Xm8BoltAssetResult out;out.status=S::Loaded;out.asset=std::move(asset);return out;
}

Xm8BoltAssetResult LoadXm8BoltAsset(const std::filesystem::path& path)noexcept {
    try {
        const auto read=[](const std::filesystem::path& p,std::vector<std::byte>& out)->bool{
            std::error_code error;const auto size=std::filesystem::file_size(p,error);if(error||!size||size>16*1024*1024)return false;
            std::ifstream file(p,std::ios::binary);if(!file)return false;out.resize(std::size_t(size));
            file.read(reinterpret_cast<char*>(out.data()),std::streamsize(size));return file.gcount()==std::streamsize(size);};
        std::vector<std::byte> render;
        if(!read(path,render)){auto r=Fail(Xm8BoltAssetStatus::NotFound,"render_input_unreadable");r.path=path;return r;}
        // The receipt names its own absolute path; only its file name is used,
        // next to the render input, so a moved intake folder stays consistent.
        std::filesystem::path validation=path.parent_path()/"xm8-bolt-v1-validation.json";
        {const std::string_view text(reinterpret_cast<const char*>(render.data()),render.size());
         if(const auto doc=JsonReader(text).Document())if(const auto* named=Text((*doc)["validation"])){
             const auto name=std::filesystem::path(*named).filename();if(!name.empty())validation=path.parent_path()/name;}}
        std::vector<std::byte> receipt;
        if(!read(validation,receipt)){auto r=Fail(Xm8BoltAssetStatus::Validation,"validation_unreadable");r.path=path;r.validationPath=validation;return r;}
        auto result=ParseXm8BoltAsset(render,std::string_view(reinterpret_cast<const char*>(receipt.data()),receipt.size()));
        result.path=path;result.validationPath=validation;return result;
    }catch(...){auto r=Fail(Xm8BoltAssetStatus::Exception,"exception");r.path=path;return r;}
}

std::optional<std::filesystem::path> ResolveXm8BoltAssetPath(std::string_view environment,
    std::span<const std::filesystem::path> anchors,const Xm8BoltExists& exists){
    if(!environment.empty())return std::filesystem::path(std::string(environment));
    const auto relative=std::filesystem::path(std::string(Xm8BoltDefaultAsset));
    for(const auto& anchor:anchors){if(anchor.empty())continue;auto at=anchor;
        for(unsigned depth=0;depth<6&&!at.empty();++depth){const auto candidate=at/relative;if(exists&&exists(candidate))return candidate;
            const auto parent=at.parent_path();if(parent==at)break;at=parent;}}
    return {};
}
} // namespace fvr::bc2
