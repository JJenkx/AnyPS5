#include "AudioOutSdl.hpp"
#include "SDL.h"

#include <mutex>

bool AudioOutInitSdl_nid_no_patch() {
    static std::mutex mutex;
    static bool initialized = false;
    std::lock_guard lock(mutex);
    if (initialized) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;
    initialized = true;
    return true;
}
