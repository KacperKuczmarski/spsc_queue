#include <gtest/gtest.h>

#include <spsc/queue.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int warmup_runs = 1;
constexpr int timed_runs = 5;
constexpr int throughput_items = 1 << 24;
constexpr int round_trips = 1 << 20;

enum class Consume { front_pop, try_pop };

constexpr std::string_view to_string(const Consume mode) {
    return mode == Consume::front_pop ? "front_pop" : "try_pop";
}

template <Consume Mode, typename Queue>
bool pop_one(Queue& q, int& v) noexcept {
    if constexpr (Mode == Consume::front_pop) {
        if (const int* p = q.front()) {
            v = *p;
            q.pop();
            return true;
        }
        return false;
    } else {
        if (auto value = q.try_pop()) {
            v = *value;
            return true;
        }
        return false;
    }
}

void report(const std::string& name, std::vector<double> samples, const std::string_view unit) {
    std::ranges::sort(samples);
    const std::size_t n = samples.size();
    const double median = n % 2 == 1 ? samples[n / 2] : (samples[n / 2 - 1] + samples[n / 2]) / 2;
    std::printf("[ BENCH    ] %-36s median %8.3f %-7s min %8.3f  max %8.3f  (%zu runs, %.1f M/s)\n",
                name.c_str(), median, std::string(unit).c_str(), samples.front(), samples.back(), n,
                1e3 / median);
    std::fflush(stdout);
    ::testing::Test::RecordProperty("median_ns", std::to_string(median));
    ::testing::Test::RecordProperty("min_ns", std::to_string(samples.front()));
    ::testing::Test::RecordProperty("max_ns", std::to_string(samples.back()));
}

template <Consume Mode>
void run_throughput(const std::size_t capacity) {
    const auto storage = std::make_unique<kuczmars::SPSCQueue<int>>(capacity);
    std::vector<double> ns_per_item;

    for (int run = 0; run < warmup_runs + timed_runs; ++run) {
        std::atomic<bool> ready{false};
        bool in_order = true;
        Clock::time_point end;

        std::thread consumer([q = storage.get(), &ready, &in_order, &end] {
            ready.store(true, std::memory_order_release);
            bool ok = true;
            for (int i = 0; i < throughput_items; ++i) {
                int v;
                while (!pop_one<Mode>(*q, v)) {
                }
                ok &= v == i;
            }
            end = Clock::now();
            in_order = ok;
        });
        while (!ready.load(std::memory_order_acquire)) {
        }

        auto* const q = storage.get();
        const auto start = Clock::now();
        for (int i = 0; i < throughput_items; ++i) {
            while (!q->try_push(i)) {
            }
        }
        consumer.join();

        ASSERT_TRUE(in_order) << "consumer received items out of order";
        if (run >= warmup_runs) {
            ns_per_item.push_back(std::chrono::duration<double, std::nano>(end - start).count() / throughput_items);
        }
    }

    report("Throughput/" + std::string(to_string(Mode)) + "/capacity:" + std::to_string(capacity),
           std::move(ns_per_item), "ns/item");
}

// Each trip sends one item to an echo thread and waits for it to come back.
template <Consume Mode>
void run_round_trip(const std::size_t capacity) {
    using Queue = kuczmars::SPSCQueue<int>;
    const auto ping_storage = std::make_unique<Queue>(capacity);
    const auto pong_storage = std::make_unique<Queue>(capacity);
    std::vector<double> ns_per_trip;

    for (int run = 0; run < warmup_runs + timed_runs; ++run) {
        std::atomic<bool> ready{false};

        std::thread echo([ping = ping_storage.get(), pong = pong_storage.get(), &ready] {
            ready.store(true, std::memory_order_release);
            for (int i = 0; i < round_trips; ++i) {
                int v;
                while (!pop_one<Mode>(*ping, v)) {
                }
                while (!pong->try_push(v)) {
                }
            }
        });
        while (!ready.load(std::memory_order_acquire)) {
        }

        auto* const ping = ping_storage.get();
        auto* const pong = pong_storage.get();
        bool matched = true;
        const auto start = Clock::now();
        for (int i = 0; i < round_trips; ++i) {
            while (!ping->try_push(i)) {
            }
            int v;
            while (!pop_one<Mode>(*pong, v)) {
            }
            matched &= v == i;
        }
        const auto end = Clock::now();
        echo.join();

        ASSERT_TRUE(matched) << "echoed item does not match the item sent";
        if (run >= warmup_runs) {
            ns_per_trip.push_back(std::chrono::duration<double, std::nano>(end - start).count() / round_trips);
        }
    }

    report("RoundTrip/" + std::string(to_string(Mode)) + "/capacity:" + std::to_string(capacity),
           std::move(ns_per_trip), "ns/trip");
}

// Small enough that the producer regularly finds the queue full.
constexpr std::size_t small_capacity = 1024;
// At least as large as a throughput run, so the producer never blocks.
constexpr std::size_t large_capacity = std::size_t{1} << 24;

} // namespace

TEST(QueueBenchmark, ThroughputFrontPopSmall) { run_throughput<Consume::front_pop>(small_capacity); }
TEST(QueueBenchmark, ThroughputTryPopSmall) { run_throughput<Consume::try_pop>(small_capacity); }
TEST(QueueBenchmark, ThroughputFrontPopLarge) { run_throughput<Consume::front_pop>(large_capacity); }
TEST(QueueBenchmark, ThroughputTryPopLarge) { run_throughput<Consume::try_pop>(large_capacity); }

TEST(QueueBenchmark, RoundTripFrontPopSmall) { run_round_trip<Consume::front_pop>(small_capacity); }
TEST(QueueBenchmark, RoundTripTryPopSmall) { run_round_trip<Consume::try_pop>(small_capacity); }
TEST(QueueBenchmark, RoundTripFrontPopLarge) { run_round_trip<Consume::front_pop>(large_capacity); }
TEST(QueueBenchmark, RoundTripTryPopLarge) { run_round_trip<Consume::try_pop>(large_capacity); }