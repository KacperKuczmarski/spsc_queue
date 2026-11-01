#include <gtest/gtest.h>

#include <spsc/queue.hpp>

#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>

TEST(QueueTest, NewQueueIsEmpty) {
    kuczmars::SPSCQueue<int> q(5);
    EXPECT_EQ(q.capacity(), 5u);
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.front(), nullptr);
    EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(QueueTest, ConstructorRejectsInvalidCapacity) {
    EXPECT_THROW(kuczmars::SPSCQueue<int>{0}, std::invalid_argument);
    EXPECT_THROW(kuczmars::SPSCQueue<int>{std::numeric_limits<std::size_t>::max()}, std::length_error);}

TEST(QueueTest, CapacityIsNotImplicitlyConvertible) {
    static_assert(std::is_constructible_v<kuczmars::SPSCQueue<int>, std::size_t>);
    static_assert(!std::is_convertible_v<std::size_t, kuczmars::SPSCQueue<int>>);
    static_assert(!std::is_default_constructible_v<kuczmars::SPSCQueue<int>>);
}

TEST(QueueTest, FrontPeeksOldestAndPopRemovesIt) {
    kuczmars::SPSCQueue<int> q(3);
    ASSERT_TRUE(q.try_push(1));
    ASSERT_TRUE(q.try_push(2));

    ASSERT_NE(q.front(), nullptr);
    EXPECT_EQ(*q.front(), 1);
    EXPECT_EQ(q.size(), 2u);

    q.pop();
    ASSERT_NE(q.front(), nullptr);
    EXPECT_EQ(*q.front(), 2);

    q.pop();
    EXPECT_EQ(q.front(), nullptr);
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, FrontAllowsInPlaceModification) {
    kuczmars::SPSCQueue<std::string> q(2);
    ASSERT_TRUE(q.try_push("abc"));
    *q.front() += "def";
    EXPECT_EQ(q.try_pop(), "abcdef");
}

TEST(QueueTest, MoveOnlyType) {
    kuczmars::SPSCQueue<std::unique_ptr<int>> q(3);
    auto p = std::make_unique<int>(7);
    ASSERT_TRUE(q.try_push(std::move(p)));
    EXPECT_EQ(p, nullptr);
    ASSERT_TRUE(q.try_emplace(new int(8)));

    auto first = q.try_pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(**first, 7);

    ASSERT_NE(q.front(), nullptr);
    EXPECT_EQ(**q.front(), 8);
    q.pop();
    EXPECT_TRUE(q.empty());
}

namespace {

struct NonMovable {
    NonMovable(int a, int b) : sum(a + b) {}
    NonMovable(const NonMovable&) = delete;
    NonMovable& operator=(const NonMovable&) = delete;
    int sum;
};

struct alignas(64) OverAligned {
    int value;
};

struct ThrowingCopy {
    static inline bool throw_on_copy = false;
    explicit ThrowingCopy(int v) : value(v) {}
    ThrowingCopy(const ThrowingCopy& other) : value(other.value) {
        if (throw_on_copy) {
            throw std::runtime_error("copy failed");
        }
    }
    int value;
};

struct ThrowingCopyNothrowMove {
    ThrowingCopyNothrowMove() = default;
    ThrowingCopyNothrowMove(const ThrowingCopyNothrowMove&) noexcept(false) {}
    ThrowingCopyNothrowMove(ThrowingCopyNothrowMove&&) noexcept = default;
};

template <typename Q>
constexpr bool has_try_pop = requires(Q& q) { q.try_pop(); };

template <typename Q>
constexpr bool has_front_and_pop = requires(Q& q) {
    q.front();
    q.pop();
};

} // namespace

TEST(QueueTest, EmplaceConstructsInPlace) {
    kuczmars::SPSCQueue<NonMovable> q(2);
    ASSERT_TRUE(q.try_emplace(2, 3));
    ASSERT_NE(q.front(), nullptr);
    EXPECT_EQ(q.front()->sum, 5);
    q.pop();
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, OverAlignedElementsAreAligned) {
    kuczmars::SPSCQueue<OverAligned> q(5);
    for (int lap = 0; lap < 3; ++lap) {
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(q.try_push(OverAligned{i}));
        }
        for (int i = 0; i < 5; ++i) {
            const auto addr = reinterpret_cast<std::uintptr_t>(q.front());
            EXPECT_EQ(addr % alignof(OverAligned), 0u);
            EXPECT_EQ(q.front()->value, i);
            q.pop();
        }
    }
}

TEST(QueueTest, NoexceptFollowsElementType) {
    using IntQueue = kuczmars::SPSCQueue<int>;
    using ThrowingQueue = kuczmars::SPSCQueue<ThrowingCopy>;
    static_assert(noexcept(std::declval<IntQueue&>().try_push(1)));
    static_assert(noexcept(std::declval<IntQueue&>().try_pop()));
    static_assert(!noexcept(std::declval<ThrowingQueue&>().try_push(std::declval<const ThrowingCopy&>())));
    static_assert(!noexcept(std::declval<ThrowingQueue&>().try_emplace(std::declval<const ThrowingCopy&>())));
    // ThrowingCopy has no move constructor, so moving falls back to the throwing copy.
    static_assert(!noexcept(std::declval<ThrowingQueue&>().try_push(std::declval<ThrowingCopy&&>())));

    using MixedQueue = kuczmars::SPSCQueue<ThrowingCopyNothrowMove>;
    static_assert(noexcept(std::declval<MixedQueue&>().try_push(std::declval<ThrowingCopyNothrowMove&&>())));
    static_assert(!noexcept(std::declval<MixedQueue&>().try_push(std::declval<const ThrowingCopyNothrowMove&>())));
    static_assert(noexcept(std::declval<MixedQueue&>().try_emplace()));
}

TEST(QueueTest, ConsumerOperationsAreNoexcept) {
    using ThrowingQueue = kuczmars::SPSCQueue<ThrowingCopy>;
    static_assert(noexcept(std::declval<ThrowingQueue&>().front()));
    static_assert(noexcept(std::declval<ThrowingQueue&>().pop()));
    static_assert(noexcept(std::declval<const ThrowingQueue&>().capacity()));
    static_assert(noexcept(std::declval<const ThrowingQueue&>().size()));
    static_assert(noexcept(std::declval<const ThrowingQueue&>().empty()));
}

TEST(QueueTest, TryPopRequiresNothrowMove) {
    static_assert(has_try_pop<kuczmars::SPSCQueue<int>>);
    static_assert(has_try_pop<kuczmars::SPSCQueue<std::unique_ptr<int>>>);
    static_assert(has_try_pop<kuczmars::SPSCQueue<ThrowingCopyNothrowMove>>);
    static_assert(!has_try_pop<kuczmars::SPSCQueue<ThrowingCopy>>);
    static_assert(!has_try_pop<kuczmars::SPSCQueue<NonMovable>>);

    static_assert(has_front_and_pop<kuczmars::SPSCQueue<ThrowingCopy>>);
    static_assert(has_front_and_pop<kuczmars::SPSCQueue<NonMovable>>);
}

TEST(QueueTest, FailedPushLeavesRvalueSourceIntact) {
    kuczmars::SPSCQueue<std::unique_ptr<int>> q(1);
    ASSERT_TRUE(q.try_push(std::make_unique<int>(1)));

    auto p = std::make_unique<int>(2);
    EXPECT_FALSE(q.try_push(std::move(p)));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 2);

    EXPECT_FALSE(q.try_emplace(std::move(p)));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 2);

    ASSERT_TRUE(q.try_pop().has_value());
    ASSERT_TRUE(q.try_push(std::move(p)));
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(**q.front(), 2);
}

TEST(QueueTest, FailedPushLeavesCopySourceIntact) {
    kuczmars::SPSCQueue<std::string> q(1);
    ASSERT_TRUE(q.try_push("first"));

    const std::string s = "second-long-enough-to-be-heap-allocated";
    EXPECT_FALSE(q.try_push(s));
    EXPECT_EQ(s, "second-long-enough-to-be-heap-allocated");
    EXPECT_EQ(*q.front(), "first");
}

TEST(QueueTest, ThrowingPushLeavesQueueUnchanged) {
    kuczmars::SPSCQueue<ThrowingCopy> q(2);
    const ThrowingCopy one{1};
    const ThrowingCopy two{2};
    ASSERT_TRUE(q.try_push(one));

    ThrowingCopy::throw_on_copy = true;
    EXPECT_THROW(ASSERT_FALSE(q.try_push(two)), std::runtime_error);
    ThrowingCopy::throw_on_copy = false;

    EXPECT_EQ(q.size(), 1u);
    ASSERT_TRUE(q.try_push(two));
    EXPECT_EQ(q.front()->value, 1);
    q.pop();
    EXPECT_EQ(q.front()->value, 2);
    q.pop();
    EXPECT_TRUE(q.empty());
}

class QueueCapacityTest : public ::testing::TestWithParam<std::size_t> {};

INSTANTIATE_TEST_SUITE_P(Capacities, QueueCapacityTest,
                         ::testing::Values(std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7},
                                           std::size_t{16}, std::size_t{1000}));

TEST_P(QueueCapacityTest, HoldsExactlyCapacityElements) {
    const std::size_t cap = GetParam();
    kuczmars::SPSCQueue<int> q(cap);
    EXPECT_EQ(q.capacity(), cap);

    for (std::size_t i = 0; i < cap; ++i) {
        ASSERT_TRUE(q.try_push(static_cast<int>(i))) << "push " << i;
        EXPECT_EQ(q.size(), i + 1);
    }
    EXPECT_FALSE(q.try_push(-1));
    EXPECT_FALSE(q.try_emplace(-1));
    EXPECT_EQ(q.size(), cap);

    EXPECT_EQ(q.try_pop(), 0);
    EXPECT_TRUE(q.try_push(-1));
    EXPECT_FALSE(q.try_push(-2));
}

TEST_P(QueueCapacityTest, PreservesOrderAcrossManyWraparounds) {
    const std::size_t cap = GetParam();
    kuczmars::SPSCQueue<std::size_t> q(cap);

    std::size_t next_push = 0;
    std::size_t next_pop = 0;
    for (std::size_t lap = 0; lap < 5; ++lap) {
        while (q.try_push(next_push)) {
            ++next_push;
        }
        ASSERT_EQ(q.size(), cap);
        while (auto v = q.try_pop()) {
            ASSERT_EQ(*v, next_pop++);
        }
        ASSERT_TRUE(q.empty());
    }
    EXPECT_EQ(next_push, 5 * cap);
    EXPECT_EQ(next_pop, 5 * cap);
}

TEST_P(QueueCapacityTest, MatchesStdDequeOnRandomOperations) {
    const std::size_t cap = GetParam();
    kuczmars::SPSCQueue<int> q(cap);
    std::deque<int> model;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> op(0, 5);

    for (int i = 0; i < 20'000; ++i) {
        switch (op(rng)) {
            case 0:
            case 1:
            case 4: {
                const bool pushed = q.try_push(i);
                ASSERT_EQ(pushed, model.size() < cap) << "step " << i;
                if (pushed) {
                    model.push_back(i);
                }
                break;
            }
            case 2: {
                const auto v = q.try_pop();
                if (model.empty()) {
                    ASSERT_EQ(v, std::nullopt) << "step " << i;
                } else {
                    ASSERT_EQ(v, model.front()) << "step " << i;
                    model.pop_front();
                }
                break;
            }
            case 3: {
                const int* f = q.front();
                if (model.empty()) {
                    ASSERT_EQ(f, nullptr) << "step " << i;
                } else {
                    ASSERT_NE(f, nullptr) << "step " << i;
                    ASSERT_EQ(*f, model.front()) << "step " << i;
                    q.pop();
                    model.pop_front();
                }
                break;
            }
            case 5: {
                // pop() without a preceding front() must keep the consumer cache consistent.
                if (!model.empty()) {
                    q.pop();
                    model.pop_front();
                }
                break;
            }
        }
        ASSERT_EQ(q.size(), model.size()) << "step " << i;
        ASSERT_EQ(q.empty(), model.empty()) << "step " << i;
    }
}