#!/usr/bin/env python3
"""Create build-only pkg-config metadata for independent staging roots.

Usage: mesa3d-pkgconfig.py OUTPUT [relocate|native ROOT]...
Earlier roots take precedence. Source metadata is never modified.
"""

import pathlib
import re
import sys


def relocate(text, root, pc_dir):
    # Metadata may describe prefix=/, prefix=/usr, or prefix= (zlib).
    # Relocate each file independently rather than applying one global sysroot.
    def path(value):
        if value == root or value.startswith(root + "/"):
            return value
        return root + value if value.startswith("/") else value

    lines = []
    for line in text.splitlines(keepends=True):
        line = line.replace("${pcfiledir}", str(pc_dir))
        line = line.replace("${pc_sysrootdir}", root)
        variable = re.match(r"^(\s*[\w.]+\s*=)([^\n]*)(\n?)$", line)
        if variable:
            key, value, newline = variable.groups()
            if key.strip().rstrip("=").strip() == "prefix" and not value.strip():
                value = root
            else:
                value = re.sub(r"^(\s*)(/\S*)", lambda m: m[1] + path(m[2]), value)
            line = key + value + newline
        if variable or re.match(r"^(?:Cflags|Libs(?:\.private)?):", line):
            # Also handle literal search paths that do not use ${prefix}.
            line = re.sub(
                r"(-I|-L|-isystem\s+|-Wl,-rpath(?:-link)?,)(/[^\s]+)",
                lambda m: m[1] + path(m[2]), line,
            )
        lines.append(line)
    return "".join(lines)


def main():
    output = pathlib.Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    packages = {}
    args = sys.argv[2:]
    for mode, directory in zip(args[::2], args[1::2]):
        root = pathlib.Path(directory)
        for pattern in ("lib/pkgconfig/*.pc", "lib64/pkgconfig/*.pc",
                        "share/pkgconfig/*.pc", "lib/*/pkgconfig/*.pc",
                        "usr/lib/pkgconfig/*.pc", "usr/lib64/pkgconfig/*.pc",
                        "usr/share/pkgconfig/*.pc", "usr/lib/*/pkgconfig/*.pc"):
            for pc in sorted(root.glob(pattern)):
                if pc.name in packages:
                    continue
                text = pc.read_text()
                if mode == "relocate":
                    text = relocate(text, str(root), pc.parent)
                else:
                    text = text.replace("${pcfiledir}", str(pc.parent))
                packages[pc.name] = text
    # Remove obsolete generated entries if the selected roots changed.
    for pc in output.glob("*.pc"):
        if pc.name not in packages:
            pc.unlink()
    for name, text in packages.items():
        pc = output / name
        if not pc.exists() or pc.read_text() != text:
            pc.write_text(text)


if __name__ == "__main__":
    main()
