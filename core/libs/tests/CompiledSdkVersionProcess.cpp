#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" int APS5_VABI sceKernelGetCompiledSdkVersion_nid_postfix(std::int32_t*);
#ifdef _WIN32
[[gnu::section(".procpar"), gnu::used]] std::uint32_t descriptor[2]{};
#endif
namespace {
#ifdef __linux__
[[gnu::section(".sdkproc"), gnu::used]]
#endif
alignas(8) std::array<std::byte, 0x41> metadata{};
void require(bool ok) { if (!ok) throw std::runtime_error("compiled SDK process test failed"); }
template<typename T> void put(std::size_t offset, T value) { std::memcpy(metadata.data() + 1 + offset, &value, sizeof(value)); }
void requireRejected() {
    alignas(4) std::array<std::byte, 6> output;
    output.fill(std::byte{0xa5});
    bool rejected = false;
    try { sceKernelGetCompiledSdkVersion_nid_postfix(reinterpret_cast<std::int32_t*>(output.data() + 1)); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
    for (auto byte : output) require(byte == std::byte{0xa5});
}
}
int main(int argc, char** argv) {
    require(sceKernelGetCompiledSdkVersion_nid_postfix(nullptr) == SCE_KERNEL_ERROR_EINVAL);
#ifdef _WIN32
    (void)argv;
    require(argc == 1);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    descriptor[0] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(metadata.data() + 1) - base);
    descriptor[1] = 0x40;
#elif defined(__linux__)
    require(argc == 1 || (argc == 2 && std::strcmp(argv[1], "reader-reject") == 0));
    if (argc == 2) {
        alignas(4) std::array<std::byte, 6> output;
        output.fill(std::byte{0xa5});
        bool rejected = false;
        try { sceKernelGetCompiledSdkVersion_nid_postfix(reinterpret_cast<std::int32_t*>(output.data() + 1)); }
        catch (const std::runtime_error& error) {
            rejected = std::strcmp(error.what(), "application heap: invalid or missing process parameters") == 0;
        }
        require(rejected);
        for (auto byte : output) require(byte == std::byte{0xa5});
        std::cout << "compiled SDK real-reader rejection passed\n";
        return 0;
    }
#endif
    put(0, std::uint64_t{0x40});
    put(8, std::uint32_t{0x4942524f});
    put(0x10, std::uint64_t{0xdeadbeef10203040});
    require(ApplicationProcessParameters_nid_no_patch() == metadata.data() + 1);
    alignas(4) std::array<std::byte, 6> output;
    output.fill(std::byte{0xa5});
    require(sceKernelGetCompiledSdkVersion_nid_postfix(reinterpret_cast<std::int32_t*>(output.data() + 1)) == 0);
    std::uint32_t result;
    std::memcpy(&result, output.data() + 1, sizeof(result));
    require(result == 0x10203040 && output.front() == std::byte{0xa5} && output.back() == std::byte{0xa5});
    put(0, std::uint64_t{0x3f});
    requireRejected();
    put(0, std::uint64_t{0x40});
    put(8, std::uint32_t{0});
    requireRejected();
#ifdef _WIN32
    descriptor[0] = 0xffffffff;
    require(sceKernelGetCompiledSdkVersion_nid_postfix(nullptr) == SCE_KERNEL_ERROR_EINVAL);
    requireRejected();
#endif
    std::cout << "compiled SDK real-reader positive passed\n";
}
