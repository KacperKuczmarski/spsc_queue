#include <gtest/gtest.h>

#include <spsc/queue.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

void run_order_stress(const std::size_t capacity, const std::uint64_t count) {
    kuczmars::SPSCQueue<std::uint64_t> q(capacity);
    std::atomic<bool> stop{false};

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            while (!q.try_push(i)) {
                if (stop.load(std::memory_order_relaxed)) {
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    // Asserting here would return with the producer still joinable and call
    // std::terminate, so stop at the first wrong value and assert after join().
    std::uint64_t expected = 0;
    std::uint64_t received = 0;
    while (expected < count) {
        if (auto value = q.try_pop()) {
            received = *value;
            if (received != expected) {
                break;
            }
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }

    stop.store(true, std::memory_order_relaxed);
    producer.join();
    ASSERT_EQ(expected, count) << "received " << received << " where " << expected << " was expected";
    EXPECT_TRUE(q.empty());
}

// Records its value into a consumer-owned sink when destroyed, so order can be
// checked even when the consumer never reads the element through front().
struct Recorder {
    Recorder(const std::uint64_t v, std::vector<std::uint64_t>& s) : value(v), sink(&s) {}
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    ~Recorder() { sink->push_back(value); }

    std::uint64_t value;
    std::vector<std::uint64_t>* sink;
};

struct Counted {
    static constexpr std::uint32_t live_tag = 0x11fe11fe;
    static constexpr std::uint32_t dead_tag = 0xdeadbeef;
    static inline std::atomic<int> alive{0};
    static inline std::atomic<int> bad_destroys{0};

    explicit Counted(const std::uint64_t v) : value(v) { alive.fetch_add(1, std::memory_order_relaxed); }
    Counted(const Counted& other) : value(other.value) { alive.fetch_add(1, std::memory_order_relaxed); }
    Counted(Counted&& other) noexcept : value(other.value) { alive.fetch_add(1, std::memory_order_relaxed); }
    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() {
        if (tag != live_tag) {
            bad_destroys.fetch_add(1, std::memory_order_relaxed);
        }
        tag = dead_tag;
        alive.fetch_sub(1, std::memory_order_relaxed);
    }

    std::uint64_t value;
    std::uint32_t tag = live_tag;
};

} // namespace

TEST(QueueStressTest, ProducerConsumerPreservesOrder) {
    run_order_stress(1024, 1'000'000);
}

TEST(QueueStressTest, SmallCapacityPreservesOrder) {
    run_order_stress(3, 200'000);
}

TEST(QueueStressTest, CapacityOnePreservesOrder) {
    run_order_stress(1, 100'000);
}

TEST(QueueStressTest, HeapAllocatedElementsArriveIntact) {
    constexpr int count = 200'000;
    kuczmars::SPSCQueue<std::string> q(100);
    std::atomic<bool> stop{false};
    const auto message = [](const int i) {
        // Long enough to defeat the small-string optimisation.
        return "message-number-" + std::to_string(i) + "-padding-padding";
    };

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            std::string s = message(i);
            while (!q.try_push(std::move(s))) {
                if (stop.load(std::memory_order_relaxed)) {
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    int i = 0;
    std::string received;
    while (i < count) {
        if (std::string* s = q.front()) {
            if (*s != message(i)) {
                received = *s;
                break;
            }
            q.pop();
            ++i;
        } else {
            std::this_thread::yield();
        }
    }

    stop.store(true, std::memory_order_relaxed);
    producer.join();
    ASSERT_EQ(i, count) << "received \"" << received << "\" where \"" << message(i) << "\" was expected";
    EXPECT_TRUE(q.empty());
}

TEST(QueueStressTest, SizeStaysWithinBoundsWhileObserved) {
    constexpr std::uint64_t count = 300'000;
    constexpr std::size_t cap = 7;
    kuczmars::SPSCQueue<std::uint64_t> q(cap);
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
                if (done.load(std::memory_order_relaxed)) {
                    return;
                }
                std::this_thread::yield();
            }
            EXPECT_LE(q.size(), cap);
        }
    });

    std::uint64_t expected = 0;
    std::uint64_t received = 0;
    std::size_t consumer_size = 0;
    while (expected < count) {
        if (auto value = q.try_pop()) {
            received = *value;
            if (received != expected) {
                break;
            }
            ++expected;
        }
        consumer_size = q.size();
        if (consumer_size > cap) {
            break;
        }
    }

    done.store(true, std::memory_order_relaxed);
    producer.join();
    observer.join();
    ASSERT_LE(consumer_size, cap);
    ASSERT_EQ(expected, count) << "received " << received << " where " << expected << " was expected";
    EXPECT_LE(max_seen.load(), cap);
}

TEST(QueueStressTest, PopWithoutFrontPreservesOrder) {
    constexpr std::uint64_t count = 200'000;
    std::vector<std::uint64_t> popped;
    popped.reserve(count + 3);
    std::atomic<bool> stop{false};
    bool in_order = true;
    {
        kuczmars::SPSCQueue<Recorder> q(3);

        std::thread producer([&] {
            for (std::uint64_t i = 0; i < count; ++i) {
                while (!q.try_emplace(i, popped)) {
                    if (stop.load(std::memory_order_relaxed)) {
                        return;
                    }
                    std::this_thread::yield();
                }
            }
        });

        while (in_order && popped.size() < count) {
            const std::uint64_t expected = popped.size();
            bool consumed = false;
            if (expected % 2 == 0) {
                if (!q.empty()) {
                    q.pop();
                    consumed = true;
                }
            } else if (const Recorder* r = q.front()) {
                in_order = r->value == expected;
                q.pop();
                consumed = true;
            }
            if (consumed) {
                in_order = in_order && popped.size() == expected + 1 && popped.back() == expected;
            } else {
                std::this_thread::yield();
            }
        }

        stop.store(true, std::memory_order_relaxed);
        producer.join();
        EXPECT_TRUE(in_order) << "out of order at element " << popped.size() - 1;
        EXPECT_TRUE(q.empty());
    }

    EXPECT_EQ(popped.size(), count);
}

TEST(QueueStressTest, ConcurrentLifetimesStayBalanced) {
    constexpr std::uint64_t count = 200'000;
    constexpr std::size_t cap = 5;
    Counted::alive = 0;
    Counted::bad_destroys = 0;
    std::atomic<bool> stop{false};
    std::uint64_t mismatch_at = count;
    {
        kuczmars::SPSCQueue<Counted> q(cap);

        std::thread producer([&] {
            const auto retry = [&] {
                std::this_thread::yield();
                return !stop.load(std::memory_order_relaxed);
            };
            for (std::uint64_t i = 0; i < count; ++i) {
                switch (i % 3) {
                    case 0:
                        while (!q.try_emplace(i)) {
                            if (!retry()) {
                                return;
                            }
                        }
                        break;
                    case 1: {
                        const Counted c{i};
                        while (!q.try_push(c)) {
                            if (!retry()) {
                                return;
                            }
                        }
                        break;
                    }
                    case 2: {
                        Counted c{i};
                        while (!q.try_push(std::move(c))) {
                            if (!retry()) {
                                return;
                            }
                        }
                        break;
                    }
                }
            }
        });

        // Leave a full queue behind so the destructor also destroys elements built by the producer thread.
        for (std::uint64_t expected = 0; expected < count - cap && mismatch_at == count;) {
            bool consumed = false;
            switch (expected % 3) {
                case 0:
                    if (auto v = q.try_pop()) {
                        if (v->value != expected) {
                            mismatch_at = expected;
                        }
                        consumed = true;
                    }
                    break;
                case 1:
                    if (const Counted* c = q.front()) {
                        if (c->value != expected) {
                            mismatch_at = expected;
                        }
                        q.pop();
                        consumed = true;
                    }
                    break;
                case 2:
                    if (!q.empty()) {
                        q.pop();
                        consumed = true;
                    }
                    break;
            }
            if (consumed) {
                ++expected;
            } else {
                std::this_thread::yield();
            }
        }

        if (mismatch_at != count) {
            stop.store(true, std::memory_order_relaxed);
        }
        producer.join();
        ASSERT_EQ(mismatch_at, count) << "wrong element received";
        EXPECT_EQ(q.size(), cap);
        EXPECT_EQ(Counted::alive.load(), static_cast<int>(cap));
        ASSERT_NE(q.front(), nullptr);
        EXPECT_EQ(q.front()->value, count - cap);
    }

    EXPECT_EQ(Counted::bad_destroys.load(), 0);
    EXPECT_EQ(Counted::alive.load(), 0);
}