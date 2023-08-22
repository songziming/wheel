#ifndef CORE_FILE_H
#define CORE_FILE_H

#include <base.h>
#include <misc/kref.h>

typedef struct file_ops file_ops_t;
typedef struct file     file_t;

typedef usize (* file_read_t)  (file_t * file,       void * buf, usize len);
typedef usize (* file_write_t) (file_t * file, const void * buf, usize len);
typedef void  (* file_lseek_t) (file_t * file, usize pos);

struct file_ops {
    file_read_t  read;
    file_write_t write;
    file_lseek_t lseek;
};

struct file {
    kref_t  ref;
    usize   ops_mode;
    void *  private;
};

// we save operations and open mode in one field
#define FILE_OPS(f)     ((file_ops_t *) ((f)->ops_mode & ~3))
#define FILE_MODE(f)    ((int)          ((f)->ops_mode &  3))

// open modes
#define O_READ          1
#define O_WRITE         2


extern file_t * file_open(const char * name, int mode);
static inline void file_close(file_t * file) {
    kref_delete(&file->ref);
}

extern usize file_read (file_t * file,       void * buf, usize len);
extern usize file_write(file_t * file, const void * buf, usize len);

#endif // CORE_FILE_H
