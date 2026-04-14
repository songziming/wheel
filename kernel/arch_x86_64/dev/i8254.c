#include "i8254.h"


// Intel 8254
// 晶振频率 1.193181666 MHz，方波
// 三个输出通道，拥有各自的 devider，每次晶振寄存器减一
// 减小到零产生一次输出，这样三个 channel 可以有自己的频率
// channel 0 连接到 IRQ-0
// channel 1 未使用
// channel 2 连接到蜂鸣器

// 支持多种输出模式，例如 pulse、square-wave、singleshot
// pulse 模式下
// 特别是蜂鸣器，更适合方波而不是 pulse
// mode 0/1，singleshot
// mode 2，输出 pulse，每次 pulse 只保持一个输入周期
// mode 3，输出方波，每次时钟不是输出 pulse，而是输出电平翻转。
//         这会让有效输出频率减半，所以 mode 3 每次收到晶振，计数器减 2

// channel 2 可以通过 port 0x61 bit 0 控制输入是否连接到晶振
// 通过 port 0x61 bit 5 可以读取 channel 2 的输出
// （port 0x61 属于键盘控制器，但承担了蜂鸣器控制功能）

// channel 2 连接到蜂鸣器，但我们不希望出声音
// 蜂鸣器出声需要按固定频率翻转 port 0x61 bit 1
// 只要不碰这个 bit，就不会出声音

#define CH0_DATA    0x40
#define CH1_DATA    0x41
#define CH2_DATA    0x42
#define MODE_CMD    0x43    // write only


INIT_TEXT void i8254_init() {
    //
}

INIT_TEXT void i8254_disable() {
    //
}
