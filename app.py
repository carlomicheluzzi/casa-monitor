"""Local Shelly dashboard. Python standard library only; serves loopback only."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import urllib.parse

from cloud_inventory import DEFAULT_ENV, DEFAULT_SERVER
from monitor import Monitor


ROOT = Path(__file__).resolve().parent
STATIC = {"/": ("index.html", "text/html; charset=utf-8"),
          "/app.js": ("app.js", "text/javascript; charset=utf-8"),
          "/style.css": ("style.css", "text/css; charset=utf-8")}


def make_handler(monitor, token, port, default_mode):
    class Handler(BaseHTTPRequestHandler):
        server_version = "CasaMonitor/1.0"

        def log_message(self, fmt, *args):
            pass  # No request URLs or user data in the logs.

        def reply(self, status, body, content_type="application/json; charset=utf-8"):
            if not isinstance(body, bytes):
                body = json.dumps(body, ensure_ascii=False, allow_nan=False).encode()
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def allowed_host(self):
            return self.headers.get("Host") in (f"127.0.0.1:{port}", f"localhost:{port}")

        def do_GET(self):
            if not self.allowed_host():
                return self.reply(403, {"error": "Host non consentito."})
            url = urllib.parse.urlsplit(self.path)
            if url.path == "/api/state":
                mode = urllib.parse.parse_qs(url.query).get("mode", [default_mode])[0]
                if mode not in ("cloud", "demo"):
                    return self.reply(400, {"error": "Modalità non valida."})
                state = monitor.snapshot(mode)
                state["session_token"] = token
                state["default_mode"] = default_mode
                return self.reply(200, state)
            if url.path in STATIC:
                filename, content_type = STATIC[url.path]
                return self.reply(200, (ROOT / "web" / filename).read_bytes(), content_type)
            if url.path == "/favicon.ico":
                return self.reply(204, b"")
            self.reply(404, {"error": "Risorsa non trovata."})

        def do_POST(self):
            origin = self.headers.get("Origin")
            if (not self.allowed_host() or
                    (origin and origin != "http://" + self.headers.get("Host", "")) or
                    not secrets.compare_digest(self.headers.get("X-Monitor-Token", ""), token)):
                return self.reply(403, {"error": "Sessione non valida. Ricarica la pagina."})
            if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                return self.reply(415, {"error": "È richiesto JSON."})
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 1 <= length <= 16384:
                    raise ValueError("Richiesta troppo grande o vuota.")
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict):
                    raise ValueError("Richiesta non valida.")
                path = urllib.parse.urlsplit(self.path).path
                if path == "/api/config":
                    monitor.save_config(data)
                elif path == "/api/appliances":
                    monitor.save_appliances(data.get("appliances"))
                elif path == "/api/mute":
                    mode = data.get("mode", default_mode)
                    if mode not in ("cloud", "demo"):
                        raise ValueError("Modalità non valida.")
                    monitor.mute(mode)
                elif path == "/api/demo":
                    monitor.set_scenario(data.get("scenario"))
                else:
                    return self.reply(404, {"error": "Risorsa non trovata."})
            except (ValueError, TypeError, UnicodeError):
                return self.reply(400, {"error": "Controlla i valori e scegli canali distinti. I misuratori dei singoli apparecchi devono essere diversi dai contatori di zona."})
            except OSError:
                return self.reply(500, {"error": "Impossibile salvare le impostazioni sul disco."})
            self.reply(200, {"ok": True})

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV)
    parser.add_argument("--server", default=DEFAULT_SERVER)
    parser.add_argument("--demo", action="store_true", help="Non contattare il cloud; avvia solo la simulazione")
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("La porta deve essere compresa tra 1024 e 65535")
    try:
        monitor = Monitor(ROOT / ".local/config.json", args.env_file, args.server)
    except (OSError, ValueError):
        parser.error("Configurazione .local/config.json non valida. Correggila o rinominala per ripartire dai valori iniziali.")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(
        monitor, secrets.token_urlsafe(32), args.port, "demo" if args.demo else "cloud"))
    if not args.demo:
        monitor.start()
    print(f"Monitor casa: http://127.0.0.1:{args.port}", flush=True)
    print("Modalità: " + ("simulazione" if args.demo else "Shelly Cloud · sola lettura"), flush=True)
    try:
        server.serve_forever(poll_interval=.5)
    except KeyboardInterrupt:
        print("\nMonitor arrestato.", flush=True)
    finally:
        monitor.stop_event.set()
        server.server_close()


if __name__ == "__main__":
    main()
