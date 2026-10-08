#include "AudioXAudio2.h"
#include <objbase.h>
#include <atomic>
#include <cassert>
#include <cstring>
#include <cstdio>

static std::atomic<int> workers{0}, starts{0};
extern "C" void UWP_LogMessage(const char* message)
{
    if (!strcmp(message, "[AUDIO] worker started")) ++workers;
    if (!strcmp(message, "[AUDIO] XAudio2 started")) ++starts;
}
static void Silence(int16_t* frames, size_t count)
{
    memset(frames, 0, count * 2 * sizeof(int16_t));
}
static void OtherMixer(int16_t* frames, size_t count) { Silence(frames, count); }

int main()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    assert(SUCCEEDED(hr));
    AudioXAudio2 audio;
    assert(audio.Initialize(Silence));
    for (int i = 0; i < 100; ++i) assert(audio.Initialize(Silence));
    assert(!audio.Initialize(OtherMixer)); // Existing callback must not be replaced.
    Sleep(100);
    assert(workers == 1 && starts == 1);
    audio.Shutdown();
    assert(audio.Initialize(Silence));
    Sleep(100);
    assert(workers == 2 && starts == 2);
    audio.Shutdown();
    CoUninitialize();
    std::puts("PASS: 100 duplicate audio starts create one worker/voice; conflicting mixer rejected; shutdown and restart succeed");
}
