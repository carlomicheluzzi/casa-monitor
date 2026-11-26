#!/usr/bin/env python3
"""Send existing Shelly credentials directly to the ESP32 over USB; never print them."""
import argparse
from pathlib import Path
import sys
import os
import json
import secrets
import select
import termios
import time
import fcntl
import array

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from cloud_inventory import DEFAULT_ENV,DEFAULT_SERVER,load_key,fetch_inventory
from monitor import DEFAULT_CONFIG,validate_config

def payload(env,server):
    config_path=ROOT/'.local/config.json'
    config=validate_config(json.loads(config_path.read_text())) if config_path.exists() else DEFAULT_CONFIG
    items={c['id']:c for c in config['circuits']}
    sources=[]
    for id_ in ['luce1','luce2','forza1','forza2']:
        c=items[id_]
        sources.append({'name':c['zone']+' · '+c['label'],'device':c['device_id'],
                        'source':f"emeters[{c['channel']}]",'zone':0 if id_.endswith('1') else 1})
    for name in ['Boiler Carlo','Boiler Gloria','Lavatrice + asciugatrice']:
        sources.append({'name':name,'device':'','source':'','zone':-1})
    energy={k:config[k] for k in ['limit_w','warning_w','hysteresis_w','stale_after_s','mute_seconds','mapping_confirmed']}
    energy.update(audio_enabled=True,sources=sources)
    key=load_key(env)
    inventory=fetch_inventory(server,key)
    catalog=[]
    for device in inventory['devices']:
        for channel in device['power_channels']:
            catalog.append({'name':device.get('model_name') or device.get('model_code') or 'Shelly',
                            'device':device['id'],'source':channel['source']})
    return {'casa_provision':1,'server':server,'auth_key':key,'config':energy,'catalog':catalog,'unix_time':int(time.time())}

def exchange(port,request,status=False):
    nonce=secrets.token_hex(8);request['request_id']=nonce
    data=(json.dumps(request,ensure_ascii=False,separators=(',',':'))+'\n').encode()
    if len(data)>8192: raise ValueError('Configurazione troppo grande')
    fd=os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
    try:
        attrs=termios.tcgetattr(fd)
        attrs[0]=0;attrs[1]=0;attrs[2]=termios.CS8|termios.CREAD|termios.CLOCAL;attrs[3]=0
        attrs[4]=attrs[5]=termios.B115200;attrs[6][termios.VMIN]=0;attrs[6][termios.VTIME]=0
        termios.tcsetattr(fd,termios.TCSANOW,attrs)
        try:
            fcntl.ioctl(fd,termios.TIOCMBIC,array.array('i',[termios.TIOCM_RTS]))
            fcntl.ioctl(fd,termios.TIOCMBIS,array.array('i',[termios.TIOCM_DTR]))
        except OSError:
            pass
        termios.tcflush(fd,termios.TCIFLUSH)
        # Pace packets below the board's receive-buffer capacity. Never echo the request.
        for start in range(0,len(data),64):
            remaining=memoryview(data)[start:start+64]
            while remaining:
                if select.select([],[fd],[],2)[1]:remaining=remaining[os.write(fd,remaining):]
                else:raise TimeoutError()
            time.sleep(0.015)
        deadline=time.monotonic()+15;buffer=b''
        while time.monotonic()<deadline:
            if not select.select([fd],[],[],0.5)[0]:continue
            part=os.read(fd,4096)
            if not part:continue
            buffer+=part
            while b'\n' in buffer:
                line,buffer=buffer.split(b'\n',1);line=line.strip()
                if line==f'CASA_ACK {nonce} OK'.encode():return None
                if line.startswith(f'CASA_ACK {nonce} '.encode()):raise ValueError('Configurazione rifiutata')
                if status and line.startswith(b'CASA_STATUS '):
                    result=json.loads(line[len(b'CASA_STATUS '):])
                    if result.get('request_id')==nonce:return result
            if len(buffer)>16384:buffer=b''
        raise TimeoutError()
    finally:os.close(fd)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port');parser.add_argument('--env-file',type=Path,default=DEFAULT_ENV)
    parser.add_argument('--server',default=DEFAULT_SERVER);parser.add_argument('--status',action='store_true')
    args=parser.parse_args()
    ports=sorted(Path('/dev').glob('cu.usb*'))
    port=args.port or (str(ports[0]) if len(ports)==1 else None)
    if not port:sys.exit('Collegare un solo display USB oppure specificare --port.')
    try:
        request={'casa_status':1} if args.status else payload(args.env_file,args.server)
        response=exchange(port,request,args.status)
    except Exception:
        sys.exit('Operazione USB non riuscita. Verificare porta, firmware e configurazione; nessuna credenziale mostrata.')
    if args.status:print(json.dumps(response,ensure_ascii=False,indent=2))
    else:print('Configurazione Shelly salvata sul display. Chiave trasferita via USB, non mostrata né copiata nei sorgenti.')

if __name__=='__main__':main()
