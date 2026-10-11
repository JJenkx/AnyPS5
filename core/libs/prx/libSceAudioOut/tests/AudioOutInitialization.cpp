#include "SceTypes.hpp"
#include "SDL.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

extern "C" {
int APS5_VABI sceAudioOutOpen(int, int, int, std::uint32_t, std::uint32_t, std::uint32_t);
int APS5_VABI sceAudioOutClose(int);
int APS5_VABI sceAudioOut2ContextCreate(const AudioOut2ContextParam*, void*, std::size_t, AudioOut2ContextHandle*);
int APS5_VABI sceAudioOut2ContextDestroy(AudioOut2ContextHandle);
}

namespace {

std::mutex mutex;
std::condition_variable changed;
int calls = 0;
int active = 0;
int peak = 0;
bool firstEntered = false;
bool secondStarted = false;

void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

void OpenLegacy() {
    const int port = sceAudioOutOpen(0x10000000, 0, 0, 256, 48000, 4);
    Require(port > 0, "legacy port creation failed");
    Require(sceAudioOutClose(port) == 0, "legacy port close failed");
}

void OpenContext() {
    AudioOut2ContextParam params{};
    params.num_grains = 256;
    params.queue_depth = 2;
    AudioOut2ContextHandle context = 0;
    Require(sceAudioOut2ContextCreate(&params, nullptr, 0, &context) == 0, "context creation failed");
    Require(sceAudioOut2ContextDestroy(context) == 0, "context destruction failed");
}

}

extern "C" int SDLCALL SDL_InitSubSystem(Uint32 flags) {
    Require(flags == SDL_INIT_AUDIO, "unexpected SDL subsystem");
    std::unique_lock lock(mutex);
    const int call = ++calls;
    ++active;
    if (active > peak) peak = active;
    changed.notify_all();
    if (call == 1) {
        firstEntered = true;
        changed.notify_all();
        Require(changed.wait_for(lock, std::chrono::seconds(2), [] { return secondStarted; }), "second caller did not start");
        changed.wait_for(lock, std::chrono::milliseconds(250), [] { return peak > 1; });
    }
    --active;
    return call == 1 ? -1 : 0;
}

extern "C" SDL_AudioDeviceID SDLCALL SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int) {
    return 0;
}

int main(int argc, char** argv) {
    const bool legacyFirst = argc == 2 && std::string_view(argv[1]) == "legacy";
    std::thread first(legacyFirst ? OpenLegacy : OpenContext);
    {
        std::unique_lock lock(mutex);
        Require(changed.wait_for(lock, std::chrono::seconds(2), [] { return firstEntered; }), "first caller did not enter SDL");
    }
    std::thread second([&] {
        {
            std::lock_guard lock(mutex);
            secondStarted = true;
            changed.notify_all();
        }
        if (legacyFirst) OpenContext();
        else OpenLegacy();
    });
    first.join();
    second.join();
    Require(peak == 1, "legacy and AudioOut2 initialized SDL concurrently");
    Require(calls == 2, "failed SDL initialization was not retried");
    std::vector<std::thread> callers;
    for (int i = 0; i < 8; ++i) callers.emplace_back(i % 2 == 0 ? OpenLegacy : OpenContext);
    for (auto& caller : callers) caller.join();
    Require(calls == 2, "successful SDL initialization was repeated");
    return 0;
}
