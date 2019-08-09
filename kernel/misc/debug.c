#include <wheel.h>

#if (CFG_DEBUG_BUFF_SIZE & (CFG_DEBUG_BUFF_SIZE - 1)) != 0
    #error "CFG_DEBUG_BUFF_SIZE must be power of 2"
#endif

static char   dbg_buff[CFG_DEBUG_BUFF_SIZE];
static spin_t dbg_lock = SPIN_INIT;
static fifo_t dbg_fifo = FIFO_INIT(dbg_buff, CFG_DEBUG_BUFF_SIZE);

// arch specific hook functions
write_func_t dbg_write_hook = NULL;
trace_func_t dbg_trace_hook = NULL;

// print debug message in the kernel log
void dbg_print(const char * fmt, ...) {
    va_list args;
    char    str[1024];

    va_start(args, fmt);
    usize len = vsnprintf(str, 1023, fmt, args);
    va_end(args);

    u32 key = irq_spin_take(&dbg_lock);
    fifo_write(&dbg_fifo, (const u8 *) str, len, YES);
    irq_spin_give(&dbg_lock, key);

    if (NULL != dbg_write_hook) {
        dbg_write_hook(str, len);
    }
}

// TEMPORARY: print the content of debug buffer
void dbg_dump() {
    static char str[1024];
    u32 key = irq_spin_take(&dbg_lock);
    usize len = fifo_read(&dbg_fifo, (u8 *) str, 1023);
    irq_spin_give(&dbg_lock, key);

    dbg_print("we got %d bytes from dbg_buff:\n", len);
    dbg_write_hook(str, len);
}
