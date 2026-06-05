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
    SIBCP_TOPIC_SYSTEM_GAME_STATE = 0xF0,
};

enum sibcp_service_id_t : uint8_t {
    SIBCP_SERVICE_ADD_THREE = 0x01,
};

struct __attribute__((packed)) sibcp_msg_robot_num_t {
    int64_t num;
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

struct __attribute__((packed)) sibcp_srv_add_three_request_t {
    int64_t a;
    int64_t b;
    int64_t c;
};

struct __attribute__((packed)) sibcp_srv_add_three_response_t {
    uint8_t status_code;
    int64_t sum;
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

#endif // SIBCP_PROTOCOL_H
