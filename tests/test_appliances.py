from copy import deepcopy
from pathlib import Path
import tempfile
import unittest

from appliances import DEFAULT_APPLIANCES, validate_appliances
from cloud_inventory import normalize_inventory
from monitor import DEFAULT_CONFIG, Monitor
from test_monitor import inventory, ready


class InventoryEnergyTests(unittest.TestCase):
    def test_em_gen1_energy_and_switch_energy_are_wh(self):
        normalized = normalize_inventory({
            "meter": {"_dev_info": {"id": "AABBCCDDEEFF", "code": "SHEM"}, "emeters": [{"power": 300, "total": 12500, "is_valid": True}]},
            "relay": {"_dev_info": {"id": "112233445566"}, "switch:0": {"id": 0, "apower": 0, "output": True, "aenergy": {"total": 0}}},
        })
        meter, relay = normalized["devices"]
        self.assertEqual(meter["id"], "aabbccddeeff")
        self.assertEqual(meter["power_channels"][0]["energy_wh"], 12500)
        self.assertEqual(relay["power_channels"][0]["energy_wh"], 0)
        self.assertEqual(relay["power_channels"][0]["power_w"], 0)
        self.assertTrue(relay["power_channels"][0]["relay_on"])

    def test_em_gen3_ct_channels_use_separate_em1data_counters(self):
        devices = normalize_inventory({"aabbccddeeff": {
            "em1:0": {"id": 0, "act_power": 1100, "voltage": 230},
            "em1data:0": {"total_act_energy": 10000},
            "em1:1": {"id": 1, "act_power": 700, "voltage": 230},
            "em1data:1": {"total_act_energy": 22000},
        }})["devices"]
        self.assertEqual([c["energy_wh"] for c in devices[0]["power_channels"]], [10000, 22000])
        self.assertEqual([c["power_w"] for c in devices[0]["power_channels"]], [1100, 700])

    def test_meter_failure_is_not_valid_power(self):
        channel = normalize_inventory({"aabbccddeeff": {"em1:0": {"id": 0, "act_power": 0, "errors": ["power_meter_failure"]}}})["devices"][0]["power_channels"][0]
        self.assertFalse(channel["measurement_valid"])


class ApplianceTests(unittest.TestCase):
    def configured(self):
        appliances = deepcopy(DEFAULT_APPLIANCES)
        appliances[0].update(parent_id="forza1", device_id="aabbccddeeff", source="switch:0")
        return appliances

    def test_appliances_start_unassigned_without_fabricated_measurements(self):
        monitor = Monitor()
        ready(monitor)
        appliances = monitor.snapshot(now=104)["appliances"]
        self.assertEqual(len(appliances), 4)
        self.assertTrue(all(a["parent_id"] is None and a["power_w"] is None and a["energy_wh"] is None for a in appliances))

    def test_child_meter_is_not_added_to_total_or_alarm(self):
        monitor = Monitor()
        monitor.save_appliances(self.configured())
        for tick, marker in ((100, "2026-10-30 10:00:00"), (103, "2026-10-30 10:00:03")):
            data = inventory(marker)
            data["devices"].append({"id": "aabbccddeeff", "model_name": "Test PM", "model_code": "test", "online": True,
                                    "cloud_updated_raw": marker, "power_channels": [{"source": "switch:0", "channel": 0,
                                    "power_w": 5000, "energy_wh": 12000, "measurement_valid": True}]})
            monitor.ingest(data, now=tick)
        state = monitor.snapshot(now=104)
        self.assertEqual(state["total_w"], 2330)
        self.assertFalse(state["alarm"]["active"])
        self.assertEqual(state["appliances"][0]["power_w"], 5000)
        self.assertEqual(state["appliances"][0]["energy_wh"], 12000)

    def test_missing_child_does_not_disable_zone_monitoring(self):
        monitor = Monitor()
        monitor.save_appliances(self.configured())
        ready(monitor)
        state = monitor.snapshot(now=104)
        self.assertEqual(state["total_w"], 2330)
        self.assertIsNone(state["appliances"][0]["power_w"])

    def test_main_zone_meter_cannot_be_relabelled_as_appliance(self):
        appliances = self.configured()
        appliances[0].update(device_id="485519d9cf2f", source="emeters[0]")
        with self.assertRaises(ValueError): validate_appliances(appliances, DEFAULT_CONFIG["circuits"])

    def test_duplicate_appliance_meter_is_rejected(self):
        appliances = self.configured()
        appliances[1].update(parent_id="forza1", device_id="aabbccddeeff", source="switch:0")
        with self.assertRaises(ValueError): validate_appliances(appliances, DEFAULT_CONFIG["circuits"])

    def test_separate_ct_channels_can_measure_two_appliances(self):
        appliances = self.configured()
        appliances[0]["source"] = "em1:0"
        appliances[1].update(parent_id="forza1", device_id="aabbccddeeff", source="em1:1")
        self.assertEqual(len(validate_appliances(appliances, DEFAULT_CONFIG["circuits"])), 4)

    def test_associations_persist_without_changing_main_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            monitor = Monitor(path)
            monitor.save_appliances(self.configured())
            restored = Monitor(path)
            self.assertEqual(restored.appliances, self.configured())
            self.assertEqual(restored.config, DEFAULT_CONFIG)


if __name__ == "__main__":
    unittest.main()
