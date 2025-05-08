// Simple RTC-based time.

#ifndef __TX_TIME_H__
#define __TX_TIME_H__

#include <tx/base.h>

struct time_ms {
    u64 ms;
};

static inline struct time_ms time_ms_new(u64 ms)
{
    struct time_ms time;
    time.ms = ms;
    return time;
}

void time_init(void);

struct time_ms time_current_ms(void);

#endif // __TX_TIME_H__
