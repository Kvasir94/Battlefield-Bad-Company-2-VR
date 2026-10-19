#pragma once
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/MagazineFeedState.h"
#include <cstdint>

// VR-side chamber for one BC2 weapon, fed from the native loaded count.
//
// BC2 keeps one loaded count per firing copy and no chamber count, and its
// magazine-out window is the native reload hold (state 11), which already
// blocks Fire. So this does NOT reorder or start the native reload. It only
//  - tracks magazine presence and the chamber (MagazineFeedState, or
//    EmptyMagazineState for open bolts) for the ammo display, and
//  - holds NeedsRack after a seat that left the chamber empty, which blocks
//    Fire with loaded > 0 until a charging-handle rack, but only when a rack
//    input is wired. Without one, BC2's own empty-reload clip is the rack and
//    the seat clears NeedsRack itself.
// Nothing here writes or credits ammunition. Every count is the game's.
namespace fvr::bc2 {
struct MagazineChamberKey {
 interaction::HandInteractionOwner owner{};interaction::HandInteractionKey weapon{};
 bool operator==(const MagazineChamberKey&)const=default;
};
struct MagazineChamberView {
 bool known=false,magazineOut=false,chamberLoaded=false,needsRack=false;
 int loaded=-1,displayAmmo=-1; // -1 until the first native read.
};
class Bc2MagazineChamber {
public:
 using Kind=interaction::MagazineActionKind;
 // A different owner or weapon identity starts a fresh, unknown chamber,
 // unless it is a weapon this soldier held before (same actor, actor
 // generation and weapon id, any equip generation): like KF2's
 // bSlideLockPending, an owed rack or open bolt survives a switch or stow and
 // comes back with that gun. The last four weapons are kept.
 void Bind(const MagazineChamberKey& key,Kind kind,bool rackInput)noexcept {
  if(!(key==key_)||kind!=kind_){Save();Reset();key_=key;kind_=kind;Restore();}
  rackInput_=rackInput;
 }
 void Reset()noexcept {feed_={};open_={};out_=false;sequence_=0;loaded_=-1;}
 // Once per fresh native lease (new sequence). Repeats are ignored.
 void Observe(int loaded,std::uint64_t sequence)noexcept {
  if(loaded<0||(sequence&&sequence==sequence_))return;
  sequence_=sequence;loaded_=loaded;
  if(!OpenBolt())feed_.Apply(Feed::Observe,loaded);
 }
 void Eject(int loaded)noexcept {
  if(loaded<0)return;loaded_=loaded;
  if(OpenBolt()){if(open_.Apply(Empty::Eject,loaded,kind_))out_=true;}
  else feed_.Apply(Feed::Eject,loaded);
 }
 // Native seat receipt (or the original magazine's return), never the
 // geometric seat: the game has already moved the rounds.
 void Seat(int loaded)noexcept {
  if(loaded<0)return;loaded_=loaded;
  if(OpenBolt()){
   if(out_&&open_.Apply(Empty::Seat,loaded,kind_))out_=false;
   if(!rackInput_&&(open_.flags&interaction::EmptyMagazineState::NeedsAction))open_.Apply(Empty::Action,loaded,kind_);
   return;
  }
  feed_.Apply(Feed::Seat,loaded);
  // NoAction guns are ready on the seat; without a rack input the native
  // empty-reload clip already worked the action.
  if((kind_==Kind::NoAction||!rackInput_)&&(feed_.flags&interaction::MagazineFeedState::NeedsRack))feed_.Apply(Feed::Rack,loaded);
 }
 // A completed charging-handle stroke. Returns true if it cleared NeedsRack
 // (or cocked an open bolt).
 bool Rack(int loaded)noexcept {
  if(loaded<0)return false;loaded_=loaded;
  if(OpenBolt()){
   const auto before=open_.flags;
   return open_.Apply(Empty::Action,loaded,kind_)&&open_.flags!=before;
  }
  const bool needed=feed_.flags&interaction::MagazineFeedState::NeedsRack;
  feed_.Apply(Feed::Rack,loaded);
  return needed&&!(feed_.flags&interaction::MagazineFeedState::NeedsRack);
 }
 [[nodiscard]] bool NeedsRack()const noexcept {
  if(OpenBolt())return !out_&&(open_.flags&interaction::EmptyMagazineState::NeedsAction);
  return (feed_.flags&interaction::MagazineFeedState::Ready)&&(feed_.flags&interaction::MagazineFeedState::NeedsRack);
 }
 [[nodiscard]] bool MagazineOut()const noexcept {return OpenBolt()?out_:feed_.Out();}
 // Fire is blocked only for a loaded, seated gun waiting on a wired rack.
 // The native empty path and the magazine-out hold are the game's to block.
 [[nodiscard]] bool BlocksFire(int loaded)const noexcept {
  return rackInput_&&loaded>0&&!MagazineOut()&&NeedsRack();
 }
 [[nodiscard]] MagazineChamberView View(int loaded)const noexcept {
  MagazineChamberView v;if(loaded<0||loaded_<0)return v;
  v.known=true;v.loaded=loaded;v.magazineOut=MagazineOut();v.needsRack=NeedsRack();
  if(OpenBolt()){v.chamberLoaded=false;v.displayAmmo=out_?0:loaded;}
  else{v.chamberLoaded=feed_.Chamber(loaded);v.displayAmmo=feed_.DisplayAmmo(loaded);}
  return v;
 }
 // Last count Observe/Eject/Seat/Rack saw for this key; -1 before the first.
 [[nodiscard]] int LastLoaded()const noexcept {return loaded_;}
 [[nodiscard]] Kind ActionKind()const noexcept {return kind_;}
 [[nodiscard]] bool RackInput()const noexcept {return rackInput_;}
private:
 using Feed=interaction::MagazineFeedState::Event;using Empty=interaction::EmptyMagazineState::Event;
 bool OpenBolt()const noexcept {return kind_==Kind::OpenBolt;}
 struct Saved {std::uint64_t actor=0,actorGeneration=0,weapon=0,stamp=0;Kind kind=Kind::ClosedBolt;bool out=false;
  interaction::MagazineFeedState feed{};interaction::EmptyMagazineState open{};int loaded=-1;};
 static bool Same(const Saved& s,const MagazineChamberKey& k)noexcept{
  return s.weapon&&s.weapon==k.weapon.id&&s.actor==k.owner.actor&&s.actorGeneration==k.owner.actorGeneration;}
 void Save()noexcept {
  if(!key_.weapon.id||loaded_<0)return;
  Saved* slot=nullptr;for(auto& s:saved_)if(Same(s,key_))slot=&s;
  if(!slot){slot=&saved_[0];for(auto& s:saved_)if(s.stamp<slot->stamp)slot=&s;}
  *slot={key_.owner.actor,key_.owner.actorGeneration,key_.weapon.id,++stamp_,kind_,out_,feed_,open_,loaded_};
 }
 void Restore()noexcept {
  for(auto& s:saved_)if(Same(s,key_)&&s.kind==kind_){out_=s.out;feed_=s.feed;open_=s.open;loaded_=s.loaded;sequence_=0;s={};return;}
 }
 std::array<Saved,4> saved_{};std::uint64_t stamp_=0;
 MagazineChamberKey key_{};Kind kind_=Kind::ClosedBolt;bool rackInput_=false,out_=false;
 interaction::MagazineFeedState feed_{};interaction::EmptyMagazineState open_{};
 std::uint64_t sequence_=0;int loaded_=-1;
};
} // namespace fvr::bc2
