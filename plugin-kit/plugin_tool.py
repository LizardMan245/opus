"""plugin_tool: make, build and read Opus 2 plugins (the authors' kit's command).

    python plugin_tool.py new AUTHOR.NAME [--out DIR]       a working plugin folder (default plugins/AUTHOR.NAME)
    python plugin_tool.py build DIR [--out FILE.opmod]     DIR's sources and manifest.txt -> its .opmod
                                                           (default build/NAME.opmod)
    python plugin_tool.py show FILE.opmod                  the file read back as the core reads it
    python plugin_tool.py svg FILE.svg ...                 will the OP-1 draw this page art, and draw it as meant?

The Blackfin compiler (bfin-elf-gcc and binutils) is found through OP1_BFIN_TOOLCHAIN, a bfin-toolchain folder
beside the kit's, or the PATH (docs/getting-started.md). Standard library only; Python 3.8 or later.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for lib in (os.path.join(HERE, 'tools', 'op1mod'), os.path.join(HERE, '..', 'op1mod')):   # the kit's, the repository's
    if os.path.isfile(os.path.join(lib, 'plugin.py')):
        sys.path.insert(0, os.path.normpath(lib))
        break
import plugin  # noqa: E402
from build import BuildError  # noqa: E402


def cmd_new(a):
    where = a.out or os.path.join(plugin.REPO, 'plugins', a.id)
    where = plugin.new(a.id, where, kit=True)
    print(f'{where}: manifest.txt, {a.id.split(".")[-1]}.c, README.md (README.md says what next)')


def cmd_build(a):
    name = os.path.basename(os.path.normpath(a.dir)).split('.')[-1]
    out = a.out or os.path.join(plugin.REPO, 'build', name + '.opmod')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    r = plugin.build(a.dir, out)
    print(f'{out}: {r["id"]} {r["version"]}, {r["bytes"]} bytes, imports {", ".join(r["imports"]) or "none"}; '
          f'{r["link"]}')


def cmd_show(a):
    info = plugin.read_opmod(open(a.file, 'rb').read())
    print(info['text'].rstrip())
    seen = plugin.checked_of(info['manifest'])
    print('checked: ' + ('; '.join(f'{k} {v}' for k, v in sorted(seen.items())) if seen else
                         'none (no checked line: not recorded as tested)'))
    print(f'image {len(info["image"])} bytes, zero fill {info["zero"]}, entry +0x{info["entry"]:x}, '
          f'{len(info["relocs"])} relocations, imports: {", ".join(info["imports"]) or "none"}'
          + (f'; exports: {", ".join(n for n, _ in info["exports"])}' if info['exports'] else ''))
    for kind, addr, target, expect, name, sw in info['places']:
        print(f'place {name}: {kind} 0x{addr:08x} -> image+0x{target:x}, expects {expect.hex()}'
              + (', switched' if sw else ''))


def cmd_svg(a):
    """svgcheck's errors (the OP-1 stops or the core refuses the file) and warnings (drawn otherwise, or dropped)."""
    import svgcheck
    bad = 0
    for path in a.files:
        errors, warnings = svgcheck.check(path)
        print(f'{path}: {len(errors)} errors, {len(warnings)} warnings')
        for e in errors:
            print(f'  error: {e}')
        for w in warnings:
            print(f'  warning: {w}')
        bad |= bool(errors)
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    n = sub.add_parser('new', help='a working plugin folder')
    n.add_argument('id', help='author.name: a-z 0-9 _ and dots')
    n.add_argument('--out', help='the folder to make (default plugins/ID)')
    b = sub.add_parser('build', help="a plugin folder -> its .opmod")
    b.add_argument('dir')
    b.add_argument('--out', help='the .opmod to write (default build/NAME.opmod)')
    s = sub.add_parser('show', help='an .opmod read back as the core reads it')
    s.add_argument('file')
    v = sub.add_parser('svg', help='check page art for the OP-1')
    v.add_argument('files', nargs='+')
    a = ap.parse_args(argv)
    try:
        return {'new': cmd_new, 'build': cmd_build, 'show': cmd_show, 'svg': cmd_svg}[a.cmd](a) or 0
    except (plugin.PluginError, BuildError) as e:
        print(f'REFUSED: {e}')
        return 2


if __name__ == '__main__':
    sys.exit(main())
