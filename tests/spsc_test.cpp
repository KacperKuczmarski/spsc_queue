#include <gtest/gtest.h>

#include <spsc/queue.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

TEST(QueueTest, ReportsCapacity) {
    kuczmars::SPSCQueue<int, 16> q;
    EXPECT_EQ(q.capacity(), 16u);
    q.try_push(42);
    q.try_push(6767);
    q.try_push(3344455);

    EXPECT_EQ(q.size(), 3u);

    EXPECT_EQ(q.try_pop(), 42);
    EXPECT_EQ(q.try_pop(), 6767);
    EXPECT_EQ(q.try_pop(), 3344455);

    EXPECT_EQ(q.size(), 0u);
}

TEST(QueueTest, ComplexType) {
    kuczmars::SPSCQueue<std::vector<int>, 16> q;
    std::vector<int> push_val{1,2,3,4,5,6,7,8,9};
    push_val.reserve(1000);
    q.try_push(push_val);
    q.try_push(push_val);
    q.try_push(push_val);

    EXPECT_EQ(q.size(), 3u);

    EXPECT_EQ(q.try_pop(), push_val);

    EXPECT_EQ(q.size(), 2u);
}

TEST(QueueTest, NewQueueIsEmpty) {
    kuczmars::SPSCQueue<int, 5> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.front(), nullptr);
    EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(QueueTest, FrontPeeksOldestAndPopRemovesIt) {
    kuczmars::SPSCQueue<int, 3> q;
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
    kuczmars::SPSCQueue<std::string, 2> q;
    ASSERT_TRUE(q.try_push("abc"));
    *q.front() += "def";
    EXPECT_EQ(q.try_pop(), "abcdef");
}

TEST(QueueTest, MoveOnlyType) {
    kuczmars::SPSCQueue<std::unique_ptr<int>, 3> q;
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

} // namespace

TEST(QueueTest, EmplaceConstructsInPlace) {
    kuczmars::SPSCQueue<NonMovable, 2> q;
    ASSERT_TRUE(q.try_emplace(2, 3));
    ASSERT_NE(q.front(), nullptr);
    EXPECT_EQ(q.front()->sum, 5);
    q.pop();
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, OverAlignedElementsAreAligned) {
    kuczmars::SPSCQueue<OverAligned, 5> q;
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
    using IntQueue = kuczmars::SPSCQueue<int, 2>;
    using ThrowingQueue = kuczmars::SPSCQueue<ThrowingCopy, 2>;
    static_assert(noexcept(std::declval<IntQueue&>().try_push(1)));
    static_assert(noexcept(std::declval<IntQueue&>().try_pop()));
    static_assert(!noexcept(std::declval<ThrowingQueue&>().try_push(std::declval<const ThrowingCopy&>())));
    static_assert(!noexcept(std::declval<ThrowingQueue&>().try_emplace(std::declval<const ThrowingCopy&>())));
}

TEST(QueueTest, ThrowingPushLeavesQueueUnchanged) {
    kuczmars::SPSCQueue<ThrowingCopy, 2> q;
    const ThrowingCopy one{1};
    const ThrowingCopy two{2};
    ASSERT_TRUE(q.try_push(one));

    ThrowingCopy::throw_on_copy = true;
    EXPECT_THROW(q.try_push(two), std::runtime_error);
    ThrowingCopy::throw_on_copy = false;

    EXPECT_EQ(q.size(), 1u);
    ASSERT_TRUE(q.try_push(two));
    EXPECT_EQ(q.front()->value, 1);
    q.pop();
    EXPECT_EQ(q.front()->value, 2);
    q.pop();
    EXPECT_TRUE(q.empty());
}

template <typename C>
class QueueCapacityTest : public ::testing::Test {};

using Capacities = ::testing::Types<
        std::integral_constant<std::size_t, 1>,
        std::integral_constant<std::size_t, 2>,
        std::integral_constant<std::size_t, 3>,
        std::integral_constant<std::size_t, 7>,
        std::integral_constant<std::size_t, 16>,
        std::integral_constant<std::size_t, 1000>>;
TYPED_TEST_SUITE(QueueCapacityTest, Capacities);

TYPED_TEST(QueueCapacityTest, HoldsExactlyCapacityElements) {
    constexpr std::size_t cap = TypeParam::value;
    kuczmars::SPSCQueue<int, cap> q;
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

TYPED_TEST(QueueCapacityTest, PreservesOrderAcrossManyWraparounds) {
    constexpr std::size_t cap = TypeParam::value;
    kuczmars::SPSCQueue<std::size_t, cap> q;

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

TYPED_TEST(QueueCapacityTest, MatchesStdDequeOnRandomOperations) {
    constexpr std::size_t cap = TypeParam::value;
    kuczmars::SPSCQueue<int, cap> q;
    std::deque<int> model;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> op(0, 3);

    for (int i = 0; i < 20'000; ++i) {
        switch (op(rng)) {
            case 0:
            case 1: {
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
        }
        ASSERT_EQ(q.size(), model.size()) << "step " << i;
        ASSERT_EQ(q.empty(), model.empty()) << "step " << i;
    }
}