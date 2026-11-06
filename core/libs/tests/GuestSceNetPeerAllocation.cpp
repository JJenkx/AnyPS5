#include "prx/libSceNet/libSceNet.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

static void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

int main() {
    Require(sceNetInit_nid_postfix() == 0, "initialize network");
    for (const auto [type, protocol] : std::array{std::pair{6, 0}, std::pair{10, 0}, std::pair{10, 6}}) {
        const int descriptor = sceNetSocket("peer-allocation", 2, type, protocol);
        Require(descriptor >= 0, "allocate peer descriptor");
        int actualType = -1;
        std::uint32_t length = sizeof(actualType);
        Require(sceNetGetsockopt(descriptor, 0xffff, 0x1008, &actualType, &length) == 0,
            "read peer socket type");
        Require(actualType == type && length == sizeof(actualType), "retain guest peer type");
        Require(sceNetSocketClose(descriptor) == 0, "close peer descriptor");
    }
    Require(sceNetSocket("peer-datagram-udp", 2, 6, 17) == static_cast<int>(0x8041012bu),
        "refuse measured peer datagram protocol");
    Require(*sceNetErrnoLoc() == 43, "retain peer protocol errno");
}
