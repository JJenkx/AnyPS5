#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include "tests/VideoOutTestEnvironment.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

extern "C" int APS5_VABI getpid_nid_postfix();
extern "C" int APS5_VABI sceKernelGetAppCategoryType(int, std::uint32_t*);

static void Require(bool value) { if (!value) throw std::runtime_error("Application category check failed"); }

int main(int argc, char** argv) {
    Require(argc == 2);
    VideoOutTestEnvironment environment;
    const std::string value(argv[1]);
    {
        std::ofstream param("app0/sce_sys/param.json");
        param << R"({"titleId":"PPSA00000","localizedParameters":{"en-US":{"titleName":"Example"}})";
        if (value != "missing") param << ",\"applicationCategoryType\":" << value;
        param << '}';
        Require(static_cast<bool>(param));
    }
    std::uint32_t output = 0xa5a5a5a5;
    Require(sceKernelGetAppCategoryType(-2, &output) == SCE_KERNEL_ERROR_EPERM && output == 0xa5a5a5a5);
    bool nullRejected = false;
    try { sceKernelGetAppCategoryType(getpid_nid_postfix(), nullptr); } catch (const std::invalid_argument&) { nullRejected = true; }
    Require(nullRejected);
    if (value == "missing" || value == "-1" || value == "4294967296" || value == "true" || value == "1.5") {
        bool rejected = false;
        try { sceKernelGetAppCategoryType(getpid_nid_postfix(), &output); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected && output == 0xa5a5a5a5);
        return 0;
    }
    std::uint32_t expected;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), expected);
    Require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size());
    std::array<std::thread, 4> callers;
    for (auto& caller : callers) caller = std::thread([expected] {
        alignas(4) std::array<std::uint8_t, 6> bytes;
        bytes.fill(0xa5);
        Require(sceKernelGetAppCategoryType(getpid_nid_postfix(), reinterpret_cast<std::uint32_t*>(bytes.data() + 1)) == 0);
        std::uint32_t category;
        std::memcpy(&category, bytes.data() + 1, sizeof(category));
        Require(category == expected && bytes.front() == 0xa5 && bytes.back() == 0xa5);
    });
    for (auto& caller : callers) caller.join();
}
