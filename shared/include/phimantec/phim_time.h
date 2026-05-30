#ifndef PHIM_TIME_H
#define PHIM_TIME_H

#include <stdint.h>

uint64_t phim_now_ms(void);
uint64_t phim_now_us(void);
void phim_sleep_ms(uint32_t ms);

#endif
