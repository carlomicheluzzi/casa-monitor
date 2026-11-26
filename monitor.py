"""Monitoring domain: four circuits, source freshness and a latched power alarm."""

from copy import deepcopy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import threading
import time
import urllib.error

from cloud_inventory import DEFAULT_ENV, DEFAULT_SERVER, fetch_inventory, load_key
from appliances import DEFAULT_APPLIANCES, validate_appliances


DEFAULT_CONFIG = {
    "limit_w": 4500,
    "warning_w": 4000,
    "hysteresis_w": 200,
    "stale_after_s": 90,
    "mute_seconds": 120,
    "mapping_confirmed": False,
    "circuits": [
        {"id": "luce1", "zone": "Zona 1", "label": "Luce", "kind": "light", "device_id": "485519d9cf2f", "channel": 1},
        {"id": "forza1", "zone": "Zona 1", "label": "Forza", "kind": "power", "device_id": "485519d9cf2f", "channel": 0},
        {"id": "luce2", "zone": "Zona 2", "label": "Luce", "kind": "light", "device_id": "485519d9502e", "channel": 1},
        {"id": "forza2", "zone": "Zona 2", "label": "Forza", "kind": "power", "device_id": "485519d9502e", "channel": 0},
    ],
}


def finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate_config(value):
    if not isinstance(value, dict) or set(value) != set(DEFAULT_CONFIG):
        raise ValueError("Configurazione incompleta o non riconosciuta.")
    checks = {
        "limit_w": (500, 30000), "warning_w": (100, 29999),
        "hysteresis_w": (10, 2000), "stale_after_s": (15, 300),
        "mute_seconds": (10, 900),
    }
    for name, (low, high) in checks.items():
        if not finite_number(value.get(name)) or not low <= value[name] <= high:
            raise ValueError(f"Valore non valido: {name}.")
    if value["warning_w"] >= value["limit_w"]:
        raise ValueError("Il preavviso deve essere inferiore alla soglia di allarme.")
    if value["hysteresis_w"] >= value["limit_w"]:
        raise ValueError("Il margine di rientro deve essere inferiore alla soglia.")
    if type(value["mapping_confirmed"]) is not bool:
        raise ValueError("Conferma dell'associazione non valida.")
    circuits = value.get("circuits")
    if not isinstance(circuits, list) or len(circuits) != 4:
        raise ValueError("Servono esattamente quattro circuiti.")
    sources, ids = set(), set()
    for circuit in circuits:
        if not isinstance(circuit, dict) or set(circuit) != set(DEFAULT_CONFIG["circuits"][0]):
            raise ValueError("Circuito non valido.")
        if circuit["id"] not in {"luce1", "forza1", "luce2", "forza2"} or circuit["id"] in ids:
            raise ValueError("Identificativo circuito duplicato o non valido.")
        ids.add(circuit["id"])
        for field in ("zone", "label"):
            if not isinstance(circuit[field], str) or not 1 <= len(circuit[field].strip()) <= 32:
                raise ValueError("I nomi devono contenere da 1 a 32 caratteri.")
        if circuit["kind"] not in ("light", "power"):
            raise ValueError("Tipo di circuito non valido.")
        device = circuit["device_id"]
        if not isinstance(device, str) or len(device) != 12 or any(c not in "0123456789abcdef" for c in device):
            raise ValueError("ID Shelly non valido: usare 12 cifre esadecimali minuscole.")
        if type(circuit["channel"]) is not int or circuit["channel"] not in (0, 1):
            raise ValueError("Canale non valido: scegliere 0 o 1.")
        source = (device, circuit["channel"])
        if source in sources:
            raise ValueError("Lo stesso canale non può essere contato due volte.")
        sources.add(source)
    return deepcopy(value)


class Alarm:
    """Never clear an alarm because telemetry has gone missing."""

    def __init__(self):
        self.active = False
        self.muted_until = 0.0

    def evaluate(self, total, config, now):
        if total is not None:
            if total > config["limit_w"] and not self.active:
                self.active = True
                self.muted_until = 0.0
            elif self.active and total <= config["limit_w"] - config["hysteresis_w"]:
                self.active = False
                self.muted_until = 0.0
        level = "alarm" if self.active else (
            "unavailable" if total is None else
            "warning" if total >= config["warning_w"] else "normal"
        )
        remaining = max(0, math.ceil(self.muted_until - now)) if self.active else 0
        return {"level": level, "active": self.active, "muted_seconds": remaining,
                "audible": self.active and remaining == 0}

    def mute(self, seconds, now):
        if self.active:
            self.muted_until = now + seconds


class Monitor:
    def __init__(self, config_path=None, env_file=DEFAULT_ENV, server=DEFAULT_SERVER):
        self.config_path = Path(config_path) if config_path else None
        self.config = deepcopy(DEFAULT_CONFIG)
        if self.config_path and self.config_path.exists():
            self.config = validate_config(json.loads(self.config_path.read_text()))
        self.appliances_path = self.config_path.with_name("appliances.json") if self.config_path else None
        self.appliances = deepcopy(DEFAULT_APPLIANCES)
        if self.appliances_path and self.appliances_path.exists():
            self.appliances = validate_appliances(json.loads(self.appliances_path.read_text()), self.config["circuits"])
        self.env_file, self.server = Path(env_file), server
        self.lock = threading.RLock()
        self.inventory = []
        self.trackers = {}
        self.last_success = None
        self.connection_error = None
        self.alarm, self.demo_alarm = Alarm(), Alarm()
        self.demo_scenario = "normal"
        self.histories = {"cloud": [], "demo": []}
        self.last_history = {"cloud": 0.0, "demo": 0.0}
        self.stop_event = threading.Event()

    def ingest(self, inventory, now=None):
        now = time.monotonic() if now is None else now
        with self.lock:
            self.inventory = deepcopy(inventory["devices"])
            self.last_success, self.connection_error = now, None
            for device in self.inventory:
                marker = device.get("cloud_updated_raw")
                if not marker:
                    continue
                marker = str(marker)
                tracker = self.trackers.get(device["id"])
                if tracker is None:
                    # A successful HTTP request may return an arbitrarily old cache.
                    # Require the cloud marker to advance before trusting freshness.
                    self.trackers[device["id"]] = {"marker": marker, "changed_at": now, "verified": False}
                elif marker != tracker["marker"]:
                    verified = marker > tracker["marker"]
                    tracker.update(marker=marker, changed_at=now, verified=verified)
            self.snapshot(now=now)

    def record_error(self, message):
        with self.lock:
            self.connection_error = message

    def poll(self):
        backoff = 3.0
        while not self.stop_event.is_set():
            try:
                inventory = fetch_inventory(self.server, load_key(self.env_file))
                self.ingest(inventory)
                backoff = 3.0
            except urllib.error.HTTPError as error:
                self.record_error("Troppe richieste al cloud. Riprovo tra poco." if error.code == 429
                                  else f"Shelly Cloud: errore HTTP {error.code}. Verifica chiave e server.")
                backoff = min(60, backoff * 2)
            except (OSError, ValueError, TypeError, KeyError, AttributeError):
                self.record_error("Cloud non raggiungibile o risposta non valida. Verifica rete e credenziali.")
                backoff = min(60, backoff * 2)
            self.stop_event.wait(backoff)

    def start(self):
        thread = threading.Thread(target=self.poll, name="shelly-cloud", daemon=True)
        thread.start()
        return thread

    def save_config(self, value):
        config = validate_config(value)
        with self.lock:
            validate_appliances(self.appliances, config["circuits"])
            if self.config_path:
                self.config_path.parent.mkdir(parents=True, exist_ok=True)
                temporary = self.config_path.with_suffix(".tmp")
                temporary.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
                temporary.replace(self.config_path)
            if config["circuits"] != self.config["circuits"]:
                self.histories = {"cloud": [], "demo": []}
            self.config = config

    def save_appliances(self, value):
        with self.lock:
            appliances = validate_appliances(value, self.config["circuits"])
            if self.appliances_path:
                self.appliances_path.parent.mkdir(parents=True, exist_ok=True)
                temporary = self.appliances_path.with_suffix(".tmp")
                temporary.write_text(json.dumps(appliances, ensure_ascii=False, indent=2) + "\n")
                temporary.replace(self.appliances_path)
            self.appliances = appliances

    def read_source(self, device_id, source, now):
        """Share validity rules between zone meters and optional appliance meters."""
        device = next((d for d in self.inventory if d["id"] == device_id), None)
        tracker = self.trackers.get(device_id)
        channel = next((c for c in device["power_channels"] if c["source"] == source), None) if device else None
        last_power = channel.get("power_w") if channel else None
        age = max(0, now - tracker["changed_at"]) if tracker else None
        if self.connection_error or self.last_success is not None and now - self.last_success > self.config["stale_after_s"]:
            reason = "connection"
        elif not device:
            reason = "waiting"
        elif device.get("online") not in (True, 1):
            reason = "offline"
        elif not channel or channel.get("measurement_valid") is not True or not finite_number(last_power):
            reason = "invalid"
        elif not tracker or not tracker["verified"]:
            reason = "verifying"
        elif age > self.config["stale_after_s"]:
            reason = "stale"
        else:
            reason = "ok"
        energy = channel.get("energy_wh") if channel else None
        voltage = channel.get("voltage_v") if channel else None
        return {
            "power_w": last_power if reason == "ok" else None,
            "last_power_w": last_power if finite_number(last_power) else None,
            "voltage_v": voltage if finite_number(voltage) else None,
            "energy_wh": energy if reason == "ok" and finite_number(energy) and energy >= 0 else None,
            "relay_on": channel.get("relay_on") if channel and reason == "ok" else None,
            "available": reason == "ok", "reason": reason,
            "age_s": round(age, 1) if age is not None else None,
        }

    def appliance_readings(self, mode, circuits, now):
        readings = []
        demo_sources = {"lavatrice": ("forza1", .32), "asciugatrice": ("forza1", .38),
                        "boiler_piccolo": ("forza1", .2), "boiler_grande": ("forza2", .9)}
        for appliance in self.appliances:
            item = deepcopy(appliance)
            if mode == "demo":
                parent_id, fraction = demo_sources[item["id"]]
                parent = next(c for c in circuits if c["id"] == parent_id)
                item.update(parent_id=parent_id, power_w=round(parent["power_w"] * fraction, 2) if parent["available"] else None,
                            energy_wh=None, available=parent["available"], reason=parent["reason"],
                            relay_on=None, age_s=0, simulated=True)
            elif appliance["device_id"] and appliance["source"]:
                item.update(self.read_source(appliance["device_id"], appliance["source"], now), simulated=False)
            else:
                item.update(power_w=None, energy_wh=None, available=False, reason="not_installed",
                            relay_on=None, age_s=None, simulated=False)
            readings.append(item)
        return readings

    def mute(self, mode, now=None):
        now = time.monotonic() if now is None else now
        with self.lock:
            self.snapshot(mode, now)
            (self.demo_alarm if mode == "demo" else self.alarm).mute(self.config["mute_seconds"], now)

    def set_scenario(self, scenario):
        if scenario not in ("normal", "warning", "overload", "offline"):
            raise ValueError("Scenario non valido.")
        with self.lock:
            self.demo_scenario = scenario

    def snapshot(self, mode="cloud", now=None):
        now = time.monotonic() if now is None else now
        with self.lock:
            config = deepcopy(self.config)
            circuits = []
            for index, circuit in enumerate(config["circuits"]):
                item = deepcopy(circuit)
                age, reason, last_power, voltage = None, "waiting", None, None
                if mode == "demo":
                    factor = {"normal": .28, "warning": config["warning_w"] / config["limit_w"] + .025,
                              "overload": 1.11, "offline": .28}[self.demo_scenario]
                    weights = [.065, .48, .085, .37]
                    last_power = round(config["limit_w"] * factor * weights[index] * (1 + .008 * math.sin(now + index)), 2)
                    voltage, age = 230.2, 0
                    reason = "offline" if self.demo_scenario == "offline" and index == 3 else "ok"
                else:
                    item.update(self.read_source(circuit["device_id"], f"emeters[{circuit['channel']}]", now))
                    circuits.append(item)
                    continue
                if not finite_number(last_power):
                    last_power = None
                if not finite_number(voltage):
                    voltage = None
                item.update(power_w=last_power if reason == "ok" else None,
                            last_power_w=last_power, voltage_v=voltage,
                            available=reason == "ok", reason=reason,
                            age_s=round(age, 1) if age is not None else None, energy_wh=None, relay_on=None)
                circuits.append(item)
            complete = all(c["available"] for c in circuits)
            total = round(sum(c["power_w"] for c in circuits), 2) if complete else None
            alarm = (self.demo_alarm if mode == "demo" else self.alarm).evaluate(total, config, now)
            if now - self.last_history[mode] >= 3:
                self.histories[mode].append({"time": datetime.now(timezone.utc).isoformat(), "total_w": total,
                                             "circuits": {c["id"]: c["power_w"] for c in circuits}})
                self.histories[mode] = self.histories[mode][-300:]
                self.last_history[mode] = now
            return {
                "mode": mode, "scenario": self.demo_scenario if mode == "demo" else None,
                "generated_at": datetime.now(timezone.utc).isoformat(),
                "poll_interval_s": 3, "config": config, "circuits": circuits,
                "total_w": total, "complete": complete,
                "remaining_w": round(config["limit_w"] - total, 2) if complete else None,
                "alarm": alarm, "history": deepcopy(self.histories[mode]),
                "appliance_config": deepcopy(self.appliances),
                "appliances": self.appliance_readings(mode, circuits, now),
                "devices": [{"id": d["id"], "model": d["model_name"] or d["model_code"],
                             "online": d.get("online"), "channels": [c["channel"] for c in d["power_channels"]
                             if c["source"].startswith("emeters[")],
                             "sources": [{"source": c["source"], "channel": c["channel"]} for c in d["power_channels"]]}
                            for d in self.inventory],
                "connection": {
                    "error": self.connection_error if mode == "cloud" else None,
                    "last_success_age_s": round(max(0, now - self.last_success), 1)
                    if self.last_success is not None and mode == "cloud" else None,
                },
            }
