#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

extern "C" int APS5_VABI sceKernelGetCompiledSdkVersion_nid_postfix(std::int32_t*);
namespace {
alignas(8) std::array<std::byte, 0x41> metadata{};
unsigned calls = 0;
bool refuse = false;
void require(bool ok) { if (!ok) throw std::runtime_error("compiled SDK test failed"); }
template<typename T> void put(std::size_t offset, T value) { std::memcpy(metadata.data() + 1 + offset, &value, sizeof(value)); }
}
const void* ApplicationProcessParameters_nid_no_patch() {
    ++calls;
    if (refuse) throw std::runtime_error("synthetic reader refusal");
    return metadata.data() + 1;
}
int main() {
    require(sceKernelGetCompiledSdkVersion_nid_postfix(nullptr) == SCE_KERNEL_ERROR_EINVAL && calls == 0);
    put(0, std::uint64_t{0x40});
    put(8, std::uint32_t{0x4942524f});
    for (auto sdk : {std::uint64_t{0}, std::uint64_t{0x12345678}, std::uint64_t{0xabcdef0187654321}}) {
        put(0, sdk == 0 ? std::uint64_t{0x100000000} : std::uint64_t{0x40});
        put(0x10, sdk);
        alignas(4) std::array<std::byte, 6> output;
        output.fill(std::byte{0xa5});
        require(sceKernelGetCompiledSdkVersion_nid_postfix(reinterpret_cast<std::int32_t*>(output.data() + 1)) == 0);
        std::uint32_t result;
        std::memcpy(&result, output.data() + 1, sizeof(result));
        require(result == static_cast<std::uint32_t>(sdk) && output.front() == std::byte{0xa5} && output.back() == std::byte{0xa5});
    }
    for (unsigned mode = 0; mode != 5; ++mode) {
        put(0, std::uint64_t{mode == 0 ? 0x3fU : 0x40U});
        put(8, std::uint32_t{mode == 1 ? 0U : mode == 3 ? 0x4f524249U : mode == 4 ? 0x4942524eU : 0x4942524fU});
        refuse = mode == 2;
        alignas(4) std::array<std::byte, 6> output;
        output.fill(std::byte{0xa5});
        bool rejected = false;
        try { sceKernelGetCompiledSdkVersion_nid_postfix(reinterpret_cast<std::int32_t*>(output.data() + 1)); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected);
        for (auto byte : output) require(byte == std::byte{0xa5});
    }
    require(sceKernelGetCompiledSdkVersion_nid_postfix(nullptr) == SCE_KERNEL_ERROR_EINVAL && calls == 8);
}
