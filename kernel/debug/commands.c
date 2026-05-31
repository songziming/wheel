#include <console.h>
#include <kshell.h>


static void show_mem(int argc, char *argv[]) {
    if (argc < 3) {
        console_printf("usage: %s <HEX_ADDR> <BYTES>\n", argv[0]);
        return;
    }

    uint8_t *data = (uint8_t*)str2num(argv[1]);
    size_t len = str2num(argv[2]);

    // 打印hex，每行16字节
    console_printf("reading memory");
    for (size_t i = 0; i < len; ++i) {
        if (0 == (i & 15)) {
            console_printf("\n%p:", data + i);
        }
        console_printf(" %02x", data[i]);
    }
    console_printf("\n");
}

KSHELL_CMD("xxd", show_mem);
