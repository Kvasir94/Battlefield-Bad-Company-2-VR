#pragma once
// Live gun-event emitter (docs/GUN-EVENT-LOG.md, schema v1). Portable core:
// fixed event structs, a bounded lock-free multi-producer ring, the JSON line
// formatter, the writer and the two state trackers that turn observations the
// existing hooks already make into rows. The DLL glue lives in
// Bc2ReloadFlowRuntime.cpp (native rows), Bc2Gameplay.cpp (fvr rows, tick) and
// NativeProbe.cpp (file, drain thread, report).
//
// Observation only. Nothing here reads native memory, calls native code or is
// read back by the game, the consumers or a replay. Producers never format,
// allocate or block: Push copies one struct into the ring or counts a drop.
// Only the drain thread formats and writes.
#include "Bc2GunEventLogConfig.h"
#include "fvr/interaction/DetachableMagazine.h"
#include "fvr/interaction/Feedback.h"
#include <optional>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
namespace fvr::bc2 {
inline constexpr int GunEventSchemaVersion=1;

enum class GunEventSource:std::uint8_t {Native,Fvr};
enum class GunEventPhase:std::uint8_t {
    // src "native"
    FiringState,Transfer,Commit,Restore,Step,AutoReload,FreezeStart,FreezeStop,Fire,WeaponSwitch,NativeSample,Dropped,
    SlideLock,AnimSound,
    // src "fvr"
    Begin,PouchStart,Capture,Seat,Credit,Rack,Cancel,Unseat,Release,Complete,End,Haptic,Claim,Tape,FvrSample,
    Chamber,Prop,Pack,RackTrace,Hint,
    Count
};
// Wire name: lower case with underscores. NativeSample and FvrSample are both "sample".
std::string_view GunEventPhaseName(GunEventPhase)noexcept;
GunEventSource GunEventPhaseSource(GunEventPhase)noexcept;
enum class GunCopy:std::uint8_t {Client0=0,Client1=1,Server=2,None=3};
std::string_view GunCopyName(GunCopy)noexcept; // "client0", "client1", "server"; empty for None (null)

// Presence/boolean bits for GunEvent::flags.
namespace gun_flag {
inline constexpr std::uint16_t Timer=1,Matched=2,Suppressed=4,Stale=8,Cancelled=16,ByGrip=32,
    Met=64,HasMet=128,Ammo=256,Cycle=512,EmptyOverride=1024,Next=2048,Wide=4096,Value0=8192,Sent=16384,MagOut=32768;
}
template<std::size_t N>using GunText=std::array<char,N>;
// One fixed-size, trivially copyable row. Field use per phase (keys in the line):
//   firing_state  v0 from, v1 to, v2 next (Next), v3 flags_a8 (Next), f timer (Timer)
//   transfer      v0..v3 loaded_before, reserve_before, loaded_after, reserve_after; reason path
//   restore       v0..v3 as transfer; Matched -> matched
//   commit        v0 from, v1 to
//   step          v0 state, v1 loaded, v2 reserve; EmptyOverride -> empty_override
//   auto_reload   Suppressed -> suppressed; v0 loaded, v1 reserve (Ammo)
//   freeze_start  reason (optional); f timer (Timer)
//   freeze_stop   reason; wide held_us (Wide)
//   fire          reason result; v0 loaded, v1 reserve, v2 consumed; dry: v3 native_click
//   weapon_switch detail from (asset), weapon.asset to; Cancelled -> cancelled
//   sample native v0 loaded, v1 reserve, v2 cur, v3 next, v4 shots, v5 transfers; magPhase; Stale -> stale
//   dropped       wide n
//   slide_lock    v0 state, v1 loaded, v2 locked, v3 last_round (derived, see GunCopyTracker::Observe)
//   anim_sound    detail anim GUID (hex); v0 listener view, v1 soldier view bit, v2 played, v3 callback id; wide soldier
//   begin         hand, detail kind
//   pouch_start   hand, detail item (optional)
//   capture       v0 distance_mm (Value0)
//   seat          v0 cycle (Cycle), v1 credits (Value0)
//   credit        v0 loaded, v1 reserve, v2 cycle (Cycle)
//   rack          ByGrip -> by_grip
//   cancel        reason
//   unseat        hand
//   release       reason -> "to"
//   complete      v0 cycle (Cycle)
//   end           reason
//   haptic        detail kind, hand; Sent -> sent
//   claim         hand, detail kind, wide token, reason result
//   tape          reason, detail name (optional); HasMet/Met -> met; v0 loaded, v1 reserve (Ammo)
//   sample fvr    hand, magPhase
//   chamber       detail event; v0 loaded, v1 display_ammo, v2 chamber_loaded, v3 needs_rack; reason label; MagOut -> mag_out
//                 (mod-owned VR chamber model, never game truth)
// Every fvr row carries mag_phase when magPhase is set, and mag_out with MagOut.
struct GunEvent {
    std::uint64_t tick=0;std::int64_t qpcUs=0,wide=0;
    std::uint32_t weaponId=0;
    GunEventPhase phase=GunEventPhase::Count;GunCopy copy=GunCopy::None;std::uint16_t flags=0;
    std::array<std::int32_t,6> v{};float f=0;
    GunText<48> asset{};     // weapon.asset; empty -> no weapon object
    GunText<24> reason{};
    GunText<48> detail{};
    GunText<8> hand{};
    GunText<24> magPhase{};
};
// Copies printable ASCII only, truncating to the field; never fails.
template<std::size_t N>void SetGunText(GunText<N>& out,std::string_view text)noexcept {
    std::size_t n=0;for(char c:text){if(n+1>=N)break;if(static_cast<unsigned char>(c)>=32&&static_cast<unsigned char>(c)<127)out[n++]=c;}
    for(;n<N;++n)out[n]=0;
}
template<std::size_t N>std::string_view GunTextView(const GunText<N>& t)noexcept {
    std::size_t n=0;while(n<N&&t[n])++n;return {t.data(),n};
}
// Appends one JSON object and '\n'. Locale independent, no pretty printing.
// sim=true adds "sim":true (used only by tests to compare against the simulator).
void AppendGunEventLine(std::string& out,const GunEvent&,bool sim=false);

// Bounded multi-producer / single-consumer ring (Vyukov sequence cells).
// Push is wait-free apart from one CAS retry loop on the enqueue cursor and
// never allocates; a full ring drops the row and counts it. Pop is for the
// single drain thread only. Capacity must be a power of two.
class GunEventRing {
public:
    explicit GunEventRing(std::size_t capacity=4096);
    bool Push(const GunEvent&)noexcept;
    bool Pop(GunEvent&)noexcept;
    std::size_t Capacity()const noexcept{return mask_+1;}
    std::uint64_t Pushed()const noexcept{return pushed_.load(std::memory_order_relaxed);}
    std::uint64_t Dropped()const noexcept{return dropped_.load(std::memory_order_relaxed);}
    // Drops not yet reported by a `dropped` row; resets to zero.
    std::uint64_t TakeUnreportedDrops()noexcept{return unreported_.exchange(0,std::memory_order_acq_rel);}
    // A producer lost a row without pushing (e.g. a busy tracker): counted like an overflow.
    void NoteDropped()noexcept{dropped_.fetch_add(1,std::memory_order_relaxed);unreported_.fetch_add(1,std::memory_order_relaxed);}
private:
    struct Cell {std::atomic<std::size_t> sequence{0};GunEvent event{};};
    std::unique_ptr<Cell[]> cells_;std::size_t mask_=0;
    alignas(64) std::atomic<std::size_t> enqueue_{0};
    alignas(64) std::size_t dequeue_=0;
    std::atomic<std::uint64_t> pushed_{0},dropped_{0},unreported_{0};
};

// Process-wide log: enabled flag, current gather tick, clock, ring and writer.
// Every producer entry point is a no-op unless Open succeeded.
class GunEventLog {
public:
    using Clock=std::int64_t(*)()noexcept; // QPC in microseconds (one clock for the process)
    // Before hooks run. Fails on a null/bad stream, null clock, or a second Open.
    bool Open(std::unique_ptr<std::ostream>,Clock,std::size_t capacity=4096);
    bool Enabled()const noexcept{return enabled_.load(std::memory_order_acquire);}
    // Gather hook, once per GatherHook update: the schema's `tick`.
    void SetTick(std::uint64_t tick)noexcept{tick_.store(tick,std::memory_order_release);}
    std::uint64_t Tick()const noexcept{return tick_.load(std::memory_order_acquire);}
    std::int64_t NowUs()const noexcept{return clock_?clock_():0;}
    // Stamps tick and qpc_us when they are zero, then pushes. Hot path.
    bool Push(GunEvent)noexcept;
    // A row was lost before Push (busy tracker); the drain reports it in a `dropped` row.
    void NoteLost()noexcept{if(Enabled()&&ring_)ring_->NoteDropped();}
    // Drain thread only. Formats and writes everything queued, then one
    // `dropped` row for any overflow since the last drain. False once a write failed.
    bool Drain();
    // After hooks are disabled: stops accepting rows, drains, flushes.
    bool Close();
    std::uint64_t Written()const noexcept{return written_;}
    std::uint64_t Pushed()const noexcept{return ring_?ring_->Pushed():0;}
    std::uint64_t Dropped()const noexcept{return ring_?ring_->Dropped():0;}
    std::uint64_t DroppedRows()const noexcept{return droppedRows_;}
    bool Failed()const noexcept{return failed_;}
    bool Closed()const noexcept{return closed_;}
    void Report(std::ostream&)const;
private:
    std::atomic<bool> enabled_{false};std::atomic<std::uint64_t> tick_{0};Clock clock_=nullptr;
    std::unique_ptr<GunEventRing> ring_;std::unique_ptr<std::ostream> out_;std::string line_;
    std::uint64_t written_=0,droppedRows_=0;bool failed_=false,closed_=false;
};
// The DLL's single instance.
GunEventLog& GunEvents()noexcept;

// Weapon identity shared by every row about one carried item: the exact
// SoldierWeaponData name and the client weapon object (stable across the three copies).
struct GunWeapon {GunText<48> asset{};std::uint32_t id=0;bool operator==(const GunWeapon&)const=default;};
GunWeapon MakeGunWeapon(std::string_view asset,std::uint32_t id)noexcept;
// Gather: the carried item or its SoldierWeaponData name changed (first sight: from is empty).
void PushWeaponSwitch(GunEventLog&,const GunWeapon& from,const GunWeapon& to)noexcept;

// Native rows from the existing reload-flow hook boundaries. One tracker per
// process; per-copy state is guarded by a non-blocking try-lock (contention
// skips the row and is counted in Busy and as a `dropped` row; it never waits
// inside a native callback; a skipped periodic sample is counted in Busy only).
struct GunCopyState {
    std::uint32_t firing=0;std::uint32_t current=0,next=0;float timer=0;
    std::int32_t loaded=0,reserve=0;std::uint8_t flagsA8=0;
};
class GunCopyTracker {
public:
    // Every entry/exit boundary an existing hook read. Emits firing_state on a
    // state change, fire when only loaded dropped (consumed = the drop), and
    // step for any other unexplained count change. A new firing object or
    // weapon resets the baseline without rows. Also derives slide_lock from the
    // same reads (static RE, not live-verified): the action is held open while
    // state 2 (ready) has loaded 0; it latches on that and clears only when
    // loaded rises or the copy enters reload states 10-12, so a dry trigger
    // pull through state 3 cannot chatter. last_round marks a lock reached by
    // a shot taking loaded 1 -> 0 (AS2 slideLockOnEmptyMag).
    // The engine has no lock state; this is game-derived evidence for the VR model.
    void Observe(GunEventLog&,const GunWeapon&,GunCopy,const GunCopyState&)noexcept;
    // Transfer hook (0x6d6f40) entry/exit: the native receipt.
    void Transfer(GunEventLog&,const GunWeapon&,GunCopy,const GunCopyState& before,const GunCopyState& after,std::string_view path)noexcept;
    // Prediction rollback hook.
    void Restore(GunEventLog&,const GunWeapon&,GunCopy,const GunCopyState& before,const GunCopyState& after,bool matched)noexcept;
    // Transition commit 0x6dd6a0: a row only when the state moved.
    void Commit(GunEventLog&,const GunWeapon&,GunCopy,const GunCopyState& before,const GunCopyState& after)noexcept;
    // Update hook: whether this call ran with the context+0x18 delta override.
    // Edge-triggered: one freeze_start when holds begin, one freeze_stop when
    // the first un-held Update follows (reason cancel when the copy left the
    // reload states, else release).
    void Freeze(GunEventLog&,const GunWeapon&,GunCopy,bool applied,const GunCopyState& after)noexcept;
    // Update hook entry/exit with the entry context's fire bit and emit flag.
    // Dry fire (static RE, docs/re BC2_DISASM_PUMP_AND_DRY_FIRE.md section 2):
    // a rising fire bit while the copy is ready (2/2) with loaded 0 and the
    // copy still ready and empty after the Update. Pushes `fire` with result
    // "dry" and native_click (reserve 0: the game plays its own out-of-ammo
    // click; reserve > 0 with auto-reload held off: silent natively).
    void Trigger(GunEventLog&,const GunWeapon&,GunCopy,const GunCopyState& before,const GunCopyState& after,bool fire,bool emit)noexcept;
    std::uint64_t DryFires()const noexcept{return dryFires_.load(std::memory_order_relaxed);}
    // Steady-clock ns of the first last-round slide lock (0 = none yet); the
    // test hitch (FVR_BC2_TEST_HITCH_MS) is timed from it.
    std::int64_t LastRoundLockNs()const noexcept{return lastRoundLockNs_.load(std::memory_order_acquire);}
    // Step hook empty-ammo override (context+0x28) applied for this copy.
    void AutoReloadSuppressed(GunEventLog&,const GunWeapon&,GunCopy,const GunCopyState& before)noexcept;
    // Every 30 ticks from the gather: one native sample per known copy, from
    // the last observed values only (no new read). stale when older than staleUs.
    void Sample(GunEventLog&,std::string_view magPhase,std::int64_t staleUs)noexcept;
    // Drain thread after hooks are disabled: close open freezes with `reason`.
    void CloseFreezes(GunEventLog&,std::string_view reason)noexcept;
    std::uint64_t Busy()const noexcept{return busy_.load(std::memory_order_relaxed);}
    // Last observed values of one copy (no native read); false when unknown or busy.
    bool Last(GunCopy,GunCopyState&)noexcept;
private:
    struct Copy {
        std::atomic_flag lock=ATOMIC_FLAG_INIT;bool known=false,frozen=false,pumpHold=false,slideLocked=false,lastRound=false,fireHeld=false;GunWeapon weapon{};GunCopyState last{};
        std::int64_t seenUs=0,frozenUs=0;std::uint32_t shots=0,transfers=0;
    };
    struct Guard {std::atomic_flag& f;bool held;explicit Guard(std::atomic_flag& x)noexcept:f(x),held(!x.test_and_set(std::memory_order_acquire)){}
        ~Guard(){if(held)f.clear(std::memory_order_release);}};
    static void Baseline(Copy&,const GunWeapon&,const GunCopyState&)noexcept;
    std::array<Copy,3> copies_{};std::atomic<std::uint64_t> busy_{0},dryFires_{0};std::atomic<std::int64_t> lastRoundLockNs_{0};
};
// The DLL's single native tracker (fed by Bc2ReloadFlowRuntime hooks, sampled by the gather).
GunCopyTracker& GunNativeTracker()noexcept;

// fvr rows from the consumers' existing probe state, read once per gather on
// the game thread. Mirrors tools/sim/Bc2ReplaySim.cpp Sim::Phase so live and
// sim rows have the same shape and order.
std::string_view GunMagPhaseName(interaction::DetachableMagazinePhase)noexcept;
std::string_view GunMagReasonName(interaction::DetachableMagazineReason)noexcept;
struct GunMagazineProbe {
    interaction::DetachableMagazinePhase phase=interaction::DetachableMagazinePhase::Attached;
    interaction::DetachableMagazineReason reason=interaction::DetachableMagazineReason::None;
    std::uint64_t cycle=0;unsigned originalReturns=0;
};
// Shell (SPAS) consumer: Bc2PhysicalReload::ProbeState fields.
struct GunShellProbe {bool active=false,held=false;unsigned submitted=0,completed=0;std::uint64_t cycle=0;};
// Bc2MagazinePhysicalReload::Chamber() (MagazineChamberView), the VR-side model.
struct GunChamberProbe {bool known=false,magazineOut=false,chamberLoaded=false,needsRack=false;int loaded=-1,display=-1;
    bool operator==(const GunChamberProbe&)const=default;};
// KF2 VR's weapon readout label for the VR chamber model (VRSpatialHUD.uc
// 938-939): magazine out shows CHAMBERED or MAGAZINE OUT, a seated magazine
// that still owes a rack shows RACK, anything else (or unknown) has no label.
// The count beside it is GunChamberProbe::display (MagazineFeedState::DisplayAmmo).
constexpr std::string_view MagazineDisplayLabel(const GunChamberProbe& c)noexcept {
    if(!c.known)return {};
    if(c.magazineOut)return c.chamberLoaded?"CHAMBERED":"MAGAZINE OUT";
    return c.needsRack?"RACK":std::string_view{};
}
// Wire names for interaction::FeedbackKind ("reload_capture", "reload_applied", "reload_seated").
std::string_view GunFeedbackKindName(interaction::FeedbackKind)noexcept;
class GunFvrTracker {
public:
    // Detachable-magazine consumer (Bc2MagazinePhysicalReload::ProbeState).
    void Magazine(GunEventLog&,const GunWeapon&,const GunMagazineProbe&)noexcept;
    // Shell consumer: begin/pouch_start/cancel/complete/end from its probe state.
    void Shell(GunEventLog&,const GunWeapon&,const GunShellProbe&)noexcept;
    // Consumer API wrappers: a submitted seat request and a consumed native receipt.
    void Seat(GunEventLog&,const GunWeapon&,std::uint64_t cycle,int credits)noexcept;
    void Credit(GunEventLog&,const GunWeapon&,std::uint64_t cycle,int loaded,int reserve)noexcept;
    // Hand claim snapshot per hand: kind (0 = none) and token id.
    void Claims(GunEventLog&,const GunWeapon&,const std::array<std::uint8_t,2>& kinds,const std::array<std::uint64_t,2>& tokens)noexcept;
    void Haptic(GunEventLog&,const GunWeapon&,std::string_view kind,unsigned hand,bool sent)noexcept;
    // Slider release-to-finish began: the hand let go and the magazine settles into the well.
    // `to` is "well" for a magazine, "port" for a shell released on insert.
    void Settle(GunEventLog&,const GunWeapon&,std::string_view to="well")noexcept;
    // A charging-handle stroke cleared NeedsRack.
    void Rack(GunEventLog&,const GunWeapon&,bool byGrip)noexcept;
    // Chamber model changes (eject, seat, rack, reset, observe); unchanged views write nothing.
    void Chamber(GunEventLog&,const GunWeapon&,const GunChamberProbe&)noexcept;
    // M6 dropped magazine prop: event drop/land/expire/recycle, prop id, world
    // position and floor height in millimetres. Presentation only.
    void Prop(GunEventLog&,const GunWeapon&,std::string_view event,std::uint64_t id,
        const std::array<float,3>& position,float floorY)noexcept;
    // Row builders shared with tools/sim (which stamps tick/qpc itself and
    // formats with AppendGunEventLine(...,sim=true)). They update tracker state
    // exactly like the pushing entry points above.
    GunEvent HapticRow(const GunWeapon&,std::string_view kind,unsigned hand,bool sent)const noexcept;
    GunEvent SettleRow(const GunWeapon&,std::string_view to="well")const noexcept;
    GunEvent RackRow(const GunWeapon&,bool byGrip)const noexcept;
    std::optional<GunEvent> ChamberRow(const GunWeapon&,const GunChamberProbe&)noexcept;
    void Tape(GunEventLog&,const GunWeapon&,std::string_view reason,int met=-1,int loaded=-1,int reserve=-1,std::string_view name={})noexcept;
    // Every 30 ticks while a session is active.
    void Sample(GunEventLog&,const GunWeapon&)noexcept;
    bool SessionActive()const noexcept{return magSession_||shellSession_;}
    std::string_view MagPhase()const noexcept{return GunMagPhaseName(magPhase_);}
private:
    GunEvent Row(GunEventPhase,const GunWeapon&)const noexcept;
    interaction::DetachableMagazinePhase magPhase_=interaction::DetachableMagazinePhase::Attached;
    bool magSession_=false,shellSession_=false;GunShellProbe shell_{};unsigned shellBegunCompleted_=0;
    std::array<std::uint8_t,2> claimKinds_{};std::array<std::uint64_t,2> claimTokens_{};
    GunChamberProbe chamber_{};GunWeapon chamberWeapon_{};
};
}
