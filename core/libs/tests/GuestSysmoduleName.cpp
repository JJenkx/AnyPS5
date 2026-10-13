#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

extern "C" {
int APS5_VABI sceSysmoduleLoadModuleByNameInternal(const char*, std::size_t, const void*, const void*, int*);
int APS5_VABI sceSysmoduleIsLoaded(std::uint16_t);
int APS5_VABI sceSysmoduleGetModuleHandleInternal(std::uint32_t, std::int32_t*);
int APS5_VABI sceSysmoduleUnloadModuleInternal(std::uint32_t);
int APS5_VABI sceKernelDlsym(int, const char*, void**);
int APS5_VABI sceKernelStopUnloadModule(int, std::size_t, const void*, unsigned, const void*, int*);
}

static void Require(bool value) { if (!value) throw std::runtime_error("Named sysmodule check failed"); }

int main() {
    int result = 123;
    for (const auto* name : {static_cast<const char*>(nullptr), "", ".", "..", "../module", "/module"}) {
        Require(sceSysmoduleLoadModuleByNameInternal(name, 0, nullptr, nullptr, &result) == static_cast<int>(0x805a1000u) && result == 123);
    }
    Require(sceSysmoduleLoadModuleByNameInternal("missing_named_fixture", 0, nullptr, nullptr, &result) == SCE_KERNEL_ERROR_ENOENT && result == 123);
    for (unsigned mode = 0; mode < 2; ++mode) {
        bool refused = false;
        try { sceSysmoduleLoadModuleByNameInternal("sysmodule_name_fixture", mode == 0 ? 0 : 4, nullptr, mode == 0 ? &result : nullptr, &result); }
        catch (const std::exception&) { refused = true; }
        Require(refused && result == 123);
    }
    const int argument = 42;
    alignas(4) std::array<std::uint8_t, 6> bytes;
    bytes.fill(0xa5);
    const int handle = sceSysmoduleLoadModuleByNameInternal("sysmodule_name_fixture", sizeof(argument), &argument, nullptr, reinterpret_cast<int*>(bytes.data() + 1));
    Require(handle > 0 && bytes.front() == 0xa5 && bytes.back() == 0xa5);
    std::memcpy(&result, bytes.data() + 1, sizeof(result));
    Require(result == 17);
    void* address = nullptr;
    Require(sceKernelDlsym(handle, "SysmoduleNameFixtureValue_nid_no_patch", &address) == 0 && address != nullptr);
    using Query = int (APS5_VABI *)();
    Require(reinterpret_cast<Query>(address)() == argument);
    Require(sceKernelStopUnloadModule(handle, 0, nullptr, 0, nullptr, nullptr) == 0);
    Require(sceSysmoduleIsLoaded(6) == static_cast<int>(0x805a1001u));
    const int fiber = sceSysmoduleLoadModuleByNameInternal("libSceFiber", 0, nullptr, nullptr, nullptr);
    Require(fiber > 0 && sceSysmoduleIsLoaded(6) == 0);
    std::int32_t named = 0;
    Require(sceSysmoduleGetModuleHandleInternal(6, &named) == 0 && named == fiber);
    Require(sceSysmoduleUnloadModuleInternal(6) == 0 && sceSysmoduleIsLoaded(6) == static_cast<int>(0x805a1001u));
    Require(sceKernelStopUnloadModule(fiber, 0, nullptr, 0, nullptr, nullptr) == 0);
}
