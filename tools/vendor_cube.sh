#!/usr/bin/env bash
# Copy the CMSIS + STM32L4 HAL files this project needs from a local
# STM32CubeL4 package into Drivers/. Re-run after bumping the Cube version.
#
#   tools/vendor_cube.sh [path/to/STM32Cube_FW_L4_Vx.y.z]
#
# The pulse timer (TIM2) is programmed through its registers (pulse.c), so the
# HAL TIM module is not needed.
set -euo pipefail

CUBE="${1:-$HOME/STM32Cube/Repository/STM32Cube_FW_L4_V1.18.2}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DST="$ROOT/Drivers"

[ -d "$CUBE/Drivers/STM32L4xx_HAL_Driver" ] || { echo "Cube package not found: $CUBE" >&2; exit 1; }

# HAL modules compiled by this project (keep in sync with CMakeLists.txt HAL_MODULES
# and Core/Inc/stm32l4xx_hal_conf.h)
HAL_SRC=(hal hal_adc hal_adc_ex hal_cortex hal_dma hal_dma_ex hal_exti hal_flash
         hal_flash_ex hal_flash_ramfunc hal_gpio hal_pwr hal_pwr_ex hal_rcc
         hal_rcc_ex hal_uart hal_uart_ex)

rm -rf "$DST"
mkdir -p "$DST/CMSIS/Include" "$DST/CMSIS/Device/ST/STM32L4xx/Include" \
         "$DST/STM32L4xx_HAL_Driver/Inc/Legacy" "$DST/STM32L4xx_HAL_Driver/Src"

cp "$CUBE"/Drivers/CMSIS/Include/{cmsis_compiler.h,cmsis_gcc.h,cmsis_version.h,core_cm4.h,mpu_armv7.h} \
   "$DST/CMSIS/Include/"
cp "$CUBE/Drivers/CMSIS/LICENSE.txt" "$DST/CMSIS/"
cp "$CUBE"/Drivers/CMSIS/Device/ST/STM32L4xx/Include/{stm32l4xx.h,stm32l451xx.h,system_stm32l4xx.h} \
   "$DST/CMSIS/Device/ST/STM32L4xx/Include/"
cp "$CUBE/Drivers/CMSIS/Device/ST/STM32L4xx/LICENSE.txt" "$DST/CMSIS/Device/ST/STM32L4xx/"

cp "$CUBE"/Drivers/STM32L4xx_HAL_Driver/Inc/*.h "$DST/STM32L4xx_HAL_Driver/Inc/"
rm -f "$DST/STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal_conf_template.h"
cp "$CUBE"/Drivers/STM32L4xx_HAL_Driver/Inc/Legacy/*.h "$DST/STM32L4xx_HAL_Driver/Inc/Legacy/"
for m in "${HAL_SRC[@]}"; do
  cp "$CUBE/Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_$m.c" "$DST/STM32L4xx_HAL_Driver/Src/"
done
cp "$CUBE/Drivers/STM32L4xx_HAL_Driver/LICENSE.txt" "$DST/STM32L4xx_HAL_Driver/"

# Startup + system file live in Core/ because projects commonly patch them
mkdir -p "$ROOT/Core/Startup" "$ROOT/Core/Src"
cp "$CUBE/Drivers/CMSIS/Device/ST/STM32L4xx/Source/Templates/gcc/startup_stm32l451xx.s" "$ROOT/Core/Startup/"
cp "$CUBE/Drivers/CMSIS/Device/ST/STM32L4xx/Source/Templates/system_stm32l4xx.c" "$ROOT/Core/Src/"

echo "$(basename "$CUBE")" > "$DST/CUBE_VERSION"
echo "Vendored $(basename "$CUBE") into $DST"
