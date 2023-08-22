#ifndef CORE_PIPE_H
#define CORE_PIPE_H

#include <base.h>
#include <misc/kref.h>

typedef struct pipe pipe_t;
typedef struct file file_t;

extern pipe_t * pipe_create();
static inline void pipe_delete(pipe_t * pipe) {
    kref_delete(pipe);
}

extern usize pipe_read (pipe_t * pipe, u8 * buf, usize len);
extern usize pipe_write(pipe_t * pipe, const u8 * buf, usize len);

extern file_t * pipe_file_open  (pipe_t * pipe, int mode);
extern void     pipe_file_create(file_t * files[2]);

#endif // CORE_PIPE_H
