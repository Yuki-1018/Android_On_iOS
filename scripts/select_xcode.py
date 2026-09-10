#!/usr/bin/env python3
"""Choose the newest installed stable Xcode with a major-26 iPhoneOS SDK."""
import os
import re
import subprocess
from pathlib import Path

candidates = []
for app in Path('/Applications').glob('Xcode*.app'):
    if 'beta' in app.name.lower() or 'preview' in app.name.lower():
        continue
    developer = app / 'Contents/Developer'
    environment = dict(os.environ, DEVELOPER_DIR=str(developer))
    try:
        sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-version'], env=environment, text=True).strip()
    except subprocess.CalledProcessError:
        continue
    if re.fullmatch(r'26(?:\.\d+)*', sdk):
        candidates.append((tuple(map(int, sdk.split('.'))), str(developer)))
if not candidates:
    raise SystemExit('No stable Xcode with an iPhoneOS 26 SDK installed on this runner')
print('DEVELOPER_DIR=' + max(candidates)[1])
