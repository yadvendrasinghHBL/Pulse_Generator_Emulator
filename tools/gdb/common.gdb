# GDB setup, loaded by tools/debug.sh.
# Connects to ST-LINK_gdbserver, flashes the debug ELF and starts the app at its reset vector.
set pagination off
set confirm off
target extended-remote localhost:61234
monitor reset
load

# BOOT0 can be high whenever the serial port is closed (docs/HARDWARE.md finding 2), so after a
# reset the core may sit in the ROM bootloader. Start the app from its own vector table instead.
# SystemInit() points VTOR at flash (USER_VECT_TAB_ADDRESS), as after the bootloader's Go.
set $sp = *(unsigned int *)0x08000000
set $pc = *(unsigned int *)0x08000004

# pulse: TIM2 (base 0x40000000) state and the PA5 / J8.1 pin level (GPIOA IDR 0x48000010, bit 5)
define pulse
  printf "TIM2 CR1 0x%04x  CNT %u  ARR %u  CCMR1 0x%05x  DIER 0x%x\n", *(unsigned int *)0x40000000, *(unsigned int *)0x40000024, *(unsigned int *)0x4000002C, *(unsigned int *)0x40000018, *(unsigned int *)0x4000000C
  printf "J8.1 (PA5) level %d\n", (*(unsigned int *)0x48000010 >> 5) & 1
end
document pulse
Show the TIM2 registers used by the pulse output and the J8.1 (PA5) pin level.
end
