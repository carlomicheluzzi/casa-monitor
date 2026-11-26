from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from cloud_inventory import load_key
from monitor import Alarm, DEFAULT_CONFIG, Monitor, validate_config


def inventory(marker="2026-10-30 10:00:00", watts=(50, 1000, 80, 1200), online=True):
    devices = []
    for index, device_id in enumerate(("485519d9cf2f", "485519d9502e")):
        devices.append({
            "id": device_id, "model_code": "SHEM", "model_name": "Shelly EM",
            "online": online, "cloud_updated_raw": marker,
            "power_channels": [
                {"source": f"emeters[{channel}]", "channel": channel,
                 "power_w": watts[index * 2 + channel], "voltage_v": 230.0,
                 "measurement_valid": True} for channel in (0, 1)
            ],
        })
    return {"devices": devices}


def ready(monitor, watts=(50, 1000, 80, 1200)):
    monitor.ingest(inventory(watts=watts), now=100)
    monitor.ingest(inventory("2026-10-30 10:00:03", watts=watts), now=103)


class MonitoringTests(unittest.TestCase):
    def test_initial_cached_data_needs_source_update(self):
        monitor = Monitor()
        monitor.ingest(inventory(), now=100)
        monitor.ingest(inventory(), now=103)
        state = monitor.snapshot(now=104)
        self.assertIsNone(state["total_w"])
        self.assertEqual({c["reason"] for c in state["circuits"]}, {"verifying"})
        self.assertTrue(all(c["last_power_w"] is not None for c in state["circuits"]))

    def test_four_unique_channels_sum_after_advancing_update(self):
        monitor = Monitor()
        ready(monitor)
        state = monitor.snapshot(now=104)
        self.assertEqual(state["total_w"], 2330)
        self.assertEqual(state["remaining_w"], 2170)
        self.assertEqual(state["alarm"]["level"], "normal")

    def test_repeated_http_success_does_not_refresh_source_age(self):
        monitor = Monitor()
        ready(monitor)
        monitor.ingest(inventory("2026-10-30 10:00:03"), now=210)
        state = monitor.snapshot(now=210)
        self.assertIsNone(state["total_w"])
        self.assertEqual(state["alarm"]["level"], "unavailable")
        self.assertEqual(state["circuits"][0]["reason"], "stale")

    def test_missing_channel_is_not_zero(self):
        monitor = Monitor()
        ready(monitor)
        data = inventory("2026-10-30 10:00:06")
        data["devices"][0]["power_channels"].pop()
        monitor.ingest(data, now=106)
        state = monitor.snapshot(now=107)
        self.assertIsNone(state["total_w"])
        self.assertEqual(sum(c["available"] for c in state["circuits"]), 3)

    def test_invalid_nan_and_offline_measurements_are_rejected(self):
        for invalid in (float("nan"), float("inf"), True, "50"):
            monitor = Monitor()
            ready(monitor)
            monitor.ingest(inventory("2026-10-30 10:00:06", watts=(invalid, 1, 2, 3)), now=106)
            state = monitor.snapshot(now=107)
            self.assertIsNone(state["total_w"])
            json.dumps(state, allow_nan=False)
        monitor.ingest(inventory("2026-10-30 10:00:09", online=False), now=109)
        self.assertEqual({c["reason"] for c in monitor.snapshot(now=110)["circuits"]}, {"offline"})

    def test_alarm_survives_missing_data_and_transport_failure(self):
        monitor = Monitor()
        ready(monitor, watts=(1000, 2000, 1000, 1000))
        self.assertTrue(monitor.snapshot(now=104)["alarm"]["active"])
        monitor.record_error("Cloud non raggiungibile")
        state = monitor.snapshot(now=105)
        self.assertIsNone(state["total_w"])
        self.assertTrue(state["alarm"]["audible"])
        self.assertEqual(state["alarm"]["level"], "alarm")

    def test_clock_regression_requires_new_advancing_update(self):
        monitor = Monitor()
        ready(monitor)
        monitor.ingest(inventory("2026-10-30 09:59:00"), now=106)
        self.assertIsNone(monitor.snapshot(now=107)["total_w"])
        monitor.ingest(inventory("2026-10-30 09:59:03"), now=109)
        self.assertEqual(monitor.snapshot(now=110)["total_w"], 2330)

    def test_demo_is_separate_from_real_alarm_and_mute(self):
        monitor = Monitor()
        ready(monitor)
        monitor.set_scenario("overload")
        self.assertTrue(monitor.snapshot("demo", now=104)["alarm"]["active"])
        monitor.mute("demo", now=104)
        self.assertFalse(monitor.snapshot("demo", now=105)["alarm"]["audible"])
        self.assertFalse(monitor.snapshot(now=105)["alarm"]["active"])
        monitor.set_scenario("offline")
        self.assertTrue(monitor.snapshot("demo", now=106)["alarm"]["active"])
        monitor.set_scenario("normal")
        self.assertFalse(monitor.snapshot("demo", now=107)["alarm"]["active"])

    def test_unknown_scenario_is_rejected(self):
        with self.assertRaises(ValueError):
            Monitor().set_scenario("unexpected")


class AlarmTests(unittest.TestCase):
    def test_hysteresis_mute_expiry_and_new_episode(self):
        alarm, config = Alarm(), deepcopy(DEFAULT_CONFIG)
        self.assertEqual(alarm.evaluate(4500, config, 100)["level"], "warning")
        self.assertTrue(alarm.evaluate(4501, config, 101)["audible"])
        alarm.mute(120, 102)
        self.assertFalse(alarm.evaluate(4400, config, 110)["audible"])
        self.assertTrue(alarm.evaluate(None, config, 223)["audible"])
        self.assertFalse(alarm.evaluate(4300, config, 224)["active"])
        self.assertTrue(alarm.evaluate(5000, config, 225)["audible"])


class ConfigTests(unittest.TestCase):
    def test_duplicate_channel_and_impossible_threshold_are_rejected(self):
        config = deepcopy(DEFAULT_CONFIG)
        config["circuits"][0]["channel"] = config["circuits"][1]["channel"]
        with self.assertRaises(ValueError): validate_config(config)
        config = deepcopy(DEFAULT_CONFIG)
        config["warning_w"] = config["limit_w"]
        with self.assertRaises(ValueError): validate_config(config)

    def test_saved_config_survives_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            config = deepcopy(DEFAULT_CONFIG)
            config["limit_w"], config["mapping_confirmed"] = 5000, True
            Monitor(path).save_config(config)
            self.assertEqual(Monitor(path).config, config)

    def test_key_file_supports_quotes_comments_and_export(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / ".env"
            path.write_text('export SHELLY_AUTH_KEY="example-test-key"\nOTHER=not-used\n')
            self.assertEqual(load_key(path), "example-test-key")
            path.write_text('SHELLY_AUTH_KEY=example-test-key # comment\n')
            self.assertEqual(load_key(path), "example-test-key")


if __name__ == "__main__":
    unittest.main()
