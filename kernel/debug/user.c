#include <wheel.h>
#include <task.h>
#include <proc.h>
#include <elf.h>

#include <heap.h>
#include <format.h>
#include <kstring.h>
#include <debug.h>
#include <kshell.h>
#include <console.h>
#include <tar.h>


// embedded user programs tar
// TODO tar 格式过于浪费，考虑改成 deflate
extern char _binary_users_tar_start;
extern char _binary_users_tar_end;


// 切换到进程的地址空间，开始执行 ring3 代码
// 跳入 ring3 之后，内核栈仍然
void user_task(proc_t *pid) {
    task_enter_process(pid);
    proc_drop(pid); // 只剩下当前一个线程持有引用

    task_t *self = current_task();
    logk("kernel stack range: %zx~%zx\n", self->stack.vaddr, self->stack.vend);

    // task_t *self = current_task();
    arch_enter_ring3(pid->entry, pid->ustack->vend);
}


static task_t *launch_user_task(const char *name, const char *data, size_t len) {
    // name = kernel_heap_mkstr("p-%s", name);
    proc_t *pid = proc_make(kernel_heap_mkstr("p-%s", name));
    if (NULL == pid) {
        logk("error: cannot create process\n");
        return NULL;
    }

    // 当前处于 shell task，临时切换到新进程的地址空间
    // 目的是加载 ELF，完成后会离开这个地址空间
    // 加载时，segment 需要允许 write，拷贝完成改为 readonly
    // 我们不需要使用该地址空间，所以 remap 之后不用 invlpg
    task_enter_process(pid); // refcnt=2

    // 解析 data 指向的 ELF 文件，加载到进程地址空间
    size_t entry = elf_load(pid, data, len);
    if (0 == entry) {
        logk("error: failed to load ELF\n");
        task_leave_process();
        proc_drop(pid);
        return NULL;
    }
    logk("ELF loaded, entry point: 0x%zx\n", entry);
    pid->entry = entry;

    // 分配用户栈，可以分配多个栈
    pid->ustack = proc_valloc(pid, 0, PAGE_SIZE, MMU_WRITE|MMU_USER);
    if (NULL == pid->ustack) {
        logk("error: failed to allocate user stack\n");
        task_leave_process();
        proc_drop(pid);
        return NULL;
    }
    pid->ustack->desc = "user stack";
    task_leave_process(); // refcnt=1

    // 创建一个新线程，入口为 entry，使用 pid
    logk("starting user program\n");
    task_t *tuser = task_make(kernel_heap_mkstr("t-%s", name), 10, user_task, pid);
    kobj_keep(tuser);
    task_start_now(tuser);
    return tuser;
}

//------------------------------------------------------------------------------
// 解析 tar，其中包含用户态程序镜像
//------------------------------------------------------------------------------

static int tar_item(const char *name, const char *data, size_t len, void *user) {
    (void)user;
    console_printf("tar-entry name=`%s`, size=%zu, ptr=%p\n", name, len, data);
    return 1;
}

void show_tar() {
    size_t tar_size = (size_t)(&_binary_users_tar_end - &_binary_users_tar_start);
    tar_iterate(&_binary_users_tar_start, tar_size, tar_item, NULL);
}

typedef struct tar_result {
    char filename[64];
    const char *data;
    size_t len;
} tar_result_t;

static int tar_find_cb(const char *name, const char *data, size_t len, void *user) {
    tar_result_t *res = (tar_result_t*)user;
    if (kstrcmp(res->filename, name)) {
        return 1;
    }

    res->data = data;
    res->len = len;
    return 0;
}

// 创建一个新任务，运行用户态代码，等待该进程结束
void run_user(int argc, char *argv[]) {
    if (argc < 2) {
        console_printf("usage: %s USER_PROG_NAME\n", argv[0]);
        return;
    }

    tar_result_t res;
    kmemset(&res, 0, sizeof(res));
    snprintk(res.filename, sizeof(res.filename), "%s.elf", argv[1]);
    size_t tar_size = (size_t)(&_binary_users_tar_end - &_binary_users_tar_start);
    tar_iterate(&_binary_users_tar_start, tar_size, tar_find_cb, &res);

    if (res.data && res.len) {
        task_t *utid = launch_user_task(res.filename, res.data, res.len);
        if (utid) {
            task_join_and_drop(utid);
        }
    }
}

// 创建一个新任务，后台运行用户态代码
void start_user(int argc, char *argv[]) {
    if (argc < 2) {
        console_printf("usage: %s USER_PROG_NAME\n", argv[0]);
        return;
    }

    tar_result_t res;
    kmemset(&res, 0, sizeof(res));
    snprintk(res.filename, sizeof(res.filename), "%s.elf", argv[1]);
    size_t tar_size = (size_t)(&_binary_users_tar_end - &_binary_users_tar_start);
    tar_iterate(&_binary_users_tar_start, tar_size, tar_find_cb, &res);

    if (res.data && res.len) {
        task_t *utid = launch_user_task(res.filename, res.data, res.len);
        if (utid) {
            task_drop(utid);
        }
    }
}

KSHELL_CMD("tar", show_tar);
KSHELL_CMD("run", run_user);
KSHELL_CMD("start", start_user);
