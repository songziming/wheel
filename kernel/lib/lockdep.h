#ifndef LOCKDEP_H
#define LOCKDEP_H

// Lock dependency validator — runtime lock ordering checker.
//
// Usage:
//   spin_t lock = SPIN_INIT;   // auto-named with __FILE__:__LINE__
//   rwspin_t rw = RWSPIN_INIT;
//
// Build with LOCKDEP=1 to enable validation.
//
// Phase 1: self-deadlock detection + held-lock stack (LIFO release order).

#include <stddef.h>

#define LOCKDEP_MAX_HELD 8

typedef struct {
    const char *file;
    int         line;
} lockdep_instance_t;

typedef struct {
    void               *lock;
    lockdep_instance_t *dep;
} lockdep_held_t;

typedef struct {
    lockdep_held_t held[LOCKDEP_MAX_HELD];
    int            depth;
} lockdep_task_t;

#if defined(LOCKDEP)

void lockdep_enable();
void lockdep_acquire(void *lock, lockdep_instance_t *dep);
void lockdep_release(void *lock, lockdep_instance_t *dep);

#else // defined(LOCKDEP)

#define lockdep_enable()               ((void)0)
#define lockdep_acquire(lock, dep)     ((void)(lock), (void)(dep))
#define lockdep_release(lock, dep)     ((void)(lock), (void)(dep))

#endif // defined(LOCKDEP)

#endif // LOCKDEP_H
