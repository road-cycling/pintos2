#include "vm/swap.c"

#include <bitmap.h>

static struct block *global_swap_block;
static struct bitmap *swap_bitmap;
static struct lock bitmapLock;

void swap_init() {
  global_swap_block = block_get_role(BLOCK_SWAP);
  swap_bitmap = bitmap_create(block_size(global_swap_block));
  lock_init(&bitmapLock);
}

size_t write_to_block(uint8_t *frame) {
  //@ 2^9 @ 512 B sector vs 4096 page 2 2^12

  lock_acquire(&bitmapLock);
  size_t idx = bitmap_scan_and_flip (swap_bitmap, 0, 8, false);
  lock_release(&bitmapLock);

  if (idx == BITMAP_ERROR)
    PANIC ("SWAP DISK SPACE EXHAUSTED");
  int i = 0;
  for (; i < 8; i++)
    block_write(global_swap_block, i + idx, frame + (512 * i));
  return idx;
}

void read_from_block(uint8_t *frame, int idx) {
  int i = 0;
  for (; i < 8; i++)
    block_read(global_swap_block, idx + i, frame + (512 * i));

  lock_acquire(&bitmapLock);
  bitmap_set_multiple(swap_bitmap, idx, 8, false);
  lock_release(&bitmapLock);


}
