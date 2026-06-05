#ifndef INTERBOT_COMM_H
#define INTERBOT_COMM_H

#include "esp_err.h"

esp_err_t interbot_comm_init();
void interbot_comm_update();
void interbot_comm_send_system_game_state(uint8_t state, bool robot_play);

#endif // INTERBOT_COMM_H
