#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
int APS5_VABI sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int, std::int64_t*);
int APS5_VABI sceKernelMapDirectMemory(void**, std::size_t, int, int, std::int64_t, std::size_t);
int APS5_VABI sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
}

namespace {

constexpr std::size_t bytes = 65536;
std::int64_t physical = -1;
unsigned char* mapped = nullptr;

void Require(bool condition) {
    if (condition) return;
    std::fputs("late direct-memory cleanup failed\n", stderr);
    std::_Exit(1);
}

void ReleaseLate() {
    Require(mapped[0] == 0xa5 && mapped[bytes - 1] == 0x5a);
    Require(sceKernelReleaseDirectMemory(physical, bytes) == 0);
    std::int64_t reused = -1;
    Require(sceKernelAllocateDirectMemory(physical, physical + bytes, bytes, bytes, 0, &reused) == 0 && reused == physical);
    Require(sceKernelReleaseDirectMemory(reused, bytes) == 0);
    std::puts("late direct-memory release and reuse passed");
}

}

int main() {
    Require(std::atexit(ReleaseLate) == 0);
    Require(sceKernelAllocateDirectMemory(0, DIRECT_MEMORY_SIZE, bytes, bytes, 0, &physical) == 0);
    void* address = nullptr;
    Require(sceKernelMapDirectMemory(&address, bytes, 3, 0, physical, bytes) == 0);
    mapped = static_cast<unsigned char*>(address);
    mapped[0] = 0xa5;
    mapped[bytes - 1] = 0x5a;
}
