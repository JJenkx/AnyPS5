#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureCache.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureDetiler.hpp"
#include "prx/libSceAgcDriver/Graphics/include/UnitShadow.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DccMetadata.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <array>
#include <map>
#include <iterator>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;
constexpr std::size_t SurfaceBytes = 256u * 256u * 4u;
constexpr std::size_t BlockBytes = 1048576;
namespace Memory = AgcDriver::GuestMemory;
std::uint64_t AddressOf(const void* data) { return reinterpret_cast<std::uint64_t>(data); }

#ifdef __linux__
struct SharedBacking {
    int file;
    std::uint64_t offset;
    std::size_t resolverReturns = 0;
};
std::map<std::uintptr_t, SharedBacking> sharedBlocks;

bool ResolveSharedBlock(std::uintptr_t address, std::size_t bytes, int* file, std::uint64_t* offset) {
    if (bytes == 0) return false;
    const auto next = sharedBlocks.upper_bound(address);
    if (next == sharedBlocks.begin()) return false;
    const auto entry = std::prev(next);
    const auto lead = address - entry->first;
    if (lead >= BlockBytes || bytes > BlockBytes - lead) return false;
    *file = fcntl(entry->second.file, F_DUPFD_CLOEXEC, 0);
    *offset = entry->second.offset + lead;
    if (*file < 0) return false;
    ++entry->second.resolverReturns;
    return true;
}

struct SharedBackingScope {
    SharedBackingScope() { GuestArena::GuestArenaSetSharedBacking_nid_postfix(ResolveSharedBlock); }
    ~SharedBackingScope() { GuestArena::GuestArenaSetSharedBacking_nid_postfix(nullptr); }
};
#endif

class GuestBlock {
public:
    GuestBlock() {
        Require(Memory::WriteWatched(), "native depth BDA clear requires guest write watching");
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, BlockBytes));
        Require(block != nullptr, "cannot allocate the native depth BDA clear block");
        section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, BlockBytes, nullptr);
        Require(section != nullptr, "cannot create the native depth BDA clear backing");
        GuestArena::GuestArenaMapDirect_nid_postfix(block, BlockBytes, section, 0, PAGE_READWRITE);
#else
#ifdef __linux__
        backing = memfd_create("native-depth-bda-clear", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        Require(backing >= 0, "cannot create the native depth BDA clear backing");
        if (ftruncate(backing, 2 * BlockBytes) != 0 || fcntl(backing, F_ADD_SEALS, F_SEAL_SHRINK) != 0) {
            close(backing);
            Require(false, "cannot size or seal the native depth BDA clear backing");
        }
        void* raw = mmap(nullptr, 2 * BlockBytes, PROT_READ | PROT_WRITE, MAP_SHARED, backing, 0);
        if (raw == MAP_FAILED) close(backing);
#else
        void* raw = mmap(nullptr, 2 * BlockBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
        Require(raw != MAP_FAILED, "cannot map the native depth BDA clear block");
        const auto begin = reinterpret_cast<std::uintptr_t>(raw);
        const auto aligned = (begin + BlockBytes - 1) & ~(static_cast<std::uintptr_t>(BlockBytes) - 1);
        if (aligned != begin) munmap(raw, aligned - begin);
        if (aligned != begin + BlockBytes) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + BlockBytes - aligned);
        block = reinterpret_cast<std::uint8_t*>(aligned);
#ifdef __linux__
        sharedBlocks.emplace(aligned, SharedBacking{backing, aligned - begin});
#endif
        GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
#endif
        Require(Memory::Watched(Address(), BlockBytes), "the native depth BDA clear block is not watched");
        std::memset(block, 0xcd, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, BlockBytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, BlockBytes);
        GuestArena::GuestArenaForgetSection_nid_postfix(section);
        CloseHandle(section);
#else
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, BlockBytes);
#ifdef __linux__
        sharedBlocks.erase(Address());
        close(backing);
#endif
        munmap(block, BlockBytes);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;
    std::uint64_t Address() const { return reinterpret_cast<std::uintptr_t>(block); }
    std::uint8_t* Data() const { return block; }
    void FillWith(std::uint8_t value) const { std::memset(block, value, BlockBytes); }
#ifdef __linux__
    std::size_t SharedBackingResolverReturns() const { return sharedBlocks.at(Address()).resolverReturns; }
#endif

private:
    std::uint8_t* block = nullptr;
#ifdef _WIN32
    HANDLE section = nullptr;
#elif defined(__linux__)
    int backing = -1;
#endif
};

GuestTextureResource Surface(std::uint64_t address, std::uint32_t format) {
    GuestTextureResource resource{};
    resource.baseAddress = address;
    resource.width = 256;
    resource.height = 256;
    resource.mipCount = 1;
    resource.tileMode = TextureTileMode::kR64KBX;
    resource.dimension = TextureDimension::k2D;
    resource.format = format;
    resource.dstSelX = 4;
    resource.dstSelY = 5;
    resource.dstSelZ = 6;
    resource.dstSelW = 7;
    return resource;
}


void Run(AgcDriver::VulkanDevice& device, GuestBlock& block, bool sameValue) {
    auto context = device.graphicsContext();
    PrepareImportWatch(context);
    Require(context.bufferDeviceAddress, "late DCC store requires buffer device addresses");
    const auto* imported = HostImportFor(context, block.Address(), BlockBytes);
    Require(imported != nullptr, "late DCC store requires a host import");
#ifdef __linux__
    Require(imported->dmaBuf && !imported->unwatched && block.SharedBackingResolverReturns() != 0, "late DCC store requires watched shared dma-buf backing");
#endif
    Require(Memory::Watched(block.Address(), BlockBytes), "late DCC store block lost write watching");
    constexpr std::size_t keyCount = SurfaceBytes / 256;
    constexpr std::size_t offset = 5 * 65536 - keyCount / 2;
    auto* keys = block.Data() + offset;
    const auto address = block.Address() + offset;
    std::memset(keys, 0, keyCount);
    Memory::CollectWritesUncached(address, keyCount);
    Recorder recorder(context);
    recorder.Activate();
    context.recorder = &recorder;
    MarkDccUncompressed(context, address, SurfaceBytes, keyCount);
    Require(recorder.QueuedKeyStoreOverlaps(address, keyCount), "late DCC store did not queue a deferred GPU key store");
    const std::uint32_t value = sameValue ? 0u : 0x12345678u;
    auto* late = reinterpret_cast<volatile std::uint32_t*>(keys + 400);
    *late = value;
    recorder.Submit();
    recorder.Sync();
    std::printf("late DCC store: CPU value %08x, after GPU %08x\n", value, static_cast<std::uint32_t>(*late));
    Require(*late == value, "deferred key store overwrote the later CPU dword");
    for (std::size_t i = 0; i < keyCount / 2; ++i) {
        const auto expected = i >= 400 && i < 404 ? static_cast<std::uint8_t>(value >> (8 * (i - 400))) : std::uint8_t{0};
        Require(keys[i] == expected, "deferred key store did not preserve the CPU-written tracker block");
    }
    for (std::size_t i = keyCount / 2; i < keyCount; ++i) Require(keys[i] == 0xff, "deferred key store did not publish keys in the untouched tracker block");
}

}

int main(int argc, char** argv) {
    try {
        if (!Memory::WriteWatched()) return VulkanTestSkipped;
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::lock_guard gpu(Memory::GpuMutex());
#ifdef __linux__
        SharedBackingScope scope;
#endif
        GuestBlock block;
        Run(*device, block, argc == 2 && std::string_view(argv[1]) == "--same-value");
        std::puts("deferred DCC key store preserves later CPU writes");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
