#include <wheel.h>
#include <task.h>
#include <proc.h>
#include <elf.h>

#include <kstring.h>
#include <debug.h>
#include <kshell.h>
#include <console.h>
#include <tar.h>


//------------------------------------------------------------------------------
// 测试用户模式
//------------------------------------------------------------------------------

// embedded user programs tar
extern char _binary_users_tar_start;
extern char _binary_users_tar_end;

// 进程使用的段，应该记录在 process 里面
static vmrange_t g_user_code;

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

    // size_t stack = (size_t)vmspace_alloc(&g_kernel_vm, &tid->user_stack,
    //     PAGE_SIZE, PT_STACK, MMU_WRITE|MMU_USER);
    // logk("ring3 code 0x%zx~0x%zx, len=0x%zx\n", g_user_code.vaddr, g_user_code.vend, len);
    // logk("ring3 stack 0x%zx~0x%zx\n", tid->user_stack.vaddr, tid->user_stack.vend);
    // g_user_code.desc = "ring3 code&data";
    // tid->user_stack.desc = "ring3 stack";

    // 将 flat binary 的代码和数据拷贝到目标地址
    size_t code3 = g_user_code.vaddr;
    kmemcpy((char*)code3, from, len);

    // arch_enter_ring3(code3, stack + PAGE_SIZE);
}


//------------------------------------------------------------------------------
// 解析 tar，其中包含用户态程序镜像
//------------------------------------------------------------------------------

static int tar_item(const char *name, const char *data, size_t len, void *user) {
    (void)user;
    console_printf("tar-entry name=`%s`, size=%zu, ptr=%p\n", name, len, data);
    return 1;
}

void test_tar() {
    size_t tar_size = (size_t)(&_binary_users_tar_end - &_binary_users_tar_start);
    tar_iterate(&_binary_users_tar_start, tar_size, tar_item, NULL);
}

static int tar_start_app(const char *name, const char *data, size_t len, void *user) {
    if (kstrcmp((const char *)user, name)) {
        return 1;
    }

    // 找到了user app，解析elf，创建任务
    console_printf("found user program %s at %p, size %zu\n", name, data, len);
    console_printf("file header is '%.4s'\n", data);

    proc_t *pid = proc_make(name);
    if (NULL == pid) {
        console_printf("error: cannot create process\n");
        return 0;
    }

    // 当前处于 shell task，临时切换到新进程的地址空间
    // 目的是加载 ELF，完成后会离开这个地址空间
    // 加载时，segment 需要允许 write，拷贝完成改为 readonly
    // 我们不需要使用该地址空间，所以 remap 之后不用 invlpg
    task_enter_process(pid);

    // 解析 data 指向的 ELF 文件，加载到进程地址空间
    size_t entry = elf_load(pid, data, len);
    if (0 == entry) {
        console_printf("error: failed to load ELF\n");
        // 加载失败，退出前需要清理
        task_leave_process();
        return 0;
    }
    console_printf("ELF loaded, entry point: 0x%zx\n", entry);

    // 回到内核页表
    task_leave_process();

    // TODO 创建一个新线程，入口为 entry，使用 pid

    return 0;
}

// 本函数在 kshell 线程里运行
// 创建一个新任务，在新任务里面运行用户态代码
void test_user(int argc, char *argv[]) {
    if (argc < 2) {
        console_printf("usage: %s USER_PROG_NAME\n", argv[0]);
        return;
    }

    size_t tar_size = (size_t)(&_binary_users_tar_end - &_binary_users_tar_start);
    tar_iterate(&_binary_users_tar_start, tar_size, tar_start_app, argv[1]);
}

KSHELL_CMD("tar", test_tar);
KSHELL_CMD("user", test_user);
