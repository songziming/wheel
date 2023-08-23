#ifndef EARLY_BUFF_H
#define EARLY_BUFF_H

#include <base.h>

typedef struct early_buff {
    uint8_t *ptr;
    uint8_t *cur;
    uint8_t *end;
} early_buff_t;

INIT_TEXT void early_buff_init(early_buff_t *buff, void *ptr, void *end);
INIT_TEXT void early_buff_set_end(early_buff_t *buff, void *end);
INIT_TEXT void *early_buff_get_cur(early_buff_t *buff);
INIT_TEXT void *early_buff_alloc(early_buff_t *buff, size_t size);

#endif // EARLY_BUFF_H
