#include "Test.h"
#include "Bc2InputBinding.h"
#include <bit>
#include <cstring>
#include <limits>
using namespace fvr;
int main(){
 CHECK(fvr::bc2::RoomscaleNativeAxis(0)==0);
 CHECK(fvr::bc2::RoomscaleNativeAxis(.002f)==0);
 CHECK(std::abs(fvr::bc2::RoomscaleNativeAxis(.06f)-.105f)<1e-5f);
 CHECK(std::abs(fvr::bc2::RoomscaleNativeAxis(-.06f)+.105f)<1e-5f);
 CHECK(std::abs(fvr::bc2::RoomscaleNativeAxis(6)-.8f/6.f)<1e-5f);

    std::array<std::byte,256> bytes{};auto put=[&](std::size_t at,unsigned x){std::memcpy(bytes.data()+at,&x,4);};
    auto get=[&](std::size_t at){unsigned x;std::memcpy(&x,bytes.data()+at,4);return x;};
    // A capture tick blocks every native entry action, including ones ordinary
    // VR Apply deliberately preserves. It has no freshness/controller lease;
    // expired or malformed native axes cannot fall back to physical controls.
    for(unsigned n=0;n<bytes.size();++n)bytes[n]=std::byte((n*19+7)%256);
    for(unsigned action=0;action<11;++action)put(8+4*action,std::bit_cast<unsigned>(action==4?std::numeric_limits<float>::quiet_NaN():.75f));
    put(0x98,~0u);put(0x9c,~0u);const auto captureBefore=bytes;
    CHECK(!bc2::SuppressNativeHandCaptureInput(bytes,false));CHECK(bytes==captureBefore);
    CHECK(!bc2::SuppressNativeHandCaptureInput(std::span(bytes).first(bc2::InputBytes-1),true));CHECK(bytes==captureBefore);
    CHECK(bc2::SuppressNativeHandCaptureInput(bytes,true));
    for(unsigned action=0;action<11;++action)CHECK(get(8+4*action)==0);
    CHECK(get(0x98)==0&&get(0x9c)==0xfffe0000u);
    for(unsigned n=0;n<bytes.size();++n)if(n<8||(n>=0x34&&n<0x98)||n>=bc2::InputBytes)CHECK(bytes[n]==captureBefore[n]);
    CHECK(bc2::SuppressNativeHandCaptureInput(bytes,true));CHECK(get(0x98)==0&&get(0x9c)==0xfffe0000u);
    // Next native Gather rebuilds the cache; the helper retains no pointer and
    // cannot restore held grenade/menu/equipment/aim inputs over a later tick.
    bytes=captureBefore;CHECK(get(0x98)==~0u&&get(0x9c)==~0u);bytes={};
    interaction::InputFrame captureFrame;captureFrame.generation=7;captureFrame.spaceGeneration=3;captureFrame.predictedNs=100;
    captureFrame.focused=captureFrame.headValid=true;captureFrame.head.position={1,2,3};
    for(auto& hand:captureFrame.hands){hand.active=interaction::Components;hand.held=interaction::Buttons;
        hand.gripTracked=hand.aimTracked=true;hand.grip.position={4,5,6};hand.aim.position={7,8,9};
        hand.stickX=hand.stickY=hand.trigger=hand.squeeze=1;hand.touchActive=hand.touched=interaction::TouchComponents;}
    bc2::SuppressNativeHandCaptureControls(captureFrame,false);CHECK(captureFrame.hands[0].held==interaction::Buttons);
    bc2::SuppressNativeHandCaptureControls(captureFrame,true);
    CHECK(captureFrame.generation==7&&captureFrame.spaceGeneration==3&&captureFrame.predictedNs==100&&captureFrame.focused&&captureFrame.headValid);
    CHECK(captureFrame.head.position.x==1&&captureFrame.head.position.y==2&&captureFrame.head.position.z==3);
    for(const auto& hand:captureFrame.hands){CHECK(!hand.active&&!hand.held&&!hand.stickX&&!hand.stickY&&!hand.trigger&&!hand.squeeze);
        CHECK(hand.gripTracked&&hand.aimTracked&&hand.grip.position.x==4&&hand.aim.position.x==7);
        CHECK(hand.touchActive==interaction::TouchComponents&&hand.touched==interaction::TouchComponents);}
    interaction::RecenterGesture recenter;CHECK(!recenter.Update(captureFrame));
    bc2::SuppressNativeHandCaptureControls(captureFrame,true);CHECK(!captureFrame.hands[1].active&&!captureFrame.hands[1].held);
    // Physical-only isolation admits the actual synthetic InputOverride commit
    // while eliminating unrelated native grenade/menu/aim/movement values.
    bytes=captureBefore;CHECK(!bc2::SuppressNativePhysicalInput(bytes,false));CHECK(bytes==captureBefore);
    CHECK(bc2::SuppressNativePhysicalInput(bytes,true));
    interaction::ActionOutput synthetic;synthetic.active=true;synthetic.owner=1;
    synthetic.held=interaction::Fire|interaction::Reload;
    {bc2::InputOverride command;CHECK(command.Apply(bytes,synthetic));command.Commit();}
    CHECK(bc2::CompleteNativePhysicalInput(bytes,true,true,true));
    CHECK(get(40)==std::bit_cast<unsigned>(1.f)&&get(0x98)==(1u<<29));
    CHECK(get(8)==0&&get(12)==0&&get(24)==0&&get(28)==0&&get(0x24)==0&&get(0x9c)==0xfffe0000u);
    // Lease loss AFTER staging also removes synthetic commands; the next native
    // gather rebuild cannot revive hardware through a stale/no-active fallback.
    CHECK(bc2::CompleteNativePhysicalInput(bytes,true,true,false));
    CHECK(get(40)==0&&get(0x98)==0&&get(0x9c)==0xfffe0000u);
    for(unsigned tick=0;tick<3;++tick){bytes=captureBefore;CHECK(bc2::SuppressNativePhysicalInput(bytes,true));
        CHECK(bc2::CompleteNativePhysicalInput(bytes,true,false,tick==0));
        for(unsigned action=0;action<11;++action)CHECK(get(8+4*action)==0);
        CHECK(get(0x98)==0&&get(0x9c)==0xfffe0000u);}
    // Rollback snapshots are taken AFTER physical suppression. Neither rollback
    // nor the strong hand-capture exit may restore/retain a physical command.
    {bc2::InputOverride aborted;CHECK(aborted.Apply(bytes,synthetic));CHECK(aborted.Restore());}
    CHECK(get(40)==0&&get(0x98)==0&&get(0x9c)==0xfffe0000u);
    {bc2::InputOverride command;CHECK(command.Apply(bytes,synthetic));command.Commit();}
    CHECK(bc2::CompleteNativePhysicalInput(bytes,true,false,true)); // Strong capture wins both switches.
    CHECK(get(40)==0&&get(0x98)==0&&get(0x9c)==0xfffe0000u);
    for(unsigned n=0;n<bytes.size();++n)if(n<8||(n>=0x34&&n<0x98)||n>=bc2::InputBytes)CHECK(bytes[n]==captureBefore[n]);
    const auto blockedBefore=bytes;CHECK(!bc2::CompleteNativePhysicalInput(std::span(bytes).first(bc2::InputBytes-1),true,true,true));
    CHECK(bytes==blockedBefore);CHECK(!bc2::CompleteNativePhysicalInput(bytes,false,false,false));CHECK(bytes==blockedBefore);bytes={};
    put(0,0x12345678);put(0x98,1u<<28);put(0x9c,0x80000000);const auto original=bytes;
    interaction::ActionOutput a{};a.owner=1;a.active=true;a.forward=.5f;a.strafe=-.7f;
    a.held=interaction::Fire|interaction::Sprint|interaction::AlternateFire|interaction::Jump;
    {bc2::InputOverride scope;CHECK(scope.Apply(bytes,a));CHECK(!scope.Apply(bytes,a));
     CHECK(get(8)==std::bit_cast<unsigned>(.5f)&&get(12)==std::bit_cast<unsigned>(-.7f));CHECK(get(40)==std::bit_cast<unsigned>(1.f));
     CHECK(get(0x98)==((1u<<28)|(1u<<31)|(1u<<14)|(1u<<15)));CHECK(get(0x9c)==0x80000000);CHECK(scope.Restore());}
    CHECK(bytes==original);
    {bc2::InputOverride scope;CHECK(scope.Apply(bytes,a));put(0x98,get(0x98)&~(1u<<31));put(0x9c,0x12345678);put(40,0);
     CHECK(!scope.Restore());CHECK(!(get(0x98)&(1u<<31)));CHECK(get(0x9c)==0x12345678);CHECK(get(40)==0);}
    bytes=original;a.active=false;{bc2::InputOverride s;CHECK(!s.Apply(bytes,a));}CHECK(bytes==original);
    a.active=true;a.forward=std::numeric_limits<float>::quiet_NaN();{bc2::InputOverride s;CHECK(!s.Apply(bytes,a));}CHECK(bytes==original);
    a.forward=0;{bc2::InputOverride s;CHECK(!s.Apply(std::span(bytes).first(100),a));}CHECK(bytes==original);
    a.held=interaction::Crouch;a.pressed=interaction::Crouch;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x98)&(1u<<26));}a.pressed=0;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x98)&(1u<<26));}
    CHECK(bytes==original);
    // Bounded grenade probe uses native bit 38, leaving other high-word buttons
    // intact; false releases it, and exact rollback keeps subsequent game edits.
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,true));CHECK(get(0x9c)==(0x80000000u|(1u<<6)));CHECK(s.Restore());}CHECK(bytes==original);
    put(0x9c,0x80000000u|(1u<<6));
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,false));CHECK(get(0x9c)==0x80000000u);CHECK(s.Restore());}CHECK(get(0x9c)&(1u<<6));
    bytes=original;
    // Weapon switching is a float action (7), not a button bit. Diagnostics
    // own its pulse/neutral value; rollback and commit preserve the native ABI.
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},true));CHECK(get(0x24)==std::bit_cast<unsigned>(1.f));CHECK(s.Restore());}CHECK(bytes==original);
    put(0x24,std::bit_cast<unsigned>(.5f));
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==0);CHECK(s.Restore());}
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},false));CHECK(get(0x24)==0);CHECK(s.Restore());}CHECK(get(0x24)==std::bit_cast<unsigned>(.5f));
    bytes=original;
    // Selection edges are signed one-tick pulses; holds and opposing requests
    // cannot cycle repeatedly. Exact rollback preserves later native changes.
    a.pressed=interaction::NextWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==std::bit_cast<unsigned>(1.f));CHECK(s.Restore());}CHECK(bytes==original);
    a.pressed=interaction::PreviousWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==std::bit_cast<unsigned>(-1.f));CHECK(s.Restore());}
    // Merely enabling the diagnostic must not erase normal stick selection.
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},false));CHECK(get(0x24)==std::bit_cast<unsigned>(-1.f));CHECK(s.Restore());}
    a.pressed=interaction::NextWeapon|interaction::PreviousWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==0);CHECK(s.Restore());}
    a.pressed=0;a.held=interaction::NextWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==0);CHECK(s.Restore());}
    // Dedicated mode commands touch only their verified selection bit. Other
    // high-word commands and subsequent native changes survive exact rollback.
    a.held=a.pressed=0;
    for(auto mode:{bc2::EntryAction::GrenadeLauncher,bc2::EntryAction::DynamicGadget2}){
        bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},mode));
        CHECK(get(0x9c)==(0x80000000u|(1u<<(unsigned(mode)-32))));
        CHECK(s.Restore());CHECK(bytes==original);
    }
    {bc2::InputOverride s;CHECK(!s.Apply(bytes,a,{},{},bc2::EntryAction::ThrowGrenade));CHECK(bytes==original);}
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},bc2::EntryAction::GrenadeLauncher));
     put(0x9c,0x80000004u);CHECK(!s.Restore());CHECK(get(0x9c)==0x80000004u);}
    bytes=original;
    // Only the verified native context alias extends the ordinary Use mask.
    a.held=interaction::Use;a.pressed=interaction::Use;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},{},true));CHECK((get(0x98)&((1u<<16)|(1u<<27)))==((1u<<16)|(1u<<27)));CHECK(s.Restore());}CHECK(bytes==original);
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(!(get(0x98)&(1u<<16))&&(get(0x98)&(1u<<27)));CHECK(s.Restore());}
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},{},true));put(0x98,get(0x98)&~(1u<<16));CHECK(!s.Restore());CHECK(!(get(0x98)&((1u<<16)|(1u<<27))));}
    bytes=original;put(0x98,get(0x98)|(1u<<16));const auto nativeHeld=bytes;a.held=a.pressed=0;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},{},true));CHECK(!(get(0x98)&((1u<<16)|(1u<<27))));CHECK(s.Restore());}CHECK(bytes==nativeHeld);
    bytes=original;
    // A vehicle exit command preserves every native driving/fire/aim byte.
    for(unsigned n=8;n<0x98;++n)bytes[n]=std::byte((n*19)%256);
    const auto vehicleBefore=bytes;a.held=interaction::Use|interaction::Fire;a.forward=1;
    {bc2::InputOverride s;CHECK(s.ApplyVehicleExit(bytes,a));CHECK(get(0x98)==((1u<<28)|(1u<<16)));
     auto expected=vehicleBefore;unsigned flags=(1u<<28)|(1u<<16);std::memcpy(expected.data()+0x98,&flags,4);CHECK(bytes==expected);CHECK(s.Restore());}
    CHECK(bytes==vehicleBefore);bytes=original;
    a.held=interaction::Fire;a.forward=.3f;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));s.Commit();}
    CHECK(get(8)==std::bit_cast<unsigned>(.3f)&&get(40)==std::bit_cast<unsigned>(1.f));
    // Native gather owns the next tick. A committed transaction stores no
    // pointer and cannot later restore over the game's fresh keyboard input.
    bytes=original;CHECK(get(8)==0&&get(40)==0);return 0;
}
