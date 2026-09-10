#!/usr/bin/env python3
import json
import argparse
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--only', help='Fetch only the named locked reference')
arguments = parser.parse_args()
for dependency in json.loads((ROOT / 'ThirdParty/dependencies.lock.json').read_text())['references']:
    if arguments.only and dependency['name'] != arguments.only:
        continue
    commit = dependency['commit']
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Dependency must be pinned to a full commit SHA')
    destination = ROOT / 'ThirdParty/checkouts' / dependency['name']
    destination.mkdir(parents=True, exist_ok=True)
    if not (destination / '.git').exists():
        subprocess.run(['git', 'init', str(destination)], check=True)
    def git(*args):
        return subprocess.check_output(['git', '-C', str(destination), *args], text=True).strip()
    if git('status', '--porcelain'):
        raise RuntimeError(f'Refusing to overwrite local reference changes: {destination}')
    git('fetch', '--depth', '1', dependency['url'], commit)
    git('checkout', '--detach', commit)
    if git('rev-parse', 'HEAD') != commit:
        raise RuntimeError('Reference revision mismatch')
