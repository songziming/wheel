#ifndef KSHELL_H
#define KSHELL_H

#include <wheel.h>

typedef struct kcmd {
    const char *name;
    void (*func)(int argc, char *argv[]);
} kcmd_t;

#define KSHELL_CMD(name, func)  \
    static kcmd_t g_cmd_##func \
    __attribute__((used, section(".kcmd"))) = { name, func }

INIT_TEXT void kshell_start();

#endif // KSHELL_H
