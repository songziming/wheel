#ifndef PROCESS_H
#define PROCESS_H

#include <vmspace.h>

typedef struct process {
    vmspace_t space;
    size_t    table;
} process_t;

extern process_t g_kernel_proc;


#endif // PROCESS_H
