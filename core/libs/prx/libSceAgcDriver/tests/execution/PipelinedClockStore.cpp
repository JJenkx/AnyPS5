#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using AgcDriver::DriverDetail::DrawPipeline;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void submit(std::vector<std::uint32_t>& words) {
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    check(sceAgcDriverSubmitDcb(&packet) == 0, "clock-store submission failed");
}

void run(std::uint32_t selection, bool wide) {
    alignas(256) std::array<std::uint64_t, 2> pending{};
    alignas(256) std::array<std::uint64_t, 2> clocks{0, 0xa5a5a5a500000000ull};
    const auto address = reinterpret_cast<std::uint64_t>(clocks.data());
    const auto held = reinterpret_cast<std::uint64_t>(pending.data());
    std::atomic<bool> release{false};
    struct Release {
        std::atomic<bool>& value;
        ~Release() {
            value.store(true, std::memory_order_release);
            try { AgcDriverWaitIdle_nid_postfix(); } catch (...) {}
        }
    } finally{release};
    DrawPipeline::Queue0().Enqueue([&release] {
        while (!release.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }, {{held, held + sizeof(pending)}});
    std::vector<std::uint32_t> words;
    const auto bytes = wide ? 8u : 4u;
    for (const auto target : {address, address + 8}) {
        const std::array<std::uint32_t, 6> packet{0xc0044000u, 9u | (selection << 8u) | (wide ? 0x10000u : 0u), 0u, 0u, static_cast<std::uint32_t>(target), static_cast<std::uint32_t>(target >> 32u)};
        words.insert(words.end(), packet.begin(), packet.end());
    }
    bool queued = false;
    bool untouched = false;
    const auto drains = DrawPipeline::DrainRequestsByOpcode(0x40);
    {
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        submit(words);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        do {
            queued = DrawPipeline::Queue0().PendingLabel(address, bytes).has_value() && DrawPipeline::Queue0().PendingLabel(address + 8, bytes).has_value();
            if (queued) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        untouched = clocks[0] == 0 && clocks[1] == 0xa5a5a5a500000000ull;
    }
    release.store(true, std::memory_order_release);
    AgcDriverWaitIdle_nid_postfix();
    check(queued, "GPU-clock COPY_DATA blocked on the GPU mutex instead of queuing its nonoverlapping stores");
    check(untouched, "a queued clock store overtook earlier uncommitted work");
    check(DrawPipeline::DrainRequestsByOpcode(0x40) == drains, "a nonoverlapping clock store drained the draw pipeline");
    if (wide) {
        check(clocks[0] != 0 && clocks[1] >= clocks[0], "queued wide clocks were not stored in increasing order");
    } else {
        check(static_cast<std::uint32_t>(clocks[0]) != 0 && static_cast<std::uint32_t>(clocks[1]) != 0, "queued narrow clocks were not stored");
        check((clocks[0] >> 32u) == 0 && (clocks[1] >> 32u) == 0xa5a5a5a5u, "a narrow clock store overwrote neighboring bytes");
    }
}

}

int main() {
#ifdef _WIN32
    _putenv_s("APS5_PIPELINED_DRAWS", "1");
#else
    setenv("APS5_PIPELINED_DRAWS", "1", 1);
#endif
    try {
        std::vector<std::uint32_t> warmup{0x80000000u};
        submit(warmup);
        AgcDriverWaitIdle_nid_postfix();
        for (const auto selection : {1u, 2u, 5u}) {
            run(selection, false);
            run(selection, true);
        }
        LibcRunShutdown_nid_postfix();
        std::puts("queued clock stores preserve ordering without a packet-worker GPU lock: ok");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { LibcRunShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
