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

using Texel = std::array<std::uint8_t, 4>;

void Run(AgcDriver::VulkanDevice& device, GuestBlock& block, bool cpuOverwrite) {
    auto context = device.graphicsContext();
    Require(context.hostImportAlignment != 0, "borrower generation: host imports unavailable");
    PrepareImportWatch(context);
    const auto address = AddressOf(block.Data());
    std::memset(block.Data(), 0x5a, SurfaceBytes);
    const auto* imported = HostImportFor(context, address, SurfaceBytes);
    Require(imported != nullptr, "borrower generation: host import refused");
#ifdef __linux__
    Require(imported->dmaBuf && !imported->unwatched && block.SharedBackingResolverReturns() != 0, "borrower generation: expected a watched shared dma-buf import");
#endif
    Require(AgcDriver::GuestMemory::Watched(address, SurfaceBytes), "borrower generation: imported memory must stay watched");
    AgcDriver::GuestMemory::CollectWritesUncached(address, SurfaceBytes);
    AgcDriver::GuestMemory::BumpCollectEpoch();
    TextureDetiler detiler(context);
    context.detiler = &detiler;
    TextureCache cache(context);
    context.textureCache = &cache;
    Recorder recorder(context);
    recorder.Activate();
    const auto x = Surface(address, 20);
    const auto y = Surface(address, 56);
    auto imageX = CachedStorageSurface(context, x);
    auto imageY = CachedStorageSurface(context, y);
    Require(imageX != imageY, "borrower generation: expected two storage formats");
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const auto write = [&](const std::shared_ptr<StorageTexture>& image, bool rgba, const Texel& texel) {
        VkClearColorValue value{};
        if (rgba) {
            for (std::size_t i = 0; i < texel.size(); ++i) value.float32[i] = static_cast<float>(texel[i]) / 255.0f;
        } else {
            value.uint32[0] = static_cast<std::uint32_t>(texel[0]) | static_cast<std::uint32_t>(texel[1]) << 8u | static_cast<std::uint32_t>(texel[2]) << 16u | static_cast<std::uint32_t>(texel[3]) << 24u;
        }
        const auto commands = recorder.Commands();
        recorder.Keep(image);
        RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        context.Function<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(commands, image->Image(), VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
        RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        image->MarkDirty();
    };
    const auto requireImage = [&](const std::shared_ptr<StorageTexture>& image, const Texel& texel, const std::string& what) {
        Buffer readback(context, SurfaceBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        const auto commands = recorder.Commands();
        recorder.Keep(image);
        RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {256, 256, 1};
        context.Function<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(commands, image->Image(), VK_IMAGE_LAYOUT_GENERAL, readback.Handle(), 1, &copy);
        RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
        recorder.Sync();
        const auto pixels = readback.Bytes();
        for (std::size_t i = 0; i < pixels.size(); ++i) Require(std::to_integer<std::uint8_t>(pixels[i]) == texel[i % 4], what);
    };
    const auto flush = [&] {
        StorageTexture::FlushPending(address, SurfaceBytes, nullptr, "borrower generation test", PublishScope::Whole);
        PublishShadow(address, SurfaceBytes, PublishScope::Whole, PublishReason::Hook);
        recorder.Sync();
    };
    Texel lastX{0x5a, 0x5a, 0x5a, 0x5a};
    for (std::uint32_t round = 0; round < 4; ++round) {
        imageY->Refresh();
        requireImage(imageY, lastX, "borrower generation: lender did not receive the borrower's latest GPU bytes");
        flush();
        const Texel fromY{static_cast<std::uint8_t>(0x10 + round), 0x21, 0x32, 0x43};
        write(imageY, true, fromY);
        imageX->Refresh();
        requireImage(imageX, fromY, "borrower generation: borrower did not receive lender bytes");
        flush();
        lastX = {static_cast<std::uint8_t>(0x80 + round), 0x91, 0xa2, 0xb3};
        write(imageX, false, lastX);
    }
    if (cpuOverwrite) std::memset(block.Data(), 0x7c, 65536);
    flush();
    requireImage(imageX, lastX, "borrower generation: final GPU bytes were lost");
    for (std::size_t i = 0; i < SurfaceBytes; ++i) {
        const auto expected = cpuOverwrite && i < 65536 ? std::uint8_t{0x7c} : lastX[i % 4];
        Require(block.Data()[i] == expected, "borrower generation: final guest bytes were lost");
    }
    recorder.Sync();
    ClearCachedTextures(context.device);
}

}

int main(int argc, char** argv) {
    try {
        if (!AgcDriver::GuestMemory::WriteWatched()) {
            std::puts("skipped, borrower generation requires write watching");
            return VulkanTestSkipped;
        }
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
#ifdef __linux__
        SharedBackingScope sharedScope;
#endif
        GuestBlock block;
        Run(*device, block, argc == 2 && std::string_view(argv[1]) == "--cpu-overwrite");
        std::puts("borrower generation survives lender publication across four rounds");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
