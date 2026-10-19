#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
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
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace AgcDriver::Graphics;
constexpr std::uint32_t Side = 256;
constexpr std::size_t SurfaceBytes = Side * Side * 4;
constexpr std::size_t SplitBytes = 65536;
constexpr std::uint8_t Sentinel = 0x93;
class GuestSurface {
public:
    explicit GuestSurface(const Context& context, bool split, std::size_t bytes = SurfaceBytes) : context(context), bytes(bytes) {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, Alignment));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, bytes, PAGE_READWRITE, bytes);
#else
        void* mapped = mmap(nullptr, bytes + Alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + Alignment - 1) & ~(static_cast<std::uintptr_t>(Alignment) - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + bytes != begin + bytes + Alignment) munmap(reinterpret_cast<void*>(aligned + bytes), begin + Alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            if (AgcDriver::GuestMemory::WriteWatched()) GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, bytes);
        }
#endif
        Require(block != nullptr, "segmented storage upload: cannot allocate the guest surface");
        std::memset(block, 0, SplitBytes);
        std::memset(block + SplitBytes, Sentinel, bytes - SplitBytes);
        GuestAllocations::Mutation mutation;
        if (split) {
            mutation.Add(block, SplitBytes, true, true, true);
            mutation.Add(block + SplitBytes, bytes - SplitBytes, true, true, true);
        } else {
            mutation.Add(block, bytes, true, true, true);
        }
        this->split = split;
    }

    ~GuestSurface() {
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(block);
            if (split) mutation.Remove(block + SplitBytes);
        }
        static_cast<void>(HostImportFor(context, reinterpret_cast<std::uintptr_t>(block), bytes));
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, bytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, bytes);
#else
        munmap(block, bytes);
        if (AgcDriver::GuestMemory::WriteWatched()) GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, bytes);
#endif
    }

    GuestSurface(const GuestSurface&) = delete;
    GuestSurface& operator=(const GuestSurface&) = delete;

    std::uint8_t* Data() { return block; }

private:
    const Context& context;
    static constexpr std::size_t Alignment = 65536;
    std::uint8_t* block = nullptr;
    bool split = false;
    std::size_t bytes;
};

std::vector<std::byte> Read(const Context& context, const StorageTexture& image, const GuestTextureResource& resource) {
    const auto geometry = DescribeSurface(resource);
    std::vector<VkBufferImageCopy> copies;
    VkDeviceSize bytes = 0;
    for (std::uint32_t layer = 0; layer < geometry.imageLayers; ++layer) {
        for (std::uint32_t level = 0; level < resource.mipCount; ++level) {
            const auto& mip = geometry.mips[level];
            VkBufferImageCopy copy{};
            copy.bufferOffset = bytes;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
            copy.imageExtent = {mip.width, mip.height, 1};
            copies.push_back(copy);
            bytes += static_cast<VkDeviceSize>(mip.width) * mip.height * 4;
        }
    }
    Buffer result(context, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    CommandBatch batch(context);
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    context.Function<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(batch.Handle(), image.Image(), VK_IMAGE_LAYOUT_GENERAL, result.Handle(), static_cast<std::uint32_t>(copies.size()), copies.data());
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    batch.SubmitAndWait();
    result.Invalidate();
    return {result.Bytes().begin(), result.Bytes().end()};
}

void ShapeTests(Context context, TextureDetiler& detiler, GuestTextureResource resource, std::size_t offset) {
    auto geometry = DescribeSurface(resource);
    const auto bytes = (geometry.guestBytes + offset + SplitBytes - 1) / SplitBytes * SplitBytes;
    GuestSurface surface(context, true, bytes);
    resource.baseAddress = reinterpret_cast<std::uintptr_t>(surface.Data()) + offset;
    geometry = DescribeSurface(resource);
    for (std::size_t block = 0; block < bytes; block += SplitBytes) {
        std::memset(surface.Data() + block, 0x21 + block / SplitBytes, SplitBytes);
        Require(HostImportFor(context, resource.baseAddress - offset + block, SplitBytes) != nullptr, "segmented shape fixture has a missing import");
    }
    Require(!HostImportCovers(context, resource.baseAddress, geometry.guestBytes), "segmented shape fixture has one import");
    auto cpu = context;
    cpu.hostImportAlignment = 0;
    auto expected = StorageTexture::Create(cpu, detiler, resource, 0);
    auto expectedPixels = Read(context, *expected, resource);
    const auto changedOffset = (offset + geometry.GuestLayerOffset(geometry.layers - 1) + geometry.mips.back().tiledOffset) / SplitBytes * SplitBytes;
    for (const bool singlePass : {false, true}) {
        context.singlePassStorage = singlePass;
        ThreadLookupOutcomes() = {};
        auto actual = StorageTexture::Create(context, detiler, resource, 0);
        Require(ThreadLookupOutcomes().counts[LookupOutcomes::UploadDirect] == 1 && ThreadLookupOutcomes().counts[LookupOutcomes::UploadCpu] == 0, "segmented shape upload took a CPU snapshot path");
        Require(Read(context, *actual, resource) == expectedPixels, "segmented shape upload differs from CPU detile");
        std::memset(surface.Data() + changedOffset, singlePass ? 0x76 : 0x65, SplitBytes);
        auto updated = StorageTexture::Create(cpu, detiler, resource, 0);
        expectedPixels = Read(context, *updated, resource);
        ThreadLookupOutcomes() = {};
        actual->Refresh();
        Require(ThreadLookupOutcomes().counts[LookupOutcomes::UploadDirect] == 1 && ThreadLookupOutcomes().counts[LookupOutcomes::UploadCpu] == 0, "segmented shape refresh took a CPU snapshot path");
        Require(Read(context, *actual, resource) == expectedPixels, "segmented shape refresh changed untouched mips or layers");
    }
}


}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        auto context = device->graphicsContext();
        if (context.hostImportAlignment == 0) return VulkanTestSkipped;
        TextureDetiler detiler(context);
        context.detiler = &detiler;
        GuestSurface surface(context, true);
        const auto address = reinterpret_cast<std::uintptr_t>(surface.Data());
        SetHostImportWindow(SplitBytes);
        for (std::size_t offset = 0; offset < SurfaceBytes; offset += SplitBytes) {
            std::memset(surface.Data() + offset, 0x31 + offset / SplitBytes, SplitBytes);
            Require(HostImportFor(context, address + offset, SplitBytes) != nullptr, "segmented upload fixture has a missing import");
        }
        Require(!HostImportCovers(context, address, SurfaceBytes), "segmented upload fixture has one import");
        const std::array<std::uint32_t, 8> words{
            static_cast<std::uint32_t>(address >> 8u),
            static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (20u << 20u) | (((Side - 1u) & 3u) << 30u),
            ((Side - 1u) >> 2u) | ((Side - 1u) << 14u),
            0xfacu | (0x1bu << 20u) | (9u << 28u), 0, 0, 0, 0
        };
        const auto resource = DecodeTextureResource(words);
        auto cpu = context;
        cpu.hostImportAlignment = 0;
        auto expected = StorageTexture::Create(cpu, detiler, resource, 0);
        const auto expectedPixels = Read(context, *expected, resource);
        Require(LookupOutcomes::Profiled(), "segmented upload test requires APS5_PROFILE_DRAW=1");
        for (const bool singlePass : {false, true}) {
            context.singlePassStorage = singlePass;
            ThreadLookupOutcomes() = {};
            auto actual = StorageTexture::Create(context, detiler, resource, 0);
            Require(ThreadLookupOutcomes().counts[LookupOutcomes::UploadDirect] == 1 && ThreadLookupOutcomes().counts[LookupOutcomes::UploadCpu] == 0, "segmented storage upload took a CPU snapshot path");
            Require(Read(context, *actual, resource) == expectedPixels, "segmented storage upload differs from CPU detile");
            std::memset(surface.Data() + SplitBytes, 0x57, SplitBytes);
            auto updated = StorageTexture::Create(cpu, detiler, resource, 0);
            const auto updatedPixels = Read(context, *updated, resource);
            ThreadLookupOutcomes() = {};
            actual->Refresh();
            Require(ThreadLookupOutcomes().counts[LookupOutcomes::UploadDirect] == 1 && ThreadLookupOutcomes().counts[LookupOutcomes::UploadCpu] == 0, "segmented refresh took a CPU snapshot path");
            Require(Read(context, *actual, resource) == updatedPixels, "segmented partial refresh differs from CPU detile");
            {
                Recorder recorder(context);
                recorder.Activate();
                ThreadLookupOutcomes() = {};
                auto recorded = StorageTexture::Create(context, detiler, resource, 0);
                Require(ThreadLookupOutcomes().counts[LookupOutcomes::UploadDirect] == 1 && ThreadLookupOutcomes().counts[LookupOutcomes::UploadCpu] == 0, "recorded segmented upload took a CPU snapshot path");
                recorder.Sync();
                Require(Read(context, *recorded, resource) == updatedPixels, "recorded segmented storage upload differs from CPU detile");
                std::memset(surface.Data() + SplitBytes, 0x32, SplitBytes);
                ThreadLookupOutcomes() = {};
                recorded->Refresh();
                Require(ThreadLookupOutcomes().counts[LookupOutcomes::UploadDirect] == 1 && ThreadLookupOutcomes().counts[LookupOutcomes::UploadCpu] == 0, "recorded segmented refresh took a CPU snapshot path");
                recorder.Sync();
                Require(Read(context, *recorded, resource) == expectedPixels, "recorded segmented partial refresh changed untouched units");
            }
            actual.reset();
            updated.reset();
        }
        expected.reset();
        auto shaped = resource;
        shaped.mipCount = 9;
        shaped.lastLevel = 8;
        ShapeTests(context, detiler, shaped, 0);
        shaped.dimension = TextureDimension::k2DArray;
        shaped.depthOrLastArray = 1;
        ShapeTests(context, detiler, shaped, 0);
        ShapeTests(context, detiler, shaped, 256);
        SetHostImportWindow(0);
        std::cout << "segmented storage upload tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
