#include "../PeerDatagram.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <winsock2.h>
#endif

static std::atomic<bool> failBackgroundAllocation{false};
static thread_local bool mainThread = false;

void* operator new(std::size_t size) {
    if (failBackgroundAllocation.load() && !mainThread) throw std::bad_alloc();
    if (void* allocation = std::malloc(size == 0 ? 1 : size)) return allocation;
    throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

static void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

int main() {
    mainThread = true;
    auto transport = NetPeer::DatagramTransport::Open(0, NetPeer::Address::Loopback(0, 0).ipv4);
    auto first = transport->Bind(7777);
    auto second = transport->Bind(8888);
    Require(first->TakePendingError() == 0 && second->TakePendingError() == 0, "new channels have no pending error");
    const std::array<std::uint8_t, 3> payload{1, 2, 3};
    failBackgroundAllocation.store(true);
    Require(second->SendTo(NetPeer::Address::Loopback(transport->LocalAddress().udpPort, 7777), payload).error == NetPeer::Error::None,
        "send real UDP datagram before receiver allocation failure");
    std::array<std::uint8_t, 8> buffer{};
    const auto result = first->Receive(buffer, false, false, std::chrono::seconds(2));
    failBackgroundAllocation.store(false);
#ifdef _WIN32
    constexpr int expected = WSAENOBUFS;
#else
    constexpr int expected = ENOMEM;
#endif
    Require(result.error == NetPeer::Error::Host && result.nativeError == expected,
        "reader allocation failure wakes the receiver with its native error");
    Require(first->TakePendingError() == expected && first->TakePendingError() == 0,
        "pending error is reported once per channel");
    Require(second->TakePendingError() == expected && second->TakePendingError() == 0,
        "one channel cannot consume another channel's pending error");
    const auto again = first->Receive(buffer, false, true);
    Require(again.error == NetPeer::Error::Host && again.nativeError == expected,
        "reading pending error preserves the terminal receive failure");
    transport->Close();
    std::puts("Peer allocation failure wakes channels and preserves independently consumed pending errors");
}
