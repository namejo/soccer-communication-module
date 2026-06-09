#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>

void status_led_init();
void status_led_set_play(bool play);
bool status_led_set_custom(uint8_t mode, uint8_t red, uint8_t green, uint8_t blue, uint16_t duration_ms);
void status_led_update();

#endif // STATUS_LED_H
