"""Status must not mistake stale reports for a live capture service."""
import os,subprocess,tempfile,unittest
from pathlib import Path
SOURCE=Path(__file__).resolve().parents[1]/'control.sh'
class Control(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='artful-control-');self.addCleanup(self.tmp.cleanup);self.r=Path(self.tmp.name)
  self.state=self.r/'state';self.state.mkdir();self.spool=self.r/'spool';self.spool.mkdir();self.bin=self.r/'bin';self.bin.mkdir()
  (self.r/'boot').write_text('boot-current\n')
  for name,body in {'pgrep':'[ -n "${TEST_PIDS:-}" ] || exit 1; echo "$TEST_PIDS"','pkill':'exit 0'}.items():
   p=self.bin/name;p.write_text('#!/bin/sh\n'+body+'\n');p.chmod(0o755)
  self.script=self.r/'control.sh';self.script.write_text(SOURCE.read_text().replace('#!/system/bin/sh','#!/bin/sh').replace('/system/bin/sh','/bin/sh').replace('/system/bin/pgrep','pgrep').replace('/system/bin/pkill','pkill').replace('/data/adb/artfuldodger',str(self.state)).replace('/data/system/artfuldodger_spool',str(self.spool)).replace('/proc/sys/kernel/random/boot_id',str(self.r/'boot')));self.script.chmod(0o755)
  self.env=dict(os.environ,PATH=str(self.bin)+':'+os.environ['PATH'])
 def report(self,pid='1234',boot='boot-current'):
  (self.state/'runtime').write_text(f'pid={pid}\nboot_id={boot}\nstate=ready\ninput_grabbed=1\nrequests=3\nsaved=2\nfailed=1\nlast_result=Image retained: gallery indexing failed\ncapture_ms=44\nsave_ms=66\n')
 def runcontrol(self,cmd='status',**env):return subprocess.run(['sh',str(self.script),cmd],env=dict(self.env,**env),capture_output=True,text=True,check=True).stdout
 def test_ready(self):
  self.report();out=self.runcontrol(TEST_PIDS='1234');self.assertIn('Readiness: ready',out);self.assertIn('2 saved, 1 failed',out)
 def test_stale_pid(self):
  self.report();self.assertIn('previous session report ignored',self.runcontrol(TEST_PIDS='5678'))
 def test_stale_boot(self):
  self.report(boot='old-boot');self.assertIn('previous session report ignored',self.runcontrol(TEST_PIDS='1234'))
 def test_absent_process(self):
  self.report();self.assertIn('waiting for supervisor',self.runcontrol())
 def test_off_on(self):
  self.assertIn('Capture: disabled',self.runcontrol('off'));self.assertTrue((self.state/'disabled').exists())
  self.assertIn('Capture: enabled',self.runcontrol('on'));self.assertFalse((self.state/'disabled').exists())
 def test_pending_ignores_symlink(self):
  (self.spool/'cap_1_2_3.png').write_bytes(b'own')
  (self.spool/'cap_4_5_6.png').symlink_to(self.spool/'cap_1_2_3.png')
  self.assertIn('Pending images: 1',self.runcontrol())
if __name__=='__main__':unittest.main()
