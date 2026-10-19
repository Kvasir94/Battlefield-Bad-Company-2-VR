#pragma once
// Offline BC2 magazine-reload simulator (docs/REPLAY_INPUT.md, "Offline simulator").
// Lifted from the closed loop in tests/Bc2MagazinePhysicalProbeTests.cpp: the real
// HandInteraction, Bc2MagazinePhysicalReload consumer and TrackedRig run against a
// MOCK native side (fixed-latency reserve/start/lease/gate/submit/ack/retire) on a
// virtual clock. One tape tick = one simulated native gather.
//
// The native model is hand-written, not fitted to traces. A sim pass is labelled
// "sim" everywhere and never stands in for a live game pass. Divergence between
// sim and live on the same tape means the model (or the tape) is stale.
#include "fvr/interaction/ControllerInput.h"
#include "fvr/runtime/InputTape.h"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
namespace fvr::sim {
struct SimOptions {
    int loaded=27,reserve=83,capacity=30; // native start state of the mocked XM8
    unsigned tailTicks=30;                // gathers after EOF (unavailable input)
    std::uint64_t maxTicks=runtime::TapeMaxTicks+1000;
};
struct SimSummary {
    std::uint64_t ticks=0,events=0;unsigned starts=0,submits=0,cancels=0,transfers=0,completed=0,originalReturns=0;
    int loaded=0,reserve=0;std::string finalPhase,stopReason;bool preconditionMet=false,tapeFinished=false,probeCompleted=false;
};
// Writes one gun-event row per line in the docs/GUN-EVENT-LOG.md v1 schema.
// Every row carries "sim":true.
using EventSink=std::function<void(const std::string& jsonLine)>;
// Replays a loaded tape open-loop. Returns false only when the tape's `#!`
// start conditions do not match the simulated start state (precondition_unmet).
bool RunTape(runtime::InputTape&,const SimOptions&,const EventSink&,SimSummary&);
// Synthetic recording generator: drives the same loop with the existing closed-loop
// Bc2MagazinePhysicalProbe (fresh XM8 magazine, optional free carry) and records the
// probe-prepared input of every gather into a tape. The tape is then an ordinary
// open-loop recording; replaying it through RunTape must reproduce the reload.
// sliderRelease: the probe opens the grip past 25% of the replacement stroke and
// keeps the hand still, so the XM8 Slider settle must seat and submit by itself.
bool GenerateXm8MagazineTape(std::ostream& tape,bool carry,SimSummary&,const EventSink& events={},bool sliderRelease=false);
void WriteSummary(std::ostream&,const SimSummary&);
}
