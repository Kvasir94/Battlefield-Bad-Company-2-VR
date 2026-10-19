#include "Test.h"
#include "fvr/graphics/D3D11FrameBridge.h"
#include <dxgi.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
using namespace fvr;
using Microsoft::WRL::ComPtr;
int main(){
 ComPtr<ID3D11Device> producerDevice,consumerDevice;ComPtr<ID3D11DeviceContext> producerContext,consumerContext;
 D3D_FEATURE_LEVEL level{};const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,levels,1,D3D11_SDK_VERSION,&producerDevice,&level,&producerContext)));
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,D3D11_SDK_VERSION,&consumerDevice,&level,&consumerContext)));
 ComPtr<IDXGIDevice> dx;CHECK(SUCCEEDED(producerDevice.As(&dx)));ComPtr<IDXGIAdapter> adapter;CHECK(SUCCEEDED(dx->GetAdapter(&adapter)));DXGI_ADAPTER_DESC ad{};CHECK(SUCCEEDED(adapter->GetDesc(&ad)));
 runtime::PresentationRequirements req{1832,1920,29,ad.AdapterLuid.LowPart,ad.AdapterLuid.HighPart};
 ipc::FrameChannel host;CHECK(host.CreateHost());host.SetPipelineDepth(2);
 graphics::D3D11FrameBridge bridge;CHECK(bridge.ConnectGraphics(producerDevice.Get(),host.Token(),graphics::FrameSharingPreference::LegacyIpc));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=req.width;desc.Height=req.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;
 ComPtr<ID3D11Texture2D> source;CHECK(SUCCEEDED(producerDevice->CreateTexture2D(&desc,nullptr,&source)));
 std::array<ComPtr<ID3D11Texture2D>,2> output;for(auto& eye:output)CHECK(SUCCEEDED(consumerDevice->CreateTexture2D(&desc,nullptr,&eye)));
 desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(consumerDevice->CreateTexture2D(&desc,nullptr,&staging)));
 std::vector<unsigned> pixels(std::size_t(req.width)*req.height);unsigned frame=0,checked=0;
 auto pattern=[](unsigned generation,unsigned eye,unsigned x,unsigned y){return 0xff000000u|((generation*19+x)%256)|(((eye*97+y)%256)<<8)|((generation%256)<<16);};
 for(unsigned wave=0;wave<8;++wave){
  std::array<std::uint64_t,2> ids{};std::array<ipc::FrameLease,2> leases{};
  for(unsigned pair=0;pair<2;++pair){
   runtime::TrackingFrame tracking{};tracking.generation=++frame;tracking.spaceGeneration=1;tracking.predictedNs=1000000000LL+frame*1000000LL;tracking.headValid=tracking.focused=true;
   tracking.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
   CHECK(host.BeginRequest(req,tracking,200,&ids[pair])==ipc::ChannelResult::Ok);
   const auto deadline=GetTickCount64()+150;bool taken=false;
   while(!(taken=bridge.TryBegin({1,frame,1},leases[pair]))&&GetTickCount64()<deadline){bridge.PumpGraphics();SwitchToThread();}
   CHECK(taken);
   for(unsigned eye=0;eye<2;++eye){
    for(unsigned y=0;y<req.height;++y)for(unsigned x=0;x<req.width;++x)pixels[std::size_t(y)*req.width+x]=pattern(frame,eye,x,y);
    producerContext->UpdateSubresource(source.Get(),0,nullptr,pixels.data(),req.width*4,0);
    CHECK(bridge.CaptureEye(eye,source.Get(),leases[pair]));
   }
   CHECK(bridge.PublishRestored(leases[pair]));
  }
  // Both distinct exports are now owned. Rendering another frame may not reuse either.
  ipc::FrameLease third{};CHECK(!bridge.TryBegin({1,frame+1,1},third));
  CHECK(bridge.SecondPairCaptures()>0);
  for(unsigned pair=0;pair<2;++pair){
   graphics::TextureDescriptor wire{};graphics::PairTicket ticket{};ipc::ChannelResult result=ipc::ChannelResult::Busy;
   const auto deadline=GetTickCount64()+180;
   while((result=host.PollRequestId(ids[pair],wire,ticket))==ipc::ChannelResult::Busy&&GetTickCount64()<deadline){bridge.PumpGraphics();SwitchToThread();}
   CHECK(result==ipc::ChannelResult::Ok);CHECK(ticket.frameId==leases[pair].native.frameId);
   if(wave==3&&pair==0){CHECK(host.Feedback(ticket,false)==ipc::ChannelResult::Ok);bridge.PumpGraphics();continue;}
   graphics::D3D11PairConsumer consumer;CHECK(consumer.Open(consumerDevice.Get(),wire)==graphics::TransferResult::Ok);
   const std::array<graphics::TextureSlice,2> targets{{{output[0].Get(),0},{output[1].Get(),0}}};
   CHECK(consumer.QueueCopy(ticket,targets)==graphics::TransferResult::Ok);
   auto copied=graphics::CopyPollResult::Pending;const auto copyDeadline=GetTickCount64()+200;
   while((copied=consumer.PollCopy(ticket))==graphics::CopyPollResult::Pending&&GetTickCount64()<copyDeadline)SwitchToThread();
   CHECK(copied==graphics::CopyPollResult::Complete);
   for(unsigned eye=0;eye<2;++eye){
    consumerContext->CopyResource(staging.Get(),output[eye].Get());D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(consumerContext->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    for(unsigned y=0;y<req.height;++y){const auto* row=reinterpret_cast<const unsigned*>(static_cast<const char*>(map.pData)+std::size_t(y)*map.RowPitch);for(unsigned x=0;x<req.width;++x)CHECK(row[x]==pattern(unsigned(ticket.trackingGeneration),eye,x,y));}
    consumerContext->Unmap(staging.Get(),0);
   }
   CHECK(host.Feedback(ticket,true)==ipc::ChannelResult::Ok);bridge.PumpGraphics();++checked;
  }
 }
 bridge.PumpGraphics();CHECK(bridge.Consumed()==15&&bridge.Discarded()==1&&bridge.FailureStage()==0);
 printf("{\"pairs\":16,\"exact_pixel_pairs\":%u,\"second_pair_captures\":%llu,\"discard_recovered\":true,\"dimensions\":[1832,1920],\"headset_used\":false}\n",checked,bridge.SecondPairCaptures());
 bridge.CloseGraphics();return 0;
}
