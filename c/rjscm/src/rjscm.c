#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "rjscm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Wire-format constants intentionally duplicated from
 * firmware/RCj_comm_module/sibcp_protocol.h: this library is standalone and
 * must not include firmware headers. Keep in sync; CI's
 * test_firmware_sibcp_protocol target compiles the firmware codec host-side. */
#define RJSCM_START_BYTE_0 0xAAu
#define RJSCM_START_BYTE_1 0x55u
#define RJSCM_HEADER_LENGTH 8u
#define RJSCM_CRC_LENGTH 2u
#define RJSCM_MAX_FRAME_LENGTH \
    (RJSCM_HEADER_LENGTH + RJSCM_MAX_PAYLOAD_LENGTH + RJSCM_CRC_LENGTH)

#define RJSCM_PACKET_TOPIC 0x01u
#define RJSCM_PACKET_SERVICE_REQUEST 0x02u
#define RJSCM_PACKET_SERVICE_RESPONSE 0x03u
#define RJSCM_PACKET_SERVICE_DISCOVERY 0x04u

#define RJSCM_SERVICE_STATUS_OK 0x00u
#define RJSCM_SERVICE_STATUS_ERROR 0x01u
#define RJSCM_SERVICE_STATUS_UNSUPPORTED 0x02u

#define RJSCM_MAX_MESSAGES 40u
#define RJSCM_MAX_SERVICES 40u
#define RJSCM_MAX_PERIODIC_PUBLISHERS 24u
#define RJSCM_NAME_LENGTH 64u
#define RJSCM_DISCOVERY_BITMAP_SIZE 32u

typedef struct rjscm_parser {
    uint8_t buffer[RJSCM_MAX_FRAME_LENGTH];
    uint16_t length;
    uint16_t expected_length;
} rjscm_parser_t;

typedef struct rjscm_frame {
    uint8_t packet_type;
    uint16_t transaction_id;
    uint8_t identifier_id;
    const uint8_t *payload;
    uint16_t payload_length;
} rjscm_frame_t;

typedef struct rjscm_message_def {
    char name[RJSCM_NAME_LENGTH];
    uint8_t id;
    size_t payload_size;
    rjscm_message_callback_t callback;
    void *user_data;
} rjscm_message_def_t;

typedef struct rjscm_service_def {
    char name[RJSCM_NAME_LENGTH];
    uint8_t id;
    size_t request_size;
    size_t response_size;
    bool served;
    rjscm_service_callback_t callback;
    void *user_data;
    rjscm_bool_callback_t bool_callback;
    void *bool_user_data;
    rjscm_role_callback_t role_callback;
    void *role_user_data;
} rjscm_service_def_t;

typedef struct rjscm_periodic_publisher {
    char name[RJSCM_NAME_LENGTH];
    uint8_t message_id;
    size_t payload_size;
    bool active;
    uint32_t interval_ms;
    uint64_t next_publish_ms;
    rjscm_topic_provider_t provider;
    void *user_data;
} rjscm_periodic_publisher_t;

typedef struct rjscm_pending_call {
    bool active;
    bool done;
    uint16_t transaction_id;
    uint8_t service_id;
    rjscm_result_t result;
    uint8_t remote_status;
    void *response;
    size_t response_capacity;
    size_t *response_length;
} rjscm_pending_call_t;

struct rjscm_module {
    int fd;
    uint8_t robot_id;
    uint16_t next_transaction_id;
    uint32_t advertise_interval_ms;
    uint64_t next_advertise_ms;
    rjscm_parser_t parser;
    rjscm_message_def_t messages[RJSCM_MAX_MESSAGES];
    size_t message_count;
    rjscm_service_def_t services[RJSCM_MAX_SERVICES];
    size_t service_count;
    rjscm_periodic_publisher_t periodic_publishers[RJSCM_MAX_PERIODIC_PUBLISHERS];
    size_t periodic_publisher_count;
    uint8_t discovered[256][RJSCM_DISCOVERY_BITMAP_SIZE];
    rjscm_pending_call_t pending;
};

static rjscm_result_t init_module(rjscm_module_t **out_module, int fd, uint8_t robot_id);
static rjscm_result_t configure_serial(int fd, int baudrate);
static speed_t baudrate_to_speed(int baudrate);
static rjscm_result_t set_nonblocking(int fd);
static uint64_t monotonic_ms(void);
static uint16_t read_u16_le(const uint8_t *data);
static void write_u16_le(uint8_t *data, uint16_t value);
static uint16_t crc16_ccitt(const uint8_t *data, size_t length);
static rjscm_result_t send_frame(
    rjscm_module_t *module,
    uint8_t packet_type,
    uint16_t transaction_id,
    uint8_t identifier_id,
    const void *payload,
    size_t payload_length
);
static rjscm_result_t write_all(int fd, const uint8_t *data, size_t length);
static rjscm_result_t read_and_dispatch(rjscm_module_t *module, uint32_t timeout_ms);
static bool parser_push(rjscm_parser_t *parser, uint8_t byte, rjscm_frame_t *frame);
static bool validate_frame(const uint8_t *data, size_t length, rjscm_frame_t *frame);
static void parser_reset(rjscm_parser_t *parser);
static rjscm_result_t dispatch_frame(rjscm_module_t *module, const rjscm_frame_t *frame);
static rjscm_result_t handle_topic(rjscm_module_t *module, const rjscm_frame_t *frame);
static rjscm_result_t handle_service_request(rjscm_module_t *module, const rjscm_frame_t *frame);
static rjscm_result_t handle_service_response(rjscm_module_t *module, const rjscm_frame_t *frame);
static rjscm_result_t handle_discovery(rjscm_module_t *module, const rjscm_frame_t *frame);
static rjscm_result_t send_service_response(
    rjscm_module_t *module,
    uint16_t transaction_id,
    uint8_t service_id,
    uint8_t status,
    const void *payload,
    size_t payload_length
);
static rjscm_message_def_t *find_message_by_name(rjscm_module_t *module, const char *name);
static rjscm_service_def_t *find_service_by_name(rjscm_module_t *module, const char *name);
static rjscm_service_def_t *find_served_service_by_id(rjscm_module_t *module, uint8_t id);
static rjscm_result_t define_message_internal(
    rjscm_module_t *module,
    const char *name,
    uint8_t message_id,
    size_t payload_size,
    bool allow_duplicate_id
);
static rjscm_result_t define_service_internal(
    rjscm_module_t *module,
    const char *name,
    uint8_t service_id,
    size_t request_size,
    size_t response_size,
    bool allow_duplicate_id
);
static bool service_is_discovered(const rjscm_module_t *module, uint8_t service_id, int peer_id);
static void set_discovered_service(rjscm_module_t *module, uint8_t peer_id, uint8_t service_id);
static bool has_discovered_service(const rjscm_module_t *module, uint8_t peer_id, uint8_t service_id);
static rjscm_result_t maybe_advertise(rjscm_module_t *module);
static rjscm_result_t maybe_publish_periodic(rjscm_module_t *module);
static uint16_t next_transaction_id(rjscm_module_t *module);
static rjscm_result_t define_standard(rjscm_module_t *module);
static rjscm_result_t copy_name(char *destination, const char *source);
static rjscm_periodic_publisher_t *find_periodic_publisher(
    rjscm_module_t *module,
    const char *name
);
static rjscm_result_t see_ball_adapter(
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    void *user_data
);
static rjscm_result_t role_adapter(
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    void *user_data
);

const char *rjscm_strerror(rjscm_result_t result) {
    switch (result) {
        case RJSCM_OK:
            return "ok";
        case RJSCM_ERROR:
            return "general error";
        case RJSCM_TIMEOUT:
            return "timeout";
        case RJSCM_INVALID_ARGUMENT:
            return "invalid argument";
        case RJSCM_NO_SPACE:
            return "no space left";
        case RJSCM_NOT_FOUND:
            return "not found";
        case RJSCM_IO_ERROR:
            return "I/O error";
        case RJSCM_PROTOCOL_ERROR:
            return "protocol error";
        case RJSCM_REMOTE_ERROR:
            return "remote service error";
        case RJSCM_UNSUPPORTED:
            return "remote service unsupported";
        default:
            return "unknown error";
    }
}

rjscm_result_t rjscm_open(
    rjscm_module_t **out_module,
    const char *port,
    uint8_t robot_id,
    int baudrate
) {
    if (out_module == NULL || port == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return RJSCM_IO_ERROR;
    }

    rjscm_result_t result = configure_serial(fd, baudrate);
    if (result != RJSCM_OK) {
        close(fd);
        return result;
    }

    result = init_module(out_module, fd, robot_id);
    if (result != RJSCM_OK) {
        close(fd);
    }
    return result;
}

rjscm_result_t rjscm_open_usb(
    rjscm_module_t **out_module,
    const char *port,
    uint8_t robot_id
) {
    return rjscm_open(out_module, port, robot_id, RJSCM_DEFAULT_BAUDRATE);
}

rjscm_result_t rjscm_open_fd(
    rjscm_module_t **out_module,
    int fd,
    uint8_t robot_id
) {
    if (fd < 0) {
        return RJSCM_INVALID_ARGUMENT;
    }
    rjscm_result_t result = set_nonblocking(fd);
    if (result != RJSCM_OK) {
        return result;
    }
    return init_module(out_module, fd, robot_id);
}

void rjscm_close(rjscm_module_t *module) {
    if (module == NULL) {
        return;
    }
    if (module->fd >= 0) {
        close(module->fd);
    }
    free(module);
}

rjscm_result_t rjscm_update(rjscm_module_t *module, uint32_t timeout_ms) {
    if (module == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    const uint64_t deadline = monotonic_ms() + timeout_ms;
    do {
        rjscm_result_t result = maybe_advertise(module);
        if (result != RJSCM_OK) {
            return result;
        }
        result = maybe_publish_periodic(module);
        if (result != RJSCM_OK) {
            return result;
        }

        uint32_t wait_ms = 0;
        if (timeout_ms > 0) {
            uint64_t now = monotonic_ms();
            if (now >= deadline) {
                break;
            }
            uint64_t remaining = deadline - now;
            wait_ms = (remaining > 10u) ? 10u : (uint32_t)remaining;
        }

        result = read_and_dispatch(module, wait_ms);
        if (result != RJSCM_OK) {
            return result;
        }

        if (timeout_ms == 0) {
            break;
        }
    } while (monotonic_ms() < deadline);

    return RJSCM_OK;
}

rjscm_result_t rjscm_advertise_once(rjscm_module_t *module) {
    if (module == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    uint8_t payload[2u + RJSCM_MAX_SERVICES];
    size_t count = 0;
    payload[0] = module->robot_id;

    for (size_t i = 0; i < module->service_count; ++i) {
        const rjscm_service_def_t *service = &module->services[i];
        if (!service->served) {
            continue;
        }
        bool already_added = false;
        for (size_t j = 0; j < count; ++j) {
            if (payload[2u + j] == service->id) {
                already_added = true;
                break;
            }
        }
        if (!already_added) {
            payload[2u + count] = service->id;
            ++count;
        }
    }

    payload[1] = (uint8_t)count;
    return send_frame(
        module,
        RJSCM_PACKET_SERVICE_DISCOVERY,
        0,
        0,
        payload,
        2u + count
    );
}

void rjscm_set_advertise_interval(rjscm_module_t *module, uint32_t interval_ms) {
    if (module == NULL) {
        return;
    }
    module->advertise_interval_ms = interval_ms;
}

rjscm_result_t rjscm_define_message(
    rjscm_module_t *module,
    const char *name,
    uint8_t message_id,
    size_t payload_size
) {
    return define_message_internal(module, name, message_id, payload_size, false);
}

static rjscm_result_t define_message_internal(
    rjscm_module_t *module,
    const char *name,
    uint8_t message_id,
    size_t payload_size,
    bool allow_duplicate_id
) {
    if (module == NULL || name == NULL || payload_size > RJSCM_MAX_PAYLOAD_LENGTH) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_message_def_t *existing = find_message_by_name(module, name);
    if (existing != NULL) {
        return (
            existing->id == message_id &&
            existing->payload_size == payload_size
        ) ? RJSCM_OK : RJSCM_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < module->message_count; ++i) {
        if (module->messages[i].id == message_id) {
            if (module->messages[i].payload_size != payload_size) {
                return RJSCM_INVALID_ARGUMENT;
            }
            if (!allow_duplicate_id) {
                return RJSCM_INVALID_ARGUMENT;
            }
        }
    }

    if (module->message_count >= RJSCM_MAX_MESSAGES) {
        return RJSCM_NO_SPACE;
    }

    rjscm_message_def_t *message = &module->messages[module->message_count++];
    memset(message, 0, sizeof(*message));
    message->id = message_id;
    message->payload_size = payload_size;
    return copy_name(message->name, name);
}

rjscm_result_t rjscm_on_message(
    rjscm_module_t *module,
    const char *name,
    rjscm_message_callback_t callback,
    void *user_data
) {
    if (module == NULL || name == NULL || callback == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_message_def_t *message = find_message_by_name(module, name);
    if (message == NULL) {
        return RJSCM_NOT_FOUND;
    }
    message->callback = callback;
    message->user_data = user_data;
    return RJSCM_OK;
}

rjscm_result_t rjscm_publish(
    rjscm_module_t *module,
    const char *name,
    const void *payload,
    size_t payload_length
) {
    if (module == NULL || name == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_message_def_t *message = find_message_by_name(module, name);
    if (message == NULL) {
        return RJSCM_NOT_FOUND;
    }
    if (payload_length != message->payload_size) {
        return RJSCM_INVALID_ARGUMENT;
    }
    if (payload_length > 0 && payload == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    return send_frame(
        module,
        RJSCM_PACKET_TOPIC,
        0,
        message->id,
        payload,
        payload_length
    );
}

rjscm_result_t rjscm_publish_at_hz(
    rjscm_module_t *module,
    const char *name,
    double hz,
    rjscm_topic_provider_t provider,
    void *user_data
) {
    if (module == NULL || name == NULL || provider == NULL || hz <= 0.0) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_message_def_t *message = find_message_by_name(module, name);
    if (message == NULL) {
        return RJSCM_NOT_FOUND;
    }

    double interval = 1000.0 / hz;
    uint32_t interval_ms = interval < 1.0 ? 1u : (uint32_t)(interval + 0.5);

    rjscm_periodic_publisher_t *publisher = find_periodic_publisher(module, name);
    if (publisher == NULL) {
        if (module->periodic_publisher_count >= RJSCM_MAX_PERIODIC_PUBLISHERS) {
            return RJSCM_NO_SPACE;
        }
        publisher = &module->periodic_publishers[module->periodic_publisher_count++];
        memset(publisher, 0, sizeof(*publisher));
        rjscm_result_t result = copy_name(publisher->name, name);
        if (result != RJSCM_OK) {
            return result;
        }
    }

    publisher->message_id = message->id;
    publisher->payload_size = message->payload_size;
    publisher->interval_ms = interval_ms;
    publisher->next_publish_ms = 0;
    publisher->provider = provider;
    publisher->user_data = user_data;
    publisher->active = true;
    return RJSCM_OK;
}

rjscm_result_t rjscm_stop_publishing(
    rjscm_module_t *module,
    const char *name
) {
    if (module == NULL || name == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_periodic_publisher_t *publisher = find_periodic_publisher(module, name);
    if (publisher == NULL) {
        return RJSCM_NOT_FOUND;
    }
    publisher->active = false;
    return RJSCM_OK;
}

rjscm_result_t rjscm_define_service(
    rjscm_module_t *module,
    const char *name,
    uint8_t service_id,
    size_t request_size,
    size_t response_size
) {
    return define_service_internal(module, name, service_id, request_size, response_size, false);
}

static rjscm_result_t define_service_internal(
    rjscm_module_t *module,
    const char *name,
    uint8_t service_id,
    size_t request_size,
    size_t response_size,
    bool allow_duplicate_id
) {
    if (
        module == NULL ||
        name == NULL ||
        request_size > RJSCM_MAX_PAYLOAD_LENGTH ||
        response_size > RJSCM_MAX_PAYLOAD_LENGTH - 1u
    ) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_service_def_t *existing = find_service_by_name(module, name);
    if (existing != NULL) {
        return (
            existing->id == service_id &&
            existing->request_size == request_size &&
            existing->response_size == response_size
        ) ? RJSCM_OK : RJSCM_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < module->service_count; ++i) {
        if (module->services[i].id == service_id) {
            if (
                module->services[i].request_size != request_size ||
                module->services[i].response_size != response_size
            ) {
                return RJSCM_INVALID_ARGUMENT;
            }
            if (!allow_duplicate_id) {
                return RJSCM_INVALID_ARGUMENT;
            }
        }
    }

    if (module->service_count >= RJSCM_MAX_SERVICES) {
        return RJSCM_NO_SPACE;
    }

    rjscm_service_def_t *service = &module->services[module->service_count++];
    memset(service, 0, sizeof(*service));
    service->id = service_id;
    service->request_size = request_size;
    service->response_size = response_size;
    return copy_name(service->name, name);
}

rjscm_result_t rjscm_serve(
    rjscm_module_t *module,
    const char *name,
    rjscm_service_callback_t callback,
    void *user_data
) {
    if (module == NULL || name == NULL || callback == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_service_def_t *service = find_service_by_name(module, name);
    if (service == NULL) {
        return RJSCM_NOT_FOUND;
    }
    service->callback = callback;
    service->user_data = user_data;
    service->served = true;
    module->next_advertise_ms = 0;
    return RJSCM_OK;
}

rjscm_result_t rjscm_wait_for_service(
    rjscm_module_t *module,
    const char *name,
    int peer_id,
    uint32_t timeout_ms
) {
    if (module == NULL || name == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_service_def_t *service = find_service_by_name(module, name);
    if (service == NULL) {
        return RJSCM_NOT_FOUND;
    }

    const uint64_t deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() <= deadline) {
        if (service_is_discovered(module, service->id, peer_id)) {
            return RJSCM_OK;
        }
        uint64_t now = monotonic_ms();
        uint32_t wait_ms = (deadline > now && deadline - now > 10u)
            ? 10u
            : (uint32_t)((deadline > now) ? deadline - now : 0u);
        rjscm_result_t result = rjscm_update(module, wait_ms);
        if (result != RJSCM_OK) {
            return result;
        }
    }
    return RJSCM_TIMEOUT;
}

rjscm_result_t rjscm_call_service(
    rjscm_module_t *module,
    const char *name,
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    int peer_id,
    uint32_t timeout_ms
) {
    if (module == NULL || name == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    if (module->pending.active) {
        return RJSCM_ERROR;
    }

    rjscm_service_def_t *service = find_service_by_name(module, name);
    if (service == NULL) {
        return RJSCM_NOT_FOUND;
    }
    if (request_length != service->request_size) {
        return RJSCM_INVALID_ARGUMENT;
    }
    if (request_length > 0 && request == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    if (service->response_size > response_capacity) {
        return RJSCM_INVALID_ARGUMENT;
    }
    if (service->response_size > 0 && response == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    if (service->id < 0xF0u) {
        rjscm_result_t result = rjscm_wait_for_service(module, name, peer_id, timeout_ms);
        if (result != RJSCM_OK) {
            return result;
        }
    }

    const uint16_t transaction_id = next_transaction_id(module);
    module->pending.active = true;
    module->pending.done = false;
    module->pending.transaction_id = transaction_id;
    module->pending.service_id = service->id;
    module->pending.result = RJSCM_TIMEOUT;
    module->pending.remote_status = RJSCM_SERVICE_STATUS_ERROR;
    module->pending.response = response;
    module->pending.response_capacity = response_capacity;
    module->pending.response_length = response_length;
    if (response_length != NULL) {
        *response_length = 0;
    }

    rjscm_result_t result = send_frame(
        module,
        RJSCM_PACKET_SERVICE_REQUEST,
        transaction_id,
        service->id,
        request,
        request_length
    );
    if (result != RJSCM_OK) {
        module->pending.active = false;
        return result;
    }

    const uint64_t deadline = monotonic_ms() + timeout_ms;
    while (!module->pending.done && monotonic_ms() <= deadline) {
        uint64_t now = monotonic_ms();
        uint32_t wait_ms = (deadline > now && deadline - now > 10u)
            ? 10u
            : (uint32_t)((deadline > now) ? deadline - now : 0u);
        result = rjscm_update(module, wait_ms);
        if (result != RJSCM_OK) {
            module->pending.active = false;
            return result;
        }
    }

    result = module->pending.done ? module->pending.result : RJSCM_TIMEOUT;
    module->pending.active = false;
    return result;
}

rjscm_result_t rjscm_publish_ball(
    rjscm_module_t *module,
    const rjscm_world_ball_t *ball
) {
    if (ball == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    return rjscm_publish(module, "ball", ball, sizeof(*ball));
}

rjscm_result_t rjscm_publish_pose(
    rjscm_module_t *module,
    const rjscm_robot_pose_t *pose
) {
    if (pose == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    return rjscm_publish(module, "robot_pose", pose, sizeof(*pose));
}

rjscm_result_t rjscm_answer_see_ball(
    rjscm_module_t *module,
    rjscm_bool_callback_t callback,
    void *user_data
) {
    if (module == NULL || callback == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    rjscm_service_def_t *service = find_service_by_name(module, "see_ball");
    if (service == NULL) {
        return RJSCM_NOT_FOUND;
    }
    service->callback = see_ball_adapter;
    service->user_data = service;
    service->bool_callback = callback;
    service->bool_user_data = user_data;
    service->served = true;
    module->next_advertise_ms = 0;
    return RJSCM_OK;
}

rjscm_result_t rjscm_ask_peer_sees_ball(
    rjscm_module_t *module,
    int peer_id,
    uint32_t timeout_ms,
    bool *out_sees_ball
) {
    if (out_sees_ball == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    uint8_t response = 0;
    size_t response_length = 0;
    rjscm_result_t result = rjscm_call_service(
        module,
        "see_ball",
        NULL,
        0,
        &response,
        sizeof(response),
        &response_length,
        peer_id,
        timeout_ms
    );
    if (result != RJSCM_OK) {
        return result;
    }
    if (response_length != sizeof(response)) {
        return RJSCM_PROTOCOL_ERROR;
    }
    *out_sees_ball = response != 0;
    return RJSCM_OK;
}

rjscm_result_t rjscm_answer_role(
    rjscm_module_t *module,
    rjscm_role_callback_t callback,
    void *user_data
) {
    if (module == NULL || callback == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    rjscm_service_def_t *service = find_service_by_name(module, "request_role");
    if (service == NULL) {
        return RJSCM_NOT_FOUND;
    }
    service->callback = role_adapter;
    service->user_data = service;
    service->role_callback = callback;
    service->role_user_data = user_data;
    service->served = true;
    module->next_advertise_ms = 0;
    return RJSCM_OK;
}

rjscm_result_t rjscm_ask_peer_role(
    rjscm_module_t *module,
    int peer_id,
    uint32_t timeout_ms,
    rjscm_tactical_role_t *out_role
) {
    if (out_role == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    rjscm_role_response_t response = {0};
    size_t response_length = 0;
    rjscm_result_t result = rjscm_call_service(
        module,
        "request_role",
        NULL,
        0,
        &response,
        sizeof(response),
        &response_length,
        peer_id,
        timeout_ms
    );
    if (result != RJSCM_OK) {
        return result;
    }
    if (response_length != sizeof(response)) {
        return RJSCM_PROTOCOL_ERROR;
    }
    *out_role = (rjscm_tactical_role_t)response.role;
    return RJSCM_OK;
}

rjscm_result_t rjscm_set_led(
    rjscm_module_t *module,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    rjscm_led_mode_t mode,
    uint16_t duration_ms,
    uint32_t timeout_ms
) {
    rjscm_set_led_request_t request = {
        .mode = (uint8_t)mode,
        .red = red,
        .green = green,
        .blue = blue,
        .duration_ms = duration_ms,
    };
    size_t response_length = 0;
    return rjscm_call_service(
        module,
        "set_led",
        &request,
        sizeof(request),
        NULL,
        0,
        &response_length,
        RJSCM_ANY_PEER,
        timeout_ms
    );
}

rjscm_result_t rjscm_play_melody(
    rjscm_module_t *module,
    rjscm_melody_id_t melody_id,
    uint8_t repeat,
    uint32_t timeout_ms
) {
    rjscm_play_melody_request_t request = {
        .melody_id = (uint8_t)melody_id,
        .repeat = repeat,
    };
    size_t response_length = 0;
    return rjscm_call_service(
        module,
        "play_melody",
        &request,
        sizeof(request),
        NULL,
        0,
        &response_length,
        RJSCM_ANY_PEER,
        timeout_ms
    );
}

static rjscm_result_t init_module(rjscm_module_t **out_module, int fd, uint8_t robot_id) {
    if (out_module == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    rjscm_module_t *module = (rjscm_module_t *)calloc(1, sizeof(*module));
    if (module == NULL) {
        return RJSCM_NO_SPACE;
    }

    module->fd = fd;
    module->robot_id = robot_id;
    module->advertise_interval_ms = 500;
    parser_reset(&module->parser);

    rjscm_result_t result = define_standard(module);
    if (result != RJSCM_OK) {
        free(module);
        return result;
    }

    *out_module = module;
    return RJSCM_OK;
}

static rjscm_result_t configure_serial(int fd, int baudrate) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        return RJSCM_IO_ERROR;
    }

    speed_t speed = baudrate_to_speed(baudrate);
    if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0) {
        return RJSCM_IO_ERROR;
    }

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
#if defined(CRTSCTS)
    tty.c_cflag &= ~CRTSCTS;
#endif

    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return RJSCM_IO_ERROR;
    }

    return set_nonblocking(fd);
}

static speed_t baudrate_to_speed(int baudrate) {
    switch (baudrate) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
#if defined(B230400)
        case 230400:
            return B230400;
#endif
#if defined(B460800)
        case 460800:
            return B460800;
#endif
        default:
#if defined(B460800)
            return B460800;
#else
            return B115200;
#endif
    }
}

static rjscm_result_t set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return RJSCM_IO_ERROR;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return RJSCM_IO_ERROR;
    }
    return RJSCM_OK;
}

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint16_t read_u16_le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_u16_le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)(value >> 8);
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static rjscm_result_t send_frame(
    rjscm_module_t *module,
    uint8_t packet_type,
    uint16_t transaction_id,
    uint8_t identifier_id,
    const void *payload,
    size_t payload_length
) {
    if (payload_length > RJSCM_MAX_PAYLOAD_LENGTH) {
        return RJSCM_INVALID_ARGUMENT;
    }
    if (payload_length > 0 && payload == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }

    uint8_t frame[RJSCM_MAX_FRAME_LENGTH];
    frame[0] = RJSCM_START_BYTE_0;
    frame[1] = RJSCM_START_BYTE_1;
    frame[2] = packet_type;
    write_u16_le(&frame[3], transaction_id);
    frame[5] = identifier_id;
    write_u16_le(&frame[6], (uint16_t)payload_length);
    if (payload_length > 0) {
        memcpy(&frame[8], payload, payload_length);
    }

    uint16_t crc = crc16_ccitt(&frame[2], 6u + payload_length);
    write_u16_le(&frame[8u + payload_length], crc);
    return write_all(module->fd, frame, 10u + payload_length);
}

static rjscm_result_t write_all(int fd, const uint8_t *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(fd, &write_set);
            struct timeval timeout = {.tv_sec = 0, .tv_usec = 10000};
            int ready = select(fd + 1, NULL, &write_set, NULL, &timeout);
            if (ready < 0 && errno != EINTR) {
                return RJSCM_IO_ERROR;
            }
            continue;
        }
        return RJSCM_IO_ERROR;
    }
    return RJSCM_OK;
}

static rjscm_result_t read_and_dispatch(rjscm_module_t *module, uint32_t timeout_ms) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(module->fd, &read_set);

    struct timeval timeout = {
        .tv_sec = (time_t)(timeout_ms / 1000u),
        .tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u),
    };

    int ready = select(module->fd + 1, &read_set, NULL, NULL, &timeout);
    if (ready < 0) {
        return (errno == EINTR) ? RJSCM_OK : RJSCM_IO_ERROR;
    }
    if (ready == 0 || !FD_ISSET(module->fd, &read_set)) {
        return RJSCM_OK;
    }

    uint8_t bytes[64];
    ssize_t count = read(module->fd, bytes, sizeof(bytes));
    if (count < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            ? RJSCM_OK
            : RJSCM_IO_ERROR;
    }
    if (count == 0) {
        return RJSCM_OK;
    }

    for (ssize_t i = 0; i < count; ++i) {
        rjscm_frame_t frame;
        if (parser_push(&module->parser, bytes[i], &frame)) {
            rjscm_result_t result = dispatch_frame(module, &frame);
            if (result != RJSCM_OK) {
                return result;
            }
        }
    }
    return RJSCM_OK;
}

static bool parser_push(rjscm_parser_t *parser, uint8_t byte, rjscm_frame_t *frame) {
    if (parser->length == 0) {
        if (byte == RJSCM_START_BYTE_0) {
            parser->buffer[parser->length++] = byte;
        }
        return false;
    }

    if (parser->length == 1) {
        if (byte == RJSCM_START_BYTE_1) {
            parser->buffer[parser->length++] = byte;
        } else if (byte == RJSCM_START_BYTE_0) {
            parser->buffer[0] = byte;
        } else {
            parser_reset(parser);
        }
        return false;
    }

    if (parser->length >= RJSCM_MAX_FRAME_LENGTH) {
        parser_reset(parser);
        return false;
    }

    parser->buffer[parser->length++] = byte;
    if (parser->length == RJSCM_HEADER_LENGTH) {
        uint16_t payload_length = read_u16_le(&parser->buffer[6]);
        if (payload_length > RJSCM_MAX_PAYLOAD_LENGTH) {
            parser_reset(parser);
            return false;
        }
        parser->expected_length = RJSCM_HEADER_LENGTH + payload_length + RJSCM_CRC_LENGTH;
    }

    if (parser->expected_length != 0 && parser->length == parser->expected_length) {
        bool valid = validate_frame(parser->buffer, parser->length, frame);
        parser_reset(parser);
        return valid;
    }

    return false;
}

static bool validate_frame(const uint8_t *data, size_t length, rjscm_frame_t *frame) {
    if (length < RJSCM_HEADER_LENGTH + RJSCM_CRC_LENGTH) {
        return false;
    }
    if (data[0] != RJSCM_START_BYTE_0 || data[1] != RJSCM_START_BYTE_1) {
        return false;
    }
    uint16_t payload_length = read_u16_le(&data[6]);
    if (payload_length > RJSCM_MAX_PAYLOAD_LENGTH) {
        return false;
    }
    if (length != RJSCM_HEADER_LENGTH + payload_length + RJSCM_CRC_LENGTH) {
        return false;
    }
    uint16_t expected_crc = read_u16_le(&data[length - RJSCM_CRC_LENGTH]);
    uint16_t actual_crc = crc16_ccitt(&data[2], 6u + payload_length);
    if (expected_crc != actual_crc) {
        return false;
    }

    frame->packet_type = data[2];
    frame->transaction_id = read_u16_le(&data[3]);
    frame->identifier_id = data[5];
    frame->payload = &data[RJSCM_HEADER_LENGTH];
    frame->payload_length = payload_length;
    return true;
}

static void parser_reset(rjscm_parser_t *parser) {
    parser->length = 0;
    parser->expected_length = 0;
}

static rjscm_result_t dispatch_frame(rjscm_module_t *module, const rjscm_frame_t *frame) {
    switch (frame->packet_type) {
        case RJSCM_PACKET_TOPIC:
            return handle_topic(module, frame);
        case RJSCM_PACKET_SERVICE_REQUEST:
            return handle_service_request(module, frame);
        case RJSCM_PACKET_SERVICE_RESPONSE:
            return handle_service_response(module, frame);
        case RJSCM_PACKET_SERVICE_DISCOVERY:
            return handle_discovery(module, frame);
        default:
            return RJSCM_OK;
    }
}

static rjscm_result_t handle_topic(rjscm_module_t *module, const rjscm_frame_t *frame) {
    for (size_t i = 0; i < module->message_count; ++i) {
        rjscm_message_def_t *message = &module->messages[i];
        if (
            message->id == frame->identifier_id &&
            message->payload_size == frame->payload_length &&
            message->callback != NULL
        ) {
            message->callback(frame->payload, frame->payload_length, message->user_data);
        }
    }
    return RJSCM_OK;
}

static rjscm_result_t handle_service_request(
    rjscm_module_t *module,
    const rjscm_frame_t *frame
) {
    rjscm_service_def_t *service = find_served_service_by_id(module, frame->identifier_id);
    if (service == NULL || service->callback == NULL) {
        return send_service_response(
            module,
            frame->transaction_id,
            frame->identifier_id,
            RJSCM_SERVICE_STATUS_UNSUPPORTED,
            NULL,
            0
        );
    }
    if (frame->payload_length != service->request_size) {
        return send_service_response(
            module,
            frame->transaction_id,
            frame->identifier_id,
            RJSCM_SERVICE_STATUS_ERROR,
            NULL,
            0
        );
    }

    uint8_t response[RJSCM_MAX_PAYLOAD_LENGTH - 1u];
    size_t response_length = 0;
    rjscm_result_t result = service->callback(
        frame->payload,
        frame->payload_length,
        response,
        sizeof(response),
        &response_length,
        service->user_data
    );
    if (result != RJSCM_OK || response_length != service->response_size) {
        return send_service_response(
            module,
            frame->transaction_id,
            frame->identifier_id,
            RJSCM_SERVICE_STATUS_ERROR,
            NULL,
            0
        );
    }

    return send_service_response(
        module,
        frame->transaction_id,
        frame->identifier_id,
        RJSCM_SERVICE_STATUS_OK,
        response,
        response_length
    );
}

static rjscm_result_t handle_service_response(
    rjscm_module_t *module,
    const rjscm_frame_t *frame
) {
    rjscm_pending_call_t *pending = &module->pending;
    if (
        !pending->active ||
        pending->transaction_id != frame->transaction_id ||
        pending->service_id != frame->identifier_id
    ) {
        return RJSCM_OK;
    }
    if (frame->payload_length < 1) {
        pending->result = RJSCM_PROTOCOL_ERROR;
        pending->done = true;
        return RJSCM_OK;
    }

    uint8_t status = frame->payload[0];
    pending->remote_status = status;
    if (status == RJSCM_SERVICE_STATUS_UNSUPPORTED) {
        pending->result = RJSCM_UNSUPPORTED;
        pending->done = true;
        return RJSCM_OK;
    }
    if (status != RJSCM_SERVICE_STATUS_OK) {
        pending->result = RJSCM_REMOTE_ERROR;
        pending->done = true;
        return RJSCM_OK;
    }

    size_t response_length = frame->payload_length - 1u;
    if (response_length > pending->response_capacity) {
        pending->result = RJSCM_PROTOCOL_ERROR;
        pending->done = true;
        return RJSCM_OK;
    }
    if (response_length > 0 && pending->response != NULL) {
        memcpy(pending->response, &frame->payload[1], response_length);
    }
    if (pending->response_length != NULL) {
        *pending->response_length = response_length;
    }
    pending->result = RJSCM_OK;
    pending->done = true;
    return RJSCM_OK;
}

static rjscm_result_t handle_discovery(rjscm_module_t *module, const rjscm_frame_t *frame) {
    if (frame->payload_length < 2) {
        return RJSCM_OK;
    }

    uint8_t peer_id = frame->payload[0];
    uint8_t count = frame->payload[1];
    if ((size_t)count + 2u > frame->payload_length) {
        return RJSCM_OK;
    }

    memset(module->discovered[peer_id], 0, RJSCM_DISCOVERY_BITMAP_SIZE);
    for (uint8_t i = 0; i < count; ++i) {
        set_discovered_service(module, peer_id, frame->payload[2u + i]);
    }
    return RJSCM_OK;
}

static rjscm_result_t send_service_response(
    rjscm_module_t *module,
    uint16_t transaction_id,
    uint8_t service_id,
    uint8_t status,
    const void *payload,
    size_t payload_length
) {
    uint8_t response[RJSCM_MAX_PAYLOAD_LENGTH];
    if (payload_length + 1u > sizeof(response)) {
        return RJSCM_INVALID_ARGUMENT;
    }
    response[0] = status;
    if (payload_length > 0) {
        memcpy(&response[1], payload, payload_length);
    }
    return send_frame(
        module,
        RJSCM_PACKET_SERVICE_RESPONSE,
        transaction_id,
        service_id,
        response,
        payload_length + 1u
    );
}

static rjscm_message_def_t *find_message_by_name(rjscm_module_t *module, const char *name) {
    for (size_t i = 0; i < module->message_count; ++i) {
        if (strcmp(module->messages[i].name, name) == 0) {
            return &module->messages[i];
        }
    }
    return NULL;
}

static rjscm_service_def_t *find_service_by_name(rjscm_module_t *module, const char *name) {
    for (size_t i = 0; i < module->service_count; ++i) {
        if (strcmp(module->services[i].name, name) == 0) {
            return &module->services[i];
        }
    }
    return NULL;
}

static rjscm_service_def_t *find_served_service_by_id(rjscm_module_t *module, uint8_t id) {
    for (size_t i = 0; i < module->service_count; ++i) {
        if (module->services[i].id == id && module->services[i].served) {
            return &module->services[i];
        }
    }
    return NULL;
}

static bool service_is_discovered(const rjscm_module_t *module, uint8_t service_id, int peer_id) {
    if (peer_id >= 0 && peer_id <= 255) {
        return has_discovered_service(module, (uint8_t)peer_id, service_id);
    }
    if (peer_id != RJSCM_ANY_PEER) {
        return false;
    }
    for (size_t i = 0; i < 256u; ++i) {
        if (has_discovered_service(module, (uint8_t)i, service_id)) {
            return true;
        }
    }
    return false;
}

static void set_discovered_service(rjscm_module_t *module, uint8_t peer_id, uint8_t service_id) {
    module->discovered[peer_id][service_id / 8u] |= (uint8_t)(1u << (service_id % 8u));
}

static bool has_discovered_service(
    const rjscm_module_t *module,
    uint8_t peer_id,
    uint8_t service_id
) {
    return (
        module->discovered[peer_id][service_id / 8u] &
        (uint8_t)(1u << (service_id % 8u))
    ) != 0;
}

static rjscm_result_t maybe_advertise(rjscm_module_t *module) {
    if (module->advertise_interval_ms == 0) {
        return RJSCM_OK;
    }

    bool has_served_service = false;
    for (size_t i = 0; i < module->service_count; ++i) {
        if (module->services[i].served) {
            has_served_service = true;
            break;
        }
    }
    if (!has_served_service) {
        return RJSCM_OK;
    }

    uint64_t now = monotonic_ms();
    if (module->next_advertise_ms != 0 && now < module->next_advertise_ms) {
        return RJSCM_OK;
    }
    module->next_advertise_ms = now + module->advertise_interval_ms;
    return rjscm_advertise_once(module);
}

static rjscm_result_t maybe_publish_periodic(rjscm_module_t *module) {
    uint64_t now = monotonic_ms();

    for (size_t i = 0; i < module->periodic_publisher_count; ++i) {
        rjscm_periodic_publisher_t *publisher = &module->periodic_publishers[i];
        if (!publisher->active) {
            continue;
        }
        if (publisher->next_publish_ms != 0 && now < publisher->next_publish_ms) {
            continue;
        }

        uint8_t payload[RJSCM_MAX_PAYLOAD_LENGTH];
        size_t payload_length = 0;
        rjscm_result_t result = publisher->provider(
            payload,
            sizeof(payload),
            &payload_length,
            publisher->user_data
        );
        if (result != RJSCM_OK) {
            return result;
        }
        if (payload_length != publisher->payload_size) {
            return RJSCM_INVALID_ARGUMENT;
        }

        result = send_frame(
            module,
            RJSCM_PACKET_TOPIC,
            0,
            publisher->message_id,
            payload,
            payload_length
        );
        if (result != RJSCM_OK) {
            return result;
        }

        now = monotonic_ms();
        publisher->next_publish_ms = now + publisher->interval_ms;
    }

    return RJSCM_OK;
}

static uint16_t next_transaction_id(rjscm_module_t *module) {
    module->next_transaction_id = (uint16_t)(module->next_transaction_id + 1u);
    if (module->next_transaction_id == 0) {
        module->next_transaction_id = 1;
    }
    return module->next_transaction_id;
}

static rjscm_result_t define_standard(rjscm_module_t *module) {
    rjscm_result_t result = RJSCM_OK;

#define CHECK(expr)       \
    do {                  \
        result = (expr);  \
        if (result != RJSCM_OK) return result; \
    } while (0)

    CHECK(rjscm_define_message(module, "robot_pose", RJSCM_TOPIC_ROBOT_POSE, sizeof(rjscm_robot_pose_t)));
    CHECK(rjscm_define_message(module, "ball", RJSCM_TOPIC_WORLD_BALL, sizeof(rjscm_world_ball_t)));
    CHECK(define_message_internal(module, "world_ball", RJSCM_TOPIC_WORLD_BALL, sizeof(rjscm_world_ball_t), true));
    CHECK(rjscm_define_message(module, "opponent", RJSCM_TOPIC_WORLD_OPPONENT, sizeof(rjscm_world_opponent_t)));
    CHECK(define_message_internal(module, "world_opponent", RJSCM_TOPIC_WORLD_OPPONENT, sizeof(rjscm_world_opponent_t), true));
    CHECK(rjscm_define_message(module, "game_state", RJSCM_TOPIC_SYSTEM_GAME_STATE, sizeof(rjscm_system_game_state_t)));
    CHECK(rjscm_define_message(module, "score", RJSCM_TOPIC_SYSTEM_SCORE, sizeof(rjscm_system_score_t)));
    CHECK(rjscm_define_message(module, "match_time", RJSCM_TOPIC_SYSTEM_MATCH_TIME, sizeof(rjscm_system_match_time_t)));
    CHECK(rjscm_define_message(module, "referee_event", RJSCM_TOPIC_SYSTEM_REFEREE_EVENT, sizeof(rjscm_system_referee_event_t)));

    CHECK(rjscm_define_service(module, "see_ball", RJSCM_SERVICE_BALL_IN_VISION, 0, sizeof(uint8_t)));
    CHECK(define_service_internal(module, "ball_in_your_vision", RJSCM_SERVICE_BALL_IN_VISION, 0, sizeof(uint8_t), true));
    CHECK(rjscm_define_service(module, "request_role", RJSCM_SERVICE_REQUEST_ROLE, 0, sizeof(rjscm_role_response_t)));
    CHECK(rjscm_define_service(module, "set_led", RJSCM_SERVICE_SYSTEM_SET_LED, sizeof(rjscm_set_led_request_t), 0));
    CHECK(rjscm_define_service(module, "play_melody", RJSCM_SERVICE_SYSTEM_PLAY_MELODY, sizeof(rjscm_play_melody_request_t), 0));

#undef CHECK
    return RJSCM_OK;
}

static rjscm_result_t copy_name(char *destination, const char *source) {
    size_t length = strlen(source);
    if (length == 0 || length >= RJSCM_NAME_LENGTH) {
        return RJSCM_INVALID_ARGUMENT;
    }
    memcpy(destination, source, length + 1u);
    return RJSCM_OK;
}

static rjscm_periodic_publisher_t *find_periodic_publisher(
    rjscm_module_t *module,
    const char *name
) {
    for (size_t i = 0; i < module->periodic_publisher_count; ++i) {
        if (strcmp(module->periodic_publishers[i].name, name) == 0) {
            return &module->periodic_publishers[i];
        }
    }
    return NULL;
}

static rjscm_result_t see_ball_adapter(
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    void *user_data
) {
    (void)request;
    if (request_length != 0 || response_capacity < 1u || user_data == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    rjscm_service_def_t *service = (rjscm_service_def_t *)user_data;
    if (service->bool_callback == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    uint8_t value = service->bool_callback(service->bool_user_data) ? 1u : 0u;
    memcpy(response, &value, sizeof(value));
    *response_length = sizeof(value);
    return RJSCM_OK;
}

static rjscm_result_t role_adapter(
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    void *user_data
) {
    (void)request;
    if (request_length != 0 || response_capacity < sizeof(rjscm_role_response_t) || user_data == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    rjscm_service_def_t *service = (rjscm_service_def_t *)user_data;
    if (service->role_callback == NULL) {
        return RJSCM_INVALID_ARGUMENT;
    }
    rjscm_role_response_t value = {
        .role = (uint8_t)service->role_callback(service->role_user_data)
    };
    memcpy(response, &value, sizeof(value));
    *response_length = sizeof(value);
    return RJSCM_OK;
}
