#include <list.h>
#include <stdio.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

//CC vs _ inconsistent

static struct list frame_table;
static struct lock frame_table_lock;
// add frame on call to palloc_get_page
static bool install_page (void *upage, void *kpage, bool writable);

void frame_init(void) {
  list_init(&frame_table);
  lock_init(&frame_table_lock);
}

void *vm_get_frame(enum palloc_flags flags) {
  uint32_t *kpage = getFrameToInstall(flags, false);
  install_frame(kpage, NULL);
  return kpage;
}

bool vm_free_frame(void *vpage_base, bool deleteSPTE) {

  ASSERT(pg_ofs(vpage_base) == 0);

  struct frame_table_entry *fte = vm_find_in_list(vpage_base);
  if (fte == NULL)
    return false;

  if (deleteSPTE) {
    // Remove from threads hashtable
    //
    printf("not implemented yet\n");
  }

  list_remove(&fte->elem);
  free(fte);
  return true;

}

//palloc_get_page returns KVA
void install_frame(uint32_t* kv_addr, struct sPageTableEntry *entry) {
  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));

  fte->frame = kv_addr;
  fte->owner = thread_current();
  fte->aux   = entry == NULL
                  ? getSupPTE(kv_addr)
                  : entry;

  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &fte->elem);
  lock_release(&frame_table_lock);
}


uint32_t *getFrameToInstall(enum palloc_flags flags, bool eviction) {
  uint32_t *kpage = palloc_get_page(flags);

  if (kpage == NULL && eviction) {
    evict_frame();

    kpage = palloc_get_page(flags);

    if (kpage == NULL)
      PANIC("No Free Frame Post Eviction\n");
  }

  return kpage;
}

void vm_write_back(struct frame_table_entry *fte) {

  struct sPageTableEntry *spte = fte->aux;

  if (spte->location & LOC_SWAP) {
    size_t swapDiskOffset = write_to_block(fte->frame);
    setLocation(LOC_SWAP, fte->aux->location);
    fte->aux->diskOffset = swapDiskOffset;
  } else if (spte->location & LOC_FILE) {
    //only for mmap
    printf("unreached (for now)\n");
  }

}


void evict_frame() {

  ASSERT(list_begin(&frame_table) != NULL);

  //Cant disable interrupts, need some other way
  enum intr_level old_level;
  old_level = intr_disable ();

  struct frame_table_entry *fte = list_entry(list_pop_front(&frame_table), \
                                  struct frame_table_entry, elem);

  pagedir_clear_page(fte->owner->pagedir, fte->frame);

  vm_write_back(fte);

  palloc_free_page(fte->aux->user_vaddr);
  intr_set_level(old_level);

  free(fte);
}

void vm_install_stack(uint32_t *fault_addr) {
  struct thread *t = thread_current();
  uint32_t *fault_addr_rd = pg_round_down(fault_addr);

  ASSERT(page_lookup(fault_addr_rd, &t->s_pte) == NULL);

  uint32_t *kpage = getFrameToInstall(PAL_USER | PAL_ZERO, true);

  if (!install_page(fault_addr_rd, kpage, true))
    PANIC("Couldn't install stack frame");

  install_frame(fault_addr_rd, NULL);
}

void vm_write_to_frame(uint32_t *write_to, struct sPageTableEntry *spte) {

  if (spte->location & LOC_SWAP) {
    read_from_block(write_to, spte->diskOffset);
  } else if (spte->location & LOC_FILE) {
    //to be implemented with mmap
    printf("not hit\n");
  } else if (spte->location & LOC_ZERO) {
    //memset(kpage, 0, 4096);
    printf("not hit\n");
  }

}


// void setUpFrame(uint32_t *fault_addr, bool eviction) {
//
//   struct thread *t = thread_current();
//   uint32_t *fault_addr_rd = pg_round_down(fault_addr);
//
//   struct sPageTableEntry *sPTE = page_lookup(fault_addr_rd, &t->s_pte);
//
//   if (sPTE == NULL)
//     PANIC ("Couldn't find sPTE for vaddr.\n");
//
//   uint32_t *kpage = getFrameToInstall(PAL_USER, true);
//
//   if (!install_page(fault_addr_rd, kpage, true))
//     PANIC("Couldn't install page.\n");
//
//   vm_write_to_page(kpage, sPTE);
//
//   install_frame(fault_addr_rd, sPTE);
//
// }

struct frame_table_entry *vm_find_in_list(uint32_t *vpage_base) {
  struct list_elem *e;
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
    struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
    if (fte->frame == vpage_base)
      return fte;
  }

  return NULL;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page (void *upage, void *kpage, bool writable) {
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}











// @on eviction mark not present
