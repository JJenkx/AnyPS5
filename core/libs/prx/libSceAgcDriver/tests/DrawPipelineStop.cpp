#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <string_view>

using AgcDriver::DriverDetail::DrawPipeline;
using namespace std::chrono_literals;

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::fflush(stderr);
    std::_Exit(1);
}

struct State {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    std::atomic<unsigned> destroyed{0};
};

struct Captured {
    std::shared_ptr<State> state;
    bool block;

    ~Captured() {
        if (block) {
            std::unique_lock lock(state->mutex);
            state->entered = true;
            state->changed.notify_all();
            state->changed.wait(lock, [&] { return state->release; });
        }
        ++state->destroyed;
    }
};

}

int main(int argc, char** argv) {
    Check(argc == 2, "expected active or tail");
    const std::string_view mode(argv[1]);
    Check(mode == "active" || mode == "tail", "unknown stop mode");
    auto state = std::make_shared<State>();
    auto& pipeline = DrawPipeline::Queue0();
    const unsigned count = mode == "active" ? 1 : 65;
    for (unsigned i = 0; i < count; ++i) {
        auto captured = std::shared_ptr<Captured>(new Captured{state, mode == "active"});
        pipeline.Enqueue([captured = std::move(captured)] {}, {});
    }
    if (mode == "active") {
        std::unique_lock lock(state->mutex);
        Check(state->changed.wait_for(lock, 3s, [&] { return state->entered; }), "closure destruction never started");
    }
    std::promise<void> stopping;
    auto started = stopping.get_future();
    auto stopped = std::async(std::launch::async, [&] {
        stopping.set_value();
        pipeline.Stop();
        return state->destroyed.load();
    });
    Check(started.wait_for(3s) == std::future_status::ready, "stop thread never started");
    const bool early = mode == "active" && stopped.wait_for(100ms) == std::future_status::ready;
    {
        std::lock_guard lock(state->mutex);
        state->release = true;
    }
    state->changed.notify_all();
    Check(stopped.wait_for(3s) == std::future_status::ready, "Stop did not finish after releasing the closure");
    const auto destroyedAtStop = stopped.get();
    Check(!early, "Stop returned while a closure destructor was still running");
    Check(destroyedAtStop == count, "Stop returned with undestroyed closures");
    Check(DrawPipeline::Committers() == 0, "Stop left a running committer");
    Check(!pipeline.Busy(), "Stop left outstanding commits");
    pipeline.Stop();
    std::printf("%.*s: Stop destroyed all %u closures and stopped the committer\n", static_cast<int>(mode.size()), mode.data(), count);
}
