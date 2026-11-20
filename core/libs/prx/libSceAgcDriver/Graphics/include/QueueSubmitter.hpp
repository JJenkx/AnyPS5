#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_QUEUESUBMITTER_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_QUEUESUBMITTER_HPP

#include "prx/libSceAgcDriver/Execution/include/Mutex.hpp"
#include <vulkan/vulkan.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace AgcDriver::Graphics {

struct QueueSubmissionDevice;

class QueueSubmitter {
public:
    struct Job {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore wait = VK_NULL_HANDLE;
        std::uint64_t waitValue = 0;
        VkSemaphore signal = VK_NULL_HANDLE;
        std::uint64_t signalValue = 0;
        VkResult* result = nullptr;
    };

    static bool Enabled();
    static void RegisterDevice(VkDevice device);
    static std::shared_ptr<QueueSubmitter> For(VkDevice device, VkQueue queue, PFN_vkQueueSubmit submit);
    static std::shared_ptr<QueueSubmitter> Existing(VkQueue queue);
    static void DrainDevice(VkDevice device);
    static void RetireDevice(VkDevice device);
    static std::uint64_t FailedSubmits(VkDevice device);
    static bool FailedThrough(VkDevice device, std::uint64_t serial);

    ~QueueSubmitter();

    std::uint64_t Enqueue(const Job& job);
    bool Returned(std::uint64_t position) const { return returned.load(std::memory_order_acquire) >= position; }
    void WaitSubmitReturned(std::uint64_t position) const;
    void Drain();
    std::uint64_t Submitted() const;

private:
    friend class QueueAccess;
    QueueSubmitter(VkDevice device, VkQueue queue, PFN_vkQueueSubmit submit, std::shared_ptr<QueueSubmissionDevice> admission);
    void run();
    void requestStop();
    void stop();

    VkDevice device;
    VkQueue queue;
    PFN_vkQueueSubmit submit;
    std::shared_ptr<QueueSubmissionDevice> admission;
    std::shared_mutex accessMutex;
    AgcDriver::Mutex queueMutex;
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable drained;
    std::deque<Job> jobs;
    bool busy = false;
    bool sleeping = false;
    bool stopping = false;
    std::uint64_t submitted = 0;
    std::uint64_t enqueued = 0;
    std::uint64_t taken = 0;
    std::atomic<std::uint64_t> returned{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> firstFailedSerial{~std::uint64_t{0}};
    VkResult stoppedWith = VK_SUCCESS;
    std::once_flag stopped;
    std::thread thread;
};

class QueueAccess {
public:
    explicit QueueAccess(VkQueue queue);
    ~QueueAccess();
    QueueAccess(const QueueAccess&) = delete;
    QueueAccess& operator=(const QueueAccess&) = delete;

private:
    std::shared_ptr<QueueSubmitter> submitter;
    std::shared_lock<std::shared_mutex> access;
};

}

#endif
