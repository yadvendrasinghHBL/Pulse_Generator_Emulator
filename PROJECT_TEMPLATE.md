# Reusable STM32 firmware project template

Extracted from `Handoff_Simulator/software/board_bringup` (STM32L451, CMake + Ninja, HAL, no CubeMX).
Everything board-specific has been replaced with placeholders. Copy the parts you need into a new project
and replace:

| Placeholder | Meaning | Example from the source project |
|---|---|---|
| `<project>` | CMake project / image name | `handoff_bringup` |
| `<Project Name>` | human-readable name | `Handoff Simulator` |
| `<MCU>` / `<mcu_lower>` | exact part / CMSIS device file | `STM32L451RCT6` / `stm32l451xx` |
| `<FAMILY>` / `<family>` | ST family | `STM32L4xx` / `stm32l4xx` |
| `<CUBE_PKG>` | STM32Cube firmware package | `STM32Cube_FW_L4_V1.18.2` |
| `<CPU_FLAGS>` | core flags | `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard` |
| `<FLASH_KB>` / `<RAM_KB>` | memory sizes | `256` / `160` |
| `<PORT>` | console serial port | `/dev/ttyUSB0` |
| `<gh-user>/<repo>` | GitHub repository | |

---

## 1. Requirements (host machine)

| Tool | Version used | Why |
|---|---|---|
| Arm GNU toolchain `arm-none-eabi-gcc` | 14.3.rel1 | compiler; does **not** need to be on PATH (toolchain file finds it) |
| CMake | ≥ 3.22 | build system, presets |
| Ninja | any | generator |
| STM32CubeProgrammer (`STM32_Programmer_CLI`) | any recent | flash over SWD (ST-LINK) or UART ROM bootloader |
| ST-LINK_gdbserver + `arm-none-eabi-gdb` | STM32Cube bundles | debugging (`tools/debug.sh`) |
| STM32Cube firmware package | `<CUBE_PKG>` | source of HAL + CMSIS (copied once by `tools/vendor_cube.sh`) |
| Python 3 + venv with `pyserial` | 3.12 | host tools (console sender, on-target tests) |
| picocom | any | interactive serial terminal |
| clangd | any | code navigation (uses `build/debug/compile_commands.json`) |
| Editor | Zed (tasks provided), any EditorConfig editor works | |
| udev rule for ST-LINK | `tools/udev/60-st-link.rules` | access the probe without sudo |

```bash
# one-time setup
sudo apt install cmake ninja-build picocom python3-venv
python3 -m venv ~/.venvs/<project> && ~/.venvs/<project>/bin/pip install pyserial
sudo install -m 644 tools/udev/60-st-link.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG dialout $USER     # serial port access (log out/in)
```

---

## 2. Directory layout

```
<project>/
├── AGENTS.md                 rules for AI coding agents (build, layout, safety, style)
├── CLAUDE.md                 "@AGENTS.md" + Claude Code specifics
├── README.md                 short landing page: quick start + table of docs + table of tools
├── CHANGELOG.md              user-visible changes per version (Keep a Changelog)
├── PLAN.md                   phases, status, decisions needed, risks, engineering log
├── CMakeLists.txt
├── CMakePresets.json         debug / release presets
├── cmake/arm-none-eabi.cmake toolchain file
├── <MCU>_FLASH.ld            linker script (reserve last flash page for settings if needed)
├── .clangd  .editorconfig  .gitattributes  .gitignore
├── Core/
│   ├── Inc/  board.h (ALL pins, single source of truth), version.h, <family>_hal_conf.h, module headers
│   ├── Src/  main.c, console.c, cli.c, diag.c, cfg.c, <family>_it.c, syscalls.c, system_<family>.c
│   └── Startup/startup_<mcu_lower>.s
├── Drivers/                  vendored CMSIS + HAL subset — DO NOT EDIT (re-run tools/vendor_cube.sh)
├── docs/
│   ├── HARDWARE.md           hardware facts for firmware, pin map, known schematic issues
│   ├── BRINGUP_CHECKLIST.md  bench procedure, one row per check
│   ├── USER_MANUAL.md        engineers: terminal, every command, tests, firmware update
│   ├── END_USER_GUIDE.md/.pdf customers: user commands only
│   └── test_reports/         release test reports (generated), measurement sheets (CSV)
├── tools/
│   ├── build_all.sh          definition-of-done build (all configs, fail on any warning)
│   ├── build_and_flash.sh    clean build → artifacts/ → flash → version check
│   ├── release_test.sh       build + flash + all feature tests → Markdown report
│   ├── vendor_cube.sh        copy HAL/CMSIS from the Cube package into Drivers/
│   ├── con_send.sh / .py     send one console command, print reply (pyserial)
│   ├── bringup_test.py       on-target automatic tests over the console
│   ├── debug.sh, gdb/        ST-LINK_gdbserver + GDB session
│   └── udev/60-st-link.rules
├── artifacts/v<ver>-<hash>/  released images (.hex/.bin/.elf/.map, SHA256SUMS, BUILD_INFO.txt)
├── .github/workflows/build.yml   CI: build on every push; tag v<ver> → GitHub release
├── .github/dependabot.yml
├── .zed/settings.json, .zed/tasks.json
└── .claude/settings.json, .claude/hooks/guard_bash.py, .claude/skills/{release,bench}/SKILL.md
```

Principles that made this layout work:

- **No CubeMX `.ioc`.** Peripheral init is hand-written HAL code inside the module that owns the peripheral.
- **`board.h` is the only place pins are defined**, every pin traceable to the schematic.
- **Vendor code is vendored, not edited**, and compiled with `-w`; our code must have **zero warnings**.
- **One fact, one place:** commands in USER_MANUAL, hardware in HARDWARE.md, status/history in PLAN.md.
- **Console output is a contract:** `OK` / `ERR` prefixes, `POST PASSED` / `POST FAILED`, `[PASS]` / `[FAIL]`,
  and the prompt `> ` stay stable because host scripts parse them.

---

## 3. Build system

### 3.1 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(<project> C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Optional build switches, e.g. hardware peripheral vs. GPIO bit-bang
# option(USE_HW_SPI "Use SPI peripheral instead of bit-banging" ON)

set(MCU_FLAGS <CPU_FLAGS>)
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/<MCU>_FLASH.ld)

set(HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/<FAMILY>_HAL_Driver)
# Only the HAL modules you use (keep in sync with <family>_hal_conf.h and tools/vendor_cube.sh)
set(HAL_MODULES hal hal_cortex hal_dma hal_dma_ex hal_exti hal_flash hal_flash_ex
                hal_flash_ramfunc hal_gpio hal_pwr hal_pwr_ex hal_rcc hal_rcc_ex
                hal_uart hal_uart_ex)
list(TRANSFORM HAL_MODULES PREPEND ${HAL_DIR}/Src/<family>_)
list(TRANSFORM HAL_MODULES APPEND .c)

file(GLOB APP_SOURCES CONFIGURE_DEPENDS Core/Src/*.c)

add_executable(${PROJECT_NAME}
  ${APP_SOURCES}
  Core/Startup/startup_<mcu_lower>.s
  ${HAL_MODULES})
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX .elf)

target_include_directories(${PROJECT_NAME} PRIVATE
  Core/Inc
  ${HAL_DIR}/Inc
  Drivers/CMSIS/Device/ST/<FAMILY>/Include
  Drivers/CMSIS/Include)

# USER_VECT_TAB_ADDRESS: SystemInit() points VTOR at our flash vector table
# (needed when the app is started by the ROM bootloader's Go command).
target_compile_definitions(${PROJECT_NAME} PRIVATE
  <MCU_DEFINE e.g. STM32L451xx> USE_HAL_DRIVER USER_VECT_TAB_ADDRESS)

target_compile_options(${PROJECT_NAME} PRIVATE
  ${MCU_FLAGS}
  -ffunction-sections -fdata-sections -fno-common
  $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-unused-parameter>
  $<$<CONFIG:Debug>:-Og -g3>
  $<$<CONFIG:Release>:-Os -g>)

# Vendor code is not ours to fix: silence its warnings
set_source_files_properties(${HAL_MODULES} Core/Src/system_<family>.c
  PROPERTIES COMPILE_OPTIONS "-w")

target_link_options(${PROJECT_NAME} PRIVATE
  ${MCU_FLAGS}
  -T${LINKER_SCRIPT}
  --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections -Wl,--print-memory-usage
  -Wl,-Map=${PROJECT_NAME}.map -Wl,--no-warn-rwx-segments)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
  VERBATIM)

# --- Flashing (STM32CubeProgrammer CLI) -------------------------------------
find_program(STM32_PROGRAMMER_CLI STM32_Programmer_CLI
  PATHS $ENV{HOME}/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin)
set(FLASH_UART_PORT /dev/ttyUSB0 CACHE STRING "Serial port for the ROM bootloader")

if(STM32_PROGRAMMER_CLI)
  add_custom_target(flash            # SWD via ST-LINK
    COMMAND ${STM32_PROGRAMMER_CLI} -c port=SWD mode=UR reset=HWrst
            -w ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex -v -rst
    DEPENDS ${PROJECT_NAME} USES_TERMINAL)
  add_custom_target(flash_uart       # ROM bootloader over UART
    COMMAND ${STM32_PROGRAMMER_CLI} -c port=${FLASH_UART_PORT} br=115200 dtr=low
            -w ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex -v -g 0x08000000
    DEPENDS ${PROJECT_NAME} USES_TERMINAL)
endif()
```

### 3.2 `CMakePresets.json`

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/debug",
      "toolchainFile": "${sourceDir}/cmake/arm-none-eabi.cmake",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "release",
      "inherits": "debug",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    }
  ],
  "buildPresets": [
    { "name": "debug", "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" }
  ]
}
```

### 3.3 `cmake/arm-none-eabi.cmake`

```cmake
# CMake toolchain file for arm-none-eabi-gcc (bare-metal Cortex-M).
# Looked up on PATH, then in ARM_TOOLCHAIN_PATH (env or -D), then in known install locations.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ARM_TOOLCHAIN_PATH AND DEFINED ENV{ARM_TOOLCHAIN_PATH})
  set(ARM_TOOLCHAIN_PATH "$ENV{ARM_TOOLCHAIN_PATH}")
endif()

find_program(ARM_GCC arm-none-eabi-gcc
  HINTS ${ARM_TOOLCHAIN_PATH} ${ARM_TOOLCHAIN_PATH}/bin
  PATHS
    /usr/local/arm-gnu-toolchain/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi/bin
    $ENV{HOME}/.local/share/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin
  REQUIRED)
get_filename_component(_tc_bin "${ARM_GCC}" DIRECTORY)

set(CMAKE_C_COMPILER   ${_tc_bin}/arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER ${_tc_bin}/arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      ${_tc_bin}/arm-none-eabi-objcopy CACHE FILEPATH "")
set(CMAKE_SIZE         ${_tc_bin}/arm-none-eabi-size    CACHE FILEPATH "")

# Do not try to link a host executable during compiler checks
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### 3.4 Linker script: memory section

Start from ST's template for your part, then reserve the last flash page if you keep settings in flash:

```ld
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x1000;

MEMORY
{
  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = <RAM_KB>K
  /* Last flash page is reserved for user settings (cfg.c) */
  ROM (rx)  : ORIGIN = 0x08000000, LENGTH = <FLASH_KB - page size>K
}
```

### 3.5 Daily commands

```bash
cmake --preset debug   && cmake --build --preset debug     # -> build/debug/<project>.{elf,hex,bin}
cmake --preset release && cmake --build --preset release
cmake --build --preset debug --target flash                # SWD
cmake --build --preset debug --target flash_uart           # ROM bootloader over UART
tools/build_all.sh                                         # definition of done: all configs, 0 warnings
tools/build_and_flash.sh [--no-flash]                      # release run
picocom -b 115200 --lower-dtr <PORT>                       # console (exit Ctrl+A Ctrl+X)
```

---

## 4. Editor / repo hygiene files

### `.clangd`
```yaml
# Use the debug build's compile database so navigation matches the real arm build.
CompileFlags:
  CompilationDatabase: build/debug
Diagnostics:
  Suppress: [drv_unknown_argument, drv_unsupported_opt_for_target]
---
If:
  PathMatch: Drivers/.*
Diagnostics:
  Suppress: "*"
```

### `.editorconfig`
```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true
indent_style = space
indent_size = 2

[*.{py,ld}]
indent_size = 4

[*.md]
trim_trailing_whitespace = false

[{CMakeLists.txt,*.cmake}]
indent_size = 2

[Drivers/**]
# vendored ST code: keep as shipped
charset = unset
end_of_line = unset
insert_final_newline = unset
trim_trailing_whitespace = unset
indent_style = unset
indent_size = unset
```

### `.gitattributes`
```gitattributes
* text=auto eol=lf
*.sh  text eol=lf
*.py  text eol=lf
*.pdf binary
*.bin binary
*.hex binary
*.elf binary
*.png binary
# Vendored ST code: byte-for-byte as shipped, hidden from GitHub stats and diffs
Drivers/** -text linguist-vendored -diff
```

### `.gitignore`
```gitignore
build/
__pycache__/
*.pyc
.cache/
compile_commands.json
.vscode/
# release images are kept in git, except builds from uncommitted code
artifacts/*-dirty/
.claude/settings.local.json
.~lock.*#
~$*
```

### `.zed/settings.json`
```jsonc
{
  "lsp": {
    "clangd": {
      "binary": {
        "arguments": ["--query-driver=**/arm-none-eabi-gcc", "--header-insertion=never"]
      }
    }
  },
  "file_scan_exclusions": ["build", "**/__pycache__"]
}
```

### `.zed/tasks.json` (pattern — add one entry per script)
```jsonc
// Ctrl+Shift+P -> "task: spawn"
[
  { "label": "build: debug",   "command": "cmake --preset debug && cmake --build --preset debug",     "cwd": "$ZED_WORKTREE_ROOT" },
  { "label": "build: release", "command": "cmake --preset release && cmake --build --preset release", "cwd": "$ZED_WORKTREE_ROOT" },
  { "label": "build: all (definition of done, zero warnings)", "command": "tools/build_all.sh --keep", "cwd": "$ZED_WORKTREE_ROOT" },
  { "label": "flash: SWD debug build", "command": "cmake --build --preset debug --target flash", "cwd": "$ZED_WORKTREE_ROOT" },
  { "label": "flash: release run",     "command": "tools/build_and_flash.sh", "cwd": "$ZED_WORKTREE_ROOT" },
  { "label": "test: release test",     "command": "tools/release_test.sh",    "cwd": "$ZED_WORKTREE_ROOT" },
  { "label": "board: status",          "command": "tools/con_send.sh status", "cwd": "$ZED_WORKTREE_ROOT" },
  { "label": "serial: console", "command": "picocom -b 115200 --lower-dtr /dev/ttyUSB0",
    "cwd": "$ZED_WORKTREE_ROOT", "use_new_terminal": true },
  { "label": "debug: GDB over SWD", "command": "tools/debug.sh <target>",
    "cwd": "$ZED_WORKTREE_ROOT", "use_new_terminal": true }
]
```

---

## 5. Tool scripts

### 5.1 `tools/build_all.sh` — definition-of-done build

```bash
#!/usr/bin/env bash
# All build configurations from a clean build/ folder, failing on any compiler warning in our code.
# Used by build_and_flash.sh, CI and the editor task.
#   tools/build_all.sh            # clean
#   tools/build_all.sh --keep     # incremental
set -euo pipefail
cd "$(dirname "$0")/.."

[ "${1:-}" = "--keep" ] || rm -rf build
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
{
  cmake --preset debug   && cmake --build --preset debug &&
  cmake --preset release && cmake --build --preset release
  # add extra variants here, e.g.:
  # && cmake -B build/variant -G Ninja -DUSE_HW_SPI=OFF && cmake --build build/variant
} 2>&1 | tee "$LOG"
WARN=$(grep -c 'warning:' "$LOG" || true)
if [ "$WARN" != 0 ]; then
  echo "ERR $WARN compiler warning(s):" >&2
  grep 'warning:' "$LOG" >&2
  exit 1
fi
echo "OK all builds, 0 warnings"
```

### 5.2 `tools/build_and_flash.sh` — release run (skeleton)

Steps: (1) `build_all.sh` clean, (2) copy release `.hex/.bin/.elf/.map` to
`artifacts/v<FW_VERSION>-<git hash>[-dirty]/` with `SHA256SUMS` + `BUILD_INFO.txt`, (3) delete `build/`,
(4) flash via SWD if an ST-LINK is found, else via UART ROM bootloader, (5) ask the board for `status`
and check the version. Exit codes: 0 done, 1 error, 2 no board.

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
PORT=${PORT:-/dev/ttyUSB0}
FLASH=1; [ "${1:-}" = "--no-flash" ] && FLASH=0
PROG=${STM32_PROGRAMMER_CLI:-$(command -v STM32_Programmer_CLI || true)}
[ -n "$PROG" ] || PROG="$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"

tools/build_all.sh

VER=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' Core/Inc/version.h)
HASH=$(git rev-parse --short HEAD)
DIRTY=""; [ -z "$(git status --porcelain)" ] || DIRTY="-dirty"
ART="artifacts/v${VER}-${HASH}${DIRTY}"
rm -rf "$ART"; mkdir -p "$ART"
for ext in hex bin elf map; do cp "build/release/<project>.$ext" "$ART/"; done
(cd "$ART" && sha256sum <project>.* > SHA256SUMS)
{
  echo "firmware:  <Project Name> v$VER (release, -Os)"
  echo "git:       $(git rev-parse HEAD)${DIRTY:+ (uncommitted changes present)}"
  echo "built:     $(date '+%Y-%m-%d %H:%M:%S %z') by $(whoami)@$(hostname)"
  SIZE=$(grep -m1 '^CMAKE_SIZE:' build/release/CMakeCache.txt | cut -d= -f2)
  [ -x "$SIZE" ] && "$SIZE" build/release/<project>.elf
} > "$ART/BUILD_INFO.txt"
rm -rf build
[ "$FLASH" = 1 ] || { echo "OK artifacts in $ART (not flashed)"; exit 0; }

IMG="$ART/<project>.hex"
if "$PROG" -l st-link 2>/dev/null | grep -qiE 'st-?link sn'; then
  "$PROG" -c port=SWD mode=UR reset=HWrst -w "$IMG" -v -rst
elif [ -e "$PORT" ]; then
  tools/con_send.sh bootloader || exit 2     # firmware jumps to the ROM bootloader
  sleep 1
  "$PROG" -c port="$PORT" br=115200 dtr=low -w "$IMG" -v -g 0x08000000
else
  echo "ERR no board found; artifacts saved in $ART" >&2; exit 2
fi

sleep 1
REPLY=$(tools/con_send.sh status) && echo "$REPLY" | grep -q "^FW $VER," \
  && echo "OK v$VER flashed and running" || { echo "ERR board does not report FW $VER" >&2; exit 1; }
```

> Never use mass erase or option bytes in this script. Write only application pages, so a reserved
> settings page survives.

### 5.3 `tools/vendor_cube.sh` — copy HAL/CMSIS once

```bash
#!/usr/bin/env bash
# Copy the CMSIS + HAL files this project needs from a local STM32Cube package into Drivers/.
#   tools/vendor_cube.sh [path/to/<CUBE_PKG>]
set -euo pipefail
CUBE="${1:-$HOME/STM32Cube/Repository/<CUBE_PKG>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DST="$ROOT/Drivers"
FAM=<FAMILY>; fam=<family>; DEV=<mcu_lower>

[ -d "$CUBE/Drivers/${FAM}_HAL_Driver" ] || { echo "Cube package not found: $CUBE" >&2; exit 1; }

# keep in sync with CMakeLists.txt HAL_MODULES and Core/Inc/${fam}_hal_conf.h
HAL_SRC=(hal hal_cortex hal_dma hal_dma_ex hal_exti hal_flash hal_flash_ex hal_flash_ramfunc
         hal_gpio hal_pwr hal_pwr_ex hal_rcc hal_rcc_ex hal_uart hal_uart_ex)

rm -rf "$DST"
mkdir -p "$DST/CMSIS/Include" "$DST/CMSIS/Device/ST/$FAM/Include" \
         "$DST/${FAM}_HAL_Driver/Inc/Legacy" "$DST/${FAM}_HAL_Driver/Src"

cp "$CUBE"/Drivers/CMSIS/Include/{cmsis_compiler.h,cmsis_gcc.h,cmsis_version.h,core_cm4.h,mpu_armv7.h} "$DST/CMSIS/Include/"
cp "$CUBE/Drivers/CMSIS/LICENSE.txt" "$DST/CMSIS/"
cp "$CUBE"/Drivers/CMSIS/Device/ST/$FAM/Include/{${fam}.h,${DEV}.h,system_${fam}.h} "$DST/CMSIS/Device/ST/$FAM/Include/"
cp "$CUBE"/Drivers/${FAM}_HAL_Driver/Inc/*.h "$DST/${FAM}_HAL_Driver/Inc/"
rm -f "$DST/${FAM}_HAL_Driver/Inc/${fam}_hal_conf_template.h"
cp "$CUBE"/Drivers/${FAM}_HAL_Driver/Inc/Legacy/*.h "$DST/${FAM}_HAL_Driver/Inc/Legacy/"
for m in "${HAL_SRC[@]}"; do cp "$CUBE/Drivers/${FAM}_HAL_Driver/Src/${fam}_$m.c" "$DST/${FAM}_HAL_Driver/Src/"; done
cp "$CUBE/Drivers/${FAM}_HAL_Driver/LICENSE.txt" "$DST/${FAM}_HAL_Driver/"

# Startup + system file live in Core/ because projects commonly patch them
cp "$CUBE/Drivers/CMSIS/Device/ST/$FAM/Source/Templates/gcc/startup_${DEV}.s" "$ROOT/Core/Startup/"
cp "$CUBE/Drivers/CMSIS/Device/ST/$FAM/Source/Templates/system_${fam}.c" "$ROOT/Core/Src/"
basename "$CUBE" > "$DST/CUBE_VERSION"
echo "Vendored $(basename "$CUBE") into $DST"
```
(Adjust `core_cm4.h` to your core: `core_cm0plus.h`, `core_cm7.h`, `core_cm33.h` …)

### 5.4 `tools/con_send.py` — send one console command (pyserial)

Called through a 3-line wrapper `tools/con_send.sh` that picks the venv python:
```bash
#!/usr/bin/env bash
PY=${PYTHON:-$HOME/.venvs/<project>/bin/python}; [ -x "$PY" ] || PY=python3
exec "$PY" "$(dirname "$0")/con_send.py" "$@"
```

Core of `con_send.py`:
```python
#!/usr/bin/env python3
"""Send one command to the firmware shell and print the reply.
Exit: 0 = reply without ERR, 1 = ERR line, 2 = no answer / port busy."""
import os, sys, time
import serial

PROMPT = b"\r\n> "
QUIET_S = 1.0     # end of reply when no prompt comes
MAX_S = 60.0

def port_users(path):
    """Other processes holding the port (Linux lets two programs steal each other's bytes)."""
    users, real = [], os.path.realpath(path)
    for pid in filter(str.isdigit, os.listdir("/proc")):
        if int(pid) == os.getpid():
            continue
        try:
            for fd in os.listdir(f"/proc/{pid}/fd"):
                if os.path.realpath(f"/proc/{pid}/fd/{fd}") == real:
                    users.append(open(f"/proc/{pid}/comm").read().strip() + f" (PID {pid})")
                    break
        except OSError:
            pass
    return users

def open_port(path):
    p = serial.Serial()
    p.port, p.baudrate, p.timeout = path, 115200, 0.05
    p.dtr = False          # set BEFORE open() if DTR is wired to reset/boot on your board
    p.rts = True
    p.open()
    return p

def main():
    path = os.environ.get("PORT", "/dev/ttyUSB0")
    cmd = " ".join(sys.argv[1:])
    if users := port_users(path):
        print(f"ERR {path} is in use by {', '.join(users)}", file=sys.stderr); return 2
    with open_port(path) as p:
        p.reset_input_buffer()
        p.write(cmd.encode() + b"\r")
        buf, t0, last = b"", time.time(), time.time()
        while time.time() - t0 < MAX_S:
            d = p.read(256)
            if d:
                buf += d; last = time.time()
                if buf.endswith(PROMPT):
                    break
            elif buf and time.time() - last > QUIET_S:
                break
    if not buf:
        print("ERR no answer", file=sys.stderr); return 2
    text = buf.decode(errors="replace")
    print(text)
    return 1 if any(l.startswith("ERR") for l in text.splitlines()) else 0

if __name__ == "__main__":
    sys.exit(main())
```

### 5.5 `tools/release_test.sh` — automatic release report (pattern)

1. board present and port free → 2. record user state (`status`) → 3. `build_and_flash.sh` →
4. `bringup_test.py` all automatic checks → 5. restore user state → write
`docs/test_reports/release_test_v<ver>_<date>.md` with a `| Step | Result | Details |` table and
`<details>` blocks containing raw logs. A `row <step> <PASS|FAIL|SKIP|INFO> <details>` helper collects rows;
exit 0 = all passed, 1 = failure, 2 = no board.

### 5.6 `tools/debug.sh` + `tools/gdb/common.gdb`

```bash
#!/usr/bin/env bash
set -eu; cd "$(dirname "$0")/.."
B=$HOME/.local/share/stm32cube/bundles
newest() { ls -d $1 2>/dev/null | sort -V | tail -1; }
SERVER=${STLINK_GDBSERVER:-$(newest "$B/stlink-gdbserver/*/bin/ST-LINK_gdbserver")}
GDB=${ARM_GDB:-$(newest "$B/gnu-gdb-for-stm32/*/bin/arm-none-eabi-gdb")}
PROG=${CUBEPROG_BIN:-$(newest "$B/programmer/*/bin")}
cmake --preset debug >/dev/null && cmake --build --preset debug
LOG=$(mktemp)
"$SERVER" -p 61234 -l 1 -d -s -cp "$PROG" -m 0 -k >"$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; wait $PID 2>/dev/null || true; rm -f "$LOG"' EXIT
for _ in $(seq 1 40); do grep -q "Waiting for debugger" "$LOG" && break; sleep 0.25; done
"$GDB" -q build/debug/<project>.elf -x tools/gdb/common.gdb ${1:+-x tools/gdb/$1.gdb}
```
```gdb
# tools/gdb/common.gdb
set pagination off
set confirm off
target extended-remote localhost:61234
monitor reset
load
# If BOOT0 may be high after reset, start the app from its own vector table:
set $sp = *(unsigned int *)0x08000000
set $pc = *(unsigned int *)0x08000004
```

### 5.7 `tools/udev/60-st-link.rules`
```udev
# TAG+="uaccess": access for the user logged in at the console only
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3748", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374b", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3752", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374d", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374e", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="374f", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3753", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="3754", TAG+="uaccess"
```

---

## 6. Reusable firmware modules (Core/)

| Module | Public API | Reuse notes |
|---|---|---|
| `main.c` | clock init, init order, main loop | Init order: **safe-state outputs first** → clocks → LEDs → console → POST → apply saved settings → loop (poll console, heartbeat LED). |
| `board.h` | pin `#define`s | Single source of truth for pins; comment each with schematic net / MCU pin number. |
| `version.h` | `#define FW_VERSION "x.y.z"` | Shown in boot banner and `status`; scripts parse it with `sed`; CI checks tag == `v`+FW_VERSION. |
| `console.c/.h` | `con_init`, `con_puts`, `con_printf`, `con_getline` (non-blocking), `con_get_stats`, `con_irq_handler` | USART 115200 8N1, IRQ RX ring buffer, blocking TX, overrun/framing/noise/drop counters, `CON_LINE_MAX`, `CON_LINE_TOO_LONG`. Fully generic. |
| `cli.c/.h` | table `commands[] = { name, help, fn, engineering_flag }` | `help` lists user commands, `help all` adds engineering ones. Replies start with `OK` / `ERR`. Prompt `> `. |
| `diag.c/.h` | `diag_init`, `diag_print_info`, `diag_measure` (VREFINT/temp ADC), `diag_post`, `diag_post_result` | Chip UID/flash size/reset cause, POST returning number of failures, `POST PASSED/FAILED` markers. |
| `cfg.c/.h` | `cfg_load_*`, `cfg_save_*`, `cfg_clear_*` | Settings record in the last flash page: `magic`, `version`, payload, `crc32`; size multiple of 8 bytes (`_Static_assert`); erase → program → read back. Blank/invalid = factory defaults. |
| `syscalls.c`, `<family>_it.c` | newlib stubs, IRQ handlers | Generic. |

Firmware conventions:

- C11, 2-space indent, braces on all blocks, `snake_case` with module prefix (`con_`, `cli_`, `diag_`, `cfg_` …).
- Each module file starts with a short comment explaining **why** it exists.
- **No dynamic allocation, no stdio `printf`.** Use `con_printf` (newlib-nano, no float formatting → print
  fixed-point integers).
- Unused pins stay in reset (analog) state; do not drive expansion headers at boot.
- Outputs that control external hardware come up in a **safe state** before anything else.
- Green LED = heartbeat (firmware alive), second LED = POST failure / fault.
- A `bootloader` command that jumps to the ROM bootloader enables UART firmware updates without a probe.

New CLI command checklist: `cmd_*` function + row in `commands[]` → line in `docs/USER_MANUAL.md`
(and END_USER_GUIDE if user-facing) → CHANGELOG `[Unreleased]` line → check in `bringup_test.py`.

---

## 7. CI / GitHub

### `.github/workflows/build.yml`

```yaml
name: build
on:
  push:
    branches: [main]
    tags: ["v*"]
  pull_request:
  workflow_dispatch:

permissions:
  contents: read

env:
  ARM_TC: arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi

jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7
      - name: Cache Arm toolchain
        id: tc-cache
        uses: actions/cache@v6
        with:
          path: ~/${{ env.ARM_TC }}
          key: ${{ env.ARM_TC }}
      - name: Download Arm toolchain
        if: steps.tc-cache.outputs.cache-hit != 'true'
        run: |
          curl -fsSL -o /tmp/tc.tar.xz \
            "https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/${ARM_TC}.tar.xz"
          tar -xJf /tmp/tc.tar.xz -C ~
      - name: Install CMake + Ninja
        run: sudo apt-get update -q && sudo apt-get install -yq ninja-build cmake
      - name: Build all, zero warnings
        run: ARM_TOOLCHAIN_PATH="$HOME/$ARM_TC/bin" tools/build_all.sh
      - name: Python syntax check
        run: python3 -m py_compile tools/*.py .claude/hooks/*.py
      - name: Collect release image
        run: |
          VER=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' Core/Inc/version.h)
          echo "VER=$VER" >> "$GITHUB_ENV"
          mkdir -p dist
          for ext in hex bin elf map; do
            cp "build/release/<project>.$ext" "dist/<project>_v${VER}.$ext"
          done
          (cd dist && sha256sum * > SHA256SUMS)
      - uses: actions/upload-artifact@v7
        with:
          name: <project>_v${{ env.VER }}_${{ github.sha }}
          path: dist/

  release:
    if: startsWith(github.ref, 'refs/tags/v')
    needs: build
    runs-on: ubuntu-24.04
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v7
      - name: Tag must match FW_VERSION; notes from CHANGELOG
        run: |
          VER=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' Core/Inc/version.h)
          test "${GITHUB_REF_NAME}" = "v${VER}" || { echo "tag != FW_VERSION"; exit 1; }
          awk -v v="$VER" '$0 ~ "^## \\[" v "\\]" {f=1; next} /^## \[/ {f=0} f' CHANGELOG.md > notes.md
          test -s notes.md || echo "See CHANGELOG.md" > notes.md
      - uses: actions/download-artifact@v8
        with:
          path: dist
          merge-multiple: true
      - uses: softprops/action-gh-release@v3
        with:
          body_path: notes.md
          files: dist/*
```

### `.github/dependabot.yml`
```yaml
version: 2
updates:
  - package-ecosystem: github-actions
    directory: /
    schedule:
      interval: monthly
    commit-message:
      prefix: "CI"
    groups:
      actions:
        patterns: ["*"]
```

---

## 8. Release process

1. `git status` clean apart from release changes.
2. Bump `FW_VERSION` in `Core/Inc/version.h`.
3. `CHANGELOG.md`: move `[Unreleased]` items under `## [x.y.z] - YYYY-MM-DD`, add compare link, leave an empty `[Unreleased]`.
4. Update docs that show the version (USER_MANUAL, END_USER_GUIDE, regenerate PDF). `grep -rn "<old ver>" --include=*.md .`
5. `tools/build_all.sh` → `OK ... 0 warnings`.
6. With a board: `tools/release_test.sh` and commit the report + matching `artifacts/` folder.
7. `PLAN.md`: status + log line (what was / was not verified on hardware).
8. Commit → `git tag -a vx.y.z -m "FW x.y.z: <summary>"` → `git push origin main vx.y.z` → CI makes the GitHub release.

### `CHANGELOG.md` skeleton
```markdown
# Changelog

User-visible changes per firmware version (`FW_VERSION` in `Core/Inc/version.h`, git tag `v<version>`).
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
### Changed
### Fixed

[Unreleased]: https://github.com/<gh-user>/<repo>/compare/v0.1.0...HEAD
```

---

## 9. Documentation set (skeletons)

| File | Sections |
|---|---|
| `README.md` | one-paragraph description · CI badge · Quick start (3 commands) · table "For → Read" linking all docs · table of tools · GitHub/CI notes. **Landing page only** — no command details. |
| `docs/HARDWARE.md` | Block diagram · MCU (exact part, clocks, crystals fitted?) · Pin map (all connected signals) · Each external IC (protocol from datasheet) · **Findings from schematic review** (numbered, referenced from code). |
| `docs/BRINGUP_CHECKLIST.md` | 0 Unpowered (shorts, resistances) · 1 Power (current-limited) · 2 Debug connection · 3 Flash and console · 4 Boot/reset circuit · 5 Main function blocks · 6 Expansion headers · 7 Application · Sign-off. One table row per check: *Check / Expected / Measured / Pass*. |
| `docs/USER_MANUAL.md` | What the board does · What you need · Connect (Linux/Windows/editor) · Power-up (LEDs, boot messages) · Terminal basics · Menu · Commands · Status · Saved defaults · Typical tasks · Safety & limits · Diagnostic commands · Troubleshooting · Firmware update · Testing · Quick reference card. |
| `docs/END_USER_GUIDE.md` | Customer-facing subset (user commands only), exported to PDF. |
| `docs/test_reports/README.md` | What each report/sheet is and which tool makes it. |
| `PLAN.md` | 1 Goal and scope · 2 Phases (0 tooling w/o hardware, 1 power & first contact, 2 firmware on target, 3 boot/reset, 4 main function, 5 headers/robustness/sign-off, 6 product app) with status column · 3 **Decisions needed** · 4 Next-rev hardware change candidates · 5 Risks · 6 Log (dated). |

---

## 10. AI agent setup (Claude Code / Codex / Cursor)

### `CLAUDE.md`
```markdown
@AGENTS.md

## Claude Code specifics
- The arm toolchain is not on PATH. Always build through the CMake presets. Do not call `arm-none-eabi-gcc` by name.
- Flashing and serial tests touch real hardware. Run them only when the user says a board is connected,
  and report the actual console output.
- `.claude/settings.json` runs `.claude/hooks/guard_bash.py` before every shell command. If it blocks something
  the user asked for, say so and let the user run it with `! <command>`; do not work around the hook.
- Skills: `/release <version>` and `bench`.
```

### `AGENTS.md` skeleton
```markdown
# AGENTS.md: <Project Name> firmware

## What this is
<board, MCU, main ICs, what the firmware proves/does>. Schematic: <path>. BOM: <path>.
Hardware facts: docs/HARDWARE.md. Plan and decisions: PLAN.md (do not decide "Decisions needed" yourself).

## Build, flash, test
<commands from §3.5>
**Definition of done:** all builds finish with zero compiler warnings in our code. If hardware is attached,
also flash and run the on-target tests. Check for a board first (`STM32_Programmer_CLI -l`, `ls /dev/ttyUSB*`).
Never claim on-target results you did not observe.

## Layout
<tree from §2 with one line per file>

## Rules
### Hardware truth
- Every pin traces back to the schematic; pins only in Core/Inc/board.h. If code and schematic disagree, stop and report.
### Safety
- Never change option bytes, RDP, write protection, or mass erase without an explicit request. RDP 2 is permanent.
- Outputs come up in a safe state; reserved flash page is touched only through cfg.c.
### Code style
- <§6 conventions>. Keep console markers (OK/ERR, POST PASSED/FAILED, prompt "> ") stable.
### Docs
- Every user-visible change → CHANGELOG [Unreleased]. One fact, one place.

## Context-saving tips
- Do not read Drivers/ or KiCad files directly; grep HAL headers, use a pin-map extraction script.
- Reference manual: <RMxxxx>. Datasheet: <DSxxxxx>.
```

### `.claude/settings.json`
```json
{
  "$schema": "https://json.schemastore.org/claude-code-settings.json",
  "permissions": {
    "allow": [
      "Bash(cmake --preset *)",
      "Bash(cmake --build --preset debug)",
      "Bash(cmake --build --preset release)",
      "Bash(tools/con_send.sh *)",
      "Bash(ls /dev/ttyUSB*)",
      "Bash(git status *)",
      "Bash(git diff *)",
      "Bash(git log *)",
      "Bash(git show *)"
    ]
  },
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Bash",
        "hooks": [
          {
            "type": "command",
            "command": "\"$CLAUDE_PROJECT_DIR\"/.claude/hooks/guard_bash.py",
            "timeout": 10,
            "onFailure": "block"
          }
        ]
      }
    ]
  }
}
```

### `.claude/hooks/guard_bash.py` (make executable)
```python
#!/usr/bin/env python3
"""PreToolUse hook: block irreversible / hardware-dangerous shell commands.
Exit 2 + reason on stderr blocks the command; exit 0 lets it through."""
import json, re, sys

RULES = [
    (r"STM32_Programmer_CLI.*\s(-ob|--optionbytes)\s+(?!displ\b)", "option-byte write (reading with -ob displ is allowed)"),
    (r"STM32_Programmer_CLI.*\bRDP\s*=", "RDP level change: RDP 2 bricks debug access for good"),
    (r"STM32_Programmer_CLI.*\s(-rdu|--readunprotect)\b", "read-unprotect does a mass erase"),
    (r"STM32_Programmer_CLI.*\s(-e|--erase)\s+(all|\[?\s*0\s*\.\.)", "mass erase wipes the settings page"),
    (r"\bstty\b.*/dev/tty(USB|ACM)", "stty opens the port with DTR on; use tools/con_send.sh"),
    (r"[<>]\s*/dev/tty(USB|ACM)", "shell redirection opens the port with DTR on; use tools/con_send.sh"),
    (r"\bcat\s+/dev/tty(USB|ACM)", "cat opens the port with DTR on; use tools/con_send.sh"),
    (r"\bgit\s+push\b.*\s(-f|--force|--force-with-lease)\b", "force push rewrites published history"),
]

def main():
    try:
        cmd = json.load(sys.stdin).get("tool_input", {}).get("command", "")
    except (ValueError, AttributeError):
        return 0
    for pattern, why in RULES:
        if re.search(pattern, cmd):
            print(f"Blocked by guard_bash.py: {why}.", file=sys.stderr)
            return 2
    if re.search(r"(^|[;&|(]\s*)(sudo\s+)?picocom\b", cmd) and "--lower-dtr" not in cmd:
        print("Blocked by guard_bash.py: picocom needs --lower-dtr.", file=sys.stderr)
        return 2
    return 0

if __name__ == "__main__":
    sys.exit(main())
```
(Drop the DTR rules if DTR is not wired to reset/BOOT on your board.)

### `.claude/skills/release/SKILL.md`
```markdown
---
name: release
description: Cut a firmware release - bump FW_VERSION, update CHANGELOG/PLAN/docs, run the definition-of-done builds, commit, tag v<version>, push so CI creates the GitHub release.
disable-model-invocation: true
argument-hint: <new version, e.g. 0.2.0>
---
# Release v$ARGUMENTS
Follow AGENTS.md. Stop and ask if anything fails. <steps from §8>.
Ask the user before each of: commit, tag, push.
```

### `.claude/skills/bench/SKILL.md`
```markdown
---
name: bench
description: Run on-target checks on a connected board (find the board, flash if asked, run the test script) and report the real console output.
---
# Bench session
1. Find the board: `ls /dev/ttyUSB*` and `STM32_Programmer_CLI -l st-link`. None → say so and stop.
2. One program per port; if busy, ask the user to close their terminal.
3. Flash only if asked (`tools/build_and_flash.sh`).
4. Note the user's state from `status`; run the tests; restore the state.
5. Report version, PASS/FAIL lines, anything unexpected. Update HARDWARE.md/checklist/PLAN log if a hardware fact was learned.
```

---

## 11. New-project checklist

- [ ] Copy §2 layout, §3 build files, §4 hygiene files.
- [ ] Replace all placeholders (`grep -rn '<project>\|<MCU>\|<FAMILY>\|<family>\|<mcu_lower>' .`).
- [ ] Run `tools/vendor_cube.sh` with the right Cube package; set HAL module list in CMake + `hal_conf.h`.
- [ ] Linker script from ST template; reserve a settings page if needed.
- [ ] `board.h` from the schematic; document pins and schematic findings in `docs/HARDWARE.md`.
- [ ] `main.c` clock tree for your oscillator setup (no HSE/LSE if no crystals are fitted).
- [ ] Copy `console`, `cli`, `diag`, `cfg` modules; adapt UART instance/pins.
- [ ] `version.h` = `0.1.0`; CHANGELOG skeleton; PLAN.md phases.
- [ ] `tools/build_all.sh` passes with 0 warnings; push → CI green.
- [ ] Write AGENTS.md/CLAUDE.md, guard hook, skills; adjust the safety rules to the new board.
- [ ] Bring-up checklist + on-target test script for the new board's blocks.
