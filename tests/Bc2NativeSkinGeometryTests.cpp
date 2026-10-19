// Synthetic triangles only. Exercises the production reader, no game asset,
// native memory, DX11 device, or shader is required.
#include "Bc2NativeSkinGeometry.h"
#include "Bc2NativePropManifest.h"
#include <filesystem>
#include "Bc2Xm8BoltAsset.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
using namespace fvr::bc2;
namespace {
using Blob=std::vector<std::byte>;
constexpr std::uint64_t Rig=0x12345678;
constexpr unsigned Record(unsigned part){return 128+320*part;}
template<class T>void Put(Blob& b,unsigned at,T value){CHECK(at+sizeof value<=b.size());std::memcpy(b.data()+at,&value,sizeof value);}
unsigned U32(const Blob& b,unsigned at){unsigned value=0;std::memcpy(&value,b.data()+at,4);return value;}
void Digest(Blob& b,unsigned at,std::span<const std::byte> bytes){
    const auto hex=Xm8BoltSha256Hex(bytes);CHECK(hex.size()==64);
    const auto nib=[](char c){return unsigned(c>='a'?c-'a'+10:c-'0');};
    for(unsigned n=0;n<32;++n)b[at+n]=std::byte((nib(hex[n*2])<<4)|nib(hex[n*2+1]));
}
void Body(Blob& b){Digest(b,96,std::span<const std::byte>(b).subspan(128));}
void Payloads(Blob& b){for(unsigned part=0;part<U32(b,24);++part){const auto r=Record(part);if(!U32(b,r+4))continue;
    Digest(b,r+128,std::span<const std::byte>(b).subspan(U32(b,r+40),U32(b,r+44)));
    Digest(b,r+160,std::span<const std::byte>(b).subspan(U32(b,r+48),U32(b,r+52)));}Body(b);}
Blob Valid(){
    Blob b(1408);std::memcpy(b.data(),"BC2NSK01",8);Put(b,8,1u);Put(b,16,Rig);Put(b,24,4u);Put(b,28,1u);
    for(unsigned part=0;part<4;++part){const auto r=Record(part),stride=part==3?16u:48u;
        Put(b,r,part);Put(b,r+4,part>=2?1u:0u);Put(b,r+8,stride);Put(b,r+12,3u);Put(b,r+16,3u);Put(b,r+28,2u);
        b[r+64]=std::byte(part+1);b[r+96]=std::byte(part+10); // Distinct synthetic source identities.
        for(unsigned n=0;n<64;++n)Put(b,r+192+n*2,std::uint16_t(n<2?10+n:0xffff));
        if(part<2)continue;
        const unsigned vo=unsigned(b.size()),vs=3*stride,io=vo+vs;
        b.resize(io+6);Put(b,r+32,3u);Put(b,r+36,3u);Put(b,r+40,vo);Put(b,r+44,vs);Put(b,r+48,io);Put(b,r+52,6u);
        if(part==3){Put(b,r+192,std::uint16_t(11));Put(b,r+194,std::uint16_t(10));}
        for(unsigned n=0;n<3;++n){const auto v=vo+n*stride;
            Put(b,v+6,std::uint16_t(0x3c00)); // half position.w=1
            if(n)Put(b,v+(n-1)*2,std::uint16_t(0x3c00));
            b[v+8]=std::byte{0};b[v+9]=std::byte{1};
            b[v+12]=std::byte(part==3?191:64);b[v+13]=std::byte(part==3?64:191);
            Put(b,io+n*2,std::uint16_t(n));
        }
    }
    Put(b,12,unsigned(b.size()));Payloads(b);return b;
}
Blob SplitColors(){
    // Five native sections: two independently bound color materials, one depth
    // union, two suppressed sections. Color triangles concatenate in part order.
    Blob b(128+5*320);std::memcpy(b.data(),"BC2NSK02",8);Put(b,8,2u);Put(b,16,Rig);Put(b,24,5u);Put(b,28,1u);
    for(unsigned part=0;part<5;++part){const auto r=Record(part),stride=part==4?16u:48u;
        const bool replace=part==1||part==3||part==4;const unsigned count=part==4?6u:3u;
        Put(b,r,part);Put(b,r+4,replace?1u:0u);Put(b,r+8,stride);Put(b,r+12,3u);Put(b,r+16,count);Put(b,r+28,2u);
        b[r+64]=std::byte(part+30);b[r+96]=std::byte(part+40);
        for(unsigned n=0;n<64;++n)Put(b,r+192+n*2,std::uint16_t(n<2?10+n:0xffff));
        if(!replace)continue;
        const unsigned vo=unsigned(b.size()),vs=3*stride,io=vo+vs;
        b.resize(io+count*2);Put(b,r+32,3u);Put(b,r+36,count);Put(b,r+40,vo);Put(b,r+44,vs);Put(b,r+48,io);Put(b,r+52,count*2);
        if(part==4){Put(b,r+192,std::uint16_t(11));Put(b,r+194,std::uint16_t(10));}
        for(unsigned n=0;n<3;++n){const auto v=vo+n*stride;
            Put(b,v+6,std::uint16_t(0x3c00));if(n)Put(b,v+(n-1)*2,std::uint16_t(0x3c00));
            b[v+9]=std::byte{1};b[v+12]=std::byte(part==4?191:64);b[v+13]=std::byte(part==4?64:191);}
        for(unsigned n=0;n<count;++n)Put(b,io+n*2,std::uint16_t(n%3));
    }
    Put(b,12,unsigned(b.size()));Payloads(b);return b;
}
Blob ExplicitLayout(unsigned layout,bool mixed=false){
    const auto source=SplitColors();Blob b(source.begin(),source.begin()+128+5*320);
    std::memcpy(b.data(),"BC2NSK03",8);Put(b,8,3u);
    for(unsigned part=0;part<5;++part){const auto r=Record(part);
        const unsigned kind=mixed&&part==1?1u:layout;
        const unsigned stride=part==4?(kind==1?32u:16u):64u;
        Put(b,r+8,stride);Put(b,r+56,kind);
        b[r+64]=std::byte(part+50+layout*10+(mixed?40:0));
        if(!U32(b,r+4))continue;
        const unsigned count=U32(b,r+36),vo=unsigned(b.size()),vs=3*stride,io=vo+vs;
        b.resize(io+count*2);Put(b,r+40,vo);Put(b,r+44,vs);Put(b,r+48,io);
        const auto ids=kind==1?12u:8u;
        for(unsigned n=0;n<3;++n){const auto v=vo+n*stride;
            if(kind==1){if(n)Put(b,v+(n-1)*4,1.0f);}
            else {Put(b,v+6,std::uint16_t(0x3c00));if(n)Put(b,v+(n-1)*2,std::uint16_t(0x3c00));}
            b[v+ids+1]=std::byte{1};b[v+ids+4]=std::byte(part==4?191:64);b[v+ids+5]=std::byte(part==4?64:191);
        }
        for(unsigned n=0;n<count;++n)Put(b,io+n*2,std::uint16_t(n%3));
    }
    Put(b,12,unsigned(b.size()));Payloads(b);return b;
}
void Reject(const Blob& b){CHECK(!nativeSkinGeometry::Configure(b,Rig));}
}
int main(int argc,char** argv){
    if(argc==3&&!std::strcmp(argv[1],"--props")){
        const std::filesystem::path root(argv[2]);
        const auto load=[](const std::filesystem::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);CHECK(f);
            const auto n=f.tellg();CHECK(n>0&&n<=8*1024*1024);Blob out(std::size_t(n),std::byte{});f.seekg(0);
            CHECK(f.read(reinterpret_cast<char*>(out.data()),n));return out;};
        const auto bytes=load(root/"manifest.fvrnprop");const auto records=ParseNativePropManifest(bytes);CHECK(records);
        std::vector<Blob> storage;storage.reserve(records->size());std::vector<nativeSkinGeometry::PropPack> packs;
        for(const auto& r:*records){storage.push_back(load(root/r.filename));CHECK(NativePropDigest(storage.back(),r.digest));
            packs.push_back({storage.back(),r.rig,r.bone});}
        CHECK(nativeSkinGeometry::ConfigureProps(packs));
        std::printf("Validated %zu isolated native prop packs, manifest digests, paired corners and rigid target bones\n",packs.size());return 0;
    }
    {Blob manifest;const auto append=[&](const auto& value){const auto p=manifest.size();manifest.resize(p+sizeof value);std::memcpy(manifest.data()+p,&value,sizeof value);};
     const auto text=[&](const char* value){const auto n=std::uint16_t(std::strlen(value));append(n);
        for(unsigned i=0;i<n;++i)manifest.push_back(std::byte(value[i]));};
     for(char c:std::string("BC2NPR01"))manifest.push_back(std::byte(c));append(1u);append(1u);
     text("Weapons/Test");text("TestMesh");text("Magazine");text("test.fvrskin");append(Rig);append(10u);append(147u);
     fvr::math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;append(identity);append(std::array<std::byte,32>{});
     CHECK(ParseNativePropManifest(manifest));
     {auto b=manifest;b.push_back(std::byte{});CHECK(!ParseNativePropManifest(b));}
     {auto b=manifest;b.pop_back();CHECK(!ParseNativePropManifest(b));}
     {auto b=manifest;Put(b,12,33u);CHECK(!ParseNativePropManifest(b));}
     {auto b=manifest;Put(b,8,2u);CHECK(!ParseNativePropManifest(b));}
     {auto b=manifest;const auto i=std::search(b.begin(),b.end(),reinterpret_cast<const std::byte*>("test.fvrskin"),reinterpret_cast<const std::byte*>("test.fvrskin")+12);
        CHECK(i!=b.end());*i=std::byte{'/'};CHECK(!ParseNativePropManifest(b));}
     auto world=identity;world.values[3]={2,3,4,1};auto inverse=identity;inverse.values[3]={-1,-2,-3,1};
     const auto packed=NativePropPackedTransform(inverse,world);CHECK(packed);
     CHECK((*packed)[0]==1&&(*packed)[5]==1&&(*packed)[10]==1);
     CHECK((*packed)[3]==1&&(*packed)[7]==1&&(*packed)[11]==-1);
     world.values[0]={0,0,1,0};world.values[2]={-1,0,0,0};
     const auto rotated=NativePropPackedTransform(identity,world);CHECK(rotated);
     // Native vertex(1,0,-2) reflects to canonical(1,0,2), then transforms to
     // canonical(0,3,5), hence native(0,3,-5). Test the shader float4 dot form.
     const float vertex[]{1,0,-2,1};float point[3]{};
     for(unsigned col=0;col<3;++col)for(unsigned row=0;row<4;++row)point[col]+=vertex[row]*(*rotated)[col*4+row];
     CHECK(point[0]==0&&point[1]==3&&point[2]==-5);
     world.values[3][3]=0;CHECK(!NativePropPackedTransform(identity,world));
    }

    // Optional local pack validation uses the same production reader. No asset
    // is embedded in this public test and no native/GPU call is performed.
    if(argc>1){CHECK(argc>=3&&argc<=34&&!std::strcmp(argv[1],"--packs"));
        std::vector<Blob> data;data.reserve(unsigned(argc-2));
        for(int i=2;i<argc;++i){std::ifstream file(argv[i],std::ios::binary|std::ios::ate);CHECK(file);
            const auto length=file.tellg();CHECK(length>0&&length<=8*1024*1024);
            data.emplace_back(std::size_t(length),std::byte{});file.seekg(0);
            CHECK(file.read(reinterpret_cast<char*>(data.back().data()),std::streamsize(data.back().size())));}
        std::vector<std::span<const std::byte>> packs;for(const auto& bytes:data)packs.emplace_back(bytes);
        CHECK(nativeSkinGeometry::ConfigureSet(packs,0xa7f219a1426216abull));
        std::printf("Validated %zu private native skin packs\n",packs.size());return 0;
    }
    const auto valid=Valid();const auto color=U32(valid,Record(2)+40),depth=U32(valid,Record(3)+40),indices=U32(valid,Record(2)+48);
    {auto b=valid;b[0]=std::byte{'X'};Reject(b);}
    {auto b=valid;Put(b,8,2u);Reject(b);}
    {auto b=valid;Put(b,12,unsigned(b.size()-1));Reject(b);}
    {auto b=valid;Put(b,16,Rig+1);Reject(b);}
    {auto b=valid;Put(b,28,0u);Reject(b);} // Diagnostic cap contract must not render.
    {auto b=valid;Put(b,28,3u);Reject(b);}
    {auto b=valid;b.pop_back();Reject(b);}
    {auto b=valid;b[96]^=std::byte{1};Reject(b);}
    {auto b=valid;b[Record(2)+128]^=std::byte{1};Body(b);Reject(b);} // Valid body, wrong VB digest.
    {auto b=valid;Put(b,Record(2)+40,UINT32_MAX);Body(b);Reject(b);}
    {auto b=valid;Put(b,Record(2),1u);Body(b);Reject(b);} // Incomplete section coverage.
    {auto b=valid;Put(b,Record(2)+192,std::uint16_t(147));Body(b);Reject(b);}
    {auto b=valid;Put(b,indices,std::uint16_t(3));Payloads(b);Reject(b);}
    {auto b=valid;b[color+8]=std::byte{2};Payloads(b);Reject(b);} // Weighted slot out of palette.
    {auto b=valid;b[color+12]=std::byte{63};Payloads(b);Reject(b);} // Weight sum254.
    {auto b=valid;Put(b,color,std::uint16_t(0x7c00));Payloads(b);Reject(b);} // Half infinity.
    {auto b=valid;Put(b,depth,std::uint16_t(0x3c00));Payloads(b);Reject(b);} // Color/depth position mismatch.
    {auto b=valid;b[depth+12]=std::byte{190};b[depth+13]=std::byte{65};Payloads(b);Reject(b);} // Sum255 but different global skin.
    {auto b=valid;Put(b,Record(2)+36,2u);Body(b);Reject(b);} // Non-triangle index count.
    {auto b=valid;Put(b,Record(3)+192,std::uint16_t(10));Put(b,Record(3)+194,std::uint16_t(11));Body(b);Reject(b);} // Same local slots, wrong global map.
    const auto split=SplitColors();
    {auto b=split;Put(b,24,9u);Reject(b);} // Above bounded native section capacity.
    {auto b=split;Put(b,24,0u);Reject(b);}
    {auto b=split;Put(b,Record(4)+36,3u);Put(b,Record(4)+52,6u);Payloads(b);Reject(b);} // Depth covers only one color material.
    {auto b=split;const auto v=U32(b,Record(3)+40);Put(b,v,std::uint16_t(0x3c00));Payloads(b);Reject(b);} // Second color material differs from depth.
    {auto b=split;Put(b,Record(1)+40,1408u);Body(b);Reject(b);} // v1 payload offset lies inside v2 table.
    const auto nva=ExplicitLayout(1),beach=ExplicitLayout(2),mixed=ExplicitLayout(2,true);
    {auto b=nva;std::memcpy(b.data(),"BC2NSK02",8);Put(b,8,2u);Reject(b);} // Old schema cannot reinterpret reserved bits.
    {auto b=beach;Put(b,Record(1)+56,0u);Body(b);Reject(b);} // Stride64 alone never infers half or float.
    {auto b=nva;Put(b,Record(1)+56,3u);Body(b);Reject(b);}
    {auto b=nva;Put(b,Record(4)+56,2u);Body(b);Reject(b);} // Beach cannot have a32-byte depth stream.
    {auto b=beach;Put(b,Record(4)+56,1u);Body(b);Reject(b);} // NVA cannot have a16-byte depth stream.
    {auto b=nva;Put(b,U32(b,Record(1)+40),0x7f800000u);Payloads(b);Reject(b);} // Float infinity.
    {auto b=nva;Put(b,U32(b,Record(4)+40),0x7fc00000u);Payloads(b);Reject(b);} // Float NaN.
    {auto b=nva;Put(b,U32(b,Record(4)+40),0.5f);Payloads(b);Reject(b);} // Float color/depth mismatch.
    {auto b=nva;b[U32(b,Record(1)+40)+12]=std::byte{2};Payloads(b);Reject(b);} // NVA ID offset12.
    {auto b=nva;b[U32(b,Record(1)+40)+16]=std::byte{63};Payloads(b);Reject(b);} // NVA weight offset16.
    {auto b=beach;Put(b,Record(1)+56,1u);Body(b);Reject(b);} // Same stride, wrong explicit interpretation.
    {auto b=beach;Put(b,U32(b,Record(1)+40),std::uint16_t(0x7e00));Payloads(b);Reject(b);}
    // A second valid family has its color action in section0, like Assault.
    auto other=valid;std::array<std::byte,320> row{};
    std::memcpy(row.data(),other.data()+Record(0),row.size());
    std::memcpy(other.data()+Record(0),other.data()+Record(2),row.size());
    std::memcpy(other.data()+Record(2),row.data(),row.size());
    Put(other,Record(0),0u);Put(other,Record(2),2u);Body(other);
    using Bytes=std::span<const std::byte>;
    {const std::array<Bytes,0> empty{};CHECK(!nativeSkinGeometry::ConfigureSet(empty,Rig));}
    {std::array<Bytes,33> many{};many.fill(valid);CHECK(!nativeSkinGeometry::ConfigureSet(many,Rig));}
    // A failed batch must not install its valid first member: later valid set
    // differs, so accidental partial installation makes the final check fail.
    {auto bad=other;bad[0]=std::byte{'X'};const std::array<Bytes,2> packs{valid,bad};CHECK(!nativeSkinGeometry::ConfigureSet(packs,Rig));}
    // Two different authored outcomes for identical native sources are ambiguous.
    {auto ambiguous=valid;ambiguous[color+16]=std::byte{1};Payloads(ambiguous);
        const std::array<Bytes,2> packs{valid,ambiguous};CHECK(!nativeSkinGeometry::ConfigureSet(packs,Rig));}
    const std::array<Bytes,7> packs{valid,other,split,nva,beach,mixed,valid}; // Identical duplicates collapse.
    CHECK(nativeSkinGeometry::ConfigureSet(packs,Rig));
    const std::array<Bytes,6> reordered{mixed,beach,nva,split,other,valid};CHECK(nativeSkinGeometry::ConfigureSet(reordered,Rig));
    CHECK(!nativeSkinGeometry::Configure(valid,Rig)); // Cannot replace the immutable set with a subset.
    // The same original source can inhabit an independent rigid prop pool.
    // It may never replace or alter the already configured ordinary hand set.
    auto rigid=valid;
    for(unsigned part:{2u,3u}){const auto r=Record(part),vo=U32(rigid,r+40),stride=U32(rigid,r+8);
        for(unsigned n=0;n<3;++n){rigid[vo+n*stride+8]=std::byte(part==3?1:0);
            rigid[vo+n*stride+12]=std::byte{255};rigid[vo+n*stride+13]=std::byte{0};}}
    Payloads(rigid);
    {const std::array<nativeSkinGeometry::PropPack,1> bad{{{rigid,Rig,11}}};CHECK(!nativeSkinGeometry::ConfigureProps(bad));}
    {const std::array<nativeSkinGeometry::PropPack,1> good{{{rigid,Rig,10}}};CHECK(nativeSkinGeometry::ConfigureProps(good));}
    CHECK(nativeSkinGeometry::ConfigureSet(reordered,Rig));
    std::puts("Native skin geometry: malformed packs, atomic sets, ambiguity, ordinal variants, explicit half/float layouts and paired palettes passed");
}
