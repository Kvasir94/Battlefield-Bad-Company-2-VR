#pragma once
// Runtime authored contact library for BC2 (include/fvr/interaction/AuthoredContacts.h).
// The library file is PRIVATE derived game-animation data and is never in git.
// Location, in order:
//   FVR_AUTHORED_CONTACTS=0        disabled (kill switch): existing IK/native poses only
//   FVR_AUTHORED_CONTACTS=<path>   that file
//   unset                          authored-contacts-v1.txt beside BC2NativeProbe.dll
// Produce it with tools/export_authored_contacts.py from the private intake
// captures (reports/floating-hands-intake/*-same-frame-contact.json).
#include "Bc2Rig.h"
#include "fvr/interaction/AuthoredContacts.h"
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif
#include <algorithm>

namespace fvr::bc2 {
inline constexpr const char* AuthoredContactsFileName="authored-contacts-v1.txt";
enum class AuthoredContactsState:unsigned {NotLoaded,Disabled,Missing,Invalid,Loaded};
// Pure path policy (tested): env value (empty = unset) and the module folder.
inline std::optional<std::string> ResolveAuthoredContactsPath(std::string_view env,std::string_view moduleDirectory){
    if(env=="0")return std::nullopt;
    if(!env.empty())return std::string(env);
    if(moduleDirectory.empty())return std::nullopt;
    std::string path(moduleDirectory);if(path.back()!='\\'&&path.back()!='/')path+='\\';
    return path+AuthoredContactsFileName;
}
struct AuthoredContactsStatus {AuthoredContactsState state=AuthoredContactsState::NotLoaded;std::size_t contacts=0;std::string path,error;};
namespace authored_contacts_detail {
struct Store {
    std::once_flag once;std::shared_ptr<const interaction::AuthoredContactLibrary> library;AuthoredContactsStatus status;
};
inline Store& Instance(){static Store store;return store;}
#ifdef _WIN32
inline std::string ModuleDirectory(){
    HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&Instance),&module))return {};
    char path[MAX_PATH]{};const auto n=GetModuleFileNameA(module,path,MAX_PATH);if(!n||n>=MAX_PATH)return {};
    std::string text(path,n);const auto slash=text.find_last_of("\\/");return slash==std::string::npos?std::string{}:text.substr(0,slash);
}
inline std::string Env(const char* name){char value[1024]{};const auto n=GetEnvironmentVariableA(name,value,sizeof(value));return n&&n<sizeof(value)?std::string(value,n):std::string{};}
#else
inline std::string ModuleDirectory(){return {};}
inline std::string Env(const char*){return {};}
#endif
}
// Parses a library from text (also used by tests).
inline AuthoredContactsStatus LoadAuthoredContactsText(std::string_view text,std::shared_ptr<const interaction::AuthoredContactLibrary>& out){
    AuthoredContactsStatus status;std::string error;
    if(auto parsed=interaction::AuthoredContactLibrary::Parse(text,&error)){
        status.state=AuthoredContactsState::Loaded;status.contacts=parsed->Size();
        out=std::make_shared<const interaction::AuthoredContactLibrary>(std::move(*parsed));
    }else{status.state=AuthoredContactsState::Invalid;status.error=error;out.reset();}
    return status;
}
// Process library, loaded once on first use (a few KB; read on the first
// rig publication, never per frame). Invalid or missing files fall back.
inline const interaction::AuthoredContactLibrary* AuthoredContacts(){
    auto& store=authored_contacts_detail::Instance();
    std::call_once(store.once,[&]{
        const auto path=ResolveAuthoredContactsPath(authored_contacts_detail::Env("FVR_AUTHORED_CONTACTS"),authored_contacts_detail::ModuleDirectory());
        if(!path){store.status.state=AuthoredContactsState::Disabled;return;}
        store.status.path=*path;
        std::ifstream file(*path,std::ios::binary);
        if(!file){store.status.state=AuthoredContactsState::Missing;return;}
        std::ostringstream text;text<<file.rdbuf();
        if(text.str().size()>4*1024*1024){store.status.state=AuthoredContactsState::Invalid;store.status.error="file too large";return;}
        const auto keep=store.status.path;store.status=LoadAuthoredContactsText(text.str(),store.library);store.status.path=keep;
    });
    return store.library.get();
}
inline const AuthoredContactsStatus& AuthoredContactsLoadStatus(){AuthoredContacts();return authored_contacts_detail::Instance().status;}
inline std::string AuthoredContactsReport(){
    const auto& s=AuthoredContactsLoadStatus();static constexpr const char* names[]{"not_loaded","disabled","missing","invalid","loaded"};
    std::string out="{\"state\":\"";out+=names[unsigned(s.state)];out+="\",\"contacts\":"+std::to_string(s.contacts)+",\"error\":\"";
    for(const char c:s.error)if(c!='"'&&c!='\\'&&c>=' ')out+=c;out+="\"}";return out;
}
// Exact asset/kind/hand/skeleton entry whose part and fingers exist uniquely in this rig.
inline const interaction::AuthoredContact* AuthoredContactFor(std::string_view asset,interaction::AuthoredContactKind kind,unsigned hand,
    std::uint64_t rigFingerprint,const RigSnapshot& rig,const interaction::AuthoredContactLibrary* library=AuthoredContacts()){
    if(!library||asset.empty())return nullptr;
    const auto found=library->Find(asset,kind,hand,rigFingerprint);if(!found.contact)return nullptr;
    const auto count=std::count(rig.names.begin(),rig.names.end(),found.contact->part);
    return count==1?found.contact:nullptr;
}
// The authored wrist relative to the rig's native weapon bone, in world units:
// C * part world * inverse(weapon world), using the current native rig.
template<class Worlds>
std::optional<math::Matrix4> AuthoredContactWeaponLocal(const interaction::AuthoredContact& c,const RigSnapshot& rig,const Worlds& native,float units){
    const auto at=std::find(rig.names.begin(),rig.names.end(),c.part);if(at==rig.names.end()||!std::isfinite(units)||!(units>0))return {};
    const auto part=std::size_t(at-rig.names.begin());
    if(part==rig.weaponBone)return interaction::AuthoredWristLocal(c,units);
    const auto inverseWeapon=interaction::InverseAnimatedTransform(native[rig.weaponBone]);if(!inverseWeapon)return {};
    return interaction::Multiply(interaction::BindAuthoredWrist(c,native[part],units),*inverseWeapon);
}
} // namespace fvr::bc2
