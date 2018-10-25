
#include <hash.h>
#include "vm/page.h"
#include "threads/vaddr.h"
// on PF look up faulting addr / pull it in
// Rss management on PEXIT ;;


struct sPageTableEntry *getSupPTE(uint32_t *user_vaddr) {
  struct sPageTableEntry *spte = malloc(sizeof (struct sPageTableEntry));
  spte->user_vaddr = pg_round_down(user_vaddr);
  spte->location = 0;
  spte->file = NULL;
  spte->off_t = 0;
  spte->diskOffset->0;
  return spte;
}

unsigned page_hash (const struct hash_elem *elem, void aux* UNUSED) {
  const struct sup_page_table_entry *p = hash_entry(elem, struct sPageTableEntry, hash_elem);
  return hash_bytes(&p->user_vaddr, sizeof p->user_vaddr);
}

bool page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  const struct sPageTableEntry *a = hash_entry (a, struct sPageTableEntry, hash_elem);
  const struct sPageTableEntry *b = hash_entry (b, struct sPageTableEntry, hash_elem);
  return a->user_vaddr < b->user_vaddr;
}

struct sPageTableEntry *page_lookup (void *user_vaddr) {
  struct sPageTableEntry p;
  struct hash_elem *e;
  p.user_vaddr = user_vaddr;
  e = hash_find (&thread_current()->s_pte, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct sPageTableEntry, hash_elem) : NULL;
}




// hash_insert (&pages, &p->hash_elem); /* inserting */
// hash_delete (&pages, &p->hash_elem); /* removing */
