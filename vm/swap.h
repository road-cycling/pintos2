#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init(void);
void read_from_block(uint32_t *, int);
size_t write_to_block(uint32_t *);

#endif /* vm/swap.h */
