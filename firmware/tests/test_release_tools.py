import base64
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT=Path(__file__).resolve().parents[1]/'scripts/release.py'

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        spec=importlib.util.spec_from_file_location('release_tool',SCRIPT);self.tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.tool)
        self.tool.ROOT=self.root;self.tool.KEY=self.root/'.local/release-signing/private.pem';self.tool.PUB=self.root/'firmware/CasaMonitor/src/update_key.h';self.tool.META=self.root/'firmware/version.json'
        self.tool.PUB.parent.mkdir(parents=True)
        self.meta=dict(version='1.2.0',version_code=10200,board='waveshare-esp32-s3-touch-lcd-4.3c');self.tool.META.write_text(json.dumps(self.meta))
        self.pem=self.tool.public_key();self.binary=self.root/'app.bin'
        self.binary.write_bytes(b'\xe9'+b'CASA_RELEASE:waveshare-esp32-s3-touch-lcd-4.3c:10200:1.2.0\0'+self.pem.encode()+bytes(2048))
    def tearDown(self):self.tmp.cleanup()
    def call(self,*args):
        with patch.object(sys,'argv',[str(SCRIPT),'--binary',str(self.binary),*args]),contextlib.redirect_stdout(io.StringIO()):self.tool.main()
    def test_signed_package_and_independent_openssl_verification(self):
        self.call('--base-url','https://example.org/releases/1.2.0')
        dest=self.root/'.local/releases/1.2.0';envelope=json.loads((dest/'manifest.json').read_text());payload=base64.b64decode(envelope['payload']);info=json.loads(payload)
        self.assertEqual(info['url'],'https://example.org/releases/1.2.0/firmware.bin')
        self.assertEqual(info['sha256'],hashlib.sha256(self.binary.read_bytes()).hexdigest())
        data=self.root/'payload';data.write_bytes(payload);signature=self.root/'signature';signature.write_bytes(base64.b64decode(envelope['signature']))
        subprocess.run(['openssl','dgst','-sha256','-verify',str(dest/'public-key.pem'),'-signature',str(signature),str(data)],check=True,capture_output=True)
        self.assertNotIn('PRIVATE KEY',(dest/'public-key.pem').read_text())
        self.assertEqual(self.tool.KEY.stat().st_mode&0o777,0o600)
    def test_unsigned_staging_removes_older_manifest(self):
        self.call('--base-url','https://example.org/r');self.call()
        dest=self.root/'.local/releases/1.2.0';self.assertFalse((dest/'manifest.json').exists());self.assertTrue((dest/'release-metadata.json').exists())
    def test_rejects_stale_binary(self):
        self.tool.META.write_text(json.dumps(dict(self.meta,version_code=10300,version='1.3.0')))
        with self.assertRaises(SystemExit):self.call()
    def test_rejects_insecure_or_credential_url(self):
        for url in ['http://example.org/r','https://secret@example.org/r','https://example.org/r?token=secret']:
            with self.assertRaises(SystemExit):self.call('--base-url',url)
    def test_missing_private_key_never_replaces_trusted_key(self):
        before=self.tool.PUB.read_text();self.tool.KEY.unlink()
        with self.assertRaises(SystemExit):self.tool.public_key()
        self.assertEqual(self.tool.PUB.read_text(),before);self.assertFalse(self.tool.KEY.exists())
    def test_rejects_wrong_private_key(self):
        other=subprocess.run(['openssl','ecparam','-name','prime256v1','-genkey','-noout'],check=True,capture_output=True).stdout
        self.tool.KEY.write_bytes(other)
        with self.assertRaises(SystemExit):self.call()

if __name__=='__main__':unittest.main()
