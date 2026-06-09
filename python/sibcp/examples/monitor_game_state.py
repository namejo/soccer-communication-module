"""Print firmware system game-state topic updates from UART or USB-C."""

from __future__ import annotations

import argparse

from sibcp import (
    GameState,
    RefereeEvent,
    SerialTransport,
    SibcpNode,
    SYSTEM_GAME_STATE_TOPIC,
    SYSTEM_MATCH_TIME_TOPIC,
    SYSTEM_REFEREE_EVENT_TOPIC,
    SYSTEM_SCORE_TOPIC,
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

    @node.on_topic(SYSTEM_SCORE_TOPIC)
    def handle_score(message):
        print(f"score: own={message['own_score']} opponent={message['opponent_score']}")

    @node.on_topic(SYSTEM_MATCH_TIME_TOPIC)
    def handle_match_time(message):
        remaining_s = message["remaining_ms"] / 1000.0
        total_s = message["phase_total_ms"] / 1000.0
        print(
            f"match time: half={message['half']} "
            f"remaining={remaining_s:.1f}s total={total_s:.1f}s"
        )

    @node.on_topic(SYSTEM_REFEREE_EVENT_TOPIC)
    def handle_referee_event(message):
        print(f"referee event: {RefereeEvent(message['event']).name}")

    try:
        while True:
            node.poll(timeout=1.0, max_frames=1)
    finally:
        node.close()


if __name__ == "__main__":
    main()
