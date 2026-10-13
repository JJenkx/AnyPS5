#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

extern "C" int APS5_VABI sceRegMgrNonSysGetInt(std::uint64_t, std::uint32_t, std::int32_t*);

static void Require(bool value) { if (!value) throw std::runtime_error("RegMgr integer check failed"); }

int main(int argc, char** argv) {
    Require(argc == 2);
    const std::string mode(argv[1]);
    const std::array<std::uint64_t, 3> requests{0x33236083918938b1, 0x508d69bde3d3a6ac, 0x34ae6c7a451600db};
    const std::array<std::int32_t, 3> expected{2, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()};
    std::int32_t value = 123;
    Require(sceRegMgrNonSysGetInt(requests[0] ^ (1ull << 48), 0, &value) == static_cast<int>(0x800d0204u) && value == 123);
    bool unsupported = false;
    try { sceRegMgrNonSysGetInt(requests[0], 1, &value); } catch (const std::runtime_error&) { unsupported = true; }
    Require(unsupported && value == 123);
    bool invalid = false;
    try { sceRegMgrNonSysGetInt(requests[0], 0, nullptr); } catch (const std::invalid_argument&) { invalid = true; }
    Require(invalid);
    for (const std::uint64_t request : {std::uint64_t{0}, std::uint64_t{0x00006b0000000000}}) {
        bool refused = false;
        try { sceRegMgrNonSysGetInt(request, 0, &value); } catch (const std::runtime_error&) { refused = true; }
        Require(refused && value == 123);
    }
    if (mode == "invalid" || mode == "duplicate") {
        bool refused = false;
        try { sceRegMgrNonSysGetInt(requests[0], 0, &value); } catch (const std::runtime_error&) { refused = true; }
        Require(refused && value == 123);
        return 0;
    }
    Require(sceRegMgrNonSysGetInt(0x40aa6b95cced05a5, 0, &value) == static_cast<int>(0x800d0203u) && value == 123);
    if (mode == "populated") Require(sceRegMgrNonSysGetInt(0x2ff26771a64ffe88, 0, &value) == 0 && value == 0);
    for (std::size_t i = 0; i < requests.size(); ++i) {
        alignas(4) std::array<std::uint8_t, 6> bytes;
        bytes.fill(0xa5);
        const int result = sceRegMgrNonSysGetInt(requests[i], 0, reinterpret_cast<std::int32_t*>(bytes.data() + 1));
        if (mode == "empty") {
            Require(result == static_cast<int>(0x800d0203u));
            for (const auto byte : bytes) Require(byte == 0xa5);
        } else {
            Require(mode == "populated" && result == 0 && bytes.front() == 0xa5 && bytes.back() == 0xa5);
            std::memcpy(&value, bytes.data() + 1, sizeof(value));
            Require(value == expected[i]);
        }
    }
}
