#pragma once
// Static executable scanning helpers shared by the render-wave discovery
// modules (Bc2EyeViewParity, Bc2PerEyePostProcess). File bytes only.
#include "fvr/engine/BindingValidation.h"
#include "fvr/engine/PeImage.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
namespace fvr::bc2::scan {
inline std::optional<std::size_t> At(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,std::size_t n){
    for(const auto& s:pe.sections)if(rva>=s.rva){const auto d=std::uint64_t(rva)-s.rva,a=std::uint64_t(s.rawOffset)+d;
        if(d<=s.rawSize&&n<=s.rawSize-d&&a<=b.size()&&n<=b.size()-a)return std::size_t(a);}return {};
}
inline std::uint32_t U(std::span<const std::byte> b,std::size_t at){std::uint32_t x=0;if(at<=b.size()&&4<=b.size()-at)std::memcpy(&x,b.data()+at,4);return x;}
// Preferred image base from the PE optional header (0 when malformed).
inline std::uint32_t Base(std::span<const std::byte> b,const engine::PeImage& pe){
    if(pe.machine!=0x14c||b.size()<64)return 0;const auto nt=U(b,0x3c);if(nt>b.size()||56>b.size()-nt)return 0;const auto base=U(b,std::size_t(nt)+52);return base<0x10000?0:base;
}
// Exact pattern ("??" wildcards) at an RVA. Returns the pattern length on match.
inline std::optional<std::size_t> Match(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,const char* text){
    const auto p=engine::ParsePattern(text);if(!p||p->empty())return {};const auto at=At(b,pe,rva,p->size());if(!at)return {};
    for(std::size_t i=0;i<p->size();++i)if(!(*p)[i].wildcard&&std::to_integer<unsigned char>(b[*at+i])!=(*p)[i].value)return {};return p->size();
}
// Unique match over executable sections; nullopt when absent or ambiguous.
inline std::optional<std::uint32_t> Find(std::span<const std::byte> b,const engine::PeImage& pe,const char* text){
    const auto pattern=engine::ParsePattern(text);if(!pattern||pattern->empty())return {};std::optional<std::uint32_t> found;
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=b.size()&&s.rawSize<=b.size()-s.rawOffset&&pattern->size()<=s.rawSize){
        for(std::size_t n=0;n<=s.rawSize-pattern->size();++n){bool okay=true;
            for(std::size_t i=0;i<pattern->size();++i)if(!(*pattern)[i].wildcard&&std::to_integer<unsigned char>(b[s.rawOffset+n+i])!=(*pattern)[i].value){okay=false;break;}
            if(okay){if(found)return {};found=s.rva+std::uint32_t(n);}}}return found;
}
// Unique 4-aligned dword in non-executable initialized sections (vtable search).
inline std::optional<std::uint32_t> FindDword(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t value){
    std::optional<std::uint32_t> found;
    for(const auto& s:pe.sections)if(!(s.flags&0x20000000)&&s.rawOffset<=b.size()&&s.rawSize<=b.size()-s.rawOffset&&s.rawSize>=4){
        for(std::uint32_t n=0;n+4<=s.rawSize;n+=4)if(U(b,std::size_t(s.rawOffset)+n)==value){if(found)return {};found=s.rva+n;}}return found;
}
// Target RVA of an E8/E9 rel32 at an RVA.
inline std::optional<std::uint32_t> Rel32(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,unsigned char opcode){
    const auto at=At(b,pe,rva,5);if(!at||b[*at]!=std::byte(opcode))return {};const auto to=std::int64_t(rva)+5+std::int32_t(U(b,*at+1));
    if(to<0||to>UINT32_MAX||!At(b,pe,std::uint32_t(to),1))return {};return std::uint32_t(to);
}
}
