import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from sibcp import PacketType, StreamingParser, crc16_ccitt, decode_frame, encode_frame


class ProtocolTests(unittest.TestCase):
    def test_crc16_ccitt_reference_value(self):
        self.assertEqual(crc16_ccitt(b"123456789"), 0x29B1)

    def test_frame_roundtrip(self):
        raw = encode_frame(
            PacketType.SERVICE_REQUEST,
            transaction_id=402,
            identifier_id=1,
            payload=b"\x01\x02\x03",
        )

        frame = decode_frame(raw)

        self.assertEqual(frame.packet_type, PacketType.SERVICE_REQUEST)
        self.assertEqual(frame.transaction_id, 402)
        self.assertEqual(frame.identifier_id, 1)
        self.assertEqual(frame.payload, b"\x01\x02\x03")
        self.assertEqual(frame.raw, raw)

    def test_streaming_parser_resyncs_after_noise(self):
        first = encode_frame(PacketType.TOPIC, 0, 1, b"\x01")
        second = encode_frame(PacketType.SERVICE_RESPONSE, 7, 1, b"\x00\x01")
        parser = StreamingParser()

        frames = parser.feed(b"noise" + first + b"\x00\xff" + second)

        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0].payload, b"\x01")
        self.assertEqual(frames[1].transaction_id, 7)


if __name__ == "__main__":
    unittest.main()
