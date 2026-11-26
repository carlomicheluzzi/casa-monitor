#!/usr/bin/env python3
"""Prepare signed OTA releases. Private key stays outside the repository and firmware."""
import argparse, base64, hashlib, json, os, re, subprocess, tempfile
from pathlib import Path
from urllib.parse import urlsplit
ROOT=Path(__file__).resolve().parents[2]
KEY=ROOT/'.local/release-signing/private.pem'
PUB=ROOT/'firmware/CasaMonitor/src/update_key.h'
META=ROOT/'firmware/version.json'

def run(*args):
    return subprocess.run([str(x) for x in args],check=True,capture_output=True).stdout

def public_key():
    if not KEY.exists() and PUB.exists():raise SystemExit("Chiave privata assente: ripristinare il backup originale prima di firmare nuovi rilasci.")
    if not KEY.exists():
        KEY.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
        fd=os.open(KEY,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
        try: os.write(fd,run('openssl','ecparam','-name','prime256v1','-genkey','-noout'))
        finally: os.close(fd)
    KEY.chmod(0o600)
    pem=run('openssl','pkey','-in',KEY,'-pubout').decode()
    header='#pragma once\nnamespace casa {\nconstexpr char UPDATE_PUBLIC_KEY[]=R"KEY('+pem+')KEY";\n}\n'
    if PUB.exists() and PUB.read_text()!=header:
        raise SystemExit('Chiave differente da quella incorporata. Non sostituirla: ripristinare la chiave privata originale.')
    PUB.write_text(header)
    return pem

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--init-key',action='store_true')
    p.add_argument('--base-url',help='HTTPS directory containing firmware.bin and manifest.json')
    p.add_argument('--notes',default='Standby display, Shelly in rete locale e aggiornamenti firmati.')
    p.add_argument('--binary',type=Path,default=ROOT/'.local/firmware-output/CasaMonitor.ino.bin')
    a=p.parse_args();pem=public_key()
    if a.init_key: print('Chiave di firma pronta. Conservare un backup privato di .local/release-signing/.');return
    meta=json.loads(META.read_text());data=a.binary.read_bytes()
    if len(data)<1024 or len(data)>0x300000 or data[0]!=0xe9:raise SystemExit('Firmware ESP32 non valido o troppo grande.')
    marker=f'CASA_RELEASE:{meta["board"]}:{meta["version_code"]}:{meta["version"]}'.encode()
    if marker+b'\0' not in data or pem.encode() not in data:raise SystemExit('La build non corrisponde alla versione o alla chiave di firma. Ricompilare prima del rilascio.')
    if len(a.notes.encode())>500 or any(ord(c)<32 or ord(c)==127 for c in a.notes):raise SystemExit('Note: massimo 500 byte, una sola riga.')
    dest=ROOT/'.local/releases'/meta['version'];dest.mkdir(parents=True,exist_ok=True)
    (dest/'firmware.bin').write_bytes(data)
    digest=hashlib.sha256(data).hexdigest()
    payload=dict(schema=1,board=meta['board'],version=meta['version'],version_code=meta['version_code'],size=len(data),sha256=digest,notes=a.notes)
    (dest/'SHA256SUMS').write_text(f'{digest}  firmware.bin\n')
    (dest/'public-key.pem').write_text(pem)
    if a.base_url:
        u=urlsplit(a.base_url)
        if u.scheme!='https' or not u.hostname or u.username or u.password or u.port or u.query or u.fragment or re.search(r'[\s\\]',a.base_url):raise SystemExit('Specificare una directory HTTPS valida.')
        payload['url']=a.base_url.rstrip('/')+'/firmware.bin'
        raw=json.dumps(payload,separators=(',',':'),ensure_ascii=False).encode()
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'payload';path.write_bytes(raw)
            signature=run('openssl','dgst','-sha256','-sign',KEY,path)
            sig=Path(tmp)/'signature';sig.write_bytes(signature)
            pub=Path(tmp)/'public.pem';pub.write_text(pem)
            run('openssl','dgst','-sha256','-verify',pub,'-signature',sig,path)
        (dest/'manifest.json').write_text(json.dumps(dict(payload=base64.b64encode(raw).decode(),signature=base64.b64encode(signature).decode()),separators=(',',':'))+'\n')
        print('Manifest firmato e verificato:',dest/'manifest.json')
        print('Indirizzo da impostare sul display:',a.base_url.rstrip('/')+'/manifest.json')
    else:
        (dest/'release-metadata.json').write_text(json.dumps(payload,indent=2)+'\n')
        # A previous signed manifest must never survive packaging for another binary.
        (dest/'manifest.json').unlink(missing_ok=True)
        print('Firmware pronto. Per firmare il manifest pubblico ripetere con --base-url HTTPS_DIRECTORY.')
    print('Pacchetto:',dest)
if __name__=='__main__':main()
