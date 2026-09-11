#!/usr/bin/env python3
"""Specialize the pinned UTM cross-build machinery for only Android51's deps.

The generated script retains UTM's ISC header and uses its pinned sources and
patches. It does not build UTM, SPICE, Vulkan, Hypervisor, other QEMU targets or
fetch Android images. Keep the upstream checkout unchanged.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UTM = ROOT / 'ThirdParty/checkouts/UTM'
REVISION = 'b6f7475be54f9cb542c46b131319454b83489ced'

def generate():
    if subprocess.check_output(['git', '-C', str(UTM), 'rev-parse', 'HEAD'], text=True).strip() != REVISION:
        raise ValueError('Unexpected UTM build-input revision')
    script = (UTM / 'scripts/build_dependencies.sh').read_text()
    script = script.replace('IOS_SDKMINVER="14.0"', 'IOS_SDKMINVER="17.0"')
    script = script.replace('PATCHES_DIR="$BASEDIR/../patches"', 'PATCHES_DIR="${ANDROID51_UTM_ROOT:?}/patches"')
    script = script.replace('source "$PATCHES_DIR/sources"', 'source "$PATCHES_DIR/sources"\nICONV_SRC="${ICONV_SRC/http:/https:}"\nGETTEXT_SRC="${GETTEXT_SRC/http:/https:}"')
    overrides = r'''
# AndroidEmu specialization: all code above is pinned upstream UTM machinery.
check_env () {
    for tool in xcrun meson ninja make pkg-config python3 msgfmt glib-mkenums glib-compile-resources; do
        command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
    done
}
download_all () {
    mkdir -p "$BUILD_DIR"
    for src in "$PKG_CONFIG_SRC" "$FFI_SRC" "$ICONV_SRC" "$GETTEXT_SRC" "$GLIB_SRC" "$PIXMAN_SRC" "$SLIRP_SRC"; do download "$src"; done
    clone "$LIBUCONTEXT_REPO" "$LIBUCONTEXT_COMMIT"
}
build_qemu_dependencies () {
    build "$FFI_SRC"
    build "$ICONV_SRC"
    gl_cv_onwards_func_strchrnul=future build "$GETTEXT_SRC" --disable-java
    meson_build "$GLIB_SRC" -Dtests=false -Ddtrace=disabled -Dintrospection=disabled -Dlibmount=disabled -Dselinux=disabled
    build "$PIXMAN_SRC" --disable-gtk
    meson_darwin_build "$SLIRP_SRC"
    meson_build "$LIBUCONTEXT_REPO" -Ddefault_library=static -Dfreestanding=true
}
'''
    needle = '\ncheck_env\necho '
    if script.count(needle) != 1:
        raise ValueError('Pinned UTM script layout changed')
    script = script.replace(needle, overrides + needle)
    end = '\nbuild $QEMU_DIR --cross-prefix=""'
    if script.count(end) != 1:
        raise ValueError('Pinned UTM build tail changed')
    script = script.split(end)[0] + '\ntouch "$BUILD_DIR/BUILD_SUCCESS"\n'
    output = ROOT / 'build/ios-dependencies/build-minimal.sh'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(script)
    return output

if __name__ == '__main__':
    print(generate())
