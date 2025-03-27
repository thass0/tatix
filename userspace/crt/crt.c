#include <tx/base.h>

extern int main(void);

__noreturn void _start(void)
{
    main();
    while (1)
        __asm__ volatile("pause"); // Can't use htl because it's privileged.
}
