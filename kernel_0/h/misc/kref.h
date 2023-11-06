#ifndef MISC_KREF_H
#define MISC_KREF_H

#include <base.h>
#include <arch.h>

typedef void (* delete_t) (void * obj);

typedef struct kref {
    int      count;
    delete_t delete;
} kref_t;

#define KREF_INIT(del) ((kref_t) { 1, (delete_t) (del) })

static inline void * kref_retain(void * obj) {
    kref_t * ref = (kref_t *) obj;
    atomic32_inc(&ref->count);
    return obj;
}

static inline void kref_delete(void * obj) {
    kref_t * ref = (kref_t *) obj;
    if (1 == atomic32_dec(&ref->count)) {
        ref->delete(obj);
    }
}

#endif // MISC_KREF_H
