#include "prx/libSceAgcDriver/Graphics/include/QueueSubmitter.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using AgcDriver::Graphics::QueueAccess;
using AgcDriver::Graphics::QueueSubmitter;

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Handle>
Handle Fake(std::uintptr_t value) {
    return reinterpret_cast<Handle>(value);
}

std::mutex submitsMutex;
std::vector<std::uintptr_t> firstSubmits;
std::atomic<int> secondSubmits{0};
std::atomic<int> slowSubmits{0};
std::atomic<bool> slowInside{false};

VKAPI_ATTR VkResult VKAPI_CALL FirstSubmit(VkQueue, std::uint32_t count, const VkSubmitInfo* infos, VkFence) {
    std::lock_guard lock(submitsMutex);
    for (std::uint32_t index = 0; index < count; ++index) firstSubmits.push_back(reinterpret_cast<std::uintptr_t>(infos[index].pCommandBuffers[0]));
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL SecondSubmit(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) {
    secondSubmits.fetch_add(1, std::memory_order_relaxed);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL SlowSubmit(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) {
    slowInside.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    slowSubmits.fetch_add(1, std::memory_order_acq_rel);
    slowInside.store(false, std::memory_order_release);
    return VK_SUCCESS;
}

std::atomic<int> failingCalls{0};

VKAPI_ATTR VkResult VKAPI_CALL FailingSubmit(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) {
    failingCalls.fetch_add(1, std::memory_order_relaxed);
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

QueueSubmitter::Job JobFor(std::uintptr_t commands, VkResult* result = nullptr, std::uint64_t serial = 0) {
    return {Fake<VkCommandBuffer>(commands), VK_NULL_HANDLE, VK_NULL_HANDLE, 0, serial != 0 ? Fake<VkSemaphore>(0x7700) : VK_NULL_HANDLE, serial, result};
}

void ordered() {
    const auto device = Fake<VkDevice>(0x1000);
    const auto queue = Fake<VkQueue>(0x2000);
    QueueSubmitter::RegisterDevice(device);
    auto submitter = QueueSubmitter::For(device, queue, FirstSubmit);
    Require(QueueSubmitter::Existing(queue) == submitter, "a queue does not find its submitter");
    Require(QueueSubmitter::For(device, queue, FirstSubmit) == submitter, "a second request for the same device and queue made another submitter");
    std::uint64_t last = 0;
    for (std::uintptr_t commands = 1; commands <= 64; ++commands) last = submitter->Enqueue(JobFor(commands));
    Require(last == 64, "the submit positions do not count the queued jobs");
    submitter->WaitSubmitReturned(last);
    Require(submitter->Returned(last), "a waited position does not read as returned");
    {
        std::lock_guard lock(submitsMutex);
        Require(firstSubmits.size() == 64, "not every queued job was submitted");
        for (std::size_t index = 0; index < firstSubmits.size(); ++index) Require(firstSubmits[index] == index + 1, "jobs were submitted out of order");
    }
    submitter->Drain();
    Require(submitter->Submitted() == 64, "the submitter does not count its submits");
    QueueSubmitter::RetireDevice(device);
}

void retiredWithItsDevice() {
    const auto first = Fake<VkDevice>(0x3000);
    const auto second = Fake<VkDevice>(0x4000);
    const auto reused = Fake<VkQueue>(0x5000);
    const auto other = Fake<VkQueue>(0x6000);
    {
        std::lock_guard lock(submitsMutex);
        firstSubmits.clear();
    }
    QueueSubmitter::RegisterDevice(first);
    QueueSubmitter::RegisterDevice(second);
    auto old = QueueSubmitter::For(first, reused, FirstSubmit);
    auto kept = QueueSubmitter::For(second, other, SecondSubmit);
    old->WaitSubmitReturned(old->Enqueue(JobFor(7)));
    QueueSubmitter::RetireDevice(first);
    Require(QueueSubmitter::Existing(reused) == nullptr, "a submitter outlived its device");
    Require(QueueSubmitter::Existing(other) == kept, "retiring one device removed another device's submitter");
    bool refused = false;
    try {
        old->Enqueue(JobFor(9));
    } catch (const std::runtime_error&) {
        refused = true;
    }
    Require(refused, "a retired submitter accepted a job");
    auto fresh = QueueSubmitter::For(second, reused, SecondSubmit);
    Require(fresh != old, "a reused queue handle found the submitter of a destroyed device");
    fresh->WaitSubmitReturned(fresh->Enqueue(JobFor(8)));
    Require(secondSubmits.load(std::memory_order_relaxed) == 1, "a reused queue handle did not submit through its own device's function");
    {
        std::lock_guard lock(submitsMutex);
        Require(firstSubmits.size() == 1 && firstSubmits[0] == 7, "a destroyed device's submit function was called again");
    }
    QueueSubmitter::RetireDevice(second);
    Require(QueueSubmitter::Existing(reused) == nullptr && QueueSubmitter::Existing(other) == nullptr, "a submitter outlived its device");
    QueueSubmitter::RetireDevice(second);
}

void queueOwnershipAndReuse() {
    const auto gone = Fake<VkDevice>(0x7000);
    const auto next = Fake<VkDevice>(0x8000);
    const auto queue = Fake<VkQueue>(0x9000);
    QueueSubmitter::RegisterDevice(gone);
    QueueSubmitter::RegisterDevice(next);
    auto stale = QueueSubmitter::For(gone, queue, FirstSubmit);
    bool refused = false;
    try { QueueSubmitter::For(next, queue, SecondSubmit); }
    catch (const std::runtime_error&) { refused = true; }
    Require(refused, "a live queue was registered to another device");
    QueueSubmitter::RetireDevice(gone);
    auto replacement = QueueSubmitter::For(next, queue, SecondSubmit);
    Require(replacement != stale, "a reused queue kept the retired worker");
    Require(QueueSubmitter::Existing(queue) == replacement, "the replacement submitter is not registered");
    const int before = secondSubmits.load(std::memory_order_relaxed);
    replacement->WaitSubmitReturned(replacement->Enqueue(JobFor(10)));
    Require(secondSubmits.load(std::memory_order_relaxed) == before + 1, "the replacement used the wrong submit function");
    QueueSubmitter::RetireDevice(next);
    QueueSubmitter::RetireDevice(gone);
    Require(QueueSubmitter::Existing(queue) == nullptr, "a submitter outlived its device");
}

void drainedBeforeAccessAndRetirement() {
    const auto device = Fake<VkDevice>(0xa000);
    const auto queue = Fake<VkQueue>(0xb000);
    QueueSubmitter::RegisterDevice(device);
    auto submitter = QueueSubmitter::For(device, queue, SlowSubmit);
    for (std::uintptr_t commands = 1; commands <= 8; ++commands) submitter->Enqueue(JobFor(commands));
    {
        const QueueAccess access(queue);
        Require(slowSubmits.load(std::memory_order_acquire) == 8, "queue access did not drain the queued submits");
        Require(!slowInside.load(std::memory_order_acquire), "queue access overlapped a submit");
    }
    for (std::uintptr_t commands = 9; commands <= 16; ++commands) submitter->Enqueue(JobFor(commands));
    QueueSubmitter::RetireDevice(device);
    Require(slowSubmits.load(std::memory_order_acquire) == 16, "retiring a device dropped its queued submits");
}

void failureReachesItsJob() {
    const auto device = Fake<VkDevice>(0xc000);
    const auto queue = Fake<VkQueue>(0xd000);
    QueueSubmitter::RegisterDevice(device);
    auto submitter = QueueSubmitter::For(device, queue, FailingSubmit);
    Require(!QueueSubmitter::FailedThrough(device, 100), "a device without a failed submit reports one");
    VkResult result = VK_SUCCESS;
    submitter->WaitSubmitReturned(submitter->Enqueue(JobFor(1, &result, 5)));
    Require(result == VK_ERROR_OUT_OF_DEVICE_MEMORY, "a failed submit did not reach its job");
    Require(QueueSubmitter::FailedSubmits(device) == 1, "a failed submit was not counted for its device");
    Require(QueueSubmitter::FailedSubmits(Fake<VkDevice>(0xe000)) == 0, "a failed submit was counted for another device");
    Require(QueueSubmitter::FailedThrough(device, 5) && QueueSubmitter::FailedThrough(device, 6), "a wait at or past a failed serial does not see the failure");
    Require(!QueueSubmitter::FailedThrough(device, 4), "a wait before a failed serial sees the failure");
    VkResult next = VK_ERROR_UNKNOWN;
    submitter->WaitSubmitReturned(submitter->Enqueue(JobFor(2, &next, 6)));
    Require(next == VK_ERROR_OUT_OF_DEVICE_MEMORY, "a job after a failed submit did not take the failure");
    Require(failingCalls.load(std::memory_order_relaxed) == 1, "the submitter kept submitting after a failed submit");
    QueueSubmitter::RetireDevice(device);
    Require(QueueSubmitter::FailedSubmits(device) == 0, "a retired device still counts failed submits");
}

std::atomic<bool> heldInside{}, heldRelease{};

VKAPI_ATTR VkResult VKAPI_CALL HeldSubmit(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) {
    heldInside.store(true, std::memory_order_release);
    while (!heldRelease.load(std::memory_order_acquire)) heldRelease.wait(false);
    return VK_SUCCESS;
}

bool AwaitRetirement(VkDevice device, VkQueue queue, PFN_vkQueueSubmit submit) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        try { QueueSubmitter::For(device, queue, submit); }
        catch (const std::runtime_error&) { return true; }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void retirementClosesAdmissionAndReleasesOwnership() {
    const auto device = Fake<VkDevice>(0x11000);
    const auto queue = Fake<VkQueue>(0x12000);
    const auto lateQueue = Fake<VkQueue>(0x13000);
    std::future<void> retiring;
    std::future<bool> producer;
    struct Cleanup {
        VkDevice device;
        std::future<void>& retiring;
        std::future<bool>& producer;
        ~Cleanup() {
            heldRelease.store(true, std::memory_order_release);
            heldRelease.notify_all();
            if (retiring.valid()) retiring.wait();
            if (producer.valid()) producer.wait();
            QueueSubmitter::RetireDevice(device);
        }
    } cleanup{device, retiring, producer};
    heldInside.store(false);
    heldRelease.store(false);
    QueueSubmitter::RegisterDevice(device);
    auto worker = QueueSubmitter::For(device, queue, HeldSubmit);
    worker->Enqueue(JobFor(1));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!heldInside.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    Require(heldInside.load(std::memory_order_acquire), "the held submit did not start");
    std::uint64_t last = 0;
    for (unsigned index = 0; index < 64; ++index) last = worker->Enqueue(JobFor(index + 2));
    producer = std::async(std::launch::async, [&] {
        try { worker->Enqueue(JobFor(66)); }
        catch (const std::runtime_error&) { return true; }
        return false;
    });
    Require(producer.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout, "a full queue did not hold its producer");
    retiring = std::async(std::launch::async, [&] { QueueSubmitter::RetireDevice(device); });
    Require(AwaitRetirement(device, queue, HeldSubmit), "retirement did not close admission");
    Require(producer.wait_for(std::chrono::seconds(1)) == std::future_status::ready && producer.get(), "retirement did not release a backpressured producer before the held submit returned");
    bool queueRefused = false, accessRefused = false, registrationRefused = false;
    try { QueueSubmitter::For(device, lateQueue, SecondSubmit); }
    catch (const std::runtime_error&) { queueRefused = true; }
    try { const QueueAccess access(queue); }
    catch (const std::runtime_error&) { accessRefused = true; }
    try { QueueSubmitter::RegisterDevice(device); }
    catch (const std::runtime_error&) { registrationRefused = true; }
    heldRelease.store(true, std::memory_order_release);
    heldRelease.notify_all();
    retiring.get();
    Require(queueRefused && accessRefused && registrationRefused, "retirement admitted a new queue, queue access or device registration");
    Require(worker->Returned(last), "retirement left an accepted job unfinished");
    Require(QueueSubmitter::Existing(queue) == nullptr && QueueSubmitter::Existing(lateQueue) == nullptr, "retirement left a registered worker");
    bool resurrectionRefused = false;
    try { QueueSubmitter::For(device, lateQueue, SecondSubmit); }
    catch (const std::runtime_error&) { resurrectionRefused = true; }
    Require(resurrectionRefused, "an unregistered retired device created another worker");
    QueueSubmitter::RegisterDevice(device);
    auto fresh = QueueSubmitter::For(device, queue, SecondSubmit);
    Require(fresh != worker, "a new device generation reused the retired worker");
    bool oldRefused = false;
    try { worker->Enqueue(JobFor(67)); }
    catch (const std::runtime_error&) { oldRefused = true; }
    Require(oldRefused, "a retained old-generation worker accepted a job");
    fresh->WaitSubmitReturned(fresh->Enqueue(JobFor(68)));
    QueueSubmitter::RetireDevice(device);
    std::weak_ptr<QueueSubmitter> weak = worker;
    worker.reset();
    Require(weak.expired(), "the retired worker leaked after its last owner released it");
}

void retirementWaitsForQueueAccess() {
    const auto device = Fake<VkDevice>(0x21000);
    const auto queue = Fake<VkQueue>(0x22000);
    std::future<void> retiring;
    std::unique_ptr<QueueAccess> access;
    struct Cleanup {
        VkDevice device;
        std::future<void>& retiring;
        std::unique_ptr<QueueAccess>& access;
        ~Cleanup() {
            access.reset();
            if (retiring.valid()) retiring.wait();
            QueueSubmitter::RetireDevice(device);
        }
    } cleanup{device, retiring, access};
    QueueSubmitter::RegisterDevice(device);
    auto worker = QueueSubmitter::For(device, queue, FirstSubmit);
    access = std::make_unique<QueueAccess>(queue);
    retiring = std::async(std::launch::async, [&] { QueueSubmitter::RetireDevice(device); });
    Require(AwaitRetirement(device, queue, FirstSubmit), "queue-access retirement did not begin");
    Require(retiring.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout, "retirement returned while a queue user still held access");
    access.reset();
    retiring.get();
    Require(QueueSubmitter::Existing(queue) == nullptr, "a borrowed worker remained registered after retirement");
}

}

int main() {
    try {
        Require(QueueSubmitter::Existing(Fake<VkQueue>(0x2000)) == nullptr, "a queue without a submitter found one");
        ordered();
        retiredWithItsDevice();
        queueOwnershipAndReuse();
        drainedBeforeAccessAndRetirement();
        failureReachesItsJob();
        retirementClosesAdmissionAndReleasesOwnership();
        retirementWaitsForQueueAccess();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("queue submitter: 7 cases passed\n");
    return 0;
}
