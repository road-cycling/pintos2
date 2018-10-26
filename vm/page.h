#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/off_t.h"

#define LOC_ZERO 0x0
#define LOC_SWAP 0x1
#define LOC_FILE 0x2
#define LOC_FRME 0x4

#define setLocation(n, val) ((val) |= (1 << (n)))
#define clrLocation(n, val) ((val) &= ~((1 << (n)))

struct sPageTableEntry {
  uint32_t *user_vaddr;          /* Virtual Page Rounded Down */
  struct hash_elem hash_elem;    /* Hash Table Element */
  uint8_t location;              /* Struct not packed / size matters less */
  struct file *file;             /* FP */
  off_t fileoffset;              /* FP Offset */
  size_t diskOffset;             /* disk Offset */
};

unsigned page_hash (const struct hash_elem *, void * UNUSED);
bool page_less (const struct hash_elem *, const struct hash_elem *, void * UNUSED);
struct sPageTableEntry *page_lookup (void *, struct hash *);
struct sPageTableEntry *getSupPTE(uint32_t *);

#endif /* vm/page.h */
