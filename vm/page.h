#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/off_t.h"
#include <hash.h>

#define LOC_ZERO 0x0
#define LOC_SWAP 0x1
#define LOC_MMAP 0x2
#define LOC_FRME 0x4

#define setLocation(n, val) ((val) |= (1 << (n)))
#define clrLocation(n, val) ((val) &= ~((1 << (n)))

struct sPageTableEntry {
  uint32_t *user_vaddr;          /* Virtual Page Rounded Down */
  struct hash_elem hash_elem;    /* Hash Table Element */
  uint8_t location;              /* Struct not packed / size matters less */
  struct file *file;             /* FP */
  off_t file_offset;              /* FP Offset */
  size_t disk_offset;             /* disk Offset */
  // struct thread *t thread_current
  //bool dirty;
  //bool accessed;
};


unsigned page_hash (const struct hash_elem *, void * UNUSED);
bool page_less (const struct hash_elem *, const struct hash_elem *, void * UNUSED);
// void page_free (struct hash_elem*, void * UNUSED);
struct sPageTableEntry *page_lookup (void *, struct hash *);
struct sPageTableEntry *getSupPTE(uint32_t *);
struct sPageTableEntry *getCustomSupPTE(uint32_t *, uint8_t, struct file *, off_t, size_t);

#endif /* vm/page.h */
