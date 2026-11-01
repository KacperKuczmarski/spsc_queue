#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <type_traits>

namespace kuczmars {

template <typename T>
class SPSCQueue {
public:
    static_assert(std::is_nothrow_destructible_v<T>);
    static_assert(std::is_object_v<T> and not std::is_array_v<T>);

    explicit SPSCQueue(const std::size_t capacity)
        : n_(slot_count(capacity)), storage_(std::make_unique_for_overwrite<Slot[]>(n_)) {}
    ~SPSCQueue() {
        auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_relaxed);
        while (h != t) {
            T* p = std::launder(slot_ptr(h));
            std::destroy_at(p);
            if (++h == n_) {
                h = 0;
            }
        }
    }
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        const auto t = tail_.load(std::memory_order_relaxed);
        auto next = t + 1;
        if (next == n_) {
            next = 0;
        }
        if (next == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (next == cached_head_) {
                return false;
            }
        }
        std::construct_at<T>(slot_ptr(t), std::forward<Args>(args)...);
        tail_.store(next, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }
    [[nodiscard]] bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(value));
    }

    [[nodiscard]] T* front() noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        if (h == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (h == cached_tail_) {
                return nullptr;
            }
        }
        return std::launder(slot_ptr(h));
    }

    // Removes the front element of the container.
    // If empty() is true, the behavior is undefined.
    void pop() noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        // The acquire load synchronizes with the producer when front() was not called first.
        if (h == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            assert(h != cached_tail_ && "pop() on empty queue");
        }
        std::destroy_at(std::launder(slot_ptr(h)));
        auto next = h + 1;
        if (next == n_) {
            next = 0;
        }
        head_.store(next, std::memory_order_release);
    }
    [[nodiscard]] std::optional<T> try_pop() noexcept requires std::is_nothrow_move_constructible_v<T> {
        const auto h = head_.load(std::memory_order_relaxed);
        if (h == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (h == cached_tail_) {
                return std::nullopt;
            }
        }
        T* p = std::launder(slot_ptr(h));
        std::optional<T> value(std::in_place, std::move(*p));
        std::destroy_at(p);
        auto next = h + 1;
        if (next == n_) {
            next = 0;
        }
        head_.store(next, std::memory_order_release);
        return value;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return n_ - 1;
    }
    // Returns an approximate size that may not correspond to any single queue state.
    [[nodiscard]] std::size_t size() const noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_relaxed);
        return t >= h ? t - h : t + n_ - h;
    }
    // Returns whether the approximate size is zero.
    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }
private:
    struct Slot {
        alignas(T) std::byte data[sizeof(T)];
    };
    static std::size_t slot_count(const std::size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("SPSCQueue capacity must be greater than 0");
        }
        if (capacity == std::numeric_limits<std::size_t>::max()) {
            throw std::length_error("SPSCQueue capacity must leave room for one spare slot");
        }
        return capacity + 1;   // one spare slot
    }
    T* slot_ptr(const std::size_t i) noexcept {
        return reinterpret_cast<T*>(storage_[i].data);
    }
    // Read-only after construction; shared by both threads.
    const std::size_t n_;
    const std::unique_ptr<Slot[]> storage_;
    // Written only by the consumer.
    alignas(128) std::atomic<std::size_t> head_{};
    std::size_t cached_tail_ = 0;
    // Written only by the producer.
    alignas(128) std::atomic<std::size_t> tail_{};
    std::size_t cached_head_ = 0;
};

} // namespace kuczmars