#include "spin.h"
#include <arch_api.h>
#include <kstring.h>


//------------------------------------------------------------------------------
// ticket spinlock
//------------------------------------------------------------------------------

void raw_spin_take(spin_t *spin) {
    uint32_t ticket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != ticket) {
        cpu_pause();
    }
    lockdep_acquire(spin, &spin->dep);
}

void raw_spin_give(spin_t *spin) {
    lockdep_release(spin, &spin->dep);
    atomic_fetch_add(&spin->service_counter, 1);
}

int irq_spin_take(spin_t *spin) {
    int key = cpu_int_lock();
    uint32_t tickket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != tickket) {
        cpu_pause();
    }
    lockdep_acquire(spin, &spin->dep);
    return key;
}

void irq_spin_give(spin_t *spin, int key) {
    lockdep_release(spin, &spin->dep);
    atomic_fetch_add(&spin->service_counter, 1);
    cpu_int_unlock(key);
}


//------------------------------------------------------------------------------
// reader-writer spinlock
//------------------------------------------------------------------------------

void rwspin_take_writer(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    raw_spin_take(&rw->spin);   // 获取写权限
    while (0 != atomic_load(&rw->reader_num)) { // 等待所有 reader 结束
        cpu_pause();
    }
}

void rwspin_give_writer(rwspin_t *rw) {
    raw_spin_give(&rw->spin);
    lockdep_release(rw, &rw->dep);
}

void rwspin_take_reader(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    raw_spin_take(&rw->spin); // 临时获取唯一锁

    // 如果成功得到了读锁，说明此时没有 writer
    // 可以给 reader 计数器加一，释放读锁
    atomic_fetch_add(&rw->reader_num, 1);
    raw_spin_give(&rw->spin);
}

void rwspin_give_reader(rwspin_t *rw) {
    atomic_fetch_sub(&rw->reader_num, 1);
    lockdep_release(rw, &rw->dep);
}


// 获取读写锁同时关闭中断
int irqrw_take_writer(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    int key = irq_spin_take(&rw->spin);
    while (0 != atomic_load(&rw->reader_num)) { // 等待所有 reader 结束
        cpu_pause();
    }
    return key;
}
int irqrw_take_reader(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    int key = irq_spin_take(&rw->spin);
    atomic_fetch_add(&rw->reader_num, 1);
    raw_spin_give(&rw->spin);
    return key;
}
void irqrw_give_writer(rwspin_t *rw, int key) {
    irq_spin_give(&rw->spin, key);
    lockdep_release(rw, &rw->dep);
}
void irqrw_give_reader(rwspin_t *rw, int key) {
    atomic_fetch_sub(&rw->reader_num, 1);
    lockdep_release(rw, &rw->dep);
    cpu_int_unlock(key);
}
