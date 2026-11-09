"""plugin_tool: make, build and read Opus 2 plugins (the authors' kit's command).

    python plugin_tool.py new AUTHOR.NAME [--out DIR]       a working plugin folder (default plugins/AUTHOR.NAME)
    python plugin_tool.py build DIR [--out FILE.opmod]     DIR's sources and manifest.txt -> its .opmod
                                                           (default build/NAME.opmod)
    python plugin_tool.py show FILE.opmod                  the file read back as the core reads it
    python plugin_tool.py svg FILE.svg ...                 will the OP-1 draw this page art, and draw it as meant?
    python plugin_tool.py doctor                           is this computer ready: Python, the compiler (run it first)

The Blackfin compiler (bfin-elf-gcc and binutils) is set up once per computer, never for each plugin. It is found
through OP1_BFIN_TOOLCHAIN, a bfin-toolchain folder beside the kit's, the PATH, or the folders the guide builds it
into (docs/getting-started.md). A new plugin is a new folder in this kit's plugins/ (`new`), not a new copy of the
kit. Standard library only; Python 3.8 or later.
"""
import argparse
import os
import subprocess
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


def strays(kit):
    """Compilers lying where the lookup does not go: bfin-toolchain folders beside other copies of the kit (in the
    kit's parent and grandparent and their folders, the home folder and its folders). One level deep each: fast, no
    disk crawl."""
    import build
    out, roots = [], []
    home = os.path.expanduser('~')
    for root in (os.path.dirname(kit), os.path.dirname(os.path.dirname(kit)), home):
        if root and os.path.isdir(root) and root not in roots:
            roots.append(root)
    for root in roots:
        cands = [os.path.join(root, 'bfin-toolchain')]
        try:
            cands += [os.path.join(root, d, 'bfin-toolchain') for d in sorted(os.listdir(root))]
            cands += [os.path.join(root, d, 'linux-install') for d in sorted(os.listdir(root)) if 'bfin' in d.lower()]
        except OSError:
            continue
        for c in cands:
            if os.path.exists(build.tool(c, 'gcc')) and os.path.normcase(c) not in [os.path.normcase(o) for o in out]:
                out.append(c)
    return out


def cmd_doctor(a):
    """Is this computer ready to build plugins? Python, and where the compiler is (or where one lies unused)."""
    import build
    ok = sys.version_info >= (3, 8)
    print(f'python {sys.version.split()[0]}: ' + ('ok' if ok else 'too old: the kit needs 3.8 or later'))
    print('the Blackfin compiler, looked for in order:')
    for why, tc in build.toolchain_places():
        print(f'  {why}: {tc}: ' + ('found' if os.path.exists(build.tool(tc, "gcc")) else 'no'))
    try:
        tc = build.toolchain()
    except BuildError as e:
        others = [s for s in strays(plugin.REPO)]
        if others:
            print('compilers lying elsewhere on this computer (point the kit at one; do not build another):')
            for s in others:
                print(f'  {s}')
            how = 'setx OP1_BFIN_TOOLCHAIN ' if os.name == 'nt' else 'export OP1_BFIN_TOOLCHAIN='
            print(f'then: {how}{others[0]}   (a new terminal sees it)')
        print(f'NOT READY: {e}')
        return 1
    try:
        version = subprocess.run([build.tool(tc, 'gcc'), '--version'], capture_output=True, text=True,
                                 timeout=30).stdout.splitlines()[0]
    except (OSError, subprocess.SubprocessError, IndexError):
        print(f'NOT READY: {build.tool(tc, "gcc")} is there but does not run')
        return 1
    print(f'compiler: {tc} ({version})')
    if '13.3.0' not in version:
        print('  note: Opus is built with GCC 13.3.0; another version works but may give other code')
    print('READY: build plugins (python plugin_tool.py build plugins/ID). The compiler is set up: never build it '
          'again on this computer.')
    return 0 if ok else 1


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
    sub.add_parser('doctor', help='is this computer ready: Python, the compiler (run it first)')
    a = ap.parse_args(argv)
    try:
        return {'new': cmd_new, 'build': cmd_build, 'show': cmd_show, 'svg': cmd_svg,
                'doctor': cmd_doctor}[a.cmd](a) or 0
    except (plugin.PluginError, BuildError) as e:
        print(f'REFUSED: {e}')
        return 2


if __name__ == '__main__':
    sys.exit(main())
