#pragma once
#include <d3d11.h>
#include <memory>

namespace fvr::graphics {
// Scene depth for XR_KHR_composition_layer_depth, moved as R32_FLOAT window depth.
// Both classes draw in a private ID3DDeviceContextState (SwapDeviceContextState),
// so the caller's pipeline state is untouched. Immediate-context thread only.

// Producer side: converts a depth-stencil texture the game rendered (D24S8, D32,
// D32S8X24 or D16; typed or typeless; single-sample) into a private R32_FLOAT
// image per eye. The source is first copied into a private typeless twin that is
// then sampled, so the game's still-bound DSV is never bound as an input.
class D3D11DepthCapture {
public:
    D3D11DepthCapture();~D3D11DepthCapture();
    D3D11DepthCapture(const D3D11DepthCapture&)=delete;D3D11DepthCapture& operator=(const D3D11DepthCapture&)=delete;
    bool Capture(ID3D11Device*,unsigned eye,ID3D11Texture2D* depth,UINT width,UINT height) noexcept;
    ID3D11Texture2D* Eye(unsigned eye)const noexcept;
    void Reset() noexcept;
    HRESULT LastError()const noexcept{return error_;}
    // 1 setup, 2 unsupported source (format/samples/size), 3 resources, 4 views.
    unsigned LastStage()const noexcept{return stage_;}
private:struct Impl;std::unique_ptr<Impl> impl_;HRESULT error_=S_OK;unsigned stage_=0;
    bool Fail(unsigned stage,HRESULT error)noexcept{stage_=stage;error_=error;return false;}
};

// Host side: writes R32_FLOAT window depth into an XR depth swapchain image by
// drawing SV_Depth (no format-cast copy into a depth-stencil resource).
class D3D11DepthWriter {
public:
    D3D11DepthWriter();~D3D11DepthWriter();
    D3D11DepthWriter(const D3D11DepthWriter&)=delete;D3D11DepthWriter& operator=(const D3D11DepthWriter&)=delete;
    bool Initialize(ID3D11DeviceContext*) noexcept;
    // `target` is a DSV of the swapchain image; `source` an SRV of the R32_FLOAT image.
    bool Write(ID3D11ShaderResourceView* source,ID3D11DepthStencilView* target,UINT width,UINT height) noexcept;
    HRESULT LastError()const noexcept{return error_;}
private:struct Impl;std::unique_ptr<Impl> impl_;HRESULT error_=S_OK;
};
}
