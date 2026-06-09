#include <ctime>
#include "esp32-hal.h"
#include <sys/_stdint.h>
#include "WString.h"
#include <Arduino.h>
#include "HardwareSerial.h"
#include "freertos/projdefs.h"
#include <stdint.h>
#include "esp_err.h"

#include "definitions.h"
#include "buzzer.h"
#include "display.h"
#include "interbot_comm.h"
#include "sibcp_protocol.h"
#include "status_led.h"
#include "state_machine.h"

static stm_states current_state = STM_INIT;
static bool state_changed = false;
static bool robot_play = false;
static uint32_t timer_stop = 0;
static uint32_t timer_duration_ms = 0;
static uint32_t last_match_time_publish_ms = 0;
static uint8_t match_half = 1;
static bool second_half_pending = false;
static stm_states last_notified_state = STM_INIT;

static bool is_match_state(stm_states state) {
    return state == STM_PLAY ||
           state == STM_STOP ||
           state == STM_DAMAGE ||
           state == STM_HALF_TIME ||
           state == STM_GAME_OVER;
}

static uint16_t get_remaining_time() {
    uint32_t current_time = millis();
    if (current_time < timer_stop) {
        return (timer_stop - current_time) / 1000;
    }
    return 0;
}

static uint32_t get_remaining_time_ms() {
    uint32_t current_time = millis();
    if (current_time < timer_stop) {
        return timer_stop - current_time;
    }
    return 0;
}

static bool has_countdown(stm_states state) {
    return state == STM_DAMAGE || state == STM_HALF_TIME;
}

static uint8_t get_sibcp_game_state(stm_states state) {
    switch (state) {
        case STM_INIT:
            return SIBCP_GAME_STATE_INIT;
        case STM_DISCONNECTED:
            return SIBCP_GAME_STATE_DISCONNECTED;
        case STM_PLAY:
            return SIBCP_GAME_STATE_PLAY;
        case STM_STOP:
            return SIBCP_GAME_STATE_STOP;
        case STM_DAMAGE:
            return SIBCP_GAME_STATE_DAMAGE;
        case STM_HALF_TIME:
            return SIBCP_GAME_STATE_HALF_TIME;
        case STM_GAME_OVER:
            return SIBCP_GAME_STATE_GAME_OVER;
        default:
            return SIBCP_GAME_STATE_INIT;
    }
}

static void update_output_state() {
    stm_states notified_state = current_state;
    bool countdown_active = has_countdown(notified_state);
    bool penalty_ended = last_notified_state == STM_DAMAGE && notified_state != STM_DAMAGE;

    if (notified_state == STM_DISCONNECTED) {
        match_half = 1;
        second_half_pending = false;
    }

    if (notified_state == STM_PLAY && second_half_pending) {
        match_half = 2;
        second_half_pending = false;
        interbot_comm_send_system_referee_event(SIBCP_REF_EVENT_HALF_STARTED);
    }

    if (robot_play) {
        digitalWrite(OUTPUT1_GPIO, HIGH);
        digitalWrite(OUTPUT2_GPIO, HIGH);
        status_led_set_play(true);
    } else {
        digitalWrite(OUTPUT1_GPIO, LOW);
        digitalWrite(OUTPUT2_GPIO, LOW);
        status_led_set_play(false);
    }

    interbot_comm_send_system_game_state(get_sibcp_game_state(notified_state), robot_play);
    interbot_comm_send_system_match_time(
        match_half,
        countdown_active ? get_remaining_time_ms() : 0,
        countdown_active ? timer_duration_ms : 0
    );
    last_match_time_publish_ms = millis();

    if (penalty_ended) {
        interbot_comm_send_system_referee_event(SIBCP_REF_EVENT_PENALTY_ENDED);
    }

    switch (notified_state) {
        case STM_DAMAGE:
            interbot_comm_send_system_referee_event(SIBCP_REF_EVENT_PENALTY_STARTED);
            break;
        case STM_HALF_TIME:
            second_half_pending = true;
            break;
        case STM_GAME_OVER:
            interbot_comm_send_system_referee_event(SIBCP_REF_EVENT_MATCH_ENDED);
            break;
        default:
            break;
    }

    last_notified_state = notified_state;
}


// States
static void state_init() {
    display_screen_init();
    delay(2000);
    stm_set_state(STM_DISCONNECTED);
}

static void state_wait_connecting() {
    robot_play = false;
    display_screen_wait_for_connection();
}

static void state_play() {
    robot_play = true;
    display_screen_play();
}

static void state_stop() {
    robot_play = false;
    display_screen_stop();
}

static void state_damage() {
    robot_play = false;
    display_screen_damage(get_remaining_time());

}

static void state_half_break() {
    robot_play = false;
    display_screen_half_break(get_remaining_time());
}

static void state_game_over() {
    robot_play = false;
    display_screen_game_over();
}



// Public functions
int8_t stm_set_timer(uint32_t miliseconds) {
    timer_stop = millis() + miliseconds;
    timer_duration_ms = miliseconds;

    return ESP_OK;
}

int8_t stm_init() {
    // Default match timer. State-specific timers overwrite this when needed.
    stm_set_timer(660000);

    return ESP_OK;
}

void stm_set_state(stm_states state) {
    current_state = state;
    state_changed = true;
}

stm_states stm_get_state() {
    return current_state;
}

int8_t stm_update() {
    bool change_noted = false;

    if (state_changed) change_noted = true;

    switch (current_state) {
        case STM_INIT:
            state_init();
            break;
        case STM_DISCONNECTED:
            if (state_changed) {
                state_wait_connecting();
            }
            break;
        case STM_PLAY:
            state_play();
            break;
        case STM_STOP:
            state_stop();
            break;
        case STM_DAMAGE:
            state_damage();
            break; 
        case STM_HALF_TIME:
            state_half_break();
            break;
        case STM_GAME_OVER:
            state_game_over();
            break;             
        default:
            // statements
            break;
    }

    if (state_changed) {
        update_output_state();
        if (is_match_state(current_state)) {
            buzzer_notify_state_change();
        }
    }

    if (change_noted) state_changed = false;

    buzzer_update();
    status_led_update();

    if (has_countdown(current_state) && millis() - last_match_time_publish_ms >= 1000) {
        last_match_time_publish_ms = millis();
        interbot_comm_send_system_match_time(match_half, get_remaining_time_ms(), timer_duration_ms);
    }

    return ESP_OK;
}
