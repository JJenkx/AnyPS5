#pragma once

#include <cstdint>

struct GuestUuid {
    std::uint32_t timeLow;
    std::uint16_t timeMid;
    std::uint16_t timeHiAndVersion;
    std::uint8_t clockSeqHiAndReserved;
    std::uint8_t clockSeqLow;
    std::uint8_t node[6];
};
static_assert(sizeof(GuestUuid) == 16);
