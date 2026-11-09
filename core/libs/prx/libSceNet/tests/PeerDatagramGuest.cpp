#include "prx/libSceNet/libSceNet.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef NET_PEER_PATCHED
#include <nid/NidCompute.hpp>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

static void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

#ifdef NET_PEER_PATCHED
template<typename TFunction>
static TFunction Function(void* module, const char* name) {
    const auto symbol = Nid::ComputeNid(name, "libSceNet");
#ifdef _WIN32
    auto* address = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), symbol.c_str()));
#else
    auto* address = dlsym(module, symbol.c_str());
#endif
    Require(address != nullptr, name);
    return reinterpret_cast<TFunction>(address);
}
#define NET_FUNCTION(symbol, name) decltype(&::symbol) name = Function<decltype(&::symbol)>(module, #name)
#else
#define NET_FUNCTION(symbol, name) decltype(&::symbol) name = &::symbol
#endif

struct Api {
    void* module;
    NET_FUNCTION(sceNetInit_nid_postfix, sceNetInit);
    NET_FUNCTION(sceNetSocket, sceNetSocket);
    NET_FUNCTION(sceNetBind_nid_postfix, sceNetBind);
    NET_FUNCTION(sceNetSocketClose, sceNetSocketClose);
    NET_FUNCTION(sceNetSetsockopt, sceNetSetsockopt);
    NET_FUNCTION(sceNetGetsockname, sceNetGetsockname);
    NET_FUNCTION(sceNetGetpeername, sceNetGetpeername);
    NET_FUNCTION(sceNetConnect, sceNetConnect);
    NET_FUNCTION(sceNetSend, sceNetSend);
    NET_FUNCTION(sceNetSendto, sceNetSendto);
    NET_FUNCTION(sceNetRecv, sceNetRecv);
    NET_FUNCTION(sceNetRecvfrom, sceNetRecvfrom);
    NET_FUNCTION(sceNetSendmsg, sceNetSendmsg);
    NET_FUNCTION(sceNetRecvmsg, sceNetRecvmsg);
    NET_FUNCTION(sceNetEpollCreate, sceNetEpollCreate);
    NET_FUNCTION(sceNetEpollControl, sceNetEpollControl);
    NET_FUNCTION(sceNetEpollWait, sceNetEpollWait);
    NET_FUNCTION(sceNetEpollDestroy, sceNetEpollDestroy);
    NET_FUNCTION(sceNetIoctl, sceNetIoctl);
    NET_FUNCTION(sceNetErrnoLoc, sceNetErrnoLoc);
};

#undef NET_FUNCTION

using Address = std::array<std::uint8_t, 16>;

static Address Loopback(std::uint16_t port, std::uint16_t channel = 0) {
    return {16, 2, static_cast<std::uint8_t>(port >> 8), static_cast<std::uint8_t>(port),
        127, 0, 0, 1, static_cast<std::uint8_t>(channel >> 8), static_cast<std::uint8_t>(channel)};
}

static Address BoundAddress(const Api& api, int descriptor) {
    Address address{};
    std::uint32_t length = address.size();
    Require(api.sceNetGetsockname(descriptor, address.data(), &length) == 0 && length == address.size(),
        "read actual bound address");
    return address;
}

static int Socket(const Api& api, int type) {
    const int descriptor = api.sceNetSocket("peer-guest-test", 2, type, 0);
    Require(descriptor >= 0, "allocate socket");
    const std::array<std::int64_t, 2> timeout{2, 0};
    Require(api.sceNetSetsockopt(descriptor, 0xffff, 0x1006, timeout.data(), sizeof(timeout)) == 0,
        "bound receive duration");
    return descriptor;
}

static void Error(const Api& api, std::int64_t result, int expected, const char* message) {
    Require(result == static_cast<int>(0x80410100u | static_cast<unsigned>(expected)) &&
        *api.sceNetErrnoLoc() == expected, message);
}

static void Test(const Api& api) {
    Require(api.sceNetInit() == 0, "initialize Net");
    const int wire = Socket(api, 2);
    auto wireAddress = Loopback(0);
    Require(api.sceNetBind(wire, wireAddress.data(), wireAddress.size()) == 0, "bind ordinary UDP endpoint");
    wireAddress = BoundAddress(api, wire);
    const int allocator = Socket(api, 2);
    auto local = Loopback(0);
    Require(api.sceNetBind(allocator, local.data(), local.size()) == 0, "choose free shared UDP port");
    local = BoundAddress(api, allocator);
    Require(api.sceNetSocketClose(allocator) == 0, "release chosen UDP port");
    const auto port = static_cast<std::uint16_t>((local[2] << 8) | local[3]);
    const auto firstAddress = Loopback(port, 7777);
    const auto secondAddress = Loopback(port, 8888);
    const int first = Socket(api, 6);
    const int second = Socket(api, 6);
    const int epoll = api.sceNetEpollCreate("peer-guest-epoll", 0);
    Require(epoll >= 0, "create epoll before bind");
    NetEpollEvent event{};
    event.events = 1;
    event.data.u64 = 101;
    Require(api.sceNetEpollControl(epoll, 1, first, &event) == 0, "register unbound first channel");
    event.data.u64 = 202;
    Require(api.sceNetEpollControl(epoll, 1, second, &event) == 0, "register unbound second channel");
    Require(api.sceNetBind(first, firstAddress.data(), firstAddress.size()) == 0, "bind first virtual channel");
    Require(api.sceNetBind(second, secondAddress.data(), secondAddress.size()) == 0, "share UDP port with second channel");
    Require(BoundAddress(api, first) == firstAddress && BoundAddress(api, second) == secondAddress,
        "read shared UDP port and distinct virtual channels");
    std::array<NetEpollEvent, 2> events{};
    Require(api.sceNetEpollWait(epoll, events.data(), events.size(), 0) == 0, "empty peer channels are not readable");
    const std::array<std::uint8_t, 11> packet{0xff, 0x83, 0, 41, 0x1e, 0x61, 'h', 'e', 'l', 'l', 'o'};
    Require(api.sceNetSendto(wire, packet.data(), packet.size(), 0, local.data(), local.size()) == packet.size(),
        "send independent wire packet to shared port");
    Require(api.sceNetEpollWait(epoll, events.data(), events.size(), 1000000) == 1 &&
        events[0].data.u64 == 101 && (events[0].events & 1) != 0, "readiness survives bind and selects only addressed channel");
    int available = -1;
    Require(api.sceNetIoctl(first, 0x4004667f, &available) == 0 && available == 5,
        "FIONREAD reports payload without wire header");
    std::array<char, 32> buffer{};
    Address source{};
    std::uint32_t sourceLength = source.size();
    Require(api.sceNetRecvfrom(first, buffer.data(), buffer.size(), 2, source.data(), &sourceLength) == 5 &&
        std::memcmp(buffer.data(), "hello", 5) == 0, "peek decoded wire payload");
    auto remote = wireAddress;
    remote[9] = 41;
    Require(source == remote && sourceLength == source.size(), "return source UDP and virtual ports");
    Require(api.sceNetEpollWait(epoll, events.data(), events.size(), 0) == 1, "peek preserves readability");
    Require(api.sceNetRecv(first, buffer.data(), 3, 0) == 3 && std::memcmp(buffer.data(), "hel", 3) == 0,
        "truncate and consume one datagram");
    Require(api.sceNetEpollWait(epoll, events.data(), events.size(), 0) == 0, "consuming clears channel readiness");
    Error(api, api.sceNetRecv(second, buffer.data(), buffer.size(), 0x80), 35, "other channel remains empty");
    Error(api, api.sceNetSend(first, "x", 1, 0), 39, "unconnected send requires destination");
    Error(api, api.sceNetSendto(first, "x", 1, 0, wireAddress.data(), wireAddress.size()), 49,
        "reject incomplete destination virtual port");
    Require(api.sceNetSendto(first, "ok", 2, 0, remote.data(), remote.size()) == 2, "send framed peer payload to ordinary UDP");
    Require(api.sceNetRecv(wire, buffer.data(), buffer.size(), 0) == 8, "wire endpoint receives complete frame");
    const std::array<std::uint8_t, 8> expected{0xff, 0x83, 0x1e, 0x61, 0, 41, 'o', 'k'};
    Require(std::memcmp(buffer.data(), expected.data(), expected.size()) == 0, "verify independent wire header and payload");
    Require(api.sceNetConnect(first, secondAddress.data(), secondAddress.size()) == 0, "connect virtual channel");
    Address connected{};
    sourceLength = connected.size();
    Require(api.sceNetGetpeername(first, connected.data(), &sourceLength) == 0 && connected == secondAddress,
        "report connected virtual endpoint");
    Error(api, api.sceNetSendto(first, "x", 1, 0, remote.data(), remote.size()), 56,
        "reject explicit destination on connected channel");
    NetIovec outgoing[]{{const_cast<char*>("ab"), 2}, {const_cast<char*>("cde"), 3}};
    NetMsghdr send{};
    send.iov = outgoing;
    send.iov_length = 2;
    Require(api.sceNetSendmsg(first, &send, 0) == 5, "gather connected datagram");
    Require(api.sceNetEpollWait(epoll, events.data(), events.size(), 1000000) == 1 && events[0].data.u64 == 202,
        "second virtual channel becomes readable");
    std::array<char, 2> left{};
    std::array<char, 4> right{};
    NetIovec incoming[]{{left.data(), left.size()}, {right.data(), right.size()}};
    NetMsghdr receive{};
    receive.name = source.data();
    receive.name_length = source.size();
    receive.iov = incoming;
    receive.iov_length = 2;
    Require(api.sceNetRecvmsg(second, &receive, 0) == 5 && std::memcmp(left.data(), "ab", 2) == 0 &&
        std::memcmp(right.data(), "cde", 3) == 0 && right[3] == 0 && source == firstAddress,
        "scatter payload and report virtual source");
    Require(api.sceNetSocketClose(first) == 0, "close first virtual channel independently");
    auto secondPacket = packet;
    secondPacket[4] = 0x22;
    secondPacket[5] = 0xb8;
    Require(api.sceNetSendto(wire, secondPacket.data(), secondPacket.size(), 0, local.data(), local.size()) == secondPacket.size(),
        "remaining channel keeps shared transport alive");
    Require(api.sceNetRecv(second, buffer.data(), buffer.size(), 0) == 5, "receive after another channel closes");
    Require(api.sceNetSocketClose(second) == 0 && api.sceNetSocketClose(wire) == 0 && api.sceNetEpollDestroy(epoll) == 0,
        "close sockets and epoll");
}

int main(int argc, char** argv) {
    void* module = nullptr;
#ifdef NET_PEER_PATCHED
    Require(argc == 2, "supply patched module directory");
    const auto path = std::filesystem::absolute(argv[1]) / "libSceNet.prx";
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS);
    module = static_cast<void*>(LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
#else
    module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) std::fprintf(stderr, "%s\n", dlerror());
#endif
    Require(module != nullptr, "load patched Net");
#else
    static_cast<void>(argc);
    static_cast<void>(argv);
#endif
    Test(Api{module});
    std::puts("peer guest API packet flow passed");
}
