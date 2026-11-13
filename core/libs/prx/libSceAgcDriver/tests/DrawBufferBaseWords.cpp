#include "prx/libSceAgcDriver/Execution/include/Driver/Dispatch/DispatchCache.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "Optimization/ShaderStageInputInfo.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckRange(std::uint64_t bufferAddress, std::uint32_t stride, std::uint32_t records, std::uint64_t tableAddress, bool allowed) {
    using namespace ShaderRecompiler;
    using namespace AgcDriver::DriverDetail;
    ShaderBufferResource raw;
    raw.UpdateAddress48(bufferAddress);
    raw.fields[1] |= stride << 16u;
    raw.fields[2] = records;
    raw.fields[3] = 0x31016facu;
    auto compiled = std::make_shared<RecompileResult>();
    DescriptorBinding buffers;
    buffers.role = DescriptorRole::GuestBuffers;
    buffers.guestDescriptor.assign(raw.fields.begin(), raw.fields.end());
    compiled->bindings.push_back(buffers);
    auto flat = buffers;
    flat.role = DescriptorRole::FlattenedSrt;
    compiled->bindings.push_back(flat);
    compiled->bufferBaseSlots.push_back({0u, 0u, 0u});
    DispatchVariant variant;
    variant.compiled = compiled;
    variant.runs.emplace_back(tableAddress, tableAddress + 16u);
    variant.words.assign(raw.fields.begin(), raw.fields.end());
    const std::array words{BufferBaseWord{0u, tableAddress, 0, raw.Base48(), raw.GetSize(), true}};
    TagDrawBufferBases_nid_no_patch(variant, words, true);
    Check(variant.dataPositions.empty() != allowed, "captured buffer range was misclassified for base relocation");
    if (allowed) {
        Check(variant.dataPositions.size() == 1u && variant.dataPositions[0] == 0u, "base relocation did not select only the base word");
        Check(variant.bufferWordBuffers.size() == 1u && variant.bufferWordBuffers[0].size() == 2u, "base relocation did not patch both the buffer and flat SRT copy");
    }
    DispatchVariant unknown;
    unknown.compiled = compiled;
    unknown.runs = variant.runs;
    unknown.words = variant.words;
    const std::array missingRange{BufferBaseWord{0u, tableAddress, 0}};
    TagDrawBufferBases_nid_no_patch(unknown, missingRange, true);
    Check(unknown.dataPositions.empty(), "buffer relocation accepted an unknown buffer range");
}

void CheckPatchComposition(bool stored) {
    using namespace ShaderRecompiler;
    using namespace AgcDriver::DriverDetail;
    const std::array<std::uint32_t, 3> original{10u, 20u, 30u};
    auto compiled = std::make_shared<RecompileResult>();
    if (stored) {
        DescriptorBinding data;
        data.role = DescriptorRole::ShaderData;
        data.guestDescriptor.assign(original.begin(), original.end());
        compiled->bindings.push_back(data);
    } else {
        compiled->pushConstants.resize(sizeof(original));
        std::memcpy(compiled->pushConstants.data(), original.data(), sizeof(original));
    }
    DescriptorBinding buffers;
    buffers.role = DescriptorRole::GuestBuffers;
    buffers.guestDescriptor = {0x12340000u, 0u, 16u, 0x31016facu};
    const auto bufferBinding = static_cast<std::uint32_t>(compiled->bindings.size());
    compiled->bindings.push_back(buffers);
    DispatchVariant variant;
    variant.compiled = compiled;
    const auto check = [&](std::array<std::uint32_t, 3> expected) {
        if (stored) {
            Check(variant.compiled->bindings[0].guestDescriptor == std::vector<std::uint32_t>(expected.begin(), expected.end()), "stored shader-data patches discarded an earlier value");
            Check(variant.patchedPushConstants.empty(), "stored shader data retained a push overlay");
        } else {
            const auto& bytes = variant.patchedPushConstants.empty() ? variant.compiled->pushConstants : variant.patchedPushConstants;
            Check(bytes.size() == sizeof(expected), "shader-data patch changed the push range");
            std::array<std::uint32_t, 3> words;
            std::memcpy(words.data(), bytes.data(), sizeof(words));
            Check(words == expected, "push shader-data patches discarded an earlier value");
        }
        Check(variant.shaderDataPatches.empty() && variant.descriptorPatches.empty(), "applied patch queues were not consumed");
        Check(compiled->bindings[bufferBinding].guestDescriptor == buffers.guestDescriptor, "descriptor patch mutated the shared donor");
        if (stored) Check(compiled->bindings[0].guestDescriptor == std::vector<std::uint32_t>(original.begin(), original.end()), "shader-data patch mutated the stored donor");
        else Check(std::memcmp(compiled->pushConstants.data(), original.data(), sizeof(original)) == 0, "shader-data patch mutated the pushed donor");
    };
    variant.shaderDataPatches = {{0u, 100u}};
    Driver::applyShaderDataPatches(variant);
    check({100u, 20u, 30u});
    variant.shaderDataPatches = {{1u, 200u}};
    Driver::applyShaderDataPatches(variant);
    check({100u, 200u, 30u});
    variant.shaderDataPatches = {{2u, 300u}};
    variant.descriptorPatches = {{bufferBinding, 0u, 0x56780000u}};
    Driver::applyShaderDataPatches(variant);
    check({100u, 200u, 300u});
    Check(variant.compiled->bindings[bufferBinding].guestDescriptor[0] == 0x56780000u, "composed descriptor patch was not applied");
    Check(variant.patchedPushConstants.empty(), "descriptor composition did not consume the push overlay");
    const auto effective = variant.compiled;
    Driver::applyShaderDataPatches(variant);
    Check(variant.compiled == effective, "an empty patch pass replaced the effective compiled result");
    check({100u, 200u, 300u});
}

}

int main() {
    try {
        constexpr std::uint64_t base = 0x100004000ull;
        CheckRange(base, 0u, 64u, base, false);
        CheckRange(base, 0u, 64u, base + 32u, false);
        CheckRange(base, 0u, 64u, base - 15u, false);
        CheckRange(base, 0u, 64u, base + 64u, true);
        CheckRange(base, 0u, 64u, base - 16u, true);
        CheckRange(base, 16u, 4u, base + 32u, false);
        CheckRange(base, 16u, 4u, base + 64u, true);
        CheckRange(base, 16u, 4u, base - 16u, true);
        CheckRange(base, 0u, 64u, 0x200004000ull, true);
        CheckRange(base, 0x3fffu, 0xffffffffu, base + 0x100000000ull, false);
        CheckPatchComposition(false);
        CheckPatchComposition(true);
        std::cout << "Buffer base tagging preserves captured ranges; pushed and stored shader-data patches compose without changing donors\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
