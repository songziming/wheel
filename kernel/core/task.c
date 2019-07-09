#include <wheel.h>

// __PERCPU int no_preempt = 0;

#if (CFG_KERNEL_STACK_SIZE & (CFG_KERNEL_STACK_SIZE - 1)) != 0
    #error "CFG_KERNEL_STACK_SIZE must be power of 2"
#endif

#if CFG_KERNEL_STACK_SIZE < PAGE_SIZE
    #error "CFG_KERNEL_STACK_SIZE must be larger than PAGE_SIZE"
#endif

// create new task
task_t * task_create(const char * name, int priority, void * proc,
                     void * a1, void * a2, void * a3, void * a4) {
    assert((0 <= priority) && (priority < PRIORITY_COUNT));

    // allocate space for kernel stack, must be a single block
    int   korder = CTZL(CFG_KERNEL_STACK_SIZE) - PAGE_SHIFT;
    pfn_t kstack = page_block_alloc(ZONE_DMA|ZONE_NORMAL, order);
    usize vstack = (usize) phys_to_virt((usize) kstack << PAGE_SHIFT);
    if (NO_PAGE == kstack) {
        return NULL;
    }
    
    // mark allocated page as kstack type
    for (pfn_t i = 0; i < (1U << korder); ++i) {
        page_array[kstack + i].type  = PT_KSTACK;
        page_array[kstack + i].block = 0;
        page_array[kstack + i].order = korder;
    }
    page_array[kstack].block = 1;
    page_array[kstack].order = korder;

    // allocate tcb
    task_t * tid = kmem_alloc(sizeof(task_t));
    if (NULL == tid) {
        page_block_free(kstack, korder);
        return NULL;
    }

    // fill register info on the kernel stack
    // regs_init(&tid->regs, vstack + (1U << korder), proc, a1, a2, a3, a4);

    return tid;
}