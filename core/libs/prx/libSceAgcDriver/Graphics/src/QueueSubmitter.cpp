#include "prx/libSceAgcDriver/Graphics/include/QueueSubmitter.hpp"
#include "prx/libSceAgcDriver/Execution/include/ThreadPriority.hpp"
#include "prx/libSceAgcDriver/Graphics/include/RuntimeSwitches.hpp"
#include "prx/libc/include/HostThread.hpp"
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>

namespace AgcDriver::Graphics {

struct QueueSubmissionDevice {
    std::atomic<bool> retiring{false};
    bool finished = false;
    std::condition_variable retired;
};

namespace {

struct Submitters {
    std::mutex mutex;
    std::map<VkQueue, std::shared_ptr<QueueSubmitter>> byQueue;
    std::map<VkDevice, std::shared_ptr<QueueSubmissionDevice>> devices;
};

Submitters& AllSubmitters() {
    static Submitters* const submitters = new Submitters();
    return *submitters;
}

std::atomic<std::size_t> liveSubmitters{0};
std::atomic<std::uint64_t> anyFailure{0};
constexpr std::size_t MaxQueuedJobs = 64;

}

bool QueueSubmitter::Enabled() {
    return RuntimeSwitches::Set(RuntimeSwitch::AsyncSubmit);
}

void QueueSubmitter::RegisterDevice(VkDevice device) {
    if (device == VK_NULL_HANDLE) throw std::invalid_argument("AGC graphics: registering a null queue submitter device");
    auto& all = AllSubmitters();
    std::lock_guard lock(all.mutex);
    if (all.devices.contains(device)) throw std::runtime_error("AGC graphics: queue submitter device is already registered");
    all.devices.emplace(device, std::make_shared<QueueSubmissionDevice>());
}

std::shared_ptr<QueueSubmitter> QueueSubmitter::For(VkDevice device, VkQueue queue, PFN_vkQueueSubmit submit) {
    if (queue == VK_NULL_HANDLE || submit == nullptr) throw std::invalid_argument("AGC graphics: invalid queue submitter parameters");
    auto& all = AllSubmitters();
    std::lock_guard lock(all.mutex);
    const auto registered = all.devices.find(device);
    if (registered == all.devices.end() || registered->second->retiring.load(std::memory_order_acquire)) throw std::runtime_error("AGC graphics: queue submitter device is not active");
    const auto found = all.byQueue.find(queue);
    if (found != all.byQueue.end()) {
        if (found->second->device != device) throw std::runtime_error("AGC graphics: queue belongs to another registered device");
        return found->second;
    }
    auto created = std::shared_ptr<QueueSubmitter>(new QueueSubmitter(device, queue, submit, registered->second));
    all.byQueue.emplace(queue, created);
    liveSubmitters.fetch_add(1, std::memory_order_release);
    return created;
}

std::shared_ptr<QueueSubmitter> QueueSubmitter::Existing(VkQueue queue) {
    if (queue == VK_NULL_HANDLE || liveSubmitters.load(std::memory_order_acquire) == 0) return nullptr;
    auto& all = AllSubmitters();
    std::lock_guard lock(all.mutex);
    const auto found = all.byQueue.find(queue);
    return found != all.byQueue.end() ? found->second : nullptr;
}

void QueueSubmitter::DrainDevice(VkDevice device) {
    auto& all = AllSubmitters();
    std::vector<std::shared_ptr<QueueSubmitter>> submitters;
    {
        std::lock_guard lock(all.mutex);
        for (const auto& entry : all.byQueue) if (entry.second->device == device) submitters.push_back(entry.second);
    }
    for (const auto& submitter : submitters) submitter->Drain();
}

void QueueSubmitter::RetireDevice(VkDevice device) {
    auto& all = AllSubmitters();
    std::vector<std::shared_ptr<QueueSubmitter>> retired;
    std::shared_ptr<QueueSubmissionDevice> admission;
    {
        std::unique_lock lock(all.mutex);
        const auto found = all.devices.find(device);
        if (found == all.devices.end()) return;
        admission = found->second;
        if (admission->retiring.load(std::memory_order_acquire)) {
            admission->retired.wait(lock, [&] { return admission->finished; });
            return;
        }
        for (const auto& entry : all.byQueue) if (entry.second->device == device) retired.push_back(entry.second);
        admission->retiring.store(true, std::memory_order_release);
        for (const auto& submitter : retired) submitter->requestStop();
    }
    for (const auto& submitter : retired) {
        submitter->stop();
        std::unique_lock access(submitter->accessMutex);
    }
    {
        std::lock_guard lock(all.mutex);
        for (const auto& submitter : retired) {
            const auto found = all.byQueue.find(submitter->queue);
            if (found != all.byQueue.end() && found->second == submitter) {
                all.byQueue.erase(found);
                liveSubmitters.fetch_sub(1, std::memory_order_release);
            }
        }
        all.devices.erase(device);
        admission->finished = true;
    }
    admission->retired.notify_all();
}

std::uint64_t QueueSubmitter::FailedSubmits(VkDevice device) {
    if (liveSubmitters.load(std::memory_order_acquire) == 0) return 0;
    auto& all = AllSubmitters();
    std::lock_guard lock(all.mutex);
    std::uint64_t count = 0;
    for (const auto& entry : all.byQueue) {
        if (entry.second->device == device) count += entry.second->failed.load(std::memory_order_acquire);
    }
    return count;
}

bool QueueSubmitter::FailedThrough(VkDevice device, std::uint64_t serial) {
    if (anyFailure.load(std::memory_order_acquire) == 0 || liveSubmitters.load(std::memory_order_acquire) == 0) return false;
    auto& all = AllSubmitters();
    std::lock_guard lock(all.mutex);
    for (const auto& entry : all.byQueue) {
        if (entry.second->device == device && entry.second->firstFailedSerial.load(std::memory_order_acquire) <= serial) return true;
    }
    return false;
}

QueueSubmitter::~QueueSubmitter() {
    stop();
}

QueueSubmitter::QueueSubmitter(VkDevice device, VkQueue queue, PFN_vkQueueSubmit submit, std::shared_ptr<QueueSubmissionDevice> admission) :
    device(device), queue(queue), submit(submit), admission(std::move(admission)) {
    thread = Libc::StartHostThread([this] {
        try {
            RaiseWorkerThreadPriority("queue submitter");
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[gpu] queue submitter: %s\n", error.what());
        }
        run();
    });
}

void QueueSubmitter::requestStop() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    ready.notify_one();
    drained.notify_all();
}

void QueueSubmitter::stop() {
    std::call_once(stopped, [&] {
        requestStop();
        if (thread.joinable()) thread.join();
    });
}

std::uint64_t QueueSubmitter::Enqueue(const Job& job) {
    bool wake = false;
    std::uint64_t position = 0;
    {
        std::unique_lock lock(mutex);
        drained.wait(lock, [&] { return jobs.size() < MaxQueuedJobs || stopping || admission->retiring.load(std::memory_order_acquire); });
        if (stopping || admission->retiring.load(std::memory_order_acquire)) throw std::runtime_error("AGC graphics: a batch was queued on the queue submitter of a retired device");
        jobs.push_back(job);
        position = ++enqueued;
        wake = sleeping;
    }
    if (wake) ready.notify_one();
    return position;
}

void QueueSubmitter::WaitSubmitReturned(std::uint64_t position) const {
    for (auto seen = returned.load(std::memory_order_acquire); seen < position; seen = returned.load(std::memory_order_acquire)) returned.wait(seen, std::memory_order_acquire);
}

void QueueSubmitter::Drain() {
    std::unique_lock lock(mutex);
    drained.wait(lock, [&] { return jobs.empty() && !busy; });
}

std::uint64_t QueueSubmitter::Submitted() const {
    std::lock_guard lock(mutex);
    return submitted;
}

void QueueSubmitter::run() {
    for (;;) {
        Job job;
        std::uint64_t position = 0;
        {
            std::unique_lock lock(mutex);
            while (jobs.empty()) {
                if (stopping) return;
                sleeping = true;
                ready.wait(lock);
                sleeping = false;
            }
            job = jobs.front();
            jobs.pop_front();
            position = ++taken;
            busy = true;
        }
        VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submission.commandBufferCount = 1;
        submission.pCommandBuffers = &job.commands;
        VkTimelineSemaphoreSubmitInfoKHR timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR};
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (job.wait != VK_NULL_HANDLE) {
            submission.waitSemaphoreCount = 1;
            submission.pWaitSemaphores = &job.wait;
            submission.pWaitDstStageMask = &waitStage;
            timeline.waitSemaphoreValueCount = 1;
            timeline.pWaitSemaphoreValues = &job.waitValue;
        }
        if (job.signal != VK_NULL_HANDLE) {
            submission.signalSemaphoreCount = 1;
            submission.pSignalSemaphores = &job.signal;
            timeline.signalSemaphoreValueCount = 1;
            timeline.pSignalSemaphoreValues = &job.signalValue;
        }
        if (job.wait != VK_NULL_HANDLE || job.signal != VK_NULL_HANDLE) submission.pNext = &timeline;
        VkResult result = stoppedWith;
        if (result == VK_SUCCESS) {
            std::lock_guard queueLock(queueMutex);
            result = submit(queue, 1, &submission, job.fence);
        }
        if (result != VK_SUCCESS) {
            stoppedWith = result;
            if (job.signal != VK_NULL_HANDLE) {
                auto first = firstFailedSerial.load(std::memory_order_relaxed);
                while (job.signalValue < first && !firstFailedSerial.compare_exchange_weak(first, job.signalValue, std::memory_order_acq_rel)) {
                }
            }
            anyFailure.fetch_add(1, std::memory_order_acq_rel);
        }
        if (job.result != nullptr) *job.result = result;
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) failed.fetch_add(1, std::memory_order_release);
        returned.store(position, std::memory_order_release);
        returned.notify_all();
        {
            std::lock_guard lock(mutex);
            busy = false;
            ++submitted;
        }
        drained.notify_all();
    }
}

QueueAccess::QueueAccess(VkQueue queue) {
    if (queue == VK_NULL_HANDLE || liveSubmitters.load(std::memory_order_acquire) == 0) return;
    auto& all = AllSubmitters();
    {
        std::lock_guard lock(all.mutex);
        const auto found = all.byQueue.find(queue);
        if (found == all.byQueue.end()) return;
        if (found->second->admission->retiring.load(std::memory_order_acquire)) throw std::runtime_error("AGC graphics: queue access during device retirement");
        submitter = found->second;
        access = std::shared_lock(submitter->accessMutex);
    }
    submitter->Drain();
    submitter->queueMutex.lock();
}

QueueAccess::~QueueAccess() {
    if (submitter != nullptr) submitter->queueMutex.unlock();
}

}
