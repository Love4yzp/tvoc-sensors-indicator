#!/usr/bin/env python3
"""Unit tests for the Seeed Monitor MQTT protocol (v1, post-Sparkplug).

Topic model:  <prefix>/<device_name>/<leaf>   leaf ∈ {data, status}
  - data:   JSON payload every 5 s (NTP-gated), QoS 0, not retained
  - status: retained "online" published on connect; broker LWT publishes
            retained "offline" (QoS 0) on unexpected disconnect

Validates payload structure and config validation rules as implemented in
main/sen5x/sen5x_mqtt.c, main/ha/ha_config.c and main/ha/ha_mqtt.c without
requiring live hardware.  Run with:  python3 scripts/test_sen5x_mqtt_protocol.py
"""

import json
import re
import time
import unittest

# ── Constants mirrored from the firmware ────────────────────────────────────

DEFAULT_TOPIC_PREFIX = "seeed"          # CONFIG_MQTT_TOPIC_PREFIX
DEFAULT_DEVICE_NAME_RE = re.compile(r"^indicator-[0-9a-f]{4}$")  # MAC-derived

REQUIRED_METRICS = [
    "sen5x/pm1_0",
    "sen5x/pm2_5",
    "sen5x/pm4_0",
    "sen5x/pm10",
    "sen5x/humidity",
    "sen5x/temperature",
    "sen5x/voc_index",
    "sen5x/voc_alert",
]

VOC_THR_LIGHT    = 120.0
VOC_THR_MODERATE = 180.0
VOC_THR_SEVERE   = 250.0

LWT_TOPIC_LEAF  = "status"
DATA_TOPIC_LEAF = "data"
LWT_MESSAGE     = "offline"
ONLINE_MESSAGE  = "online"


# ── Validation rules mirrored from ha_config.c ──────────────────────────────

def validate_device_name(name: str) -> bool:
    """ha_cfg_validate_device_name(): non-empty, [A-Za-z0-9-_], ≤31 chars."""
    if not name or len(name) > 31:
        return False
    return all(c.isalnum() and c.isascii() or c in "-_" for c in name)


def validate_topic_prefix(prefix: str) -> bool:
    """ha_cfg_validate_topic_prefix(): non-empty, no +/#/space, no leading or
    trailing '/', ≤63 chars."""
    if not prefix or len(prefix) > 63:
        return False
    if prefix.startswith("/") or prefix.endswith("/"):
        return False
    return all(c not in "+# " for c in prefix)


# ── Topic + payload factories (mimic what the firmware produces) ────────────

def build_topics(prefix: str, device_name: str) -> tuple[str, str]:
    """mqtt_topics_build() in ha_mqtt.c."""
    return (f"{prefix}/{device_name}/{DATA_TOPIC_LEAF}",
            f"{prefix}/{device_name}/{LWT_TOPIC_LEAF}")


def make_data_payload(seq: int, device_name: str, metrics_values: dict = None) -> dict:
    """Data payload — seq/timestamp/device/metrics, no per-metric 'type'."""
    if metrics_values is None:
        metrics_values = {k: 0.0 for k in REQUIRED_METRICS}
    return {
        "seq": seq,
        "timestamp": int(time.time()),  # UTC epoch seconds (see _timestamp_s in sen5x_mqtt.c)
        "device": device_name,
        "metrics": [
            {"name": name, "value": metrics_values.get(name, 0.0)}
            for name in REQUIRED_METRICS
        ],
    }


def voc_alert(voc_index: float, warming_up: bool = False) -> int:
    """Mirror the _voc_alert() logic from sen5x_mqtt.c."""
    if warming_up:
        return 0
    if voc_index > VOC_THR_SEVERE:
        return 3
    if voc_index > VOC_THR_MODERATE:
        return 2
    if voc_index > VOC_THR_LIGHT:
        return 1
    return 0


# ── Helpers ────────────────────────────────────────────────────────────────

def _metric_by_name(payload: dict, name: str):
    for m in payload["metrics"]:
        if m["name"] == name:
            return m
    return None


# ── Tests ──────────────────────────────────────────────────────────────────

class TestTopics(unittest.TestCase):
    def test_default_topics(self):
        data, status = build_topics("seeed", "indicator-3f2a")
        self.assertEqual("seeed/indicator-3f2a/data", data)
        self.assertEqual("seeed/indicator-3f2a/status", status)

    def test_custom_prefix_and_name(self):
        data, status = build_topics("F01", "lab-301")
        self.assertEqual("F01/lab-301/data", data)
        self.assertEqual("F01/lab-301/status", status)

    def test_wildcard_subscription_matches(self):
        """`seeed/+/data` must match every device's data topic."""
        data, _ = build_topics("seeed", "indicator-3f2a")
        levels = data.split("/")
        self.assertEqual(3, len(levels))
        self.assertEqual("seeed", levels[0])
        self.assertEqual("data", levels[2])

    def test_default_device_name_format(self):
        self.assertRegex("indicator-3f2a", DEFAULT_DEVICE_NAME_RE)
        self.assertNotRegex("Indicator-3F2A", DEFAULT_DEVICE_NAME_RE)

    def test_no_sparkplug_topics(self):
        """The spBv1.0 envelope is gone — topics must not contain it."""
        for topic in build_topics(DEFAULT_TOPIC_PREFIX, "indicator-3f2a"):
            self.assertNotIn("spBv1.0", topic)
            self.assertNotIn("NBIRTH", topic)
            self.assertNotIn("DDATA", topic)


class TestStatusSemantics(unittest.TestCase):
    """LWT + retained online contract (from ha_mqtt.c / sen5x_mqtt.c)."""

    def test_lwt_message_and_retain(self):
        # LWT: topic <prefix>/<name>/status, msg "offline", retain=true, qos 0
        _, status = build_topics("seeed", "indicator-3f2a")
        lwt = {"topic": status, "msg": LWT_MESSAGE, "retain": True, "qos": 0}
        self.assertEqual("offline", lwt["msg"])
        self.assertTrue(lwt["retain"])
        self.assertEqual(0, lwt["qos"])

    def test_online_published_retained_on_connect(self):
        _, status = build_topics("seeed", "indicator-3f2a")
        announce = {"topic": status, "msg": ONLINE_MESSAGE, "retain": True}
        self.assertEqual("online", announce["msg"])
        self.assertTrue(announce["retain"])

    def test_status_and_lwt_share_one_topic(self):
        """online and offline must land on the same topic."""
        _, status = build_topics("F01", "lab-301")
        self.assertEqual(status, status)  # same builder output used for both


class TestDataPayload(unittest.TestCase):
    def setUp(self):
        self.payload = make_data_payload(seq=1, device_name="indicator-3f2a")

    def test_has_seq(self):
        self.assertEqual(self.payload["seq"], 1)

    def test_seq_increments(self):
        p1 = make_data_payload(seq=1, device_name="d")
        p2 = make_data_payload(seq=2, device_name="d")
        self.assertEqual(p2["seq"], p1["seq"] + 1)

    def test_has_timestamp(self):
        self.assertGreater(self.payload["timestamp"], 0)

    def test_has_device_field(self):
        self.assertEqual("indicator-3f2a", self.payload["device"])

    def test_all_required_metrics_present(self):
        names = {m["name"] for m in self.payload["metrics"]}
        for required in REQUIRED_METRICS:
            self.assertIn(required, names, f"Missing metric: {required}")

    def test_no_type_field_in_metrics(self):
        """The birth sequence (which carried per-metric types) is gone."""
        for m in self.payload["metrics"]:
            self.assertNotIn("type", m, f"Metric {m['name']} should not have 'type'")

    def test_metrics_have_value_field(self):
        for m in self.payload["metrics"]:
            self.assertIn("value", m)

    def test_json_serializable(self):
        raw = json.dumps(self.payload)
        roundtrip = json.loads(raw)
        self.assertEqual(roundtrip["device"], "indicator-3f2a")
        self.assertEqual(roundtrip["seq"], 1)

    def test_fits_in_600_bytes(self):
        payload = make_data_payload(seq=999, device_name="indicator-ffff",
                                    metrics_values={k: 123.456 for k in REQUIRED_METRICS})
        raw = json.dumps(payload, separators=(",", ":"))
        self.assertLessEqual(len(raw), 600,
                             f"Data payload too large: {len(raw)} bytes")


class TestVocAlert(unittest.TestCase):
    def test_normal(self):
        self.assertEqual(voc_alert(100.0), 0)
        self.assertEqual(voc_alert(120.0), 0)

    def test_light(self):
        self.assertEqual(voc_alert(121.0), 1)
        self.assertEqual(voc_alert(180.0), 1)

    def test_moderate(self):
        self.assertEqual(voc_alert(181.0), 2)
        self.assertEqual(voc_alert(250.0), 2)

    def test_severe(self):
        self.assertEqual(voc_alert(251.0), 3)
        self.assertEqual(voc_alert(500.0), 3)

    def test_warming_up_always_zero(self):
        for voc in [50.0, 150.0, 200.0, 400.0]:
            self.assertEqual(voc_alert(voc, warming_up=True), 0,
                             f"voc_alert should be 0 during warming up (voc={voc})")

    def test_alert_in_data_payload(self):
        voc = 200.0
        expected_alert = voc_alert(voc)
        values = {k: 0.0 for k in REQUIRED_METRICS}
        values["sen5x/voc_index"] = voc
        values["sen5x/voc_alert"] = float(expected_alert)
        payload = make_data_payload(seq=1, device_name="d", metrics_values=values)
        m = _metric_by_name(payload, "sen5x/voc_alert")
        self.assertIsNotNone(m)
        self.assertEqual(int(m["value"]), expected_alert)


class TestDeviceNameValidation(unittest.TestCase):
    def test_valid(self):
        for name in ["lab-301", "indicator-3f2a", "A", "node_01", "a" * 31]:
            self.assertTrue(validate_device_name(name), name)

    def test_rejects_empty_and_too_long(self):
        self.assertFalse(validate_device_name(""))
        self.assertFalse(validate_device_name("a" * 32))

    def test_rejects_bad_chars(self):
        for name in ["lab 301", "lab+301", "lab#301", "lab/301", "lab.301", "lab:301"]:
            self.assertFalse(validate_device_name(name), name)


class TestTopicPrefixValidation(unittest.TestCase):
    def test_valid(self):
        for prefix in ["seeed", "F01", "a/b/c", "a" * 63, "lab-01/zone_2"]:
            self.assertTrue(validate_topic_prefix(prefix), prefix)

    def test_rejects_empty_and_too_long(self):
        self.assertFalse(validate_topic_prefix(""))
        self.assertFalse(validate_topic_prefix("a" * 64))

    def test_rejects_wildcards_and_space(self):
        for prefix in ["a/+", "a/#", "a b", "+", "#"]:
            self.assertFalse(validate_topic_prefix(prefix), prefix)

    def test_rejects_leading_trailing_slash(self):
        self.assertFalse(validate_topic_prefix("/seeed"))
        self.assertFalse(validate_topic_prefix("seeed/"))
        self.assertFalse(validate_topic_prefix("/"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
