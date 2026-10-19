#include "SupportGrip.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace fvr::interaction {
namespace {
math::Quaternion Product(math::Quaternion a,math::Quaternion b){
 return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
 a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
math::Quaternion Normalize(math::Quaternion q){const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return {q.x/n,q.y/n,q.z/n,q.w/n};}
math::Quaternion Inverse(math::Quaternion q){return {-q.x,-q.y,-q.z,q.w};}
math::Vec3 Rotate(math::Quaternion q,math::Vec3 p){const auto v=Product(Product(q,{p.x,p.y,p.z,0}),Inverse(q));return {v.x,v.y,v.z};}
bool Finite(math::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
SupportGripResult SupportGrip::Update(const SupportGripOwner& owner,const InputFrame& input,const SupportGripContact& contact,bool cancel,bool handBusy,bool consumeBusySqueeze)noexcept {
 auto out=UpdateCore(owner,input,contact,cancel,handBusy,consumeBusySqueeze);
 if(out.holding){releaseNs_=0;return out;}
 if(out.released){const bool fade=out.reason!=SupportRelease::Tracking&&out.reason!=SupportRelease::Identity&&out.reason!=SupportRelease::Crossed;
  releaseNs_=fade?input.predictedNs:0;}
 if(releaseNs_&&input.predictedNs>=releaseNs_&&input.predictedNs-releaseNs_<ReleaseNs&&ValidInput(input)){
  // Normalized lerp from the last correction to identity (small angles).
  const float t=float(double(input.predictedNs-releaseNs_)/double(ReleaseNs)),k=1-t;
  auto q=Normalize({lastDelta_.x*k,lastDelta_.y*k,lastDelta_.z*k,lastDelta_.w*k+t});
  out.input.hands[1].grip.orientation=Normalize(Product(q,Normalize(input.hands[1].grip.orientation)));
  out.input.hands[1].aim.orientation=Normalize(Product(q,Normalize(input.hands[1].aim.orientation)));
 }else releaseNs_=0;
 return out;
}
SupportGripResult SupportGrip::UpdateCore(const SupportGripOwner& owner,const InputFrame& input,const SupportGripContact& contact,bool cancel,bool handBusy,bool consumeBusySqueeze)noexcept {
 SupportGripResult out;out.input=input;const bool previouslyHeld=holding_;
 const auto drop=[&](SupportRelease reason){out.reason=reason;holding_=armed_=false;token_=0;out.holding=out.engaged=false;out.released=previouslyHeld;return out;};
 const auto wait=[&](SupportRelease reason){const bool pending=armed_&&!previouslyHeld;out=drop(reason);armed_=pending;return out;};
 if(!owner.actor||!owner.generation||!owner.equipped||!ValidInput(input)||!input.focused||!input.headValid||
 !input.hands[0].gripTracked||!input.hands[1].gripTracked||!input.hands[1].aimTracked||
 !(input.hands[0].active&Squeeze))return drop(SupportRelease::Tracking);
 if(cancel)return drop(SupportRelease::Action);
 const bool transition=owner_!=owner||space_!=input.spaceGeneration||units_!=input.worldUnitsPerMeter||
 input.generation<generation_||input.predictedNs<time_||(!holding_&&time_&&input.predictedNs-time_>250000000);
 if(transition){holding_=armed_=false;token_=0;out.released=previouslyHeld;out.reason=SupportRelease::Identity;}
 const bool fresh=input.generation!=generation_;
 owner_=owner;space_=input.spaceGeneration;units_=input.worldUnitsPerMeter;generation_=input.generation;time_=input.predictedNs;
 const float squeeze=input.hands[0].squeeze;
 const bool validContact=contact.valid&&std::isfinite(contact.distanceMeters)&&contact.distanceMeters>=0&&Finite(contact.wristOffsetMeters);
 if(handBusy||(!holding_&&!validContact)){
  // No pose/contact/claim is authorized here. A genuinely new tracked neutral
  // packet is useful even while ammunition owns the hand or contact is absent.
  const bool neutral=squeeze<=config_.squeezeOff&&(fresh||armed_);
  out=wait(handBusy?SupportRelease::Action:SupportRelease::Contact);armed_|=neutral;
  // A squeeze used by ammunition/mechanisms belongs to that action. It must
  // not become a deferred support grab when the shell leaves the fingers.
  if(handBusy&&consumeBusySqueeze&&squeeze>=config_.squeezeOn)armed_=false;
  return out;
 }
 if(squeeze<=config_.squeezeOff){holding_=false;token_=0;out.reason=SupportRelease::Button;if(fresh)armed_=true;out.released=previouslyHeld;return out;}
 if(!holding_&&contact.distanceMeters>config_.captureMeters)return wait(SupportRelease::Distance);
 const auto left=math::MakeRelativePose(input.referenceHead,input.hands[0].grip),right=math::MakeRelativePose(input.referenceHead,input.hands[1].grip);
 if(!left||!right)return drop(SupportRelease::Tracking);
 const auto offset=holding_?heldWristOffset_:contact.wristOffsetMeters;
 math::Vec3 direction{left->position.x-right->position.x+offset.x,
 left->position.y-right->position.y+offset.y,left->position.z-right->position.z+offset.z};
 const float length=std::hypot(direction.x,direction.y,direction.z);
 if(!std::isfinite(length)||length<.1f||length>1.2f){
  if(holding_){out.holding=true;out.token=token_;return out;}
  return wait(SupportRelease::Separation);
 }
 direction={direction.x/length,direction.y/length,direction.z/length};
 if(!holding_){if(!armed_||!fresh||squeeze<config_.squeezeOn)return out;if(nextToken_==std::numeric_limits<std::uint64_t>::max())return drop(SupportRelease::Identity);token_=++nextToken_;localDirection_=Rotate(Inverse(right->orientation),direction);heldWristOffset_=contact.wristOffsetMeters;holding_=true;armed_=false;out.engaged=true;}
 const auto from=Rotate(right->orientation,localDirection_);
 const float dot=std::clamp(from.x*direction.x+from.y*direction.y+from.z*direction.z,-1.f,1.f);
 // Crossed hands must not select an arbitrary 180-degree rotation axis.
 if(dot<-.5f){out.holding=true;out.token=token_;return out;}
 const auto delta=Normalize({from.y*direction.z-from.z*direction.y,from.z*direction.x-from.x*direction.z,from.x*direction.y-from.y*direction.x,1+dot});
 const auto reference=Normalize(input.referenceHead.orientation),worldDelta=Product(Product(reference,delta),Inverse(reference));
 lastDelta_=worldDelta;
 out.input.hands[1].grip.orientation=Normalize(Product(worldDelta,Normalize(input.hands[1].grip.orientation)));
 out.input.hands[1].aim.orientation=Normalize(Product(worldDelta,Normalize(input.hands[1].aim.orientation)));
 out.holding=true;out.token=token_;out.correctionRadians=std::acos(dot);return out;
}
}
