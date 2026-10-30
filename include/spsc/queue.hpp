#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <utility>

namespace kuczmars {

template <typename T, std::size_t Capacity>
class SPSCQueue {
public:
    static_assert(Capacity > 0, "Capacity must be greater than 0");
    SPSCQueue() = default;
    ~SPSCQueue() {
        auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_relaxed);
        while (t >= h ? t - h : t + N - h) {
            T* p = std::launder(slot_ptr(h));
            std::destroy_at(p);
            if (++h == N) {
                h = 0;
            }
        }
    }
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        const auto t = tail_.load(std::memory_order_relaxed);
        auto next = t + 1;
        if (next == N) {
            next = 0;
        }
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        std::construct_at<T>(slot_ptr(t), std::forward<Args>(args)...);
        tail_.store(next, std::memory_order_release);
        return true;
    }
    bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }
    bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(value));
    }

    [[nodiscard]] T* front() noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return std::launder(slot_ptr(h));
    }

    // Removes the front element of the container.
    // If empty() is true, the behavior is undefined.
    void pop() noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        std::destroy_at(std::launder(slot_ptr(h)));
        auto next = h + 1;
        if (next == N) {
            next = 0;
        }
        head_.store(next, std::memory_order_release);
    }
    std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const auto h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        T* p = std::launder(slot_ptr(h));
        std::optional<T> value = std::move(*p);
        std::destroy_at(p);
        auto next = h + 1;
        if (next == N) {
            next = 0;
        }
        head_.store(next, std::memory_order_release);
        return value;
    }

    static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }
    // size() member value is approximation as it may change right after the value is read
    [[nodiscard]]std::size_t size() const noexcept {
        const auto h = head_.load(std::memory_order_acquire);
        const auto t = tail_.load(std::memory_order_acquire);
        return t >= h ? t - h : t + N - h;
    }
    // empty() member state is approximation as it may change right after the value is read
    [[nodiscard]]bool empty() const noexcept {
        return size() == 0;
    }
private:
    struct Slot {
        alignas(T) std::byte data[sizeof(T)];
    };
    T* slot_ptr(const std::size_t i) noexcept {
        return reinterpret_cast<T*>(storage_[i].data);
    }
    std::unique_ptr<Slot[]> storage_ = std::make_unique_for_overwrite<Slot[]>(N);
    alignas(64) std::atomic<std::size_t> head_{};
    alignas(64) std::atomic<std::size_t> tail_{};
    static constexpr std::size_t N = Capacity + 1;   // one spare slot
};

} // namespace kuczmars
