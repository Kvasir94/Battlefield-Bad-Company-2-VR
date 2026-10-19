#pragma once
// Bounded, rearmable stereo-pair diagnostic (FVR_BC2_STEREO_CAPTURE). CPU-only
// state machine and serialization; the D3D11 glue lives in Bc2StereoCaptureGpu.
//
// Read-only on the game: view memory is copied as raw bytes through a caller
// supplied reader. The pointer fields at view+0x1540/+0x1548 are recorded as
// raw values and never dereferenced. Nothing here writes native memory.
//
// Evidence target: left-eye-only ghosted building lines after explosions and
// per-eye damaged-edge mismatch (docs/HANDOFF-2026-10-08-LATE.md, "Rendering
// findings and next diagnostic"). This records; it does not decide a cause.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace fvr::bc2::stereoCapture {

inline constexpr unsigned PairsPerArm=4;          // consecutive complete pairs per arm
inline constexpr unsigned MaxAttemptsPerArm=8;    // pair attempts (complete or not) per arm
inline constexpr unsigned Points=3;               // before visibility, before Draw, after Draw
inline constexpr unsigned EffectBegin=0x1530,EffectEnd=0x1670; // [begin,end): covers +0x1530..+0x1660 inclusive
inline constexpr unsigned EffectBytes=EffectEnd-EffectBegin;
inline constexpr unsigned PointerFieldA=0x1540,PointerFieldB=0x1548; // raw values only
inline constexpr unsigned CameraBlocks=4,CameraBytes=0x460;
inline constexpr unsigned ViewRecordBytes=EffectBytes+CameraBlocks*CameraBytes; // views.bin record
inline constexpr unsigned HeadTransitions=2048,TailTransitions=4096; // per eye: first + last rows
inline constexpr unsigned MaxColorInFlight=8;      // hard cap on staging textures in flight
inline constexpr unsigned ColorMaxDimension=512;   // reduced eye color: both sides <= this
inline constexpr unsigned ColorPollLimit=600;      // nonblocking polls before a readback times out
inline constexpr std::uint64_t ArmByteBudget=32ull<<20; // enforced per arm by the writer
inline constexpr unsigned DefaultMaxArms=6,HardMaxArms=16,DefaultAutoArms=1,MaxAutoArms=4;
inline constexpr unsigned AutoStableFrames=120;    // signal must be stable this long before a change counts
// Auto signal: the Draw-consumed effect block (slot 56 returns +0x15d0; Draw
// tests +0x25 and forwards +0,+4,+0x10,+0x20,+0x24) and getters at +0x1640/+0x1644.
inline constexpr unsigned AutoBlockBegin=0x15d0,AutoBlockBytes=0x28,AutoTailBegin=0x1640,AutoTailBytes=8;
inline constexpr unsigned AutoSignalBytes=AutoBlockBytes+AutoTailBytes;

enum class Point:unsigned {BeforeVisibility,BeforeDraw,AfterDraw};
enum class ArmSource:unsigned {None,Startup,File,Auto};
enum class State:unsigned {Disabled,Idle,Capturing,Draining,Ready,Writing};
enum class ArmStatus:unsigned {None,Complete,Exhausted,SessionEnded};
enum class DrawKind:unsigned {Draw,DrawIndexed,DrawInstanced,DrawIndexedInstanced};
enum class Category:unsigned {Viewport,VertexShader,PixelShader,GeometryShader,RenderTarget,DepthStencilView,
    PixelResource,DepthStencilState,BlendState,VertexConstants,PixelConstants,Count};
enum class ColorStatus:unsigned {None,Pending,Complete,Failed,Unsupported,InFlightCap,Timeout,Unresolved};

const char* Name(Point)noexcept;const char* Name(ArmSource)noexcept;const char* Name(State)noexcept;
const char* Name(ArmStatus)noexcept;const char* Name(DrawKind)noexcept;const char* Name(Category)noexcept;
const char* Name(ColorStatus)noexcept;

// Env: FVR_BC2_STEREO_CAPTURE unset/0 = off (kill switch), 1 = on + armed at
// startup, 2 = on but armed only by trigger file/auto signal.
// FVR_BC2_STEREO_CAPTURE_AUTO = auto arms (default 1, 0..4).
// FVR_BC2_STEREO_CAPTURE_MAX_ARMS = arms per session (default 6, 1..16).
struct Config {bool enabled=false,armAtStartup=false;unsigned autoArms=0,maxArms=DefaultMaxArms;};
Config ParseConfig(const char* capture,const char* autoArms,const char* maxArms)noexcept;

// Exact identity of one tracked stereo transaction.
struct Key {
    std::uint64_t leaseChannel=0,leaseRequest=0,trackingGeneration=0;
    std::uint32_t world=0,request=0,nativeFrame=0;
    bool operator==(const Key&)const=default;
};
using ReadFn=bool(*)(std::uintptr_t address,void* out,std::size_t bytes)noexcept;

struct ViewSnapshot {
    bool valid=false;std::uint32_t view=0,readFailures=0;std::int64_t nowNs=0;
    std::array<std::uint32_t,CameraBlocks> cameraOffsets{};
    std::array<std::byte,EffectBytes> effect{};
    std::array<std::array<std::byte,CameraBytes>,CameraBlocks> cameras{};
};

// Pipeline state at one draw. Interface identities are raw (x86) pointers.
struct Surface {
    std::uint32_t view=0,resource=0,width=0,height=0,format=0,samples=0,mip=0,dimension=0;
    bool operator==(const Surface&)const=default;
};
struct Viewport {float x=0,y=0,width=0,height=0,minDepth=0,maxDepth=0;};
struct PipelineState {
    std::uint32_t viewportCount=0;std::array<Viewport,4> viewports{};
    std::uint32_t vs=0,ps=0,gs=0;
    std::array<Surface,4> rtv{};Surface dsv{};
    std::array<Surface,16> psSrv{};
    std::uint32_t depthState=0,stencilRef=0,depthBits=0;
    std::uint32_t blendState=0,blendBits=0,sampleMask=0,writeMask=0;std::array<float,4> blendFactor{};
    std::array<std::uint32_t,8> vsCb{},vsCbBytes{},psCb{},psCbBytes{};
};
struct Transition {
    std::uint32_t sequence=0,draw=0;std::uint8_t category=0,slot=0,kind=0,initial=0;
    std::array<std::uint32_t,8> values{};
};
// Ordered state changes observed at draws. No resolution filter and no
// deduplication: A->B->A yields every row. Keeps the first HeadTransitions
// rows and the last TailTransitions rows (post-processing is at the end).
class EyeTransitions {
public:
    void Reset()noexcept;
    void Reserve();
    void Observe(const PipelineState&,DrawKind)noexcept;
    const PipelineState* Last()const noexcept{return hasLast_?&last_:nullptr;}
    std::uint32_t Draws()const noexcept{return draws_;}
    std::uint32_t KindCount(DrawKind k)const noexcept{return kinds_[unsigned(k)];}
    std::uint64_t Rows()const noexcept{return sequence_;}
    std::uint64_t Dropped()const noexcept{return sequence_>head_.size()+tailCount_?sequence_-head_.size()-tailCount_:0;}
    template<class F> void ForEach(F&& f)const{
        for(const auto& row:head_)f(row);
        const auto start=tailCount_<TailTransitions?0u:tailNext_;
        for(unsigned i=0;i<tailCount_;++i)f(tail_[(start+i)%TailTransitions]);
    }
private:
    void Emit(Category,unsigned slot,std::initializer_list<std::uint32_t>,DrawKind,bool initial)noexcept;
    PipelineState last_{};bool hasLast_=false;
    std::uint32_t draws_=0,sequence_=0;std::array<std::uint32_t,4> kinds_{};
    std::vector<Transition> head_,tail_;unsigned tailNext_=0,tailCount_=0;
};

struct ColorResult {
    ColorStatus status=ColorStatus::None;long error=0;
    std::uint32_t sourceWidth=0,sourceHeight=0,sourceFormat=0,sourceSamples=0;
    std::uint32_t mip=0,width=0,height=0,format=0,bytesPerPixel=0,polls=0;
    std::vector<std::uint8_t> pixels; // tightly packed rows, top-down
};
struct EyeCapture {
    std::uint32_t view=0;bool drawn=false;
    std::array<ViewSnapshot,Points> points{};
    EyeTransitions transitions;
    ColorResult color;
};
struct PairCapture {
    Key key{};std::atomic<bool> open=false;bool published=false,complete=false;
    std::uint32_t outcomeBits=0,sequence=0;std::int64_t beginNs=0,finishNs=0;
    std::array<EyeCapture,2> eyes;
};
// Outcome bits recorded from FinishTrackedFrame (diagnostic, not a verdict).
enum OutcomeBit:std::uint32_t {CompletePair=1,Restored=2,CapturesOkay=4,VisibilityBoth=8,Published=16,Superseded=32,SessionEnd=64};

struct Telemetry {State state=State::Disabled;unsigned armsStarted=0,armsWritten=0,pending=0,attempts=0,consecutive=0,
    autoRemaining=0,autoSignalChanges=0,autoArmed=0,rejectedArms=0;std::uint64_t bytesWritten=0;};

class Session {
public:
    Session();~Session();
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    // Setup thread, before any callback.
    bool Configure(const Config&);
    bool Enabled()const noexcept{return enabled_;}
    const Config& Configuration()const noexcept{return config_;}
    // Any thread. Coalesces: at most one pending arm. False if disabled or arms exhausted.
    bool RequestArm(ArmSource,std::uint64_t detail=0)noexcept;
    // Update thread, after a tracked lease began (serialized with FinishPair by the tracked state).
    bool BeginPair(const Key&,std::int64_t nowNs)noexcept;
    // Visibility/graphics threads. Null unless capturing this exact open pair.
    EyeCapture* Eye(const Key&,unsigned eye)noexcept;
    void CaptureView(const Key&,unsigned eye,Point,std::uint32_t view,const std::array<std::uint32_t,CameraBlocks>& cameraOffsets,ReadFn,std::int64_t nowNs)noexcept;
    // Graphics thread: the transaction for this lease request ended.
    void FinishPair(std::uint64_t leaseRequest,std::uint32_t outcomeBits,std::int64_t nowNs)noexcept;
    // Graphics thread: Draining -> Ready once no color readback is in flight.
    bool TryMarkReady(bool colorsResolved)noexcept;
    // Graphics thread, owned main view before Draw: optional auto-arm signal.
    bool ObserveAutoSignal(const std::array<std::byte,AutoSignalBytes>&)noexcept;
    // Probe thread: write a Ready arm. Returns true if an arm was written.
    bool WriteReady(const std::filesystem::path& root);
    // Probe thread, after all callbacks have quiesced: end a partial arm and write it.
    bool Finalize(const std::filesystem::path& root,bool callbacksQuiesced);
    // Probe/setup thread: rewrite manifest.json (status, bounds, written arms).
    void WriteManifest(const std::filesystem::path& root)const;
    State CurrentState()const noexcept{return state_.load(std::memory_order_acquire);}
    Telemetry ReadTelemetry()const noexcept;
    void WriteTelemetry(std::ostream&)const;
    // Test/inspection access (only valid while Ready/Writing or single-threaded).
    const PairCapture& Pair(unsigned attempt)const noexcept{return (*pairs_)[attempt];}
    unsigned Attempts()const noexcept{return attempts_.load(std::memory_order_acquire);}
    ArmStatus Status()const noexcept{return status_;}
private:
    void StartArm(ArmSource,std::uint64_t,std::int64_t)noexcept;
    void ClosePair(PairCapture&,std::uint32_t bits,std::int64_t nowNs)noexcept;
    bool WriteArm(const std::filesystem::path& root);
    Config config_{};bool enabled_=false;
    std::atomic<State> state_=State::Disabled;
    std::atomic<unsigned> pending_=0,pendingSource_=0,armsStarted_=0,armsWritten_=0,rejectedArms_=0;
    std::atomic<std::uint64_t> pendingDetail_=0,bytesWritten_=0;
    std::atomic<int> current_=-1;
    std::unique_ptr<std::array<PairCapture,MaxAttemptsPerArm>> pairs_;
    std::atomic<unsigned> attempts_=0,consecutive_=0;ArmStatus status_=ArmStatus::None;
    ArmSource armSource_=ArmSource::None;std::uint64_t armDetail_=0;std::int64_t armNs_=0,endNs_=0;
    // Auto signal (graphics thread only, except counters).
    std::array<std::byte,AutoSignalBytes> autoLast_{};bool autoSeen_=false;unsigned autoStable_=0;
    std::atomic<unsigned> autoRemaining_=0,autoChanges_=0,autoArmed_=0;
    struct WrittenFile {std::string name;std::uint64_t bytes=0,hash=0;};
    struct WrittenArm {unsigned index=0;std::string folder,status,source;std::uint64_t detail=0,bytes=0;unsigned attempts=0,complete=0;
        std::vector<WrittenFile> files;std::vector<std::string> skipped;};
    std::vector<WrittenArm> written_;
};

std::uint64_t Fnv1a(const void*,std::size_t)noexcept;
// One transitions.jsonl row (no newline).
void WriteTransition(std::ostream&,const Key&,unsigned eye,const Transition&);
}
