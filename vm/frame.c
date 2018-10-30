#include <list.h>
#include <stdio.h>
#include <string.h>
#include "lib/round.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"

//CC vs _ inconsistent


static struct list frame_table;
static struct lock frame_table_lock;

static bool install_page (void *upage, void *kpage, bool writable);

void frame_init(void) {
  list_init(&frame_table);
  lock_init(&frame_table_lock);
}


bool vm_free_frame(void *vpage_base) {

  ASSERT(pg_ofs(vpage_base) == 0);

  struct frame_table_entry *fte = vm_find_in_list(vpage_base);
  if (fte == NULL)
    return false;

  _vm_evict_frame(fte);
  return true;
}


//Clone of vm_grow_stack
// dangerous func - dont call
// void* vm_get_no_pf_frame(enum palloc_flags flags) {
//
//   uint32_t *frame = _vm_get_frame(flags);
//   //Need to map vaddr address?
//   struct sPageTableEntry *spte = getCustomSupPTE(frame, LOC_FRME, NULL, 0, 0);
//   struct frame_table_entry *fte = _vm_malloc_fte(frame, spte);
//
//   lock_acquire(&frame_table_lock);
//   list_push_back(&frame_table, &fte->elem);
//   lock_release(&frame_table_lock);
//
//   return frame;
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


static bool install_page (void *upage, void *kpage, bool writable) {
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

void vm_load_install(uint32_t *fault_addr, struct sPageTableEntry *spte) {

  uint32_t *fault_base = pg_round_down(fault_addr);
  uint32_t *frame = _vm_get_frame(PAL_USER | PAL_ZERO);
  struct frame_table_entry *fte = _vm_malloc_fte(frame, spte);

  if (spte->location & LOC_SWAP)
    _vm_load_from_disk(fault_base, fte);
  else if (spte->location & LOC_MMAP)
    _vm_load_from_file(fault_base, fte);
  else if (spte->location & LOC_ZERO)
    memset(frame, 0, PGSIZE); //UNNECESSARY @ page is zeroed
  else
    PANIC("Couldn't locate contents of SPTE");

  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &fte->elem);
  lock_release(&frame_table_lock);

  install_page(fault_base, frame, true);

}

void _vm_load_from_disk(uint32_t *fault_base UNUSED, struct frame_table_entry *fte) {

  ASSERT (fte != NULL && fte->aux != NULL);

  struct sPageTableEntry *spte = fte->aux;
  read_from_block(fte->frame, spte->disk_offset);

}

void _vm_load_from_file(uint32_t *fault_base UNUSED, struct frame_table_entry *fte) {


  struct sPageTableEntry *spte = fte->aux;

  ASSERT (fte != NULL && spte != NULL);
  ASSERT (spte->file != NULL /*&& spte->file_offset != -1*/);

  struct file *file = file_reopen(spte->file);

  off_t bytes_transferred = file_read_at(file, fte->frame, spte->read_bytes, spte->file_offset);
  //EOF -> vm_load_install uses flag PAL_ZERO (just in case)
  if (bytes_transferred != PGSIZE) {
    off_t unwritten_bytes = PGSIZE - bytes_transferred; //lol
    memset(fte->frame + bytes_transferred, 0, (size_t) unwritten_bytes);
  }

}


struct mmap_file *_vm_malloc_mmap(void *vaddr_base, int fd, int pages_taken, struct thread *t) {
  struct mmap_file *mmap_f = malloc(sizeof(struct mmap_file));

  if (mmap_f == NULL)
    PANIC("NO MORE ARENAS COULD BE ALLOCATED @ MALLOC");

  mmap_f->base = vaddr_base;
  mmap_f->fd = fd;
  mmap_f->pages_taken = pages_taken;
  mmap_f->m_id = get_mmap_id();
  mmap_f->owner = t;

  return mmap_f;
}


uint32_t *_vm_get_frame(enum palloc_flags flags) {
  uint32_t *kpage = palloc_get_page(flags);

  if (kpage == NULL) {
    _vm_evict_frame(NULL);
    kpage = palloc_get_page(flags);
    if (kpage == NULL)
      PANIC("RACE CONDITION\n");
  }
    //PANIC("YOU NEED TO IMPLEMENT EVICTION\n");

  return kpage;
}

struct mmap_file *vm_install_mmap(void *vaddr_base, struct file *file, int fd) {

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
      return NULL;
    }
  }

  // TODO: Fix
  for (i = 0; i < pages_taken; i++) {
    struct sPageTableEntry *spte = getCustomSupPTE(vaddr_base + i * PGSIZE, LOC_MMAP, mmap_file, i * PGSIZE,0, 0);
    hash_insert(&t->s_pte, &spte->hash_elem);
  }

  return mmap_f;
}

// TODO: muunmap
bool vm_muunmap_helper(struct mmap_file *mmf) {
  ASSERT(mmf != NULL);

  if (mmf == NULL)
    printf("foo");
  //
  return true;

}



struct frame_table_entry *_vm_malloc_fte(uint32_t *frame, struct sPageTableEntry *spte) {
  struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));

  if (fte == NULL)
    PANIC("NO MORE ARENAS COULD BE ALLOCATED @ MALLOC");

  fte->frame = frame;
  fte->owner = thread_current();
  fte->aux = spte;

  return fte;
}


void _vm_evict_write_back(struct frame_table_entry *fte) {

  ASSERT (fte != NULL && fte->aux != NULL);

  if (fte->aux->location & LOC_SWAP)
    _vm_write_back_to_disk(fte);
  else if ((fte->aux->location & LOC_MMAP) && (fte->aux->dirty == true)) //check dirty @@@
    _vm_write_back_to_file(fte);
  else if (fte->aux->location & LOC_ZERO) //Zeroed page...
    return;
  // else
  //   PANIC ("Couldn't locate where to write frame data.");
}

void _vm_write_back_to_disk(struct frame_table_entry *fte) {

  ASSERT (fte != NULL && fte->aux != NULL);
  ASSERT (fte->aux->location & LOC_SWAP);

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
void _vm_evict_frame(struct frame_table_entry *fte) {

  if (fte == NULL) {
    ASSERT(list_begin(&frame_table) != NULL);

    lock_acquire(&frame_table_lock);
    fte = list_entry(list_pop_front(&frame_table), struct frame_table_entry, elem);
    lock_release(&frame_table_lock);
  }

  pagedir_clear_page(fte->owner->pagedir, fte->aux->user_vaddr);
  _vm_evict_write_back(fte);
  palloc_free_page(fte->frame);
  free(fte);
}

void vm_grow_stack(uint32_t *fault_addr) {
  struct thread *t = thread_current();
  uint32_t *fault_addr_rd = pg_round_down(fault_addr);

  ASSERT(page_lookup(fault_addr_rd, &t->s_pte) == NULL);

  //aka *kpage
  uint32_t *frame = _vm_get_frame(PAL_USER | PAL_ZERO);

  // TEST
  // struct sPageTableEntry *spte = getCustomSupPTE(fault_addr_rd, LOC_FRME, NULL, 0, 0);
  // struct frame_table_entry *fte = _vm_malloc_fte(fault_addr_rd, spte);
  struct sPageTableEntry *spte = getCustomSupPTE(fault_addr_rd, LOC_FRME, NULL, 0, 0, 0);
  struct frame_table_entry *fte = _vm_malloc_fte(frame, spte);

  if (!install_page(fault_addr_rd, frame, true))
    PANIC("Couldn't install stack frame");

  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &fte->elem);
  lock_release(&frame_table_lock);

}

//FIX
void* vm_grow_stack_bandaid(uint32_t *fault_addr) {
  struct thread *t = thread_current();
  uint32_t *fault_addr_rd = pg_round_down(fault_addr);

  ASSERT(page_lookup(fault_addr_rd, &t->s_pte) == NULL);

  //aka *kpage
  uint32_t *frame = _vm_get_frame(PAL_USER | PAL_ZERO);

  // TEST
  // struct sPageTableEntry *spte = getCustomSupPTE(fault_addr_rd, LOC_FRME, NULL, 0, 0);
  // struct frame_table_entry *fte = _vm_malloc_fte(fault_addr_rd, spte);
  struct sPageTableEntry *spte = getCustomSupPTE(fault_addr_rd, LOC_FRME, NULL, 0, 0, 0);
  struct frame_table_entry *fte = _vm_malloc_fte(frame, spte);

  // if (!install_page(fault_addr_rd, frame, true))
  //   PANIC("Couldn't install stack frame");

  lock_acquire(&frame_table_lock);
  list_push_back(&frame_table, &fte->elem);
  lock_release(&frame_table_lock);

  return frame;
}
