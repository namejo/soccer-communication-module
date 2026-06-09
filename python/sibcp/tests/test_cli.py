import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from sibcp.cli import _find_service, _parse_bool, _parse_role, build_parser
from sibcp import BALL_IN_VISION_SERVICE, REQUEST_ROLE_SERVICE, TacticalRole


class CliTests(unittest.TestCase):
    def test_find_service_accepts_student_friendly_aliases(self):
        self.assertEqual(_find_service("see_ball").path, BALL_IN_VISION_SERVICE)
        self.assertEqual(_find_service("ball_in_your_vision").path, BALL_IN_VISION_SERVICE)
        self.assertEqual(_find_service("request_role").path, REQUEST_ROLE_SERVICE)

    def test_parse_bool_accepts_common_values(self):
        self.assertTrue(_parse_bool("yes"))
        self.assertTrue(_parse_bool("1"))
        self.assertFalse(_parse_bool("no"))
        self.assertFalse(_parse_bool("0"))

    def test_parse_role_accepts_name_and_number(self):
        self.assertEqual(_parse_role("defender"), {"role": TacticalRole.DEFENDER})
        self.assertEqual(_parse_role("0x01"), {"role": TacticalRole.ATTACKER})

    def test_parser_defaults_to_service_specific_value(self):
        parser = build_parser()
        args = parser.parse_args(["service", "serve", "see_ball"])
        if args.value is None:
            args.value = args.service.default_value
        self.assertEqual(args.value, "false")


if __name__ == "__main__":
    unittest.main()
