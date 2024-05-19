In 16-bit real mode, everything is nice and simple. The world is good and we are happy.
But at once evil programs appear and we realize that a means to protect our memory is
desperately needed.

The BIOS loads the first sector of some disk that ends with the magic number `0xaa55`.
That's the boot sector. It's copied into memory and jumped to. That's how our bootloader
comes alive in 16-bit real mode.

Before switching to protected mode, the Global Descriptor Table has to be set up so that
protected memory segmentation can be used. See `gdt.s` for further notes on that.

TODO: Caching needs to be enabled manually after a reset. See 10.3 in Volume 3A of
the IA-32 Manual.

TODO: To complete the rest of the initialization, refer to section 10.8 in Volume
3A of the IA-32 Manual.

TODO: Before the switch to protected mode, we disable interrupts. We need to
re-enable them manually. Before doing so, we also need to provide an interrupt
descriptor table.

TODO: Do I need to load extra stuff from the disk?

# Links

[Writing a Simple Operating System -- from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)
[Guide OS (1): A Guide to Developing an 64-Bit Operating System on x86](https://codetector.org/post/guideos/1_intro_and_setuup/)
[Setting Up Long Mode](https://wiki.osdev.org/Setting_Up_Long_Mode)
