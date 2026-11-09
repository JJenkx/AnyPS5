#include "PeerDatagram.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace NetPeer {
namespace {
#ifdef _WIN32
using NativeSocket = SOCKET;
using NativeLength = int;
constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
int LastError() { return WSAGetLastError(); }
void CloseSocket(NativeSocket socket) { closesocket(socket); }
bool WouldBlock(int error) { return error == WSAEWOULDBLOCK; }
bool Interrupted(int error) { return error == WSAEINTR; }
#else
using NativeSocket = int;
using NativeLength = socklen_t;
constexpr NativeSocket InvalidSocket = -1;
int LastError() { return errno; }
void CloseSocket(NativeSocket socket) { ::close(socket); }
bool WouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
bool Interrupted(int error) { return error == EINTR; }
#endif

void InitializeSockets() {
#ifdef _WIN32
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        WSADATA data{};
        const int error = WSAStartup(MAKEWORD(2, 2), &data);
        if (error != 0) throw std::system_error(error, std::system_category());
    });
#endif
}

sockaddr_in NativeAddress(Address address) {
    sockaddr_in native{};
    native.sin_family = AF_INET;
    native.sin_addr.s_addr = address.ipv4;
    native.sin_port = htons(address.udpPort);
    return native;
}

NativeSocket BoundSocket(Address& address) {
    InitializeSockets();
    const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == InvalidSocket) throw std::system_error(LastError(), std::system_category());
    try {
        auto native = NativeAddress(address);
        if (::bind(socket, reinterpret_cast<const sockaddr*>(&native), sizeof(native)) != 0)
            throw std::system_error(LastError(), std::system_category());
#ifdef _WIN32
        unsigned long enabled = 1;
        if (ioctlsocket(socket, FIONBIO, &enabled) != 0)
            throw std::system_error(LastError(), std::system_category());
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0)
            throw std::system_error(LastError(), std::system_category());
#endif
        NativeLength size = sizeof(native);
        if (::getsockname(socket, reinterpret_cast<sockaddr*>(&native), &size) != 0)
            throw std::system_error(LastError(), std::system_category());
        address.ipv4 = native.sin_addr.s_addr;
        address.udpPort = ntohs(native.sin_port);
        return socket;
    } catch (...) {
        CloseSocket(socket);
        throw;
    }
}

int WaitReadable(NativeSocket socket, int milliseconds) {
#ifdef _WIN32
    WSAPOLLFD descriptor{};
    descriptor.fd = socket;
    descriptor.events = POLLRDNORM;
    return WSAPoll(&descriptor, 1, milliseconds);
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, milliseconds);
#endif
}

std::uint16_t ReadPort(std::span<const std::uint8_t> packet, std::size_t offset) {
    return static_cast<std::uint16_t>((packet[offset] << 8) | packet[offset + 1]);
}

struct QueuedPacket {
    std::vector<std::uint8_t> payload;
    Address peer;
};
}

Address Address::Loopback(std::uint16_t udpPort, std::uint16_t channel) {
    return {htonl(INADDR_LOOPBACK), udpPort, channel};
}

std::vector<std::uint8_t> EncodeDatagram(std::uint16_t source, std::uint16_t destination,
    std::span<const std::uint8_t> payload, const std::optional<CommunicationId>& identifier) {
    const std::size_t header = identifier ? 10 : 6;
    if (payload.size() > 65507 - header) throw std::length_error("Peer datagram exceeds IPv4 UDP payload");
    std::vector<std::uint8_t> packet{0xff, static_cast<std::uint8_t>(identifier ? 0xa3 : 0x83),
        static_cast<std::uint8_t>(source >> 8), static_cast<std::uint8_t>(source),
        static_cast<std::uint8_t>(destination >> 8), static_cast<std::uint8_t>(destination)};
    if (identifier) packet.insert(packet.end(), identifier->begin(), identifier->end());
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

Frame DecodeDatagram(std::span<const std::uint8_t> packet,
    const std::optional<CommunicationId>& expectedIdentifier) {
    Frame frame;
    if (packet.size() < 2 || packet[0] != 0xff || (packet[1] & 0x80) == 0) return frame;
    if (packet[1] != 0x83 && packet[1] != 0xa3) {
        frame.status = FrameStatus::Unsupported;
        return frame;
    }
    const bool tagged = packet[1] == 0xa3;
    const std::size_t header = tagged ? 10 : 6;
    if (packet.size() < header) return frame;
    if (expectedIdentifier && (!tagged ||
        !std::equal(expectedIdentifier->begin(), expectedIdentifier->end(), packet.begin() + 6))) {
        frame.status = FrameStatus::Filtered;
        return frame;
    }
    frame.source = ReadPort(packet, 2);
    frame.destination = ReadPort(packet, 4);
    frame.payload.assign(packet.begin() + header, packet.end());
    frame.status = FrameStatus::Valid;
    return frame;
}

struct ChannelState {
    std::weak_ptr<TransportState> transport;
    std::uint16_t number = 0;
    std::uint64_t generation = 0;
    NativeSocket notification = InvalidSocket;
    Address notificationAddress;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<QueuedPacket> packets;
    std::size_t queuedBytes = 0;
    std::size_t capacity = 0;
    bool closed = false;
    int terminalError = 0;
    int pendingError = 0;
    std::optional<Address> expectedPeer;

    ~ChannelState() {
        if (notification != InvalidSocket) CloseSocket(notification);
    }

    void Signal() {
        const auto address = NativeAddress(notificationAddress);
        const char byte = 1;
        ::sendto(notification, &byte, 1, 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    }

    void Stop(int error) {
        std::lock_guard lock(mutex);
        if (closed) return;
        closed = true;
        terminalError = error;
        pendingError = error;
        const bool empty = packets.empty();
        packets.clear();
        queuedBytes = 0;
        if (empty) Signal();
        changed.notify_all();
    }
};

struct TransportState {
    NativeSocket socket = InvalidSocket;
    Address local;
    std::mutex mutex;
    std::map<std::uint16_t, std::pair<std::uint64_t, std::weak_ptr<ChannelState>>> channels;
    std::uint64_t nextGeneration = 1;
    std::optional<CommunicationId> identifier;
    Statistics statistics;
    std::atomic<bool> stopping{false};

    ~TransportState() {
        if (socket != InvalidSocket) CloseSocket(socket);
    }

    void StopChannels(int error) {
        std::lock_guard lock(mutex);
        for (const auto& entry : channels) {
            if (const auto channel = entry.second.second.lock()) channel->Stop(error);
        }
        channels.clear();
    }

    void ReceiveLoop() {
        std::array<std::uint8_t, 65536> packet;
        while (!stopping.load()) {
            const int ready = WaitReadable(socket, 20);
            if (ready == 0) continue;
            if (ready < 0) {
                const int error = LastError();
                if (Interrupted(error)) continue;
                stopping.store(true);
                StopChannels(error);
                return;
            }
            sockaddr_in from{};
            NativeLength size = sizeof(from);
            const int count = static_cast<int>(::recvfrom(socket, reinterpret_cast<char*>(packet.data()),
                static_cast<int>(packet.size()), 0, reinterpret_cast<sockaddr*>(&from), &size));
            if (count < 0) {
                const int error = LastError();
                if (WouldBlock(error) || Interrupted(error)) continue;
                stopping.store(true);
                StopChannels(error);
                return;
            }
            std::shared_ptr<ChannelState> channel;
            Frame frame;
            {
                std::lock_guard lock(mutex);
                ++statistics.received;
                frame = DecodeDatagram(std::span(packet.data(), static_cast<std::size_t>(count)), identifier);
                if (frame.status != FrameStatus::Valid) {
                    if (frame.status == FrameStatus::Malformed) ++statistics.malformed;
                    else if (frame.status == FrameStatus::Unsupported) ++statistics.unsupported;
                    else ++statistics.filtered;
                    continue;
                }
                const auto entry = channels.find(frame.destination);
                if (entry != channels.end()) channel = entry->second.second.lock();
                if (!channel) {
                    ++statistics.unbound;
                    continue;
                }
            }
            bool dropped = false;
            {
                std::lock_guard lock(channel->mutex);
                const std::size_t charge = std::max<std::size_t>(1, frame.payload.size());
                const bool wrongPeer = channel->expectedPeer &&
                    (channel->expectedPeer->ipv4 != from.sin_addr.s_addr ||
                     channel->expectedPeer->udpPort != ntohs(from.sin_port) || channel->expectedPeer->channel != frame.source);
                if (channel->closed || wrongPeer || channel->queuedBytes > channel->capacity ||
                    charge > channel->capacity - channel->queuedBytes) dropped = true;
                else {
                    const bool empty = channel->packets.empty();
                    channel->packets.push_back({std::move(frame.payload),
                        {from.sin_addr.s_addr, ntohs(from.sin_port), frame.source}});
                    channel->queuedBytes += charge;
                    if (empty) channel->Signal();
                    channel->changed.notify_all();
                }
            }
            if (dropped) {
                std::lock_guard lock(mutex);
                ++statistics.dropped;
            }
        }
    }
};

struct TransportControl {
    std::shared_ptr<TransportState> state;
    std::thread receiver;
    std::mutex closeMutex;
};

DatagramChannel::DatagramChannel(std::shared_ptr<ChannelState> state) : state(std::move(state)) {}
DatagramChannel::~DatagramChannel() { Close(); }
std::shared_ptr<DatagramChannel> DatagramChannel::Create() {
    auto state = std::make_shared<ChannelState>();
    state->notificationAddress = Address::Loopback(0, 0);
    state->notification = BoundSocket(state->notificationAddress);
    return std::shared_ptr<DatagramChannel>(new DatagramChannel(std::move(state)));
}
std::uint16_t DatagramChannel::Number() const {
    std::lock_guard lock(state->mutex);
    return state->number;
}
std::uintptr_t DatagramChannel::NotificationSocket() const { return static_cast<std::uintptr_t>(state->notification); }

std::size_t DatagramChannel::ReadableBytes() const {
    std::lock_guard lock(state->mutex);
    return state->packets.empty() ? 0 : state->packets.front().payload.size();
}

void DatagramChannel::SetPeer(std::optional<Address> peer) {
    std::lock_guard lock(state->mutex);
    state->expectedPeer = std::move(peer);
}

int DatagramChannel::TakePendingError() {
    std::lock_guard lock(state->mutex);
    const int error = state->pendingError;
    state->pendingError = 0;
    return error;
}

void DatagramChannel::SetReceiveCapacity(std::size_t capacity) {
    std::lock_guard lock(state->mutex);
    state->capacity = capacity;
}

IoResult DatagramChannel::SendTo(Address peer, std::span<const std::uint8_t> payload) {
    if (peer.udpPort == 0 || peer.channel == 0 || peer.channel == 0xffff) return {Error::InvalidArgument};
    std::shared_ptr<TransportState> transport;
    std::uint16_t source;
    {
        std::lock_guard channelLock(state->mutex);
        if (state->closed) return {Error::Closed};
        transport = state->transport.lock();
        source = state->number;
    }
    if (!transport) return {Error::InvalidArgument};
    std::lock_guard transportLock(transport->mutex);
    {
        std::lock_guard channelLock(state->mutex);
        if (state->closed || transport->stopping.load()) return {Error::Closed};
    }
    std::vector<std::uint8_t> packet;
    try { packet = EncodeDatagram(source, peer.channel, payload, transport->identifier); }
    catch (const std::length_error&) { return {Error::MessageTooLarge}; }
    const auto address = NativeAddress(peer);
    const int result = static_cast<int>(::sendto(transport->socket, reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()), 0, reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
    if (result < 0) return {Error::Host, LastError()};
    if (static_cast<std::size_t>(result) != packet.size()) throw std::runtime_error("Partial native peer datagram");
    return {Error::None, 0, payload.size()};
}

IoResult DatagramChannel::Receive(std::span<std::uint8_t> output, bool peek, bool dontWait,
    std::chrono::microseconds timeout) {
    std::unique_lock lock(state->mutex);
    const auto available = [&] { return state->closed || !state->packets.empty(); };
    if (!available()) {
        if (dontWait) return {Error::WouldBlock};
        if (timeout.count() > 0) {
            if (!state->changed.wait_for(lock, timeout, available)) return {Error::WouldBlock};
        } else state->changed.wait(lock, available);
    }
    if (state->closed) return {state->terminalError ? Error::Host : Error::Closed, state->terminalError};
    const auto& packet = state->packets.front();
    const std::size_t count = std::min(output.size(), packet.payload.size());
    std::copy_n(packet.payload.begin(), count, output.begin());
    const IoResult result{Error::None, 0, count, count < packet.payload.size(), packet.peer};
    if (!peek) {
        state->queuedBytes -= std::max<std::size_t>(1, packet.payload.size());
        state->packets.pop_front();
        if (state->packets.empty()) {
            char byte;
            ::recv(state->notification, &byte, 1, 0);
        }
    }
    return result;
}

void DatagramChannel::Close() {
    state->Stop(0);
    std::shared_ptr<TransportState> transport;
    std::uint16_t number;
    std::uint64_t generation;
    {
        std::lock_guard lock(state->mutex);
        transport = state->transport.lock();
        number = state->number;
        generation = state->generation;
    }
    if (transport) {
        std::lock_guard lock(transport->mutex);
        const auto entry = transport->channels.find(number);
        if (entry != transport->channels.end() && entry->second.first == generation)
            transport->channels.erase(entry);
    }
}

DatagramTransport::DatagramTransport(std::unique_ptr<TransportControl> control) : control(std::move(control)) {}
DatagramTransport::~DatagramTransport() { Close(); }

std::shared_ptr<DatagramTransport> DatagramTransport::Open(std::uint16_t udpPort, std::uint32_t ipv4) {
    auto control = std::make_unique<TransportControl>();
    control->state = std::make_shared<TransportState>();
    control->state->local = {ipv4, udpPort, 0};
    control->state->socket = BoundSocket(control->state->local);
    auto transport = std::shared_ptr<DatagramTransport>(new DatagramTransport(std::move(control)));
    transport->control->receiver = std::thread([state = transport->control->state] {
        try { state->ReceiveLoop(); }
        catch (const std::bad_alloc&) {
            state->stopping.store(true);
#ifdef _WIN32
            state->StopChannels(WSAENOBUFS);
#else
            state->StopChannels(ENOMEM);
#endif
        }
    });
    return transport;
}

Address DatagramTransport::LocalAddress() const { return control->state->local; }

std::shared_ptr<DatagramChannel> DatagramTransport::Bind(std::uint16_t number, std::size_t receiveCapacity) {
    auto channel = DatagramChannel::Create();
    BindChannel(channel, number, receiveCapacity);
    return channel;
}

void DatagramTransport::BindChannel(const std::shared_ptr<DatagramChannel>& endpoint, std::uint16_t number,
    std::size_t receiveCapacity) {
    if (!endpoint || number == 0 || number == 0xffff || receiveCapacity == 0)
        throw std::system_error(std::make_error_code(std::errc::invalid_argument));
    const auto& channel = endpoint->state;
    const auto& state = control->state;
    std::lock_guard lock(state->mutex);
    std::lock_guard channelLock(channel->mutex);
    if (state->stopping.load()) throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
    if (channel->closed) throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
    if (channel->number != 0) throw std::system_error(std::make_error_code(std::errc::invalid_argument));
    const auto entry = state->channels.find(number);
    if (entry != state->channels.end() && !entry->second.second.expired())
        throw std::system_error(std::make_error_code(std::errc::address_in_use));
    const auto generation = state->nextGeneration;
    state->channels[number] = {generation, channel};
    ++state->nextGeneration;
    channel->transport = state;
    channel->number = number;
    channel->capacity = receiveCapacity;
    channel->generation = generation;
}

void DatagramTransport::SetCommunicationId(std::optional<CommunicationId> identifier) {
    std::lock_guard lock(control->state->mutex);
    control->state->identifier = std::move(identifier);
}

Statistics DatagramTransport::GetStatistics() const {
    std::lock_guard lock(control->state->mutex);
    return control->state->statistics;
}

void DatagramTransport::Close() {
    std::lock_guard closeLock(control->closeMutex);
    const auto& state = control->state;
    state->stopping.store(true);
    state->StopChannels(0);
    if (control->receiver.joinable()) control->receiver.join();
    std::lock_guard lock(state->mutex);
    if (state->socket != InvalidSocket) {
        CloseSocket(state->socket);
        state->socket = InvalidSocket;
    }
}

}
