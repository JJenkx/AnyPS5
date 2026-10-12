#include "VulkanTestDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <cstring>
#include <mutex>
#include <vector>
#ifdef __linux__
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
int APS5_VABI sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int, std::int64_t*);
int APS5_VABI sceKernelMapDirectMemory(void**, std::size_t, int, int, std::int64_t, std::size_t);
int APS5_VABI sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
int APS5_VABI sceKernelMunmap(void*, std::size_t);
}

namespace {
using AgcDriver::Graphics::Require;
constexpr std::size_t MappingBytes = 65536;
constexpr std::size_t InspectionBytes = 64u << 20u;
constexpr std::size_t ResidencyBudget = 8u << 20u;

class DirectAliases {
public:
    DirectAliases() {
        Require(sceKernelAllocateDirectMemory(0, 0x10000000000ll, MappingBytes, InspectionBytes, 3, &physical) == 0, "footprint: allocation failed");
        try {
            for (auto& alias : aliases) Require(sceKernelMapDirectMemory(&alias, MappingBytes, 0x33, 0, physical, MappingBytes) == 0, "footprint: alias mapping failed");
        } catch (...) {
            release();
            throw;
        }
    }
    ~DirectAliases() { release(); }
    DirectAliases(const DirectAliases&) = delete;
    DirectAliases& operator=(const DirectAliases&) = delete;
    std::uint64_t Address(std::size_t index) const { return reinterpret_cast<std::uintptr_t>(aliases.at(index)); }
private:
    void release() {
        for (auto& alias : aliases) {
            if (alias != nullptr) sceKernelMunmap(alias, MappingBytes);
            alias = nullptr;
        }
        if (physical >= 0) sceKernelReleaseDirectMemory(physical, MappingBytes);
        physical = -1;
    }
    std::int64_t physical = -1;
    std::array<void*, 2> aliases{};
};

class BackingWindow {
public:
    explicit BackingWindow(std::uint64_t address) {
        int file = -1;
        std::uint64_t offset = 0;
        Require(GuestArena::GuestArenaSharedBacking_nid_postfix(address, MappingBytes, &file, &offset), "footprint: no shared backing");
        struct CloseFile { int file; ~CloseFile() { close(file); } } closeFile{file};
        struct stat info{};
        Require(fstat(file, &info) == 0 && offset % InspectionBytes == 0 && offset + InspectionBytes <= static_cast<std::uint64_t>(info.st_size), "footprint: inspection window exceeds backing");
        const auto page = sysconf(_SC_PAGESIZE);
        Require(page > 0 && InspectionBytes % static_cast<std::size_t>(page) == 0, "footprint: invalid host page size");
        pageBytes = static_cast<std::size_t>(page);
        mapping = mmap(nullptr, InspectionBytes, PROT_READ, MAP_SHARED, file, static_cast<off_t>(offset));
        Require(mapping != MAP_FAILED, "footprint: inspection mapping failed");
    }
    ~BackingWindow() { if (mapping != MAP_FAILED) munmap(mapping, InspectionBytes); }
    BackingWindow(const BackingWindow&) = delete;
    BackingWindow& operator=(const BackingWindow&) = delete;
    std::size_t ResidentBytes() const {
        std::vector<unsigned char> pages(InspectionBytes / pageBytes);
        Require(mincore(mapping, InspectionBytes, pages.data()) == 0, "footprint: residency query failed");
        return static_cast<std::size_t>(std::count_if(pages.begin(), pages.end(), [](auto value) { return (value & 1u) != 0; })) * pageBytes;
    }
private:
    void* mapping = MAP_FAILED;
    std::size_t pageBytes = 0;
};
}
#endif

int main() {
#ifndef __linux__
    std::puts("skipped, Linux DMA-buffer imports are required");
    return VulkanTestSkipped;
#else
    try {
        DirectAliases aliases;
        BackingWindow backing(aliases.Address(0));
        const auto before = backing.ResidentBytes();
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        const std::array<std::uint32_t, 4> first{1, 2, 3, 4}, second{5, 6, 7, 8};
        if (!device->FillBuffer(aliases.Address(0), 64, first)) {
            std::puts("skipped, direct memory is not imported");
            return VulkanTestSkipped;
        }
        device->WaitIdle();
        AgcDriver::Graphics::Context probe{};
        probe.device = device->Device();
        probe.hostImportAlignment = 1;
        Require(AgcDriver::Graphics::HostImportCovers(probe, aliases.Address(0), MappingBytes), "footprint: fill did not retain its import");
        const auto* imported = AgcDriver::Graphics::HostImportFor(probe, aliases.Address(0), MappingBytes);
        if (imported == nullptr || !imported->dmaBuf) {
            std::puts("skipped, the device uses host-pointer imports");
            return VulkanTestSkipped;
        }
        Require(std::memcmp(reinterpret_cast<const void*>(aliases.Address(1)), first.data(), sizeof(first)) == 0, "footprint: alias missed the first GPU write");
        Require(device->FillBuffer(aliases.Address(1) + 64, 64, second), "footprint: alias was not imported");
        device->WaitIdle();
        Require(std::memcmp(reinterpret_cast<const void*>(aliases.Address(0) + 64), second.data(), sizeof(second)) == 0, "footprint: original mapping missed the alias GPU write");
        const auto after = backing.ResidentBytes();
        std::printf("DMA-buffer aliases: resident backing before=%zu after=%zu\n", before, after);
        Require(after <= before + ResidencyBudget, "footprint: two small aliases pinned more than 8 MiB of additional backing");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
#endif
}
