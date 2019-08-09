#include <wheel.h>

// copy and remove the data from fifo
usize fifo_read(fifo_t * fifo, u8 * buf, usize len) {
    u8  * data = fifo->data;
    usize size = fifo->size;
    usize copy = MIN(fifo->w_offset - fifo->r_offset, len);
    assert(0 == (size & (size - 1)));

    for (unsigned int i = 0; i < copy; ++i) {
        buf[i] = data[(fifo->r_offset + i) & (size - 1)];
    }
    fifo->r_offset += copy;
    return copy;
}

// copy and keep the data in fifo
// `fifo->r_offset` remains unchanged
usize fifo_peek(fifo_t * fifo, u8 * buf, usize len) {
    u8  * data = fifo->data;
    usize size = fifo->size;
    usize copy = MIN(fifo->w_offset - fifo->r_offset, len);
    assert(0 == (size & (size - 1)));

    for (unsigned int i = 0; i < copy; ++i) {
        buf[i] = data[(fifo->r_offset + i) & (size - 1)];
    }
    return copy;
}

// `force` means whether overwrite old data if space not sufficient
usize fifo_write(fifo_t * fifo, const u8 * buf, usize len, int force) {
    u8  * data = fifo->data;
    usize size = fifo->size;
    assert(0 == (size & (size - 1)));

    if (YES == force) {
        for (unsigned int i = 0; i < len; ++i) {
            data[(fifo->w_offset + i) & (size - 1)] = buf[i];
        }
        fifo->w_offset += len;
        if (fifo->w_offset - fifo->r_offset > size) {
            fifo->r_offset = fifo->w_offset - size;
        }
        return len;
    } else {
        usize copy = MIN(fifo->r_offset + size - fifo->w_offset, len);
        for (unsigned int i = 0; i < copy; ++i) {
            data[(fifo->w_offset + i) & (size - 1)] = buf[i];
        }
        fifo->w_offset += copy;
        return copy;
    }
}
