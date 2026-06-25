#!/usr/bin/env python3
"""Host-side tests for the Home Assistant switch MQTT protocol."""

import json
import unittest


SWITCH_SET_TOPIC = "indicator/switch/set"
SWITCH_STATE_TOPIC = "indicator/switch/state"
SWITCH_KEYS = [f"switch{i}" for i in range(1, 9)]
SWITCH_COUNT = 8


def parse_switch_message(topic: str, payload: str) -> dict | None:
    """Parse an incoming Home Assistant switch command message."""
    if topic != SWITCH_SET_TOPIC:
        return None

    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        return None

    if not isinstance(data, dict):
        return None

    for index, key in enumerate(SWITCH_KEYS):
        if key not in data:
            continue

        value = data[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return None

        return {"index": index, "value": int(value)}

    return None


def format_switch_publish(index: int, value: int) -> tuple[str, str] | None:
    """Format an outgoing Home Assistant switch state message."""
    if index < 0 or index >= SWITCH_COUNT:
        return None

    payload = json.dumps({SWITCH_KEYS[index]: value})
    return SWITCH_STATE_TOPIC, payload


class TestSwitchIncoming(unittest.TestCase):
    def test_valid_switch_on(self):
        self.assertEqual(
            parse_switch_message(SWITCH_SET_TOPIC, '{"switch1": 1}'),
            {"index": 0, "value": 1},
        )

    def test_valid_switch_off(self):
        self.assertEqual(
            parse_switch_message(SWITCH_SET_TOPIC, '{"switch2": 0}'),
            {"index": 1, "value": 0},
        )

    def test_all_switch_indices(self):
        for i in range(SWITCH_COUNT):
            key = f"switch{i + 1}"
            with self.subTest(key=key):
                result = parse_switch_message(SWITCH_SET_TOPIC, json.dumps({key: 1}))
                self.assertIsNotNone(result)
                self.assertEqual(result["index"], i)

    def test_wrong_topic(self):
        self.assertIsNone(parse_switch_message("other/topic", '{"switch1": 1}'))

    def test_invalid_json(self):
        self.assertIsNone(parse_switch_message(SWITCH_SET_TOPIC, "not_json"))

    def test_missing_key(self):
        self.assertIsNone(parse_switch_message(SWITCH_SET_TOPIC, '{"unknown_key": 1}'))

    def test_non_number_value(self):
        self.assertIsNone(parse_switch_message(SWITCH_SET_TOPIC, '{"switch1": "on"}'))


class TestSwitchPublish(unittest.TestCase):
    def test_publish_format_on(self):
        self.assertEqual(
            format_switch_publish(0, 1),
            (SWITCH_STATE_TOPIC, '{"switch1": 1}'),
        )

    def test_publish_format_off(self):
        self.assertEqual(
            format_switch_publish(3, 0),
            (SWITCH_STATE_TOPIC, '{"switch4": 0}'),
        )

    def test_invalid_index_negative(self):
        self.assertIsNone(format_switch_publish(-1, 1))

    def test_invalid_index_overflow(self):
        self.assertIsNone(format_switch_publish(8, 1))


if __name__ == "__main__":
    unittest.main(verbosity=2)
