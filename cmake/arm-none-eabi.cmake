# CMake toolchain file for arm-none-eabi-gcc (bare-metal Cortex-M).
# The toolchain is looked up on PATH, then in ARM_TOOLCHAIN_PATH (env or -D),
# then in the locations where it is installed on the development machine.
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
