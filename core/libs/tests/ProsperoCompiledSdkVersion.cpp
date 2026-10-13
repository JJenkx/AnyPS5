#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

extern "C" int APS5_VABI sceKernelGetProsperoCompiledSdkVersion(std::uint32_t*);

namespace {
alignas(8) std::array<std::byte, 0x41> metadata{};
bool refuse = false;
unsigned reads = 0;
void Require(bool ok) { if (!ok) throw std::runtime_error("Prospero compiled SDK check failed"); }
template <typename TValue>
void Put(std::size_t offset, TValue value) { std::memcpy(metadata.data() + 1 + offset, &value, sizeof(value)); }
}

const void* ApplicationProcessParameters_nid_no_patch() {
    ++reads;
    if (refuse) throw std::runtime_error("metadata reader refused");
    return metadata.data() + 1;
}

int main() {
    Require(sceKernelGetProsperoCompiledSdkVersion(nullptr) == SCE_KERNEL_ERROR_EINVAL && reads == 0);
    Put(0, std::uint64_t{0x40});
    Put(8, std::uint32_t{0x4942524f});
    for (const auto packed : {std::uint64_t{0}, std::uint64_t{0x0400003109040001}, std::uint64_t{0xfedcba9800000000}}) {
        Put(0x10, packed);
        alignas(4) std::array<std::byte, 6> output;
        output.fill(std::byte{0xa5});
        Require(sceKernelGetProsperoCompiledSdkVersion(reinterpret_cast<std::uint32_t*>(output.data() + 1)) == 0);
        std::uint32_t value;
        std::memcpy(&value, output.data() + 1, sizeof(value));
        Require(value == packed >> 32 && output.front() == std::byte{0xa5} && output.back() == std::byte{0xa5});
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        Put(0, std::uint64_t{mode == 0 ? 0x3fU : 0x40U});
        Put(8, std::uint32_t{mode == 1 ? 0U : 0x4942524fU});
        refuse = mode == 2;
        std::uint32_t output = 0xa5a5a5a5;
        bool rejected = false;
        try { sceKernelGetProsperoCompiledSdkVersion(&output); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected && output == 0xa5a5a5a5);
    }
    Require(sceKernelGetProsperoCompiledSdkVersion(nullptr) == SCE_KERNEL_ERROR_EINVAL && reads == 6);
}
