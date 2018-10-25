#ifndef VM_FRAME_H
#define VM_FRAME_H

void frame_init();
void install_frame(uint32_t* );
void evict_frame();

#endif /* vm/page.h */
