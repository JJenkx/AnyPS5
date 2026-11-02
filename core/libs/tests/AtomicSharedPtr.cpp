#include <atomic>
#include <memory>
#if defined(APS5_TEST_ATOMIC_SHARED_PTR_FALLBACK)
#undef __cpp_lib_atomic_shared_ptr
#endif
#include "prx/libc/include/general/AtomicSharedPtr.hpp"
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

#if defined(APS5_TEST_ATOMIC_SHARED_PTR_FALLBACK)
static_assert(!std::is_same_v<AtomicSharedPtr<int>, std::atomic<std::shared_ptr<int>>>);
#endif

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<typename T>
bool SameOwner(const std::shared_ptr<T>& first, const std::shared_ptr<T>& second) {
    return !first.owner_before(second) && !second.owner_before(first);
}

void CheckOwnership() {
    int number = 1;
    const auto first = std::shared_ptr<int>(&number, [](int*) {});
    const auto other = std::shared_ptr<int>(&number, [](int*) {});
    AtomicSharedPtr<int> value(first);
    auto expected = other;
    const auto desired = std::make_shared<int>(2);
    Require(!value.compare_exchange_strong(expected, desired), "equal addresses with distinct owners were exchanged");
    Require(expected.get() == first.get() && SameOwner(expected, first), "failed comparison did not return the stored owner");
    Require(SameOwner(value.load(), first), "failed comparison changed the stored owner");
    Require(value.compare_exchange_strong(expected, desired), "equal pointers and owners were not exchanged");
    Require(expected.get() == first.get() && SameOwner(expected, first), "successful comparison changed expected");
    Require(SameOwner(value.load(), desired), "successful comparison lost desired ownership");

    const auto pair = std::make_shared<std::pair<int, int>>(3, 4);
    const auto left = std::shared_ptr<int>(pair, &pair->first);
    const auto right = std::shared_ptr<int>(pair, &pair->second);
    value.store(left);
    expected = right;
    Require(!value.compare_exchange_strong(expected, desired), "different aliases with the same owner were exchanged");
    Require(expected.get() == left.get() && SameOwner(expected, left), "failed alias comparison did not return the stored pointer");
    const auto equivalent = std::shared_ptr<int>(pair, &pair->first);
    expected = equivalent;
    Require(value.compare_exchange_strong(expected, desired), "equivalent aliases were not exchanged");

    const auto owningNull = std::shared_ptr<int>(pair, nullptr);
    value.store(owningNull);
    expected.reset();
    Require(!value.compare_exchange_strong(expected, desired), "owning and empty null pointers were exchanged");
    Require(expected.get() == nullptr && SameOwner(expected, owningNull), "null comparison did not retain ownership");
    Require(value.compare_exchange_strong(expected, desired), "equivalent owning null pointers were not exchanged");

    const std::shared_ptr<int> empty;
    value.store(empty);
    expected = owningNull;
    Require(!value.compare_exchange_strong(expected, desired), "empty and owning null pointers were exchanged");
    Require(expected.use_count() == 0, "failed empty comparison retained the old owner");
    Require(value.compare_exchange_strong(expected, desired), "empty pointers were not exchanged");

    const auto unowned = std::shared_ptr<int>(empty, &number);
    value.store(unowned);
    expected = std::shared_ptr<int>(empty, &number);
    Require(value.compare_exchange_strong(expected, desired), "equivalent non-owning aliases were not exchanged");
}

void CheckRelease() {
    AtomicSharedPtr<int> value(std::make_shared<int>(7));
    bool released = false;
    auto expected = std::shared_ptr<int>(new int(9), [&](int* pointer) {
        released = *value.load() == 7;
        delete pointer;
    });
    Require(!value.compare_exchange_strong(expected, std::make_shared<int>(11)), "different pointers were exchanged");
    Require(released, "failed comparison did not release expected outside the atomic operation");
    Require(*expected == 7, "failed comparison did not update expected");
}

void CheckConcurrent() {
    AtomicSharedPtr<int> value(std::make_shared<int>(0));
    constexpr int Workers = 4;
    constexpr int Rounds = 500;
    std::vector<std::thread> workers;
    for (int worker = 0; worker < Workers; ++worker) {
        workers.emplace_back([&] {
            for (int round = 0; round < Rounds; ++round) {
                auto expected = value.load();
                for (;;) {
                    const auto desired = std::make_shared<int>(*expected + 1);
                    if (value.compare_exchange_strong(expected, desired)) break;
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    Require(*value.load() == Workers * Rounds, "concurrent comparisons lost an update");
}

}

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "expected one test group");
        const std::string_view group(argv[1]);
        if (group == "ownership") CheckOwnership();
        else if (group == "release") CheckRelease();
        else if (group == "concurrent") CheckConcurrent();
        else throw std::runtime_error("unknown test group");
        std::puts("atomic shared pointer tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "AtomicSharedPtr: %s\n", error.what());
        return 1;
    }
}
