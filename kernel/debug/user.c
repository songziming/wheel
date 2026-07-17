#include <wheel.h>
#include <task.h>
#include <proc.h>
#include <elf.h>

#include <kstring.h>
#include <debug.h>
#include <kshell.h>
#include <console.h>
#include <tar.h>


// embedded user programs tar
extern char _binary_users_tar_start;
extern char _binary_users_tar_end;


// 切换到进程的地址空间，开始执行 ring3 代码
// 跳入 ring3 之后，内核栈仍然
void user_task(proc_t *pid) {
    task_enter_process(pid);
    proc_drop(pid); // 只剩下当前一个线程持有引用

    // task_t *self = current_task();
    arch_enter_ring3(pid->entry, pid->ustack.vend);
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
        task_leave_process();
        proc_drop(pid);
        return 0;
    }
    console_printf("ELF loaded, entry point: 0x%zx\n", entry);
    pid->entry = entry;

    // 分配用户栈，可以分配多个栈
    vmspace_alloc(&pid->vm, &pid->ustack, PAGE_SIZE, PT_STACK, MMU_WRITE|MMU_USER);
    task_leave_process();

    // 创建一个新线程，入口为 entry，使用 pid
    logk("starting user program\n");
    task_t *tuser = task_make("ring3", 10, user_task, pid);
    kobj_keep(tuser);
    task_start_now(tuser);
    task_join_and_drop(tuser);
    logk("user program stopped\n");
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
