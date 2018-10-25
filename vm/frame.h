#ifndef VM_FRAME_H
#define VM_FRAME_H

void frame_init();
void install_frame(uint32_t*, struct sPageTableEntry *);
void evict_frame();
void setUpFrame(uint32_t*);

#endif /* vm/page.h */
