#pragma once
// Build 795745: graphics options are re-applied by GameRenderer::Update before
// native view construction. Preserve the launch-selected dimensions there, so
// native DX11 reset/resource creation and world viewport setup agree with XR.
namespace renderResolution {
constexpr std::uintptr_t Apply = 0x7de030, OptionsGlobal = 0x1570ea0, OptionsVtable = 0x142e570;
constexpr unsigned char ApplyCode[] = {0x83,0xec,0x4c,0x53,0x56,0x8b,0xf1,0x33,0xdb,0x38,0x9e,0xc0,0,0,0,0x0f,0x84,0x5e,0x11,0,0};
using ApplyFn = void(__thiscall*)(void*);
ApplyFn original = nullptr;
unsigned width = 0, height = 0, corrections = 0;
unsigned Dimension(const char* name) {
    char value[32]{};const auto n=GetEnvironmentVariableA(name,value,sizeof(value));
    if(!n)return 0;if(n>=sizeof(value))return UINT_MAX;
    unsigned result=0;for(unsigned i=0;i<n;++i){if(value[i]<'0'||value[i]>'9'||result>8192)return UINT_MAX;result=result*10+unsigned(value[i]-'0');}
    return result>=128&&result<=8192?result:UINT_MAX;
}
void __fastcall ApplyHook(void* self,void*) {
    // The ctor installs this exact singleton/vtable. Never modify a temporary
    // options copy, other object's storage, or either world-view rectangle.
    if(self && self==*reinterpret_cast<void* volatile*>(OptionsGlobal) &&
       *reinterpret_cast<std::uintptr_t*>(self)==OptionsVtable) {
        auto* bytes=static_cast<unsigned char*>(self);
        auto* size=reinterpret_cast<float*>(bytes+0xa0);
        if(size[0]!=float(width)||size[1]!=float(height)||bytes[0xac]) {
            const float oldWidth=size[0],oldHeight=size[1];
            size[0]=float(width);size[1]=float(height);bytes[0xac]=0;bytes[0xc0]=1;
            if(++corrections<=4)Log("render-resolution: native options %.0fx%.0f -> %ux%u windowed (application %u)",oldWidth,oldHeight,width,height,corrections);
        }
    }
    original(self); // Native apply owns DX11 settings, reset, and allocation.
}
bool Install() {
    char off[4]{};if(GetEnvironmentVariableA("FVR_BC2_RENDER_RESOLUTION",off,sizeof(off))&&off[0]=='0')return true;
    width=Dimension("FVR_BC2_RENDER_WIDTH");height=Dimension("FVR_BC2_RENDER_HEIGHT");
    if(!width&&!height)return true;
    if(!width||!height||width==UINT_MAX||height==UINT_MAX){Log("render-resolution: REFUSED invalid launch dimensions");return false;}
    if(std::memcmp(reinterpret_cast<const void*>(Apply),ApplyCode,sizeof(ApplyCode)) ||
       *reinterpret_cast<const unsigned char*>(0x7df1a8)!=0xc3){Log("render-resolution: REFUSED native graphics apply signature");return false;}
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(reinterpret_cast<void*>(Apply),reinterpret_cast<void*>(&ApplyHook),reinterpret_cast<void**>(&original))!=MH_OK ||
       MH_EnableHook(reinterpret_cast<void*>(Apply))!=MH_OK){Log("render-resolution: REFUSED graphics apply hook");return false;}
    Log("render-resolution: bound native graphics apply for %ux%u windowed",width,height);return true;
}
}
