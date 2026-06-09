#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

void buzzer_init();
void buzzer_notify_state_change();
bool buzzer_play_melody(uint8_t melody_id, uint8_t repeat);
void buzzer_update();

#endif // BUZZER_H
