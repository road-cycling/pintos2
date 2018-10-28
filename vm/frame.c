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

  //printf("install_frame before lock_acquire\n");
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
    fte->aux->disk_offset = swapDiskOffset;
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
    read_from_block(write_to, spte->disk_offset);
  } else if (spte->location & LOC_FILE) {
    //to be implemented with mmap
    printf("not hit\n");
  } else if (spte->location & LOC_ZERO) {
    //memset(kpage, 0, 4096);
    printf("not hit\n");
  }

}

//This function needs to be rewritten
void setUpFrame(uint32_t *fault_addr, bool eviction) {

  struct thread *t = thread_current();
  uint32_t *fault_addr_rd = pg_round_down(fault_addr);

  struct sPageTableEntry *sPTE = page_lookup(fault_addr_rd, &t->s_pte);

  if (sPTE == NULL)
    PANIC ("Couldn't find sPTE for vaddr.\n");

  uint32_t *kpage = getFrameToInstall(PAL_USER, true);

  if (!install_page(fault_addr_rd, kpage, true))
    PANIC("Couldn't install page.\n");

  vm_write_to_frame(kpage, sPTE);

  install_frame(fault_addr_rd, sPTE);

}

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


/*

New API

*/

void vm_load_install(uint32_t *fault_addr, struct sPageTableEntry *spte) {

  uint32_t *fault_base = pg_round_down(fault_addr);
  uint32_t *frame = _vm_get_frame(PAL_USER | PAL_ZERO);
  struct frame_table_entry *fte = _vm_malloc_fte(fault_base, spte);

  if (spte->location & LOC_SWAP)
    _vm_load_from_disk(fault_base, fte);
  else if (spte->location & LOC_MMAP)
    _vm_load_from_file(fault_base, fte);
  else
    PANIC("Couldn't locate contents of SPTE");

  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &fte->elem);
  lock_release(&frame_table_lock);

}

void _vm_load_from_disk(uint32_t *fault_base, struct frame_table_entry *fte) {

  struct sPageTableEntry *spte = fte->aux;

  ASSERT (fte != NULL && spte != NULL);
  ASSERT (spte->disk_offset != -1);

  read_from_block(fault_base, spte->disk_offset);

  // spte->location = LOC_FRME;
  // spte->disk_offset = -1;
}

void _vm_load_from_file(uint32_t *fault_base, struct frame_table_entry *fte) {

  struct sPageTableEntry *spte = fte->aux;

  ASSERT (fte != NULL && spte != NULL);
  ASSERT (spte->file != NULL && spte->file_offset != -1);

  off_t bytes_transferred = file_read_at(fte->aux, fault_base, PGSIZE, spte->file_offset);

  //EOF -> vm_load_install uses flag PAL_ZERO (just in case)
  if (bytes_transferred != PGSIZE) {
    off_t unwritten_bytes = bytes_transferred - PGSIZE;
    memset(fault_base + bytes_transferred, 0, unwritten_bytes);
  }

  // spte->location = LOC_FRAME;
  // spte->file_offset = -1;
}

struct frame_table_entry *_vm_malloc_fte(uint32_t *frame, struct sPageTableEntry *spte) {
  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));

  if (fte == NULL)
    PANIC("NO MORE ARENAS COULD BE ALLOCATED @ MALLOC")

  fte->frame = frame;
  fte->owner = thread_current();
  fte->aux = spte;

  return fte;
}

struct mmap_file *_vm_malloc_mmap(void *vaddr_base, int fd, int pages_taken, struct thread *t) {
  struct mmap_file *mmap_f = malloc(sizeof(struct mmap_file));

  if (mmap_f == NULL)
    PANIC("NO MORE ARENAS COULD BE ALLOCATED @ MALLOC")

  mmap_f->base = vaddr_base;
  mmap_f->fd = fd;
  mmap_f->pages_taken = pages_taken;
  mmap_f->m_id = get_mmap_id();
  mmap_f->owner = t;

  return mmap_f;
}


uint32_t *_vm_get_frame(enum palloc_flags flags) {
  uint32_t *kpage = palloc_get_page(flags);

  if (kpage == NULL)
    PANIC("YOU NEED TO IMPLEMENT EVICTION\n");

  return kpage;
}

bool vm_install_mmap(void *vaddr_base, struct file *file, int fd) {

  ASSERT(pg_ofs(vaddr_base) == 0);

  struct file *mmap_file = file_reopen(file);
  struct thread *t = thread_current();
  uint32_t size_file_bytes = file_length(mmap_file);

  int pages_taken = DIV_ROUND_UP(size_file_bytes, PGSIZE);

  struct mmap_file *mmap_f = _vm_malloc_mmap(vaddr_base, fd, pages_taken, t);

  int i = 0;
  for (; i < pages_taken; i++) {
    if (page_lookup(vaddr_base + i * PGSIZE, &t->s_pte) == NULL) {
      free(mmap_f);
      return false;
    }
  }

  for (i = 0; i < pages_taken; i++) {
    struct sPageTableEntry *spte = getCustomSupPTE(vaddr_base + i * PGSIZE, LOC_MMAP, mmap_file, i * PGSIZE, 0);
    hash_insert(spte->hash_elem, &t->s_pte);
  }

  return true;
}

struct frame_table_entry *_vm_malloc_fte(uint32_t *frame, struct sPageTableEntry *spte) {
  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));

  if (fte == NULL)
    PANIC("NO MORE ARENAS COULD BE ALLOCATED @ MALLOC")

  fte->frame = frame;
  fte->owner = thread_current();
  fte->aux = spte;

  return fte;
}


void _vm_evict_write_back(struct frame_table_entry *fte) {

  ASSERT (fte != NULL && fte->aux != NULL);

  if (fte->aux->location & LOC_SWAP)
    _vm_write_back_to_disk(fte);
  else if (fte->aux->location & LOC_MMAP)
    _vm_write_back_to_file(fte);
  else if (fte->aux->location & LOC_ZERO) //Zeroed page...
    continue;
  else
    PANIC ("Couldn't locate where to write frame data.");
}

void _vm_write_back_to_disk(struct frame_table_entry *fte) {

  ASSERT (fte != NULL & fte->aux != NULL);
  ASSERT (fte->aux->loaction & LOC_SWAP);

  size_t block_index = write_to_block(fte->frame);
  fte->aux->location = LOC_SWAP;
  fte->aux->disk_offset = block_index;
}

void _vm_write_back_to_file(struct frame_table_entry *fte) {

  ASSERT (fte != NULL && fte->aux != NULL);
  ASSERT(pg_ofs(fte->frame) == 0);
  ASSERT(fte->aux->location & LOC_MMAP);
  ASSERT(fte->aux->file != NULL);

  struct file *file = file_reopen(fte->aux->file);

  //Works now - since the file size doesn't grow...need to add file size to sPTE
  /*off_t bytes_written = */file_write_at(file, fte->frame, PGSIZE, fte->aux->file_offset);
  //check to make sure bytes_written = (to be added value) file segment size
}


//Implementing LRA (last recently added)
// will add LRU / clock at the end
void _vm_evict_frame() {

  ASSERT(list_begin(&frame_table) != NULL);

  lock_acquire(&frame_table_lock);
  struct frame_table_entry *fte = list_entry(list_pop_front(&frame_table), struct frame_table_entry, elem);
  lock_release(&frame_table_lock);

  pagedir_clear_page(fte->owner->pagedir, fte->frame);
  _vm_evict_write_back(fte);
  palloc_free_page(fte->frame);
  free(fte);
}

void vm_grow_stack(uint32_t *fault_addr) {
  struct thread *t = thread_current();
  uint32_t *fault_addr_rd = pg_round_down(fault_addr);

  ASSERT(page_lookup(fault_addr_rd, &t->s_pte) == NULL);

  uint32_t *frame = _vm_get_frame(PAL_USER | PAL_ZERO);
  struct sPageTableEntry *spte = getCustomSupPTE(fault_addr_rd, LOC_FRME, NULL, 0, 0);
  struct frame_table_entry *fte = _vm_malloc_fte(fault_addr_rd, spte);

  if (!install_page(fault_addr_rd, frame, true))
    PANIC("Couldn't install stack frame");

  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &fte->elem);
  lock_release(&frame_table_lock);

}
