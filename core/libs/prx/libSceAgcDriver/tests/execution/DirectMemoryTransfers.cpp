#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <stdexcept>

extern "C" {
int APS5_VABI sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int, std::int64_t*);
int APS5_VABI sceKernelMapDirectMemory(void**, std::size_t, int, int, std::int64_t, std::size_t);
int APS5_VABI sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
int APS5_VABI sceKernelMunmap(void*, std::size_t);
}

namespace {

using AgcDriver::Graphics::Require;
constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t ChunkBytes = 64u << 20u;
constexpr std::size_t AllocationBytes = ChunkBytes + 2 * BlockBytes;
constexpr std::size_t CopyOffset = 1024;
constexpr std::size_t CopyBytes = 32768;
constexpr std::byte Guard{0xd7};

class DirectBlocks {
public:
    DirectBlocks() {
        Require(sceKernelAllocateDirectMemory(0, 0x10000000000ll, AllocationBytes, ChunkBytes, 3, &physical) == 0, "direct transfers: allocate backing failed");
        try {
            const std::array<std::size_t, 3> offsets{BlockBytes, 3 * BlockBytes, ChunkBytes + BlockBytes};
            for (std::size_t index = 0; index < blocks.size(); ++index) {
                Require(sceKernelMapDirectMemory(&blocks[index], BlockBytes, 0x33, 0, physical + offsets[index], BlockBytes) == 0, "direct transfers: map backing failed");
            }
        } catch (...) {
            release();
            throw;
        }
    }
    ~DirectBlocks() { release(); }
    DirectBlocks(const DirectBlocks&) = delete;
    DirectBlocks& operator=(const DirectBlocks&) = delete;
    std::span<std::byte> Bytes(std::size_t index) { return {static_cast<std::byte*>(blocks.at(index)), BlockBytes}; }
    std::uint64_t Address(std::size_t index) const { return reinterpret_cast<std::uintptr_t>(blocks.at(index)); }

private:
    void release() {
        for (auto& block : blocks) {
            if (block != nullptr) sceKernelMunmap(block, BlockBytes);
            block = nullptr;
        }
        if (physical >= 0) sceKernelReleaseDirectMemory(physical, AllocationBytes);
        physical = -1;
    }
    std::int64_t physical = -1;
    std::array<void*, 3> blocks{};
};

void RequireGuards(std::span<const std::byte> block) {
    Require(std::all_of(block.begin(), block.begin() + CopyOffset, [](auto value) { return value == Guard; }), "direct transfers: prefix guard changed");
    Require(std::all_of(block.begin() + CopyOffset + CopyBytes, block.end() - 16, [](auto value) { return value == Guard; }), "direct transfers: suffix guard changed");
}

}

int main() {
    try {
        DirectBlocks guest;
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        AgcDriver::Graphics::Context probe{};
        probe.device = device->Device();
        probe.hostImportAlignment = 1;
        for (unsigned round = 0; round < 4; ++round) {
            for (std::size_t index = 0; index < 3; ++index) std::fill(guest.Bytes(index).begin(), guest.Bytes(index).end(), Guard);
            const std::array<std::uint32_t, 4> fill{0x01020304u + round, 0xaabbccddu - round, 0x76543210u + round, 0x12345678u - round};
            if (!device->FillBuffer(guest.Address(0) + CopyOffset, CopyBytes, fill)) {
                std::puts("skipped, direct memory cannot be imported on this device");
                return VulkanTestSkipped;
            }
            auto labelWords = fill;
            if (round == 3) labelWords.fill(0);
            constexpr std::array<std::size_t, 4> labelSizes{4, 8, 16, 8};
            const auto label = std::as_bytes(std::span(labelWords)).first(labelSizes[round]);
            Require(device->WriteLabelOnGpu(guest.Address(0) + BlockBytes - label.size(), label, 100 + round, 0) == 0, "direct transfers: label was not recorded on the GPU");
            device->WaitIdle();
            Require(AgcDriver::Graphics::HostImportCovers(probe, guest.Address(0), BlockBytes), "direct transfers: fill did not use the import");
            const auto source = guest.Bytes(0).subspan(CopyOffset, CopyBytes);
            for (std::size_t offset = 0; offset < source.size(); offset += sizeof(fill)) {
                Require(std::memcmp(source.data() + offset, fill.data(), sizeof(fill)) == 0, "direct transfers: GPU fill was not visible to the CPU");
            }
            Require(std::memcmp(guest.Bytes(0).data() + BlockBytes - label.size(), label.data(), label.size()) == 0, "direct transfers: GPU label was not visible to the CPU");
            Require(std::all_of(guest.Bytes(0).end() - 16, guest.Bytes(0).end() - label.size(), [](auto value) { return value == Guard; }), "direct transfers: label changed its prefix guard");
            RequireGuards(guest.Bytes(0));
            for (std::size_t offset = 0; offset < source.size(); ++offset) source[offset] = static_cast<std::byte>((offset * 37u + round * 19u) & 0xffu);
            const auto note = [](std::span<const std::byte>, std::uint64_t) {};
            for (std::size_t destination = 1; destination < 3; ++destination) {
                const auto outcome = device->CopyBuffer(guest.Address(destination) + CopyOffset, guest.Address(destination - 1) + CopyOffset, CopyBytes, 0, CopyBytes, 0, 0, 0, note);
                Require(outcome.path == 1, "direct transfers: copy did not use the GPU imports");
            }
            device->WaitIdle();
            for (std::size_t destination = 1; destination < 3; ++destination) {
                const auto result = guest.Bytes(destination);
                Require(std::memcmp(result.data() + CopyOffset, source.data(), CopyBytes) == 0, "direct transfers: CPU stores or the preceding GPU copy were not visible");
                RequireGuards(result);
                Require(std::all_of(result.end() - 16, result.end(), [](auto value) { return value == Guard; }), "direct transfers: copy changed the end guard");
            }
        }
        std::puts("direct memory GPU fills, labels, CPU uploads and chained GPU copies passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
