#ifndef CORE_LIBS_PRX_LIBKERNEL_EQUEUE_INOTIFYMOVES_HPP
#define CORE_LIBS_PRX_LIBKERNEL_EQUEUE_INOTIFYMOVES_HPP

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace FileEvents {
namespace {

class InotifyMoves {
public:
    using Clock = std::chrono::steady_clock;

    std::vector<std::uint32_t> Add(std::uint32_t cookie, bool from, bool directory, Clock::time_point now) {
        const std::uint32_t hint = 0x0002 | 0x0004 | (directory ? 0x0010 : 0);
        if (cookie == 0) return {hint};
        std::vector<std::uint32_t> hints;
        const auto found = pending.find(cookie);
        if (found != pending.end()) {
            if (found->second.from != from) {
                pending.erase(found);
                return {0x0002};
            }
            hints.push_back(found->second.hint);
        }
        pending[cookie] = {from, hint, now + std::chrono::milliseconds(20)};
        return hints;
    }

    std::vector<std::uint32_t> Expire(Clock::time_point now) {
        std::vector<std::uint32_t> hints;
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->second.until <= now) {
                hints.push_back(it->second.hint);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
        return hints;
    }

    int Timeout(Clock::time_point now) const {
        int timeout = -1;
        for (const auto& [cookie, move] : pending) {
            if (move.until <= now) return 0;
            const auto remaining = static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(move.until - now).count());
            if (timeout < 0 || remaining < timeout) timeout = remaining;
        }
        return timeout;
    }

private:
    struct Pending {
        bool from;
        std::uint32_t hint;
        Clock::time_point until;
    };
    std::unordered_map<std::uint32_t, Pending> pending;
};

}
}

#endif
