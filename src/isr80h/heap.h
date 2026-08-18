#ifndef ISR80_HEAP_H
#define ISR80_HEAP_H

struct interrupt_frame;
void *isr80h_command4_malloc(struct interrupt_frame *frame);

#endif
