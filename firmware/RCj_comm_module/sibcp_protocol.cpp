#include "sibcp_protocol.h"

#include <string.h>

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = value & 0xff;
    data[1] = value >> 8;
}

static void fill_frame_metadata(sibcp_frame_t *frame)
{
    frame->packet_type = frame->data[2];
    frame->transaction_id = read_u16_le(&frame->data[3]);
    frame->identifier_id = frame->data[5];
    frame->payload_length = read_u16_le(&frame->data[6]);
}

void sibcp_parser_reset(sibcp_parser_t *parser)
{
    parser->length = 0;
    parser->expected_length = 0;
}

uint16_t sibcp_crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;

        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool sibcp_build_frame(
    uint8_t packet_type,
    uint16_t transaction_id,
    uint8_t identifier_id,
    const uint8_t *payload,
    uint16_t payload_length,
    sibcp_frame_t *frame
)
{
    if (frame == NULL || payload_length > SIBCP_MAX_PAYLOAD_LENGTH) {
        return false;
    }

    if (payload_length > 0 && payload == NULL) {
        return false;
    }

    frame->data[0] = SIBCP_START_BYTE_0;
    frame->data[1] = SIBCP_START_BYTE_1;
    frame->data[2] = packet_type;
    write_u16_le(&frame->data[3], transaction_id);
    frame->data[5] = identifier_id;
    write_u16_le(&frame->data[6], payload_length);

    if (payload_length > 0) {
        memcpy(&frame->data[SIBCP_HEADER_LENGTH], payload, payload_length);
    }

    frame->length = SIBCP_HEADER_LENGTH + payload_length + SIBCP_CRC_LENGTH;
    uint16_t crc = sibcp_crc16_ccitt(&frame->data[2], payload_length + SIBCP_HEADER_LENGTH - 2);
    write_u16_le(&frame->data[frame->length - SIBCP_CRC_LENGTH], crc);
    fill_frame_metadata(frame);

    return true;
}

bool sibcp_validate_frame(const uint8_t *data, size_t length, sibcp_frame_t *frame)
{
    if (length < SIBCP_HEADER_LENGTH + SIBCP_CRC_LENGTH || length > SIBCP_MAX_FRAME_LENGTH) {
        return false;
    }

    if (data[0] != SIBCP_START_BYTE_0 || data[1] != SIBCP_START_BYTE_1) {
        return false;
    }

    uint16_t payload_length = read_u16_le(&data[6]);
    if (payload_length > SIBCP_MAX_PAYLOAD_LENGTH) {
        return false;
    }

    size_t expected_length = SIBCP_HEADER_LENGTH + payload_length + SIBCP_CRC_LENGTH;
    if (length != expected_length) {
        return false;
    }

    uint16_t expected_crc = read_u16_le(&data[length - SIBCP_CRC_LENGTH]);
    uint16_t actual_crc = sibcp_crc16_ccitt(&data[2], length - SIBCP_CRC_LENGTH - 2);
    if (expected_crc != actual_crc) {
        return false;
    }

    memcpy(frame->data, data, length);
    frame->length = length;
    fill_frame_metadata(frame);

    return true;
}

bool sibcp_parser_push_byte(sibcp_parser_t *parser, uint8_t byte, sibcp_frame_t *frame)
{
    if (parser->length == 0) {
        if (byte == SIBCP_START_BYTE_0) {
            parser->buffer[parser->length++] = byte;
        }
        return false;
    }

    if (parser->length == 1) {
        if (byte == SIBCP_START_BYTE_1) {
            parser->buffer[parser->length++] = byte;
        } else if (byte != SIBCP_START_BYTE_0) {
            sibcp_parser_reset(parser);
        }
        return false;
    }

    if (parser->length >= SIBCP_MAX_FRAME_LENGTH) {
        sibcp_parser_reset(parser);
        return false;
    }

    parser->buffer[parser->length++] = byte;

    if (parser->length == SIBCP_HEADER_LENGTH) {
        uint16_t payload_length = read_u16_le(&parser->buffer[6]);
        if (payload_length > SIBCP_MAX_PAYLOAD_LENGTH) {
            sibcp_parser_reset(parser);
            return false;
        }

        parser->expected_length = SIBCP_HEADER_LENGTH + payload_length + SIBCP_CRC_LENGTH;
    }

    if (parser->expected_length > 0 && parser->length == parser->expected_length) {
        bool valid = sibcp_validate_frame(parser->buffer, parser->length, frame);
        sibcp_parser_reset(parser);
        return valid;
    }

    return false;
}

const char *sibcp_packet_type_name(uint8_t packet_type)
{
    switch (packet_type) {
        case SIBCP_PACKET_TOPIC:
            return "TOPIC";
        case SIBCP_PACKET_SERVICE_REQUEST:
            return "SRV_REQ";
        case SIBCP_PACKET_SERVICE_RESPONSE:
            return "SRV_RES";
        case SIBCP_PACKET_SERVICE_DISCOVERY:
            return "DISCOVERY";
        default:
            return "UNKNOWN";
    }
}
