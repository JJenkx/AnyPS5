#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <windows.h>

extern "C" int APS5_VABI sceKernelGetCompiledSdkVersion_nid_postfix(std::int32_t*);
[[gnu::section(".procpar"), gnu::used]] std::uint32_t descriptor[2]{};
namespace {
alignas(8) std::array<std::byte, 0x41> metadata{};
void require(bool ok) { if (!ok) throw std::runtime_error("compiled SDK process test failed"); }
template<typename T> void put(std::size_t offset, T value) { std::memcpy(metadata.data() + 1 + offset, &value, sizeof(value)); }
}
int main() {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    descriptor[0] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(metadata.data() + 1) - base);
    descriptor[1] = 0x40;
    put(0, std::uint64_t{0x40});
    put(8, std::uint32_t{0x4942524f});
    put(0x10, std::uint64_t{0xdeadbeef10203040});
    alignas(4) std::array<std::byte, 6> output;
    output.fill(std::byte{0xa5});
    require(sceKernelGetCompiledSdkVersion_nid_postfix(reinterpret_cast<std::int32_t*>(output.data() + 1)) == 0);
    std::uint32_t result;
    std::memcpy(&result, output.data() + 1, sizeof(result));
    require(result == 0x10203040 && output.front() == std::byte{0xa5} && output.back() == std::byte{0xa5});
    descriptor[0] = 0xffffffff;
    require(sceKernelGetCompiledSdkVersion_nid_postfix(nullptr) == SCE_KERNEL_ERROR_EINVAL);
    output.fill(std::byte{0xa5});
    bool rejected = false;
    try { sceKernelGetCompiledSdkVersion_nid_postfix(reinterpret_cast<std::int32_t*>(output.data() + 1)); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
    for (auto byte : output) require(byte == std::byte{0xa5});
}
