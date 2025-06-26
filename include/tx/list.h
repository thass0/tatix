#ifndef __TX_LIST_H__
#define __TX_LIST_H__

#include <tx/assert.h>

struct dlist {
    struct dlist *prev;
    struct dlist *next;
};

static inline void dlist_init_empty(struct dlist *head)
{
    assert(head);

    head->next = head;
    head->prev = head;
}

static inline void dlist_insert(struct dlist *head, struct dlist *new)
{
    assert(head);
    assert(new);

    assert(head->next);
    head->next->prev = new;
    new->next = head->next;
    head->next = new;
    new->prev = head;
}

static inline void dlist_remove(struct dlist *head)
{
    assert(head);

    assert(head->prev);
    head->prev->next = head->next;
    assert(head->next);
    head->next->prev = head->prev;

    dlist_init_empty(head);
}

static inline bool dlist_is_empty(struct dlist *head)
{
    assert(head);

    if (head == head->next && head != head->prev)
        crash("Doubly linked list in invalid state\n");
    if (head == head->prev && head != head->next)
        crash("Doubly linked list in invalid state\n");

    return head == head->next;
}

#endif // __TX_LIST_H__
