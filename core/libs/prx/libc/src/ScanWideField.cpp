#include "prx/libc/include/ScanWideField.hpp"
#include "prx/libc/include/General.hpp"

#include <cstring>

namespace LibcDetail {

bool InWideScanset(const std::string& specifier, unsigned char byte) {
    std::size_t index = 1;
    const bool negated = specifier[index] == '^';
    if (negated) ++index;
    const std::size_t end = specifier.size() - 1;
    bool member = false;
    for (std::size_t first = index; index < end; ++index) {
        const auto value = static_cast<unsigned char>(specifier[index]);
        if (specifier[index] == '-' && index != first && index + 1 < end) {
            const auto low = static_cast<unsigned char>(specifier[index - 1]);
            const auto high = static_cast<unsigned char>(specifier[index + 1]);
            if (byte >= low && byte <= high) member = true;
            ++index;
            continue;
        }
        if (byte == value) member = true;
    }
    return member != negated;
}

void RequireNoPositionalWideScan(const char* format) {
    if (std::strchr(format, '$') != nullptr) NotImplemented_nid_no_patch("sscanf positional arguments with a 16-bit wide conversion");
}

}
