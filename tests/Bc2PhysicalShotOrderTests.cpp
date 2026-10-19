#include "Test.h"
#include "Bc2PhysicalShotOrder.h"
using fvr::bc2::PhysicalShotOrder;
int main(){
    // Server-first last round: readiness is already false when both independent
    // client copies reach the same native input/round. Each gets one effect.
    PhysicalShotOrder serverFirst;
    CHECK(serverFirst.ServerAccepted({100,1}));
    CHECK(serverFirst.Permit(0,{100,1},0));
    CHECK(serverFirst.ClientAccepted(0,100,0));
    CHECK(serverFirst.Permit(1,{100,1},0));
    CHECK(serverFirst.ClientAccepted(1,100,0));
    CHECK(!serverFirst.Permit(0,{100,1},0));
    CHECK(!serverFirst.Permit(1,{101,1},0));

    // Client-first pump or detached chamber: an unconfirmed presentation can
    // consume at most one round, even if native Restore reinstates ammunition.
    PhysicalShotOrder predicted;
    CHECK(predicted.Permit(0,{200,4},1));
    CHECK(predicted.ClientAccepted(0,200,3));
    CHECK(!predicted.Permit(0,{200,4},1)); // exact Restore replay
    CHECK(!predicted.Permit(0,{201,3},1)); // second predicted pump shot
    CHECK(predicted.ServerAccepted({200,4}));
    CHECK(!predicted.Permit(0,{200,4},0));
    CHECK(predicted.Permit(1,{200,4},0));
    CHECK(predicted.ClientAccepted(1,200,3));

    // Multiple automatic shots in one input carry the same native seed, but
    // loaded-before separates each round and its duplicate prediction replay.
    PhysicalShotOrder automatic;
    CHECK(automatic.Permit(0,{300,3},3));CHECK(automatic.ClientAccepted(0,300,2));
    CHECK(automatic.Permit(0,{300,2},3));CHECK(automatic.ClientAccepted(0,300,1));
    CHECK(automatic.Permit(0,{300,1},3));CHECK(automatic.ClientAccepted(0,300,0));
    CHECK(!automatic.Permit(0,{301,3},3));
    CHECK(automatic.ServerAccepted({300,3}));CHECK(automatic.ServerAccepted({300,2}));CHECK(automatic.ServerAccepted({300,1}));
    CHECK(!automatic.Permit(0,{300,3},0));

    // Mechanical changes revoke prediction budget by supplying the NEW model
    // budget; they neither erase recent replay history nor a committed effect.
    PhysicalShotOrder mechanics;
    CHECK(mechanics.ServerAccepted({400,2}));mechanics.MechanicalBoundary();
    CHECK(mechanics.Permit(0,{400,2},0));CHECK(mechanics.ClientAccepted(0,400,1));
    CHECK(!mechanics.Permit(0,{401,1},0));
    mechanics.MechanicalBoundary();
    CHECK(!mechanics.Permit(0,{400,2},30));
    CHECK(mechanics.Permit(0,{402,31},31));CHECK(mechanics.ClientAccepted(0,402,30));

    // An unrelated input cannot spend accumulated old server-first effects.
    PhysicalShotOrder stale;
    CHECK(stale.ServerAccepted({500,8}));CHECK(stale.ServerAccepted({501,7}));
    CHECK(!stale.Permit(0,{600,8},0));
    CHECK(stale.Permit(0,{501,7},0));CHECK(stale.ClientAccepted(0,501,6));
    CHECK(!stale.Permit(0,{500,8},0));

    // A callback without the immediately preceding permit, wrong native input,
    // or a non-decrementing positive count cannot advance presentation order.
    PhysicalShotOrder invalid;
    CHECK(!invalid.ClientAccepted(0,1,0));
    CHECK(invalid.Permit(0,{700,2},1));
    CHECK(!invalid.ClientAccepted(0,701,1));
    CHECK(!invalid.ClientAccepted(0,700,2));
    CHECK(invalid.ClientAccepted(0,700,1));
    CHECK(!invalid.ClientAccepted(0,700,1));
    CHECK(!invalid.Permit(2,{1,1},1));
    // A native input stamp can differ during prediction. Already-observed
    // server progress must not manufacture an extra client effect count.
    PhysicalShotOrder offset;
    CHECK(offset.ServerAccepted({800,4}));
    CHECK(offset.Permit(0,{801,4},3));CHECK(offset.ClientAccepted(0,801,3));
    CHECK(offset.Client(0)==1);
    CHECK(offset.ServerAccepted({802,3}));
    CHECK(offset.Permit(0,{802,3},0));CHECK(offset.ClientAccepted(0,802,2));
    CHECK(offset.Client(0)==2);
    return 0;
}
