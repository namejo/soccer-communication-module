#include <stdio.h>
#include "WString.h"
#include <sys/_stdint.h>
#include <Arduino.h>
#include "HardwareSerial.h"
#include "freertos/projdefs.h"
#include <stdint.h>
#include "esp_err.h"

#include "definitions.h"
#include "functions.h"
#include "interbot_comm.h"
#include "sibcp_protocol.h"
#include "state_machine.h"
#include "ble.h"
#include "ble_processing.h"

static QueueHandle_t ble_msg_queue;

static int8_t expected_data_length(uint8_t msg_id) {
    switch (msg_id) {
        case BLE_MSG_PING:
            return -1;
        case BLE_MSG_FW_VERSION:
        case BLE_MSG_PLAY:
        case BLE_MSG_STOP:
            return 0;
        case BLE_MSG_SET_NAME:
        case BLE_MSG_SET_SCORE:
        case BLE_MSG_GAME_OVER:
            return 2;
        case BLE_MSG_DAMAGE:
        case BLE_MSG_HALF_BREAK:
            return 4;
        default:
            return -2;
    }
}

static bool has_valid_data_length(const ble_msg_t &msg) {
    int8_t expected_length = expected_data_length(msg.msg_id);
    if (expected_length == -1) {
        return msg.data_length <= BLE_DATA_MAX_LENGTH;
    }

    return expected_length >= 0 && msg.data_length == expected_length;
}

static void update_score(uint8_t my_score, uint8_t opponent_score) {
    uint8_t previous_my_score = module_get_my_score();
    uint8_t previous_opponent_score = module_get_opponent_score();

    module_set_my_score(my_score);
    module_set_opponent_score(opponent_score);
    interbot_comm_send_system_score(my_score, opponent_score);

    if (my_score > previous_my_score) {
        interbot_comm_send_system_referee_event(SIBCP_REF_EVENT_GOAL_OWN);
    }

    if (opponent_score > previous_opponent_score) {
        interbot_comm_send_system_referee_event(SIBCP_REF_EVENT_GOAL_OPPONENT);
    }
}

int8_t ble_msg_processing_init() {
    ble_msg_queue = xQueueCreate(BLE_QUEUE_MAX_SIZE, sizeof(ble_msg_t));

    return ESP_OK;
}

int8_t ble_msg_processing() {
    ble_msg_t received_msg;
    ble_msg_t send_msg;

    if( xQueueReceive( ble_msg_queue, &received_msg, 0) != pdTRUE) {
        // Nothing in receive queue
        return ESP_OK;
    }

    if (!has_valid_data_length(received_msg)) {
        return ESP_OK;
    }

    switch(received_msg.msg_id) {
        case BLE_MSG_PING:
            ble_send_msg((uint8_t*) &received_msg, received_msg.data_length + 1); // +1 for MSG ID witch is not couted in data_length
            break;
        case BLE_MSG_FW_VERSION:
            send_msg.msg_id = BLE_MSG_FW_VERSION;
            send_msg.data[0] = FW_VERSION_MAJOR;
            send_msg.data[1] = FW_VERSION_MINOR;
            ble_send_msg((uint8_t*) &send_msg, 3);
            break;
        case BLE_MSG_SET_NAME:
            module_set_indicator(String((char)received_msg.data[0]) + String((char)received_msg.data[1]));
            break;
        case BLE_MSG_SET_SCORE:
            update_score(received_msg.data[0], received_msg.data[1]);
            break;
        case BLE_MSG_PLAY:
            stm_set_state(STM_PLAY);
            break;
        case BLE_MSG_STOP:
            stm_set_state(STM_STOP);
            break;        
        case BLE_MSG_DAMAGE:
            stm_set_timer((uint32_t) received_msg.data[0] << 24 | (uint32_t) received_msg.data[1] << 16 | (uint32_t) received_msg.data[2] << 8 | (uint32_t) received_msg.data[3]);
            stm_set_state(STM_DAMAGE);
            break;
        case BLE_MSG_HALF_BREAK:
            stm_set_timer((uint32_t) received_msg.data[0] << 24 | (uint32_t) received_msg.data[1] << 16 | (uint32_t) received_msg.data[2] << 8 | (uint32_t) received_msg.data[3]);
            stm_set_state(STM_HALF_TIME);
            break;            
        case BLE_MSG_GAME_OVER:
            update_score(received_msg.data[0], received_msg.data[1]);
            stm_set_state(STM_GAME_OVER);
            break;
        default:
            //Serial.println("ERROR: Unknown message type");
            break;
    }
    return ESP_OK;
}

QueueHandle_t ble_msg_processing_get_queue() {
    return ble_msg_queue;
}

void ble_msg_processing_ask_for_penalty() {
    if (stm_get_state() == STM_PLAY) {
        uint8_t data = BLE_MSG_ASK_FOR_PENALTY;
        ble_send_msg(&data, 1);
    }
}
