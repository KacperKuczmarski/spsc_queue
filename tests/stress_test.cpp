#include <gtest/gtest.h>

#include <spsc/queue.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace {

template <std::size_t Capacity>
void run_order_stress(const std::uint64_t count) {
    kuczmars::SPSCQueue<std::uint64_t, Capacity> q;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::uint64_t expected = 0;
    while (expected < count) {
        if (auto value = q.try_pop()) {
            ASSERT_EQ(*value, expected);
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();
    EXPECT_TRUE(q.empty());
}

} // namespace

TEST(QueueStressTest, ProducerConsumerPreservesOrder) {
    run_order_stress<1024>(1'000'000);
}

TEST(QueueStressTest, SmallCapacityPreservesOrder) {
    run_order_stress<3>(200'000);
}

TEST(QueueStressTest, CapacityOnePreservesOrder) {
    run_order_stress<1>(100'000);
}

TEST(QueueStressTest, HeapAllocatedElementsArriveIntact) {
    constexpr int count = 200'000;
    kuczmars::SPSCQueue<std::string, 100> q;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            // Long enough to defeat the small-string optimisation.
            std::string s = "message-number-" + std::to_string(i) + "-padding-padding";
            while (!q.try_push(std::move(s))) {
                std::this_thread::yield();
            }
        }
    });

    for (int i = 0; i < count;) {
        if (std::string* s = q.front()) {
            ASSERT_EQ(*s, "message-number-" + std::to_string(i) + "-padding-padding");
            q.pop();
            ++i;
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();
    EXPECT_TRUE(q.empty());
}

TEST(QueueStressTest, SizeStaysWithinBoundsWhileObserved) {
    constexpr std::uint64_t count = 300'000;
    constexpr std::size_t cap = 7;
    kuczmars::SPSCQueue<std::uint64_t, cap> q;
    std::atomic<bool> done{false};
    std::atomic<std::size_t> max_seen{0};

    std::thread observer([&] {
        while (!done.load(std::memory_order_relaxed)) {
            const std::size_t s = q.size();
            if (s > max_seen.load(std::memory_order_relaxed)) {
                max_seen.store(s, std::memory_order_relaxed);
            }
        }
    });

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
            EXPECT_LE(q.size(), cap);
        }
    });

    for (std::uint64_t expected = 0; expected < count;) {
        if (auto value = q.try_pop()) {
            ASSERT_EQ(*value, expected);
            ++expected;
        }
        ASSERT_LE(q.size(), cap);
    }

    producer.join();
    done.store(true, std::memory_order_relaxed);
    observer.join();
    EXPECT_LE(max_seen.load(), cap);
}