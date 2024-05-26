#include <string.h>
#include <vga.h>
#include <arena.h>
#include <fmt.h>
#include <idt.h>

#include <assert.h>

// TODO:
// - Printing to serial console (create a unified kprint that prints to console and vga)

#define global_kernel_arena_buffer_size 10000
u8 global_kernel_arena_buffer[global_kernel_arena_buffer_size];

void kernel_start(void)
{
    struct arena arn = {
        .beg = global_kernel_arena_buffer,
        .end = global_kernel_arena_buffer + global_kernel_arena_buffer_size,
    };

    vga_clear_screen();

    struct arena scratch = {
        .beg = NEW(&arn, u8, 100),
        .end = scratch.beg + 100,
    };

    struct fmt_buf buf = NEW_FMT_BUF(&arn, 4000);

    fmt_str(STR("Wow, this is the smallest number in the world: "), &buf);
    fmt_i64(-9223372036854775807, &buf, scratch);
    fmt_str(STR(" And this is a pointer: "), &buf);
    fmt_ptr(&buf, &buf);
    vga_println(FMT_2_STR(buf));
    FMT_CLEAR(&buf);

    fmt_str(STR("And here are all the types:"), &buf);
    vga_println(FMT_2_STR(buf));
    FMT_CLEAR(&buf);

    fmt_i8(-1, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_i16(-2, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_i32(-3, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_i64(-4, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_sz(-646464, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_u8(1, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_u16(2, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_u32(3, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_u64(4, &buf, scratch); fmt_str(STR(", "), &buf);
    fmt_ptr((void*)0xdeadbeef, &buf);
    vga_println(FMT_2_STR(buf));

    init_interrupts();

    __asm__ volatile ("int $0x22");
    __asm__ volatile ("int3");
}
