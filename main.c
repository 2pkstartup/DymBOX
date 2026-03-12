/*
 * dymbox - ATmega8, 3-digit 7-segment display (common anode)
 *
 * Zapojení (active LOW - pull-up na všech výstupech):
 *
 * Segmenty:
 *   A  = PB0     E  = PC4
 *   B  = PD6     F  = PD7
 *   C  = PC0     G  = PC1
 *   D  = PC3     DP = PC2
 *
 * Výběr číslic (PORTD, active LOW):
 *   PD2 = DIG1 (stovky)
 *   PD1 = DIG2 (desítky)
 *   PD0 = DIG3 (jednotky)
 *
 * Rotační enkodér KY-040:
 *   PD3 = CLK (A)
 *   PD4 = DT  (B)
 *   PD5 = SW  (tlačítko)
 *
 * Všechny výstupy: HIGH = neaktivní (pull-up), LOW = aktivní
 */

#include <avr/io.h>
#include <avr/interrupt.h>
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
    /* PORTB: PB0 výstup (seg A), ostatní vstup s pull-up */
    DDRB  = SEG_B_MASK;
    PORTB = 0xFF;

    /* PORTC: PC0-PC4 výstup (seg G,C,D,DP,E), PC5 vstup s pull-up */
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

int main(void)
{
    display_init();
    sei();

    int16_t value = 0;
    encoder_set(0);

    while (1) {
        value = encoder_get();

        /* Omezit rozsah 0-999 */
        if (value < 0) {
            value = 0;
            encoder_set(0);
        } else if (value > 999) {
            value = 999;
            encoder_set(999);
        }

        display_number(value);

        /* Tlačítko = reset na 0 */
        if (encoder_button()) {
            encoder_set(0);
            while (encoder_button())
                ;  /* počkat na uvolnění */
        }
    }

    return 0;
}
