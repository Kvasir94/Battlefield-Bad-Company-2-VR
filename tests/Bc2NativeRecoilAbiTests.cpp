#include "Test.h"
#include <Windows.h>
#include <MinHook.h>
#include "Bc2NativeRecoilTransfer.h"
using namespace fvr::bc2;
namespace {
struct State {unsigned ticks=0,cameras=0,observedTicks=0,observedCameras=0;float dt=0;void* context=nullptr;};
NativeSwayTickFn originalTick=nullptr;
NativeSwayCameraFn originalCamera=nullptr;
__declspec(noinline) void __fastcall FakeTick(void* self,void*,float dt,void* context){
    auto& s=*static_cast<State*>(self);++s.ticks;s.dt=dt;s.context=context;
}
__declspec(noinline) void __fastcall FakeCamera(void* self,void*,void* output){
    auto& s=*static_cast<State*>(self);++s.cameras;
    auto* p=static_cast<float*>(output);for(unsigned n=0;n<16;++n)p[n]=float(n)+s.dt;
}
void __fastcall TickHook(void* self,void*,float dt,void* context){
    originalTick(self,dt,context);auto& s=*static_cast<State*>(self);++s.observedTicks;
}
void __fastcall CameraHook(void* self,void*,void* output){
    originalCamera(self,output);auto& s=*static_cast<State*>(self);++s.observedCameras;
}
}
int main(){
    CHECK(MH_Initialize()==MH_OK);
    CHECK(MH_CreateHook(FakeTick,TickHook,reinterpret_cast<void**>(&originalTick))==MH_OK);
    CHECK(MH_CreateHook(FakeCamera,CameraHook,reinterpret_cast<void**>(&originalCamera))==MH_OK);
    CHECK(MH_EnableHook(MH_ALL_HOOKS)==MH_OK);
    NativeSwayTickFn volatile tick=reinterpret_cast<NativeSwayTickFn>(FakeTick);
    NativeSwayCameraFn volatile camera=reinterpret_cast<NativeSwayCameraFn>(FakeCamera);
    State state;unsigned context=0;std::array<float,18> output{};
    for(unsigned n=0;n<1024;++n){
        const float dt=.001f+float(n)*.00001f;context=n;
        tick(&state,dt,&context);
        CHECK(state.ticks==n+1&&state.observedTicks==n+1&&state.dt==dt&&state.context==&context);
        output.front()=-123.f;output.back()=-456.f;camera(&state,output.data()+1);
        CHECK(state.cameras==n+1&&state.observedCameras==n+1&&output.front()==-123.f&&output.back()==-456.f);
        for(unsigned component=0;component<16;++component)CHECK(output[component+1]==float(component)+dt);
    }
    CHECK(MH_DisableHook(MH_ALL_HOOKS)==MH_OK);
    tick(&state,.25f,&context);camera(&state,output.data()+1);
    CHECK(state.ticks==1025&&state.cameras==1025&&state.observedTicks==1024&&state.observedCameras==1024);
    CHECK(MH_Uninitialize()==MH_OK);return 0;
}
