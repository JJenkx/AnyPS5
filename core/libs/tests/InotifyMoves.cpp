#include "prx/libkernel/Equeue/InotifyMoves.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <vector>

static void Require(bool value, std::source_location where = std::source_location::current()) {
    if (!value) {
        std::fprintf(stderr, "inotify move assertion line %u\n", where.line());
        std::abort();
    }
}

int main() {
    using Moves = FileEvents::InotifyMoves;
    using namespace std::chrono_literals;
    const Moves::Clock::time_point start{};
    Moves moves;
    Require(moves.Timeout(start) == -1);
    Require(moves.Add(1, true, false, start).empty());
    Require(moves.Timeout(start) == 20);
    Require(moves.Add(1, false, false, start + 5ms) == std::vector<std::uint32_t>{0x2});
    Require(moves.Timeout(start) == -1 && moves.Expire(start + 100ms).empty());

    Require(moves.Add(2, true, true, start).empty());
    Require(moves.Add(3, true, false, start + 1ms).empty());
    Require(moves.Add(2, false, true, start + 2ms) == std::vector<std::uint32_t>{0x2});
    Require(moves.Expire(start + 20ms).empty());
    Require(moves.Timeout(start + 20ms) == 1);
    Require(moves.Expire(start + 21ms) == std::vector<std::uint32_t>{0x6});

    Require(moves.Add(4, false, true, start).empty());
    Require(moves.Timeout(start + 19500us) == 1);
    Require(moves.Expire(start + 19ms).empty());
    Require(moves.Timeout(start + 20ms) == 0);
    Require(moves.Expire(start + 20ms) == std::vector<std::uint32_t>{0x16});

    Require(moves.Add(5, true, false, start).empty());
    Require(moves.Add(5, false, false, start + 50ms) == std::vector<std::uint32_t>{0x2});
    Require(moves.Expire(start + 50ms).empty());

    Require(moves.Add(6, true, false, start).empty());
    Require(moves.Expire(start + 20ms) == std::vector<std::uint32_t>{0x6});
    Require(moves.Add(6, false, false, start + 22ms).empty());
    Require(moves.Expire(start + 42ms) == std::vector<std::uint32_t>{0x6});
    Require(moves.Add(0, false, true, start) == std::vector<std::uint32_t>{0x16});
    Require(moves.Timeout(start) == -1);

    Require(moves.Add(7, true, false, start).empty());
    Require(moves.Add(7, true, false, start + 5ms) == std::vector<std::uint32_t>{0x6});
    Require(moves.Expire(start + 20ms).empty());
    Require(moves.Expire(start + 25ms) == std::vector<std::uint32_t>{0x6});
    Require(moves.Add(8, true, true, start).empty());
    Require(moves.Expire(Moves::Clock::time_point::max()) == std::vector<std::uint32_t>{0x16});
    Require(moves.Timeout(start) == -1);
}
