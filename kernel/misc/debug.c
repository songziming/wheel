#include <wheel.h>

#if (CFG_DEBUG_BUFF_SIZE & (CFG_DEBUG_BUFF_SIZE - 1)) != 0
    #error "CFG_DEBUG_BUFF_SIZE must be power of 2"
#endif

// circular buffer, write to head, read from tail
static char  debug_buff[CFG_DEBUG_BUFF_SIZE];
static usize head = 0;
static usize tail = 0;

// arch specific hook functions
write_func_t dbg_write_hook = NULL;
trace_func_t dbg_trace_hook = NULL;

// read data from debug buffer
static usize buff_read(char * buf, usize len) {
    usize copy = MIN(head - tail, len);
    for (unsigned int i = 0; i < copy; ++i) {
        buf[i] = debug_buff[(tail+i) & (CFG_DEBUG_BUFF_SIZE-1)];
    }
    tail += copy;
    return copy;
}

// write data to debug buffer
static usize buff_write(const char * buf, usize len) {
    for (unsigned int i = 0; i < len; ++i) {
        debug_buff[(head+i) & (CFG_DEBUG_BUFF_SIZE-1)] = buf[i];
    }
    head += len;
    if (head - tail > CFG_DEBUG_BUFF_SIZE) {
        tail = head - CFG_DEBUG_BUFF_SIZE;
    }
    return len;
}

// print debug message in the kernel log
void dbg_print(const char * fmt, ...) {
    va_list args;
    char    buff[1024];

    va_start(args, fmt);
    usize len = vsnprintf(buff, 1023, fmt, args);
    va_end(args);

    buff_write(buff, len);

    if (NULL != dbg_write_hook) {
        dbg_write_hook(buff, len);
    }
}

// TEMPORARY: print the content of debug buffer
void dbg_dump() {
    char buf[1024];
    usize len = buff_read(buf, 1023);
    buf[len] = 0;
    dbg_print("we got %d bytes from debug_buff.\n", len);
}
