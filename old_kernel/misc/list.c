#include <wheel.h>

void dl_push_head(dllist_t * list, dlnode_t * node) {
    dlnode_t * head = list->head;
    list->head = node;
    node->prev = NULL;
    node->next = head;
    if (NULL == head) {
        list->tail = node;
    } else {
        head->prev = node;
    }
}

void dl_push_tail(dllist_t * list, dlnode_t * node) {
    dlnode_t * tail = list->tail;
    list->tail = node;
    node->prev = tail;
    node->next = NULL;
    if (NULL == tail) {
        list->head = node;
    } else {
        tail->next = node;
    }
}

dlnode_t * dl_pop_head(dllist_t * list) {
    dlnode_t * head = list->head;
    if (NULL != head) {
        list->head = head->next;
        if (NULL == head->next) {
            list->tail = NULL;
        } else {
            head->next->prev = NULL;
        }
    }
    return head;
}

dlnode_t * dl_pop_tail(dllist_t * list) {
    dlnode_t * tail = list->tail;
    if (NULL != tail) {
        list->tail = tail->prev;
        if (NULL == tail->prev) {
            list->head = NULL;
        } else {
            tail->prev->next = NULL;
        }
    }
    return tail;
}

// insert node `x` before `y`
void dl_insert_before(dllist_t * list, dlnode_t * x, dlnode_t * y) {
    if (NULL == y) {
        dl_push_tail(list, x);
    } else {
        dlnode_t * p = y->prev;
        x->prev = p;
        x->next = y;
        y->prev = x;
        if (NULL == p) {
            list->head = x;
        } else {
            p->next = x;
        }
    }
}

// insert node `x` after `y`
void dl_insert_after(dllist_t * list, dlnode_t * x, dlnode_t * y) {
    if (NULL == y) {
        dl_push_head(list, x);
    } else {
        dlnode_t * n = y->next;
        x->prev = y;
        x->next = n;
        y->next = x;
        if (NULL == n) {
            list->tail = x;
        } else {
            n->prev = x;
        }
    }
}

void dl_remove(dllist_t * list, dlnode_t * node) {
    dlnode_t * prev = node->prev;
    dlnode_t * next = node->next;
    if (NULL == prev) {
        list->head = next;
    } else {
        prev->next = next;
    }
    if (NULL == next) {
        list->tail = prev;
    } else {
        next->prev = prev;
    }
}

int dl_is_empty(dllist_t * list) {
    if ((NULL == list->head) && (NULL == list->tail)) {
        return YES;
    } else {
        return NO;
    }
}
