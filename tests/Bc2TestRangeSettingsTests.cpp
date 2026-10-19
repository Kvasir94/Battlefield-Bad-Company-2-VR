#include "Bc2TestRangeSettings.h"
#include "Test.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
using namespace fvr::bc2;
namespace {
constexpr std::uint32_t Server=0x02000000,Client=0x02001000,Game=0x02002000;
// Byte-addressed fake process memory with per-object reflection identity.
struct Memory {
    std::vector<std::byte> bytes=std::vector<std::byte>(0x3000);
    std::map<std::uint32_t,std::uint32_t> types{{Server,RangeServerSettingsType},{Client,RangeClientSettingsType},{Game,RangeGameSettingsType}};
    std::uint32_t failWriteAt=0,stickyAt=0;unsigned writes=0;
    bool In(std::uint32_t a,std::size_t n)const{return a>=Server&&a-Server+n<=bytes.size();}
    std::byte* At(std::uint32_t a){return bytes.data()+(a-Server);}
};
Memory* M(void* c){return static_cast<Memory*>(c);}
RangeMemory Access(Memory& m){
    RangeMemory r;r.context=&m;
    r.read=[](void* c,std::uint32_t a,void* out,std::size_t n){if(!M(c)->In(a,n))return false;std::memcpy(out,M(c)->At(a),n);return true;};
    r.write=[](void* c,std::uint32_t a,const void* in,std::size_t n){
        auto* m=M(c);if(!m->In(a,n)||a==m->failWriteAt)return false;++m->writes;
        if(a!=m->stickyAt)std::memcpy(m->At(a),in,n); // sticky: write "succeeds" but nothing changes
        return true;};
    r.isType=[](void* c,std::uint32_t o,std::uint32_t t){const auto it=M(c)->types.find(o);return it!=M(c)->types.end()&&it->second==t;};
    return r;
}
std::uint8_t Byte(Memory& m,std::uint32_t a){return std::to_integer<std::uint8_t>(*m.At(a));}
std::vector<fvr::runtime::RangeSetting> Pairs(std::initializer_list<fvr::runtime::RangeSetting> p){return p;}
}
int main(){
    const RangeSettingsInstances live{Server,Client,Game};
    // The six registry-route switches the PC targets first, plus the deferred level.
    const auto pairs=Pairs({{"Client.Level","Levels/sp_02"},{"Client.LoadMenu","false"},{"Client.DisableMovies","true"},
        {"Client.IsFastStartupEnabled","true"},{"Server.DisableCutscenes","true"},{"Server.IsAiEnabled","false"},
        {"Server.UnrestrictedUnlocks","true"},{"Game.ForceDeltaTime","0.0166667"},{"Client.Kit","3"}});
    {
        const auto plan=PlanRangeSettingWrites(pairs,live);CHECK(plan.size()==pairs.size());
        CHECK(plan[0].status==RangeSettingStatus::DeferredString); // 12-byte String layout not reversed yet
        for(std::size_t n=1;n<plan.size();++n)CHECK(plan[n].status==RangeSettingStatus::Planned);
        CHECK(plan[1].address==Client+0xfa&&plan[2].address==Client+0xf1&&plan[3].address==Client+0xf0);
        CHECK(plan[4].address==Server+0xb5&&plan[5].address==Server+0xc5&&plan[6].address==Server+0xbe&&plan[7].address==Game+0x58);
        CHECK(RangeSettingPlanWritable(plan));
        Memory m;*m.At(Client+0xfa)=std::byte{1};*m.At(Server+0xc5)=std::byte{1}; // engine defaults
        RangeSettingsWriter w;CHECK(w.Apply(plan,Access(m)));CHECK(w.Applied()==8&&w.Deferred()==1);
        CHECK(Byte(m,Client+0xfa)==0&&Byte(m,Client+0xf1)==1&&Byte(m,Server+0xc5)==0&&Byte(m,Server+0xbe)==1);
        float dt=0;std::memcpy(&dt,m.At(Game+0x58),4);CHECK(Near(dt,0.0166667f));
        std::uint32_t kit=0;std::memcpy(&kit,m.At(Client+0x18),4);CHECK(kit==3);
        CHECK(!w.Apply(plan,Access(m))); // single use
        *m.At(Server+0xb5)=std::byte{2}; // engine changed one field since: left alone
        CHECK(!w.Restore(Access(m)));CHECK(w.Conflicts()==1);
        CHECK(Byte(m,Client+0xfa)==1&&Byte(m,Server+0xc5)==1&&Byte(m,Client+0xf1)==0&&Byte(m,Server+0xbe)==0);
        CHECK(std::all_of(m.bytes.begin()+0x2058,m.bytes.begin()+0x205c,[](std::byte b){return b==std::byte{};}));
    }
    {   // Fail closed before any write: unresolved singleton, unmapped passthrough, bad value, wrong type.
        Memory m;
        auto plan=PlanRangeSettingWrites(pairs,{Server,0,Game});
        CHECK(plan[1].status==RangeSettingStatus::Unresolved&&!RangeSettingPlanWritable(plan));
        RangeSettingsWriter a;CHECK(!a.Apply(plan,Access(m))&&m.writes==0);
        plan=PlanRangeSettingWrites(Pairs({{"Client.DisableMenuFlow","true"}}),live);
        CHECK(plan[0].status==RangeSettingStatus::Unmapped&&!RangeSettingPlanWritable(plan));
        plan=PlanRangeSettingWrites(Pairs({{"Client.LoadMenu","yes"}}),live);CHECK(plan[0].status==RangeSettingStatus::InvalidValue);
        plan=PlanRangeSettingWrites(Pairs({{"Client.Kit","-1"}}),live);CHECK(plan[0].status==RangeSettingStatus::InvalidValue);
        plan=PlanRangeSettingWrites(Pairs({{"Game.ForceDeltaTime","nan"}}),live);CHECK(plan[0].status==RangeSettingStatus::InvalidValue);
        plan=PlanRangeSettingWrites(Pairs({{"Client.Level","Levels/sp_02"}}),live);CHECK(!RangeSettingPlanWritable(plan)); // nothing writable
        m.types[Client]=RangeServerSettingsType; // live object is not ClientSettings
        plan=PlanRangeSettingWrites(Pairs({{"Server.IsAiEnabled","false"},{"Client.LoadMenu","false"}}),live);
        RangeSettingsWriter b;CHECK(!b.Apply(plan,Access(m))&&m.writes==0);
        RangeMemory none;RangeSettingsWriter c;CHECK(!c.Apply(plan,none));
    }
    {   // A failed or non-sticking write rolls back everything written so far.
        Memory m;*m.At(Server+0xc5)=std::byte{1};
        const auto plan=PlanRangeSettingWrites(Pairs({{"Server.IsAiEnabled","false"},{"Client.LoadMenu","false"},{"Client.DisableMovies","true"}}),live);
        m.failWriteAt=Client+0xf1;
        RangeSettingsWriter w;CHECK(!w.Apply(plan,Access(m)));CHECK(Byte(m,Server+0xc5)==1&&Byte(m,Client+0xf1)==0);
        Memory n;*n.At(Client+0xfa)=std::byte{1};n.stickyAt=Client+0xfa;
        RangeSettingsWriter v;CHECK(!v.Apply(plan,Access(n)));CHECK(Byte(n,Server+0xc5)==0&&Byte(n,Client+0xfa)==1&&Byte(n,Client+0xf1)==0);
    }
    {   // Overlapping writes to the same field are refused.
        Memory m;const auto plan=PlanRangeSettingWrites(Pairs({{"Client.LoadMenu","false"},{"Client.LoadMenu","true"}}),live);
        RangeSettingsWriter w;CHECK(!w.Apply(plan,Access(m))&&m.writes==0);
    }
    CHECK(RangeSettingStatusName(RangeSettingStatus::DeferredString)=="deferred_string");
    std::puts("Registry-route settings writes are typed, all-or-nothing and exactly restored.");return 0;
}
