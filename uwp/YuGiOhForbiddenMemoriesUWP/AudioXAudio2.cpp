#include "AudioXAudio2.h"

AudioXAudio2::AudioXAudio2()
{
}

AudioXAudio2::~AudioXAudio2()
{
    Shutdown();
}

void AudioXAudio2::OnBufferEnd(void* pBufferContext)
{
    if (m_bufferEvent)
    {
        SetEvent(m_bufferEvent);
    }
}

bool AudioXAudio2::Initialize(MemoriesAudioMixCallback mixCallback)
{
    m_mixCallback = mixCallback;

    if (FAILED(XAudio2Create(&m_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
    {
        return false;
    }

    if (FAILED(m_xaudio2->CreateMasteringVoice(&m_masterVoice)))
    {
        return false;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    if (FAILED(m_xaudio2->CreateSourceVoice(&m_sourceVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this)))
    {
        return false;
    }

    for (int i = 0; i < BufferCount; i++)
    {
        m_buffers[i].resize(FramesPerBuffer * 2, 0);
    }

    m_bufferEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_bufferEvent) return false;

    m_running = true;
    m_audioThread = std::thread(&AudioXAudio2::AudioWorker, this);

    m_sourceVoice->Start(0);
    return true;
}

void AudioXAudio2::AudioWorker()
{
    int bufferIndex = 0;

    // Pre-fill and submit initial buffers
    for (int i = 0; i < BufferCount; i++)
    {
        if (m_mixCallback)
        {
            m_mixCallback(m_buffers[i].data(), FramesPerBuffer);
        }

        XAUDIO2_BUFFER xBuffer = {};
        xBuffer.AudioBytes = static_cast<UINT32>(m_buffers[i].size() * sizeof(int16_t));
        xBuffer.pAudioData = reinterpret_cast<const BYTE*>(m_buffers[i].data());
        xBuffer.pContext = (void*)(uintptr_t)i;
        m_sourceVoice->SubmitSourceBuffer(&xBuffer);
    }

    while (m_running)
    {
        XAUDIO2_VOICE_STATE state;
        m_sourceVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

        while (state.BuffersQueued < BufferCount && m_running)
        {
            if (m_mixCallback)
            {
                m_mixCallback(m_buffers[bufferIndex].data(), FramesPerBuffer);
            }
            else
            {
                std::fill(m_buffers[bufferIndex].begin(), m_buffers[bufferIndex].end(), (int16_t)0);
            }

            XAUDIO2_BUFFER xBuffer = {};
            xBuffer.AudioBytes = static_cast<UINT32>(m_buffers[bufferIndex].size() * sizeof(int16_t));
            xBuffer.pAudioData = reinterpret_cast<const BYTE*>(m_buffers[bufferIndex].data());
            xBuffer.pContext = (void*)(uintptr_t)bufferIndex;
            m_sourceVoice->SubmitSourceBuffer(&xBuffer);

            bufferIndex = (bufferIndex + 1) % BufferCount;
            m_sourceVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        }

        WaitForSingleObject(m_bufferEvent, 10);
    }
}

void AudioXAudio2::SetVolume(float volume)
{
    if (m_sourceVoice)
    {
        m_sourceVoice->SetVolume(volume);
    }
}

void AudioXAudio2::Shutdown()
{
    m_running = false;
    if (m_bufferEvent)
    {
        SetEvent(m_bufferEvent);
    }

    if (m_audioThread.joinable())
    {
        m_audioThread.join();
    }

    if (m_sourceVoice)
    {
        m_sourceVoice->Stop(0);
        m_sourceVoice->DestroyVoice();
        m_sourceVoice = nullptr;
    }

    if (m_masterVoice)
    {
        m_masterVoice->DestroyVoice();
        m_masterVoice = nullptr;
    }

    if (m_bufferEvent)
    {
        CloseHandle(m_bufferEvent);
        m_bufferEvent = nullptr;
    }

    m_xaudio2.Reset();
}
