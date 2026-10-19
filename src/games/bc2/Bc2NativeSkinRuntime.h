#pragma once
#include "fvr/engine/PeImage.h"
#include "Bc2BodyAmmoRenderer.h"
#include "fvr/graphics/BodyPropFrame.h"
#include <cstdint>
#include <ostream>
#include <span>
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace fvr::bc2::nativeSkinRuntime {
// Dynamic scope around the exact original tracked eye Draw. Workers outside
// this scope cannot borrow another eye's metadata or suppress its fallback.
struct PropEyeScope {
    BodyAmmoEyeKey key{};graphics::BodyPropEye props{};
    std::array<unsigned,graphics::MaxBodyProps> submitted{};
    PropEyeScope* previous=nullptr;
    PropEyeScope(BodyAmmoEyeKey,const graphics::BodyPropEye&)noexcept;
    ~PropEyeScope();
    PropEyeScope(const PropEyeScope&)=delete;
    bool Rendered(const graphics::BodyPropInstance&,std::int64_t now)const noexcept;
};
bool Requested()noexcept;
// Native character requests, not mesh-name or triangle-count attribution.
// Install validates the complete recovered functions against disk and memory.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uint32_t base,ID3D11Device*);
// Called only at the game's established immediate-context graphics boundary.
void Graphics(ID3D11DeviceContext*)noexcept;
void BeginFrame(std::uint32_t world,std::uint32_t nativeFrame)noexcept;
void Started(bool globalEnableSucceeded)noexcept;
void Disable()noexcept;
bool Quiescent()noexcept;
// Diagnostics sampled without taking the render/verification lock. Counts are
// cumulative except readPending (outstanding copies); registered is pinned entries.
// Fields are independent relaxed loads; an in-progress transition may span a sample.
struct SourceVerificationCounters {
    std::uint64_t registered=0,availableHits=0,pendingHits=0,rejectedHits=0,full=0;
    std::uint64_t copyStarts=0,readPending=0,readBusy=0,matches=0,hashMismatches=0;
    std::uint64_t createFailures=0,mapFailures=0;
    unsigned capacity=0;
};
SourceVerificationCounters ReadSourceVerificationCounters()noexcept;
void WriteSourceVerificationTelemetry(std::ostream&);
void Report(std::ostream&);
}
