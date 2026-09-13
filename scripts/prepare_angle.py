#!/usr/bin/env python3
"""Apply SDK compatibility fixes to the pinned WebKit build copy, idempotently."""
from pathlib import Path
import sys


def prepare(root):
    replacements = {
        'Configurations/CommonBase.xcconfig': (
            '-D_LIBCPP_ENABLE_ASSERTIONS=1',
            '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE', 5),
        'Source/ThirdParty/ANGLE/src/common/bitset_utils.h': (
            'if (priv::kDefaultBitSetSize < 64)',
            'if constexpr (priv::kDefaultBitSetSize < 64)', 1),
    }
    pending = []
    for name, (old, new, count) in replacements.items():
        path = Path(root) / name
        source = path.read_text()
        if old not in source and source.count(new) == count:
            continue
        if source.count(old) != count or new in source:
            raise ValueError(f'Unexpected pinned ANGLE source: {name}')
        pending.append((path, source.replace(old, new)))
    for path, source in pending:
        path.write_text(source)


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
