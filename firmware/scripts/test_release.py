#!/usr/bin/env python3
"""Produce disposable signing fixtures, never using the device's release key."""
import base64,json,subprocess
from pathlib import Path

def fixtures(dest):
    def run(*args):return subprocess.run([str(x) for x in args],check=True,capture_output=True).stdout
    key=dest/'test-private.pem'
    key.write_bytes(run('openssl','ecparam','-name','prime256v1','-genkey','-noout'));key.chmod(0o600)
    (dest/'test-public.pem').write_bytes(run('openssl','pkey','-in',key,'-pubout'))
    other=dest/'other-private.pem';other.write_bytes(run('openssl','ecparam','-name','prime256v1','-genkey','-noout'));other.chmod(0o600)
    (dest/'other-public.pem').write_bytes(run('openssl','pkey','-in',other,'-pubout'))
    payload=dict(schema=1,board='waveshare-esp32-s3-touch-lcd-4.3c',version='1.2.0',version_code=10200,size=2048,url='https://example.org/releases/firmware.bin',sha256='ab'*32,notes='Test firmware')
    raw=json.dumps(payload,separators=(',',':')).encode();(dest/'payload.json').write_bytes(raw)
    sig=run('openssl','dgst','-sha256','-sign',key,dest/'payload.json')
    (dest/'signed.json').write_text(json.dumps(dict(payload=base64.b64encode(raw).decode(),signature=base64.b64encode(sig).decode()),separators=(',',':')))
    key.unlink();other.unlink()
