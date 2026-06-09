#ifndef RJSCM_H
#define RJSCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RJSCM_DEFAULT_BAUDRATE 460800
#define RJSCM_DEFAULT_TIMEOUT_MS 500u
#define RJSCM_ANY_PEER (-1)
#define RJSCM_MAX_PAYLOAD_LENGTH 240u

#define RJSCM_TOPIC_ROBOT_POSE 0x10u
#define RJSCM_TOPIC_WORLD_BALL 0x11u
#define RJSCM_TOPIC_WORLD_OPPONENT 0x12u
#define RJSCM_TOPIC_SYSTEM_GAME_STATE 0xF0u
#define RJSCM_TOPIC_SYSTEM_SCORE 0xF1u
#define RJSCM_TOPIC_SYSTEM_MATCH_TIME 0xF2u
#define RJSCM_TOPIC_SYSTEM_REFEREE_EVENT 0xF3u

#define RJSCM_SERVICE_BALL_IN_VISION 0x01u
#define RJSCM_SERVICE_REQUEST_ROLE 0x10u
#define RJSCM_SERVICE_SYSTEM_SET_LED 0xF0u
#define RJSCM_SERVICE_SYSTEM_PLAY_MELODY 0xF1u

#define RJSCM_DEFINE_MESSAGE_TYPE(module, name, id, type) \
    rjscm_define_message((module), (name), (id), sizeof(type))

#define RJSCM_PUBLISH_VALUE(module, name, value_pointer) \
    rjscm_publish((module), (name), (value_pointer), sizeof(*(value_pointer)))

#define RJSCM_DEFINE_SERVICE_TYPE(module, name, id, request_type, response_type) \
    rjscm_define_service((module), (name), (id), sizeof(request_type), sizeof(response_type))

#define RJSCM_DEFINE_EMPTY_SERVICE_TYPE(module, name, id, response_type) \
    rjscm_define_service((module), (name), (id), 0, sizeof(response_type))

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rjscm_result {
    RJSCM_OK = 0,
    RJSCM_ERROR = -1,
    RJSCM_TIMEOUT = -2,
    RJSCM_INVALID_ARGUMENT = -3,
    RJSCM_NO_SPACE = -4,
    RJSCM_NOT_FOUND = -5,
    RJSCM_IO_ERROR = -6,
    RJSCM_PROTOCOL_ERROR = -7,
    RJSCM_REMOTE_ERROR = -8,
    RJSCM_UNSUPPORTED = -9
} rjscm_result_t;

typedef enum rjscm_game_state {
    RJSCM_GAME_STATE_INIT = 0x00,
    RJSCM_GAME_STATE_DISCONNECTED = 0x01,
    RJSCM_GAME_STATE_PLAY = 0x02,
    RJSCM_GAME_STATE_STOP = 0x03,
    RJSCM_GAME_STATE_DAMAGE = 0x04,
    RJSCM_GAME_STATE_HALF_TIME = 0x05,
    RJSCM_GAME_STATE_GAME_OVER = 0x06
} rjscm_game_state_t;

typedef enum rjscm_tactical_role {
    RJSCM_ROLE_UNKNOWN = 0x00,
    RJSCM_ROLE_ATTACKER = 0x01,
    RJSCM_ROLE_DEFENDER = 0x02,
    RJSCM_ROLE_GOALIE_SUPPORT = 0x03,
    RJSCM_ROLE_SEARCHING = 0x04
} rjscm_tactical_role_t;

typedef enum rjscm_referee_event {
    RJSCM_REF_EVENT_GOAL_OWN = 0x01,
    RJSCM_REF_EVENT_GOAL_OPPONENT = 0x02,
    RJSCM_REF_EVENT_PENALTY_STARTED = 0x03,
    RJSCM_REF_EVENT_PENALTY_ENDED = 0x04,
    RJSCM_REF_EVENT_HALF_STARTED = 0x05,
    RJSCM_REF_EVENT_MATCH_ENDED = 0x06
} rjscm_referee_event_t;

typedef enum rjscm_led_mode {
    RJSCM_LED_MODE_OFF = 0x00,
    RJSCM_LED_MODE_SOLID = 0x01,
    RJSCM_LED_MODE_BLINK = 0x02,
    RJSCM_LED_MODE_PULSE = 0x03
} rjscm_led_mode_t;

typedef enum rjscm_melody_id {
    RJSCM_MELODY_GOAL = 0x01,
    RJSCM_MELODY_ACK = 0x02
} rjscm_melody_id_t;

/* Use RJSCM_PACKED on custom C payload structs to avoid compiler padding. */
#if defined(_MSC_VER)
#define RJSCM_PACKED
#pragma pack(push, 1)
#else
#define RJSCM_PACKED __attribute__((packed))
#endif

typedef struct RJSCM_PACKED rjscm_robot_pose {
    int32_t x_mm;
    int32_t y_mm;
    int16_t heading_mrad;
    uint8_t confidence;
    uint32_t timestamp_ms;
} rjscm_robot_pose_t;

typedef struct RJSCM_PACKED rjscm_world_ball {
    uint8_t visible;
    int32_t x_mm;
    int32_t y_mm;
    uint8_t confidence;
    uint32_t timestamp_ms;
} rjscm_world_ball_t;

typedef struct RJSCM_PACKED rjscm_world_opponent {
    uint8_t visible;
    int32_t x_mm;
    int32_t y_mm;
    uint8_t threat;
    uint32_t timestamp_ms;
} rjscm_world_opponent_t;

typedef struct RJSCM_PACKED rjscm_system_game_state {
    uint8_t state;
    uint8_t robot_play;
} rjscm_system_game_state_t;

typedef struct RJSCM_PACKED rjscm_system_score {
    uint8_t own_score;
    uint8_t opponent_score;
} rjscm_system_score_t;

typedef struct RJSCM_PACKED rjscm_system_match_time {
    uint8_t half;
    uint32_t remaining_ms;
    uint32_t phase_total_ms;
} rjscm_system_match_time_t;

typedef struct RJSCM_PACKED rjscm_system_referee_event {
    uint8_t event;
} rjscm_system_referee_event_t;

typedef struct RJSCM_PACKED rjscm_role_response {
    uint8_t role;
} rjscm_role_response_t;

typedef struct RJSCM_PACKED rjscm_set_led_request {
    uint8_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint16_t duration_ms;
} rjscm_set_led_request_t;

typedef struct RJSCM_PACKED rjscm_play_melody_request {
    uint8_t melody_id;
    uint8_t repeat;
} rjscm_play_melody_request_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

typedef struct rjscm_module rjscm_module_t;

typedef void (*rjscm_message_callback_t)(
    const void *payload,
    size_t payload_length,
    void *user_data
);

typedef rjscm_result_t (*rjscm_topic_provider_t)(
    void *payload,
    size_t payload_capacity,
    size_t *payload_length,
    void *user_data
);

typedef rjscm_result_t (*rjscm_service_callback_t)(
    const void *request,
    size_t request_length,
    void *response,
    size_t response_capacity,
    size_t *response_length,
    void *user_data
);

typedef bool (*rjscm_bool_callback_t)(void *user_data);
typedef rjscm_tactical_role_t (*rjscm_role_callback_t)(void *user_data);

const char *rjscm_strerror(rjscm_result_t result);

rjscm_result_t rjscm_open(
    rjscm_module_t **out_module,
    const char *port,
    uint8_t robot_id,
    int baudrate
);

rjscm_result_t rjscm_open_usb(
    rjscm_module_t **out_module,
    const char *port,
    uint8_t robot_id
);

/* Takes ownership of fd and closes it in rjscm_close(). Useful for tests. */
rjscm_result_t rjscm_open_fd(
    rjscm_module_t **out_module,
    int fd,
    uint8_t robot_id
);

void rjscm_close(rjscm_module_t *module);

rjscm_result_t rjscm_update(rjscm_module_t *module, uint32_t timeout_ms);
rjscm_result_t rjscm_advertise_once(rjscm_module_t *module);
void rjscm_set_advertise_interval(rjscm_module_t *module, uint32_t interval_ms);

rjscm_result_t rjscm_define_message(
    rjscm_module_t *module,
    const char *name,
    uint8_t message_id,
    size_t payload_size
);

rjscm_result_t rjscm_on_message(
    rjscm_module_t *module,
    const char *name,
    rjscm_message_callback_t callback,
    void *user_data
);

rjscm_result_t rjscm_publish(
    rjscm_module_t *module,
    const char *name,
    const void *payload,
    size_t payload_length
);

rjscm_result_t rjscm_publish_at_hz(
    rjscm_module_t *module,
    const char *name,
    double hz,
    rjscm_topic_provider_t provider,
    void *user_data
);

rjscm_result_t rjscm_stop_publishing(
    rjscm_module_t *module,
    const char *name
);

rjscm_result_t rjscm_define_service(
    rjscm_module_t *module,
    const char *name,
    uint8_t service_id,
    size_t request_size,
    size_t response_size
);

rjscm_result_t rjscm_serve(
    rjscm_module_t *module,
    const char *name,
    rjscm_service_callback_t callback,
    void *user_data
);

rjscm_result_t rjscm_wait_for_service(
    rjscm_module_t *module,
    const char *name,
    int peer_id,
    uint32_t timeout_ms
);

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
);

rjscm_result_t rjscm_publish_ball(
    rjscm_module_t *module,
    const rjscm_world_ball_t *ball
);

rjscm_result_t rjscm_publish_pose(
    rjscm_module_t *module,
    const rjscm_robot_pose_t *pose
);

rjscm_result_t rjscm_answer_see_ball(
    rjscm_module_t *module,
    rjscm_bool_callback_t callback,
    void *user_data
);

rjscm_result_t rjscm_ask_peer_sees_ball(
    rjscm_module_t *module,
    int peer_id,
    uint32_t timeout_ms,
    bool *out_sees_ball
);

rjscm_result_t rjscm_answer_role(
    rjscm_module_t *module,
    rjscm_role_callback_t callback,
    void *user_data
);

rjscm_result_t rjscm_ask_peer_role(
    rjscm_module_t *module,
    int peer_id,
    uint32_t timeout_ms,
    rjscm_tactical_role_t *out_role
);

rjscm_result_t rjscm_set_led(
    rjscm_module_t *module,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    rjscm_led_mode_t mode,
    uint16_t duration_ms,
    uint32_t timeout_ms
);

rjscm_result_t rjscm_play_melody(
    rjscm_module_t *module,
    rjscm_melody_id_t melody_id,
    uint8_t repeat,
    uint32_t timeout_ms
);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rjscm {

class Error : public std::runtime_error {
public:
    explicit Error(rjscm_result_t result)
        : std::runtime_error(rjscm_strerror(result)), result_(result) {}

    rjscm_result_t result() const noexcept { return result_; }

private:
    rjscm_result_t result_;
};

inline void check(rjscm_result_t result) {
    if (result != RJSCM_OK) {
        throw Error(result);
    }
}

class Module {
public:
    static Module open(
        const std::string &port = "/dev/ttyACM0",
        uint8_t robot_id = 0,
        int baudrate = RJSCM_DEFAULT_BAUDRATE
    ) {
        rjscm_module_t *handle = nullptr;
        check(rjscm_open(&handle, port.c_str(), robot_id, baudrate));
        return Module(handle);
    }

    explicit Module(rjscm_module_t *handle = nullptr) : handle_(handle) {}
    ~Module() { rjscm_close(handle_); }

    Module(const Module &) = delete;
    Module &operator=(const Module &) = delete;

    Module(Module &&other) noexcept : handle_(other.handle_), holders_(std::move(other.holders_)) {
        other.handle_ = nullptr;
    }

    Module &operator=(Module &&other) noexcept {
        if (this != &other) {
            rjscm_close(handle_);
            handle_ = other.handle_;
            holders_ = std::move(other.holders_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    rjscm_module_t *handle() const noexcept { return handle_; }

    void update(uint32_t timeout_ms = 0) {
        check(rjscm_update(handle_, timeout_ms));
    }

    template <typename Payload>
    void defineMessage(const std::string &name, uint8_t id) {
        check(rjscm_define_message(handle_, name.c_str(), id, sizeof(Payload)));
    }

    template <typename Payload>
    void publish(const std::string &name, const Payload &payload) {
        check(rjscm_publish(handle_, name.c_str(), &payload, sizeof(Payload)));
    }

    template <typename Payload>
    void onMessage(const std::string &name, std::function<void(const Payload &)> callback) {
        auto holder = std::make_unique<MessageHolder<Payload>>(std::move(callback));
        auto *raw = holder.get();
        check(rjscm_on_message(handle_, name.c_str(), &MessageHolder<Payload>::call, raw));
        holders_.push_back(std::move(holder));
    }

    template <typename Payload>
    void subscribe(const std::string &name, std::function<void(const Payload &)> callback) {
        onMessage<Payload>(name, std::move(callback));
    }

    template <typename Payload>
    void publishAtHz(
        const std::string &name,
        double hz,
        std::function<Payload()> provider
    ) {
        auto holder = std::make_unique<TopicProviderHolder<Payload>>(std::move(provider));
        auto *raw = holder.get();
        check(rjscm_publish_at_hz(
            handle_,
            name.c_str(),
            hz,
            &TopicProviderHolder<Payload>::call,
            raw
        ));
        holders_.push_back(std::move(holder));
    }

    void stopPublishing(const std::string &name) {
        check(rjscm_stop_publishing(handle_, name.c_str()));
    }

    template <typename Response>
    void defineService(const std::string &name, uint8_t id) {
        check(rjscm_define_service(handle_, name.c_str(), id, 0, sizeof(Response)));
    }

    template <typename Request, typename Response>
    void defineService(const std::string &name, uint8_t id) {
        check(rjscm_define_service(handle_, name.c_str(), id, sizeof(Request), sizeof(Response)));
    }

    template <typename Response>
    void serve(const std::string &name, std::function<Response()> callback) {
        auto holder = std::make_unique<EmptyServiceHolder<Response>>(std::move(callback));
        auto *raw = holder.get();
        check(rjscm_serve(handle_, name.c_str(), &EmptyServiceHolder<Response>::call, raw));
        holders_.push_back(std::move(holder));
    }

    template <typename Request, typename Response>
    void serve(const std::string &name, std::function<Response(const Request &)> callback) {
        auto holder = std::make_unique<ServiceHolder<Request, Response>>(std::move(callback));
        auto *raw = holder.get();
        check(rjscm_serve(handle_, name.c_str(), &ServiceHolder<Request, Response>::call, raw));
        holders_.push_back(std::move(holder));
    }

    template <typename Response>
    Response callService(
        const std::string &name,
        int peer_id = RJSCM_ANY_PEER,
        uint32_t timeout_ms = RJSCM_DEFAULT_TIMEOUT_MS
    ) {
        Response response{};
        size_t response_length = 0;
        check(rjscm_call_service(
            handle_,
            name.c_str(),
            nullptr,
            0,
            &response,
            sizeof(response),
            &response_length,
            peer_id,
            timeout_ms
        ));
        if (response_length != sizeof(response)) {
            throw Error(RJSCM_PROTOCOL_ERROR);
        }
        return response;
    }

    template <typename Request, typename Response>
    Response callService(
        const std::string &name,
        const Request &request,
        int peer_id = RJSCM_ANY_PEER,
        uint32_t timeout_ms = RJSCM_DEFAULT_TIMEOUT_MS
    ) {
        Response response{};
        size_t response_length = 0;
        check(rjscm_call_service(
            handle_,
            name.c_str(),
            &request,
            sizeof(request),
            &response,
            sizeof(response),
            &response_length,
            peer_id,
            timeout_ms
        ));
        if (response_length != sizeof(response)) {
            throw Error(RJSCM_PROTOCOL_ERROR);
        }
        return response;
    }

    void answerSeeBall(std::function<bool()> callback) {
        auto holder = std::make_unique<BoolHolder>(std::move(callback));
        auto *raw = holder.get();
        check(rjscm_answer_see_ball(handle_, &BoolHolder::call, raw));
        holders_.push_back(std::move(holder));
    }

    bool askPeerSeesBall(
        int peer_id = RJSCM_ANY_PEER,
        uint32_t timeout_ms = RJSCM_DEFAULT_TIMEOUT_MS
    ) {
        bool sees_ball = false;
        check(rjscm_ask_peer_sees_ball(handle_, peer_id, timeout_ms, &sees_ball));
        return sees_ball;
    }

    void publishBall(const rjscm_world_ball_t &ball) {
        check(rjscm_publish_ball(handle_, &ball));
    }

    void publishPose(const rjscm_robot_pose_t &pose) {
        check(rjscm_publish_pose(handle_, &pose));
    }

    void setLed(
        uint8_t red,
        uint8_t green,
        uint8_t blue,
        rjscm_led_mode_t mode = RJSCM_LED_MODE_SOLID,
        uint16_t duration_ms = 0,
        uint32_t timeout_ms = RJSCM_DEFAULT_TIMEOUT_MS
    ) {
        check(rjscm_set_led(handle_, red, green, blue, mode, duration_ms, timeout_ms));
    }

    void playMelody(
        rjscm_melody_id_t melody_id = RJSCM_MELODY_GOAL,
        uint8_t repeat = 1,
        uint32_t timeout_ms = RJSCM_DEFAULT_TIMEOUT_MS
    ) {
        check(rjscm_play_melody(handle_, melody_id, repeat, timeout_ms));
    }

private:
    struct Holder {
        virtual ~Holder() = default;
    };

    template <typename Payload>
    struct MessageHolder : Holder {
        explicit MessageHolder(std::function<void(const Payload &)> callback_in)
            : callback(std::move(callback_in)) {}

        static void call(const void *payload, size_t payload_length, void *user_data) {
            if (payload_length != sizeof(Payload)) {
                return;
            }
            auto *self = static_cast<MessageHolder *>(user_data);
            Payload value{};
            std::memcpy(&value, payload, sizeof(value));
            self->callback(value);
        }

        std::function<void(const Payload &)> callback;
    };

    template <typename Payload>
    struct TopicProviderHolder : Holder {
        explicit TopicProviderHolder(std::function<Payload()> callback_in)
            : callback(std::move(callback_in)) {}

        static rjscm_result_t call(
            void *payload,
            size_t payload_capacity,
            size_t *payload_length,
            void *user_data
        ) {
            if (payload_capacity < sizeof(Payload)) {
                return RJSCM_INVALID_ARGUMENT;
            }
            auto *self = static_cast<TopicProviderHolder *>(user_data);
            Payload value = self->callback();
            std::memcpy(payload, &value, sizeof(value));
            *payload_length = sizeof(value);
            return RJSCM_OK;
        }

        std::function<Payload()> callback;
    };

    template <typename Response>
    struct EmptyServiceHolder : Holder {
        explicit EmptyServiceHolder(std::function<Response()> callback_in)
            : callback(std::move(callback_in)) {}

        static rjscm_result_t call(
            const void *,
            size_t request_length,
            void *response,
            size_t response_capacity,
            size_t *response_length,
            void *user_data
        ) {
            if (request_length != 0 || response_capacity < sizeof(Response)) {
                return RJSCM_INVALID_ARGUMENT;
            }
            auto *self = static_cast<EmptyServiceHolder *>(user_data);
            Response value = self->callback();
            std::memcpy(response, &value, sizeof(value));
            *response_length = sizeof(value);
            return RJSCM_OK;
        }

        std::function<Response()> callback;
    };

    template <typename Request, typename Response>
    struct ServiceHolder : Holder {
        explicit ServiceHolder(std::function<Response(const Request &)> callback_in)
            : callback(std::move(callback_in)) {}

        static rjscm_result_t call(
            const void *request,
            size_t request_length,
            void *response,
            size_t response_capacity,
            size_t *response_length,
            void *user_data
        ) {
            if (
                request_length != sizeof(Request) ||
                response_capacity < sizeof(Response)
            ) {
                return RJSCM_INVALID_ARGUMENT;
            }
            auto *self = static_cast<ServiceHolder *>(user_data);
            Request request_value{};
            std::memcpy(&request_value, request, sizeof(request_value));
            Response response_value = self->callback(request_value);
            std::memcpy(response, &response_value, sizeof(response_value));
            *response_length = sizeof(response_value);
            return RJSCM_OK;
        }

        std::function<Response(const Request &)> callback;
    };

    struct BoolHolder : Holder {
        explicit BoolHolder(std::function<bool()> callback_in)
            : callback(std::move(callback_in)) {}

        static bool call(void *user_data) {
            auto *self = static_cast<BoolHolder *>(user_data);
            return self->callback();
        }

        std::function<bool()> callback;
    };

    rjscm_module_t *handle_ = nullptr;
    std::vector<std::unique_ptr<Holder>> holders_;
};

}  // namespace rjscm

#endif  // __cplusplus

#endif  // RJSCM_H
