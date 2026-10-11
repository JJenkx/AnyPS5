#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

extern "C" int APS5_VABI sceKernelGetCompiledSdkVersion_nid_postfix(std::int32_t* version) {
    if (version == nullptr) return SCE_KERNEL_ERROR_EINVAL;
    const auto* parameters = static_cast<const std::byte*>(ApplicationProcessParameters_nid_no_patch());
    std::uint64_t size;
    std::uint32_t magic;
    std::uint64_t sdk;
    std::memcpy(&size, parameters, sizeof(size));
    std::memcpy(&magic, parameters + 8, sizeof(magic));
    if (size < 0x40 || magic != 0x4942524f) throw std::runtime_error("compiled SDK: unsupported process parameters");
    std::memcpy(&sdk, parameters + 0x10, sizeof(sdk));
    const auto result = static_cast<std::uint32_t>(sdk);
    std::memcpy(version, &result, sizeof(result));
    return 0;
}
