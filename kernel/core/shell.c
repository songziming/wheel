#include <wheel.h>

typedef usize (* cmd_func_t) ();

static file_t * tty = NULL;

void hello() {
    char s[] = "welcome to wheel operating system.\n"
               "this shell is still very primitive.\n";
    file_write(tty, s, strlen(s));
}

void help() {
    char s[] = "execute any kernel function in global scope.\n";
    file_write(tty, s, strlen(s));
}

void tick() {
    dbg_print("current tick is %lld.\n", tick_get());
}

// prevent shell command functions got removed by linker
static void * syms[] = {
    hello,
    help,
    tick,
};

// kernel shell
static void sh_proc() {
    // fdesc_t * tty = ios_open("/dev/tty", IOS_READ|IOS_WRITE);
    static char buf[128];
    dbg_print("commands list 0x%llx.\n", syms);
    while (1) {
        // read user input
        file_write(tty, "write something> ", 17);
        usize len = file_read(tty, buf, 128);
        if ('\n' == buf[len-1]) {
            --len;
        }

        // echo
        buf[len] = '\0';
        file_write(tty, "we got: `", 9);
        file_write(tty, buf, strlen(buf));
        file_write(tty, "`.\n", 3);

        // try to execute function
        usize addr = dbg_sym_locate(buf);
        if (NO_ADDR != addr) {
            cmd_func_t func = (cmd_func_t) addr;
            dbg_print("return 0x%llx.\n", func());
        }
    }
}

void shell_lib_init() {
    // tty = ios_open("/dev/tty", IOS_READ|IOS_WRITE);
    tty = file_open("/dev/tty", O_READ|O_WRITE);
    task_resume(task_create(0, sh_proc, 0,0,0,0));
}
