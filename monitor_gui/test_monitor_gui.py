#!/usr/bin/env python3
"""Tests for the ESP Bridge Debug Monitor GUI core logic."""

import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))

from monitor_gui import DataManager, SerialReader


class TestDataManager(unittest.TestCase):
    """Tests for DataManager class."""

    def setUp(self):
        self.dm = DataManager(max_history=100)

    def test_detect_type_integer(self):
        dtype, val = DataManager.detect_type("42")
        self.assertEqual(dtype, "integer")
        self.assertEqual(val, 42)

    def test_detect_type_negative_integer(self):
        dtype, val = DataManager.detect_type("-7")
        self.assertEqual(dtype, "integer")
        self.assertEqual(val, -7)

    def test_detect_type_double(self):
        dtype, val = DataManager.detect_type("3.14")
        self.assertEqual(dtype, "double")
        self.assertAlmostEqual(val, 3.14)

    def test_detect_type_bool_true(self):
        dtype, val = DataManager.detect_type("true")
        self.assertEqual(dtype, "bool")
        self.assertTrue(val)

    def test_detect_type_bool_false(self):
        dtype, val = DataManager.detect_type("False")
        self.assertEqual(dtype, "bool")
        self.assertFalse(val)

    def test_detect_type_string(self):
        dtype, val = DataManager.detect_type("hello world")
        self.assertEqual(dtype, "string")
        self.assertEqual(val, "hello world")

    def test_add_data_integer(self):
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "count", "10")
        data = self.dm.get_table_data("Robot 1")
        self.assertEqual(len(data), 1)
        self.assertEqual(data[0]["name"], "count")
        self.assertEqual(data[0]["type"], "integer")
        self.assertEqual(data[0]["value"], 10)
        self.assertEqual(data[0]["min"], 10)
        self.assertEqual(data[0]["max"], 10)

    def test_add_data_double_min_max(self):
        mac = "AA:BB:CC:DD:EE:FF"
        self.dm.add_data(mac, "temp", "20.5")
        self.dm.add_data(mac, "temp", "18.2")
        self.dm.add_data(mac, "temp", "25.7")
        data = self.dm.get_table_data("Robot 1")
        entry = [d for d in data if d["name"] == "temp"][0]
        self.assertAlmostEqual(entry["min"], 18.2)
        self.assertAlmostEqual(entry["max"], 25.7)
        self.assertAlmostEqual(entry["value"], 25.7)

    def test_add_data_bool(self):
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "active", "true")
        data = self.dm.get_table_data("Robot 1")
        entry = data[0]
        self.assertEqual(entry["type"], "bool")
        self.assertTrue(entry["value"])

    def test_add_data_string(self):
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "status", "OK")
        data = self.dm.get_table_data("Robot 1")
        entry = data[0]
        self.assertEqual(entry["type"], "string")
        self.assertEqual(entry["value"], "OK")
        self.assertEqual(entry["min"], "-")
        self.assertEqual(entry["max"], "-")

    def test_robot_assignment_first_mac(self):
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "x", "1")
        self.assertEqual(self.dm.mac_to_robot["AA:BB:CC:DD:EE:FF"], "Robot 1")

    def test_robot_assignment_second_mac(self):
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "x", "1")
        self.dm.add_data("11:22:33:44:55:66", "y", "2")
        self.assertEqual(self.dm.mac_to_robot["AA:BB:CC:DD:EE:FF"], "Robot 1")
        self.assertEqual(self.dm.mac_to_robot["11:22:33:44:55:66"], "Robot 2")

    def test_robot_data_separation(self):
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "a", "10")
        self.dm.add_data("11:22:33:44:55:66", "b", "20")
        r1 = self.dm.get_table_data("Robot 1")
        r2 = self.dm.get_table_data("Robot 2")
        self.assertEqual(len(r1), 1)
        self.assertEqual(r1[0]["name"], "a")
        self.assertEqual(len(r2), 1)
        self.assertEqual(r2[0]["name"], "b")

    def test_graph_data_numeric_only(self):
        mac = "AA:BB:CC:DD:EE:FF"
        self.dm.add_data(mac, "number", "42")
        self.dm.add_data(mac, "text", "hello")
        gdata = self.dm.get_graph_data("Robot 1")
        self.assertIn("number", gdata)
        self.assertNotIn("text", gdata)

    def test_numeric_keys(self):
        mac = "AA:BB:CC:DD:EE:FF"
        self.dm.add_data(mac, "speed", "5.5")
        self.dm.add_data(mac, "active", "true")
        self.dm.add_data(mac, "label", "foo")
        keys = self.dm.get_numeric_keys("Robot 1")
        self.assertIn("speed", keys)
        self.assertIn("active", keys)
        self.assertNotIn("label", keys)

    def test_reset(self):
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "x", "1")
        self.dm.reset()
        self.assertEqual(self.dm.get_table_data("Robot 1"), [])
        self.assertEqual(self.dm.get_table_data("Robot 2"), [])
        self.assertEqual(self.dm.get_known_macs(), {})

    def test_callback(self):
        called = []
        self.dm.on_update(lambda: called.append(True))
        self.dm.add_data("AA:BB:CC:DD:EE:FF", "x", "1")
        self.assertTrue(len(called) > 0)


class TestSerialReaderParser(unittest.TestCase):
    """Tests for SerialReader line parsing."""

    def setUp(self):
        self.dm = DataManager()
        self.reader = SerialReader(self.dm)

    def test_parse_colon_format(self):
        self.reader._parse("[34:94:54:AB:CD:EF] Ball Angle: 180")
        data = self.dm.get_table_data("Robot 1")
        self.assertEqual(len(data), 1)
        self.assertEqual(data[0]["name"], "Ball Angle")
        self.assertEqual(data[0]["value"], 180)

    def test_parse_equals_format(self):
        self.reader._parse("[34:94:54:AB:CD:EF] Temperature=25.5")
        data = self.dm.get_table_data("Robot 1")
        self.assertEqual(len(data), 1)
        self.assertEqual(data[0]["name"], "Temperature")
        self.assertAlmostEqual(data[0]["value"], 25.5)

    def test_parse_space_format(self):
        self.reader._parse("[34:94:54:AB:CD:EF] Distance 42")
        data = self.dm.get_table_data("Robot 1")
        self.assertEqual(len(data), 1)
        self.assertEqual(data[0]["name"], "Distance")
        self.assertEqual(data[0]["value"], 42)

    def test_parse_bool_value(self):
        self.reader._parse("[34:94:54:AB:CD:EF] Active: true")
        data = self.dm.get_table_data("Robot 1")
        self.assertEqual(data[0]["type"], "bool")
        self.assertTrue(data[0]["value"])

    def test_parse_two_robots(self):
        self.reader._parse("[34:94:54:AB:CD:EF] Sensor: 100")
        self.reader._parse("[34:94:54:11:22:33] Motor: 200")
        r1 = self.dm.get_table_data("Robot 1")
        r2 = self.dm.get_table_data("Robot 2")
        self.assertEqual(r1[0]["name"], "Sensor")
        self.assertEqual(r2[0]["name"], "Motor")

    def test_parse_ignores_non_matching(self):
        self.reader._parse("=== ESP32 Debug Monitor ===")
        self.assertEqual(self.dm.get_table_data("Robot 1"), [])

    def test_parse_fallback_message(self):
        self.reader._parse("[34:94:54:AB:CD:EF] CONNECTED")
        data = self.dm.get_table_data("Robot 1")
        self.assertEqual(len(data), 1)
        self.assertEqual(data[0]["name"], "message")

    def test_raw_callback(self):
        lines = []
        self.reader.on_raw_line(lambda l: lines.append(l))
        # Raw callback is called from run(), not from _parse()
        # This just tests the registration
        self.assertEqual(len(self.reader.raw_callbacks), 1)


if __name__ == "__main__":
    unittest.main()
