#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <sstream>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void FormatAndDrain() {
    std::string written;
    const std::string longLine(6000, 'x');
    {
        AgcDriver::ProfileOutput output([&](std::string_view text) { written += text; });
        output.Print("%s %.2f %llu\n", "shader", 1.25, 42ull);
        output.Print("%s\n", longLine.c_str());
    }
    Check(written == "shader 1.25 42\n" + longLine + "\n", "formatting or shutdown drain changed the output");
}

void SlowWriter() {
    std::mutex mutex;
    std::condition_variable release;
    bool released = false;
    std::promise<void> entered;
    bool first = true;
    std::string written;
    AgcDriver::ProfileOutput output([&](std::string_view text) {
        if (first) {
            first = false;
            entered.set_value();
            std::unique_lock lock(mutex);
            release.wait(lock, [&] { return released; });
        }
        written += text;
    }, 16);
    std::future<bool> producer;
    struct Release {
        std::mutex& mutex;
        std::condition_variable& changed;
        bool& released;
        ~Release() {
            {
                std::lock_guard lock(mutex);
                released = true;
            }
            changed.notify_one();
        }
    } cleanup{mutex, release, released};
    Check(output.Write("12345678"), "first write was rejected");
    Check(entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready, "writer did not start");
    producer = std::async(std::launch::async, [&] { return output.Write("abcdefgh"); });
    Check(producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "producer blocked behind the diagnostic writer");
    Check(producer.get(), "write within the bound was rejected");
    Check(!output.Write("z"), "queue bound did not include the in-flight output");
    {
        std::lock_guard lock(mutex);
        released = true;
    }
    release.notify_one();
    output.Flush();
    Check(written.starts_with("12345678abcdefgh"), "accepted chunks changed order");
    Check(written.find("[profile-output] dropped 1 chunks") != std::string::npos, "bounded overflow was not reported");
}

void ConcurrentWriters() {
    std::string written;
    AgcDriver::ProfileOutput output([&](std::string_view text) { written += text; });
    std::vector<std::thread> producers;
    for (int thread = 0; thread < 8; ++thread) {
        producers.emplace_back([&, thread] {
            for (int item = 0; item < 100; ++item) output.Print("%d:%d\n", thread, item);
        });
    }
    for (auto& producer : producers) producer.join();
    output.Flush();
    std::istringstream stream(written);
    std::vector<std::string> lines;
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    std::sort(lines.begin(), lines.end());
    Check(lines.size() == 800 && std::adjacent_find(lines.begin(), lines.end()) == lines.end(), "concurrent writes lost or duplicated chunks");
    for (int thread = 0; thread < 8; ++thread) {
        for (int item = 0; item < 100; ++item)
            Check(std::binary_search(lines.begin(), lines.end(), std::to_string(thread) + ":" + std::to_string(item)), "concurrent formatting changed a chunk");
    }
}

void WriterFailure() {
    AgcDriver::ProfileOutput output([](std::string_view) { throw std::runtime_error("test writer failure"); });
    output.Write("test");
    for (int operation = 0; operation < 2; ++operation) {
        bool failed = false;
        try {
            if (operation == 0) output.Flush();
            else output.Write("next");
        } catch (const std::runtime_error& error) {
            failed = std::string(error.what()) == "test writer failure";
        }
        Check(failed, "writer failure was not propagated");
    }
}

void ExplicitStop() {
    std::string written;
    AgcDriver::ProfileOutput output([&](std::string_view text) { written += text; });
    output.Write("pending output");
    output.Stop();
    output.Stop();
    output.Flush();
    Check(written == "pending output", "explicit stop did not drain pending output");
    bool failed = false;
    try { output.Write("after stop"); }
    catch (const std::runtime_error& error) { failed = std::string(error.what()) == "profile output is stopping"; }
    Check(failed, "write after stop was accepted");
    failed = false;
    try { output.Print("%s", std::string(5 * 1024 * 1024, 'x').c_str()); }
    catch (const std::runtime_error& error) { failed = std::string(error.what()) == "profile output is stopping"; }
    Check(failed, "oversized print after stop was accepted");
    output.Flush();
}

void SynchronousDrain() {
    std::string written;
    AgcDriver::ProfileOutput output([&](std::string_view text) { written += text; }, 16);
    output.Write("pending\n");
    output.DrainToSynchronous();
    Check(written == "pending\n", "transition did not drain queued output");
    output.Print("late\n");
    Check(written == "pending\nlate\n", "late output was not written synchronously");
    output.DrainToSynchronous();
    output.Print("%s", std::string(6000, 'x').c_str());
    Check(!output.Write(std::string(17, 'y')), "oversized synchronous write was accepted");
    output.Flush();
    const std::string notice = "[profile-output] dropped 1 chunks because the diagnostic writer fell behind\n";
    Check(written == "pending\nlate\n" + notice + notice, "late oversized output was not reported");
}

void ConcurrentDrain() {
    std::string written;
    AgcDriver::ProfileOutput output([&](std::string_view text) { written += text; });
    std::promise<void> ready;
    auto start = ready.get_future().share();
    std::vector<std::thread> producers;
    for (int thread = 0; thread < 8; ++thread) {
        producers.emplace_back([&, thread] {
            start.wait();
            for (int item = 0; item < 100; ++item) output.Print("%d:%d\n", thread, item);
        });
    }
    ready.set_value();
    output.DrainToSynchronous();
    for (auto& producer : producers) producer.join();
    output.Flush();
    std::istringstream stream(written);
    std::array<int, 8> next{};
    for (int thread, item; stream >> thread;) {
        char separator;
        Check(static_cast<bool>(stream >> separator >> item) && separator == ':' && thread >= 0 && thread < 8,
            "transition corrupted an output line");
        Check(item == next[thread]++, "transition lost, duplicated or reordered a producer's output");
    }
    Check(std::all_of(next.begin(), next.end(), [](int count) { return count == 100; }), "transition lost output");
}

void SynchronousFailure() {
    AgcDriver::ProfileOutput output([](std::string_view) { throw std::runtime_error("late writer failure"); });
    output.DrainToSynchronous();
    for (int operation = 0; operation < 2; ++operation) {
        bool failed = false;
        try {
            if (operation == 0) output.Write("late");
            else output.Flush();
        } catch (const std::runtime_error& error) {
            failed = std::string(error.what()) == "late writer failure";
        }
        Check(failed, "synchronous writer failure was not propagated");
    }
}

struct FinalReporter {
    ~FinalReporter() { AgcDriver::ProfilePrint_nid_no_patch("late static report\n"); }
};

}

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            const std::string_view mode = argv[1];
            if (mode == "--late-before-init" || mode == "--late-auto") {
                static FinalReporter reporter;
                AgcDriver::ProfilePrint_nid_no_patch("before shutdown\n");
            } else if (mode == "--late-after-init") {
                AgcDriver::ProfilePrint_nid_no_patch("before shutdown\n");
                static FinalReporter reporter;
            } else throw std::runtime_error("unknown profile output test mode");
            if (mode != "--late-auto") AgcDriver::StopProfileOutput_nid_no_patch();
            return 0;
        }
        FormatAndDrain();
        SlowWriter();
        ConcurrentWriters();
        WriterFailure();
        ExplicitStop();
        SynchronousDrain();
        ConcurrentDrain();
        SynchronousFailure();
        std::cout << "profile output tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
