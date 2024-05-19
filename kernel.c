char *msg = "Hello, world!";

void _start(void)
{
    char *vga_mem = (char *) 0xb8000;
    for (int i = 0; i < 13; i++)
        vga_mem[i * 2] = msg[i];
}
