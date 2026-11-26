#!/usr/bin/env python3
"""Control the temporary display diagnostic over USB; no relay commands."""
import argparse
from pathlib import Path
from provision import exchange

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port')
group=parser.add_mutually_exclusive_group(required=True)
group.add_argument('--clock',type=int,choices=[16,25])
group.add_argument('--service',choices=['wifi','cloud','audio','refresh','wifi_off','reset'])
args=parser.parse_args()
ports=sorted(Path('/dev').glob('cu.usb*'))
port=args.port or (str(ports[0]) if len(ports)==1 else None)
if not port:parser.error('Collegare un solo display o specificare --port.')
request={'casa_diag_clock':args.clock} if args.clock else {'casa_diag_service':args.service}
exchange(port,request)
print('Comando diagnostico confermato; nessuna configurazione salvata modificata.')
