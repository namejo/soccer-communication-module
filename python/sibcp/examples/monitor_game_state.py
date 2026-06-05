"""Print firmware system game-state topic updates from UART or USB-C."""

from __future__ import annotations

import argparse

from sibcp import (
    GameState,
    SerialTransport,
    SibcpNode,
    SYSTEM_GAME_STATE_TOPIC,
    define_system_topics,
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baudrate", type=int, default=460800)
    args = parser.parse_args()

    node = SibcpNode(SerialTransport(args.port, baudrate=args.baudrate), logger=print)
    define_system_topics(node)

    @node.on_topic(SYSTEM_GAME_STATE_TOPIC)
    def handle_game_state(message):
        state = GameState(message["state"]).name
        robot_play = "PLAY" if message["robot_play"] else "STOP"
        print(f"{state}: robot output {robot_play}")

    try:
        while True:
            node.poll(timeout=1.0, max_frames=1)
    finally:
        node.close()


if __name__ == "__main__":
    main()
