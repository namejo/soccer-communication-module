#include "rjscm.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    const std::string port = argc > 1 ? argv[1] : "/dev/ttyACM0";
    const auto robot_id = static_cast<uint8_t>(argc > 2 ? std::strtoul(argv[2], nullptr, 0) : 1u);
    const int peer_id = argc > 3 ? static_cast<int>(std::strtol(argv[3], nullptr, 0)) : 2;
    const auto timeout_ms = static_cast<uint32_t>(
        argc > 4 ? std::strtoul(argv[4], nullptr, 0) : 2000u
    );

    try {
        auto robot = rjscm::Module::open(port, robot_id);
        bool sees_ball = robot.askPeerSeesBall(peer_id, timeout_ms);
        std::cout << "Robot " << peer_id << " sees ball: "
                  << (sees_ball ? "yes" : "no") << "\n";
    } catch (const rjscm::Error &error) {
        std::cerr << "RJSCM error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
