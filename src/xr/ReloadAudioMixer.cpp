#include "fvr/xr/ReloadAudioMixer.h"
#include <Windows.h>
#include <xaudio2.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <vector>
namespace fvr::xr {
struct ReloadAudioMixer::State {
    struct EngineEvents final:IXAudio2EngineCallback {
        std::atomic<HRESULT> fault{S_OK};
        void STDMETHODCALLTYPE OnProcessingPassStart()override{}
        void STDMETHODCALLTYPE OnProcessingPassEnd()override{}
        void STDMETHODCALLTYPE OnCriticalError(HRESULT hr)override{fault.store(hr);}
    } callbacks;
    HMODULE module=nullptr;IXAudio2* engine=nullptr;IXAudio2MasteringVoice* master=nullptr;bool com=false;
    struct Voice {IXAudio2SourceVoice* source=nullptr;ipc::ReloadAudioEvent event{};bool active=false;};
    std::array<Voice,8> voices{};std::array<std::vector<char>,9> cues;unsigned variant=0;
    ~State(){if(engine)engine->UnregisterForCallbacks(&callbacks);for(auto& v:voices)if(v.source)v.source->DestroyVoice();if(master)master->DestroyVoice();if(engine)engine->Release();if(module)FreeLibrary(module);if(com)CoUninitialize();}
    static bool ReadPcm(const std::filesystem::path& path,std::vector<char>& pcm){
        std::ifstream file(path,std::ios::binary);std::array<unsigned char,44> h{};
        if(!file.read(reinterpret_cast<char*>(h.data()),h.size()))return false;
        const auto u16=[&](unsigned n){return unsigned(h[n])|unsigned(h[n+1])<<8;};
        const auto u32=[&](unsigned n){return std::uint32_t(h[n])|std::uint32_t(h[n+1])<<8|std::uint32_t(h[n+2])<<16|std::uint32_t(h[n+3])<<24;};
        const auto size=u32(40);
        if(std::memcmp(h.data(),"RIFF",4)||std::memcmp(h.data()+8,"WAVEfmt ",8)||std::memcmp(h.data()+36,"data",4)
            ||u32(16)!=16||u16(20)!=1||u16(22)!=1||u32(24)!=48000||u32(28)!=96000||u16(32)!=2||u16(34)!=16
            ||!size||size>288000||size%2||u32(4)!=size+36)return false;
        pcm.resize(size);return bool(file.read(pcm.data(),size))&&file.peek()==std::char_traits<char>::eof();
    }
    HRESULT Position(Voice& voice,const math::Pose& head)noexcept {
        math::Pose emitter{};emitter.position={voice.event.emitterX,voice.event.emitterY,voice.event.emitterZ};
        const auto relative=math::MakeRelativePose(head,emitter);if(!relative)return E_INVALIDARG;
        const auto p=relative->position;const float distance=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
        const float pan=std::clamp(p.x/(std::max)(distance,.1f),-1.f,1.f);
        const float gain=voice.event.gain/(std::max)(1.f,distance/.7f);
        const float matrix[]{gain*std::sqrt((1-pan)*.5f),gain*std::sqrt((1+pan)*.5f)};
        return voice.source->SetOutputMatrix(master,1,2,matrix);
    }
};
ReloadAudioMixer::ReloadAudioMixer()=default;ReloadAudioMixer::~ReloadAudioMixer()=default;
bool ReloadAudioMixer::Initialize(const std::filesystem::path& directory,const std::wstring& endpoint)noexcept {
    state_.reset();stats_={};try {
        auto s=std::make_unique<State>();
        static constexpr const wchar_t* names[]{L"xm8_mag_out.wav",L"xm8_mag_in.wav",L"xm8_handle_back.wav",L"xm8_handle_forward.wav",L"spas_shell_1_cue.wav",L"spas_pump_back.wav",L"spas_pump_forward.wav",L"spas_shell_2_cue.wav",L"spas_shell_3_cue.wav"};
        for(unsigned i=0;i<s->cues.size();++i)if(!State::ReadPcm(directory/names[i],s->cues[i])){stats_.error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);return false;}
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);s->com=SUCCEEDED(com);
        if(FAILED(com)&&com!=RPC_E_CHANGED_MODE){stats_.error=com;return false;}
        s->module=LoadLibraryExW(L"xaudio2_9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!s->module){stats_.error=HRESULT_FROM_WIN32(GetLastError());return false;}
        using Create=HRESULT(WINAPI*)(IXAudio2**,UINT32,XAUDIO2_PROCESSOR);
        const auto create=reinterpret_cast<Create>(GetProcAddress(s->module,"XAudio2Create"));if(!create){stats_.error=E_NOINTERFACE;return false;}
        auto hr=create(&s->engine,0,XAUDIO2_DEFAULT_PROCESSOR);if(FAILED(hr)){stats_.error=hr;return false;}
        hr=s->engine->RegisterForCallbacks(&s->callbacks);if(FAILED(hr)){stats_.error=hr;return false;}
        hr=s->engine->CreateMasteringVoice(&s->master,2,48000,0,endpoint.empty()?nullptr:endpoint.c_str(),nullptr,AudioCategory_GameEffects);
        if(FAILED(hr)){stats_.error=hr;return false;}
        WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=1;f.nSamplesPerSec=48000;f.wBitsPerSample=16;f.nBlockAlign=2;f.nAvgBytesPerSec=96000;
        for(auto& voice:s->voices){hr=s->engine->CreateSourceVoice(&voice.source,&f,XAUDIO2_VOICE_NOSRC,1.f);if(FAILED(hr)){stats_.error=hr;return false;}}
        state_=std::move(s);return true;
    }catch(...){stats_.error=E_FAIL;return false;}
}
void ReloadAudioMixer::Stop()noexcept {
    if(!state_)return;bool stopped=false;
    for(auto& voice:state_->voices)if(voice.active){voice.source->Stop();voice.source->FlushSourceBuffers();voice.active=false;stopped=true;}
    if(stopped)++stats_.stops;
}
void ReloadAudioMixer::UpdateHead(const math::Pose& head,const math::Vec3* currentEmitter)noexcept {
    if(!state_)return;
    const auto fault=state_->callbacks.fault.load();if(FAILED(fault)){stats_.error=fault;Stop();return;}
    for(auto& voice:state_->voices)if(voice.active){XAUDIO2_VOICE_STATE status{};voice.source->GetState(&status,XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if(!status.BuffersQueued){voice.active=false;continue;}
        if(currentEmitter){voice.event.emitterX=currentEmitter->x;voice.event.emitterY=currentEmitter->y;voice.event.emitterZ=currentEmitter->z;}
        const auto hr=state_->Position(voice,head);if(FAILED(hr)){stats_.error=hr;Stop();return;}}
}
bool ReloadAudioMixer::Play(const ipc::ReloadAudioEvent& event,const math::Pose& head)noexcept {
    if(!state_||unsigned(event.role)>=unsigned(ipc::ReloadAudioRole::Count)){++stats_.dropped;return false;}
    if(FAILED(state_->callbacks.fault.load())){stats_.error=state_->callbacks.fault.load();++stats_.dropped;Stop();return false;}
    UpdateHead(head);auto& s=*state_;
    for(auto& voice:s.voices)if(!voice.active){
        XAUDIO2_VOICE_STATE status{};voice.source->GetState(&status,XAUDIO2_VOICE_NOSAMPLESPLAYED);if(status.BuffersQueued)continue;
        unsigned cue=unsigned(event.role);if(event.role==ipc::ReloadAudioRole::ShellIn){constexpr unsigned variants[]{4,7,8};cue=variants[s.variant++%3];}
        voice.event=event;auto hr=s.Position(voice,head);if(FAILED(hr)){stats_.error=hr;++stats_.dropped;return false;}
        XAUDIO2_BUFFER buffer{};buffer.Flags=XAUDIO2_END_OF_STREAM;buffer.AudioBytes=static_cast<UINT32>(s.cues[cue].size());buffer.pAudioData=reinterpret_cast<const BYTE*>(s.cues[cue].data());
        hr=voice.source->SubmitSourceBuffer(&buffer);if(SUCCEEDED(hr))hr=voice.source->Start();
        if(FAILED(hr)){voice.source->Stop();voice.source->FlushSourceBuffers();stats_.error=hr;++stats_.dropped;return false;}
        voice.active=true;++stats_.played;return true;
    }
    ++stats_.dropped;return false;
}
}
