#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t MaxThreads = 64;
constexpr std::uint32_t Sentinel = 0x5eed0001u;
constexpr std::size_t OffsetWord = 2;
alignas(256) std::array<std::uint32_t, MaxThreads + 1> Buffer{};
alignas(256) std::array<std::uint32_t, MaxThreads> Output{};

alignas(256) auto Code = std::to_array<std::uint32_t>({
    0x34020082,
    0x7e040281,
    0xe0c85000, 0x80000201,
    0xbf8c3f70,
    0xe0701000, 0x80010201,
    0xbf810000,
});

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::uint32_t Initial(std::uint32_t lane) {
    return lane * 100u + lane % 2u;
}

void Run(AgcDriver::VulkanDevice& device, std::uint32_t offset, std::uint32_t waveSize, bool atomicOr, bool returning, const ShaderRecompiler::SpirvTarget& target) {
    Buffer.fill(Sentinel);
    for (std::uint32_t lane = 0; lane < waveSize; ++lane) Buffer[lane] = Initial(lane);
    Output.fill(0xdeadbeefu);
    Code[OffsetWord] = (atomicOr ? 0xe0e81000u : 0xe0c81000u) | (returning ? 0x4000u : 0u) | offset;
    std::vector<std::uint32_t> userData(8, 0u);
    const auto buffer = BufferDescriptor(Buffer.data(), static_cast<std::uint32_t>(Buffer.size() * 4u));
    const auto output = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size() * 4u));
    std::copy(buffer.begin(), buffer.end(), userData.begin());
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    const std::span<const std::uint32_t> code(Code);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{waveSize, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {waveSize, 0, userData, compute, std::nullopt, std::nullopt, memory},
        target,
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
}

void Check(std::uint32_t offset, std::uint32_t waveSize, bool atomicOr, bool returning) {
    const auto what = std::string(atomicOr ? "buffer_atomic_or" : "buffer_atomic_add") + " offset " + std::to_string(offset) + " wave" + std::to_string(waveSize) + (returning ? " glc" : " no glc") + ": lane ";
    for (std::uint32_t lane = 0; lane < waveSize; ++lane) {
        const auto previous = Initial(lane);
        const auto expectedReturn = returning ? previous : 1u;
        const auto expectedMemory = atomicOr ? previous | 1u : previous + 1u;
        Require(Output[lane] == expectedReturn, what + std::to_string(lane) + " returned " + std::to_string(Output[lane]) + ", expected " + std::to_string(expectedReturn));
        Require(Buffer[lane] == expectedMemory, what + std::to_string(lane) + " left dword " + std::to_string(Buffer[lane]) + ", expected " + std::to_string(expectedMemory));
    }
    for (std::uint32_t lane = waveSize; lane < Buffer.size(); ++lane) Require(Buffer[lane] == Sentinel, what + "an inactive or guard dword changed");
    for (std::uint32_t lane = waveSize; lane < Output.size(); ++lane) Require(Output[lane] == 0xdeadbeefu, what + "an inactive lane wrote a result");
}

void Cases(AgcDriver::VulkanDevice& device, std::uint32_t waveSize, const ShaderRecompiler::SpirvTarget& target) {
    for (const bool atomicOr : {false, true}) {
        for (const bool returning : {false, true}) {
            for (std::uint32_t offset = 0; offset < 4u; ++offset) {
                Run(device, offset, waveSize, atomicOr, returning, target);
                Check(offset, waveSize, atomicOr, returning);
            }
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (!TargetHasCapability(device->Target(), spv::CapabilityInt64)) {
            std::puts("skipped, the device has no shaderInt64");
            return VulkanTestSkipped;
        }
        Cases(*device, 32u, device->Target());
        Cases(*device, 64u, device->Target());
        Cases(*device, 64u, device->ComputeTarget(32));
        std::puts("buffer atomic unaligned tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
