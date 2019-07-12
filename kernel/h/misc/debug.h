#ifndef MISC_DEBUG_H
#define MISC_DEBUG_H

#include <base.h>

typedef void (* write_func_t) (const char * str, usize len);
typedef void (* trace_func_t) ();

extern write_func_t dbg_write_hook;
extern trace_func_t dbg_trace_hook;

extern void dbg_print(const char * msg, ...);
extern void dbg_dump();

#define panic(...) ({                           \
    dbg_print("\n");                            \
    dbg_print("==> Panic " __VA_ARGS__);        \
    if (NULL != dbg_trace_hook) {               \
        dbg_trace_hook();                       \
    }                                           \
    while (1) {}                                \
})

#ifdef DEBUG
#define assert(x) ({ if (!(x)) {                \
    panic("assertion failed at %s:%d.\n",       \
          __FILE__, __LINE__);                  \
    if (NULL != dbg_trace_hook) {               \
        dbg_trace_hook();                       \
    }                                           \
    while (1) {}                                \
} })
#else
#define assert(x)
#endif

#endif // MISC_DEBUG_H
