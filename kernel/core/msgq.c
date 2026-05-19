#include "msgq.h"

void msgq_init(msgq_t *q) {
    q->lock = SPIN_INIT;

    prioq_init(&q->readers);
    prioq_init(&q->writers);
    // vmspace_alloc()

    // TODO 需要手动分配物理内存，手动映射的接口
}
