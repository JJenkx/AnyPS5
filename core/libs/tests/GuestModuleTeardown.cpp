#include "SceTypes.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

namespace {
using Open = void* (APS5_VABI*)(const char*, int);
using Symbol = void* (APS5_VABI*)(void*, const char*);
using Close = int (APS5_VABI*)(void*);
using Query = int (APS5_VABI*)(std::uint64_t, int, ModuleInfoEx*);
using Add = int (APS5_VABI*)(int, int);

Symbol symbol = nullptr;
Close closeModule = nullptr;
Query query = nullptr;
void* module = nullptr;
void* address = nullptr;
KernelModule selfId = 0;

[[noreturn]] void Fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    std::fflush(stderr);
    std::_Exit(1);
}

template<typename T>
T Resolve(void* library, const char* name) {
    auto* result = ::dlsym(library, name);
    if (result == nullptr) Fail(name);
    return reinterpret_cast<T>(result);
}

KernelModule ModuleId(const void* pointer) {
    ModuleInfoEx info{};
    info.st_size = sizeof(info);
    if (query(reinterpret_cast<std::uintptr_t>(pointer), 2, &info) != 0) Fail("module query failed");
    return info.id;
}

void UseAfterStaticTeardown() {
    if (symbol(module, "GuestModuleAdd") != address) Fail("module handle was lost during static teardown");
    if (reinterpret_cast<Add>(address)(19, 23) != 42) Fail("module is no longer callable");
    if (ModuleId(address) != static_cast<KernelModule>(reinterpret_cast<std::uintptr_t>(module))) Fail("loaded module identity changed during static teardown");
    if (ModuleId(reinterpret_cast<const void*>(&ModuleId)) != selfId) Fail("image identity changed during static teardown");
    if (closeModule(module) != 0) Fail("explicit close failed after static teardown");
    if (closeModule(module) != -1) Fail("closed handle remains valid");
}
}

int main(int argc, char** argv) {
    if (argc != 3) Fail("usage: guest_module_teardown_tests <libkernel> <fixture>");
    if (std::atexit(UseAfterStaticTeardown) != 0) Fail("atexit failed");
    auto* library = ::dlopen(argv[1], RTLD_NOW);
    if (library == nullptr) Fail(::dlerror());
    const auto openModule = Resolve<Open>(library, "dlopen_nid_postfix");
    symbol = Resolve<Symbol>(library, "dlsym_nid_postfix");
    closeModule = Resolve<Close>(library, "dlclose_nid_postfix");
    query = Resolve<Query>(library, "sceKernelGetModuleInfoFromAddr");
    module = openModule(argv[2], 2);
    if (module == nullptr) Fail("fixture load failed");
    address = symbol(module, "GuestModuleAdd");
    if (address == nullptr) Fail("fixture symbol missing");
    selfId = ModuleId(reinterpret_cast<const void*>(&ModuleId));
    if (ModuleId(address) != static_cast<KernelModule>(reinterpret_cast<std::uintptr_t>(module))) Fail("initial module identity mismatch");
    return 0;
}
