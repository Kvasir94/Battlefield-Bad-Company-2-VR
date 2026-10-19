#pragma once
// Game-window presentation during a VR stream; gameplay and XR are unaffected.
//
// With mirroring enabled, each Present copies the most recent compatible
// tracked left eye while its cache is at most 300 ms old. This is publication
// age, not GPU render duration. Missing, stale or incompatible eye output uses
// an opaque waiting surface instead of intentionally showing the native camera.
// A waiting clear needs a valid render-target view; device failures must still
// be diagnosed. A fresh left-eye cache alone does not prove a complete XR pair.
//
// The optional capture records every presented back buffer at quarter size
// for consecutive-frame checks (tools/replay/frame_spikes.py --desktop).
#include <cstdint>
#include <filesystem>
#include <memory>
struct ID3D11DeviceContext;struct ID3D11Texture2D;struct IDXGISwapChain;
namespace fvr::bc2 {
struct DesktopEyeMirrorStats {std::uint64_t eyes=0,presents=0,mirrored=0,stale=0,mismatched=0,captured=0,captureBusy=0,captureDropped=0;
    std::uint64_t waitingAttempts=0,waitingClears=0,waitingFailures=0;
    std::int32_t waitingLastHresult=0;
};
class DesktopEyeMirror {
public:
    DesktopEyeMirror();~DesktopEyeMirror();
    void Configure(bool mirror,bool capture,const std::filesystem::path& folder);
    bool Active()const noexcept;
    // Render thread, right after the tracked left eye was captured for the headset.
    void OnLeftEye(ID3D11DeviceContext*,ID3D11Texture2D*,std::int64_t nowNs,unsigned frame)noexcept;
    // Render thread, before the game's Present of the observed swap chain.
    void OnPresent(IDXGISwapChain*,ID3D11DeviceContext*,std::int64_t nowNs,unsigned frame)noexcept;
    void Finish()noexcept;
    DesktopEyeMirrorStats Statistics()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
