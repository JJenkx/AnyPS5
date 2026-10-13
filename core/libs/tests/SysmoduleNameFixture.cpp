#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/Module/ModuleArgs.hpp"
#include <cstdint>
#include <cstring>

extern "C" int APS5_VABI sceSysmoduleIsLoaded(std::uint16_t);

static const int initialized = [] {
    const auto* pending = static_cast<const PendingModuleArgs*>(__aps5_get_pending_module_args_nid_no_patch());
    int value = -1;
    if (pending != nullptr && pending->args == sizeof(value) && pending->argp != nullptr) std::memcpy(&value, pending->argp, sizeof(value));
    if (sceSysmoduleIsLoaded(6) != static_cast<int>(0x805a1001u)) value = -2;
    __aps5_set_module_init_result_nid_no_patch(17);
    return value;
}();

extern "C" int APS5_VABI SysmoduleNameFixtureValue_nid_no_patch() { return initialized; }
