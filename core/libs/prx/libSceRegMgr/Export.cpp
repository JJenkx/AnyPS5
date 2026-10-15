#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
constexpr int MissingKey = static_cast<int>(0x800d0203u);
constexpr int InvalidChecksum = static_cast<int>(0x800d0204u);
constexpr std::array<std::uint8_t, 16> FirstTable{0x68, 0xe8, 0x98, 0x03, 0x9a, 0x70, 0x23, 0x5a, 0x63, 0xee, 0xf5, 0x7b, 0xff, 0xa4, 0x4c, 0x8c};
constexpr std::array<std::uint8_t, 16> SecondTable{0x14, 0xee, 0xde, 0xe1, 0x80, 0xac, 0xf3, 0x78, 0x47, 0x43, 0xdb, 0x40, 0x93, 0xdd, 0xb1, 0x34};

std::unordered_map<std::uint32_t, std::int32_t> LoadIntegers() {
    std::unordered_map<std::uint32_t, std::int32_t> values;
    const auto* path = std::getenv("APS5_REGMGR_INTS");
    if (path == nullptr) return values;
    std::ifstream file(path);
    if (!file) throw std::runtime_error("RegMgr: cannot open integer settings");
    std::string text(1024 * 1024 + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (text.size() > 1024 * 1024) throw std::runtime_error("RegMgr: integer settings exceed one MiB");
    if (file.bad()) throw std::runtime_error("RegMgr: failed to read integer settings");
    std::istringstream rows(text);
    std::string line;
    while (std::getline(rows, line)) {
        std::istringstream fields(line);
        std::string keyText, valueText, extra;
        if (!(fields >> keyText)) continue;
        if (!(fields >> valueText) || fields >> extra) throw std::runtime_error("RegMgr: invalid integer setting");
        const bool hex = keyText.starts_with("0x");
        const auto* first = keyText.data() + (hex ? 2 : 0);
        const auto* last = keyText.data() + keyText.size();
        std::uint32_t key;
        const auto parsedKey = std::from_chars(first, last, key, hex ? 16 : 10);
        std::int32_t value;
        const auto parsedValue = std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
        if (parsedKey.ec != std::errc{} || parsedKey.ptr != last || parsedValue.ec != std::errc{} || parsedValue.ptr != valueText.data() + valueText.size()) {
            throw std::runtime_error("RegMgr: integer setting is out of range or malformed");
        }
        if (!values.emplace(key, value).second) throw std::runtime_error("RegMgr: duplicate integer setting");
    }
    return values;
}
}

extern "C" int APS5_VABI sceRegMgrNonSysGetInt(std::uint64_t request, std::uint32_t scope, std::int32_t* output) {
    if (output == nullptr) throw std::invalid_argument("RegMgr: null integer output");
    if (scope != 0) throw std::runtime_error("RegMgr: scoped non-system integers are not implemented");
    std::array<std::uint8_t, 6> encoded;
    for (std::size_t i = 0; i < encoded.size(); ++i) encoded[i] = static_cast<std::uint8_t>(request >> (i * 8));
    const auto checksum = static_cast<std::uint16_t>(encoded[0] + encoded[1] + encoded[2] + encoded[3] + encoded[4] * encoded[5]);
    if (checksum != request >> 48) return InvalidChecksum;
    const auto index = static_cast<unsigned>(encoded[5] ^ 0x6b);
    if (index > 12) throw std::runtime_error("RegMgr: unsupported request index");
    const auto kind = encoded[4] ^ FirstTable[15 - index];
    if (kind != 0x19 && kind != 0x72) throw std::runtime_error("RegMgr: unsupported request kind");
    const auto& table = kind == 0x19 ? SecondTable : FirstTable;
    const auto key = static_cast<std::uint32_t>(encoded[0] ^ table[index + 3]) |
        (static_cast<std::uint32_t>(encoded[2] ^ table[index + 2]) << 8) |
        (static_cast<std::uint32_t>(encoded[3] ^ table[index + 1]) << 16) |
        (static_cast<std::uint32_t>(encoded[1] ^ table[index]) << 24);
    static const auto integers = LoadIntegers();
    const auto found = integers.find(key);
    if (found == integers.end()) return MissingKey;
    std::memcpy(output, &found->second, sizeof(found->second));
    return 0;
}
