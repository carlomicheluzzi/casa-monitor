import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT=Path(__file__).resolve().parents[1]/'scripts/flash_usb.py'
spec=importlib.util.spec_from_file_location('flash_tool',SCRIPT)
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)

class FlashSettingsTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();root=Path(self.tmp.name)
        self.args=root/'flash_args';self.boot=root/'bootloader.bin'
        self.boot.write_bytes(bytes([0xe9,1,2,0x4f])+bytes(32))
        self.args.write_text('--flash-mode dio --flash-freq 80m --flash-size 16MB\n0x0 bootloader.bin\n')
    def tearDown(self):self.tmp.cleanup()
    def test_uses_generated_dio_parameters(self):
        self.assertEqual(tool.flash_settings(self.args,self.boot),['--flash-mode','dio','--flash-freq','80m','--flash-size','16MB'])
    def test_rejects_qio_override_of_dio_bootloader(self):
        self.args.write_text('--flash-mode qio --flash-freq 80m --flash-size 16MB\n')
        with self.assertRaises(RuntimeError):tool.flash_settings(self.args,self.boot)
    def test_rejects_unexpected_arguments(self):
        self.args.write_text('--flash-mode dio --flash-freq 80m --erase-all yes\n')
        with self.assertRaises(RuntimeError):tool.flash_settings(self.args,self.boot)
    def test_rejects_clock_or_size_mismatch(self):
        for args in ['--flash-mode dio --flash-freq 40m --flash-size 16MB','--flash-mode dio --flash-freq 80m --flash-size 8MB']:
            self.args.write_text(args+'\n')
            with self.assertRaises(RuntimeError):tool.flash_settings(self.args,self.boot)

if __name__=='__main__':unittest.main()
