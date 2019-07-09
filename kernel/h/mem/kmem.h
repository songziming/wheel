#ifndef MEM_KMEM_H
#define MEM_KMEM_H

#include <base.h>

extern void * kmem_alloc(usize obj_size);
extern void   kmem_free (usize obj_size, void * obj);

extern __INIT void kmem_lib_init();

#endif // MEM_KMEM_H
