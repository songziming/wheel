#include <arch_impl.h>
#include <wheel.h>


//------------------------------------------------------------------------------
// 辅助函数
//------------------------------------------------------------------------------

inline void cpu_halt() { __asm__("hlt"); }
inline void cpu_pause() { __asm__("pause"); }
inline void cpu_rfence() { __asm__("lfence" ::: "memory"); }
inline void cpu_wfence() { __asm__("sfence" ::: "memory"); }
inline void cpu_rwfence() { __asm__("mfence" ::: "memory"); }

// bochs magic breakpoint
void emu_break() {
    __asm__("xchgw %bx, %bx");
}

// 退出 QEMU/Bochs 模拟器并返回值
void emu_exit(int ret) {
#ifdef DEBUG
    __asm__("outl %0, %1" :: "a"(ret), "Nd"(0xf4));
#else
    (void)ret;
#endif
}



//------------------------------------------------------------------------------
// 启动阶段内存分配，不释放
//------------------------------------------------------------------------------

typedef struct membuff {
    uint8_t *ptr;
    size_t size;
    size_t used;
} membuff_t;

#define BUFF_ALIGN 16

static SECTION(".rotail") ALIGNED(BUFF_ALIGN) uint8_t g_ro_area[EARLY_RO_BUFF_SIZE];
static SECTION(".rwtail") ALIGNED(BUFF_ALIGN) uint8_t g_rw_area[EARLY_RW_BUFF_SIZE];
static INIT_DATA membuff_t g_ro_buff = { g_ro_area, EARLY_RO_BUFF_SIZE, 0 };
static INIT_DATA membuff_t g_rw_buff = { g_rw_area, EARLY_RW_BUFF_SIZE, 0 };

static INIT_TEXT void *membuff_grow(membuff_t *buff, size_t size) {
    if (buff->used + size >= buff->size) {
        return NULL;
    }
    size +=   BUFF_ALIGN - 1;
    size &= ~(BUFF_ALIGN - 1);
    uint8_t *p = &buff->ptr[buff->used];
    buff->used += size;
    return p;
}

INIT_TEXT void *early_alloc_ro(size_t size) {
    void *p = membuff_grow(&g_ro_buff, size);
    if (NULL == p) {
        klog("early ro alloc buffer overflow!\n");
        cpu_halt();
    }
    return p;
}

INIT_TEXT void *early_alloc_rw(size_t size) {
    void *p = membuff_grow(&g_rw_buff, size);
    if (NULL == p) {
        klog("early rw alloc buffer overflow!\n");
        cpu_halt();
    }
    return p;
}



//------------------------------------------------------------------------------
// 堆栈展开
//------------------------------------------------------------------------------

// AMD64 栈结构（向下生长）：
// high |     arg 8     |
//      |     arg 7     |
//      |  return addr  | <- 刚跳转之后的 RSP
//      |    old RBP    | <- RBP
//      |  local var x  |
//  low |  local var y  | <- RSP
int arch_unwind(size_t *addrs, int max, uint64_t rbp) {
    int i = 0;

    for (; i < max; ++i) {
        uint64_t *frame = (uint64_t *)rbp;
        addrs[i] = (size_t)frame[1];
        if (0 == addrs[i]) {
            break;
        }
        rbp = frame[0];
    }

    return i;
}

// 获取当前调用栈，返回深度
int unwind(size_t *addrs, int max) {
    uint64_t rbp;
    __asm__("movq %%rbp, %0" : "=r"(rbp));
    return arch_unwind(addrs, max, rbp);
}
