#pragma once
// Authored contact samples loaded at runtime (docs/INTERACTION-POLISH-20261008.md,
// docs/floating-hands-contacts.md). An authored contact is the native animation's
// own same-frame relationship between a hand and the part it holds:
//   wristInPart  C    = authored wrist world * inverse(authored part world)
//   fingerInWrist F[j] = authored finger world * inverse(authored wrist world)
// Bound at runtime (row-vector matrices, metres scaled to world units):
//   wrist  = C * part world,   finger[j] = F[j] * wrist.
// Applying one rigid transform to player, gun and hand preserves the contact,
// including an inverted weapon. The samples are a transformation of game
// animation data, so they are NEVER compiled in or committed: the BC2 adapter
// reads them from a private file (tools/export_authored_contacts.py writes it).
// No file, no entry or a mismatched identity: the caller keeps its existing IK
// or native pose. Presentation only: no claim, ammunition or native state.
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fvr::interaction {
enum class AuthoredContactKind:std::uint8_t {Support,GunGrip,MagazineGrasp,Handle};
inline constexpr std::size_t AuthoredFingerCount=15;
struct AuthoredContact {
    AuthoredContactKind kind=AuthoredContactKind::Support;
    unsigned hand=0; // 0 left, 1 right
    std::string asset,part,clipSha256;
    std::uint64_t rigFingerprint=0;
    float timeSeconds=0;
    math::Matrix4 wristInPart{};
    std::array<std::string,AuthoredFingerCount> fingerNames{};
    std::array<math::Matrix4,AuthoredFingerCount> fingerInWrist{};
    unsigned fingerCount=0; // 0 (wrist only) or 15
};
enum class AuthoredContactSource:std::uint8_t {Fallback,Authored};
struct AuthoredContactLookup {
    AuthoredContactSource source=AuthoredContactSource::Fallback;
    const AuthoredContact* contact=nullptr;
};
namespace authored_contact_detail {
inline std::optional<AuthoredContactKind> Kind(std::string_view v)noexcept {
    if(v=="support")return AuthoredContactKind::Support;if(v=="gun_grip")return AuthoredContactKind::GunGrip;
    if(v=="magazine")return AuthoredContactKind::MagazineGrasp;if(v=="handle")return AuthoredContactKind::Handle;return {};
}
inline bool Rigid(const math::Matrix4& m,float maxTranslation)noexcept {
    for(unsigned r=0;r<3;++r){float l=0;for(unsigned c=0;c<3;++c){if(!std::isfinite(m.values[r][c]))return false;l+=m.values[r][c]*m.values[r][c];}
        if(std::fabs(l-1)>.002f||std::fabs(m.values[r][3])>1e-4f)return false;}
    for(unsigned c=0;c<3;++c)if(!std::isfinite(m.values[3][c]))return false;
    return std::hypot(m.values[3][0],m.values[3][1],m.values[3][2])<=maxTranslation&&std::fabs(m.values[3][3]-1)<1e-4f&&InverseRigid(m).has_value();
}
struct Tokens {
    std::string_view rest;
    std::optional<std::string_view> Next()noexcept {
        std::size_t a=0;while(a<rest.size()&&(rest[a]==' '||rest[a]=='\t'||rest[a]=='\r'))++a;
        std::size_t b=a;while(b<rest.size()&&rest[b]!=' '&&rest[b]!='\t'&&rest[b]!='\r')++b;
        if(a==b){rest={};return {};}const auto t=rest.substr(a,b-a);rest=rest.substr(b);return t;
    }
};
// Asset names may contain spaces. Percent escapes keep one token on disk;
// identity matching uses the decoded native name, never an alias.
inline std::optional<std::string> Asset(std::string_view token) {
    std::string result;
    for(std::size_t n=0;n<token.size();++n){
        unsigned value=static_cast<unsigned char>(token[n]);
        if(value=='%'){
            if(n+2>=token.size())return {};
            const auto r=std::from_chars(token.data()+n+1,token.data()+n+3,value,16);
            if(r.ec!=std::errc{}||r.ptr!=token.data()+n+3)return {};
            n+=2;
        }
        if(value<32||value>126)return {};
        result+=static_cast<char>(value);
    }
    return result.empty()?std::nullopt:std::optional<std::string>{std::move(result)};
}
inline bool Float(std::string_view t,float& out)noexcept {
    const auto r=std::from_chars(t.data(),t.data()+t.size(),out);return r.ec==std::errc{}&&r.ptr==t.data()+t.size()&&std::isfinite(out);
}
inline bool Matrix(Tokens& in,math::Matrix4& m)noexcept {
    for(unsigned n=0;n<16;++n){const auto t=in.Next();if(!t||!Float(*t,m.values[n/4][n%4]))return false;}return !in.Next();
}
}
class AuthoredContactLibrary {
public:
    // Text format "fvr-authored-contacts 1" (see the exporter). Any malformed
    // line rejects the whole library: partial authored data is never used.
    static std::optional<AuthoredContactLibrary> Parse(std::string_view text,std::string* error=nullptr) {
        using namespace authored_contact_detail;
        AuthoredContactLibrary out;unsigned line=0;bool header=false;AuthoredContact* open=nullptr;
        const auto fail=[&](const char* why)->std::optional<AuthoredContactLibrary>{
            if(error)*error="line "+std::to_string(line)+": "+why;return std::nullopt;};
        const auto close=[&]()->bool{if(open&&open->fingerCount!=0&&open->fingerCount!=AuthoredFingerCount)return false;open=nullptr;return true;};
        while(!text.empty()){
            ++line;const auto end=text.find('\n');auto row=text.substr(0,end);text=end==std::string_view::npos?std::string_view{}:text.substr(end+1);
            Tokens in{row};const auto first=in.Next();if(!first||(*first)[0]=='#')continue;
            if(!header){if(*first!="fvr-authored-contacts"||in.Next()!=std::optional<std::string_view>("1")||in.Next())return fail("missing header");header=true;continue;}
            if(*first=="contact"){
                if(!close())return fail("contact needs 0 or 15 fingers");
                if(out.contacts_.size()>=256)return fail("too many contacts");
                AuthoredContact c;const auto kind=in.Next();const auto hand=in.Next();const auto asset=in.Next();const auto part=in.Next();
                const auto rig=in.Next();const auto clip=in.Next();const auto time=in.Next();
                if(!kind||!hand||!asset||!part||!rig||!clip||!time)return fail("short contact");
                const auto k=Kind(*kind);if(!k)return fail("unknown kind");c.kind=*k;
                if(*hand=="left")c.hand=0;else if(*hand=="right")c.hand=1;else return fail("hand");
                const auto r=std::from_chars(rig->data(),rig->data()+rig->size(),c.rigFingerprint,16);
                if(r.ec!=std::errc{}||r.ptr!=rig->data()+rig->size()||!c.rigFingerprint)return fail("rig");
                if(clip->size()!=64||!Float(*time,c.timeSeconds)||c.timeSeconds<0)return fail("clip/time");
                const auto name=Asset(*asset);if(!name)return fail("asset token");
                c.asset=*name;c.part=std::string(*part);c.clipSha256=std::string(*clip);
                if(!Matrix(in,c.wristInPart)||!Rigid(c.wristInPart,1.5f))return fail("wrist matrix");
                for(const auto& e:out.contacts_)if(e.kind==c.kind&&e.hand==c.hand&&e.asset==c.asset&&e.rigFingerprint==c.rigFingerprint)return fail("duplicate contact");
                out.contacts_.push_back(std::move(c));open=&out.contacts_.back();continue;
            }
            if(*first=="finger"){
                if(!open||open->fingerCount>=AuthoredFingerCount)return fail("finger outside contact");
                const auto name=in.Next();if(!name)return fail("finger name");
                auto& m=open->fingerInWrist[open->fingerCount];
                if(!Matrix(in,m)||!Rigid(m,.2f))return fail("finger matrix");
                open->fingerNames[open->fingerCount++]=std::string(*name);continue;
            }
            return fail("unknown record");
        }
        if(!header)return fail("empty");
        if(!close())return fail("contact needs 0 or 15 fingers");
        return out;
    }
    // Exact asset + kind + hand + skeleton. Anything else is the caller's fallback.
    AuthoredContactLookup Find(std::string_view asset,AuthoredContactKind kind,unsigned hand,std::uint64_t rig)const noexcept {
        for(const auto& c:contacts_)if(c.kind==kind&&c.hand==hand&&c.rigFingerprint==rig&&c.asset==asset)
            return {AuthoredContactSource::Authored,&c};
        return {};
    }
    std::size_t Size()const noexcept{return contacts_.size();}
    std::span<const AuthoredContact> Contacts()const noexcept{return contacts_;}
private:
    std::vector<AuthoredContact> contacts_;
};
inline math::Matrix4 ScaleTranslation(math::Matrix4 m,float units)noexcept{for(unsigned n=0;n<3;++n)m.values[3][n]*=units;return m;}
// wrist = C * part (C in metres, scaled to world units).
inline math::Matrix4 BindAuthoredWrist(const AuthoredContact& c,const math::Matrix4& partWorld,float units)noexcept {
    return Multiply(ScaleTranslation(c.wristInPart,units),partWorld);
}
// The wrist relative to the part in world units (what grip attachments store).
inline math::Matrix4 AuthoredWristLocal(const AuthoredContact& c,float units)noexcept{return ScaleTranslation(c.wristInPart,units);}
struct AuthoredBoneWrite {std::uint32_t index=0;math::Matrix4 transform{};};
// Replaces the 15 finger joints with the authored pose for the given (final)
// wrist world transform, then carries every already-written descendant of a
// replaced joint (finger tips) with its previous local relation. `writes` are
// world transforms in skeleton order (parents before children). Fingers whose
// names are not in the skeleton leave the pose untouched (returns false).
template<class Write,class Names>
bool ApplyAuthoredFingers(std::span<const std::int32_t> parents,const Names& names,
    std::vector<Write>& writes,const AuthoredContact& c,const math::Matrix4& wristWorld,float units) {
    if(c.fingerCount!=AuthoredFingerCount||!std::isfinite(units)||!(units>0))return false;
    std::array<std::uint32_t,AuthoredFingerCount> bones{};
    for(std::size_t f=0;f<AuthoredFingerCount;++f){
        const std::string_view want=c.fingerNames[f];
        const auto at=std::find_if(names.begin(),names.end(),[&](const auto& n){return std::string_view(n)==want;});
        if(at==names.end()||std::find_if(at+1,names.end(),[&](const auto& n){return std::string_view(n)==want;})!=names.end())return false;
        bones[f]=std::uint32_t(at-names.begin());
    }
    // Previous world of every written bone (for descendants' local relation).
    std::vector<std::pair<std::uint32_t,math::Matrix4>> before;before.reserve(writes.size());
    for(const auto& w:writes)before.push_back({w.index,w.transform});
    const auto find=[&](std::uint32_t index)->Write*{for(auto& w:writes)if(w.index==index)return &w;return nullptr;};
    const auto old=[&](std::uint32_t index)->const math::Matrix4*{for(const auto& b:before)if(b.first==index)return &b.second;return nullptr;};
    std::vector<std::uint32_t> replaced;
    for(std::size_t f=0;f<AuthoredFingerCount;++f){
        const auto world=Multiply(ScaleTranslation(c.fingerInWrist[f],units),wristWorld);
        if(auto* w=find(bones[f]))w->transform=world;else writes.push_back(Write{bones[f],world});
        replaced.push_back(bones[f]);
    }
    for(auto& w:writes){
        if(std::find(replaced.begin(),replaced.end(),w.index)!=replaced.end())continue;
        if(w.index>=parents.size())continue;const auto parent=parents[w.index];
        if(parent<0||std::find(replaced.begin(),replaced.end(),std::uint32_t(parent))==replaced.end())continue;
        const auto* parentOld=old(std::uint32_t(parent));const auto* self=old(w.index);const auto* parentNew=find(std::uint32_t(parent));
        if(!parentOld||!self||!parentNew)continue;
        const auto inverse=InverseAnimatedTransform(*parentOld);if(!inverse)continue;
        w.transform=Multiply(Multiply(*self,*inverse),parentNew->transform);
        replaced.push_back(w.index);
    }
    return true;
}
} // namespace fvr::interaction
