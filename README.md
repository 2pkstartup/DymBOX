# dymbox – Regulátor teploty s ATmega8

Regulátor teploty postavený na mikrokontroléru **ATmega8** s 3-místným 7-segmentovým displejem, rotačním enkodérem, teploměrem DS18B20, PWM výstupem pro ventilátor a ON/OFF řízením topné spirály.

## Vlastnosti

- **Měření teploty** – DS18B20 (1-Wire), rozlišení 0.1 °C
- **3-místný 7-segment display** – multiplexovaný v Timer0 ISR (~163 Hz/číslice)
- **Rotační enkodér KY-040** – nastavení žádané teploty a otáček ventilátoru
- **PWM ventilátor** – 11 kroků (0–100 % po 10 %), Timer1 Fast PWM 8-bit na 62.5 kHz
- **Topná spirála** – ON/OFF regulace s hysterezí 5 °C, signalizace DP3
- **Nastavitelný setpoint** – výchozí 32.0 °C, editace po 0.1 °C

## Zapojení pinů

### 7-segment display (common anode, active LOW)

| Segment | Pin ATmega8 | Port |
|---------|-------------|------|
| A       | PB0         | PORTB |
| B       | PD6         | PORTD |
| C       | PC0         | PORTC |
| D       | PC3         | PORTC |
| E       | PC4         | PORTC |
| F       | PD7         | PORTD |
| G       | PC1         | PORTC |
| DP      | PC2         | PORTC |

### Výběr číslic (active LOW)

| Číslice | Pin | Pozice |
|---------|-----|--------|
| DIG1    | PD2 | Stovky |
| DIG2    | PD1 | Desítky |
| DIG3    | PD0 | Jednotky |

### Rotační enkodér KY-040

| Signál | Pin |
|--------|-----|
| CLK (A) | PD3 |
| DT (B)  | PD4 |
| SW       | PD5 |
| +        | VCC |
| GND      | GND |

### DS18B20 teploměr

| Pin DS18B20 | Kam |
|-------------|-----|
| DQ (data)   | PC5 + 4.7 kΩ pull-up na VCC |
| VCC         | VCC (3.3–5 V) |
| GND         | GND |

### Výstupy

| Funkce | Pin | Typ |
|--------|-----|-----|
| Ventilátor (PWM) | PB1 (OC1A) | Fast PWM 62.5 kHz |
| Topná spirála     | PB2        | SW ON/OFF (active LOW) |

### Souhrn obsazení pinů

```
PORTB: PB0 = seg A       PB1 = PWM fan    PB2 = heater    PB3–PB5 = volné
PORTC: PC0 = seg C       PC1 = seg G      PC2 = seg DP    PC3 = seg D
       PC4 = seg E       PC5 = DS18B20
PORTD: PD0 = DIG3        PD1 = DIG2       PD2 = DIG1
       PD3 = ENC CLK     PD4 = ENC DT     PD5 = ENC SW
       PD6 = seg B       PD7 = seg F
```

## Ovládání

Aplikace má 3 režimy zobrazení:

### 1. Teplota (výchozí)

Zobrazuje aktuální teplotu ve formátu **XX.X** (tečka za druhým digitem).

- **Otočení enkodéru** → přepne na zobrazení ventilátoru
- **Stisk tlačítka** → přepne do editace setpointu
- **DP3 (poslední tečka)** svítí, pokud topí spirála

### 2. Ventilátor (FXX)

Zobrazuje rychlost ventilátoru ve formátu **FXX** (např. F50 = 50 %). Při 100 % zobrazí **100**.

- **Otočení enkodéru** → mění rychlost po 10 % (0–100 %)
- **Stisk tlačítka** → přepne do editace setpointu
- Po **2 sekundách** bez pohybu se automaticky vrátí na zobrazení teploty

### 3. Editace setpointu

Zobrazuje žádanou teplotu ve formátu **XX.X** s rozsvícenou **DP1** (první tečka) jako indikací editačního režimu.

- **Otočení enkodéru** → mění hodnotu po 0.1 °C (rozsah 0.0–99.9 °C)
- **Stisk tlačítka** → uloží hodnotu a vrátí se na zobrazení teploty

## Regulace teploty

Topná spirála je řízena jednoduchou ON/OFF regulací s hysterezí:

| Podmínka | Akce |
|----------|------|
| teplota ≤ setpoint − 5 °C | Spirála **ON** (DP3 svítí) |
| teplota ≥ setpoint | Spirála **OFF** (DP3 nesvítí) |
| v pásmu hystereze | Bez změny |

Výchozí setpoint: **32.0 °C**, hystereze: **5.0 °C** (konstanta `HEATER_HYST`).

## Požadavky

- `avr-gcc`, `avr-libc`, `binutils-avr`
- `avrdude`
- Programátor USBasp (nebo jiný – viz `PROGRAMMER` v Makefile)

### Instalace toolchainu (Debian/Ubuntu)

```bash
sudo apt install gcc-avr avr-libc binutils-avr avrdude
```

## Kompilace a nahrání

```bash
make            # Kompilace
make flash      # Nahrání do MCU
make fuses      # Nastavení fuses (interní 16 MHz)
make size       # Velikost firmware
make clean      # Smazání build souborů
```

### Velikost firmware

```
Program:  ~2046 bytes (25.0% Full)
Data:       ~61 bytes  (6.0% Full)
```

## Konfigurace

Hlavní konstanty v `main.c`:

| Konstanta | Hodnota | Popis |
|-----------|---------|-------|
| `F_CPU` | 16 MHz | Frekvence krystalu/oscilátoru (Makefile) |
| `HEATER_HYST` | 50 (5.0 °C) | Hystereze topné spirály (v 1/10 °C) |
| `HEATER_PERIOD` | 4880 | Perioda ON/OFF cyklu (~10 s) |
| Výchozí setpoint | 320 (32.0 °C) | Žádaná teplota (v 1/10 °C) |
| Výchozí fan | 5 (50 %) | Výchozí rychlost ventilátoru |

## Schéma zapojení

```
                    ATmega8
                 ┌───────────┐
    seg A ← PB0 ┤1        28├ PC5 → DS18B20 DQ
  PWM fan ← PB1 ┤2        27├ PC4 → seg E
   heater ← PB2 ┤3        26├ PC3 → seg D
           PB3 ┤4        25├ PC2 → seg DP
           PB4 ┤5        24├ PC1 → seg G
           PB5 ┤6        23├ PC0 → seg C
               ┤7        22├ GND
               ┤8        21├ AREF
               ┤9        20├ VCC
               ┤10       19├ PB5 (SCK)
    DIG3 ← PD0 ┤11       18├ PB4 (MISO)
    DIG2 ← PD1 ┤12       17├ PB3 (MOSI)
    DIG1 ← PD2 ┤13       16├ PB2
 ENC CLK → PD3 ┤14       15├ PB1
  ENC DT → PD4 ┤15       14├ PD7 → seg F
  ENC SW → PD5 ┤16       13├ PD6 → seg B
                 └───────────┘
```

## Licence

Tento projekt je volně k použití.
