#include "buzzer.h"

#include <Arduino.h>
#include <stdint.h>

#include "definitions.h"
#include "sibcp_protocol.h"

#define MELODY_REPEAT_MAX 4

struct melody_tone_t {
    uint16_t frequency_hz;
    uint16_t duration_ms;
};

static const melody_tone_t goal_melody[] = {
    {2700, 90},
    {0, 50},
    {3200, 110},
    {0, 50},
    {2700, 160},
};

static const melody_tone_t ack_melody[] = {
    {2700, 70},
    {0, 35},
    {2700, 70},
};

static bool buzzer_ready = false;
static bool buzzer_active = false;
static uint32_t buzzer_stop_time = 0;
static const melody_tone_t *active_melody = NULL;
static uint8_t active_melody_length = 0;
static uint8_t active_melody_index = 0;
static uint8_t active_melody_repeat_left = 0;
static uint32_t active_tone_stop_time = 0;

static void stop_tone()
{
    ledcWriteTone(BUZZER_GPIO, 0);
}

static void stop_melody()
{
    active_melody = NULL;
    active_melody_length = 0;
    active_melody_index = 0;
    active_melody_repeat_left = 0;
    active_tone_stop_time = 0;
}

static bool select_melody(uint8_t melody_id, const melody_tone_t **melody, uint8_t *length)
{
    switch (melody_id) {
        case SIBCP_MELODY_GOAL:
            *melody = goal_melody;
            *length = sizeof(goal_melody) / sizeof(goal_melody[0]);
            return true;
        case SIBCP_MELODY_ACK:
            *melody = ack_melody;
            *length = sizeof(ack_melody) / sizeof(ack_melody[0]);
            return true;
        default:
            return false;
    }
}

static void start_current_melody_tone()
{
    if (active_melody == NULL || active_melody_index >= active_melody_length) {
        stop_melody();
        stop_tone();
        return;
    }

    melody_tone_t tone = active_melody[active_melody_index];
    ledcWriteTone(BUZZER_GPIO, tone.frequency_hz);
    active_tone_stop_time = millis() + tone.duration_ms;
}

void buzzer_init()
{
    pinMode(BUZZER_GPIO, OUTPUT);
    digitalWrite(BUZZER_GPIO, LOW);

    buzzer_ready = ledcAttach(BUZZER_GPIO, BUZZER_FREQUENCY_HZ, 10);

    if (buzzer_ready) {
        stop_tone();
    }
}

void buzzer_notify_state_change()
{
    if (!buzzer_ready) {
        return;
    }

    stop_melody();
    ledcWriteTone(BUZZER_GPIO, BUZZER_FREQUENCY_HZ);
    buzzer_stop_time = millis() + BUZZER_DURATION_MS;
    buzzer_active = true;
}

bool buzzer_play_melody(uint8_t melody_id, uint8_t repeat)
{
    if (!buzzer_ready) {
        return false;
    }

    const melody_tone_t *melody = NULL;
    uint8_t melody_length = 0;
    if (!select_melody(melody_id, &melody, &melody_length)) {
        return false;
    }

    buzzer_active = false;
    active_melody = melody;
    active_melody_length = melody_length;
    active_melody_index = 0;
    active_melody_repeat_left = repeat == 0 ? 1 : repeat;
    if (active_melody_repeat_left > MELODY_REPEAT_MAX) {
        active_melody_repeat_left = MELODY_REPEAT_MAX;
    }
    start_current_melody_tone();

    return true;
}

static void update_melody()
{
    if (active_melody == NULL || (int32_t)(millis() - active_tone_stop_time) < 0) {
        return;
    }

    active_melody_index++;
    if (active_melody_index >= active_melody_length) {
        active_melody_repeat_left--;
        if (active_melody_repeat_left == 0) {
            stop_melody();
            stop_tone();
            return;
        }
        active_melody_index = 0;
    }

    start_current_melody_tone();
}

void buzzer_update()
{
    if (buzzer_active && (int32_t)(millis() - buzzer_stop_time) >= 0) {
        stop_tone();
        buzzer_active = false;
    }

    if (!buzzer_active) {
        update_melody();
    }
}
