"""Prepare files for the embedded Python runtime."""

import re
import runpy
import shutil
import sys
from pathlib import Path


def relocate_sysconfig(config, original):
    variables = runpy.run_path(str(config))["build_time_vars"]
    store_path = re.compile(r"/nix/store/[a-z0-9]{32}-")
    rows = ["import sys", "", "build_time_vars = {"]
    for name, value in variables.items():
        if isinstance(value, str) and value.startswith(f"{original}/"):
            suffix = value[len(str(original)):]
            if store_path.search(suffix):
                continue
            expression = f"sys.base_prefix + {suffix!r}"
        elif name in ("prefix", "exec_prefix") and value == str(original):
            expression = "sys.base_prefix"
        elif name == "TZPATH":
            expression = "sys.base_prefix + '/share/zoneinfo'"
        elif isinstance(value, str) and store_path.search(value):
            # Build flags and tools from the original CPython derivation.
            continue
        else:
            expression = repr(value)
        rows.append(f"    {name!r}: {expression},")
    config.write_text("\n".join([*rows, "}", ""]), encoding="utf-8")


def prepare_stdlib(stdlib, original):
    for config in stdlib.glob("_sysconfigdata_*.py"):
        relocate_sysconfig(config, original)

    subprocess = stdlib / "subprocess.py"
    source = subprocess.read_text(encoding="utf-8")
    subprocess.write_text(
        re.sub(r"/nix/store/[a-z0-9]{32}-bash-[^/'\s]+/bin/sh", "/bin/sh", source),
        encoding="utf-8",
    )

    mimetypes = stdlib / "mimetypes.py"
    source, count = re.subn(
        r'"/nix/store/[a-z0-9]{32}-mailcap-[^/"]+/etc/mime.types"',
        'os.path.join(sys.base_prefix, "share", "mime.types")',
        mimetypes.read_text(encoding="utf-8"),
    )
    if count:
        mimetypes.write_text(
            source.replace("knownfiles = [", "import os\nimport sys\n\nknownfiles = [", 1),
            encoding="utf-8",
        )

    ssl = stdlib / "ssl.py"
    source = ssl.read_text(encoding="utf-8")
    # OpenSSL's Nix default trust path can be absent on a packed app's Mac.
    source = source.replace(
        "    parts = _ssl.get_default_verify_paths()\n",
        '    parts = _ssl.get_default_verify_paths()\n'
        '    if not os.path.isfile(parts[1]):\n'
        '        parts = (parts[0], os.path.join(sys.base_prefix, "share", "ca-bundle.crt"), *parts[2:])\n',
        1,
    )
    ssl.write_text(
        source.replace(
            "        self.set_default_verify_paths()\n",
            '        self.set_default_verify_paths()\n'
            '        paths = get_default_verify_paths()\n'
            '        bundle = os.path.join(sys.base_prefix, "share", "ca-bundle.crt")\n'
            '        if paths.cafile == bundle:\n'
            '            self.load_verify_locations(cafile=bundle)\n',
            1,
        ),
        encoding="utf-8",
    )


def prepare_site_packages(site_packages, original):
    # NumPy's internal CPU tests embed build-time tool paths.
    numpy_tests = site_packages / "numpy" / "_core" / "tests"
    if numpy_tests.is_dir():
        shutil.rmtree(numpy_tests)

    for metadata in site_packages.rglob("pkgconfig/*.pc"):
        metadata.unlink()

    # Importable scripts still work after their original interpreter is removed.
    shebang = b"#!" + bytes(original / "bin") + b"/"
    for script in site_packages.rglob("*.py"):
        with script.open("rb") as stream:
            header = stream.readline()
            if not header.startswith(shebang):
                continue
            body = stream.read()
        script.write_bytes(b"#!/usr/bin/env -S " + header[len(shebang):] + body)


def relocate_ffi(library, original):
    # Nix's Darwin libffi dlopens its trampolines using this C string.
    path = bytes(original / "libffi-trampolines.dylib") + b"\0"
    data = library.read_bytes()
    relative = b"@loader_path/libffi-trampolines.dylib\0"
    if len(relative) > len(path) or data.count(path) != 1:
        raise ValueError(f"Cannot relocate the libffi trampolines in {library}")
    library.write_bytes(data.replace(path, relative.ljust(len(path), b"\0")))


if __name__ == "__main__":
    operation, root, original = sys.argv[1:]
    prepare = {
        "stdlib": prepare_stdlib,
        "site-packages": prepare_site_packages,
        "ffi": relocate_ffi,
    }
    prepare[operation](Path(root), Path(original))
