#ifndef SHELL_H
#define SHELL_H

#include <def.h>
#include <rbtree.h>

typedef struct shell_cmd {
    rbnode_t rb;
    const char *name;
    int (*func)(int argc, char **argv);
} shell_cmd_t;

INIT_TEXT void shell_add_cmd(shell_cmd_t *cmd);

// typedef void (*print_func_t)(const char *s, size_t n);
// typedef void (*color_func_t)(uint8_t r, uint8_t g, uint8_t b);

// INIT_TEXT void shell_init(print_func_t print, color_func_t color);
INIT_TEXT void shell_init();

#endif // SHELL_H
