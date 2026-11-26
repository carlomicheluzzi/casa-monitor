import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

SCRIPT=Path(__file__).resolve().parents[1]/'scripts/provision.py'
spec=importlib.util.spec_from_file_location('provision_tool',SCRIPT)
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)

class UpdateProvisionTests(unittest.TestCase):
    def run_command(self,*args):
        with patch.object(sys,'argv',[str(SCRIPT),'--port','/dev/test',*args]),patch.object(tool,'payload',side_effect=AssertionError('Cloud credentials must not be loaded')),patch.object(tool,'exchange',return_value={}) as exchange,contextlib.redirect_stdout(io.StringIO()):
            tool.main();return exchange.call_args
    def test_update_url_only_sends_update_configuration(self):
        url='https://github.com/carlomicheluzzi/casa-monitor/releases/latest/download/manifest.json'
        call=self.run_command('--updates-url',url)
        self.assertEqual(call.args,('/dev/test',{'casa_updates_configure':1,'manifest_url':url},False))
    def test_update_check_only_sends_check_request(self):
        call=self.run_command('--check-updates')
        self.assertEqual(call.args,('/dev/test',{'casa_updates_check':1},False))
    def test_status_does_not_load_credentials(self):
        call=self.run_command('--status')
        self.assertEqual(call.args,('/dev/test',{'casa_status':1},True))
    def test_mixed_actions_are_rejected_before_usb_access(self):
        with patch.object(sys,'argv',[str(SCRIPT),'--port','/dev/test','--status','--check-updates']),patch.object(tool,'exchange') as exchange,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):tool.main()
        exchange.assert_not_called()

if __name__=='__main__':unittest.main()
