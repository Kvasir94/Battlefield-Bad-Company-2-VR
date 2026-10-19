#pragma once
#include "fvr/graphics/SharedTextureProtocol.h"
#include <d3d11.h>
#include <memory>

namespace fvr::graphics {
enum class TransferResult { Ok, Busy, Invalid, Unavailable, Faulted };
enum class CopyPollResult { Pending, Complete, Invalid, Unavailable, Faulted, TimedOut };
struct TextureSlice { ID3D11Texture2D* texture=nullptr; UINT subresource=0; };
// One producer and one consumer, each called from its device's immediate-context
// thread. Caller serializes all context use. No game offsets or OpenXR dependency.
// Backpressure drops a whole pair with Busy; neither side blocks on mutexes.
// A fault requires recreation with a NEW descriptor/session (never retry its keys).
class D3D11PairProducer {
public:
    D3D11PairProducer();~D3D11PairProducer();
    D3D11PairProducer(const D3D11PairProducer&)=delete;
    D3D11PairProducer& operator=(const D3D11PairProducer&)=delete;
    TransferResult Create(ID3D11Device*,UINT width,UINT height,DXGI_FORMAT,std::uint64_t epoch,TextureSharing sharing=TextureSharing::NamedKeyed) noexcept;
    TransferResult Publish(const std::array<TextureSlice,2>&,const FrameMetadata&,PairTicket&) noexcept;
    void Acknowledge(const PairTicket&,bool consumed)noexcept;
    // The consumer discarded this pair (PairConsumed false). Hands the keyed-mutex
    // key back (or clears the legacy ack wait) so the SAME textures, descriptor
    // and generation are reused: the host reopens nothing. False means the pair
    // cannot be reused (stale ticket, fault, FVR_PAIR_REUSE=0) and the caller must
    // Reset() it. True with the mutex still held by the consumer completes inside
    // Publish, which reports Busy until then and Faulted past ~250 ms.
    bool Discard(const PairTicket&)noexcept;
    TextureDescriptor Descriptor() const noexcept;
    HRESULT LastError() const noexcept;
    unsigned LastOperation() const noexcept{return operation_;}
    void Reset() noexcept;
private: struct State;std::unique_ptr<State> state_;HRESULT error_=S_OK;unsigned operation_=0;
};
class D3D11PairConsumer {
public:
    D3D11PairConsumer();~D3D11PairConsumer();
    D3D11PairConsumer(const D3D11PairConsumer&)=delete;
    D3D11PairConsumer& operator=(const D3D11PairConsumer&)=delete;
    TransferResult Open(ID3D11Device*,const TextureDescriptor&) noexcept;
    // Ok means both copies were queued, NOT that the producer can reuse their
    // sources. Poll the exact ticket until Complete before positive feedback.
    // One transfer may be pending; the source/destination references stay owned
    // until completion or Reset. All queued GPU work uses this device's context.
    TransferResult QueueCopy(const PairTicket&,const std::array<TextureSlice,2>&) noexcept;
    // Checks the event once with DONOTFLUSH. Pending never acknowledges a pair.
    // An event still incomplete after 20 ms is only COUNTED (LateCopies) and stays
    // Pending: GPU contention routinely delays a copy past a frame. A removed
    // device (Faulted) or an event incomplete after ~250 ms (TimedOut) retires the
    // generation; negative feedback must then not reuse its pixels. Time is QPC,
    // not GetTickCount64 (a 15.6 ms tick made 20 ms anything from 15.6 to 31.3).
    CopyPollResult PollCopy(const PairTicket&) noexcept;
    bool CopyPending() const noexcept;
    // Copies that stayed incomplete past 20 ms, counted once each (cumulative).
    std::uint64_t LateCopies() const noexcept{return lateCopies_;}
    // After PollCopy returned TimedOut (until Reset/Open), the abandoned copy's
    // GPU event, so a caller can OBSERVE its eventual completion for telemetry.
    // Never use it to acknowledge: the generation was already retired. Else null.
    ID3D11Query* TimedOutCompletion() const noexcept;
    // Copies both eyes into caller-owned resources (e.g. acquired XR slices).
    // On failure their contents must not be submitted. No resource is exposed
    // while another process owns it. Ticket is supplied over reliable IPC.
    // LegacyFenced retains its bounded synchronous completion wait; NamedKeyed
    // retains its existing GPU mutex handoff without a CPU completion wait.
    TransferResult Copy(const PairTicket&,const std::array<TextureSlice,2>&) noexcept;
    HRESULT LastError() const noexcept;
    void Reset() noexcept;
private: struct State;std::unique_ptr<State> state_;HRESULT error_=S_OK;std::uint64_t lateCopies_=0;
};
// Test seam only (production never sets it): a ticket for which the hook
// returns true reads as an incomplete GPU event past its budget, i.e. PollCopy
// reports TimedOut while the real queued copy still runs. Pass nullptr to clear.
using CopyLateTestHook=bool(*)(const PairTicket&);
void SetCopyLateTestHook(CopyLateTestHook) noexcept;
}
