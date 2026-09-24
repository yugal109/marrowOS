#ifndef IO_POWER_H
#define IO_POWER_H

// Restarts the machine; does not return
void system_reboot();

// Powers the machine off. Returns -1 if the hardware ignored the request.
int system_poweroff();

#endif
