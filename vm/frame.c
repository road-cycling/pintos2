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

//CC vs _ inconsistent

static struct list frame_table;
static struct lock frame_table_lock;
// add frame on call to palloc_get_page
static bool install_page (void *upage, void *kpage, bool writable);

void frame_init(void) {
  list_init(&frame_table);
  lock_init(&frame_table_lock);
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

void evict_frame() {

  ASSERT(list_begin(&frame_table) != NULL);

  //Cant disable interrupts, need some other way
  enum intr_level old_level;
  old_level = intr_disable ();

  struct frame_table_entry *fte = list_entry(list_pop_front(&frame_table), \
                                  struct frame_table_entry, elem);

  ASSERT(fte->owner->pagedir && PTE_P);

  //flip present bit in owning threads page table
  uint32_t *pt = pde_get_pt(*fte->owner->pagedir);
  //This doesnt clear the TLB
  *pt &= ~PTE_P;

  //write to disk
  size_t swapDiskOffset = write_to_block(fte->frame);
  setLocation(LOC_SWAP, fte->aux->location);
  fte->aux->diskOffset = swapDiskOffset;

  palloc_free_page(fte->aux->user_vaddr);
  intr_set_level(old_level);

  //need to check disk @ zeroed page
  free(fte);
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

void setUpStackFrame(uint32_t *faultingAddr) {
  struct thread *t = thread_current();
  uint32_t *fault_addr_rd = pg_round_down(fault_addr);

  ASSERT(page_lookup(fault_addr_rd, &t->s_pte) == NULL);

  uint32_t *kpage = getFrameToInstall(PAL_USER | PAL_ZERO, true);

  if (!install_page(fault_addr_rd, kpage, true))
    PANIC("Couldn't install stack frame");

  install_frame(fault_addr_rd, NULL);
}


void setUpFrame(uint32_t *faultingAddr, bool eviction) {

  struct thread *t = thread_current();
  uint32_t *fault_addr_rd = pg_round_down(fault_addr);

  struct sPageTableEntry *sPTE = page_lookup(fault_addr_rd, &t->s_pte);

  if (sPTE == NULL)
    PANIC ("Couldn't find sPTE for vaddr.\n");

  uint32_t *kpage = getFrameToInstall(PAL_USER, true);

  if (!install_page(fault_addr_rd, kpage, true))
    PANIC("Couldn't install page.\n");

  if (sPTE->location & LOC_SWAP) {
    read_from_block(kpage, sPTE->diskOffset);
  } else if (sPTE->location & LOC_FILE) {
    //to be implemented with mmap
    printf("UNUSED");
  } else if (sPTE->location & LOC_ZERO) {
    //memset(kpage, 0, 4096);
    printf("UNUSED");
  }

  install_frame(fault_addr_rd, sPTE);
  // else nothing for us to do
//PF done
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
