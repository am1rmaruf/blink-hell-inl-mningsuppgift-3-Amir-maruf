#include <avr/io.h>
#include <avr/interrupt.h>
#include "millis.h"

volatile uint32_t g_millis = 0;

ISR(TIMER0_COMPA_vect)
{
    g_millis++;
}

void millis_init(void)
{
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00);
    OCR0A = 249;
    TIMSK0 = (1 << OCIE0A);
}

uint32_t millis_now(void)
{
    uint32_t ms;
    cli();
    ms = g_millis;
    sei();
    return ms;
}