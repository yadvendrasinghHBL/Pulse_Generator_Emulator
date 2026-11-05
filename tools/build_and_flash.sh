#!/usr/bin/env bash
# One-shot release run: clean build of both configurations, keep the image that goes into the
# board in artifacts/, delete build/, then flash that image.
#
#   tools/build_and_flash.sh              # release image, flash via ST-LINK if found, else over UART
#   tools/build_and_flash.sh --no-flash   # stop after saving the artifacts and deleting build/
#   PORT=/dev/ttyUSB1 tools/build_and_flash.sh
#
# Steps:
#   1. delete build/ and build debug + release from scratch; stop on any compiler warning
#   2. copy the release .hex/.bin/.elf/.map to artifacts/v<FW_VERSION>-<git hash>[-dirty]/ with
#      SHA256SUMS and BUILD_INFO.txt. Clean folders are committed to git; -dirty ones (built from
#      uncommitted code) are git-ignored, so every image in git matches a commit
#   3. delete build/
#   4. flash artifacts/.../pulse_gen.hex: SWD if an ST-LINK is attached (J6), otherwise send
#      `bootloader` on the console and use the ROM UART bootloader with DTR low (HARDWARE.md finding 3)
#   5. ask the board for `status` and check that it reports the new FW version
#
# Writes only the application pages: no mass erase, no option bytes, and the settings page
# (0x0803F800, reserved for stage 2) is left alone because the image does not cover it.
#
# Exit status: 0 = done, 1 = build/warning/flash error, 2 = no board found (artifacts are saved).
set -euo pipefail

cd "$(dirname "$0")/.."
PORT=${PORT:-/dev/ttyUSB0}
IMAGE=pulse_gen
FLASH=1
for arg in "$@"; do
  case "$arg" in
    --no-flash) FLASH=0 ;;
    -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "ERR unknown option: $arg (see --help)" >&2; exit 1 ;;
  esac
done

step() { printf '\n=== %s ===\n' "$*"; }

PROG=${STM32_PROGRAMMER_CLI:-$(command -v STM32_Programmer_CLI || true)}
[ -n "$PROG" ] || PROG="$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"
if [ "$FLASH" = 1 ] && [ ! -x "$PROG" ]; then
  echo "ERR STM32_Programmer_CLI not found (set STM32_PROGRAMMER_CLI)" >&2
  exit 1
fi

# ---- 1. clean build -------------------------------------------------------------------------------
step "1/5 clean build: debug, release"
tools/build_all.sh   # stops here on any warning; build/ is kept for inspection

# ---- 2. artifacts -----------------------------------------------------------------------------
step "2/5 save the release image"
VER=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' Core/Inc/version.h)
HASH=$(git rev-parse --short HEAD 2>/dev/null || echo nogit)
DIRTY=""
[ -z "$(git status --porcelain 2>/dev/null)" ] || DIRTY="-dirty"
ART="artifacts/v${VER}-${HASH}${DIRTY}"
rm -rf "$ART"
mkdir -p "$ART"
for ext in hex bin elf map; do
  cp "build/release/$IMAGE.$ext" "$ART/"
done
(cd "$ART" && sha256sum $IMAGE.* > SHA256SUMS)
{
  echo "firmware:  Pulse Generator Emulator v$VER (release build, -Os)"
  echo "git:       $(git rev-parse HEAD 2>/dev/null || echo none)${DIRTY:+ (uncommitted changes present)}"
  echo "tag:       $(git describe --tags --always --dirty 2>/dev/null || echo none)"
  echo "built:     $(date '+%Y-%m-%d %H:%M:%S %z') by $(whoami)@$(hostname)"
  arm_size=$(grep -m1 '^CMAKE_SIZE:' build/release/CMakeCache.txt | cut -d= -f2)
  echo "toolchain: $("$(dirname "$arm_size")/arm-none-eabi-gcc" --version 2>/dev/null | head -1)"
  echo "flash at:  0x08000000 (.bin), .hex carries its own addresses"
  echo
  [ -x "$arm_size" ] && "$arm_size" build/release/$IMAGE.elf
} > "$ART/BUILD_INFO.txt"
ls -l "$ART"
[ -z "$DIRTY" ] || echo "NOTE: the working tree has uncommitted changes; commit and tag before shipping."

# ---- 3. delete build/ -----------------------------------------------------------------------------
step "3/5 delete build/"
rm -rf build
echo "OK build/ deleted"

if [ "$FLASH" = 0 ]; then
  echo
  echo "OK artifacts in $ART (not flashed: --no-flash)"
  exit 0
fi

# ---- 4. flash ---------------------------------------------------------------------------------
step "4/5 flash $ART/$IMAGE.hex"
IMG="$ART/$IMAGE.hex"
PROBES=$("$PROG" -l st-link 2>/dev/null || true)
if echo "$PROBES" | grep -qiE 'st-?link sn'; then
  echo "ST-LINK found: flashing over SWD (J6)"
  "$PROG" -c port=SWD mode=UR reset=HWrst -w "$IMG" -v -rst
elif [ -e "$PORT" ]; then
  echo "No ST-LINK: flashing over UART ($PORT) via the ROM bootloader"
  # con_send refuses a port that another program (picocom) holds; exit 2 = busy or no answer
  if ! tools/con_send.sh bootloader; then
    echo "ERR the board did not take the bootloader command; close picocom and check the board" >&2
    exit 2
  fi
  sleep 1
  "$PROG" -c port="$PORT" br=115200 dtr=low -w "$IMG" -v -g 0x08000000
else
  echo "ERR no board found: no ST-LINK and no $PORT. Artifacts are saved in $ART." >&2
  exit 2
fi

# ---- 5. check ---------------------------------------------------------------------------------
step "5/5 check the running firmware"
sleep 1
if [ -e "$PORT" ] && REPLY=$(tools/con_send.sh status); then
  echo "$REPLY"
  if echo "$REPLY" | grep -q "^FW $VER,"; then
    echo
    echo "OK v$VER flashed from $ART and running"
  else
    echo "ERR board does not report FW $VER" >&2
    exit 1
  fi
else
  echo "NOTE: flashed, but no console answer on $PORT (with SWD only, connect the USB cable to check)."
fi
