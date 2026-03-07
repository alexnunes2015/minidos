set pagination off
set confirm off
set architecture i386
file build/kernel.elf
target remote :1234
echo Connected to MiniDOS kernel debug target.\n
echo Useful breakpoints: kernel_main, paging_init, interrupts_init, scheduler_phase5_self_test\n
