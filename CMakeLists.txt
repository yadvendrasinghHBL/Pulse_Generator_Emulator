cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(pulse_gen C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32L451RCTX_FLASH.ld)

# Keep in sync with tools/vendor_cube.sh and Core/Inc/stm32l4xx_hal_conf.h
set(HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32L4xx_HAL_Driver)
set(HAL_MODULES hal hal_adc hal_adc_ex hal_cortex hal_dma hal_dma_ex hal_exti
                hal_flash hal_flash_ex hal_flash_ramfunc hal_gpio hal_pwr
                hal_pwr_ex hal_rcc hal_rcc_ex hal_uart hal_uart_ex)
list(TRANSFORM HAL_MODULES PREPEND ${HAL_DIR}/Src/stm32l4xx_)
list(TRANSFORM HAL_MODULES APPEND .c)

file(GLOB APP_SOURCES CONFIGURE_DEPENDS Core/Src/*.c)

add_executable(${PROJECT_NAME}
  ${APP_SOURCES}
  Core/Startup/startup_stm32l451xx.s
  ${HAL_MODULES})
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX .elf)

target_include_directories(${PROJECT_NAME} PRIVATE
  Core/Inc
  ${HAL_DIR}/Inc
  Drivers/CMSIS/Device/ST/STM32L4xx/Include
  Drivers/CMSIS/Include)

# USER_VECT_TAB_ADDRESS: SystemInit() points VTOR at our flash vector table.
# Without it VTOR stays 0, which is the ROM's table when the app is started by
# the bootloader's Go command (system flash still remapped at 0).
target_compile_definitions(${PROJECT_NAME} PRIVATE
  STM32L451xx USE_HAL_DRIVER USER_VECT_TAB_ADDRESS)

target_compile_options(${PROJECT_NAME} PRIVATE
  ${MCU_FLAGS}
  -ffunction-sections -fdata-sections -fno-common
  $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-unused-parameter>
  $<$<CONFIG:Debug>:-Og -g3>
  $<$<CONFIG:Release>:-Os -g>)

# Vendor code is not ours to fix: silence its warnings
set_source_files_properties(${HAL_MODULES} Core/Src/system_stm32l4xx.c
  PROPERTIES COMPILE_OPTIONS "-w")

target_link_options(${PROJECT_NAME} PRIVATE
  ${MCU_FLAGS}
  -T${LINKER_SCRIPT}
  --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections -Wl,--print-memory-usage
  -Wl,-Map=${PROJECT_NAME}.map -Wl,--no-warn-rwx-segments)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
  VERBATIM)

# --- Flashing (STM32CubeProgrammer CLI) -------------------------------------
find_program(STM32_PROGRAMMER_CLI STM32_Programmer_CLI
  PATHS $ENV{HOME}/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin)
set(FLASH_UART_PORT /dev/ttyUSB0 CACHE STRING "Serial port of the on-board FT231X")

if(STM32_PROGRAMMER_CLI)
  # SWD via ST-LINK on J6 (1=SWCLK 2=GND 3=SWDIO 4=NRST)
  add_custom_target(flash
    COMMAND ${STM32_PROGRAMMER_CLI} -c port=SWD mode=UR reset=HWrst
            -w ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex -v -rst
    DEPENDS ${PROJECT_NAME} USES_TERMINAL)
  # ROM bootloader over USART1/FT231X: send `bootloader` on the console first, then close it.
  # dtr=low: DTR on holds the MCU in reset (docs/HARDWARE.md finding 3).
  add_custom_target(flash_uart
    COMMAND ${STM32_PROGRAMMER_CLI} -c port=${FLASH_UART_PORT} br=115200 dtr=low
            -w ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex -v -g 0x08000000
    DEPENDS ${PROJECT_NAME} USES_TERMINAL)
endif()
