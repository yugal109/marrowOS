#include "idt/irq.h"
#include "io/io.h"

void IRQ_enable(IRQ irq)
{
    int port = IRQ_MASTER_PORT;

    // The IRQ_line relative to the PIC
    IRQ relative_irq = irq;

    // If the IRQ belongs to slave pig
    // then update
    if (irq >= PIC_SLAVE_STARTING_IRQ)
    {
        port = IRQ_SLAVE_PORT;
        relative_irq = irq - PIC_SLAVE_STARTING_IRQ;
    }

    // read the current value from the master or slave port
    int pic_value = insb(port);
    // set the bit corresponding to the IRQ to mask (disable) it
    pic_value &= ~(1 << relative_irq);

    // write the updated mask back to the PIC
    // disabling that IRQ functionality
    outb(port, pic_value);
}

void IRQ_disable(IRQ irq)
{
    int port = IRQ_MASTER_PORT;
    // the IRQ_line relative to the PIC
    IRQ relative_irq = irq;

    // if the IRQ belongs to slave pig
    // then update
    if (irq >= PIC_SLAVE_STARTING_IRQ)
    {
        port = IRQ_SLAVE_PORT;
        relative_irq = irq - PIC_SLAVE_STARTING_IRQ;
    }

    // read the current value from the master or slave port
    int pic_value = insb(port);

    // set the bit corresponding to the IRQ mask (disable) it
    pic_value |= (1 << relative_irq);

    // write the updated mask back to the PIC
    // disabling that IRQ functionality
    outb(port, pic_value);
}
