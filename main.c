/*
 * ============================================================================
 * dymbox - Regulátor teploty s ATmega8
 * ============================================================================
 *
 * Popis:
 *   Regulátor teploty pro topnou spirálu s chladícím ventilátorem.
 *   Měření teploty DS18B20, zobrazení na 3-místném 7-segmentovém displeji,
 *   ovládání rotačním enkodérem KY-040. Setpoint uložen v EEPROM.
 *
 * Funkce:
 *   - Měření teploty DS18B20 (1-Wire), rozlišení 0.1 °C
 *   - 3-místný 7-seg display (common anode), multiplexovaný Timer0 ISR
 *   - Rotační enkodér KY-040 (polling v Timer0 ISR, debounce tlačítka)
 *   - PWM ventilátor 0-100% po 10% (Timer1, 62.5 kHz, active HIGH)
 *   - Topná spirála ON/OFF s hysterezí 5 °C (active HIGH)
 *   - Setpoint ukládán do EEPROM (s magic byte validací)
 *
 * Režimy zobrazení:
 *   1. Teplota (XX.X) - výchozí, DP3 = spirála topí
 *   2. Ventilátor (FXX/100) - po otočení enkodéru, timeout 2 s
 *   3. Setpoint (XX.X + DP1) - stisk tlačítka, rotací nastavit, stiskem uložit
 *
 * Zapojení pinů ATmega8 (DIP-28):
 *
 *   PORTB:  PB0 = seg A (display)
 *           PB1 = OC1A - PWM ventilátor (active HIGH)
 *           PB2 = topná spirála (active HIGH)
 *           PB3..PB5 = ISP (MOSI, MISO, SCK) / volné
 *
 *   PORTC:  PC0 = seg C        PC3 = seg D
 *           PC1 = seg G        PC4 = seg E
 *           PC2 = seg DP       PC5 = DS18B20 DQ (+ 4.7kΩ pull-up)
 *
 *   PORTD:  PD0 = DIG3 (jednotky)   PD3 = ENC CLK (A)
 *           PD1 = DIG2 (desítky)    PD4 = ENC DT  (B)
 *           PD2 = DIG1 (stovky)     PD5 = ENC SW  (tlačítko)
 *           PD6 = seg B             PD7 = seg F
 *
 * Schéma:
 *
 *                         ATmega8 (DIP-28)
 *                      ┌────────────────────┐
 *         seg A ← PB0 ─┤1  (PC5) RESET   28├─ PC5 → DS18B20 DQ
 *     PWM fan ← PB1 ─┤2  (PC4)  ADC5   27├─ PC4 → seg E
 *      heater ← PB2 ─┤3  (PC3)  ADC4   26├─ PC3 → seg D
 *    ISP MOSI   PB3 ─┤4  (PC2)  ADC3   25├─ PC2 → seg DP
 *    ISP MISO   PB4 ─┤5  (PC1)  ADC2   24├─ PC1 → seg G
 *     ISP SCK   PB5 ─┤6  (PC0)  ADC1   23├─ PC0 → seg C
 *                     ─┤7  AVCC         22├─ GND
 *                     ─┤8  AREF         21├─ AREF
 *               VCC  ─┤9  VCC          20├─ VCC
 *               GND  ─┤10 GND          19├─ PB5 (SCK)
 *              XTAL1 ─┤11              18├─ PB4 (MISO)
 *              XTAL2 ─┤12              17├─ PB3 (MOSI)
 *        DIG3 ← PD0 ─┤13 (RXD)        16├─ PB2 → heater
 *        DIG2 ← PD1 ─┤14 (TXD)        15├─ PB1 → PWM fan
 *        DIG1 ← PD2 ─┤15 (INT0)       14├─ PD7 → seg F
 *     ENC CLK → PD3 ─┤16 (INT1)       13├─ PD6 → seg B
 *      ENC DT → PD4 ─┤17 (T0)
 *      ENC SW → PD5 ─┤18 (T1)
 *                      └────────────────────┘
 *
 *   Display: common anode, segmenty active LOW
 *   Digit select: active LOW (PNP/P-FET)
 *   Výstupy PB1, PB2: active HIGH (pro D4184 MOSFET moduly)
 *   DS18B20: externě 4.7 kΩ pull-up na VCC
 *   KY-040: interní pull-up na PD3, PD4, PD5
 *   F_CPU = 16 MHz
 *
 * Konstanty:
 *   HEATER_HYST    = 5.0 °C (hystereze topné spirály)
 *   HEATER_PERIOD  = 10 s (perioda ON/OFF cyklu)
 *   Výchozí setpoint = 32.0 °C (pokud EEPROM prázdná)
 *   Výchozí fan = 50%
 *
 * ============================================================================
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

/* Masky segmentových pinů na jednotlivých portech */
#define SEG_B_MASK  ((1 << PB0))                                          /* A */
#define SEG_C_MASK  ((1 << PC0)|(1 << PC1)|(1 << PC2)|(1 << PC3)|(1 << PC4)) /* G,C,D,DP,E */
#define SEG_D_MASK  ((1 << PD6)|(1 << PD7))                              /* B,F */

/* Maska digit-select pinů na PORTD */
#define DIG_MASK    ((1 << PD0)|(1 << PD1)|(1 << PD2))

/* Rotační enkodér KY-040 na PORTD */
#define ENC_CLK     PD3
#define ENC_DT      PD4
#define ENC_SW      PD5
#define ENC_MASK    ((1 << ENC_CLK)|(1 << ENC_DT)|(1 << ENC_SW))

/* DS18B20 1-Wire na PC5 */
#define OW_DDR      DDRC
#define OW_PORT     PORTC
#define OW_PIN      PINC
#define OW_BIT      PC5

/* PWM výstupy */
#define PWM1_PIN    PB1   /* OC1A - ventilátor */
#define PWM2_PIN    PB2   /* topná spirála (SW ON/OFF) */

/* Topná spirála: konstanty */
#define HEATER_PERIOD  4880   /* 10 s perioda (488 Hz × 10) */
#define HEATER_HYST    25     /* hystereze 5.0°C (v 1/10 °C) */

/* Pin pro každou pozici: DIG1(stovky)=PD2, DIG2(desítky)=PD1, DIG3(jednotky)=PD0 */
static const uint8_t dig_pin[] = {PD2, PD1, PD0};

/*
 * Lookup tabulky: bity = piny, které mají být LOW (aktivní) pro danou číslici.
 *
 *   Číslice:  0     1     2     3     4     5     6     7     8     9
 *   Seg ON:  ABCDEF BC   ABDEG ABCDG BCFG  ACDFG ACDEFG ABC  ABCDEFG ABCDFG
 */

/*         A=PB0 */
static const uint8_t seg_b[] = {
    0x01, /* 0: A */
    0x00, /* 1:   */
    0x01, /* 2: A */
    0x01, /* 3: A */
    0x00, /* 4:   */
    0x01, /* 5: A */
    0x01, /* 6: A */
    0x01, /* 7: A */
    0x01, /* 8: A */
    0x01, /* 9: A */
};

/*         C=PC0  G=PC1  DP=PC2  D=PC3  E=PC4 */
static const uint8_t seg_c[] = {
    0x19, /* 0: C,D,E         = (1<<0)|(1<<3)|(1<<4) */
    0x01, /* 1: C             = (1<<0) */
    0x1A, /* 2: G,D,E         = (1<<1)|(1<<3)|(1<<4) */
    0x0B, /* 3: G,C,D         = (1<<0)|(1<<1)|(1<<3) */
    0x03, /* 4: G,C           = (1<<0)|(1<<1) */
    0x0B, /* 5: G,C,D         = (1<<0)|(1<<1)|(1<<3) */
    0x1B, /* 6: G,C,D,E       = (1<<0)|(1<<1)|(1<<3)|(1<<4) */
    0x01, /* 7: C             = (1<<0) */
    0x1B, /* 8: G,C,D,E       = (1<<0)|(1<<1)|(1<<3)|(1<<4) */
    0x0B, /* 9: G,C,D         = (1<<0)|(1<<1)|(1<<3) */
};

/*         B=PD6  F=PD7 */
static const uint8_t seg_d[] = {
    0xC0, /* 0: B,F  = (1<<6)|(1<<7) */
    0x40, /* 1: B    = (1<<6) */
    0x40, /* 2: B    = (1<<6) */
    0x40, /* 3: B    = (1<<6) */
    0xC0, /* 4: B,F  = (1<<6)|(1<<7) */
    0x80, /* 5: F    = (1<<7) */
    0x80, /* 6: F    = (1<<7) */
    0x40, /* 7: B    = (1<<6) */
    0xC0, /* 8: B,F  = (1<<6)|(1<<7) */
    0xC0, /* 9: B,F  = (1<<6)|(1<<7) */
};

/* Display buffer: aktivní segmentové bity pro každý port, pro 3 číslice */
/* 0 = vše zhasnuto (žádné segmenty aktivní) */
volatile uint8_t disp_b[3];
volatile uint8_t disp_c[3];
volatile uint8_t disp_d[3];

static volatile uint8_t current_digit = 0;

/* Enkodér: stav a výstupní hodnoty */
static volatile int16_t enc_counter = 0;
static volatile uint8_t enc_button  = 0;   /* 1 = stisknuto (debouncovaně) */
static uint8_t enc_last_clk = 0;
static uint8_t sw_debounce = 0;
static uint8_t sw_last = 0;

/* Timer0 overflow - multiplexing + polling enkodéru (~488 Hz) */
ISR(TIMER0_OVF_vect)
{
    /* --- Display multiplexing --- */

    /* Zhasnout všechny číslice (HIGH = neaktivní) */
    PORTD |= DIG_MASK;

    /* Všechny segmenty OFF (HIGH = pull-up) */
    PORTB |= SEG_B_MASK;
    PORTC |= SEG_C_MASK;
    PORTD |= SEG_D_MASK;

    /* Aktivní segmenty LOW */
    PORTB &= ~disp_b[current_digit];
    PORTC &= ~disp_c[current_digit];
    PORTD &= ~disp_d[current_digit];

    /* Aktivovat aktuální číslici (LOW) */
    PORTD &= ~(1 << dig_pin[current_digit]);

    if (++current_digit >= 3)
        current_digit = 0;

    /* --- Polling enkodéru --- */
    uint8_t pind = PIND;
    uint8_t clk_now = !(pind & (1 << ENC_CLK));

    /* Detekce hrany CLK: falling edge (1->0 v aktivním stavu) */
    if (clk_now && !enc_last_clk) {
        if (pind & (1 << ENC_DT))
            enc_counter++;
        else
            enc_counter--;
    }
    enc_last_clk = clk_now;

    /* Debounce tlačítka (~8 ms = 4 vzorky při 488 Hz) */
    uint8_t sw_now = !(pind & (1 << ENC_SW));
    if (sw_now == sw_last) {
        if (sw_debounce < 4)
            sw_debounce++;
        if (sw_debounce >= 4)
            enc_button = sw_now;
    } else {
        sw_debounce = 0;
    }
    sw_last = sw_now;
}

static void display_init(void)
{
    /* PORTB: PB0 výstup (seg A), PB1/PB2 výstup (active HIGH, start LOW), ostatní pull-up */
    DDRB  = SEG_B_MASK | (1 << PWM1_PIN) | (1 << PWM2_PIN);
    PORTB = 0xFF & ~(1 << PWM1_PIN) & ~(1 << PWM2_PIN);  /* PB1,PB2 start LOW */

    /* PORTC: PC0-PC4 výstup (segmenty), PC5 vstup s pull-up (1-Wire idle) */
    DDRC  = SEG_C_MASK;
    PORTC = 0x3F;

    /* PORTD: PD0-PD2 výstup (digit), PD6-PD7 výstup (seg B,F), PD3-PD5 vstup s pull-up */
    DDRD  = DIG_MASK | SEG_D_MASK;  /* PD3,PD4,PD5 zůstávají vstup */
    PORTD = 0xFF;                   /* pull-up na všech (i enkodér) */

    /* Timer0: prescaler 64, overflow interrupt */
    /* 8 MHz / 64 / 256 = ~488 Hz */
    TCCR0 = (1 << CS01) | (1 << CS00);
    TIMSK |= (1 << TOIE0);
}

/* Zobrazit 3-místné číslo 0-999 */
void display_number(uint16_t num)
{
    if (num > 999)
        num = 999;

    uint8_t d2 = num % 10; num /= 10;
    uint8_t d1 = num % 10; num /= 10;
    uint8_t d0 = num % 10;

    disp_b[0] = seg_b[d0]; disp_c[0] = seg_c[d0]; disp_d[0] = seg_d[d0];
    disp_b[1] = seg_b[d1]; disp_c[1] = seg_c[d1]; disp_d[1] = seg_d[d1];
    disp_b[2] = seg_b[d2]; disp_c[2] = seg_c[d2]; disp_d[2] = seg_d[d2];
}

/* Nastavit jednu číslici (pos 0-2), volitelně s desetinnou tečkou */
void display_digit(uint8_t pos, uint8_t value, uint8_t dp)
{
    if (pos > 2 || value > 9)
        return;

    disp_b[pos] = seg_b[value];
    disp_c[pos] = seg_c[value];
    disp_d[pos] = seg_d[value];

    if (dp)
        disp_c[pos] |= (1 << PC2);  /* DP ON (active LOW = přidat do active masky) */
}

/* Zhasnout celý display */
void display_off(void)
{
    for (uint8_t i = 0; i < 3; i++) {
        disp_b[i] = 0;
        disp_c[i] = 0;
        disp_d[i] = 0;
    }
}

/* Přečíst aktuální hodnotu enkodéru (atomicky) */
int16_t encoder_get(void)
{
    cli();
    int16_t val = enc_counter;
    sei();
    return val;
}

/* Nastavit hodnotu enkodéru */
void encoder_set(int16_t val)
{
    cli();
    enc_counter = val;
    sei();
}

/* Přečíst stav tlačítka (1 = stisknuto) */
uint8_t encoder_button(void)
{
    return enc_button;
}

/* ====== 1-Wire protokol (DS18B20) ====== */

static uint8_t ow_reset(void)
{
    uint8_t presence;
    cli();
    OW_DDR |= (1 << OW_BIT);    /* LOW */
    OW_PORT &= ~(1 << OW_BIT);
    _delay_us(480);
    OW_DDR &= ~(1 << OW_BIT);   /* uvolnit (pull-up externí) */
    OW_PORT &= ~(1 << OW_BIT);  /* bez interního pull-up */
    _delay_us(70);
    presence = !(OW_PIN & (1 << OW_BIT));
    sei();
    _delay_us(410);
    return presence;
}

static void ow_write_bit(uint8_t bit)
{
    cli();
    OW_DDR |= (1 << OW_BIT);
    OW_PORT &= ~(1 << OW_BIT);
    if (bit) {
        _delay_us(6);
        OW_DDR &= ~(1 << OW_BIT);
        sei();
        _delay_us(64);
    } else {
        _delay_us(60);
        OW_DDR &= ~(1 << OW_BIT);
        sei();
        _delay_us(10);
    }
}

static uint8_t ow_read_bit(void)
{
    uint8_t bit;
    cli();
    OW_DDR |= (1 << OW_BIT);
    OW_PORT &= ~(1 << OW_BIT);
    _delay_us(6);
    OW_DDR &= ~(1 << OW_BIT);
    _delay_us(9);
    bit = !!(OW_PIN & (1 << OW_BIT));
    sei();
    _delay_us(55);
    return bit;
}

static void ow_write_byte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++) {
        ow_write_bit(data & 0x01);
        data >>= 1;
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++)
        data |= (ow_read_bit() << i);
    return data;
}

/* Spustit konverzi teploty (trvá až 750 ms) */
void ds18b20_start(void)
{
    ow_reset();
    ow_write_byte(0xCC);  /* Skip ROM */
    ow_write_byte(0x44);  /* Convert T */
}

/* Přečíst teplotu v 1/10 °C (např. 235 = 23.5°C). Vrátí -999 při chybě. */
int16_t ds18b20_read_temp(void)
{
    if (!ow_reset())
        return -999;

    ow_write_byte(0xCC);  /* Skip ROM */
    ow_write_byte(0xBE);  /* Read Scratchpad */

    uint8_t lsb = ow_read_byte();
    uint8_t msb = ow_read_byte();

    int16_t raw = (int16_t)((msb << 8) | lsb);
    /* raw je v 1/16 °C, převod na 1/10 °C: raw * 10 / 16 */
    return (raw * 10 + 8) / 16;
}

/* Zobrazit teplotu na displeji: XX.X (s tečkou za druhým digitem) */
/* Pokud heater_active = 1, rozsvítí DP3 */
static uint8_t heater_active = 0;

static void display_temp(int16_t temp_x10)
{
    if (temp_x10 < 0 || temp_x10 > 999)
        temp_x10 = 0;

    uint8_t d2 = temp_x10 % 10; temp_x10 /= 10;
    uint8_t d1 = temp_x10 % 10; temp_x10 /= 10;
    uint8_t d0 = temp_x10 % 10;

    disp_b[0] = seg_b[d0]; disp_c[0] = seg_c[d0]; disp_d[0] = seg_d[d0];
    /* DIG2 s desetinnou tečkou */
    disp_b[1] = seg_b[d1]; disp_c[1] = seg_c[d1] | (1 << PC2); disp_d[1] = seg_d[d1];
    /* DIG3: DP3 svítí když topí spirála */
    disp_b[2] = seg_b[d2]; disp_c[2] = seg_c[d2]; disp_d[2] = seg_d[d2];
    if (heater_active)
        disp_c[2] |= (1 << PC2);
}

/* Zobrazit žádanou hodnotu: XX.X s DP1 (první tečka) jako indikace editačního režimu */
static void display_setpoint(int16_t sp_x10)
{
    if (sp_x10 < 0 || sp_x10 > 999)
        sp_x10 = 0;

    uint8_t d2 = sp_x10 % 10; sp_x10 /= 10;
    uint8_t d1 = sp_x10 % 10; sp_x10 /= 10;
    uint8_t d0 = sp_x10 % 10;

    /* DIG1 s DP (indikace edit módu) */
    disp_b[0] = seg_b[d0]; disp_c[0] = seg_c[d0] | (1 << PC2); disp_d[0] = seg_d[d0];
    /* DIG2 s desetinnou tečkou */
    disp_b[1] = seg_b[d1]; disp_c[1] = seg_c[d1] | (1 << PC2); disp_d[1] = seg_d[d1];
    disp_b[2] = seg_b[d2]; disp_c[2] = seg_c[d2]; disp_d[2] = seg_d[d2];
}

/* Čekání na uvolnění tlačítka enkodéru */
static void wait_button_release(void)
{
    while (encoder_button())
        ;
    _delay_ms(50);  /* krátký debounce po uvolnění */
}

/* ====== PWM ventilátor (Timer1, OC1A = PB1) ====== */

/* Invertovaná tabulka: 0%=0(LOW), 10%=25, ... 100%=255(HIGH) */
static const uint8_t pwm_table[] = {
    0, 25, 51, 76, 102, 128, 153, 179, 204, 230, 255
};

static void pwm_init(void)
{
    /* Timer1: Fast PWM 8-bit, non-inverting na OC1A (PB1) */
    /* HIGH když counter < OCR1A, LOW když counter >= OCR1A */
    /* 16 MHz / 1 / 256 = 62.5 kHz */
    TCCR1A = (1 << COM1A1) | (1 << WGM10);
    TCCR1B = (1 << WGM12) | (1 << CS10);
    OCR1A  = pwm_table[5];  /* 50% výchozí */
}

static void pwm_set(uint8_t step)
{
    if (step > 10) step = 10;
    OCR1A = pwm_table[step];
}

/* Zobrazit fan speed: "FXX" (0-90%) nebo "100" */
#define CHAR_F_B  0x01        /* A=PB0 */
#define CHAR_F_C  0x12        /* G=PC1, E=PC4 */
#define CHAR_F_D  0x80        /* F=PD7 */

static void display_fan(uint8_t fan_pct)
{
    if (fan_pct >= 100) {
        /* "100" */
        disp_b[0] = seg_b[1]; disp_c[0] = seg_c[1]; disp_d[0] = seg_d[1];
        disp_b[1] = seg_b[0]; disp_c[1] = seg_c[0]; disp_d[1] = seg_d[0];
        disp_b[2] = seg_b[0]; disp_c[2] = seg_c[0]; disp_d[2] = seg_d[0];
    } else {
        /* "FXX" */
        disp_b[0] = CHAR_F_B; disp_c[0] = CHAR_F_C; disp_d[0] = CHAR_F_D;
        uint8_t tens  = fan_pct / 10;
        uint8_t units = fan_pct % 10;
        disp_b[1] = seg_b[tens];  disp_c[1] = seg_c[tens];  disp_d[1] = seg_d[tens];
        disp_b[2] = seg_b[units]; disp_c[2] = seg_c[units]; disp_d[2] = seg_d[units];
    }
}

/* ====== Topná spirála (ON/OFF s hysterezí, PB2) ====== */

static void heater_set(uint8_t on)
{
    heater_active = on;
    if (on)
        PORTB |= (1 << PWM2_PIN);   /* HIGH = aktivní */
    else
        PORTB &= ~(1 << PWM2_PIN);  /* LOW = neaktivní */
}

/* Vyhodnotit stav spirály na základě teploty a setpointu */
static void heater_update(int16_t temp_x10, int16_t setpoint_x10)
{
    if (temp_x10 <= setpoint_x10 - HEATER_HYST)
        heater_set(1);
    else if (temp_x10 >= setpoint_x10)
        heater_set(0);
    /* v pásmu hystereze: ponechat současný stav */
}

/* ====== EEPROM: uložení setpointu ====== */

#define EEPROM_MAGIC     0xDB   /* kontrolní byte pro validaci */
#define EEPROM_ADDR_MAGIC  ((uint8_t *)0)
#define EEPROM_ADDR_SP     ((int16_t *)1)

static int16_t eeprom_load_setpoint(int16_t default_val)
{
    if (eeprom_read_byte(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC)
        return default_val;

    int16_t val = eeprom_read_word((uint16_t *)EEPROM_ADDR_SP);
    if (val < 0 || val > 999)
        return default_val;

    return val;
}

static void eeprom_save_setpoint(int16_t val)
{
    eeprom_update_byte(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
    eeprom_update_word((uint16_t *)EEPROM_ADDR_SP, (uint16_t)val);
}

/* ====== Hlavní program ====== */

#define MODE_TEMP      0
#define MODE_FAN       1
#define MODE_SETPOINT  2

int main(void)
{
    display_init();
    pwm_init();
    sei();

    int16_t setpoint = eeprom_load_setpoint(320);  /* z EEPROM nebo 32.0°C */
    int8_t  fan_step = 5;     /* 50% */
    uint8_t mode = MODE_TEMP;
    uint8_t fan_timeout = 0;  /* odpočet 2 s (40 × 50 ms) */
    int16_t last_enc;

    ds18b20_start();
    _delay_ms(750);
    last_enc = encoder_get();

    while (1) {
        /* Přečíst delta enkodéru */
        int16_t enc_now = encoder_get();
        int16_t delta   = enc_now - last_enc;
        last_enc = enc_now;

        switch (mode) {

        /* --- Teplota (normální režim) --- */
        case MODE_TEMP:
        {
            int16_t temp = ds18b20_read_temp();
            ds18b20_start();
            heater_update(temp, setpoint);
            display_temp(temp);

            /* Rotace enkodéru → přepni na fan */
            if (delta) {
                fan_step += delta;
                if (fan_step < 0)  fan_step = 0;
                if (fan_step > 10) fan_step = 10;
                pwm_set(fan_step);
                display_fan(fan_step * 10);
                fan_timeout = 40;  /* 2 s */
                mode = MODE_FAN;
                break;
            }

            /* Stisk tlačítka → editace setpointu */
            if (encoder_button()) {
                wait_button_release();
                mode = MODE_SETPOINT;
                display_setpoint(setpoint);
            }

            _delay_ms(250);
            break;
        }

        /* --- Zobrazení fan rychlosti --- */
        case MODE_FAN:
        {
            if (delta) {
                fan_step += delta;
                if (fan_step < 0)  fan_step = 0;
                if (fan_step > 10) fan_step = 10;
                pwm_set(fan_step);
                display_fan(fan_step * 10);
                fan_timeout = 40;  /* reset timeoutu */
            }

            /* Tlačítko → editace setpointu */
            if (encoder_button()) {
                wait_button_release();
                mode = MODE_SETPOINT;
                display_setpoint(setpoint);
                break;
            }

            /* Timeout → zpět na teplotu */
            if (--fan_timeout == 0) {
                mode = MODE_TEMP;
                ds18b20_start();
                _delay_ms(750);
            }

            _delay_ms(50);
            break;
        }

        /* --- Editace žádané hodnoty --- */
        case MODE_SETPOINT:
        {
            setpoint += delta;
            if (setpoint < 0)   setpoint = 0;
            if (setpoint > 999) setpoint = 999;

            display_setpoint(setpoint);

            /* Stisk tlačítka → ulož a vrať se */
            if (encoder_button()) {
                eeprom_save_setpoint(setpoint);
                wait_button_release();
                mode = MODE_TEMP;
                ds18b20_start();
                _delay_ms(750);
            }

            _delay_ms(50);
            break;
        }
        }
    }

    return 0;
}
