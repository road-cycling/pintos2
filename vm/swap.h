#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init();
void read_from_block(uint8_t *frame, int idx);
void write_to_block(uint8_t *frame, int idx);

#endif /* vm/swap.h */
