#include <tx/arena.h>
#include <tx/com.h>
#include <tx/fmt.h>
#include <tx/idt.h>
#include <tx/string.h>
#include <tx/vga.h>

#include <tx/assert.h>

// TODO:
// - Printing to serial console (create a unified kprint that prints to console and vga)

#define global_kernel_arena_buffer_size 10000
u8 global_kernel_arena_buffer[global_kernel_arena_buffer_size];

void kernel_start(void)
{
    struct arena arn = arena_new(global_kernel_arena_buffer, global_kernel_arena_buffer_size);
    struct str_buf buf = fmt_buf_new(&arn, 1000);

    vga_clear_screen();
    com_init(COM1_PORT);
    com_write(COM1_PORT, STR("Hello, world\n"));

    fmt(&buf, "Wow, this is the smallest number in the world: %lld And this is a pointer: %llx", -9223372036854775807LL,
        (unsigned long long)&buf);
    vga_println(str_from_buf(buf));
    str_buf_clear(&buf);

    fmt(&buf, "And here are all the types: %hhd, %hd, %d, %ld, %lld, %hhu, %hu, %u, %lu, %llx", -1, -2, -3, -4L,
        -29583LL, 1, 2, 3, 4LU, 0xdeadbeefLLU);
    vga_println(str_from_buf(buf));
    str_buf_clear(&buf);

    init_interrupts();

    __asm__ volatile("int $0x22");
    __asm__ volatile("int3");
}
