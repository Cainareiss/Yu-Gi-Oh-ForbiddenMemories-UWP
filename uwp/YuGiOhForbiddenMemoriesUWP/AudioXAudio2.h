#pragma once

#include <xaudio2.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <atomic>
#include <thread>

typedef void (*MemoriesAudioMixCallback)(int16_t *frames, size_t count);

class AudioXAudio2 : public IXAudio2VoiceCallback
{
public:
    AudioXAudio2();
    ~AudioXAudio2();

    bool Initialize(MemoriesAudioMixCallback mixCallback);
    void Shutdown();
    void SetVolume(float volume);

    // IXAudio2VoiceCallback implementation
    STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32 BytesRequired) override {}
    STDMETHOD_(void, OnVoiceProcessingPassEnd)() override {}
    STDMETHOD_(void, OnStreamEnd)() override {}
    STDMETHOD_(void, OnBufferStart)(void* pBufferContext) override {}
    STDMETHOD_(void, OnBufferEnd)(void* pBufferContext) override;
    STDMETHOD_(void, OnLoopEnd)(void* pBufferContext) override {}
    STDMETHOD_(void, OnVoiceError)(void* pBufferContext, HRESULT Error) override {}

private:
    static const int BufferCount = 4;
    static const int FramesPerBuffer = 1024; // ~23 ms at 44.1 kHz

    Microsoft::WRL::ComPtr<IXAudio2> m_xaudio2;
    IXAudio2MasteringVoice* m_masterVoice = nullptr;
    IXAudio2SourceVoice* m_sourceVoice = nullptr;

    std::vector<int16_t> m_buffers[BufferCount];
    HANDLE m_bufferEvent = nullptr;
    std::atomic<bool> m_running{ false };
    std::thread m_audioThread;
    MemoriesAudioMixCallback m_mixCallback = nullptr;

    void AudioWorker();
};
