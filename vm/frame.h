#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "userprog/syscall.h"


/*
../../vm/frame.h:15: warning: ‘enum palloc_page’ declared inside parameter list
../../vm/frame.h:15: warning: its scope is only this definition or declaration, which is probably not what you want
*/
struct frame_table_entry {
  uint32_t *frame;
  struct thread *owner;
  struct sPageTableEntry *aux;
  struct list_elem elem;
};

void frame_init(void);
bool vm_free_frame(void *);
void* vm_get_no_pf_frame(enum palloc_flags);
void vm_grow_stack (uint32_t *);
struct frame_table_entry *vm_find_in_list(uint32_t *vpage_base);
void vm_load_install (uint32_t *, struct sPageTableEntry *);

void _vm_load_from_file (uint32_t *, struct frame_table_entry *);
void _vm_load_from_disk (uint32_t *, struct frame_table_entry *);
uint32_t *_vm_get_frame (enum palloc_flags);

void _vm_evict_frame(struct frame_table_entry *);
void _vm_write_back_to_disk (struct frame_table_entry *);
void _vm_write_back_to_file (struct frame_table_entry *);
void _vm_evict_write_back (struct frame_table_entry *);

struct mmap_file *vm_install_mmap(void *, struct file *, int);
bool vm_muunmap_helper(struct mmap_file *mmf);
struct frame_table_entry *_vm_malloc_fte (uint32_t *, struct sPageTableEntry *);
struct mmap_file *_vm_malloc_mmap(void *, int, int, struct thread *);

#endif /* vm/frame.h */

/*

Frame table (see Section 4.1.5 [Managing the Frame Table], page 42). Change
‘process.c’ to use your frame table allocator.
Do not implement swapping yet. If you run out of frames, fail the allocator or panic
the kernel.

Supplemental page table and page fault handler (see Section 4.1.4 [Managing the Supplemental
Page Table], page 42). Change ‘process.c’ to record the necessary information
in the supplemental page table when loading an executable and setting up its
stack. Implement loading of code and data segments in the page fault handler. For
now, consider only valid accesses.
After this step, your kernel should pass all of the project 2 functionality test cases, but
only some of the robustness tests.

*/
