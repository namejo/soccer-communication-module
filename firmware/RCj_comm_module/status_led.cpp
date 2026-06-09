#include "status_led.h"

#include <Arduino.h>
#include <stdint.h>

#include "definitions.h"
#include "sibcp_protocol.h"

#define CUSTOM_LED_BLINK_MS 250
#define CUSTOM_LED_PULSE_MS 1000

static bool status_play = false;
static bool custom_active = false;
static uint8_t custom_mode = SIBCP_LED_MODE_OFF;
static uint8_t custom_red = 0;
static uint8_t custom_green = 0;
static uint8_t custom_blue = 0;
static uint32_t custom_stop_time = 0;
static uint32_t last_blink_toggle = 0;
static bool blink_on = true;

static uint8_t scale_custom_channel(uint8_t value)
{
    return ((uint16_t)value * RGB_LED_PWM_DUTY) / 255;
}

static void write_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    ledcWrite(RGB_LED_RED_GPIO, red);
    ledcWrite(RGB_LED_GREEN_GPIO, green);
    ledcWrite(RGB_LED_BLUE_GPIO, blue);
}

static void write_status_led()
{
    write_rgb(
        status_play ? 0 : RGB_LED_PWM_DUTY,
        status_play ? RGB_LED_PWM_DUTY : 0,
        0
    );
}

static void write_custom_led(uint8_t scale)
{
    write_rgb(
        ((uint16_t)scale_custom_channel(custom_red) * scale) / 255,
        ((uint16_t)scale_custom_channel(custom_green) * scale) / 255,
        ((uint16_t)scale_custom_channel(custom_blue) * scale) / 255
    );
}

void status_led_init()
{
    ledcAttach(RGB_LED_RED_GPIO, RGB_LED_PWM_HZ, RGB_LED_PWM_RESOLUTION);
    ledcAttach(RGB_LED_GREEN_GPIO, RGB_LED_PWM_HZ, RGB_LED_PWM_RESOLUTION);
    ledcAttach(RGB_LED_BLUE_GPIO, RGB_LED_PWM_HZ, RGB_LED_PWM_RESOLUTION);

    write_rgb(0, 0, 0);
}

void status_led_set_play(bool play)
{
    status_play = play;
    custom_active = false;
    write_status_led();
}

bool status_led_set_custom(uint8_t mode, uint8_t red, uint8_t green, uint8_t blue, uint16_t duration_ms)
{
    if (mode > SIBCP_LED_MODE_PULSE) {
        return false;
    }

    custom_active = true;
    custom_mode = mode;
    custom_red = red;
    custom_green = green;
    custom_blue = blue;
    custom_stop_time = duration_ms == 0 ? 0 : millis() + duration_ms;
    last_blink_toggle = millis();
    blink_on = true;

    status_led_update();
    return true;
}

void status_led_update()
{
    if (!custom_active) {
        return;
    }

    uint32_t now = millis();
    if (custom_stop_time != 0 && (int32_t)(now - custom_stop_time) >= 0) {
        custom_active = false;
        write_status_led();
        return;
    }

    switch (custom_mode) {
        case SIBCP_LED_MODE_OFF:
            write_rgb(0, 0, 0);
            break;
        case SIBCP_LED_MODE_SOLID:
            write_custom_led(255);
            break;
        case SIBCP_LED_MODE_BLINK:
            if (last_blink_toggle == 0 || now - last_blink_toggle >= CUSTOM_LED_BLINK_MS) {
                blink_on = !blink_on;
                last_blink_toggle = now;
            }
            write_custom_led(blink_on ? 255 : 0);
            break;
        case SIBCP_LED_MODE_PULSE:
        {
            uint16_t phase = now % CUSTOM_LED_PULSE_MS;
            uint8_t scale = phase < (CUSTOM_LED_PULSE_MS / 2)
                ? (phase * 255) / (CUSTOM_LED_PULSE_MS / 2)
                : ((CUSTOM_LED_PULSE_MS - phase) * 255) / (CUSTOM_LED_PULSE_MS / 2);
            write_custom_led(scale);
            break;
        }
        default:
            custom_active = false;
            write_status_led();
            break;
    }
}
