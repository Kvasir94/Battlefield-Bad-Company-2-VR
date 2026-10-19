#pragma once
#include "Bc2EquipmentIdentity.h"
#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace fvr::bc2 {
struct NativeSkinOwner {
    std::uint64_t actor=0,weak=0,weapon=0,ownerGeneration=0,space=0,equipmentGeneration=0;
    WeaponEquipmentIdentity equipment{};
    bool operator==(const NativeSkinOwner&)const=default;
};
inline bool NativeSkinOwnerValid(const NativeSkinOwner& owner)noexcept {
    return owner.actor&&owner.weak&&owner.weapon&&owner.ownerGeneration&&owner.space&&owner.equipmentGeneration&&
        owner.equipment.weapon==owner.weapon&&owner.equipment.data&&!owner.equipment.Asset().empty();
}
struct NativeSkinRequestRecord {
    std::uint64_t request=0,size=0,token=0;
    NativeSkinOwner owner{};
    bool operator==(const NativeSkinRequestRecord&)const=default;
};
// The native allocator must not reuse an allocation while it is actively in use.
// Call once after EVERY original allocator return, not only character requests.
// Mutex ordering cannot repair native overlapping allocations that return out of
// order. Tokens identify allocations, not frame/eye leases or queue retirement.
class Bc2NativeSkinRequestRegistry {
public:
    static constexpr unsigned Slots=256;
    static constexpr std::uint64_t CharacterRequestSize=0xd0;
    using Token=std::uint64_t;
    // selectedOwner is supplied only after checking the known producer caller,
    // row and unchanged current owner before/after the native allocation.
    std::optional<Token> RecordAllocation(std::uint64_t address,std::uint64_t size,
        std::optional<NativeSkinOwner> selectedOwner={})noexcept {
        std::lock_guard lock(mutex_);
        if(!address||!size||size>std::numeric_limits<std::uint64_t>::max()-address){ClearLocked();return {};}
        const auto end=address+size;
        for(auto& record:records_)if(record.token&&address<record.request+record.size&&record.request<end)record={};
        if(serial_==std::numeric_limits<Token>::max()){ClearLocked();return {};}
        const auto token=++serial_;
        if(size!=CharacterRequestSize||!selectedOwner||!NativeSkinOwnerValid(*selectedOwner))return {};
        // Prefer an unused slot; eviction only drops proof, never transfers it.
        unsigned slot=next_;
        for(unsigned n=0;n<Slots;++n){const auto candidate=(next_+n)%Slots;
            if(!records_[candidate].token){slot=candidate;break;}}
        records_[slot]={address,size,token,*selectedOwner};next_=(slot+1)%Slots;
        return token;
    }
    std::optional<NativeSkinRequestRecord> Read(std::uint64_t request,const NativeSkinOwner& currentOwner)const noexcept {
        std::lock_guard lock(mutex_);
        if(!NativeSkinOwnerValid(currentOwner))return {};
        for(const auto& record:records_)if(record.token&&record.request==request&&record.owner==currentOwner)return record;
        return {};
    }
    // Recheck after reading native request/descriptor fields to detect reuse or
    // owner replacement since Read. The caller also revalidates live ownership.
    bool Matches(const NativeSkinRequestRecord& expected,const NativeSkinOwner& currentOwner)const noexcept {
        std::lock_guard lock(mutex_);
        if(!expected.token||expected.owner!=currentOwner||!NativeSkinOwnerValid(currentOwner))return false;
        for(const auto& record:records_)if(record.token==expected.token)return record==expected;
        return false;
    }
    void Erase(Token token)noexcept {
        std::lock_guard lock(mutex_);
        for(auto& record:records_)if(record.token==token)record={};
    }
    void Clear()noexcept {std::lock_guard lock(mutex_);ClearLocked();}
private:
    void ClearLocked()noexcept {records_={};next_=0;} // Never recycle tokens.
    std::array<NativeSkinRequestRecord,Slots> records_{};
    unsigned next_=0;Token serial_=0;
    mutable std::mutex mutex_;
};
}
