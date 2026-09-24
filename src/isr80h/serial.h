#ifndef ISR80H_SERIAL_H
#define ISR80H_SERIAL_H

struct interrupt_frame;

// serial_write(buffer, length): returns bytes sent (at most EHCI_SERIAL_CHUNK) or a negative error
void *isr80h_command29_serial_write(struct interrupt_frame *frame);

#endif
