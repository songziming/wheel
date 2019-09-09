#include <wheel.h>

typedef usize (* cmd_func_t) ();

static file_t * tty = NULL;

void do_hello() {
    char s[] = "welcome to wheel operating system.\n"
               "this shell is still very primitive.\n";
    file_write(tty, s, strlen(s));
}

void do_help() {
    char s[] = "execute any kernel function in global scope.\n";
    file_write(tty, s, strlen(s));
}

void do_tick() {
    dbg_print("current tick is %lld.\n", tick_get());
}

void do_mem() {
    page_info_show();
}

#define PROMPT "wheel> "
#define ERRMSG "no such command.\n"

typedef struct cmd {
    const char * name;
    cmd_func_t   func;
} cmd_t;

static cmd_t cmd_list[] = {
    { "hello", (cmd_func_t) do_hello },
    { "help",  (cmd_func_t) do_help  },
    { "tick",  (cmd_func_t) do_tick  },
    { "mem",   (cmd_func_t) do_mem   },
    { NULL,    (cmd_func_t) NULL     }
};

// kernel shell process
static void sh_proc() {
    static char buf[128];
    while (1) {
        // read user input
        file_write(tty, PROMPT, strlen(PROMPT));
        usize len = file_read(tty, buf, 128);

        // trim whitespace
        char * cmd = &buf[0];
        while ((len > 0) && isspace(cmd[0])) {
            ++cmd;
            --len;
        }
        while ((len > 0) && isspace(cmd[len-1])) {
            --len;
        }
        cmd[len] = '\0';

        // TODO: tokenize command?
        for (int i = 0; YES; ++i) {
            if ((NULL == cmd_list[i].name) && (NULL == cmd_list[i].func)) {
                file_write(tty, ERRMSG, strlen(ERRMSG));
                break;
            }
            if (0 == strncmp(cmd, cmd_list[i].name, len)) {
                cmd_list[i].func();
                break;
            }
        }

        // // try to execute a function
        // usize addr = dbg_sym_locate(buf);
        // if (NO_ADDR != addr) {
        //     cmd_func_t func = (cmd_func_t) addr;
        //     dbg_print("return 0x%llx.\n", func());
        // }
    }
}

// shell is started after freeing init space
// so this function cannot have __INIT attribute
void shell_lib_init() {
    tty = file_open("/dev/tty", O_READ|O_WRITE);
    if (NULL == tty) {
        dbg_print("cannot open tty, shell not starting.\n");
    } else {
        task_resume(task_create(0, sh_proc, 0,0,0,0));
    }
}
