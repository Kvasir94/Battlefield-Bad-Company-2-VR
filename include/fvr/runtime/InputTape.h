#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>
namespace fvr::runtime {
// Offline BC2 input tape, version 1 (docs/REPLAY_INPUT.md). A process-local
// developer source: it owns samples, never gameplay, consumer or native state.
// Each Next() is one native input gather, not a render pass or a wall-clock poll.
//
//   FVR_BC2_INPUT 1 STEP_MICROSECONDS SEED
//   #! weapon=XM8_sp_s loaded<capacity reserve>=30 anchor=weapon
//   #! expect transfers=1 no-auto-reload
//   TICKS AGE_MS EPOCH FOCUSED HEADVALID FLOOR head(7) referenceHead(7) left right
//   hand: FLAGS BUTTONS trigger squeeze stickX stickY grip(7) aim(7)
inline constexpr unsigned TapeVersion=1,TapeMaxTicks=360000,TapeStaleMs=100;
inline constexpr unsigned TapeMinStepUs=1000,TapeMaxStepUs=100000;
inline constexpr unsigned TapeHandFlags=2047,TapeHandButtons=511;
enum class TapeOp:std::uint8_t {Eq,Ne,Lt,Le,Gt,Ge};
// One `#!` token: `key`, `key=value` or `key<op>value`. Values stay text so
// `loaded<capacity` compares against the live capacity, not a recorded number.
struct TapeCondition {std::string key;TapeOp op=TapeOp::Eq;std::string value;};
struct TapeHeader {
    unsigned stepUs=0;std::uint64_t seed=0;
    std::vector<TapeCondition> conditions; // `#!` lines other than `#! expect`
    std::vector<std::string> expect;       // raw `#! expect` tokens, read by tools/replay
    std::optional<std::string> Value(const std::string& key)const;
    bool AnchorWeapon()const{const auto v=Value("anchor");return v&&*v=="weapon";}
};
// Live native observation compared against the `#!` start conditions.
struct TapeLiveState {std::string weaponAsset;int loaded=0,reserve=0,capacity=0;};
// Unknown keys (checkpoint, source, notes) are provenance and never block.
// weapon matches exactly or as a case-insensitive prefix of the native asset;
// an empty live weaponAsset means "not observable here" and skips only that condition.
bool TapePreconditionMet(const TapeHeader&,const TapeLiveState&)noexcept;
struct TapeSample {unsigned ticks=0,ageMs=0;std::uint64_t epoch=0;interaction::InputFrame frame{};
    // anchor=weapon: left grip/aim in right-grip space, captured at load.
    bool anchored=false;math::Pose leftGripInGun{},leftAimInGun{};};
struct TapeTick {
    std::uint64_t tick=0;   // 1-based gather counter, also the published generation
    bool available=false;   // false: stale, epoch transition, EOF, stopped or failed load
    bool finished=false;    // EOF or stopped; unavailable forever after
    interaction::InputFrame frame{}; // unavailable ticks carry an empty, unfocused frame
};
class InputTape {
public:
    bool Load(std::istream&,std::string& error);
    // owner: native player/soldier identity (0 = not bound). A changed owner
    // stops the tape permanently. liveGunGrip: optional current right grip used
    // to re-compose an anchor=weapon left hand; null re-uses the tape's own.
    TapeTick Next(std::uint64_t owner=0,const math::Pose* liveGunGrip=nullptr);
    bool Finished()const noexcept{return stopped_||cursor_>=samples_.size();}
    bool Stopped()const noexcept{return stopped_;}
    std::uint64_t Ticks()const noexcept{return tick_;}
    std::uint64_t TotalTicks()const noexcept{return total_;}
    const TapeHeader& Header()const noexcept{return header_;}
    const std::vector<TapeSample>& Samples()const noexcept{return samples_;}
private:
    bool Fail(std::string& error,const std::string& message);
    TapeHeader header_{};std::vector<TapeSample> samples_;
    std::size_t cursor_=0;unsigned remaining_=0;std::uint64_t tick_=0,total_=0,owner_=0,epoch_=0;
    bool haveEpoch_=false,stopped_=true;
};
// Run-length encoder for live recording and generated tapes. Rows are written
// with round-trip float precision, so a written tape replays bit-identically.
class InputTapeWriter {
public:
    bool Begin(std::ostream&,unsigned stepUs,std::uint64_t seed,const std::vector<std::string>& headerLines={});
    // available=false records an unavailable gather (age clamps above 100 ms).
    bool Append(const interaction::InputFrame&,unsigned ageMs,bool available=true,std::uint64_t epoch=0);
    bool Finish();
    std::uint64_t Ticks()const noexcept{return ticks_;}
    std::uint64_t Rows()const noexcept{return rows_;}
private:
    bool Flush();
    std::ostream* out_=nullptr;std::string pending_;unsigned pendingTicks_=0;std::uint64_t ticks_=0,rows_=0;
};
// Pose helpers shared by anchor=weapon and the offline simulator.
math::Pose ComposePose(const math::Pose& parent,const math::Pose& child)noexcept;
math::Pose InversePose(const math::Pose&)noexcept;
}
