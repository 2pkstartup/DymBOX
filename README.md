# DymBOX

Regulátor teploty pro udírnu postavený na mikrokontroléru **ATmega8**.

Měří teplotu pomocí DS18B20, zobrazuje ji na 3-místném 7-segmentovém displeji a řídí ventilátor (PWM) a topnou spirálu (ON/OFF s hysterezí). Žádaná teplota a otáčky ventilátoru se nastavují rotačním enkodérem.

## Vlastnosti

- **Měření teploty** – DS18B20 (1-Wire), rozlišení 0.1 °C
- **3-místný 7-segment display** – multiplexovaný v Timer0 ISR (~163 Hz/číslice)
- **Rotační enkodér KY-040** – nastavení žádané teploty a otáček ventilátoru
- **PWM ventilátor** – 11 kroků (0–100 % po 10 %), Timer1 Fast PWM 8-bit na ~977 Hz
- **Topná spirála** – ON/OFF regulace s hysterezí 5 °C
- **Nastavitelný setpoint** – výchozí 32.0 °C, editace po 0.1 °C

## Struktura repozitáře

```
DymBOX/
├── fw/                 # Firmware (C, ATmega8)
│   ├── main.c
│   ├── CMakeLists.txt
│   ├── Makefile
│   ├── cmake/          # AVR-GCC toolchain soubor
│   └── README.md
├── hw/                 # Hardware (KiCad 8)
│   ├── DymBOX_AVR.kicad_sch    # Schéma
│   ├── DymBOX_AVR.kicad_pcb    # PCB layout
│   ├── gerber/                  # Gerber soubory pro výrobu
│   ├── dymbox_gerber.zip        # Zabalené gerbery
│   ├── 2pkstartup.pretty/      # Vlastní footprinty
│   └── DymBOX_AVR-backups/
└── README.md
```

## Hardware

Schéma a PCB jsou navrženy v **KiCadu**. Gerber soubory připravené k výrobě najdete v `hw/gerber/` nebo jako `hw/dymbox_gerber.zip`.

### Hlavní komponenty

| Součástka | Popis |
|-----------|-------|
| ATmega8 | Hlavní MCU (16 MHz) |
| DS18B20 | Digitální teploměr (1-Wire) |
| 3× 7-segment (common anode) | Displej – multiplexovaný |
| KY-040 | Rotační enkodér s tlačítkem |
| Ventilátor | PWM řízený (~977 Hz) |
| Topná spirála | ON/OFF přes PB2 |

### Zapojení pinů

```
PORTB: PB0 = seg A       PB1 = PWM fan    PB2 = heater
PORTC: PC0 = seg E       PC1 = seg D      PC2 = seg DP
       PC3 = seg C       PC4 = seg G      PC5 = DS18B20
PORTD: PD0 = ENC SW      PD1 = DIG3       PD2 = DIG2
       PD3 = ENC CLK     PD4 = ENC DT     PD5 = DIG1
       PD6 = seg B       PD7 = seg F
```

## Firmware

Firmware je napsaný v **C** pro AVR a překládá se pomocí `avr-gcc` + CMake.

### Požadavky

```bash
sudo apt install gcc-avr avr-libc binutils-avr avrdude cmake
```

### Kompilace a nahrání

```bash
cd fw
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/avr-gcc.cmake
cmake --build build              # Kompilace
cmake --build build --target flash   # Nahrání do MCU (USBasp)
cmake --build build --target fuses   # Nastavení fuses
```

### Velikost firmware

```
Program:  ~2186 bytes (26.7 % Flash)
Data:       ~61 bytes  (6.0 % SRAM)
```

## Ovládání

Zařízení má 3 režimy zobrazení:

| Režim | Displej | Ovládání |
|-------|---------|----------|
| **Teplota** (výchozí) | `XX.X` – aktuální teplota | Otočení → ventilátor · Stisk → editace setpointu |
| **Ventilátor** | `FXX` – rychlost v % | Otočení → změna po 10 % · Po 2 s → zpět na teplotu |
| **Editace setpointu** | `XX.X` + DP1 svítí | Otočení → změna po 0.1 °C · Stisk → uložit a zpět |

Topná spirála se indikuje rozsvícením DP3 (poslední tečka).

## Regulace teploty

ON/OFF regulace s hysterezí:

| Podmínka | Akce |
|----------|------|
| teplota ≤ setpoint − 5 °C | Spirála **ON** |
| teplota ≥ setpoint | Spirála **OFF** |
| v pásmu hystereze | Beze změny |

## Licence

Tento projekt je volně k použití.
