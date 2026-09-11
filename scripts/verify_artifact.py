#!/usr/bin/env python3
"""Inspect an IPA without extracting untrusted archive paths."""
import plistlib
import json
import stat
import struct
import sys
import zipfile
from pathlib import PurePosixPath

def verify(path):
    with zipfile.ZipFile(path) as archive:
        seen = set()
        entries = archive.infolist()
        if len(entries) > 20000 or sum(e.file_size for e in entries) > 1024**3:
            raise ValueError('Artifact exceeds application-only size limits')
        for entry in entries:
            name = entry.filename
            parts = PurePosixPath(name).parts
            if name in seen or not parts or parts[0] != 'Payload' or '..' in parts or '\\' in name:
                raise ValueError(f'Unsafe or duplicate ZIP entry: {name}')
            seen.add(name)
            mode = entry.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValueError(f'Symlink is not permitted: {name}')
            if name.lower().endswith(('.img', '.apk', '.mobileprovision', '.p12', '.pem')) or parts[-1] in ('kernel', 'kernel-qemu', 'ramdisk', '_CodeSignature'):
                raise ValueError(f'Forbidden guest image, APK, or signing material: {name}')
            if '_CodeSignature' in parts:
                raise ValueError('Signed app found in unsigned artifact')
        prefix = 'Payload/AndroidEmu.app/'
        template_name = prefix + 'cache-template.sparse'
        if template_name not in seen:
            raise ValueError('Missing empty cache filesystem template')
        if not 40 <= archive.getinfo(template_name).file_size <= 4 * 1024 * 1024:
            raise ValueError('Unexpected cache template size')
        with archive.open(template_name) as template:
            magic, major, minor, header, chunk, block, blocks, chunks, crc = struct.unpack('<I4H4I', template.read(28))
        if (magic, major, minor, header, chunk, block, blocks) != (0xed26ff3a, 1, 0, 28, 12, 4096, 16384) or chunks == 0:
            raise ValueError('Invalid empty cache template header')
        info_entry = archive.getinfo(prefix + 'Info.plist')
        if info_entry.file_size > 65536:
            raise ValueError('Oversized app plist')
        info = plistlib.loads(archive.read(info_entry))
        if info.get('MinimumOSVersion', '').split('.')[0] != '26':
            raise ValueError('Expected iOS 26 deployment target')
        if info.get('CFBundleExecutable') != 'AndroidEmu':
            raise ValueError('Unexpected executable')
        if 'stikdebug' not in info.get('LSApplicationQueriesSchemes', []):
            raise ValueError('Missing StikDebug scheme')
        if prefix + 'AndroidEmu' not in seen:
            raise ValueError('Missing executable')
        manifest_name = prefix + 'EngineLicenses/engine-manifest.json'
        if manifest_name not in seen or archive.getinfo(manifest_name).file_size > 65536:
            raise ValueError('Missing engine dependency manifest')
        frameworks = json.loads(archive.read(manifest_name))['frameworks']
        if not isinstance(frameworks, list) or 'AndroidQEMU' not in frameworks or len(frameworks) > 128:
            raise ValueError('Missing AndroidQEMU engine')
        for framework in frameworks:
            if not isinstance(framework, str) or not framework.replace('_', '').isalnum():
                raise ValueError('Invalid engine framework name')
            name = prefix + f'Frameworks/{framework}.framework/{framework}'
            with archive.open(name) as engine:
                engine_header = engine.read(32)
            if len(engine_header) != 32:
                raise ValueError('Truncated engine framework')
            magic, cpu, _, kind, _, _, _, _ = struct.unpack('<8I', engine_header)
            if magic != 0xfeedfacf or cpu != 0x100000c or kind != 6:
                raise ValueError('Expected an arm64 Mach-O dynamic engine library')
        with archive.open(prefix + 'AndroidEmu') as binary:
            header = binary.read(32)
        if len(header) != 32:
            raise ValueError('Truncated Mach-O executable')
        magic, cpu, _, file_type, _, _, _, _ = struct.unpack('<8I', header)
        if magic != 0xfeedfacf or cpu != 0x100000c or file_type != 2:
            raise ValueError('Expected a thin arm64 Mach-O executable')
    print('Verified unsigned IPA with arm64 engine framework closure')

if __name__ == '__main__':
    verify(sys.argv[1])
