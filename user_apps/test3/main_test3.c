#include <libc.h>

int main() {
    sys_print("hello from user program\n");
    sys_print("type something:");

    char line[512];
    sys_read(line, sizeof(line));

    sys_print("you typed: <");
    sys_print(line);
    sys_print(">\n");

    return 0;
}
