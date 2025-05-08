#include <tx/asm.h>
#include <tx/assert.h>
#include <tx/print.h>
#include <tx/time.h>

///////////////////////////////////////////////////////////////////////////////
// PIT code                                                                 //
///////////////////////////////////////////////////////////////////////////////

// The PIT is not used as the primary timer, but it's used to estimate the frequency of the TSC.

// The code in the PIT section is from Unikraft. It's under the ISC license. See:
// https://github.com/unikraft/unikraft/blob/4fd2c0129c2d4946497f40163c985d8604cb5a2a/plat/kvm/x86/tscclock.c#L123

#define PIT_MAX_HZ 1193182 /* Base frequency of the PIT in Hz */
#define PIT_DIVISOR_HZ 100

#define PIT_PORT_CHAN0 0x40
#define PIT_PORT_CMD 0x43

#define PIT_CMD_RATEGEN BIT(2) /* Operating mode: rate generator */
#define PIT_CMD_ACCESS_HILO (BIT(4) | BIT(5)) /* Access mode: both the lobyte and the hibyte */

static u32 pit_gettick(void)
{
    u16 read = 0;
    outb(PIT_PORT_CMD, 0); // Select channel 0 (bits 6 and 7 are zero) and use latch mode (4 and 5 are zero).
    read = inb(PIT_PORT_CHAN0);
    read |= (u16)inb(PIT_PORT_CHAN0) << 8;
    return read;
}

static void pit_delay_us(u64 n)
{
    u64 reload_value = PIT_MAX_HZ / PIT_DIVISOR_HZ;
    i64 rem_ticks = n * PIT_MAX_HZ / 1000000; // Number of ticks required for `n` us to pass.
    u64 cur_tick = 0;
    u64 prev_tick = pit_gettick();

    while (rem_ticks > 1) {
        cur_tick = pit_gettick();
        if (cur_tick > prev_tick) // The counter wrapped around to the reload value.
            rem_ticks -= reload_value - (cur_tick - prev_tick);
        else // The counter was decremented as usual.
            rem_ticks -= prev_tick - cur_tick;
        prev_tick = cur_tick;
    }
}

///////////////////////////////////////////////////////////////////////////////
// TSC-based time                                                            //
///////////////////////////////////////////////////////////////////////////////

static u64 global_tsc_base;
static u64 global_tsc_freq_hz;
static bool global_time_initialized;

void time_init(void)
{
    assert(!global_time_initialized);

    // This technique of calibrating the TSC is from Unikraft (like the PIT code above). The idea is that we can use
    // the PIT to wait for a known amount of time and count how many ticks were counted in the TSC in this interval.

    // Initialize PIT channel 0 to rate generation mode with a reload value of PIT_MAX_HZ / PIT_DIVISOR_HZ.
    outb(PIT_PORT_CMD, PIT_CMD_RATEGEN | PIT_CMD_ACCESS_HILO); // Bits 6 and 7 are zero which selects channel 0.
    outb(PIT_PORT_CHAN0, (PIT_MAX_HZ / PIT_DIVISOR_HZ) & 0xff);
    outb(PIT_PORT_CHAN0, (PIT_MAX_HZ / PIT_DIVISOR_HZ) >> 8);

    u64 base = rdtsc();
    pit_delay_us(100000); // 0.1 seconds
    u64 freq_est = (rdtsc() - base) * 10;

    print_dbg(PINFO, STR("TSC frequency estimate: %lu Hz\n"), freq_est);

    global_tsc_base = base;
    global_tsc_freq_hz = freq_est;

    global_time_initialized = true;
}

struct time_ms time_current_ms(void)
{
    assert(global_time_initialized);

    u64 elapsed_ticks = rdtsc() - global_tsc_base;
    assert(!MUL_OVERFLOW(elapsed_ticks, 1000));
    return time_ms_new((elapsed_ticks * 1000) / global_tsc_freq_hz);
}
