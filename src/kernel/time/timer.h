#ifndef TIMER_H
#define TIMER_H

void timer_init(unsigned int hz);
void timer_on_tick(void);
unsigned int timer_get_ticks(void);
unsigned int timer_get_hz(void);
int timer_is_ready(void);
unsigned int timer_ms_to_ticks_ceil(unsigned int ms);
void timer_sleep_ticks(unsigned int ticks);
void timer_sleep_ms(unsigned int ms);
void timer_wait_for_interrupt(void);

#endif
