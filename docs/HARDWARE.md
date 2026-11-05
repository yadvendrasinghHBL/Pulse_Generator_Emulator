# Hardware reference for firmware

This firmware runs on the **Handoff Simulator rev A** board. Only the MCU, the USB console and
expansion header **J8** are used; the RF attenuator section is not used.

Source of truth: `Handoff_Simulator/hardware/1_Handoff_Simulator/DesignFiles/Handoff_Simulator.kicad_pro`
(sheet `STM32`). The pin rows below were extracted from the KiCad netlist with
`Handoff_Simulator/software/board_bringup/tools/extract_pinmap.py` (2026-11-05). Re-run it after any
schematic change and diff against this file and `Core/Inc/board.h`.

## Block diagram

```
USB-A J7 ─ USB6B1 ESD (U2) ─┬─ FT231XS USB-UART (U4) ── USART1 ── STM32L451RCT6 (U1) ── J8 (1x8 header)
                            │     DTR# ─D1▶─ NRST          │           PA5  → J8.1  pulse output 1 (TIM2_CH1)
                            │     RTS# ─D2▶─ BOOT0 (R2 10k↓)│           PB0/PB1 → J8.6/.7 outputs A/B (TIM1); PA6/PA7 → J8.2/.3 inputs A/B (TIM3)
                            └─ LDO40LPU33 (U3) → Vdrive 3.3 V (MCU, FT231X VCCIO, attenuators)

QPC6713 attenuators IC2..IC9: not used. Their LE (PB6) is driven low and kept low (finding 4).
```

## MCU

| Item | Value |
|------|-------|
| Part | **STM32L451RCT6**: Cortex-M4F, 256 KB flash, 160 KB RAM, LQFP64 |
| DEV_ID | 0x462 (checked by POST) |
| Clock | **No crystals** (PH0/PH1/PC14/PC15 not connected). HSI16 → PLL → 80 MHz |
| Supply | `Vdrive` 3.3 V from U3; VDDA via ferrite L1 + C2; VBAT tied to Vdrive |
| Debug | J6 2×2: 1 SWCLK (PA14), 2 GND, 3 SWDIO (PA13), 4 NRST. **No VREF/VCC pin** |

## Pin map (pins this firmware uses or must hold)

| MCU pin | Port | Function in firmware | Net / destination |
|--------:|------|----------------------|-------------------|
| 7  | NRST | reset | J6.4, C1, D1 (from FT231X DTR#) |
| 21 | PA5  | **pulse output 1, TIM2_CH1 (AF1)** | J8.1 |
| 42 | PA9  | USART1_TX (AF7) | FT231X RXD (U4.4) |
| 43 | PA10 | USART1_RX (AF7) | FT231X TXD (U4.20) |
| 46 | PA13 | SWDIO | J6.3 |
| 49 | PA14 | SWCLK | J6.1 |
| 51 | PC10 | LED green D5 (active high, 470R) | R4 |
| 53 | PC12 | LED orange D3 (active high, 470R) | R3 |
| 58 | PB6  | attenuator LE, **held low** (not used otherwise) | IC2–IC9 pin 12 |
| 60 | PH3/BOOT0 | boot select, readable as GPIO | R2 10k to GND, D2 from FT231X RTS# |

PB3/PB5 (attenuator CLK/SI) and all other GPIOs are left in their reset (analog) state.

## J8 expansion header (1x8, `Conn_01x08_Pin`)

| J8 pin | MCU pin | Port | Timer channel (AF) | Use |
|-------:|--------:|------|--------------------|-----|
| 1 | 21 | PA5 | TIM2_CH1 (AF1), 32-bit timer | **pulse output 1 (stage 1)** |
| 2 | 22 | PA6 | TIM3_CH1 (AF2) | receiver input A (planned, stage 4) |
| 3 | 23 | PA7 | TIM3_CH2 (AF2); TIM1_CH1N (AF1) | receiver input B (planned, stage 4) |
| 4 | 24 | PC4 | (none) | unused |
| 5 | 25 | PC5 | (none) | unused |
| 6 | 26 | PB0 | TIM1_CH2N (AF1); TIM3_CH3 (AF2) | **sensor output A** (TIM1_CH2N, stage 2) |
| 7 | 27 | PB1 | TIM1_CH3N (AF1); TIM3_CH4 (AF2) | **sensor output B** (TIM1_CH3N, stage 2) |
| 8 | 28 | PB2 | (none) | unused |

Timer/AF columns are from the STM32L451 datasheet (DS11912) alternate-function table.

- **J8 has no GND pin.** All 8 pins go to the MCU. Take the ground reference from **J6 pin 2**
  (SWD header) or another ground point on the board.
- Levels are **3.3 V CMOS** (Vdrive), a few mA per pin. Not isolated, no protection. Connect a scope,
  logic analyser or 3.3 V logic input directly (a 100–330 Ω series resistor is a good idea). A real
  train receiver (15–24 V or 7–16 mA current loop) needs an output stage (PLAN.md stage 4).
- Nothing is driven on J8 at power-up: PA5 stays in its reset (analog) state until the first `start`.

## Findings from the schematic review (check on the first board)

> Copied unchanged from `Handoff_Simulator/software/board_bringup/docs/HARDWARE.md` (2026-11-05), because
> this firmware runs on the same rev A boards. References to `PLAN` items, decisions D1–D5,
> `bringup_test.py` and `att_init()` point to that project. Here the attenuator LE is held low by
> `board_hold_att_latch()` in `main.c`; the console tools are `tools/con_send.sh` and `tools/pulse_test.py`.
> Findings that matter most for this project: **2** (BOOT0 / option bytes) and **3** (open the port with DTR off).

1. **MCU value field is wrong.** The schematic value reads `STM32L152RET6`. The BOM MPN is
   STM32L451RCT6 and the symbol's pin names (pin 1 VBAT, pin 60 PH3-BOOT0) are L451.
   The firmware targets the **L451RC**, as confirmed by the designer. Fix the value field. POST checks DEV_ID 0x462.
2. **BOOT0 can be high at power-up (auto-boot circuit).** D2 has its anode on FT231X RTS# and its
   cathode on BOOT0, with R2 10k to GND. RTS# idles **high** whenever no program has the port open
   with RTS asserted, which pulls BOOT0 to about 3 V. With factory option bytes (nSWBOOT0 = 1) the
   MCU then starts the **ROM bootloader instead of the application** after power-up or reset.
   - To check: measure BOOT0 (pin 60) with USB connected and no terminal open. Then power-cycle
     with no terminal open and see whether the green heartbeat LED runs.
   - Possible fix: program option bytes `nSWBOOT0=0 nBOOT0=1` so the BOOT0 pin is ignored. SWD
     still works, and the firmware `bootloader` command still allows UART flashing. **Option bytes are
     changed only by a human** (see AGENTS.md).
   - **Bench, board 1 (2026-10-30): confirmed.** With GPIOH clocked and PH3 set to a digital input
     over SWD, PH3 reads **0 with RTS asserted and 1 with RTS released or the port closed**. It still reads
     1 with the internal pull-down on, so RTS# drives it high through D2. After an SWD reset with the
     port closed, the MCU runs the ROM bootloader (PC 0x1FFF32FA, SYSCFG_MEMRMP = 1). Holding the port open
     (RTS asserted) and resetting with `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -rst` boots the
     app normally. A GND jumper "on BOOT0" did not pull PH3 low, so it was not on the pin-60 net.
     `-hardRst` (NRST on J6.4) did not reset the MCU on this setup. Use `-rst` (SYSRESETREQ). An earlier
     note that BOOT0 stayed high with RTS asserted came from such a non-reset and was wrong.
   - **Manual check with picocom, board 1:** RTS lowered (`C-a C-g`) + `reset` → console silent
     (ROM bootloader). RTS raised + SWD `-rst` with picocom still open → app banner, POST PASSED.
     Closing picocom drops RTS, so any reset after closing boots the bootloader. Working rule until
     D1 is decided: keep a terminal open with RTS up whenever the board resets, or use
     `tools/bringup_test.py --reset`.
   - **Cold power-up, board 1 (2026-11-02): the app starts.** USB unplugged and replugged with no
     terminal open: lamp test, then green heartbeat. SWD read afterwards: PC 0x08001870 (flash),
     SYSCFG_MEMRMP = 0, yet PH3 reads 1. Likely the FT231X does not drive RTS# high yet when the MCU
     samples BOOT0 at power-on (not proven; a scope on pin 60 vs Vdrive at power-up would show it).
     So the problem hits **resets after power-up** with the port closed (`reset`, SWD, NRST,
     brown-out, a future watchdog), not a plain power-up.
   - **Decision D1 (2026-11-02): applied on board 1.** Option bytes `nSWBOOT0=0 nBOOT0=1`
     (`STM32_Programmer_CLI -c port=SWD -ob nSWBOOT0=0 nBOOT0=1`, OPTR 0xFFFFF8AA → 0xFBFFF8AA).
     The BOOT0 pin is now ignored: an SWD reset with the port closed (PH3 = 1) boots the app. Apply the
     same change to every rev A board after its first flash. Undo with `-ob nSWBOOT0=1`. The `bootloader`
     command jumps to system memory in software, so UART flashing does not need the BOOT0 pin. Rev B
     should fix the RTS#→BOOT0 circuit (PLAN B2).
   - **UART flashing works (board 1):** `bootloader` command → close terminal → `flash_uart`
     (bootloader protocol 3.1, write + verify about 15 s). The bootloader's Go command leaves
     SYSCFG_MEMRMP = 1 (system flash at 0), so the firmware sets VTOR to 0x08000000 in `SystemInit()`
     (`USER_VECT_TAB_ADDRESS` in CMakeLists.txt). Without that, interrupts after Go would use the ROM's
     vector table. With it, `uwTick` runs after Go and `bringup_test.py` passes.
3. **D1: DTR# low holds the MCU in reset on board 1 (opposite of the schematic).** Original review note: D1 has its anode on DTR# and its cathode on NRST. A diode in that
   direction can only source current into NRST and can never pull it low. If DTR-driven reset was
   intended, D1 must be reversed (cathode to DTR#). Until then, reset over USB is not possible. Use
   SWD NRST, a power cycle, or the `reset` command.
   **Bench, board 1 (2026-11-02), first test: no reset** with 10 DTR pulses. **Invalid:** the ST-LINK was
   connected to J6 and held NRST high.
   **Bench, board 1 (2026-11-02), ST-LINK unplugged (user):** opening picocom (DTR asserted, DTR# low)
   **stops the green heartbeat and the board does not answer**; `picocom --lower-dtr` works normally.
   So on board 1 **DTR# low holds the MCU in reset**, the opposite of what the schematic predicts.
   Not yet explained: D1 fitted reversed, a different part, or a schematic/footprint pin mismatch.
   To check: D1 orientation (cathode band) and diode-mode reading; NRST (J6.4) voltage with the port open.
   Effect: any terminal that asserts DTR (picocom default, PuTTY, Windows FTDI driver) stops the board
   while it is open, unless an ST-LINK holds NRST high. Decision D2 reopened.
   **Open-pulse test (ST-LINK unplugged, 2026-11-02):** pyserial opened with `dtr=False` 4 times: no boot
   banner, `up` 19 → 25 → 31 → 37 s (no reset). `picocom --lower-dtr` 3 times: `up` 18 → 21 → 25 s (no reset).
   pyserial with DTR on: no answer; switching DTR off while open: boot banner, `up 4 s`. So opening the
   port with DTR off is safe; DTR on = reset held; DTR released = MCU boots.
   **D2 decided by the user (2026-11-02): both.** (b) now: every tool opens the port with DTR off
   (`bringup_test.py`, `con_send.py`, Zed picocom tasks with `--lower-dtr`, `flash_uart` with `dtr=low`).
   (a) later: remove D1 on rev A boards (then DTR has no effect; PuTTY works). Rev B: B3.
4. **LE has no pull-down.** The datasheet requires LE low at power-up. While the MCU is held in reset
   or before `att_init()` runs, PB6 floats. Firmware drives LE low first thing in `main()`. A 10k–100k
   pull-down on the LE net is recommended for the next revision.
5. **J6 has no VREF pin.** ST-LINK/V3 and genuine ST-LINK/V2 probes sense target voltage on
   VREF/T_VCC. Wire that probe pin to Vdrive (any 3.3 V decoupling cap), or the probe may refuse to
   connect.
6. **FT231X CBUS0 is tied to GND through R9 (0 Ω). Confirmed on board 1, fixed in EEPROM (D5).** Read the FT231X EEPROM with
   FT_PROG. If CBUS0 is configured as an output (for example TXDEN), it will drive against GND.
   Set it to an input or tristate function, or remove R9.
   **Bench, board 1 (2026-11-02): confirmed.** EEPROM read with pyftdi (read-only): CBUS0 = **TXDEN**
   (factory default, raw byte 0x1A = 0x09), CBUS1 TXLED, CBUS2 RXLED, CBUS3 SLEEP. TXDEN is an output that
   goes high while the FT231X transmits (host → MCU), so on every received byte it drives into the 0 Ω R9
   to GND. UART tests are still clean (0 errors), but the pin is fighting a short. Fix: decision D5 in PLAN.md.
   **Fixed on board 1 (D5 option a, 2026-11-02):** EEPROM CBUS0 set to TRISTATE with pyftdi (byte 0x1A 0x09→0x00 plus CRC).
   Takes effect after a USB power cycle. Factory image: `docs/ft231x_eeprom_board1_factory.bin`. Every new rev A board needs
   the same change (FT231X ships with CBUS0 = TXDEN). Rev B: leave CBUS0 unconnected or remove R9 (PLAN B6).
7. FT231X VCCIO and RESET# are tied to Vdrive through R8 (0 Ω), so the UART levels are 3.3 V and match the MCU.
