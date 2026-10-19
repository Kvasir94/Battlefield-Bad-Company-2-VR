#pragma once
#include "Bc2Xm8BoltAsset.h"
#include "fvr/interaction/TrackingMath.h"
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace fvr::bc2 {
struct NativePropRecord {
    std::string asset,mesh,part,filename;
    std::uint64_t rig=0;unsigned bone=0,paletteCount=0;
    math::Matrix4 inverseBind{};
    std::array<std::byte,32> digest{};
};
inline bool NativePropDigest(std::span<const std::byte> bytes,const std::array<std::byte,32>& digest){
    const auto hex=Xm8BoltSha256Hex(bytes);if(hex.size()!=64)return false;
    constexpr char digits[]="0123456789abcdef";
    for(unsigned n=0;n<32;++n){const auto b=std::to_integer<unsigned>(digest[n]);
        if(hex[n*2]!=digits[b>>4]||hex[n*2+1]!=digits[b&15])return false;}
    return true;
}
inline std::optional<std::vector<NativePropRecord>> ParseNativePropManifest(std::span<const std::byte> bytes){
    if(bytes.size()<16||bytes.size()>65536||std::memcmp(bytes.data(),"BC2NPR01",8))return {};
    std::size_t at=8;
    const auto read=[&](auto& out){if(sizeof out>bytes.size()-at)return false;
        std::memcpy(&out,bytes.data()+at,sizeof out);at+=sizeof out;return true;};
    const auto text=[&](std::string& out){std::uint16_t size=0;
        if(!read(size)||!size||size>256||size>bytes.size()-at)return false;
        for(unsigned n=0;n<size;++n){const auto c=std::to_integer<unsigned>(bytes[at+n]);if(c<32||c>126)return false;}
        out.assign(reinterpret_cast<const char*>(bytes.data()+at),size);at+=size;return true;};
    unsigned version=0,count=0;if(!read(version)||version!=1||!read(count)||!count||count>32)return {};
    std::vector<NativePropRecord> result;
    for(unsigned n=0;n<count;++n){NativePropRecord r;
        if(!text(r.asset)||!text(r.mesh)||!text(r.part)||!text(r.filename)||
           r.filename.find_first_of("/\\:")!=std::string::npos||r.filename.find("..")!=std::string::npos||
           !r.filename.ends_with(".fvrskin")||!read(r.rig)||!read(r.bone)||!read(r.paletteCount)||
           !read(r.inverseBind)||!read(r.digest)||!r.rig||r.bone>=147||r.paletteCount!=147||
           !interaction::InverseAnimatedTransform(r.inverseBind))return {};
        for(const auto& prior:result)if(prior.asset==r.asset&&prior.mesh==r.mesh&&prior.part==r.part)return {};
        result.push_back(std::move(r));
    }
    if(at!=bytes.size())return {};return result;
}
// Original vertices are native RH; metadata/physical world are canonical LH.
// Reflection on BOTH sides converts inverseBind*world, then the verified native
// packer transposes its first three columns into three float4 vectors (48B).
inline std::optional<std::array<float,12>> NativePropPackedTransform(
    const math::Matrix4& inverseBind,const math::Matrix4& world)noexcept {
    if(!interaction::InverseAnimatedTransform(inverseBind)||!interaction::InverseAnimatedTransform(world))return {};
    auto native=interaction::Multiply(inverseBind,world);
    for(unsigned n=0;n<4;++n){native.values[2][n]=-native.values[2][n];native.values[n][2]=-native.values[n][2];}
    std::array<float,12> out{};
    for(unsigned col=0;col<3;++col)for(unsigned row=0;row<4;++row){
        const auto value=native.values[row][col];if(!std::isfinite(value))return {};out[col*4+row]=value;}
    return out;
}
}
