#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/UnitShadow.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <utility>

#ifndef __linux__
int main() {
    std::printf("skipped, udmabuf imports are Linux only\n");
    return VulkanTestSkipped;
}
#else
namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint64_t Unit = 65536;
constexpr std::size_t MappingBytes = std::size_t{1} << 20u;
constexpr std::uint32_t ShadowWord = 0x5AD0BEEFu;
constexpr std::uint8_t HostByte = 0x11;
constexpr std::uint8_t AroundByte = 0x33;

std::uint8_t* mapping = nullptr;
int backing = -1;

bool Resolve(std::uintptr_t address, std::size_t bytes, int* file, std::uint64_t* offset) {
    const auto begin = reinterpret_cast<std::uintptr_t>(mapping);
    if (address < begin || address + bytes > begin + MappingBytes) return false;
    *file = dup(backing);
    *offset = address - begin;
    return *file >= 0;
}

int Skip(const char* why) {
    std::printf("skipped, %s\n", why);
    return VulkanTestSkipped;
}

void (*landHook)(std::uint64_t, std::size_t) = nullptr;
std::uint64_t publishAfterLand = 0;
std::size_t publishedAfterLand = 0;

std::size_t PublishUnit(std::uint64_t unitBegin) {
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    return AgcDriver::Graphics::PublishShadow(unitBegin, Unit, AgcDriver::Graphics::PublishScope::Whole, AgcDriver::Graphics::PublishReason::Address);
}

void LandThenPublish(std::uint64_t address, std::size_t bytes) {
    landHook(address, bytes);
    if (publishAfterLand == 0) return;
    publishedAfterLand = PublishUnit(std::exchange(publishAfterLand, 0));
}

void ShadowUnit(const AgcDriver::Graphics::Context& context, const AgcDriver::Graphics::HostImport& import, std::uint64_t unitBegin) {
    using namespace AgcDriver::Graphics;
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    auto* recorder = Recorder::Active();
    Require(recorder != nullptr, "host write over a shadowed unit: no active recorder");
    AgcDriver::GuestMemory::CollectWritesUncached(unitBegin, Unit);
    const auto destination = ShadowDestinationFor(context, import, unitBegin, unitBegin + Unit);
    Require(destination.has_value(), "host write over a shadowed unit: no unit shadow for a whole unit of the import");
    const auto commands = recorder->Commands();
    recorder->Keep(destination->slab);
    context.Function<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(commands, destination->buffer, destination->offset, Unit, ShadowWord);
    RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    recorder->MarkCovered(VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    const ShadowedRange range{unitBegin, unitBegin + Unit, destination->slab};
    MarkShadowed(import, std::span(&range, 1), AgcDriver::GuestMemory::TrackerGeneration());
    recorder->Sync();
    Require(AnyShadowedOverlaps(unitBegin, Unit), "host write over a shadowed unit: the unit is not shadowed before the host write");
}

bool HostWriteKeepsItsBytes(const AgcDriver::Graphics::Context& context, const AgcDriver::Graphics::HostImport& import, std::uint64_t unitBegin, bool publishBeforeStamp, const char* name) {
    ShadowUnit(context, import, unitBegin);
    auto* const unit = reinterpret_cast<std::uint8_t*>(unitBegin);
    std::size_t published = 0;
    publishedAfterLand = 0;
    publishAfterLand = publishBeforeStamp ? unitBegin : 0;
    {
        const GuestArena::HostWrite scope(unit, Unit);
        Require(scope.Open(), "host write over a shadowed unit: the destination is not writable guest memory");
        published = publishBeforeStamp ? publishedAfterLand : PublishUnit(unitBegin);
        std::memset(unit, HostByte, Unit);
    }
    {
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        AgcDriver::Graphics::Recorder::Active()->Sync();
    }
    Require(unit[-1] == AroundByte && unit[Unit] == AroundByte, std::string("host write over a shadowed unit, ") + name + ": bytes outside the unit changed");
    for (std::uint64_t offset = 0; offset < Unit; ++offset) {
        if (unit[offset] == HostByte) continue;
        std::cout << "host write over a shadowed unit, " << name << ": FAILED, byte " << offset << " holds the shadow's GPU results, not the host write's (the publish copied " << published << " units over it)\n";
        return false;
    }
    std::cout << "host write over a shadowed unit, " << name << ": the host write's bytes survived the publish\n";
    return true;
}

bool ShortHostWriteKeepsUnwrittenBytes(const AgcDriver::Graphics::Context& context, const AgcDriver::Graphics::HostImport& import, std::uint64_t unitBegin, std::size_t written) {
    ShadowUnit(context, import, unitBegin);
    auto* const unit = reinterpret_cast<std::uint8_t*>(unitBegin);
    {
        const GuestArena::HostWrite scope(unit, Unit);
        Require(scope.Open(), "short host write: destination is not writable guest memory");
        std::memset(unit, HostByte, written);
    }
    PublishUnit(unitBegin);
    {
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        AgcDriver::Graphics::Recorder::Active()->Sync();
    }
    Require(unit[-1] == AroundByte && unit[Unit] == AroundByte, "short host write: surrounding bytes changed");
    const auto* shadow = reinterpret_cast<const std::uint8_t*>(&ShadowWord);
    for (std::size_t offset = 0; offset < Unit; ++offset) {
        const auto expected = offset < written ? HostByte : shadow[offset % sizeof(ShadowWord)];
        if (unit[offset] == expected) continue;
        std::cout << "short host write " << written << "/" << Unit << ": FAILED byte " << offset << " got " << static_cast<unsigned>(unit[offset]) << " expected " << static_cast<unsigned>(expected) << '\n';
        return false;
    }
    std::cout << "short host write " << written << "/" << Unit << ": prefix and unwritten GPU tail preserved\n";
    return true;
}

}

int main(int argc, char** argv) {
    using namespace AgcDriver::Graphics;
    try {
        const int udmabuf = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
        if (udmabuf < 0) return Skip("host imports are unavailable: /dev/udmabuf is not accessible");
        close(udmabuf);
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (!UnitShadowEnabled()) return Skip("unit shadows are off");

        backing = memfd_create("anyps5-host-write-shadowed-unit", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        Require(backing >= 0 && ftruncate(backing, static_cast<off_t>(MappingBytes)) == 0, "host write over a shadowed unit: cannot make the backing file");
        Require(fcntl(backing, F_ADD_SEALS, F_SEAL_SHRINK) == 0, "host write over a shadowed unit: cannot seal the backing file");
        void* mapped = mmap(nullptr, MappingBytes, PROT_READ | PROT_WRITE, MAP_SHARED, backing, 0);
        Require(mapped != MAP_FAILED, "host write over a shadowed unit: cannot map the backing file");
        mapping = static_cast<std::uint8_t*>(mapped);
        GuestArena::GuestArenaSetSharedBacking_nid_postfix(Resolve);
        std::memset(mapping, AroundByte, MappingBytes);

        const auto address = reinterpret_cast<std::uint64_t>(mapping);
        GuestAllocations::Mutation().Add(mapping, MappingBytes, true, true, true);
        GuestAllocations::Mapped ranges;
        for (const auto& range : GuestAllocations::GuestAllocationsAcquire_nid_postfix()) {
            if (range->address == address) ranges.push_back(range);
        }
        Require(ranges.size() == 1, "host write over a shadowed unit: the allocation is not registered");
        Require(device->ImportGuestMemory(ranges, GuestAllocations::GuestAllocationsGeneration_nid_postfix(), false), "host write over a shadowed unit: the import made without the GPU lock was refused");
        GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(mapping, MappingBytes);

        const auto firstUnit = (address + Unit - 1) & ~(Unit - 1);
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        const HostImport* import = nullptr;
        {
            std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
            import = HostImportFor(context, address, MappingBytes);
        }
        if (import == nullptr || !import->dmaBuf) return Skip("the device imported the mapping through a host pointer, which is compared, not watched");
        if (!AgcDriver::GuestMemory::Watched(firstUnit, 4 * Unit)) return Skip("the imported units are not write-watched");

        landHook = AgcDriver::GuestMemory::CurrentFlushHook();
        Require(landHook != nullptr, "host write over a shadowed unit: the device installed no flush hook");
        AgcDriver::GuestMemory::SetFlushHook(&LandThenPublish);
        GuestArena::GuestArenaSetHostWriteHook_nid_no_patch(&AgcDriver::GuestMemory::PrepareHostWrite);
        bool passed = true;
        if (argc == 2 && std::strcmp(argv[1], "--short-write") == 0) {
            for (const auto written : {std::size_t{0}, std::size_t{1}, std::size_t{4096}, std::size_t{Unit - 1}, std::size_t{Unit}})
                passed = ShortHostWriteKeepsUnwrittenBytes(context, *import, firstUnit + 6 * Unit, written) && passed;
        } else {
            passed = HostWriteKeepsItsBytes(context, *import, firstUnit + Unit, false, "a publish made while the host write is open");
            passed = HostWriteKeepsItsBytes(context, *import, firstUnit + 3 * Unit, true, "a publish made between the host write's flush and its write") && passed;
        }
        AgcDriver::GuestMemory::SetFlushHook(landHook);
        Require(passed, "host write over a shadowed unit: a publish put a unit shadow's results over a host write");

        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(mapping, MappingBytes);
        GuestAllocations::Mutation().Remove(mapping);
        std::cout << "host write shadowed unit tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
