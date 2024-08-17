#include <tx/com.h>
#include <tx/idt.h>
#include <tx/print.h>

#define global_kernel_arena_buffer_size 10000
u8 global_kernel_arena_buffer[global_kernel_arena_buffer_size];

void kernel_init(void)
{
    com_init(COM1_PORT);
    interrupt_init();

    print_str(STR(" ______   ______    ______   __    __  __\n"
                  "/\\__  _\\ /\\  __ \\  /\\__  _\\ /\\ \\  /\\_\\_\\_\\\n"
                  "\\/_/\\ \\/ \\ \\  __ \\ \\/_/\\ \\/ \\ \\ \\ \\/_/\\_\\/_\n"
                  "   \\ \\_\\  \\ \\_\\ \\_\\   \\ \\_\\  \\ \\_\\  /\\_\\/\\_\\\n"
                  "    \\/_/   \\/_/\\/_/    \\/_/   \\/_/  \\/_/\\/_/\n"));

    __asm__ volatile("int $0x22");
    __asm__ volatile("int3");
}
