#include "fvr/graphics/D3D11MirrorWindow.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
// Formats a copy may move between: the same 8-bit RGBA family as the swap chain.
bool RgbaFamily(DXGI_FORMAT f)noexcept {
    return f==DXGI_FORMAT_R8G8B8A8_TYPELESS||f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}
bool BgraFamily(DXGI_FORMAT f)noexcept {
    return f==DXGI_FORMAT_B8G8R8A8_TYPELESS||f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}
LRESULT CALLBACK MirrorProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
struct D3D11MirrorWindow::Impl {
    HWND window=nullptr;ComPtr<IDXGISwapChain1> chain;unsigned width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;MirrorWindowStats stats{};
    ~Impl(){chain.Reset();if(window&&IsWindow(window))DestroyWindow(window);}
};
D3D11MirrorWindow::D3D11MirrorWindow():impl_(std::make_unique<Impl>()){}
D3D11MirrorWindow::~D3D11MirrorWindow()=default;
const MirrorWindowStats& D3D11MirrorWindow::Statistics()const noexcept{return impl_->stats;}
bool D3D11MirrorWindow::Create(ID3D11Device* device,unsigned width,unsigned height,unsigned format)noexcept {
    auto& m=*impl_;if(!device||m.window||!width||!height)return false;
    const auto eyeFormat=DXGI_FORMAT(format);
    // Flip-model swap chains take no sRGB formats; copies stay within the family.
    const auto chainFormat=RgbaFamily(eyeFormat)?DXGI_FORMAT_R8G8B8A8_UNORM:BgraFamily(eyeFormat)?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_UNKNOWN;
    if(chainFormat==DXGI_FORMAT_UNKNOWN)return false;
    WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=MirrorProc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)) /* IDC_ARROW */;type.lpszClassName=L"FrostbiteVrMirror";
    RegisterClassExW(&type);
    // Fit the eye's aspect into at most 960 px wide on the desktop; DXGI stretches.
    const unsigned clientWidth=width>960?960:width,clientHeight=unsigned(double(height)*clientWidth/width);
    RECT area{0,0,LONG(clientWidth),LONG(clientHeight)};AdjustWindowRect(&area,WS_OVERLAPPEDWINDOW,FALSE);
    m.window=CreateWindowExW(0,type.lpszClassName,L"Frostbite VR mirror (left eye, as the headset sees it)",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,area.right-area.left,area.bottom-area.top,nullptr,nullptr,type.hInstance,nullptr);
    if(!m.window)return false;
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory2> factory;
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))||FAILED(dxgi->GetAdapter(&adapter))||FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))){DestroyWindow(m.window);m.window=nullptr;return false;}
    DXGI_SWAP_CHAIN_DESC1 d{};d.Width=width;d.Height=height;d.Format=chainFormat;d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount=2;d.Scaling=DXGI_SCALING_STRETCH;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;d.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    if(FAILED(factory->CreateSwapChainForHwnd(device,m.window,&d,nullptr,nullptr,&m.chain))){DestroyWindow(m.window);m.window=nullptr;return false;}
    factory->MakeWindowAssociation(m.window,DXGI_MWA_NO_ALT_ENTER);
    // Shown without activation so the game keeps keyboard and mouse focus.
    ShowWindow(m.window,SW_SHOWNOACTIVATE);
    m.width=width;m.height=height;m.format=eyeFormat;m.stats.created=true;return true;
}
void D3D11MirrorWindow::Pump()noexcept {
    auto& m=*impl_;if(!m.chain||m.stats.closed)return;
    for(MSG message{};PeekMessageW(&message,m.window,0,0,PM_REMOVE);){TranslateMessage(&message);DispatchMessageW(&message);}
    if(!IsWindow(m.window)){m.stats.closed=true;m.chain.Reset();m.window=nullptr;}
}
void D3D11MirrorWindow::Show(ID3D11DeviceContext* context,ID3D11Texture2D* leftEye)noexcept {
    auto& m=*impl_;Pump();if(!m.chain||m.stats.closed)return;
    if(!context||!leftEye){++m.stats.skipped;return;}
    D3D11_TEXTURE2D_DESC source{};leftEye->GetDesc(&source);
    if(source.Width!=m.width||source.Height!=m.height||source.ArraySize!=1||source.SampleDesc.Count!=1||
       !(RgbaFamily(source.Format)?RgbaFamily(m.format):BgraFamily(source.Format)&&BgraFamily(m.format))){++m.stats.skipped;return;}
    ComPtr<ID3D11Texture2D> buffer;if(FAILED(m.chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))){++m.stats.failures;return;}
    context->CopyResource(buffer.Get(),leftEye);
    const auto result=m.chain->Present(0,DXGI_PRESENT_DO_NOT_WAIT);
    if(result==DXGI_ERROR_WAS_STILL_DRAWING)++m.stats.busy;else if(FAILED(result))++m.stats.failures;else ++m.stats.presented;
}
} // namespace fvr::graphics
