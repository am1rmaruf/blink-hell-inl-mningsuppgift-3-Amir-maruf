#define F_CPU 16000000UL

#include "millis.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

#define LED_WHITE PB2
#define LED_BLUE PB3
#define LED_GREEN PB4
#define LED_RED PB5

#define RGB_G PB0
#define BTN_TOGGLE PB1

#define RGB_B PD6
#define RGB_R PD7

#define ENC_CLK PD2
#define ENC_DT PD3
#define ENC_SW PD4
#define BTN_RESET PD5

#define POT_CHANNEL 0

#define DEBOUNCE_MS 40UL
#define RGB_BLINK_MS 250UL

typedef enum {
    OFF = 0,
    RED,
    GREEN,
    BLUE,
    WHITE
} Color;

typedef struct {
    volatile uint8_t *pinReg;
    uint8_t pinMask;
    uint8_t lastRead;
    uint8_t stable;
    uint32_t lastTime;
} Button;

Color selectedColor = OFF;

bool activeMode = false;
bool normalBlink = false;
bool rgbBlink = false;

uint32_t lastBlinkTime = 0;
uint32_t lastRgbTime = 0;

uint8_t lastClk = 1;

bool redOverride = false;
bool greenOverride = false;
bool blueOverride = false;
bool whiteOverride = false;

bool redState = false;
bool greenState = false;
bool blueState = false;
bool whiteState = false;

Button encBtn = {&PIND, (1 << ENC_SW), 1, 1, 0};
Button toggleBtn = {&PINB, (1 << BTN_TOGGLE), 1, 1, 0};
Button resetBtn = {&PIND, (1 << BTN_RESET), 1, 1, 0};

void adc_init(void) {
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t adc_read(uint8_t channel) {
    channel &= 0x07;
    ADMUX = (ADMUX & 0xF8) | channel;
    ADCSRA |= (1 << ADSC);

    while (ADCSRA & (1 << ADSC)) {
    }

    return ADC;
}

void gpio_init(void) {
    DDRB |= (1 << LED_WHITE) | (1 << LED_BLUE) | (1 << LED_GREEN) | (1 << LED_RED);
    DDRB |= (1 << RGB_G);
    DDRD |= (1 << RGB_B) | (1 << RGB_R);

    DDRB &= ~(1 << BTN_TOGGLE);
    DDRD &= ~((1 << ENC_CLK) | (1 << ENC_DT) | (1 << ENC_SW) | (1 << BTN_RESET));
    DDRC &= ~(1 << PC0);

    PORTB |= (1 << BTN_TOGGLE);
    PORTD |= (1 << ENC_CLK) | (1 << ENC_DT) | (1 << ENC_SW) | (1 << BTN_RESET);

    PORTB &= ~((1 << LED_WHITE) | (1 << LED_BLUE) | (1 << LED_GREEN) | (1 << LED_RED));
    PORTB &= ~(1 << RGB_G);
    PORTD &= ~((1 << RGB_R) | (1 << RGB_B));
}

void clear_rgb(void) {
    PORTB &= ~(1 << RGB_G);
    PORTD &= ~((1 << RGB_R) | (1 << RGB_B));
}

void show_rgb(void) {
    clear_rgb();

    if (selectedColor == RED) {
        PORTD |= (1 << RGB_R);
    } else if (selectedColor == GREEN) {
        PORTB |= (1 << RGB_G);
    } else if (selectedColor == BLUE) {
        PORTD |= (1 << RGB_B);
    } else if (selectedColor == WHITE) {
        PORTD |= (1 << RGB_R);
        PORTB |= (1 << RGB_G);
        PORTD |= (1 << RGB_B);
    }
}

void rgb_blink_func(void) {
    uint32_t now = millis_now();

    if (now - lastRgbTime >= RGB_BLINK_MS) {
        lastRgbTime = now;
        rgbBlink = !rgbBlink;
    }

    if (rgbBlink) {
        show_rgb();
    } else {
        clear_rgb();
    }
}

uint32_t get_blink_time(void) {
    uint16_t pot = adc_read(POT_CHANNEL);
    uint16_t value = (uint16_t)((pot * 255UL) / 1023UL);
    return 250UL + ((uint32_t)value * 10UL);
}

bool button_pressed(Button *btn) {
    uint8_t readNow = 0;
    uint32_t now = millis_now();

    if (*btn->pinReg & btn->pinMask) {
        readNow = 1;
    }

    if (readNow != btn->lastRead) {
        btn->lastRead = readNow;
        btn->lastTime = now;
    }

    if ((now - btn->lastTime) > DEBOUNCE_MS) {
        if (readNow != btn->stable) {
            btn->stable = readNow;

            if (btn->stable == 0) {
                return true;
            }
        }
    }

    return false;
}

void set_led(uint8_t pin, bool on) {
    if (on) {
        PORTB |= (1 << pin);
    } else {
        PORTB &= ~(1 << pin);
    }
}

void update_leds(void) {
    uint32_t now = millis_now();
    uint32_t blinkTime = get_blink_time();

    if (now - lastBlinkTime >= blinkTime) {
        lastBlinkTime = now;
        normalBlink = !normalBlink;
    }

    if (whiteOverride) {
        set_led(LED_WHITE, whiteState);
    } else {
        set_led(LED_WHITE, normalBlink);
    }

    if (blueOverride) {
        set_led(LED_BLUE, blueState);
    } else {
        set_led(LED_BLUE, normalBlink);
    }

    if (greenOverride) {
        set_led(LED_GREEN, greenState);
    } else {
        set_led(LED_GREEN, normalBlink);
    }

    if (redOverride) {
        set_led(LED_RED, redState);
    } else {
        set_led(LED_RED, normalBlink);
    }
}

void encoder_step(void) {
    if (activeMode) {
        return;
    }

    uint8_t clk = 0;
    uint8_t dt = 0;

    if (PIND & (1 << ENC_CLK)) {
        clk = 1;
    }

    if (clk != lastClk && clk == 0) {
        if (PIND & (1 << ENC_DT)) {
            dt = 1;
        }

        if (dt != clk) {
            selectedColor = (Color)((selectedColor + 1) % 5);
        } else {
            if (selectedColor == OFF) {
                selectedColor = WHITE;
            } else {
                selectedColor = (Color)(selectedColor - 1);
            }
        }
    }

    lastClk = clk;
}

void encoder_button_func(void) {
    if (button_pressed(&encBtn)) {
        if (selectedColor == OFF) {
            activeMode = false;
            clear_rgb();
            return;
        }

        activeMode = !activeMode;

        if (activeMode) {
            lastRgbTime = millis_now();
            rgbBlink = false;
        }
    }
}

void toggle_led_func(void) {
    if (selectedColor == RED) {
        redOverride = true;
        redState = !redState;
    } else if (selectedColor == GREEN) {
        greenOverride = true;
        greenState = !greenState;
    } else if (selectedColor == BLUE) {
        blueOverride = true;
        blueState = !blueState;
    } else if (selectedColor == WHITE) {
        whiteOverride = true;
        whiteState = !whiteState;
    }
}

void reset_led_func(void) {
    if (selectedColor == RED) {
        redOverride = false;
    } else if (selectedColor == GREEN) {
        greenOverride = false;
    } else if (selectedColor == BLUE) {
        blueOverride = false;
    } else if (selectedColor == WHITE) {
        whiteOverride = false;
    }
}

void handle_buttons(void) {
    if (!activeMode || selectedColor == OFF) {
        return;
    }

    if (button_pressed(&toggleBtn)) {
        toggle_led_func();
    }

    if (button_pressed(&resetBtn)) {
        reset_led_func();
    }
}

int main(void) {
    gpio_init();
    adc_init();
    millis_init();

    if (PIND & (1 << ENC_CLK)) {
        lastClk = 1;
    } else {
        lastClk = 0;
    }

    sei();

    while (1) {
        update_leds();
        encoder_step();
        encoder_button_func();
        handle_buttons();

        if (activeMode) {
            rgb_blink_func();
        } else {
            show_rgb();
        }
    }

    return 0;
}