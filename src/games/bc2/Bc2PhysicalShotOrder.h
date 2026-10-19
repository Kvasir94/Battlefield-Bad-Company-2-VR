#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace fvr::bc2 {
// Presentation ordering only: this class NEVER authorizes a server projectile
// or changes ammunition. Each client copy may finish server-accepted shots or
// predict at most the physical model's remaining fireable rounds. A pump or
// detached chamber supplies a budget of one. The owner serializes this object.
class PhysicalShotOrder {
public:
    struct Stamp {
        std::uint32_t input=0,loadedBefore=0;
        bool operator==(const Stamp&)const=default;
    };
    // The native input word copied from ctx+1c to firing+94, together with the
    // pre-decrement loaded count, distinguishes rounds within one native input.
    // Restore copies both words, so replaying an already accepted client round
    // does not consume a new presentation permit. History is deliberately kept
    // across mechanical changes; old replay is not a new shot after seating.
    static constexpr unsigned History=64;
    bool Permit(unsigned copy,Stamp stamp,std::uint32_t readyBudget)noexcept {
        if(copy>=clients_.size())return false;
        auto& c=clients_[copy];c.pending=false;
        for(unsigned n=0;n<c.historyCount;++n)if(c.history[n]==stamp)return false;
        // A catch-up permit belongs to an actual recent native input/round;
        // an unrelated future trigger cannot spend an old visual backlog.
        for(const auto& shot:serverHistory_)if(shot.sequence>c.serverMatched&&shot.stamp==stamp){
            c.pending=true;c.stamp=stamp;c.pendingServer=shot.sequence;return true;
        }
        const auto ahead=c.accepted>server_?c.accepted-server_:0;
        if(!readyBudget||ahead>=readyBudget)return false;
        c.pending=true;c.stamp=stamp;c.pendingServer=0;return c.accepted!=std::numeric_limits<std::uint64_t>::max();
    }
    bool ClientAccepted(unsigned copy,std::uint32_t input,std::uint32_t loadedAfter)noexcept {
        if(copy>=clients_.size())return false;
        auto& c=clients_[copy];
        if(!c.pending||c.stamp.input!=input||
            (c.stamp.loadedBefore?loadedAfter!=c.stamp.loadedBefore-1:loadedAfter!=0))return false;
        c.pending=false;
        if(c.accepted==std::numeric_limits<std::uint64_t>::max())return false;
        ++c.accepted;if(c.pendingServer>c.serverMatched)c.serverMatched=c.pendingServer;
        c.history[c.historyNext]=c.stamp;
        c.historyNext=(c.historyNext+1)%History;if(c.historyCount<History)++c.historyCount;
        return true;
    }
    bool ServerAccepted(Stamp stamp)noexcept {
        if(server_==std::numeric_limits<std::uint64_t>::max())return false;
        ++server_;serverHistory_[server_%History]={stamp,server_};
        for(auto& c:clients_)for(unsigned n=0;n<c.historyCount;++n)if(c.history[n]==stamp){c.serverMatched=server_;break;}
        return true;
    }
    void MechanicalBoundary()noexcept {
        // There is no cached fire budget to revoke: Permit always receives the
        // current model's readiness. Keep committed catch-up and already emitted
        // predictions; resetting either creates a duplicate or drops a valid
        // server-first final-round/pump effect. An in-flight original shot may
        // still acknowledge its immediately preceding permit.
        if(mechanical_!=std::numeric_limits<std::uint64_t>::max())++mechanical_;
    }
    std::uint64_t Server()const noexcept{return server_;}
    std::uint64_t Client(unsigned copy)const noexcept{return copy<clients_.size()?clients_[copy].accepted:0;}
private:
    struct ClientState {
        std::uint64_t accepted=0;
        // Count effects actually emitted separately from the highest matched
        // server receipt. A server-first prediction must not skip a count and
        // incorrectly consume the next pump's single-round budget.
        std::uint64_t pendingServer=0,serverMatched=0;
        std::array<Stamp,History> history{};
        unsigned historyCount=0,historyNext=0;
        Stamp stamp{};
        bool pending=false;
    };
    std::array<ClientState,2> clients_{};
    struct ServerShot {Stamp stamp{};std::uint64_t sequence=0;};
    std::array<ServerShot,History> serverHistory_{};
    std::uint64_t server_=0,mechanical_=0;
};
}
