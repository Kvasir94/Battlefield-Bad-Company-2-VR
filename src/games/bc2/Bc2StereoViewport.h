#pragma once
#include <array>

namespace fvr::bc2 {
using StereoViewport = std::array<unsigned,4>;
enum class StereoViewportResult : unsigned {Matched, Synchronized, InvalidOwner, Unreadable, SizeChanged, SetterMismatch, Count};
// Native reads/calls remain at the owned pre-visibility boundary. The callbacks
// make rejection before a native write and setter readback independently testable.
template<class ReadMain,class ReadEye,class SetEye>
StereoViewportResult SynchronizeStereoViewport(bool owned,unsigned width,unsigned height,
    StereoViewport& main,StereoViewport& eye,ReadMain&& readMain,ReadEye&& readEye,SetEye&& setEye) noexcept {
    if(!owned)return StereoViewportResult::InvalidOwner;
    if(!readMain(main)||!readEye(eye))return StereoViewportResult::Unreadable;
    if(!width||!height||main[2]!=width||main[3]!=height)return StereoViewportResult::SizeChanged;
    if(main==eye)return StereoViewportResult::Matched;
    setEye(main);
    if(!readEye(eye)||eye!=main)return StereoViewportResult::SetterMismatch;
    return StereoViewportResult::Synchronized;
}
}
