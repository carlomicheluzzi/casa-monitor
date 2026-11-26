"""Read Shelly Cloud inventory without changing devices or exposing credentials."""

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request


DEFAULT_ENV = Path.home() / "Documents/workspace-python/ShellyMonitor/.env"
DEFAULT_SERVER = "https://shelly-52-eu.shelly.cloud"
MODELS = {"SHEM": "Shelly EM", "S3SN-0U12A": "Shelly H&T Gen3",
          "S3EM-002CXCEU": "Shelly EM Gen3", "S4SW-001P16EU": "Shelly 1PM Gen4"}


class NoRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def load_key(path):
    for line in path.read_text().splitlines():
        match = re.match(r"^\s*(?:export\s+)?SHELLY_AUTH_KEY\s*=\s*(.*)$", line)
        if match:
            value = match[1].strip()
            if value[:1] in ("'", '"'):
                value = value[1:].split(value[0], 1)[0]
            else:
                value = value.split(" #", 1)[0].strip()
            if value:
                return value
    raise ValueError("SHELLY_AUTH_KEY assente o vuota nel file indicato")


def fetch_inventory(server, key):
    url = urllib.parse.urlsplit(server)
    if (url.scheme != "https" or not url.hostname
            or not url.hostname.endswith(".shelly.cloud")
            or url.username or url.password or url.query or url.fragment
            or url.path not in ("", "/") or url.port not in (None, 443)):
        raise ValueError("Il server deve essere un host HTTPS di Shelly Cloud")
    request = urllib.request.Request(
        server.rstrip("/") + "/device/all_status",
        data=urllib.parse.urlencode({"auth_key": key, "show_info": "true"}).encode(),
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    with urllib.request.build_opener(NoRedirects).open(request, timeout=25) as response:
        payload = json.load(response)
    if payload.get("isok") is not True:
        raise ValueError("Shelly Cloud ha rifiutato la richiesta; verificare chiave e server")
    statuses = payload.get("data", {}).get("devices_status")
    if not isinstance(statuses, dict):
        raise ValueError("Risposta Shelly Cloud priva dell'elenco dispositivi atteso")
    return normalize_inventory(statuses, server)


def numeric(value):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) else None


def normalize_inventory(statuses, server=DEFAULT_SERVER):
    """Normalize only measured power/energy; counters are Wh, never estimates."""
    devices = []
    for device_id, status in statuses.items():
        info = status.get("_dev_info", {})
        device_id = str(info.get("id", device_id)).lower()
        code = info.get("code", status.get("code"))
        channels = []
        for index, meter in enumerate(status.get("emeters", [])):
            channels.append({
                "source": f"emeters[{index}]", "channel": index,
                "power_w": meter.get("power"), "voltage_v": meter.get("voltage"),
                "measurement_valid": meter.get("is_valid"),
                "energy_wh": numeric(meter.get("total")),
                "relay_on": None,
                "display_label": None,
            })
        for component, value in status.items():
            if component.startswith("switch:") and isinstance(value, dict):
                if "apower" in value:
                    channels.append({
                        "source": component, "channel": value.get("id"),
                        "power_w": value.get("apower"), "voltage_v": value.get("voltage"),
                        "measurement_valid": numeric(value.get("apower")) is not None and not value.get("errors"),
                        "energy_wh": numeric(value.get("aenergy", {}).get("total")),
                        "relay_on": value.get("output") if type(value.get("output")) is bool else None,
                        "display_label": None,
                    })
            elif component.startswith("em1:") and isinstance(value, dict):
                index = value.get("id")
                energy = status.get(f"em1data:{index}", {})
                channels.append({
                    "source": component, "channel": index,
                    "power_w": value.get("act_power"), "voltage_v": value.get("voltage"),
                    "measurement_valid": numeric(value.get("act_power")) is not None and not value.get("errors"),
                    "energy_wh": numeric(energy.get("total_act_energy")) if not energy.get("errors") else None,
                    "relay_on": None, "display_label": None,
                })
        devices.append({
            "id": device_id, "model_code": code,
            "model_name": MODELS.get(code),
            "cloud_generation": info.get("gen"),
            "online": info.get("online"),
            "cloud_updated_raw": status.get("_updated"),
            "power_channels": channels,
        })
    return {
        "observed_at_utc": datetime.now(timezone.utc).isoformat(),
        "server": server, "source": "Shelly Cloud last-known status",
        "notes": [
            "Cloud timestamps are retained as supplied; timezone is not assumed.",
            "An online flag does not guarantee a fresh measurement.",
            "Display labels and circuit coverage still require confirmation.",
            "cloud_generation is an API family, not necessarily the marketed generation.",
        ],
        "devices": devices,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV)
    parser.add_argument("--server", default=DEFAULT_SERVER)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        inventory = fetch_inventory(args.server, load_key(args.env_file))
    except urllib.error.HTTPError as error:
        raise SystemExit(f"Shelly Cloud: errore HTTP {error.code}") from None
    except urllib.error.URLError:
        raise SystemExit("Shelly Cloud non raggiungibile: verificare rete e server") from None
    except (OSError, ValueError):
        raise SystemExit("Impossibile leggere inventario: verificare file credenziali, server e risposta API") from None
    result = json.dumps(inventory, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
