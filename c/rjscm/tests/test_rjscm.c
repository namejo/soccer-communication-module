#include "rjscm.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct location {
    int32_t x_mm;
    int32_t y_mm;
} location_t;

static atomic_bool responder_running;

static int fail(const char *message, rjscm_result_t result) {
    fprintf(stderr, "%s: %s\n", message, rjscm_strerror(result));
    return 1;
}

#define CHECK(expr)                         \
    do {                                    \
        rjscm_result_t result_ = (expr);    \
        if (result_ != RJSCM_OK) {          \
            return fail(#expr, result_);    \
        }                                   \
    } while (0)

static void handle_location(const void *payload, size_t length, void *user_data) {
    if (length != sizeof(location_t)) {
        return;
    }
    memcpy(user_data, payload, sizeof(location_t));
}

static rjscm_result_t provide_location(
    void *payload,
    size_t payload_capacity,
    size_t *payload_length,
    void *user_data
) {
    if (payload_capacity < sizeof(location_t)) {
        return RJSCM_INVALID_ARGUMENT;
    }
    location_t *location = (location_t *)user_data;
    location->x_mm += 1;
    memcpy(payload, location, sizeof(*location));
    *payload_length = sizeof(*location);
    return RJSCM_OK;
}

static rjscm_result_t can_shoot_handler(
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    void *user_data
) {
    (void)request;
    (void)user_data;
    if (request_length != 0 || response_capacity < 1) {
        return RJSCM_INVALID_ARGUMENT;
    }
    uint8_t value = 1;
    memcpy(response, &value, sizeof(value));
    *response_length = sizeof(value);
    return RJSCM_OK;
}

static bool see_ball(void *user_data) {
    return *(bool *)user_data;
}

static void *responder_loop(void *user_data) {
    rjscm_module_t *module = (rjscm_module_t *)user_data;
    while (atomic_load(&responder_running)) {
        (void)rjscm_update(module, 10);
    }
    return NULL;
}

int main(void) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        perror("socketpair");
        return 1;
    }

    rjscm_module_t *left = NULL;
    rjscm_module_t *right = NULL;
    CHECK(rjscm_open_fd(&left, fds[0], 1));
    CHECK(rjscm_open_fd(&right, fds[1], 2));

    CHECK(rjscm_define_message(left, "my_location", 0x30, sizeof(location_t)));
    CHECK(rjscm_define_message(right, "my_location", 0x30, sizeof(location_t)));
    if (
        rjscm_define_message(left, "my_location_alias", 0x30, sizeof(location_t)) !=
        RJSCM_INVALID_ARGUMENT
    ) {
        fprintf(stderr, "duplicate message id was accepted\n");
        return 1;
    }

    location_t received = {0};
    CHECK(rjscm_on_message(right, "my_location", handle_location, &received));

    const location_t sent = {.x_mm = 1200, .y_mm = -400};
    CHECK(rjscm_publish(left, "my_location", &sent, sizeof(sent)));
    CHECK(rjscm_update(right, 100));
    if (received.x_mm != sent.x_mm || received.y_mm != sent.y_mm) {
        fprintf(stderr, "message payload mismatch\n");
        return 1;
    }

    location_t provided = {.x_mm = 2000, .y_mm = -50};
    received = (location_t){0};
    CHECK(rjscm_publish_at_hz(left, "my_location", 50.0, provide_location, &provided));
    for (int i = 0; i < 20 && received.x_mm < 2002; ++i) {
        CHECK(rjscm_update(left, 10));
        CHECK(rjscm_update(right, 10));
    }
    CHECK(rjscm_stop_publishing(left, "my_location"));
    if (received.x_mm < 2002 || received.y_mm != -50) {
        fprintf(stderr, "periodic topic did not publish repeatedly\n");
        return 1;
    }

    CHECK(rjscm_define_service(left, "can_shoot", 0x31, 0, sizeof(uint8_t)));
    CHECK(rjscm_define_service(right, "can_shoot", 0x31, 0, sizeof(uint8_t)));
    if (
        rjscm_define_service(left, "can_kick", 0x31, 0, sizeof(uint8_t)) !=
        RJSCM_INVALID_ARGUMENT
    ) {
        fprintf(stderr, "duplicate service id was accepted\n");
        return 1;
    }
    CHECK(rjscm_serve(right, "can_shoot", can_shoot_handler, NULL));

    bool sees_ball = true;
    CHECK(rjscm_answer_see_ball(right, see_ball, &sees_ball));

    atomic_store(&responder_running, true);
    pthread_t responder_thread;
    if (pthread_create(&responder_thread, NULL, responder_loop, right) != 0) {
        perror("pthread_create");
        return 1;
    }

    uint8_t can_shoot_response = 0;
    size_t response_length = 0;
    CHECK(rjscm_call_service(
        left,
        "can_shoot",
        NULL,
        0,
        &can_shoot_response,
        sizeof(can_shoot_response),
        &response_length,
        2,
        500
    ));
    if (response_length != 1 || can_shoot_response != 1) {
        fprintf(stderr, "service response mismatch\n");
        return 1;
    }

    bool peer_sees_ball = false;
    CHECK(rjscm_ask_peer_sees_ball(left, 2, 500, &peer_sees_ball));
    if (!peer_sees_ball) {
        fprintf(stderr, "see_ball helper returned false\n");
        return 1;
    }

    uint8_t alias_response = 0;
    response_length = 0;
    CHECK(rjscm_call_service(
        left,
        "ball_in_your_vision",
        NULL,
        0,
        &alias_response,
        sizeof(alias_response),
        &response_length,
        2,
        500
    ));
    if (response_length != 1 || alias_response != 1) {
        fprintf(stderr, "standard service alias returned wrong value\n");
        return 1;
    }

    atomic_store(&responder_running, false);
    pthread_join(responder_thread, NULL);

    rjscm_close(right);
    rjscm_close(left);
    return 0;
}
