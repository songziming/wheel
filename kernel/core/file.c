#include <wheel.h>

// file descriptor, link to another device object

file_t * sample_file = NULL;

static pipe_t * kbd_pipe = NULL;

file_t * file_open(const char * name, int mode) {
    // TODO: check vfs, get file type, and call
    //       the corresponding open function

    if (0 == strcmp("/dev/kbd", name)) {
        if (NULL == kbd_pipe) {
            kbd_pipe = pipe_create();
        }
        return pipe_file_create(kbd_pipe, mode);
    }

    if (0 == strcmp("/dev/tty", name)) {
        return tty_file_create(mode);
    }

    return NULL;
}

void file_close(file_t * file) {
    kref_delete(&file->ref);
}

usize file_read(file_t * file, void * buf, usize len) {
    if (file->ops_mode & O_READ) {
        return FILE_OPS(file)->read(file, (u8 *) buf, len);
    } else {
        return (usize) -1;
    }
}

usize file_write(file_t * file, const void * buf, usize len) {
    if (file->ops_mode & O_WRITE) {
        return FILE_OPS(file)->write(file, (const u8 *) buf, len);
    } else {
        return (usize) -1;
    }
}
