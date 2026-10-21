#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_REFERENCESTORAGE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_REFERENCESTORAGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace AgcDriver::Graphics {

class ReferenceStorage {
public:
    static constexpr std::size_t Slots = 8;

    explicit ReferenceStorage(std::size_t capacityBytes) noexcept;
    ReferenceStorage(const ReferenceStorage&) = delete;
    ReferenceStorage& operator=(const ReferenceStorage&) = delete;

    std::vector<std::byte> Take(std::size_t bytes) noexcept;
    void Return(std::vector<std::byte>&& storage) noexcept;
    std::size_t IdleBytes() const noexcept;
    std::size_t IdleCount() const noexcept;

    static ReferenceStorage& Shared() noexcept;

private:
    mutable std::mutex mutex;
    std::array<std::vector<std::byte>, Slots> idle;
    std::array<std::uint64_t, Slots> returned{};
    std::uint64_t returns = 0;
    std::size_t bytes = 0;
    std::size_t capacityBytes;
};

}

#endif
