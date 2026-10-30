#include <gtest/gtest.h>

#include <spsc/queue.hpp>

#include <cstdint>

namespace {

struct Tracked {
    static constexpr std::uint32_t live_tag = 0x11fe11fe;
    static constexpr std::uint32_t dead_tag = 0xdeadbeef;
    static inline int alive = 0;

    explicit Tracked(int v) : value(v) { ++alive; }
    Tracked(const Tracked& other) : value(other.value) { ++alive; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++alive; }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&) noexcept = default;
    // Catches destroying a slot that holds no live object (never constructed or already destroyed).
    ~Tracked() {
        EXPECT_EQ(tag, live_tag) << "destroying an object that is not alive";
        tag = dead_tag;
        --alive;
    }

    int value;
    std::uint32_t tag = live_tag;
};

class QueueLifetimeTest : public ::testing::Test {
protected:
    void SetUp() override { Tracked::alive = 0; }
    void TearDown() override { EXPECT_EQ(Tracked::alive, 0); }
};

} // namespace

TEST_F(QueueLifetimeTest, EmptyQueueConstructsNoElements) {
    kuczmars::SPSCQueue<Tracked, 8> q;
    EXPECT_EQ(Tracked::alive, 0);
}

TEST_F(QueueLifetimeTest, PushConstructsExactlyOneElement) {
    kuczmars::SPSCQueue<Tracked, 4> q;
    ASSERT_TRUE(q.try_emplace(1));
    EXPECT_EQ(Tracked::alive, 1);

    const Tracked t{2};
    ASSERT_TRUE(q.try_push(t));
    EXPECT_EQ(Tracked::alive, 3);
}

TEST_F(QueueLifetimeTest, RejectedPushConstructsNothing) {
    kuczmars::SPSCQueue<Tracked, 1> q;
    ASSERT_TRUE(q.try_emplace(1));
    ASSERT_FALSE(q.try_emplace(2));
    EXPECT_EQ(Tracked::alive, 1);
}

TEST_F(QueueLifetimeTest, TryPopDestroysSlotElement) {
    kuczmars::SPSCQueue<Tracked, 4> q;
    ASSERT_TRUE(q.try_emplace(1));
    {
        auto v = q.try_pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(v->value, 1);
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST_F(QueueLifetimeTest, PopDestroysFrontElement) {
    kuczmars::SPSCQueue<Tracked, 4> q;
    ASSERT_TRUE(q.try_emplace(1));
    ASSERT_TRUE(q.try_emplace(2));
    q.pop();
    EXPECT_EQ(Tracked::alive, 1);
    EXPECT_EQ(q.front()->value, 2);
    q.pop();
    EXPECT_EQ(Tracked::alive, 0);
}

TEST_F(QueueLifetimeTest, DestructorDestroysRemainingElements) {
    {
        kuczmars::SPSCQueue<Tracked, 4> q;
        ASSERT_TRUE(q.try_emplace(1));
        ASSERT_TRUE(q.try_emplace(2));
        ASSERT_TRUE(q.try_emplace(3));
        EXPECT_EQ(Tracked::alive, 3);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST_F(QueueLifetimeTest, DestructorDestroysRemainingElementsAfterWraparound) {
    {
        kuczmars::SPSCQueue<Tracked, 3> q;
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(q.try_emplace(i));
        }
        for (int i = 0; i < 3; ++i) {
            q.pop();
        }
        // Remaining elements now span the end of the buffer and wrap to its start.
        ASSERT_TRUE(q.try_emplace(10));
        ASSERT_TRUE(q.try_emplace(11));
        ASSERT_TRUE(q.try_emplace(12));
        EXPECT_EQ(Tracked::alive, 3);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST_F(QueueLifetimeTest, ManyLapsKeepElementCountBalanced) {
    kuczmars::SPSCQueue<Tracked, 5> q;
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(q.try_emplace(i));
        if (i % 3 != 0) {
            ASSERT_TRUE(q.try_pop().has_value());
        }
        if (q.size() == q.capacity()) {
            q.pop();
        }
        EXPECT_EQ(static_cast<std::size_t>(Tracked::alive), q.size());
    }
}