#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>

int main() {
#ifdef _WIN32
    _putenv_s("APS5_PIPELINED_DRAWS", "1");
#else
    setenv("APS5_PIPELINED_DRAWS", "1", 1);
#endif
    using AgcDriver::DriverDetail::DrawPipeline;
    std::atomic<bool> release{false};
    try {
        constexpr std::uint32_t offset = 256;
        constexpr std::uint64_t earlier = 0x123456789abcdef0ull;
        const auto address = AgcDriver::Pm4::GdsAddress() + offset;
        auto* destination = reinterpret_cast<volatile std::uint64_t*>(address);
        *destination = 0;
        DrawPipeline::Queue0().Enqueue([&] {
            while (!release.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            *destination = earlier;
        }, {{address, address + 8}});
        const auto before = DrawPipeline::DrainRequestsByOpcode(0x40);
        std::jthread releaser([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (*destination == 0 && DrawPipeline::DrainRequestsByOpcode(0x40) == before && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            release.store(true, std::memory_order_release);
        });
        std::array<std::uint32_t, 6> words{0xc0044000u, 0x00010309u, 0, 0, offset, 0};
        Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
        if (sceAgcDriverSubmitDcb(&packet) != 0) throw std::runtime_error("GDS clock copy submission failed");
        releaser.join();
        AgcDriverWaitIdle_nid_postfix();
        if (*destination == earlier) throw std::runtime_error("GDS clock copy was overwritten by an earlier pending pipeline write");
        if (*destination == 0) throw std::runtime_error("GDS clock copy did not store a clock");
        if (DrawPipeline::DrainRequestsByOpcode(0x40) != before + 1) throw std::runtime_error("GDS clock copy did not drain its actual pending destination");
        LibcRunShutdown_nid_postfix();
        std::puts("GDS clock copy preserves overlapping pipeline write order");
        return 0;
    } catch (const std::exception& error) {
        release.store(true, std::memory_order_release);
        std::fprintf(stderr, "%s\n", error.what());
        try { LibcRunShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
