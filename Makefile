# Project: dymbox
# MCU: ATmega8
# F_CPU: 16000000 Hz (internal 16 MHz oscillator)

MCU     = atmega8
F_CPU   = 16000000UL
TARGET  = dymbox

CC      = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE    = avr-size
AVRDUDE = avrdude

CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -Wextra -std=gnu99
LDFLAGS = -mmcu=$(MCU)

# Programmer settings (adjust to your programmer)
PROGRAMMER = usbasp
PORT       =

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

.PHONY: all flash clean size fuses

all: $(TARGET).hex size

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

size: $(TARGET).elf
	$(SIZE) --mcu=$(MCU) --format=avr $<

flash: $(TARGET).hex
	$(AVRDUDE) -c $(PROGRAMMER) -p $(MCU) $(if $(PORT),-P $(PORT)) -U flash:w:$<:i

# ATmega8 fuses: internal 16 MHz, no CKDIV8
# lfuse = 0xE4, hfuse = 0xD9
fuses:
	$(AVRDUDE) -c $(PROGRAMMER) -p $(MCU) $(if $(PORT),-P $(PORT)) -U lfuse:w:0xE4:m -U hfuse:w:0xD9:m

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).hex
