import importlib.util
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('highmem', Path(__file__).resolve().parents[1] / 'scripts/build_highmem_kernel.py')
highmem = importlib.util.module_from_spec(spec)
spec.loader.exec_module(highmem)


class HighmemFetchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'upstream'
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        self.git('config', 'user.name', 'Test')
        self.git('config', 'user.email', 'test@example.invalid')
        (self.repo / 'source.c').write_text('pinned source\n')
        (self.repo / 'compiler').write_text('#!/bin/sh\nexit 0\n')
        (self.repo / 'compiler').chmod(0o755)
        (self.repo / 'alias').symlink_to('source.c')
        self.git('add', '.')
        self.git('commit', '-qm', 'pinned input')
        self.revision = self.git('rev-parse', 'HEAD').strip()
        self.archive = self.root / 'cache/source.tar.gz'

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], text=True)

    def fetch(self):
        return highmem.verified_archive(self.archive, str(self.repo), self.revision)

    def test_pinned_contents_modes_and_old_archive_replaced(self):
        self.archive.parent.mkdir()
        self.archive.write_bytes(b'old Gitiles response with a different checksum')
        # A moving upstream HEAD must not affect the pinned source selection.
        (self.repo / 'source.c').write_text('newer source\n')
        self.git('commit', '-qam', 'new head')
        self.fetch()
        with tarfile.open(self.archive) as archive:
            self.assertEqual(archive.extractfile('source.c').read(), b'pinned source\n')
            self.assertTrue(archive.getmember('compiler').mode & 0o111)
            self.assertTrue(archive.getmember('alias').issym())
        self.archive.write_bytes(b'corrupt local archive')
        self.fetch()
        with tarfile.open(self.archive) as archive:
            self.assertEqual(archive.extractfile('source.c').read(), b'pinned source\n')

    def test_rejects_non_pinned_revision(self):
        for revision in ('HEAD', 'main', self.revision[:12], '--all'):
            with self.assertRaises(ValueError):
                highmem.verified_archive(self.archive, str(self.repo), revision)

    def test_retries_transient_fetch_failure(self):
        real_run = subprocess.run
        attempts = []
        def run(args, **kwargs):
            if 'fetch' in args:
                attempts.append(args)
                if len(attempts) < 3:
                    raise subprocess.CalledProcessError(128, args)
            return real_run(args, **kwargs)
        with patch.object(highmem.subprocess, 'run', side_effect=run), patch.object(highmem.time, 'sleep'):
            self.fetch()
        self.assertEqual(len(attempts), 3)

    def test_corrupt_git_objects_fail_without_replacing_archive(self):
        self.fetch()
        original = self.archive.read_bytes()
        git_dir = self.archive.with_suffix('.git')
        # Normalize loose/packed storage across Git versions, then corrupt it.
        subprocess.run(['git', '-C', str(git_dir), 'repack', '-ad'], check=True)
        pack = next((git_dir / 'objects/pack').glob('*.pack'))
        data = bytearray(pack.read_bytes())
        data[len(data) // 2] ^= 0xff
        pack.chmod(0o644)
        pack.write_bytes(data)
        with patch.object(highmem.time, 'sleep'), self.assertRaises(subprocess.CalledProcessError):
            self.fetch()
        self.assertEqual(self.archive.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
