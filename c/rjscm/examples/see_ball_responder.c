#include "rjscm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct responder_state {
    bool sees_ball;
} responder_state_t;

static bool parse_bool(const char *value) {
    if (value == NULL) {
        return true;
    }
    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "on") == 0;
}

static bool see_ball(void *user_data) {
    const responder_state_t *state = (const responder_state_t *)user_data;
    return state->sees_ball;
}

int main(int argc, char **argv) {
    const char *port = argc > 1 ? argv[1] : "/dev/ttyACM1";
    const uint8_t robot_id = argc > 2 ? (uint8_t)strtoul(argv[2], NULL, 0) : 2u;
    responder_state_t state = {
        .sees_ball = argc > 3 ? parse_bool(argv[3]) : true,
    };

    rjscm_module_t *robot = NULL;
    rjscm_result_t result = rjscm_open_usb(&robot, port, robot_id);
    if (result != RJSCM_OK) {
        fprintf(stderr, "open %s failed: %s\n", port, rjscm_strerror(result));
        return 1;
    }

    result = rjscm_answer_see_ball(robot, see_ball, &state);
    if (result != RJSCM_OK) {
        fprintf(stderr, "serve see_ball failed: %s\n", rjscm_strerror(result));
        rjscm_close(robot);
        return 1;
    }

    printf(
        "Serving see_ball=%s on %s as robot %u\n",
        state.sees_ball ? "true" : "false",
        port,
        (unsigned)robot_id
    );

    while (true) {
        result = rjscm_update(robot, 10);
        if (result != RJSCM_OK) {
            fprintf(stderr, "update failed: %s\n", rjscm_strerror(result));
            rjscm_close(robot);
            return 1;
        }
    }
}
