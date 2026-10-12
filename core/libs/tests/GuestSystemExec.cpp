#include "prx/libc/include/General.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

extern "C" int APS5_VABI sceSystemServiceLoadExec(const char*, const char* const*);

namespace {
bool replacing = false;
std::filesystem::path marker;
void Require(bool value) { if (!value) std::abort(); }
void OldCleanup() {
    if (replacing) { std::ofstream file(marker); file << "old shutdown ran"; }
}
void ExpectFailure(const char* path, int expected) {
    try { sceSystemServiceLoadExec(path, nullptr); }
    catch (const std::system_error& error) { Require(error.code().value() == expected); return; }
    std::abort();
}
}

int main(int argc, char** argv) {
    const std::string guest = "/exec-fixture/next.bin";
    const auto root = std::filesystem::temp_directory_path() / ("anyps5-exec-" + std::to_string(getpid()));
    marker = root / "shutdown-marker";
    if (std::strcmp(argv[0], guest.c_str()) == 0) {
        Require(!std::filesystem::exists(marker));
        if (argc == 1) {
            Require(std::filesystem::exists(root / "no-arguments"));
        } else {
            Require(argc == 5 && std::strcmp(argv[1], "--replacement") == 0);
            Require(std::strcmp(argv[2], std::to_string(getpid()).c_str()) == 0);
            Require(std::strcmp(argv[3], "argument with spaces") == 0 && std::strcmp(argv[4], "") == 0);
        }
        std::filesystem::remove_all(root);
        std::puts("Guest executable replacement preserved PID and arguments");
        return 0;
    }
    std::filesystem::create_directories(root);
    AddPathAlias_nid_no_patch("exec-fixture", root.c_str());
    const auto next = root / "next.bin.guest.elf";
    std::filesystem::copy_file(std::filesystem::canonical("/proc/self/exe"), next);
    ExpectFailure("/exec-fixture/missing.bin", ENOENT);
    {
        std::ofstream file(root / "bad.bin.guest.elf"); file << "not an executable";
    }
    ExpectFailure("/exec-fixture/bad.bin", EACCES);
    std::filesystem::permissions(root / "bad.bin.guest.elf", std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
    ExpectFailure("/exec-fixture/bad.bin", ENOEXEC);
    Require(!std::filesystem::exists(marker));
    LibcRegisterShutdown_nid_postfix(OldCleanup);
    Require(std::atexit(OldCleanup) == 0);
    replacing = true;
    if (argc > 1 && (std::strcmp(argv[1], "--no-arguments") == 0 || std::strcmp(argv[1], "--empty-arguments") == 0)) {
        { std::ofstream file(root / "no-arguments"); file << "expect argv0 only"; }
        const char* empty[]{nullptr};
        sceSystemServiceLoadExec(guest.c_str(), std::strcmp(argv[1], "--empty-arguments") == 0 ? empty : nullptr);
    } else {
        const auto pid = std::to_string(getpid());
        const char* args[]{"--replacement", pid.c_str(), "argument with spaces", "", nullptr};
        sceSystemServiceLoadExec(guest.c_str(), args);
    }
    return 1;
}
