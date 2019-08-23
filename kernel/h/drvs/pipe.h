#ifndef DRVS_PIPE_H
#define DRVS_PIPE_H

#include <base.h>

typedef struct pipe pipe_t;
typedef struct file file_t;

extern pipe_t * pipe_create();
extern usize    pipe_read  (pipe_t * pipe, u8 * buf, usize len);
extern usize    pipe_write (pipe_t * pipe, const u8 * buf, usize len);

extern file_t * pipe_file_create(pipe_t * pipe, int mode);

#endif // DRVS_PIPE_H
