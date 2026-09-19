#ifndef ISR80H_TIME_H
#define ISR80H_TIME_H

struct interrupt_frame;
void *isr80h_command25_udelay(struct interrupt_frame *frame);

#endif
