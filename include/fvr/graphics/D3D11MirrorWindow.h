#pragma once
#include <cstdint>
#include <memory>
struct ID3D11Device;struct ID3D11DeviceContext;struct ID3D11Texture2D;
namespace fvr::graphics {
struct MirrorWindowStats {std::uint64_t presented=0,busy=0,skipped=0,failures=0;bool created=false,closed=false;};
// HOST DEVICE ONLY. A desktop window that shows the left eye exactly as the
// headset receives it (after host prop composition). It never touches the
// game, its window or its swap chain. Show() pumps only this window's
// messages and never waits on the display: a busy queue skips the image.
// Closing the window stops the mirror; the XR session is unaffected.
class D3D11MirrorWindow final {
public:
    D3D11MirrorWindow();~D3D11MirrorWindow();
    bool Create(ID3D11Device*,unsigned width,unsigned height,unsigned format)noexcept;
    void Show(ID3D11DeviceContext*,ID3D11Texture2D* leftEye)noexcept;
    void Pump()noexcept; // Messages only, for frames without a new image.
    const MirrorWindowStats& Statistics()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace fvr::graphics
