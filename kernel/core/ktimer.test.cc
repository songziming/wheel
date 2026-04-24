#include <gtest/gtest.h>

extern "C" {
    #include "ktimer.h"
}


class TimerTest;


struct MyTimer {
    ktimer_t   base;
    TimerTest *test;
    int        id;
    int        fire_at;
};

struct TimerTest : public ::testing::Test {
    int current_tick = 0;
    MyTimer m_timers[10];

    void SetUp() override;
    void TearDown() override;

    void start(int id, int tick);
    void cancel(int id);
    void forward();
    static void timer_fire(ktimer_t *self);
};

void TimerTest::SetUp() {
    timer_init();
    for (int i = 0; i < 10; ++i) {
        // m_timers[i].base.func = timer_fire;
        m_timers[i].test = this;
        m_timers[i].id = i;
        m_timers[i].fire_at = -1;
    }
}

void TimerTest::TearDown() {}

void TimerTest::start(int id, int tick) {
    timer_start(&m_timers[id].base, timer_fire, tick);
}

void TimerTest::cancel(int id) {
    timer_cancel(&m_timers[id].base);
}

void TimerTest::forward() {
    timer_process();
    ++current_tick;
}

void TimerTest::timer_fire(ktimer_t *self) {
    MyTimer *timer = (MyTimer*)self;
    // std::cout << "timer-" << timer->id << " fires at tick-" << timer->test->current_tick << std::endl;
    timer->fire_at = timer->test->current_tick;
}


// 按一定顺序准备定时器，模拟时间流逝
TEST_F(TimerTest, Defer) {
    start(0, 0);
    start(1, 1);
    start(2, 2);
    start(3, 3);

    forward();
    forward();
    forward();
    EXPECT_EQ(m_timers[0].fire_at, 0);
    EXPECT_EQ(m_timers[1].fire_at, 1);
    EXPECT_EQ(m_timers[2].fire_at, 2);
    EXPECT_EQ(m_timers[3].fire_at, -1);

    forward();
    EXPECT_EQ(m_timers[3].fire_at, 3);
}

TEST_F(TimerTest, ZeroTick) {
    start(1, 1);
    start(2, 0);

    forward();
    EXPECT_EQ(m_timers[2].fire_at, 0);
    EXPECT_EQ(m_timers[1].fire_at, -1);

    forward();
    EXPECT_EQ(m_timers[1].fire_at, 1);
}

TEST_F(TimerTest, Reverse) {
    start(3, 3);
    start(2, 2);
    start(1, 1);

    forward();
    forward();
    forward();
    forward();

    EXPECT_EQ(m_timers[1].fire_at, 1);
    EXPECT_EQ(m_timers[2].fire_at, 2);
    EXPECT_EQ(m_timers[3].fire_at, 3);
}

TEST_F(TimerTest, SameTime) {
    start(1, 1);
    start(2, 2);
    start(3, 2);
    start(4, 2);
    start(5, 3);
    start(6, 3);

    forward();
    forward();
    EXPECT_EQ(m_timers[1].fire_at, 1);

    forward();
    EXPECT_EQ(m_timers[2].fire_at, 2);
    EXPECT_EQ(m_timers[3].fire_at, 2);
    EXPECT_EQ(m_timers[4].fire_at, 2);

    forward();
    EXPECT_EQ(m_timers[5].fire_at, 3);
    EXPECT_EQ(m_timers[6].fire_at, 3);

    forward();
    forward();
}

TEST_F(TimerTest, OneByOne) {
    start(0, 0);
    forward();
    start(1, 0);
    forward();
    start(2, 0);
    forward();
    start(3, 0);
    forward();

    EXPECT_EQ(m_timers[0].fire_at, 0);
    EXPECT_EQ(m_timers[1].fire_at, 1);
    EXPECT_EQ(m_timers[2].fire_at, 2);
    EXPECT_EQ(m_timers[3].fire_at, 3);
}

// 取消队列中间的 timer
TEST_F(TimerTest, CancelMid) {
    start(1, 1);
    start(2, 2);
    start(3, 3);
    start(4, 4);

    forward();
    forward();
    EXPECT_EQ(m_timers[1].fire_at, 1);

    cancel(3);

    forward();
    EXPECT_EQ(m_timers[2].fire_at, 2);

    forward();
    forward();
    EXPECT_EQ(m_timers[4].fire_at, 4);
}


static ktimer_t t1;
static ktimer_t t2;
static ktimer_t t3;
static bool state1 = false;
static bool state2 = false;
static bool state3 = false;

static void timer3_fire(ktimer_t *tmr) {
    state3 = true;
}
static void timer2_fire(ktimer_t *tmr) {
    state2 = true;
    timer_start(&t3, timer3_fire, 0);
}
static void timer1_fire(ktimer_t *tmr) {
    state1 = true;
    timer_start(&t2, timer2_fire, 0);
}

// 在 timer 触发函数里注册另一个 timer
TEST_F(TimerTest, NewTimer) {
    timer_start(&t1, timer1_fire, 0);

    forward();
    EXPECT_TRUE(state1);
    EXPECT_FALSE(state2);
    EXPECT_FALSE(state3);

    forward();
    EXPECT_TRUE(state2);
    EXPECT_FALSE(state3);

    forward();
    EXPECT_TRUE(state3);
}

int repeat_val = 0;
static void repeat_func(ktimer_t *tmr) {
    ++repeat_val;
    if (repeat_val < 5) {
        timer_start(&t1, repeat_func, 0);
    }
}

// 在 timer 触发函数里重复注册自己
TEST_F(TimerTest, RepeatSelf) {
    repeat_val = 0;
    timer_start(&t1, repeat_func, 0);

    forward();
    EXPECT_EQ(repeat_val, 1);
    forward();
    EXPECT_EQ(repeat_val, 2);
    forward();
    EXPECT_EQ(repeat_val, 3);
    forward();
    EXPECT_EQ(repeat_val, 4);
    forward();
    EXPECT_EQ(repeat_val, 5);
}
