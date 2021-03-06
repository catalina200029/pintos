#ifndef PINTOS_VM_H
#define PINTOS_VM_H

#include <threads/palloc.h>

void vm_init(void);
void * vm_alloc_user_page(enum palloc_flags flags, void *upage);
void vm_free_user_page(void *kpage);
void *vm_handle_page_fault(void *uaddr, void *esp);
void vm_reclaim_pages(void);



#endif //PINTOS_VM_H
