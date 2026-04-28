cmake_minimum_required(VERSION 3.20)
project(dymbox C)

# MCU and clock
set(MCU "atmega8" CACHE STRING "AVR MCU type")
set(F_CPU "8000000UL" CACHE STRING "CPU frequency in Hz")

# Programmer settings
set(PROGRAMMER "usbasp" CACHE STRING "avrdude programmer type")
set(PORT "" CACHE STRING "avrdude port (leave empty for auto)")

# Compiler flags
add_compile_options(-mmcu=${MCU} -Os -Wall -Wextra -std=gnu99)
add_compile_definitions(F_CPU=${F_CPU})
add_link_options(-mmcu=${MCU})

# Sources
add_executable(${PROJECT_NAME}.elf main.c)

# Generate .hex from .elf
add_custom_command(
    TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    BYPRODUCTS ${PROJECT_NAME}.hex
    COMMENT "Generating ${PROJECT_NAME}.hex"
)

# Size report
find_program(AVR_SIZE avr-size REQUIRED)
add_custom_command(
    TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${AVR_SIZE} --mcu=${MCU} --format=avr $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Size report"
)

# Flash target
find_program(AVRDUDE avrdude)
if(AVRDUDE)
    set(_port_arg "")
    if(NOT PORT STREQUAL "")
        set(_port_arg -P ${PORT})
    endif()

    add_custom_target(flash
        COMMAND ${AVRDUDE} -c ${PROGRAMMER} -p ${MCU} ${_port_arg}
                -U flash:w:${PROJECT_NAME}.hex:i
        DEPENDS ${PROJECT_NAME}.elf
        COMMENT "Flashing ${PROJECT_NAME}.hex"
    )

    # Fuse programming (ATmega8: external crystal 8 MHz, low-power mode)
    # lfuse=0xFF: CKSEL=1111 (xtal 8-16 MHz range), SUT=11 (65ms startup), BOD disabled
    # hfuse=0xD9: CKOPT=1 (low-power crystal, ok up to ~8 MHz), SPIEN=0, BOOTRST=1
    add_custom_target(fuses
        COMMAND ${AVRDUDE} -c ${PROGRAMMER} -p ${MCU} ${_port_arg}
                -U lfuse:w:0xFF:m -U hfuse:w:0xD9:m
        COMMENT "Programming fuses: lfuse=0xFF, hfuse=0xD9"
    )
endif()
