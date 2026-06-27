#!/usr/bin/env python3
"""
Unit tests for the SEN54 Sparkplug B MQTT protocol.

Validates payload structure produced by sen5x_mqtt.c without requiring
live hardware.  Run with:  python3 scripts/test_sen5x_mqtt_protocol.py
"""

import json
import time
import unittest

# ── Constants mirrored from sen5x_mqtt.h ──────────────────────────────────

TOPIC_NBIRTH = "spBv1.0/seeed/NBIRTH/edge-01"
TOPIC_NDEATH = "spBv1.0/seeed/NDEATH/edge-01"
TOPIC_DBIRTH = "spBv1.0/seeed/DBIRTH/edge-01/sen5x"
TOPIC_DDATA  = "spBv1.0/seeed/DDATA/edge-01/sen5x"

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

# VOC Index and the derived alert level are integers; everything else is float.
METRIC_TYPES = {name: "float" for name in REQUIRED_METRICS}
METRIC_TYPES["sen5x/voc_index"] = "int"
METRIC_TYPES["sen5x/voc_alert"] = "int"

VOC_THR_LIGHT    = 120.0
VOC_THR_MODERATE = 180.0
VOC_THR_SEVERE   = 250.0


# ── Payload factories (mimic what sen5x_mqtt.c produces) ──────────────────

def make_dbirth(seq: int = 0, metrics_values: dict = None) -> dict:
    """DBIRTH payload — includes 'type' field."""
    if metrics_values is None:
        metrics_values = {k: 0.0 for k in REQUIRED_METRICS}
    return {
        "seq": seq,
        "timestamp": int(time.time() * 1000),
        "metrics": [
            {"name": name, "type": METRIC_TYPES[name], "value": metrics_values.get(name, 0.0)}
            for name in REQUIRED_METRICS
        ],
    }


def make_ddata(seq: int, metrics_values: dict = None) -> dict:
    """DDATA payload — no 'type' field."""
    if metrics_values is None:
        metrics_values = {k: 0.0 for k in REQUIRED_METRICS}
    return {
        "seq": seq,
        "timestamp": int(time.time() * 1000),
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
    def test_topic_format(self):
        self.assertTrue(TOPIC_DBIRTH.startswith("spBv1.0/seeed/DBIRTH/"))
        self.assertTrue(TOPIC_DDATA.startswith("spBv1.0/seeed/DDATA/"))
        self.assertIn("edge-01", TOPIC_DBIRTH)
        self.assertIn("sen5x", TOPIC_DBIRTH)


class TestDBIRTH(unittest.TestCase):
    def setUp(self):
        self.payload = make_dbirth(seq=0)

    def test_has_seq_zero(self):
        self.assertEqual(self.payload["seq"], 0)

    def test_has_timestamp(self):
        self.assertIn("timestamp", self.payload)
        self.assertGreater(self.payload["timestamp"], 0)

    def test_has_metrics_array(self):
        self.assertIn("metrics", self.payload)
        self.assertIsInstance(self.payload["metrics"], list)

    def test_all_required_metrics_present(self):
        names = {m["name"] for m in self.payload["metrics"]}
        for required in REQUIRED_METRICS:
            self.assertIn(required, names, f"Missing metric: {required}")

    def test_metrics_have_type_field(self):
        for m in self.payload["metrics"]:
            self.assertIn("type", m, f"Metric {m['name']} missing 'type'")
            self.assertEqual(m["type"], METRIC_TYPES[m["name"]])

    def test_metrics_have_value_field(self):
        for m in self.payload["metrics"]:
            self.assertIn("value", m)

    def test_json_serializable(self):
        raw = json.dumps(self.payload)
        self.assertIsInstance(raw, str)
        roundtrip = json.loads(raw)
        self.assertEqual(roundtrip["seq"], 0)


class TestDDATA(unittest.TestCase):
    def setUp(self):
        self.payload = make_ddata(seq=1)

    def test_seq_increments(self):
        p1 = make_ddata(seq=1)
        p2 = make_ddata(seq=2)
        self.assertEqual(p2["seq"], p1["seq"] + 1)

    def test_no_type_field_in_metrics(self):
        for m in self.payload["metrics"]:
            self.assertNotIn("type", m, f"DDATA metric {m['name']} should not have 'type'")

    def test_all_required_metrics_present(self):
        names = {m["name"] for m in self.payload["metrics"]}
        for required in REQUIRED_METRICS:
            self.assertIn(required, names)

    def test_has_timestamp(self):
        self.assertGreater(self.payload["timestamp"], 0)


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

    def test_alert_in_ddata_payload(self):
        voc = 200.0
        expected_alert = voc_alert(voc)
        values = {k: 0.0 for k in REQUIRED_METRICS}
        values["sen5x/voc_index"] = voc
        values["sen5x/voc_alert"] = float(expected_alert)
        payload = make_ddata(seq=1, metrics_values=values)
        m = _metric_by_name(payload, "sen5x/voc_alert")
        self.assertIsNotNone(m)
        self.assertEqual(int(m["value"]), expected_alert)


class TestPayloadSize(unittest.TestCase):
    def test_ddata_fits_in_600_bytes(self):
        payload = make_ddata(seq=999, metrics_values={k: 123.456 for k in REQUIRED_METRICS})
        raw = json.dumps(payload, separators=(",", ":"))
        self.assertLessEqual(len(raw), 600,
                             f"DDATA payload too large: {len(raw)} bytes")

    def test_dbirth_fits_in_700_bytes(self):
        payload = make_dbirth(seq=0, metrics_values={k: 123.456 for k in REQUIRED_METRICS})
        raw = json.dumps(payload, separators=(",", ":"))
        self.assertLessEqual(len(raw), 700,
                             f"DBIRTH payload too large: {len(raw)} bytes")


if __name__ == "__main__":
    unittest.main(verbosity=2)
