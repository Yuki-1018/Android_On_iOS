#!/usr/bin/env python3
"""Apply SDK compatibility fixes to the pinned WebKit build copy, idempotently."""
from pathlib import Path
import sys
import runpy


def prepare(root):
    replacements = {
        # These are our own BSD-licensed ANGLE binaries, not Apple's SDK copy.
        # WebKit's client allowlist must not be inherited by the app build.
        'Source/ThirdParty/ANGLE/Configurations/ANGLE-dynamic.xcconfig': (
            'ANGLE_ALLOWABLE_CLIENTS_YES = -allowable_client WebCore -allowable_client WebCoreTestSupport;',
            'ANGLE_ALLOWABLE_CLIENTS_YES = ; // AndroidEmu: standalone ANGLE library', 1),
        'Configurations/CommonBase.xcconfig': (
            '-D_LIBCPP_ENABLE_ASSERTIONS=1',
            '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE', 5),
        'Source/ThirdParty/ANGLE/src/common/bitset_utils.h': (
            'if (priv::kDefaultBitSetSize < 64)',
            'if constexpr (priv::kDefaultBitSetSize < 64)', 1),
    }
    metal = runpy.run_path(str(Path(__file__).with_name('angle_metal_image.py')))['REPLACEMENTS']
    edits = [(name, old, new, count) for name, (old, new, count) in replacements.items()]
    edits += [(name, old, new, 1) for name, old, new in metal]
    pending = {}
    for name, old, new, count in edits:
        path = Path(root) / name
        source = pending.get(path, path.read_text())
        if source.count(new) == count:
            continue
        if source.count(old) != count:
            raise ValueError(f'Unexpected pinned ANGLE source: {name}')
        pending[path] = source.replace(old, new)
    for path, source in pending.items():
        path.write_text(source)


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
