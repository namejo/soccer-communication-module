#ifndef SIBCP_PROTOCOL_H
#define SIBCP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define SIBCP_START_BYTE_0          0xAA
#define SIBCP_START_BYTE_1          0x55
#define SIBCP_HEADER_LENGTH         8
#define SIBCP_CRC_LENGTH            2
#define SIBCP_MAX_PAYLOAD_LENGTH    240
#define SIBCP_MAX_FRAME_LENGTH      (SIBCP_HEADER_LENGTH + SIBCP_MAX_PAYLOAD_LENGTH + SIBCP_CRC_LENGTH)

enum sibcp_packet_type_t : uint8_t {
    SIBCP_PACKET_TOPIC = 0x01,
    SIBCP_PACKET_SERVICE_REQUEST = 0x02,
    SIBCP_PACKET_SERVICE_RESPONSE = 0x03,
    SIBCP_PACKET_SERVICE_DISCOVERY = 0x04,
};

enum sibcp_topic_id_t : uint8_t {
    SIBCP_TOPIC_ROBOT_NUM = 0x01,
    SIBCP_TOPIC_ROBOT_POSE = 0x10,
    SIBCP_TOPIC_WORLD_BALL = 0x11,
    SIBCP_TOPIC_WORLD_OPPONENT = 0x12,
    SIBCP_TOPIC_SYSTEM_GAME_STATE = 0xF0,
    SIBCP_TOPIC_SYSTEM_SCORE = 0xF1,
    SIBCP_TOPIC_SYSTEM_MATCH_TIME = 0xF2,
    SIBCP_TOPIC_SYSTEM_REFEREE_EVENT = 0xF3,
};

enum sibcp_service_id_t : uint8_t {
    SIBCP_SERVICE_BALL_IN_VISION = 0x01,
    SIBCP_SERVICE_REQUEST_ROLE = 0x10,
    SIBCP_SERVICE_SYSTEM_SET_LED = 0xF0,
    SIBCP_SERVICE_SYSTEM_PLAY_MELODY = 0xF1,
};

enum sibcp_service_status_t : uint8_t {
    SIBCP_SERVICE_STATUS_OK = 0x00,
    SIBCP_SERVICE_STATUS_ERROR = 0x01,
    SIBCP_SERVICE_STATUS_UNSUPPORTED = 0x02,
};

struct __attribute__((packed)) sibcp_msg_robot_num_t {
    int64_t num;
};

struct __attribute__((packed)) sibcp_msg_robot_pose_t {
    int32_t x_mm;
    int32_t y_mm;
    int16_t heading_mrad;
    uint8_t confidence;
    uint32_t timestamp_ms;
};

struct __attribute__((packed)) sibcp_msg_world_ball_t {
    uint8_t visible;
    int32_t x_mm;
    int32_t y_mm;
    uint8_t confidence;
    uint32_t timestamp_ms;
};

struct __attribute__((packed)) sibcp_msg_world_opponent_t {
    uint8_t visible;
    int32_t x_mm;
    int32_t y_mm;
    uint8_t threat;
    uint32_t timestamp_ms;
};

enum sibcp_system_game_state_t : uint8_t {
    SIBCP_GAME_STATE_INIT = 0x00,
    SIBCP_GAME_STATE_DISCONNECTED = 0x01,
    SIBCP_GAME_STATE_PLAY = 0x02,
    SIBCP_GAME_STATE_STOP = 0x03,
    SIBCP_GAME_STATE_DAMAGE = 0x04,
    SIBCP_GAME_STATE_HALF_TIME = 0x05,
    SIBCP_GAME_STATE_GAME_OVER = 0x06,
};

struct __attribute__((packed)) sibcp_msg_system_game_state_t {
    uint8_t state;
    uint8_t robot_play;
};

struct __attribute__((packed)) sibcp_msg_system_score_t {
    uint8_t own_score;
    uint8_t opponent_score;
};

struct __attribute__((packed)) sibcp_msg_system_match_time_t {
    uint8_t half;
    uint32_t remaining_ms;
    uint32_t phase_total_ms;
};

enum sibcp_system_referee_event_t : uint8_t {
    SIBCP_REF_EVENT_GOAL_OWN = 0x01,
    SIBCP_REF_EVENT_GOAL_OPPONENT = 0x02,
    SIBCP_REF_EVENT_PENALTY_STARTED = 0x03,
    SIBCP_REF_EVENT_PENALTY_ENDED = 0x04,
    SIBCP_REF_EVENT_HALF_STARTED = 0x05,
    SIBCP_REF_EVENT_MATCH_ENDED = 0x06,
};

struct __attribute__((packed)) sibcp_msg_system_referee_event_t {
    uint8_t event;
};

enum sibcp_system_led_mode_t : uint8_t {
    SIBCP_LED_MODE_OFF = 0x00,
    SIBCP_LED_MODE_SOLID = 0x01,
    SIBCP_LED_MODE_BLINK = 0x02,
    SIBCP_LED_MODE_PULSE = 0x03,
};

struct __attribute__((packed)) sibcp_srv_system_set_led_request_t {
    uint8_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint16_t duration_ms;
};

enum sibcp_system_melody_id_t : uint8_t {
    SIBCP_MELODY_GOAL = 0x01,
    SIBCP_MELODY_ACK = 0x02,
};

struct __attribute__((packed)) sibcp_srv_system_play_melody_request_t {
    uint8_t melody_id;
    uint8_t repeat;
};

struct sibcp_frame_t {
    uint8_t data[SIBCP_MAX_FRAME_LENGTH];
    uint16_t length;
    uint8_t packet_type;
    uint16_t transaction_id;
    uint8_t identifier_id;
    uint16_t payload_length;
};

struct sibcp_parser_t {
    uint8_t buffer[SIBCP_MAX_FRAME_LENGTH];
    uint16_t length;
    uint16_t expected_length;
};

void sibcp_parser_reset(sibcp_parser_t *parser);
bool sibcp_parser_push_byte(sibcp_parser_t *parser, uint8_t byte, sibcp_frame_t *frame);

bool sibcp_build_frame(
    uint8_t packet_type,
    uint16_t transaction_id,
    uint8_t identifier_id,
    const uint8_t *payload,
    uint16_t payload_length,
    sibcp_frame_t *frame
);
uint16_t sibcp_crc16_ccitt(const uint8_t *data, size_t length);
bool sibcp_validate_frame(const uint8_t *data, size_t length, sibcp_frame_t *frame);
const char *sibcp_packet_type_name(uint8_t packet_type);
bool sibcp_is_system_topic(uint8_t topic_id);
bool sibcp_is_system_service(uint8_t service_id);
bool sibcp_is_reserved_external_frame(const sibcp_frame_t *frame);

#endif // SIBCP_PROTOCOL_H
