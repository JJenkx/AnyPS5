#include "../PeerDatagram.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <system_error>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace NetPeer;
using namespace std::chrono_literals;

static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static bool Ready(std::uintptr_t socket, int timeout = 0) {
#ifdef _WIN32
    WSAPOLLFD descriptor{};
    descriptor.fd = static_cast<SOCKET>(socket);
    descriptor.events = POLLRDNORM;
    return WSAPoll(&descriptor, 1, timeout) > 0 && (descriptor.revents & POLLRDNORM) != 0;
#else
    pollfd descriptor{};
    descriptor.fd = static_cast<int>(socket);
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, timeout) > 0 && (descriptor.revents & POLLIN) != 0;
#endif
}

template<typename TPredicate>
static void WaitFor(TPredicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        Require(std::chrono::steady_clock::now() < deadline, "bounded async observation expired");
        std::this_thread::sleep_for(1ms);
    }
}

struct RawPeer {
#ifdef _WIN32
    using Socket = SOCKET;
    using Length = int;
#else
    using Socket = int;
    using Length = socklen_t;
#endif
    Socket socket;
    std::uint16_t port;
    RawPeer() {
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Require(socket != static_cast<Socket>(-1), "create independent UDP peer");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Require(::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "bind independent peer");
        Length size = sizeof(address);
        Require(::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0, "read independent peer port");
        port = ntohs(address.sin_port);
    }
    ~RawPeer() {
#ifdef _WIN32
        closesocket(socket);
#else
        ::close(socket);
#endif
    }
    void Send(std::uint16_t destination, std::span<const std::uint8_t> bytes) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(destination);
        Require(::sendto(socket, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
            reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == static_cast<int>(bytes.size()), "send independent wire frame");
    }
    std::vector<std::uint8_t> Receive(std::uint16_t expectedPort) {
        Require(Ready(static_cast<std::uintptr_t>(socket), 2000), "independent peer receives frame");
        std::array<std::uint8_t, 64> bytes{};
        sockaddr_in address{};
        Length size = sizeof(address);
        const int count = static_cast<int>(::recvfrom(socket, reinterpret_cast<char*>(bytes.data()),
            static_cast<int>(bytes.size()), 0, reinterpret_cast<sockaddr*>(&address), &size));
        Require(count >= 0 && ntohs(address.sin_port) == expectedPort, "wire source is shared transport port");
        return {bytes.begin(), bytes.begin() + count};
    }
};

static void CheckCodec() {
    const std::array<std::uint8_t, 3> payload{'x', 0, 'y'};
    const auto plain = EncodeDatagram(7777, 8888, payload);
    Require(plain == std::vector<std::uint8_t>({0xff, 0x83, 0x1e, 0x61, 0x22, 0xb8, 'x', 0, 'y'}), "plain wire bytes and port byte order");
    const CommunicationId identifier{1, 2, 3, 4};
    const auto tagged = EncodeDatagram(7777, 8888, payload, identifier);
    Require(tagged == std::vector<std::uint8_t>({0xff, 0xa3, 0x1e, 0x61, 0x22, 0xb8, 1, 2, 3, 4, 'x', 0, 'y'}), "tagged wire bytes");
    const auto frame = DecodeDatagram(tagged, identifier);
    Require(frame.status == FrameStatus::Valid && frame.source == 7777 && frame.destination == 8888 &&
        frame.payload == std::vector<std::uint8_t>(payload.begin(), payload.end()), "decode tagged wire frame");
    Require(DecodeDatagram(plain, identifier).status == FrameStatus::Filtered, "missing required communication identifier");
    Require(DecodeDatagram(tagged, CommunicationId{4, 3, 2, 1}).status == FrameStatus::Filtered, "mismatched identifier");
    for (std::size_t count = 0; count < 6; ++count)
        Require(DecodeDatagram(std::span(plain.data(), count)).status == FrameStatus::Malformed, "reject short plain frame");
    for (std::size_t count = 2; count < 10; ++count)
        Require(DecodeDatagram(std::span(tagged.data(), count)).status == FrameStatus::Malformed, "reject short tagged frame");
    for (const std::uint8_t flags : {0x84, 0x85, 0x86, 0xc3, 0x87}) {
        auto unknown = plain;
        unknown[1] = flags;
        Require(DecodeDatagram(unknown).status == FrameStatus::Unsupported, "encrypted, signed, broadcast and stream frames remain explicit unsupported kinds");
    }
}

static void CheckTransport() {
    auto first = DatagramTransport::Open(0);
    auto second = DatagramTransport::Open(0);
    const auto firstPort = first->LocalAddress().udpPort;
    const auto secondPort = second->LocalAddress().udpPort;
    auto source = first->Bind(1111);
    auto firstGame = DatagramChannel::Create();
    const auto beforeBind = firstGame->NotificationSocket();
    Require(firstGame->Number() == 0 && !Ready(beforeBind), "unbound channel has stable empty poll handle");
    first->BindChannel(firstGame, 7777);
    Require(firstGame->Number() == 7777 && firstGame->NotificationSocket() == beforeBind,
        "bind preserves pre-registered native poll handle");
    auto target = second->Bind(2222);
    auto secondGame = second->Bind(7777);
    const std::array<std::uint8_t, 4> payload{10, 20, 30, 40};
    Require(source->SendTo(Address::Loopback(secondPort, 2222), payload).error == Error::None, "send through actual UDP transport");
    Require(Ready(target->NotificationSocket(), 2000) && !Ready(secondGame->NotificationSocket()), "readiness belongs to destination virtual channel");
    std::array<std::uint8_t, 8> output{};
    auto result = target->Receive(output, false, false, 2s);
    Require(result.error == Error::None && result.size == payload.size() && !result.truncated &&
        result.peer.udpPort == firstPort && result.peer.channel == 1111 &&
        std::equal(payload.begin(), payload.end(), output.begin()), "receive payload and real source endpoint");
    Require(!Ready(target->NotificationSocket()), "dequeue clears readiness");
    RawPeer raw;
    const std::array<std::uint8_t, 9> wire{0xff, 0x83, 0, 0xa1, 0x1e, 0x61, 'r', 'a', 'w'};
    raw.Send(firstPort, wire);
    result = firstGame->Receive(output, false, false, 2s);
    Require(result.error == Error::None && result.size == 3 && result.peer.udpPort == raw.port && result.peer.channel == 161 &&
        output[0] == 'r' && output[1] == 'a' && output[2] == 'w', "independent wire peer interoperates");
    Require(firstGame->SendTo(result.peer, payload).error == Error::None, "reply to independent peer");
    Require(raw.Receive(firstPort) == std::vector<std::uint8_t>({0xff, 0x83, 0x1e, 0x61, 0, 0xa1, 10, 20, 30, 40}), "independent peer sees exact response frame");

    Require(source->SendTo(Address::Loopback(secondPort, 2222), payload).error == Error::None, "queue peek packet");
    output.fill(0xa5);
    result = target->Receive(std::span(output.data(), 2), true, false, 2s);
    Require(result.error == Error::None && result.size == 2 && result.truncated && output[0] == 10 && output[1] == 20 &&
        output[2] == 0xa5 && Ready(target->NotificationSocket()), "peek preserves datagram and output tail");
    result = target->Receive(std::span(output.data(), 1), false, true);
    Require(result.error == Error::None && result.size == 1 && result.truncated && !Ready(target->NotificationSocket()), "short receive consumes whole datagram");
    Require(target->Receive(output, false, true).error == Error::WouldBlock, "empty nonblocking channel");
    Require(target->Receive(output, false, false, 10ms).error == Error::WouldBlock, "receive deadline");
    Require(source->SendTo(Address::Loopback(secondPort, 2222), {}).error == Error::None, "send zero length datagram");
    result = target->Receive(output, false, false, 2s);
    Require(result.error == Error::None && result.size == 0 && !Ready(target->NotificationSocket()), "zero length datagram is real queued data");
    std::vector<std::uint8_t> excessive(65502);
    Require(source->SendTo(Address::Loopback(secondPort, 2222), excessive).error == Error::MessageTooLarge, "enforce framed UDP payload bound");

    bool duplicate = false;
    try { second->Bind(2222); }
    catch (const std::system_error& error) { duplicate = error.code() == std::errc::address_in_use; }
    Require(duplicate, "duplicate channel registration fails");
    target->Close();
    auto replacement = second->Bind(2222);
    target.reset();
    Require(source->SendTo(Address::Loopback(secondPort, 2222), payload).error == Error::None, "send after channel reuse");
    Require(replacement->Receive(output, false, false, 2s).error == Error::None, "old channel destructor cannot unregister replacement");

    auto limited = second->Bind(3333, 4);
    const auto dropped = second->GetStatistics().dropped;
    const std::array<std::uint8_t, 5> tooLarge{1, 2, 3, 4, 5};
    Require(source->SendTo(Address::Loopback(secondPort, 3333), tooLarge).error == Error::None, "UDP sender may send beyond receiver capacity");
    WaitFor([&] { return second->GetStatistics().dropped > dropped; });
    Require(!Ready(limited->NotificationSocket()), "dropped packet does not produce readiness");
    Require(source->SendTo(Address::Loopback(secondPort, 3333), payload).error == Error::None, "queue full-capacity packet");
    Require(Ready(limited->NotificationSocket(), 2000), "capacity packet readable");
    Require(source->SendTo(Address::Loopback(secondPort, 3333), std::span(payload.data(), 1)).error == Error::None, "send to full receiver");
    WaitFor([&] { return second->GetStatistics().dropped > dropped + 1; });
    Require(limited->Receive(output, false, true).size == 4, "full queue preserves older packet");

    auto pending = std::async(std::launch::async, [&] { return limited->Receive(output, false, false, 2s); });
    Require(pending.wait_for(10ms) == std::future_status::timeout, "receive is pending before close");
    limited->Close();
    Require(pending.wait_for(1s) == std::future_status::ready && pending.get().error == Error::Closed, "channel close wakes blocked receiver");
    Require(source->SendTo(Address::Loopback(secondPort, 2222), payload).error == Error::None &&
        replacement->Receive(output, false, false, 2s).error == Error::None, "closing one channel preserves shared transport");

    const CommunicationId identifier{8, 7, 6, 5};
    first->SetCommunicationId(identifier);
    second->SetCommunicationId(identifier);
    Require(source->SendTo(Address::Loopback(secondPort, 2222), payload).error == Error::None &&
        replacement->Receive(output, false, false, 2s).error == Error::None, "tagged transports interoperate");
    const auto filtered = second->GetStatistics().filtered;
    raw.Send(secondPort, std::array<std::uint8_t, 7>{0xff, 0x83, 0, 1, 8, 0xae, 1});
    WaitFor([&] { return second->GetStatistics().filtered > filtered; });
    Require(!Ready(replacement->NotificationSocket()), "filtered frame is not queued");
    first->SetCommunicationId({});
    second->SetCommunicationId({});

    auto sender = std::async(std::launch::async, [&] {
        for (std::uint8_t i = 0; i < 30; ++i)
            Require(source->SendTo(Address::Loopback(secondPort, 2222), std::span(&i, 1)).error == Error::None, "concurrent UDP send");
    });
    for (std::uint8_t i = 0; i < 30; ++i) {
        result = replacement->Receive(output, false, false, 2s);
        Require(result.error == Error::None && result.size == 1 && output[0] == i, "FIFO packet delivery during concurrent I/O");
    }
    sender.get();
    auto attaching = DatagramChannel::Create();
    std::promise<void> start;
    auto gate = start.get_future().share();
    auto binder = std::async(std::launch::async, [&] {
        gate.wait();
        try { first->BindChannel(attaching, 4444); }
        catch (const std::system_error& error) {
            Require(error.code() == std::errc::bad_file_descriptor, "concurrent close only cancels bind with closed error");
        }
    });
    auto closer = std::async(std::launch::async, [&] { gate.wait(); attaching->Close(); });
    start.set_value();
    binder.get();
    closer.get();
    auto rebound = first->Bind(4444);
    Require(rebound->Number() == 4444 && attaching->Receive(output, false, true).error == Error::Closed,
        "bind-close race leaves no stale channel registration");
    auto waiter = std::async(std::launch::async, [&] { return replacement->Receive(output, false, false, 2s); });
    Require(waiter.wait_for(10ms) == std::future_status::timeout, "receive waits before transport close");
    second->Close();
    Require(waiter.wait_for(1s) == std::future_status::ready && waiter.get().error == Error::Closed, "transport close wakes all receivers");
    Require(replacement->SendTo(Address::Loopback(firstPort, 1111), payload).error == Error::Closed, "closed transport cannot send");
}

int main() {
    try {
        CheckCodec();
        CheckTransport();
        std::puts("Peer datagram codec, independent UDP peer, channel routing, readiness, queue and close checks passed");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
