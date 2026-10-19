#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

namespace fvr::bc2 {
// Event mailbox (design step 3, FVR_BC2_RELOAD_MAILBOX). The gather thread
// decides once per event and publishes one board; the Update/Step hooks only
// read it, find their own copy by firing address, apply at most one override
// (exact write/restore as today) and push a receipt. No lease, request lock or
// reserve read inside a hook. Portable: no native reads or writes here.
enum class ReloadCommandKind : std::uint8_t {None,Hold,AdvanceOnce,Observe};
struct ReloadCopyCommand {
    ReloadCommandKind kind=ReloadCommandKind::None;
    std::uint64_t cycle=0,serial=0; // serial: AdvanceOnce consumed once per copy
    float advanceSeconds=0;
    std::int64_t expiresNs=0;      // Hold/AdvanceOnce only
};
struct ReloadEmptyPolicy {
    bool block=false;               // never expires: cleared only by a new owner or a token
    std::uint64_t tokenSerial=0;std::int64_t tokenExpiresNs=0;
};
struct ReloadBoardOwner {
    std::uint64_t key=0;            // owner identity (player/soldier/weapon/generations), 0 = none
    std::array<std::uint32_t,3> firing{};
    std::uint32_t firingData=0,ammoAddress=0; // hook-side structural check of the copy
};
struct ReloadCommandBoardState {
    ReloadBoardOwner owner{};
    std::array<ReloadCopyCommand,3> copies{};
    ReloadEmptyPolicy empty{};
    std::uint64_t publication=0;
};
// What one hook invocation may do for its own copy, after expiry polarity:
// Hold expires to release (native continues and credits, as today);
// AdvanceOnce expires to dropped (native timer finishes a little later);
// the empty block never expires (fail-closed); an unknown firing is native.
struct ReloadHookCommand {
    unsigned branch=3;ReloadCommandKind kind=ReloadCommandKind::None;
    std::uint64_t cycle=0,serial=0;float advanceSeconds=0;
    bool blockEmpty=false,tokenOpen=false;
};
inline ReloadHookCommand ResolveReloadCommand(const ReloadCommandBoardState& b,std::uint32_t firing,std::int64_t now)noexcept {
    ReloadHookCommand out;
    if(!b.owner.key||!firing)return out;
    for(unsigned n=0;n<3;++n)if(b.owner.firing[n]==firing){out.branch=n;break;}
    if(out.branch>=3)return out;
    const auto& c=b.copies[out.branch];
    const bool live=c.kind!=ReloadCommandKind::None&&now>=0&&now<c.expiresNs;
    if(live&&c.kind!=ReloadCommandKind::Observe){out.kind=c.kind;out.cycle=c.cycle;out.serial=c.serial;out.advanceSeconds=c.advanceSeconds;}
    else if(c.kind==ReloadCommandKind::Observe)out.kind=ReloadCommandKind::Observe;
    out.blockEmpty=b.empty.block;
    out.tokenOpen=b.empty.tokenSerial&&now<b.empty.tokenExpiresNs;
    return out;
}
struct ReloadCommandReceipt {
    std::uint64_t publication=0,serial=0,cycle=0;
    std::int64_t nowNs=0;
    unsigned branch=3;ReloadCommandKind kind=ReloadCommandKind::None;
    bool applied=false,restored=false;
    unsigned stateBefore=0,stateAfter=0;std::int32_t loadedBefore=-1,loadedAfter=-1;
};
// Single-writer seqlocked value for any trivially copyable T.
template<class T> class SeqValue {
public:
    void Publish(const T& v)noexcept {const auto s=sequence_.load(std::memory_order_relaxed);
        sequence_.store(s+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);
        value_=v;sequence_.store(s+2,std::memory_order_release);}
    std::optional<T> Read()const noexcept {
        for(unsigned n=0;n<8;++n){const auto s=sequence_.load(std::memory_order_acquire);if(s&1u)continue;
            T c=value_;std::atomic_thread_fence(std::memory_order_acquire);
            if(sequence_.load(std::memory_order_relaxed)==s)return c;}
        return {};}
private:
    std::atomic<unsigned> sequence_{0};T value_{};
};
// Lock-free multi-producer ring drained by one reader, in push order.
template<class T,unsigned N> class ReceiptRing {
public:
    void Push(const T& v)noexcept {const auto n=next_.fetch_add(1,std::memory_order_acq_rel);auto& slot=slots_[n%N];
        slot.ticket.store(0,std::memory_order_release);slot.value=v;slot.ticket.store(n+1,std::memory_order_release);}
    template<class F> unsigned Drain(std::uint64_t& cursor,F&& take,unsigned* lost=nullptr)const noexcept {
        const auto end=next_.load(std::memory_order_acquire);
        if(end>cursor+N){if(lost)*lost+=unsigned(end-N-cursor);cursor=end-N;}
        unsigned count=0;
        while(cursor<end){const auto& slot=slots_[cursor%N];
            if(slot.ticket.load(std::memory_order_acquire)!=cursor+1)break;
            T v=slot.value;if(slot.ticket.load(std::memory_order_acquire)!=cursor+1)break;
            take(v);++cursor;++count;}
        return count;}
private:
    struct Slot {std::atomic<std::uint64_t> ticket{0};T value{};};
    std::atomic<std::uint64_t> next_{0};std::array<Slot,N> slots_{};
};
class ReloadCommandBoard {
public:
    static constexpr unsigned ReceiptSlots=64;
    // Single writer (the gather thread).
    void Publish(ReloadCommandBoardState next)noexcept {
        const auto s=sequence_.load(std::memory_order_relaxed);
        next.publication=++publications_;
        sequence_.store(s+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);
        state_=next;
        sequence_.store(s+2,std::memory_order_release);
    }
    // Any thread; nullopt only under sustained writer contention.
    std::optional<ReloadCommandBoardState> Read()const noexcept {
        for(unsigned n=0;n<8;++n){const auto s=sequence_.load(std::memory_order_acquire);
            if(s&1u)continue;
            ReloadCommandBoardState c=state_;std::atomic_thread_fence(std::memory_order_acquire);
            if(sequence_.load(std::memory_order_relaxed)==s)return c;}
        return {};
    }
    // AdvanceOnce runs exactly once per copy and serial, across threads.
    bool ConsumeAdvance(unsigned branch,std::uint64_t serial)noexcept {
        if(branch>=3||!serial)return false;
        auto seen=consumed_[branch].load(std::memory_order_acquire);
        while(seen<serial)if(consumed_[branch].compare_exchange_weak(seen,serial,std::memory_order_acq_rel))return true;
        return false;
    }
    // Hooks push; the gather thread drains in order. A slow reader that falls
    // more than ReceiptSlots behind loses the oldest receipts (counted).
    void PushReceipt(const ReloadCommandReceipt& r)noexcept {
        if(r.branch>=3)return;auto& ring=rings_[r.branch];
        const auto n=ring.next.fetch_add(1,std::memory_order_acq_rel);auto& slot=ring.slots[n%ReceiptSlots];
        slot.ticket.store(0,std::memory_order_release);slot.value=r;slot.ticket.store(n+1,std::memory_order_release);
    }
    unsigned Drain(unsigned branch,std::span<ReloadCommandReceipt> out,std::uint64_t& cursor,unsigned* lost=nullptr)const noexcept {
        if(branch>=3)return 0;const auto& ring=rings_[branch];const auto end=ring.next.load(std::memory_order_acquire);
        if(end>cursor+ReceiptSlots){if(lost)*lost+=unsigned(end-ReceiptSlots-cursor);cursor=end-ReceiptSlots;}
        unsigned count=0;
        while(cursor<end&&count<out.size()){const auto& slot=ring.slots[cursor%ReceiptSlots];
            if(slot.ticket.load(std::memory_order_acquire)!=cursor+1)break; // still being written
            const auto v=slot.value;if(slot.ticket.load(std::memory_order_acquire)!=cursor+1)break;
            out[count++]=v;++cursor;}
        return count;
    }
private:
    std::atomic<unsigned> sequence_{0};std::uint64_t publications_=0;
    ReloadCommandBoardState state_{};
    std::array<std::atomic<std::uint64_t>,3> consumed_{};
    struct Slot {std::atomic<std::uint64_t> ticket{0};ReloadCommandReceipt value{};};
    struct Ring {std::atomic<std::uint64_t> next{0};std::array<Slot,ReceiptSlots> slots{};};
    std::array<Ring,3> rings_{};
};
}
