cmake_minimum_required(VERSION 3.20)
project(abn_ch32v203_bridge LANGUAGES C ASM)

if(CMAKE_C_COMPILER_VERSION VERSION_LESS 15.0)
    message(FATAL_ERROR "GCC 15 or newer is required; tested with Arch riscv64-elf-gcc 15.2")
endif()

set(CH32V20X_SDK "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ch32v20x" CACHE PATH
    "Pinned openwch/ch32v20x checkout")
set(SDK_SRC "${CH32V20X_SDK}/EVT/EXAM/SRC")
set(SDK_USB "${CH32V20X_SDK}/EVT/EXAM/USB/USBD/SimulateCDC/User")

if(NOT EXISTS "${SDK_SRC}/Startup/startup_ch32v20x_D6.S")
    message(FATAL_ERROR "CH32V20x SDK missing. Run: bash fetch-sdk.sh")
endif()

set(USB_DRIVER "${SDK_USB}/USBLIB/USB-Driver")
set(USB_CONFIG "${SDK_USB}/USBLIB/CONFIG")

add_executable(abn_ch32v203_bridge.elf
    src/main.c
    src/bridge.c
    src/nrf24.c
    src/platform.c
    src/usb_bridge.c
    src/usb_endp.c
    src/usb_hw.c
    src/usb_prop.c
    ../common/abn_protocol.c
    ../common/crc32.c
    "${SDK_SRC}/Startup/startup_ch32v20x_D6.S"
    "${SDK_SRC}/Peripheral/src/ch32v20x_exti.c"
    "${SDK_SRC}/Peripheral/src/ch32v20x_gpio.c"
    "${SDK_SRC}/Peripheral/src/ch32v20x_misc.c"
    "${SDK_SRC}/Peripheral/src/ch32v20x_rcc.c"
    "${SDK_SRC}/Peripheral/src/ch32v20x_spi.c"
    "${SDK_USB}/system_ch32v20x.c"
    "${USB_CONFIG}/usb_desc.c"
    "${USB_CONFIG}/usb_istr.c"
    "${USB_CONFIG}/usb_pwr.c"
    "${USB_DRIVER}/src/usb_core.c"
    "${USB_DRIVER}/src/usb_init.c"
    "${USB_DRIVER}/src/usb_int.c"
    "${USB_DRIVER}/src/usb_mem.c"
    "${USB_DRIVER}/src/usb_regs.c"
    "${USB_DRIVER}/src/usb_sil.c"
)

target_include_directories(abn_ch32v203_bridge.elf PRIVATE
    inc
    ../common
    "${SDK_SRC}/Core"
    "${SDK_SRC}/Debug"
    "${SDK_SRC}/Peripheral/inc"
    "${SDK_USB}"
    "${USB_CONFIG}"
    "${USB_DRIVER}/inc"
)

target_compile_definitions(abn_ch32v203_bridge.elf PRIVATE CH32V20x_D6)
target_compile_options(abn_ch32v203_bridge.elf PRIVATE
    -march=rv32imac_zicsr_zifencei
    -mabi=ilp32
    -mcmodel=medany
    -msmall-data-limit=8
    -Os
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fsigned-char
    -Wall
    -Wextra
    $<$<COMPILE_LANGUAGE:ASM>:-x$<SEMICOLON>assembler-with-cpp>
)

target_link_options(abn_ch32v203_bridge.elf PRIVATE
    -march=rv32imac_zicsr_zifencei
    -mabi=ilp32
    -mcmodel=medany
    -nostartfiles
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/abn_ch32v203_bridge.map
    -T${SDK_SRC}/Ld/Link.ld
)

add_custom_command(TARGET abn_ch32v203_bridge.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:abn_ch32v203_bridge.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/abn_ch32v203_bridge.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:abn_ch32v203_bridge.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/abn_ch32v203_bridge.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:abn_ch32v203_bridge.elf>
)
