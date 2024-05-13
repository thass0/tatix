The most useful unfinished document in existence:

[Writing a Simple Operating System -- from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)

In 16-bit real mode, everything is nice and simple. The world is good and we are happy.
But at once evil programs appear and we realize that a means to protect our memory is
desperately needed.

The BIOS loads the first sector of some disk that ends with the magic number `0xaa55`.
That's the boot sector. It's copied into memory and jumped to. That's how our bootloader
comes alive in 16-bit real mode.

Before switching to protected mode, the Global Descriptor Table has to be set up so that
protected memory segmentation can be used. See `gdt.s` for further notes on that.
