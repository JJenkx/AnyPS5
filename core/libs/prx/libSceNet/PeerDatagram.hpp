#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace NetPeer {

using CommunicationId = std::array<std::uint8_t, 4>;

struct Address {
    std::uint32_t ipv4 = 0;
    std::uint16_t udpPort = 0;
    std::uint16_t channel = 0;
    static Address Loopback(std::uint16_t udpPort, std::uint16_t channel);
};

enum class FrameStatus { Valid, Malformed, Unsupported, Filtered };

struct Frame {
    FrameStatus status = FrameStatus::Malformed;
    std::uint16_t source = 0;
    std::uint16_t destination = 0;
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> EncodeDatagram(std::uint16_t source, std::uint16_t destination,
    std::span<const std::uint8_t> payload, const std::optional<CommunicationId>& identifier = {});
Frame DecodeDatagram(std::span<const std::uint8_t> packet,
    const std::optional<CommunicationId>& expectedIdentifier = {});

enum class Error { None, Closed, WouldBlock, MessageTooLarge, InvalidArgument, Host };

struct IoResult {
    Error error = Error::None;
    int nativeError = 0;
    std::size_t size = 0;
    bool truncated = false;
    Address peer{};
};

struct Statistics {
    std::uint64_t received = 0;
    std::uint64_t malformed = 0;
    std::uint64_t unsupported = 0;
    std::uint64_t filtered = 0;
    std::uint64_t unbound = 0;
    std::uint64_t dropped = 0;
};

struct TransportState;
struct ChannelState;
struct TransportControl;

class DatagramChannel {
    std::shared_ptr<ChannelState> state;
    explicit DatagramChannel(std::shared_ptr<ChannelState> state);
    friend class DatagramTransport;
public:
    ~DatagramChannel();
    DatagramChannel(const DatagramChannel&) = delete;
    DatagramChannel& operator=(const DatagramChannel&) = delete;
    std::uint16_t Number() const;
    std::uintptr_t NotificationSocket() const;
    IoResult SendTo(Address peer, std::span<const std::uint8_t> payload);
    IoResult Receive(std::span<std::uint8_t> output, bool peek, bool dontWait,
        std::chrono::microseconds timeout = {});
    void Close();
};

class DatagramTransport {
    std::unique_ptr<TransportControl> control;
    explicit DatagramTransport(std::unique_ptr<TransportControl> control);
public:
    static std::shared_ptr<DatagramTransport> Open(std::uint16_t udpPort, std::uint32_t ipv4 = 0);
    ~DatagramTransport();
    DatagramTransport(const DatagramTransport&) = delete;
    DatagramTransport& operator=(const DatagramTransport&) = delete;
    Address LocalAddress() const;
    std::shared_ptr<DatagramChannel> Bind(std::uint16_t channel, std::size_t receiveCapacity = 262144);
    void SetCommunicationId(std::optional<CommunicationId> identifier);
    Statistics GetStatistics() const;
    void Close();
};

}
