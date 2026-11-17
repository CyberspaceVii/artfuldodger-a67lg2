"""Exercise the real publisher with isolated paths and simulated Android services."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'publish.sh'

class Publisher(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='artfuldodger-publisher-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.spool = self.root/'spool'; self.spool.mkdir()
        self.dest = self.root/'media/Pictures/Screenshots'
        self.name = 'cap_123_456_1.png'
        self.src = self.spool/self.name; self.src.write_bytes(b'captured-image')
        self.script = self.root/'publish.sh'
        self.script.write_text(SOURCE.read_text().replace('/data/system/artfuldodger_spool',str(self.spool)).replace('/data/adb/artfuldodger',str(self.root/'state')).replace('/system/bin/flock','flock').replace('/data/media/0',str(self.root/'media')).replace('/storage/emulated/0',str(self.root/'public')))
        self.bin = self.root/'bin'; self.bin.mkdir()
        tools = {'cmd':'echo "${TEST_USER:-0}"', 'getprop':'echo "${TEST_UNLOCKED:-true}"', 'cp':'[ "${TEST_COPY_FAIL:-0}" = 0 ] || exit 1; exec /bin/cp "$@"', 'chown':'exit 0', 'restorecon':'exit 0', 'content':'if [ "$1" = query ]; then if [ "${TEST_SCAN_FAIL:-0}" = 1 ]; then echo "No result found."; else echo "Row: 0 _id=42"; fi; else echo "Result: Bundle[{android.intent.extra.STREAM=Supplier{VAL_PARCELABLE@64+136}}]"; fi'}
        for name, body in tools.items():
            p=self.bin/name; p.write_text('#!/bin/sh\n'+body+'\n');p.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.bin)+':'+os.environ['PATH'])
    def run_publish(self, name=None, **env):
        return subprocess.run(['sh',str(self.script),name or self.name],env=dict(self.env,**env),capture_output=True)
    def test_disk_copy_failure(self):
        self.assertEqual(self.run_publish(TEST_COPY_FAIL='1').returncode,26)
        self.assertTrue(self.src.exists())
        self.assertFalse(list(self.dest.glob('Screenshot_*')))
    def test_success(self):
        self.assertEqual(self.run_publish().returncode,0)
        self.assertFalse(self.src.exists())
        self.assertEqual((self.dest/('Screenshot_'+self.name[4:])).read_bytes(),b'captured-image')
    def test_failed_index_retains_source_and_can_retry(self):
        self.assertEqual(self.run_publish(TEST_SCAN_FAIL='1').returncode,27)
        self.assertTrue(self.src.exists())
        self.assertEqual(self.run_publish().returncode,0)
    def test_other_user_refused(self):
        self.assertEqual(self.run_publish(TEST_USER='10').returncode,21)
        self.assertTrue(self.src.exists());self.assertFalse(self.dest.exists())
    def test_locked_storage_refused(self):
        self.assertEqual(self.run_publish(TEST_UNLOCKED='false').returncode,22)
        self.assertFalse(self.dest.exists())
    def test_path_injection_refused(self):
        self.assertNotEqual(self.run_publish('cap_../../bad.png').returncode,0)
    def test_source_symlink_refused(self):
        self.src.unlink(); self.src.symlink_to(self.root/'other')
        (self.root/'other').write_bytes(b'other')
        self.assertNotEqual(self.run_publish().returncode,0)
    def test_destination_conflict_not_overwritten(self):
        self.dest.mkdir(parents=True)
        out=self.dest/('Screenshot_'+self.name[4:]);out.write_bytes(b'existing')
        self.assertNotEqual(self.run_publish().returncode,0)
        self.assertEqual(out.read_bytes(),b'existing');self.assertTrue(self.src.exists())

if __name__ == '__main__': unittest.main()
