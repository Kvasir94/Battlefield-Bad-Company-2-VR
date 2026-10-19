#pragma once
namespace fvr::bc2 {
// The native UI raster must be copied while the swapchain still contains native
// composition. The desktop mirror replaces that same buffer afterwards.
template<class CaptureMenu,class MirrorEye>
void CaptureMenuBeforeDesktopMirror(CaptureMenu&& captureMenu,MirrorEye&& mirrorEye) noexcept {
    captureMenu();
    mirrorEye();
}
}
