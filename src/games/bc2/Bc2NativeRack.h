#pragma once
#include "Bc2NativeActionPose.h"
#include "fvr/interaction/MotionGuard.h"

namespace fvr::bc2 {
namespace native_rack_detail {
using Vec=std::array<double,3>;
inline double Dot(const Vec& a,const Vec& b)noexcept {double v=0;for(unsigned i=0;i<3;++i)v+=a[i]*b[i];return v;}
inline Vec Position(const math::Matrix4& m)noexcept {return {m.values[3][0],m.values[3][1],m.values[3][2]};}
inline Vec Sub(Vec a,const Vec& b)noexcept {for(unsigned i=0;i<3;++i)a[i]-=b[i];return a;}
inline Vec RotationLog(const math::Matrix4& a,const math::Matrix4& b)noexcept {
    const auto inverse=interaction::InverseAnimatedTransform(b);if(!inverse)return {};
    const auto r=interaction::Multiply(a,*inverse);
    const double angle=std::acos(std::clamp((double(r.values[0][0])+r.values[1][1]+r.values[2][2]-1)*.5,-1.,1.));
    if(angle<1e-7)return {};
    // Near pi has no stable skew axis. Such a discontinuity cannot be an
    // adjacent native sample or accepted tracked-controller step.
    if(angle>3.13)return {100.,100.,100.};
    const double s=angle/(2*std::sin(angle));
    return {(r.values[1][2]-r.values[2][1])*s,(r.values[2][0]-r.values[0][2])*s,(r.values[0][1]-r.values[1][0])*s};
}
}
struct NativeRackProjection {std::size_t segment=0;float progress=0;double errorMeters=0;};
// Project raw wrist displacement AND twist onto a bounded contiguous native
// pose curve. The angular weight is a VR input metric, not game asset data.
inline std::optional<NativeRackProjection> ProjectNativeRackPose(std::span<const NativeActionPoseFrame> frames,
 const math::Matrix4& raw,std::size_t cursor,unsigned radius=12,float rotationWeight=.05f,double reachableArcMeters=-1)noexcept {
    using namespace native_rack_detail;
    if(frames.size()<2||frames.size()>4096||cursor>=frames.size()-1||radius>64||!radius||
       !std::isfinite(rotationWeight)||rotationWeight<=0||!interaction::authored_contact_detail::Rigid(raw,2.f))return {};
    std::optional<NativeRackProjection> best;
    auto begin=cursor>radius?cursor-radius:0,end=std::min(frames.size()-1,cursor+radius+1);
    if(reachableArcMeters>=0){
        if(!std::isfinite(reachableArcMeters))return {};
        const auto segmentLength=[&](std::size_t i){
            const auto d=Sub(Position(frames[i+1].wristInWeapon),Position(frames[i].wristInWeapon));
            const auto r=RotationLog(frames[i+1].wristInWeapon,frames[i].wristInWeapon);
            return std::sqrt(Dot(d,d)+double(rotationWeight)*rotationWeight*Dot(r,r));
        };
        begin=cursor;end=cursor+1;double backward=0,forward=0;
        while(begin>0&&backward+segmentLength(begin-1)<=reachableArcMeters){--begin;backward+=segmentLength(begin);}
        while(end<frames.size()-1&&forward+segmentLength(end)<=reachableArcMeters){forward+=segmentLength(end);++end;}
    }
    for(std::size_t i=begin;i<end;++i){
        const auto& a=frames[i];const auto& b=frames[i+1];
        const auto d=Sub(Position(b.wristInWeapon),Position(a.wristInWeapon));
        const auto p=Sub(Position(raw),Position(a.wristInWeapon));
        const auto angle=RotationLog(b.wristInWeapon,a.wristInWeapon),r=RotationLog(raw,a.wristInWeapon);
        const double w=double(rotationWeight)*rotationWeight,den=Dot(d,d)+w*Dot(angle,angle);
        const double t=den>1e-15?std::clamp((Dot(d,p)+w*Dot(angle,r))/den,0.,1.):0.;
        Vec pe=p,re=r;for(unsigned n=0;n<3;++n){pe[n]-=t*d[n];re[n]-=t*angle[n];}
        const double error=std::sqrt(Dot(pe,pe)+w*Dot(re,re));
        if(!best||error<best->errorMeters)best=NativeRackProjection{i,float(a.progress+t*(b.progress-a.progress)),error};
    }
    return best;
}
// A held forward stroke reverses the original grasped Pull samples. Once the
// hand lets go, join the ORIGINAL spring-return curve at the closest part pose,
// rather than restarting its rear dwell and teleporting the handle backward.
// Every result is an interpolation of adjacent original frames; no corrective
// part transform or rigid hand attachment is introduced.
inline std::optional<NativeRackProjection> ProjectNativeRackParts(std::span<const NativeActionPoseFrame> frames,
 std::span<const math::Matrix4> parts,float rotationWeight)noexcept {
    using namespace native_rack_detail;
    if(frames.size()<2||parts.empty()||parts.size()>32)return {};
    std::optional<NativeRackProjection> best;
    for(std::size_t i=0;i+1<frames.size();++i){
        const auto& a=frames[i];const auto& b=frames[i+1];
        if(a.partsInWeapon.size()!=parts.size()||b.partsInWeapon.size()!=parts.size())return {};
        double numerator=0,denominator=0;const double w=double(rotationWeight)*rotationWeight;
        for(std::size_t n=0;n<parts.size();++n){
            const auto d=Sub(Position(b.partsInWeapon[n]),Position(a.partsInWeapon[n]));
            const auto p=Sub(Position(parts[n]),Position(a.partsInWeapon[n]));
            const auto angle=RotationLog(b.partsInWeapon[n],a.partsInWeapon[n]),r=RotationLog(parts[n],a.partsInWeapon[n]);
            numerator+=Dot(d,p)+w*Dot(angle,r);denominator+=Dot(d,d)+w*Dot(angle,angle);
        }
        const double t=denominator>1e-15?std::clamp(numerator/denominator,0.,1.):0.;double error=0;
        for(std::size_t n=0;n<parts.size();++n){
            auto p=Sub(Position(parts[n]),Position(a.partsInWeapon[n]));
            const auto d=Sub(Position(b.partsInWeapon[n]),Position(a.partsInWeapon[n]));
            auto r=RotationLog(parts[n],a.partsInWeapon[n]);const auto angle=RotationLog(b.partsInWeapon[n],a.partsInWeapon[n]);
            for(unsigned k=0;k<3;++k){p[k]-=t*d[k];r[k]-=t*angle[k];}
            error+=Dot(p,p)+w*Dot(r,r);
        }
        error=std::sqrt(error/double(parts.size()));
        if(!best||error<best->errorMeters)best=NativeRackProjection{i,float(a.progress+t*(b.progress-a.progress)),error};
    }
    return best;
}
enum class NativeRackPhase:std::uint8_t {Idle,Prep,Pull,Rear,Return,CancelPull,CancelPrep};
struct NativeRackIdentity {
    std::uint64_t weapon=0,owner=0,claim=0;
    bool operator==(const NativeRackIdentity&)const=default;
    bool Valid()const noexcept{return weapon&&owner&&claim;}
};
struct NativeRackConfig {
    // Acquire, Prep, Pull, Return in that order, matching one original clip.
    // XM8 proposal provenance: original US_rif_XM8/1P/1P_Reload.res applied to
    // the compatible US_rgl_XM8 receiver, never a linear moving-part donor.
    std::span<const NativeActionPoseProfile> phases;
    float grabRadiusMeters=.06f,grabAngleRadians=.8f,releaseErrorMeters=.08f;
    float rotationWeight=.05f,maxAngularStepRadians=.65f;
    unsigned cursorRadius=12;
    interaction::MotionGuardConfig motion=interaction::MotionGuardConfig::Rack();
    float maxAngularSpeedRadiansPerSecond=15.f;
};
struct NativeRackSample {
    NativeRackIdentity identity;
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,nowNs=0;
    math::Matrix4 wristInWeapon{};
    bool focused=false,tracked=false,grip=false;
};
struct NativeRackResult {
    NativeRackPhase phase=NativeRackPhase::Idle;
    const NativeActionPoseProfile* pose=nullptr;
    float progress=0;
    bool ownsHand=false,ownsPart=false,grabbed=false,rearStop=false,closed=false,cancelled=false;
};
class NativeRack {
public:
    explicit NativeRack(NativeRackConfig config):config_(config),valid_(Validate(config)){
        if(valid_){
            for(unsigned i=0;i<4;++i){owned_[i].Copy(config.phases[i]);profiles_[i]=owned_[i].profile;}
            config_.phases=profiles_;
        }
    }
    NativeRack(const NativeRack&)=delete;NativeRack& operator=(const NativeRack&)=delete;
    NativeRack(NativeRack&&)=delete;NativeRack& operator=(NativeRack&&)=delete;
    bool Valid()const noexcept{return valid_;}
    void Reset()noexcept {phase_=NativeRackPhase::Idle;identity_={};neutral_=false;geometryPaused_=false;cursor_=0;progress_=0;guard_.Reset();sequence_=0;lastNs_=0;}
    // A real current ownership heartbeat may retain a sampled pose or release
    // it. It is NOT a wrist sample and cannot advance physical progress.
    NativeRackResult Heartbeat(NativeRackIdentity identity,bool focused,bool tracked,bool grip,std::int64_t now)noexcept {
        if(!valid_||!focused||!tracked||now<=0||identity!=identity_||phase_==NativeRackPhase::Idle)return Abort();
        if(!grip&&Held())StartReturn(now);
        if(Returning())return AdvanceReturn(now);
        geometryPaused_=true;
        return Result();
    }
    NativeRackResult Update(const NativeRackSample& s)noexcept {
        using namespace native_rack_detail;
        // Ownership/focus/tracking invalidation beats duplicate suppression.
        if(!valid_||!s.identity.Valid()||!s.focused||!s.tracked||!s.sequence||s.observedNs<=0||s.nowNs<s.observedNs||
           s.nowNs-s.observedNs>config_.motion.maxGapNs||!interaction::authored_contact_detail::Rigid(s.wristInWeapon,2.f)||
           (phase_!=NativeRackPhase::Idle&&s.identity!=identity_))return Abort();
        if(s.sequence<sequence_)return Abort();
        if(phase_!=NativeRackPhase::Idle&&!s.grip&&Held())StartReturn(s.nowNs);
        if(Returning())return AdvanceReturn(s.nowNs);
        if(s.sequence<=sequence_)return Result();
        sequence_=s.sequence;
        if(phase_==NativeRackPhase::Idle){
            if(!s.grip){neutral_=true;return Result();}
            if(!neutral_)return Result();neutral_=false;
            const auto& entry=config_.phases[1].frames.front().wristInWeapon;
            const auto offset=Sub(Position(s.wristInWeapon),Position(entry)),rotation=RotationLog(s.wristInWeapon,entry);
            if(std::sqrt(Dot(offset,offset))>config_.grabRadiusMeters||std::sqrt(Dot(rotation,rotation))>config_.grabAngleRadians)return Result();
            const interaction::MotionGuard::Position pos{float(Position(s.wristInWeapon)[0]),float(Position(s.wristInWeapon)[1]),float(Position(s.wristInWeapon)[2])};
            if(!guard_.Begin(config_.motion,pos,s.observedNs))return Abort();
            identity_=s.identity;phase_=NativeRackPhase::Prep;cursor_=0;progress_=0;lastWrist_=s.wristInWeapon;lastNs_=s.observedNs;
            auto r=Result();r.grabbed=true;return r;
        }
        const auto rotation=RotationLog(s.wristInWeapon,lastWrist_);
        const interaction::MotionGuard::Position pos{s.wristInWeapon.values[3][0],s.wristInWeapon.values[3][1],s.wristInWeapon.values[3][2]};
        if(s.observedNs-lastNs_>config_.motion.maxGapNs){
            if(!geometryPaused_)return Abort();
            // Resume from an actual fresh pose without inferring any motion
            // during missing renderer geometry. No phase/progress change.
            guard_.Begin(config_.motion,pos,s.observedNs);lastWrist_=s.wristInWeapon;lastNs_=s.observedNs;geometryPaused_=false;return Result();
        }
        geometryPaused_=false;
        const double angularStep=std::max(double(config_.maxAngularStepRadians),
            double(config_.maxAngularSpeedRadiansPerSecond)*double(s.observedNs-lastNs_)*1e-9);
        if(s.observedNs<=lastNs_||std::sqrt(Dot(rotation,rotation))>angularStep||
           guard_.Check(pos,s.observedNs)!=interaction::MotionGuardVerdict::Accepted)return Abort();
        const auto displacement=Sub(Position(s.wristInWeapon),Position(lastWrist_));
        const double angularDistance=std::sqrt(Dot(rotation,rotation))*config_.rotationWeight;
        const double trackedDistance=std::sqrt(Dot(displacement,displacement)+angularDistance*angularDistance);
        // Search an arc reachable by this observed motion, not a fixed number
        // of 120 Hz source frames. A deliberate fast gesture can cross many
        // densely sampled segments without leaving the input cursor behind.
        const double reachable=std::min(2*trackedDistance+config_.motion.positionSlackMeters,
            config_.motion.speedLimitMetersPerSecond*double(s.observedNs-lastNs_)*1e-9+
            config_.motion.positionSlackMeters+angularDistance);
        const bool stationary=Dot(displacement,displacement)<1e-14&&Dot(rotation,rotation)<1e-12;
        lastWrist_=s.wristInWeapon;lastNs_=s.observedNs;
        if(stationary)return Result(); // Repeated poses cannot walk the cursor through a distant curve.
        const auto& profile=config_.phases[phase_==NativeRackPhase::Prep?1:2];
        const auto projected=ProjectNativeRackPose(profile.frames,s.wristInWeapon,cursor_,config_.cursorRadius,config_.rotationWeight,reachable);
        if(!projected||projected->errorMeters>config_.releaseErrorMeters)return Abort();
        cursor_=projected->segment;progress_=projected->progress;
        if(phase_==NativeRackPhase::Rear){
            // KF2 permits riding the action forward while still squeezing.
            // Keep the sampled hand bound until <=35% of the axial pull, not
            // merely until the first forward sample. Native progress includes
            // wrist twist and is deliberately NOT this physical distance.
            if(HeldForwardHome(s.wristInWeapon))StartReturn(s.nowNs);
            return Result();
        }
        if(progress_>=.99999f){
            if(phase_==NativeRackPhase::Prep){phase_=NativeRackPhase::Pull;progress_=0;cursor_=0;}
            else {phase_=NativeRackPhase::Rear;auto r=Result();r.rearStop=true;return r;}
        }
        return Result();
    }
private:
    // Snapshots keep caller-owned spans and strings from changing underneath
    // a live claim. Nonmovable ownership keeps all internal views stable.
    struct OwnedPhase {
        NativeActionPoseProfile profile;
        std::array<std::string,5> strings;
        std::vector<std::string> names;
        std::vector<std::string_view> nameViews;
        std::vector<NativeActionPoseFrame> frames;
        std::vector<std::vector<math::Matrix4>> parts;
        void Copy(const NativeActionPoseProfile& p){
            profile=p;strings={std::string(p.assetName),std::string(p.meshPath),std::string(p.clipSha256),std::string(p.skeletonSha256),std::string(p.captureSha256)};
            profile.assetName=strings[0];profile.meshPath=strings[1];profile.clipSha256=strings[2];profile.skeletonSha256=strings[3];profile.captureSha256=strings[4];
            names.reserve(p.partBones.size());for(auto n:p.partBones)names.emplace_back(n);
            for(const auto& n:names)nameViews.push_back(n);profile.partBones=nameViews;
            frames.assign(p.frames.begin(),p.frames.end());parts.resize(frames.size());
            for(std::size_t i=0;i<frames.size();++i){parts[i].assign(frames[i].partsInWeapon.begin(),frames[i].partsInWeapon.end());frames[i].partsInWeapon=parts[i];}
            profile.frames=frames;
        }
    };
    static bool Validate(const NativeRackConfig& c)noexcept {
        if(c.phases.size()!=4||!interaction::MotionGuard::ValidConfig(c.motion)||!c.cursorRadius||c.cursorRadius>64||
           !std::isfinite(c.maxAngularSpeedRadiansPerSecond)||c.maxAngularSpeedRadiansPerSecond<=0||c.maxAngularSpeedRadiansPerSecond>100)return false;
        for(float v:{c.grabRadiusMeters,c.grabAngleRadians,c.releaseErrorMeters,c.rotationWeight,c.maxAngularStepRadians})if(!std::isfinite(v)||v<=0||v>2)return false;
        const auto& first=c.phases.front();
        for(const auto& p:c.phases){
            if(!ValidNativeActionPose(p)||p.action!=NativeActionPoseKind::Rack||p.frames.size()<2||p.partBones.empty()||
               p.assetName!=first.assetName||p.meshPath!=first.meshPath||p.rigFingerprint!=first.rigFingerprint||
               p.clipSha256!=first.clipSha256||p.skeletonSha256!=first.skeletonSha256||p.partBones.size()!=first.partBones.size()||
               !std::equal(p.partBones.begin(),p.partBones.end(),first.partBones.begin())||
               p.frames.front().progress!=0||p.frames.back().progress!=1)return false;
            for(std::size_t i=0;i<p.frames.size();++i)if(p.frames[i].returning||(i&&
                (p.frames[i].progress<=p.frames[i-1].progress||p.frames[i].timeSeconds<=p.frames[i-1].timeSeconds)))return false;
        }
        for(unsigned phase=1;phase<4;++phase){
            const auto& a=c.phases[phase-1].frames.back();const auto& b=c.phases[phase].frames.front();
            if(std::abs(a.timeSeconds-b.timeSeconds)>.00001f)return false;
            for(std::size_t n=0;n<a.partsInWeapon.size();++n)for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)
                if(std::abs(a.partsInWeapon[n].values[r][col]-b.partsInWeapon[n].values[r][col])>.0001f)return false;
        }
        return true;
    }
    bool Held()const noexcept{return phase_==NativeRackPhase::Prep||phase_==NativeRackPhase::Pull||phase_==NativeRackPhase::Rear;}
    bool Returning()const noexcept{return phase_==NativeRackPhase::Return||phase_==NativeRackPhase::CancelPull||phase_==NativeRackPhase::CancelPrep;}
    NativeRackResult Result()const noexcept {
        NativeRackResult r;r.phase=phase_;r.progress=progress_;r.ownsHand=Held();r.ownsPart=phase_!=NativeRackPhase::Idle;
        if(r.ownsPart)r.pose=&config_.phases[phase_==NativeRackPhase::Prep||phase_==NativeRackPhase::CancelPrep?1:phase_==NativeRackPhase::Return?3:2];
        return r;
    }
    NativeRackResult Abort()noexcept {const bool active=phase_!=NativeRackPhase::Idle;Reset();auto r=Result();r.cancelled=active;return r;}
    bool HeldForwardHome(const math::Matrix4& wrist)const noexcept {
        using namespace native_rack_detail;
        const auto& pull=config_.phases[2];
        const auto axis=Sub(Position(pull.frames.back().partsInWeapon.front()),Position(pull.frames.front().partsInWeapon.front()));
        const double squared=Dot(axis,axis);
        return squared>1e-12&&Dot(Sub(Position(wrist),Position(pull.frames.front().wristInWeapon)),axis)<=.35*squared;
    }
    float MatchedReturnProgress()const noexcept {
        const auto& pull=config_.phases[2];
        const NativeActionPoseFrame *a=&pull.frames.front(),*b=&pull.frames.back();
        for(const auto& f:pull.frames){if(f.progress<=progress_)a=&f;if(f.progress>=progress_){b=&f;break;}}
        const float t=b->progress>a->progress?(progress_-a->progress)/(b->progress-a->progress):0;
        std::array<math::Matrix4,32> parts;
        for(std::size_t n=0;n<pull.partBones.size();++n){
            const auto blended=interaction::BlendTransform(a->partsInWeapon[n],b->partsInWeapon[n],t);
            if(!blended)return 0;parts[n]=*blended;
        }
        const auto match=ProjectNativeRackParts(config_.phases[3].frames,
            std::span<const math::Matrix4>(parts.data(),pull.partBones.size()),config_.rotationWeight);
        return match?match->progress:0;
    }
    void StartReturn(std::int64_t now)noexcept {
        // At the untouched rear stop preserve the full original released tail.
        // After any real held forward travel, skip only the already-travelled
        // portion by matching its current part pose onto that same native tail.
        const float matched=phase_==NativeRackPhase::Rear&&progress_<.99999f?MatchedReturnProgress():0;
        phase_=phase_==NativeRackPhase::Rear?NativeRackPhase::Return:phase_==NativeRackPhase::Pull?NativeRackPhase::CancelPull:NativeRackPhase::CancelPrep;
        if(phase_==NativeRackPhase::Return)progress_=matched;
        returnStart_=now;returnFrom_=progress_;guard_.Reset();
    }
    NativeRackResult AdvanceReturn(std::int64_t now)noexcept {
        if(now<returnStart_)return Abort();
        const auto* p=Result().pose;
        const double duration=double(p->frames.back().timeSeconds-p->frames.front().timeSeconds)*1e9;
        const float elapsed=float(double(now-returnStart_)/duration);
        progress_=phase_==NativeRackPhase::Return?std::min(1.f,returnFrom_+elapsed):std::max(0.f,returnFrom_-elapsed);
        if((phase_==NativeRackPhase::Return&&progress_>=1)||(phase_!=NativeRackPhase::Return&&progress_<=0)){
            if(phase_==NativeRackPhase::CancelPull){phase_=NativeRackPhase::CancelPrep;progress_=returnFrom_=1;returnStart_=now;}
            else {const bool closed=phase_==NativeRackPhase::Return;Reset();auto r=Result();r.closed=closed;return r;}
        }
        return Result();
    }
    NativeRackConfig config_;std::array<OwnedPhase,4> owned_;std::array<NativeActionPoseProfile,4> profiles_;
    bool valid_=false,neutral_=false,geometryPaused_=false;
    NativeRackPhase phase_=NativeRackPhase::Idle;NativeRackIdentity identity_;
    std::size_t cursor_=0;float progress_=0,returnFrom_=0;
    std::uint64_t sequence_=0;std::int64_t lastNs_=0,returnStart_=0;
    math::Matrix4 lastWrist_{};interaction::MotionGuard guard_;
};
}
