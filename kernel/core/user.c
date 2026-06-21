#include <wheel.h>
#include <task.h>

#include <kstring.h>
#include <debug.h>
#include <kshell.h>
#include <console.h>
#include <tar.h>


// 用户态支持


//------------------------------------------------------------------------------
// 系统调用
//------------------------------------------------------------------------------

size_t do_syscall(int id, size_t a1, size_t a2, size_t a3, size_t a4) {
    console_printf("handling syscall #%d\n", id);

    if (123 == id) {
        console_printf("print: `%s`\n", (char*)a1);
    }

    if (0 == id) {
        task_exit();
    }

    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;

    return id + 1;
}

//------------------------------------------------------------------------------
// 解析 tar，其中包含用户态程序镜像
//------------------------------------------------------------------------------

extern char _binary_users_tar_start;
extern char _binary_users_tar_end;

void test_tar() {
    size_t tar_size = (size_t)(&_binary_users_tar_end - &_binary_users_tar_start);
    tar_iterate(&_binary_users_tar_start, tar_size);
}

KSHELL_CMD("tar", test_tar);


//------------------------------------------------------------------------------
// 测试用户模式
//------------------------------------------------------------------------------

// 进程使用的段，应该记录在 process 里面
static vmrange_t g_user_code;

// extern char _binary_hello3_bin_start;
// extern char _binary_hello3_bin_end;

// 用来运行用户态代码的任务
static task_t tcb_user;

// 为当前任务分配用户栈，启动用户态
void user_kernel_task() {
    console_printf("user task running in kernel\n");

    task_t *tid = current_task();
    // TODO 检查这个任务是否已经分配用户栈（可以使用 flags）

    char *from = &_binary_users_tar_start;
    size_t len = (size_t)(&_binary_users_tar_end - from);

    size_t paged_size = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vmspace_alloc_at(&g_kernel_vm, &g_user_code, 0x400000,
        paged_size, PT_KERNEL, MMU_WRITE|MMU_EXEC|MMU_USER);

    size_t stack = (size_t)vmspace_alloc(&g_kernel_vm, &tid->user_stack,
        PAGE_SIZE, PT_STACK, MMU_WRITE|MMU_USER);

    logk("ring3 code 0x%zx~0x%zx, len=0x%zx\n", g_user_code.vaddr, g_user_code.vend, len);
    logk("ring3 stack 0x%zx~0x%zx\n", tid->user_stack.vaddr, tid->user_stack.vend);
    g_user_code.desc = "ring3 code&data";
    tid->user_stack.desc = "ring3 stack";

    // 将 flat binary 的代码和数据拷贝到目标地址
    size_t code3 = g_user_code.vaddr;
    kmemcpy((char*)code3, from, len);

    arch_enter_ring3(code3, stack + PAGE_SIZE);
}


// 本函数在 kshell 线程里运行
// 创建一个新任务，在新任务里面运行用户态代码
void test_user() {
    task_create(&tcb_user, "user", 10, user_kernel_task);
    task_start_now(&tcb_user);

    while (tcb_user.state != TS_DELETED) {
        cpu_pause();
    }
    console_printf("user task deleted!\n");
}

KSHELL_CMD("user", test_user);
