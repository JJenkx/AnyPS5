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
#endif
#include <array>
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
constexpr std::size_t BlockBytes = SurfaceBytes + 65536u;
std::uint64_t AddressOf(const void* data) { return reinterpret_cast<std::uint64_t>(data); }

class GuestBlock {
public:
    explicit GuestBlock(bool watched) {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
        static_cast<void>(watched);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, BlockBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + BlockBytes != begin + BlockBytes + alignment) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            if (watched) GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
        }
#endif
        Require(block != nullptr, "one-slice alias: cannot allocate the guest block");
        Require(!watched || AgcDriver::GuestMemory::Watched(AddressOf(block), BlockBytes), "one-slice alias: the guest block is not write-watched");
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, BlockBytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, BlockBytes);
#else
        munmap(block, BlockBytes);
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, BlockBytes);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint8_t* Data() { return block; }

private:
    std::uint8_t* block = nullptr;
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
    Require(HostImportFor(context, address, SurfaceBytes) != nullptr, "borrower generation: host import refused");
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
        GuestBlock block(true);
        Run(*device, block, argc == 2 && std::string_view(argv[1]) == "--cpu-overwrite");
        std::puts("borrower generation survives lender publication across four rounds");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
