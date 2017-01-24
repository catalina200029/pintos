#include <lib/debug.h>
#include "ordered_list.h"

void
ordered_list_init(struct ordered_list *ol, list_less_func *less) {
    list_init(&ol->list);
    ol->less = less;
}

/**
 * Given an ordered_list whose single list_elem has become out of order,
 * puts it back in order by removal then ordered insertion.
 */
// TODO: WRITE COMMENT HERE EXPLAINING WHY REMOVE/INSERT IS FASTER THAN SORT
void
ordered_list_resort(struct ordered_list *ol, struct list_elem *elem,
                    void *aux) {
    struct list_elem *removed = list_remove(elem);
    ASSERT(removed != NULL);
    list_insert_ordered(&ol->list, elem, ol->less, aux);
}

void
ordered_list_insert(struct ordered_list *ol, struct list_elem *elem, void
*aux) {
    list_insert_ordered(&ol->list, elem, ol->less, aux);
}
