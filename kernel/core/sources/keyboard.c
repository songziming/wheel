#include <keyboard.h>
#include <debug.h>
#include <wheel.h>
#include <fifo.h>


// 提供一个共享管道，键盘驱动向管道写入 keycode
// tty、GUI 等线程不断从管道读取 keycode

// 创建一个虚拟文件 /dev/keyboard，任何进程都可以读写
// 对 /dev/keyboard 文件的读写是线性的，自动序列化

static keycode_t g_kbd_buff[KEYBOARD_BUFF_LEN];
static fifo_t g_kbd_fifo = FIFO_INIT;



INIT_TEXT void keyboard_init() {
    fifo_init(&g_kbd_fifo, g_kbd_buff, sizeof(g_kbd_buff));
}


// 由键盘驱动在中断里调用
// keycode 放入队列，并唤醒阻塞的任务
void keyboard_send(keycode_t key) {
    if (KEY_RELEASE & key) {
        klog("<up:%x>", key & ~KEY_RELEASE);
    } else {
        klog("<down:%x>", key);
    }
}
