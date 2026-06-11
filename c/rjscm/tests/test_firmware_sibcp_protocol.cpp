#include "sibcp_protocol.h"

#include <cstdio>
#include <cstring>

static int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

static int expect_reserved(
    uint8_t packet_type,
    uint8_t identifier_id,
    bool expected_reserved
)
{
    const uint8_t payload[] = {0x01};
    sibcp_frame_t frame;
    if (!sibcp_build_frame(packet_type, 7, identifier_id, payload, sizeof(payload), &frame)) {
        return fail("failed to build frame");
    }

    sibcp_frame_t validated;
    if (!sibcp_validate_frame(frame.data, frame.length, &validated)) {
        return fail("failed to validate built frame");
    }

    if (sibcp_is_reserved_external_frame(&validated) != expected_reserved) {
        return fail("reserved-frame classification mismatch");
    }
    return 0;
}

static int test_reserved_classification()
{
    if (sibcp_is_reserved_external_frame(nullptr)) {
        return fail("null frame was classified as reserved");
    }
    if (expect_reserved(SIBCP_PACKET_TOPIC, SIBCP_TOPIC_WORLD_BALL, false) != 0) {
        return 1;
    }
    if (expect_reserved(SIBCP_PACKET_TOPIC, SIBCP_TOPIC_SYSTEM_GAME_STATE, true) != 0) {
        return 1;
    }
    if (expect_reserved(SIBCP_PACKET_SERVICE_REQUEST, SIBCP_SERVICE_BALL_IN_VISION, false) != 0) {
        return 1;
    }
    if (expect_reserved(SIBCP_PACKET_SERVICE_REQUEST, SIBCP_SERVICE_SYSTEM_SET_LED, true) != 0) {
        return 1;
    }
    if (expect_reserved(SIBCP_PACKET_SERVICE_RESPONSE, SIBCP_SERVICE_SYSTEM_PLAY_MELODY, true) != 0) {
        return 1;
    }
    if (expect_reserved(SIBCP_PACKET_SERVICE_DISCOVERY, 0xF0, false) != 0) {
        return 1;
    }
    return 0;
}

static int test_crc16()
{
    const uint8_t check_input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (sibcp_crc16_ccitt(check_input, sizeof(check_input)) != 0x29B1) {
        return fail("CRC16-CCITT check vector \"123456789\" mismatch");
    }
    if (sibcp_crc16_ccitt(nullptr, 0) != 0xFFFF) {
        return fail("CRC16-CCITT of empty input should be the initial value 0xFFFF");
    }
    return 0;
}

static int test_build_frame()
{
    sibcp_frame_t frame;

    if (!sibcp_build_frame(SIBCP_PACKET_TOPIC, 1, 2, nullptr, 0, &frame)) {
        return fail("empty payload frame should build");
    }
    if (frame.length != SIBCP_HEADER_LENGTH + SIBCP_CRC_LENGTH) {
        return fail("empty payload frame has wrong length");
    }

    uint8_t max_payload[SIBCP_MAX_PAYLOAD_LENGTH];
    std::memset(max_payload, 0xA5, sizeof(max_payload));
    if (!sibcp_build_frame(SIBCP_PACKET_TOPIC, 1, 2, max_payload, SIBCP_MAX_PAYLOAD_LENGTH, &frame)) {
        return fail("max payload frame should build");
    }
    if (frame.length != SIBCP_MAX_FRAME_LENGTH) {
        return fail("max payload frame has wrong length");
    }

    uint8_t oversized[SIBCP_MAX_PAYLOAD_LENGTH + 1] = {0};
    if (sibcp_build_frame(SIBCP_PACKET_TOPIC, 1, 2, oversized, sizeof(oversized), &frame)) {
        return fail("oversized payload should be rejected");
    }

    const uint8_t payload[] = {0x01};
    if (sibcp_build_frame(SIBCP_PACKET_TOPIC, 1, 2, payload, sizeof(payload), nullptr)) {
        return fail("null frame pointer should be rejected");
    }
    if (sibcp_build_frame(SIBCP_PACKET_TOPIC, 1, 2, nullptr, 1, &frame)) {
        return fail("null payload with non-zero length should be rejected");
    }
    return 0;
}

static int test_validate_frame()
{
    const uint8_t payload[] = {0x10, 0x20, 0x30};
    sibcp_frame_t frame;
    if (!sibcp_build_frame(SIBCP_PACKET_TOPIC, 42, 7, payload, sizeof(payload), &frame)) {
        return fail("failed to build frame for validation tests");
    }

    sibcp_frame_t validated;
    if (!sibcp_validate_frame(frame.data, frame.length, &validated)) {
        return fail("valid frame should validate");
    }
    if (validated.transaction_id != 42 || validated.identifier_id != 7 ||
        validated.payload_length != sizeof(payload)) {
        return fail("validated frame metadata mismatch");
    }

    uint8_t corrupted[SIBCP_MAX_FRAME_LENGTH];

    std::memcpy(corrupted, frame.data, frame.length);
    corrupted[0] = 0x00;
    if (sibcp_validate_frame(corrupted, frame.length, &validated)) {
        return fail("bad first magic byte should be rejected");
    }

    std::memcpy(corrupted, frame.data, frame.length);
    corrupted[1] = 0x00;
    if (sibcp_validate_frame(corrupted, frame.length, &validated)) {
        return fail("bad second magic byte should be rejected");
    }

    std::memcpy(corrupted, frame.data, frame.length);
    corrupted[SIBCP_HEADER_LENGTH] ^= 0x01;
    if (sibcp_validate_frame(corrupted, frame.length, &validated)) {
        return fail("payload bit flip should fail the CRC check");
    }

    if (sibcp_validate_frame(frame.data, frame.length - 1, &validated)) {
        return fail("truncated frame should be rejected");
    }

    std::memcpy(corrupted, frame.data, frame.length);
    corrupted[6] = (uint8_t)(sizeof(payload) + 1);
    if (sibcp_validate_frame(corrupted, frame.length, &validated)) {
        return fail("declared/actual length mismatch should be rejected");
    }

    std::memcpy(corrupted, frame.data, frame.length);
    corrupted[6] = 0xF1;  // 241 > SIBCP_MAX_PAYLOAD_LENGTH
    corrupted[7] = 0x00;
    if (sibcp_validate_frame(corrupted, frame.length, &validated)) {
        return fail("declared payload above the maximum should be rejected");
    }

    sibcp_frame_t minimal;
    if (!sibcp_build_frame(SIBCP_PACKET_TOPIC, 0, 0, nullptr, 0, &minimal)) {
        return fail("failed to build minimal frame");
    }
    if (sibcp_validate_frame(minimal.data, SIBCP_HEADER_LENGTH + SIBCP_CRC_LENGTH - 1, &validated)) {
        return fail("buffer below the minimal frame size should be rejected");
    }
    if (!sibcp_validate_frame(minimal.data, minimal.length, &validated)) {
        return fail("minimal empty-payload frame should validate");
    }
    return 0;
}

static int push_bytes(
    sibcp_parser_t *parser,
    const uint8_t *data,
    size_t length,
    sibcp_frame_t *frame
)
{
    int frames = 0;
    for (size_t i = 0; i < length; i++) {
        if (sibcp_parser_push_byte(parser, data[i], frame)) {
            frames++;
        }
    }
    return frames;
}

static int test_parser()
{
    const uint8_t payload[] = {0xDE, 0xAD};
    sibcp_frame_t reference;
    if (!sibcp_build_frame(SIBCP_PACKET_TOPIC, 9, 4, payload, sizeof(payload), &reference)) {
        return fail("failed to build parser reference frame");
    }

    sibcp_parser_t parser;
    sibcp_parser_reset(&parser);
    sibcp_frame_t frame;

    // Garbage before a valid frame: parser must resync on the magic bytes.
    const uint8_t noise[] = {'n', 'o', 'i', 's', 'e', 0xAA, 0x13};
    if (push_bytes(&parser, noise, sizeof(noise), &frame) != 0) {
        return fail("noise must not produce a frame");
    }
    if (push_bytes(&parser, reference.data, reference.length, &frame) != 1) {
        return fail("parser failed to resync after noise");
    }
    if (frame.transaction_id != 9 || frame.identifier_id != 4 ||
        frame.payload_length != sizeof(payload) ||
        std::memcmp(&frame.data[SIBCP_HEADER_LENGTH], payload, sizeof(payload)) != 0) {
        return fail("parsed frame content mismatch");
    }

    // A repeated start byte must not lose sync (0xAA 0xAA 0x55 ...).
    const uint8_t repeated_start = 0xAA;
    if (push_bytes(&parser, &repeated_start, 1, &frame) != 0) {
        return fail("lone start byte must not produce a frame");
    }
    if (push_bytes(&parser, reference.data, reference.length, &frame) != 1) {
        return fail("parser failed after a repeated start byte");
    }

    // Two back-to-back frames through the same parser.
    if (push_bytes(&parser, reference.data, reference.length, &frame) != 1 ||
        push_bytes(&parser, reference.data, reference.length, &frame) != 1) {
        return fail("back-to-back frames failed to parse");
    }

    // A header declaring an oversized payload resets the parser, which then
    // recovers on the next valid frame.
    const uint8_t oversized_header[] = {0xAA, 0x55, 0x01, 0x00, 0x00, 0x01, 0xF1, 0x00};
    if (push_bytes(&parser, oversized_header, sizeof(oversized_header), &frame) != 0) {
        return fail("oversized declared payload must not produce a frame");
    }
    if (push_bytes(&parser, reference.data, reference.length, &frame) != 1) {
        return fail("parser failed to recover after an oversized header");
    }

    // A frame with a corrupted CRC is rejected and the parser recovers.
    uint8_t corrupted[SIBCP_MAX_FRAME_LENGTH];
    std::memcpy(corrupted, reference.data, reference.length);
    corrupted[SIBCP_HEADER_LENGTH] ^= 0xFF;
    if (push_bytes(&parser, corrupted, reference.length, &frame) != 0) {
        return fail("corrupted frame must not be reported as valid");
    }
    if (push_bytes(&parser, reference.data, reference.length, &frame) != 1) {
        return fail("parser failed to recover after a corrupted frame");
    }
    return 0;
}

static int test_packet_type_names()
{
    if (std::strcmp(sibcp_packet_type_name(SIBCP_PACKET_TOPIC), "TOPIC") != 0 ||
        std::strcmp(sibcp_packet_type_name(SIBCP_PACKET_SERVICE_REQUEST), "SRV_REQ") != 0 ||
        std::strcmp(sibcp_packet_type_name(SIBCP_PACKET_SERVICE_RESPONSE), "SRV_RES") != 0 ||
        std::strcmp(sibcp_packet_type_name(SIBCP_PACKET_SERVICE_DISCOVERY), "DISCOVERY") != 0 ||
        std::strcmp(sibcp_packet_type_name(0x99), "UNKNOWN") != 0) {
        return fail("packet type name mismatch");
    }
    return 0;
}

static int test_discovery_builder()
{
    const uint8_t service_ids[] = {
        SIBCP_SERVICE_SYSTEM_SET_LED,
        SIBCP_SERVICE_SYSTEM_PLAY_MELODY,
    };

    sibcp_frame_t frame;
    if (!sibcp_build_service_discovery_frame(0, service_ids, sizeof(service_ids), &frame)) {
        return fail("discovery frame should build");
    }

    // Byte-exact layout shared with node.py:_handle_discovery and
    // rjscm.c:handle_discovery: [source_robot_id, count, id0, id1].
    if (frame.packet_type != SIBCP_PACKET_SERVICE_DISCOVERY ||
        frame.transaction_id != 0 || frame.identifier_id != 0 ||
        frame.payload_length != 4 ||
        frame.length != SIBCP_HEADER_LENGTH + 4 + SIBCP_CRC_LENGTH) {
        return fail("discovery frame metadata mismatch");
    }
    if (frame.data[SIBCP_HEADER_LENGTH] != 0 ||
        frame.data[SIBCP_HEADER_LENGTH + 1] != 2 ||
        frame.data[SIBCP_HEADER_LENGTH + 2] != SIBCP_SERVICE_SYSTEM_SET_LED ||
        frame.data[SIBCP_HEADER_LENGTH + 3] != SIBCP_SERVICE_SYSTEM_PLAY_MELODY) {
        return fail("discovery payload byte layout mismatch");
    }

    // The built frame must survive its own parser.
    sibcp_parser_t parser;
    sibcp_parser_reset(&parser);
    sibcp_frame_t parsed;
    if (push_bytes(&parser, frame.data, frame.length, &parsed) != 1) {
        return fail("discovery frame failed to parse");
    }
    if (parsed.payload_length != 4 ||
        std::memcmp(&parsed.data[SIBCP_HEADER_LENGTH], &frame.data[SIBCP_HEADER_LENGTH], 4) != 0) {
        return fail("parsed discovery payload mismatch");
    }

    // Empty service list is valid: payload is [source_robot_id, 0].
    if (!sibcp_build_service_discovery_frame(3, nullptr, 0, &frame)) {
        return fail("discovery frame with no services should build");
    }
    if (frame.payload_length != 2 || frame.data[SIBCP_HEADER_LENGTH] != 3 ||
        frame.data[SIBCP_HEADER_LENGTH + 1] != 0) {
        return fail("empty discovery payload mismatch");
    }

    // Boundary: 238 ids fit the 240-byte payload, 239 do not.
    uint8_t many_ids[239];
    std::memset(many_ids, 0x01, sizeof(many_ids));
    if (!sibcp_build_service_discovery_frame(1, many_ids, 238, &frame)) {
        return fail("238 service ids should fit");
    }
    if (sibcp_build_service_discovery_frame(1, many_ids, 239, &frame)) {
        return fail("239 service ids should be rejected");
    }

    if (sibcp_build_service_discovery_frame(0, service_ids, sizeof(service_ids), nullptr)) {
        return fail("null discovery frame pointer should be rejected");
    }
    if (sibcp_build_service_discovery_frame(0, nullptr, 2, &frame)) {
        return fail("null service ids with non-zero count should be rejected");
    }
    return 0;
}

int main()
{
    if (test_reserved_classification() != 0) {
        return 1;
    }
    if (test_crc16() != 0) {
        return 1;
    }
    if (test_build_frame() != 0) {
        return 1;
    }
    if (test_validate_frame() != 0) {
        return 1;
    }
    if (test_parser() != 0) {
        return 1;
    }
    if (test_packet_type_names() != 0) {
        return 1;
    }
    if (test_discovery_builder() != 0) {
        return 1;
    }

    return 0;
}
