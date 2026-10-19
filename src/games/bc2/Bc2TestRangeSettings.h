#pragma once
#include "Bc2TestRangeFields.h"
#include "fvr/runtime/TestRange.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace fvr::bc2 {
// Registry route (docs/BC2_TEST_RANGE.md §4.2): exact, reversible writes of the
// range's settings pairs into the live Server/Client/Game settings singletons,
// before the engine reads them. Portable planning and apply/restore over a
// memory accessor; the in-process boot stage supplies real memory.
//
// Fail closed: one unresolved singleton, unmapped key, type mismatch or bad
// value refuses the whole batch before any byte is written. String fields
// (Level, SpawnPoint, SavePoint) are DEFERRED until the 12-byte Frostbite
// String layout is reversed; they are reported, never written.
struct RangeSettingsInstances {std::uint32_t server=0,client=0,game=0;}; // live object addresses, 0 = unresolved
enum class RangeSettingStatus:std::uint8_t {Planned,DeferredString,Unmapped,Unresolved,InvalidValue};
std::string_view RangeSettingStatusName(RangeSettingStatus)noexcept;
struct RangeSettingWrite {
    std::string key;RangeSettingStatus status=RangeSettingStatus::Unmapped;
    std::uint32_t object=0,typeInfo=0,address=0,bytes=0;std::array<std::byte,4> value{};
};
std::vector<RangeSettingWrite> PlanRangeSettingWrites(std::span<const runtime::RangeSetting>,const RangeSettingsInstances&);
// True when every entry is Planned or DeferredString and at least one is Planned.
bool RangeSettingPlanWritable(std::span<const RangeSettingWrite>)noexcept;
struct RangeMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t address,void* out,std::size_t bytes)=nullptr;
    bool (*write)(void*,std::uint32_t address,const void* in,std::size_t bytes)=nullptr;
    // Live reflection identity of `object` (exact TypeInfo VA); required.
    bool (*isType)(void*,std::uint32_t object,std::uint32_t typeInfo)=nullptr;
};
class RangeSettingsWriter {
public:
    // All-or-nothing: verifies types, records originals, writes, reads back.
    // Any failure rolls back what this call wrote and returns false.
    bool Apply(std::span<const RangeSettingWrite>,const RangeMemory&);
    // Restores each byte range still holding the value written; a range the
    // engine has since changed is left alone and makes the result false.
    bool Restore(const RangeMemory&);
    std::size_t Applied()const noexcept{return applied_.size();}
    std::size_t Deferred()const noexcept{return deferred_;}
    std::size_t Conflicts()const noexcept{return conflicts_;}
private:
    struct Entry {std::uint32_t address=0,bytes=0;std::array<std::byte,4> before{},written{};};
    std::vector<Entry> applied_;std::size_t deferred_=0,conflicts_=0;bool used_=false;
};
}
