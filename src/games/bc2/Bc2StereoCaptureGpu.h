#pragma once
// D3D11 glue for Bc2StereoCapture. Graphics thread only, except Abandon after
// every callback has quiesced. Uses only public ID3D11DeviceContext Get*
// queries, private copies and nonblocking Map(DO_NOT_WAIT); it never changes a
// game binding and never waits for the GPU.
#include "Bc2StereoCapture.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace fvr::bc2::stereoCapture {
// Snapshot the bound state used by one draw. Descriptions are reused from
// `previous` for unchanged interface identities.
void GatherPipelineState(ID3D11DeviceContext*,PipelineState& out,const PipelineState* previous)noexcept;

class ColorReadback {
public:
    // Queue a reduced-resolution copy of `source` (subresource 0) into `target`.
    // Bounded: at most MaxColorInFlight staging textures are alive.
    void Queue(ID3D11DeviceContext*,ID3D11Texture2D* source,ColorResult& target)noexcept;
    void Poll(ID3D11DeviceContext*)noexcept;
    unsigned InFlight()const noexcept;
    // After callbacks have quiesced: release private resources, mark Unresolved.
    void Abandon()noexcept;
private:
    struct Item {Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;ColorResult* target=nullptr;};
    std::array<Item,MaxColorInFlight> items_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> mip_;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mipView_;
    D3D11_TEXTURE2D_DESC mipDesc_{};
};
}
