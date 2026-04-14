#include <gtest/gtest.h>

extern "C" {
    #include "task.h"
}


class TimerTest;


struct MyTimer {
    timerjob_t base;
    int        id;
    TimerTest *test;
    int        fire_at = -1;
};

struct TimerTest : public ::testing::Test {
    int current_tick = 0;
    MyTimer m_timers[10];

    void SetUp() override;
    void TearDown() override;

    void start(int id, int tick);
    void forward();
    void fire(int id);
};

void timer_fire(timerjob_t *self) {
    MyTimer *timer = (MyTimer*)self;
    timer->test->fire(timer->id);
    // std::cout << "timer-" << timer->id << " fire" << std::endl;
}

void TimerTest::SetUp() {
    timer_init();
}

void TimerTest::TearDown() {}

void TimerTest::start(int id, int tick) {
    m_timers[id].base.func = timer_fire;
    m_timers[id].test = this;
    m_timers[id].id = id;
    timer_start(&m_timers[id].base, tick);
}

void TimerTest::forward() {
    ++current_tick;
    timer_forward();
}

void TimerTest::fire(int id) {
    m_timers[id].fire_at = current_tick;
}


// 按一定顺序准备定时器，模拟时间流逝
TEST_F(TimerTest, Defer) {
    start(1, 1);
    start(2, 2);
    start(3, 3);

    forward();
    forward();
    forward();

    EXPECT_EQ(m_timers[1].fire_at, 1);
    EXPECT_EQ(m_timers[2].fire_at, 2);
    EXPECT_EQ(m_timers[3].fire_at, 3);
}
