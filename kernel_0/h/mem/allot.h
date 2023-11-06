#ifndef MEM_ALLOT_H
#define MEM_ALLOT_H

extern __INIT void * allot_temporary(usize len);
extern __INIT void * allot_permanent(usize len);

#ifdef DEBUG
extern __INIT void allot_disable();
#else
#define allot_disable(x)
#endif

#endif // MEM_ALLOT_H
