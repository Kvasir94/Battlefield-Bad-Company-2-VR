#pragma once
#include "Bc2RigWorkerBinding.h"
#include <ostream>
namespace fvr::bc2::rigWorkerRuntime {
using OwnerResolver=std::optional<ReloadProducerOwner>(*)()noexcept;
// Optional immutable copied context for diagnostic frame comparisons only.
// NoExactView never becomes admitted, even when this lease matches its frame.
using DiagnosticLeaseResolver=std::optional<RigWorkerViewLease>(*)()noexcept;
// The parent initializes MinHook, supplies independently verified scene types,
// and keeps this module resident. Install creates a DISABLED diagnostic hook.
// An explicit true flag is required. Bracket the parent's MH_ALL_HOOKS call with
// BeginGlobalStart/CompleteGlobalStart; callbacks cannot admit in between.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uint32_t imageBase,
    RigWorkerSceneTypes,OwnerResolver,RigWorkerViewResolver,bool diagnostic=false,DiagnosticLeaseResolver=nullptr);
bool BeginGlobalStart()noexcept;
bool CompleteGlobalStart(bool globalEnableSucceeded)noexcept;
void Disable()noexcept;
// No removal/freeing while an original call can still return through the hook.
// Parent must include this in its existing disable/drain/resident-module logic.
bool Quiescent()noexcept;
void Report(std::ostream&);
}
