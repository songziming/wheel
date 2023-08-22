#ifndef CORE_WORK_H
#define CORE_WORK_H

#include <base.h>

extern void work_enqueue(void * proc, void * a1, void * a2, void * a3, void * a4);
extern void work_dequeue();

// requires: percpu-var
extern __INIT void work_lib_init();

#endif // CORE_WORK_H
