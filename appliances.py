"""Optional appliance meters: subordinate details, never extra house totals."""

from copy import deepcopy
import re


DEFAULT_APPLIANCES = [
    {"id": "lavatrice", "label": "Lavatrice", "location": "Bagno piccolo", "parent_id": None, "device_id": None, "source": None},
    {"id": "asciugatrice", "label": "Asciugatrice", "location": "Bagno piccolo", "parent_id": None, "device_id": None, "source": None},
    {"id": "boiler_piccolo", "label": "Boiler", "location": "Bagno piccolo", "parent_id": None, "device_id": None, "source": None},
    {"id": "boiler_grande", "label": "Boiler", "location": "Bagno grande", "parent_id": None, "device_id": None, "source": None},
]


def validate_appliances(value, circuits):
    if not isinstance(value, list) or len(value) != len(DEFAULT_APPLIANCES):
        raise ValueError("Servono le quattro associazioni degli apparecchi.")
    ids, sources = set(), set()
    main_sources = {(c["device_id"], f"emeters[{c['channel']}]") for c in circuits}
    for appliance in value:
        if not isinstance(appliance, dict) or set(appliance) != set(DEFAULT_APPLIANCES[0]):
            raise ValueError("Associazione non valida.")
        if appliance["id"] not in {a["id"] for a in DEFAULT_APPLIANCES} or appliance["id"] in ids:
            raise ValueError("Apparecchio duplicato o non riconosciuto.")
        ids.add(appliance["id"])
        for key in ("label", "location"):
            if not isinstance(appliance[key], str) or not 1 <= len(appliance[key].strip()) <= 48:
                raise ValueError("Nome o posizione non validi.")
        if appliance["parent_id"] not in (None, "forza1", "forza2"):
            raise ValueError("Associare l'apparecchio a una tile Forza.")
        device, source = appliance["device_id"], appliance["source"]
        if device is None and source is None:
            continue
        if not appliance["parent_id"]:
            raise ValueError("Scegliere la zona Forza del misuratore.")
        if not isinstance(device, str) or not re.fullmatch(r"[0-9a-f]{12}", device):
            raise ValueError("ID del misuratore non valido.")
        if not isinstance(source, str) or not re.fullmatch(r"(?:emeters\[[0-9]+\]|switch:[0-9]+|em1:[0-9]+)", source):
            raise ValueError("Canale di misura non valido.")
        identity = (device, source)
        if identity in sources or identity in main_sources:
            raise ValueError("Usare un misuratore distinto per ogni apparecchio; i contatori di zona non sono misuratori del singolo carico.")
        sources.add(identity)
    return deepcopy(value)
