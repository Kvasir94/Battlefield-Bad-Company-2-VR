#include "Test.h"
#include "fvr/ipc/RemoteFrameProvider.h"
#include <Windows.h>
using namespace fvr;
void Ticket(const ipc::FrameRequest& request,graphics::TextureDescriptor& d,graphics::PairTicket& t){
    d={};d.width=request.width;d.height=request.height;d.format=request.format;d.adapterLow=request.adapterLow;d.adapterHigh=request.adapterHigh;d.resourceEpoch=1;d.session[0]=1;
    t={};t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=request.requestId;t.frameId=request.requestId;
    t.spaceGeneration=request.spaceGeneration;t.trackingGeneration=request.trackingGeneration;t.predictedNs=request.predictedNs;
}
int main(){
    ipc::RemoteFrameProvider host;CHECK(host.Create());CHECK(host.SetBudget(50));ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame now{};
    now.generation=0x100000005;now.spaceGeneration=0x200000007;now.predictedNs=1000000000;now.focused=now.headValid=true;
    now.eyes[0].position.x=-.032f;now.eyes[1].position.x=.032f;now.fov={math::FovTangents{-1.2f,.8f,1,-1},math::FovTangents{-.8f,1.2f,1,-1}};
    runtime::TrackingFrame rendered;graphics::TextureDescriptor d;graphics::PairTicket ticket;ipc::FrameRequest request;
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    // Controls update while the immutable render request stays in flight.
    interaction::InputFrame input{};input.generation=1;input.spaceGeneration=now.spaceGeneration;input.predictedNs=now.predictedNs;input.focused=input.headValid=true;
    input.hands[1].active=interaction::Trigger;input.hands[1].trigger=.9f;host.UpdateInput(input);
    std::int64_t inputDeadline=0;interaction::InputFrame controls{};CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Ok&&controls.hands[1].trigger==.9f);
    CHECK(inputDeadline>0);const auto originalDeadline=inputDeadline;CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Ok&&inputDeadline==originalDeadline);
    input.focused=false;input.hands={};++input.generation;host.UpdateInput(input);
    CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Ok&&!controls.focused&&!controls.hands[1].trigger);
    const auto original=now;++now.generation;now.predictedNs+=11000000;now.eyes[0].position.x+=.1f;
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket)); // No CPU wait or request replacement.
    graphics::TextureDescriptor source;graphics::PairTicket ready;Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(rendered.generation==original.generation);
    CHECK(rendered.eyes[0].position.x==original.eyes[0].position.x);CHECK(runtime::PairMatchesFrame(req,rendered,d,ticket));
    CHECK(!runtime::PairMatchesFrame(req,now,d,ticket)); // The new head pose cannot label old pixels.
    Sleep(60);CHECK(producer.PollOutcome()==ipc::Outcome::Pending);host.PairConsumed(ticket,true);
    // Already queued from completion-time tracking: no additional XR frame call.
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy);
    CHECK(host.Statistics().requested==2);
    ++now.spaceGeneration;CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));
    // A recenter replaces CPU metadata while preserving the old consumed lease.
    CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(request.trackingGeneration==now.generation&&request.predictedNs==now.predictedNs&&request.spaceGeneration==now.spaceGeneration);
    // Recenter while the producer is rendering cancels without publishing stale views.
    ++now.spaceGeneration;CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));Ticket(request,source,ready);
    CHECK(producer.Publish(source,ready)!=ipc::ChannelResult::Ok);
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(request.spaceGeneration==now.spaceGeneration);Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    host.Suspend();CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(producer.Skip()==ipc::ChannelResult::Ok);CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));
    host.Suspend();producer.Close();CHECK(!host.TryGetCompletedPair(req,now,rendered,d,ticket));
    // Split API never cancels a delivered GPU pair on deadline or CancelRequest.
    ipc::FrameChannel channel;CHECK(channel.CreateHost());CHECK(producer.ConnectProducer(channel.Token()));
    CHECK(channel.BeginRequest(req,now,50)==ipc::ChannelResult::Ok);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);Ticket(request,source,ready);
    CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Ok);
    CHECK(channel.CancelRequest()==ipc::ChannelResult::Busy);Sleep(60);CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Busy);
    CHECK(producer.PollOutcome()==ipc::Outcome::Pending);CHECK(channel.Feedback(ticket,false)==ipc::ChannelResult::Ok);CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    CHECK(channel.BeginRequest(req,now,1)==ipc::ChannelResult::Ok);Sleep(10);CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Timeout);
    CHECK(channel.BeginRequest(req,now,50)==ipc::ChannelResult::Ok);CHECK(channel.CancelRequest()==ipc::ChannelResult::Ok);
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy);
    CHECK(channel.BeginRequest(req,now,0)==ipc::ChannelResult::Invalid);
    CHECK(channel.BeginRequest(req,now,201)==ipc::ChannelResult::Invalid);
    CHECK(channel.RequestPair(req,now,51,d,ticket)==ipc::ChannelResult::Invalid);
    CHECK(channel.BeginRequest(req,now,200)==ipc::ChannelResult::Ok);CHECK(channel.CancelRequest()==ipc::ChannelResult::Ok);
    // Queue only after acknowledgment; duplicates cannot consume the successor.
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Ok);
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Busy);
    CHECK(channel.Feedback(ticket,true)==ipc::ChannelResult::Ok);
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);
    CHECK(channel.Feedback(ticket,false)==ipc::ChannelResult::Invalid);
    CHECK(channel.PollRequest(d,ready)==ipc::ChannelResult::Busy);
    CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(channel.CancelRequest()==ipc::ChannelResult::Ok);CHECK(producer.Skip()==ipc::ChannelResult::Ok);
    // Suspend cancels a queued successor without erasing the old GPU outcome.
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Ok);CHECK(channel.Feedback(ticket,true)==ipc::ChannelResult::Ok);
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);CHECK(channel.CancelRequest()==ipc::ChannelResult::Ok);
    CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy);
    // Expiring while queued still reports the prior pair's consumed outcome.
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Ok);CHECK(channel.Feedback(ticket,true)==ipc::ChannelResult::Ok);
    CHECK(channel.BeginRequest(req,now,1)==ipc::ChannelResult::Ok);Sleep(10);
    CHECK(channel.PollRequest(d,ready)==ipc::ChannelResult::Timeout);
    CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy);
    producer.Close();channel.Close();
    // A native multi-frame handoff can exceed 50 ms at 60 Hz. A short blocking
    // budget must not kill this nonblocking request, or relabel its older pose.
    ipc::RemoteFrameProvider delayed;CHECK(delayed.Create());CHECK(delayed.SetBudget(1));
    CHECK(delayed.RequestLifetimeMs()==150);CHECK(!delayed.SetRequestLifetime(0));CHECK(!delayed.SetRequestLifetime(201));
    CHECK(producer.ConnectProducer(delayed.Token()));
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    const auto requestedPose=now;Sleep(80);++now.generation;now.predictedNs+=88000000;now.eyes[0].position.x+=.2f;
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(runtime::PairMatchesFrame(req,rendered,d,ticket));
    CHECK(rendered.generation==requestedPose.generation&&rendered.eyes[0].position.x==requestedPose.eyes[0].position.x);
    CHECK(!runtime::PairMatchesFrame(req,now,d,ticket));
    CHECK(delayed.Statistics().completedAfter50Ms==1&&delayed.Statistics().completed==1&&delayed.Statistics().timeouts==0);
    CHECK(delayed.SetRequestLifetime(10));delayed.PairConsumed(ticket,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    // A genuinely expired request still rejects its completed image and releases
    // producer ownership; extending the async lifetime did not remove cancellation.
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);Sleep(20);
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(delayed.Statistics().timeouts==1);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)!=ipc::ChannelResult::Ok);
    // Stale controls expire independently; no frame request is needed to release them.
    input.generation=99;delayed.UpdateInput(input);CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Ok);
    Sleep(120);CHECK(producer.ReadInput(controls,&inputDeadline)==ipc::ChannelResult::Timeout&&controls.generation==0&&inputDeadline==0);
    CHECK(delayed.SetRequestLifetime(150));delayed.Suspend();
    CHECK(!delayed.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(delayed.TryGetCompletedPair(req,now,rendered,d,ticket));delayed.PairConsumed(ticket,true);
    delayed.Suspend();CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy);
    delayed.Close();CHECK(producer.ReadInput(controls)==ipc::ChannelResult::Closed);producer.Close();
    // A deferred GPU acknowledgment schedules the newest valid tracking sample,
    // while the delivered image retains its original immutable render pose.
    ipc::RemoteFrameProvider gpu;CHECK(gpu.Create());CHECK(producer.ConnectProducer(gpu.Token()));
    CHECK(!gpu.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    const auto gpuOriginal=now;Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(gpu.TryGetCompletedPair(req,now,rendered,d,ticket));
    ++now.generation;now.predictedNs+=11000000;now.eyes[0].position.x+=.3f;
    gpu.PrepareFrame(req,now);CHECK(producer.PollOutcome()==ipc::Outcome::Pending);
    CHECK(rendered.generation==gpuOriginal.generation&&rendered.eyes[0].position.x==gpuOriginal.eyes[0].position.x);
    gpu.PairConsumed(ticket,true);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Ok);
    CHECK(request.trackingGeneration==now.generation&&request.predictedNs==now.predictedNs);
    Ticket(request,source,ready);CHECK(producer.Publish(source,ready)==ipc::ChannelResult::Ok);
    CHECK(gpu.TryGetCompletedPair(req,now,rendered,d,ticket));
    ++now.spaceGeneration;gpu.PrepareFrame(req,now); // Recenter cancels successor, not the held GPU source.
    CHECK(producer.PollOutcome()==ipc::Outcome::Pending);gpu.PairConsumed(ticket,true);
    CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy);
    gpu.Close();producer.Close();
    // Pipeline depth 2: the producer renders N+1 while the host still owns N, and a
    // depth attachment travels only with the exact pair that carried it.
    ipc::RemoteFrameProvider deep;CHECK(deep.SetPipelineDepth(2));CHECK(!deep.SetPipelineDepth(5));CHECK(deep.Create());deep.RequestDepth(true);
    CHECK(producer.ConnectProducer(deep.Token()));++now.generation;now.predictedNs+=11000000;
    CHECK(!deep.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(!deep.TryGetCompletedPair(req,now,rendered,d,ticket)); // Never stack an untaken request.
    CHECK(deep.Statistics().requested==1);
    ipc::FrameRequest first;CHECK(producer.TryTake(first)==ipc::ChannelResult::Ok);CHECK((first.flags&ipc::FrameDepthRequested)!=0);
    CHECK(producer.TryTake(request)==ipc::ChannelResult::Busy); // One render at a time.
    ++now.generation;now.predictedNs+=11000000;
    CHECK(!deep.TryGetCompletedPair(req,now,rendered,d,ticket)); // Overlaps the frame being rendered.
    CHECK(deep.Statistics().requested==2&&deep.Statistics().overlapped==1);
    graphics::TextureDescriptor firstSource;graphics::PairTicket firstReady;Ticket(first,firstSource,firstReady);
    graphics::DepthAttachment depth{};depth.descriptor=firstSource;depth.descriptor.format=graphics::DepthFormat;depth.descriptor.session[0]=2;
    depth.ticket=firstReady;depth.ticket.session=depth.descriptor.session;depth.nearFar={.1f,500.f,.1f,500.f};depth.present=1;
    CHECK(graphics::ValidDepth(depth,firstSource,firstReady));
    auto badDepth=depth;badDepth.ticket.predictedNs+=1;CHECK(!graphics::ValidDepth(badDepth,firstSource,firstReady));
    badDepth=depth;badDepth.nearFar[3]=.05f;CHECK(!graphics::ValidDepth(badDepth,firstSource,firstReady));
    CHECK(producer.Publish(firstSource,firstReady,nullptr,nullptr,&depth)==ipc::ChannelResult::Ok);
    ipc::FrameRequest second;CHECK(producer.TryTake(second)==ipc::ChannelResult::Ok);CHECK(second.requestId>first.requestId);
    CHECK(producer.PollOutcome(first.requestId)==ipc::Outcome::Pending);
    CHECK(deep.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(ticket.frameId==first.requestId);
    graphics::DepthAttachment received{};CHECK(deep.ReadDepth(ticket,received)&&received.nearFar[1]==500.f&&received.descriptor.session[0]==2);
    graphics::TextureDescriptor secondSource;graphics::PairTicket secondReady;Ticket(second,secondSource,secondReady);
    secondSource.session[0]=3;secondReady.session=secondSource.session;
    CHECK(producer.Publish(secondSource,secondReady)==ipc::ChannelResult::Ok); // While the host still holds the first pair.
    deep.PairConsumed(ticket,true);CHECK(!deep.ReadDepth(ticket,received)&&!received.present);
    CHECK(producer.PollOutcome(first.requestId)==ipc::Outcome::Consumed);
    CHECK(deep.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(ticket.frameId==second.requestId&&!deep.ReadDepth(ticket,received));
    deep.PairConsumed(ticket,false);CHECK(producer.PollOutcome(second.requestId)==ipc::Outcome::Discarded);
    deep.Suspend();deep.Close();producer.Close();
    // Cancel two slots while one is delivered: the second publication is discarded,
    // but the consumer's first depth/props lease stays readable until feedback.
    CHECK(channel.CreateHost());channel.SetPipelineDepth(2);channel.SetDepthRequested(true);
    CHECK(producer.ConnectProducer(channel.Token()));
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);
    CHECK(producer.TryTake(first)==ipc::ChannelResult::Ok);Ticket(first,firstSource,firstReady);
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);
    depth.descriptor=firstSource;depth.descriptor.format=graphics::DepthFormat;depth.descriptor.session[0]=2;
    depth.ticket=firstReady;depth.ticket.session=depth.descriptor.session;
    graphics::BodyPropFrame props{};props.frameId=firstReady.frameId;props.spaceGeneration=firstReady.spaceGeneration;props.trackingGeneration=firstReady.trackingGeneration;
    CHECK(producer.Publish(firstSource,firstReady,&props,nullptr,&depth)==ipc::ChannelResult::Ok);
    CHECK(producer.TryTake(second)==ipc::ChannelResult::Ok);Ticket(second,secondSource,secondReady);
    CHECK(producer.Publish(secondSource,secondReady)==ipc::ChannelResult::Ok);
    CHECK(channel.PollRequest(d,ticket)==ipc::ChannelResult::Ok);
    CHECK(channel.CancelRequest()==ipc::ChannelResult::Busy);
    CHECK(producer.PollOutcome(second.requestId)==ipc::Outcome::Discarded);
    CHECK(producer.PollOutcome(first.requestId)==ipc::Outcome::Pending);
    graphics::BodyPropFrame gotProps{};
    CHECK(channel.ReadDepth(ticket,received)&&channel.ReadBodyProps(ticket,gotProps));
    CHECK(channel.Feedback(ticket,true)==ipc::ChannelResult::Ok);
    CHECK(!channel.ReadDepth(ticket,received)&&!channel.ReadBodyProps(ticket,gotProps));
    CHECK(producer.PollOutcome(first.requestId)==ipc::Outcome::Consumed);
    CHECK(channel.Feedback(ticket,false)==ipc::ChannelResult::Invalid);
    // Both slots are reusable after cancellation and explicit feedback.
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);
    CHECK(producer.TryTake(first)==ipc::ChannelResult::Ok);
    CHECK(channel.BeginRequest(req,now,150)==ipc::ChannelResult::Ok);
    CHECK(channel.CancelRequest()==ipc::ChannelResult::Ok);
    CHECK(producer.Skip()==ipc::ChannelResult::Ok);
    CHECK(producer.TryTake(second)==ipc::ChannelResult::Busy);
    channel.Close();producer.Close();
    // Depth 4: one untaken request is always queued, also while the host holds a
    // delivered pair, so every game frame finds one; never two untaken at once.
    ipc::RemoteFrameProvider full;CHECK(full.SetPipelineDepth(4));CHECK(full.Create());CHECK(producer.ConnectProducer(full.Token()));
    ++now.generation;now.predictedNs+=11000000;
    CHECK(!full.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(!full.TryGetCompletedPair(req,now,rendered,d,ticket));
    CHECK(full.Statistics().requested==1&&!full.CompletedPairReady());
    ipc::FrameRequest a,b,c;CHECK(producer.TryTake(a)==ipc::ChannelResult::Ok);
    CHECK(!full.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(full.Statistics().requested==2);
    CHECK(producer.TryTake(b)==ipc::ChannelResult::Busy); // Still one render at a time.
    graphics::TextureDescriptor aSource;graphics::PairTicket aReady;Ticket(a,aSource,aReady);CHECK(producer.Publish(aSource,aReady)==ipc::ChannelResult::Ok);
    CHECK(full.CompletedPairReady());CHECK(producer.TryTake(b)==ipc::ChannelResult::Ok);
    CHECK(full.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(ticket.frameId==a.requestId);
    CHECK(full.Statistics().requested==3&&full.Statistics().queuedWhileDelivered==1); // Queued while A is delivered.
    CHECK(!full.CompletedPairReady()); // A's copy is still pending feedback.
    graphics::TextureDescriptor bSource;graphics::PairTicket bReady;Ticket(b,bSource,bReady);bSource.session[0]=3;bReady.session=bSource.session;
    CHECK(producer.Publish(bSource,bReady)==ipc::ChannelResult::Ok);CHECK(producer.TryTake(c)==ipc::ChannelResult::Ok);
    full.PairConsumed(ticket,true);CHECK(producer.PollOutcome(a.requestId)==ipc::Outcome::Consumed);
    CHECK(full.CompletedPairReady());CHECK(full.TryGetCompletedPair(req,now,rendered,d,ticket));CHECK(ticket.frameId==b.requestId);
    CHECK(full.Statistics().requested==4);full.PairConsumed(ticket,true);
    full.Suspend();full.Close();producer.Close();return 0;
}
