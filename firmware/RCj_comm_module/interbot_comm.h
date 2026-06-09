#ifndef INTERBOT_COMM_H
#define INTERBOT_COMM_H

#include "esp_err.h"

esp_err_t interbot_comm_init();
void interbot_comm_update();
void interbot_comm_send_system_game_state(uint8_t state, bool robot_play);
void interbot_comm_send_system_score(uint8_t own_score, uint8_t opponent_score);
void interbot_comm_send_system_match_time(uint8_t half, uint32_t remaining_ms, uint32_t phase_total_ms);
void interbot_comm_send_system_referee_event(uint8_t event);

#endif // INTERBOT_COMM_H
