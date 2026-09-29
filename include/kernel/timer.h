#ifndef _KERNEL_TIMER_H
#define _KERNEL_TIMER_H
#include <stdint.h>
void timer_init(uint32_t freq_hz);
uint32_t timer_ticks(void);
void timer_sleep(uint32_t ticks);
#endif
