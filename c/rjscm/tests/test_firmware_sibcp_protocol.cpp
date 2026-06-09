#include "sibcp_protocol.h"

#include <cstdio>

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

int main()
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
