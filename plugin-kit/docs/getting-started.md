# Getting started

From nothing to your own plugin running on the OP-1. You need:

- an **original OP-1 with Opus 2 installed** (the Opus site's patcher makes the update from your own `op1_246.op1`);
- **Python 3.8 or later** (only its standard library);
- the **Blackfin compiler**: GCC and binutils for `bfin-elf`, the OP-1's processor (an Analog Devices BF524). The
  kit builds it from GNU's own sources (below).

## 1. The Blackfin compiler

`tools/toolchain/build-bfin-toolchain.sh` downloads binutils 2.38 and GCC 13.3.0 from ftp.gnu.org, checks their
signatures against the GNU keyring, and builds a C compiler for `bfin-elf`. It runs on Ubuntu (or Debian) and takes
a while. It is the same compiler Opus itself is built with.

**Linux:**

```
sudo bash tools/toolchain/build-bfin-toolchain.sh deps            # build tools from apt (needs root)
WORK=$HOME/bfin-build bash tools/toolchain/build-bfin-toolchain.sh fetch verify extract linux
export PATH=$HOME/bfin-build/linux-install/bin:$PATH             # add this to your shell's profile
bfin-elf-gcc --version                                           # bfin-elf-gcc (GCC) 13.3.0
```

**Windows:** in WSL with Ubuntu (`wsl --install -d Ubuntu`), from the kit's folder as WSL sees it (`/mnt/c/...`):

```
sudo OUT=/mnt/c/bfin-toolchain bash tools/toolchain/build-bfin-toolchain.sh
```

That builds the Linux compiler first, then the same compiler as Windows programs (`bfin-elf-gcc.exe` and friends) and
copies them to `C:\bfin-toolchain`. **`OUT` must be a new folder: the script empties it first.** WSL is only needed
for this one build; afterwards everything runs in Windows.

**macOS:** not tested yet. The Linux steps with Homebrew's build tools (`make`, `texinfo`, `bison`, `flex`) are the
likely way; if you get it working, tell us how.

**Where the kit looks for it:** the folder in the environment variable `OP1_BFIN_TOOLCHAIN` (the folder that holds
`bin/`), else a `bfin-toolchain` folder next to the kit's folder, else `bfin-elf-gcc` on your PATH. On Windows:

```
setx OP1_BFIN_TOOLCHAIN C:\bfin-toolchain
```

(`setx` takes effect in terminals opened after it.)

## 2. Your first plugin

```
python plugin_tool.py new yourname.hello
```

makes `plugins/yourname.hello/` with three files:

- `manifest.txt`: who the plugin is (its id, name, author, version, the core version it needs), the setting it
  offers and the control it uses. Every line: [manifest.md](manifest.md).
- `hello.c`: the code. `opl_init` runs once when the OP-1 starts; it registers what the plugin does (here: a setting,
  a panel on the mods list, a turn of the green encoder) and returns 0. Every call: [api.md](api.md).
- `README.md`: the commands for this plugin.

An id is `author.name`: lowercase letters, digits and `_`, with dots between. Use your own name as the first part;
`opus.` is Opus's own.

Build it:

```
python plugin_tool.py build plugins/yourname.hello
```

The build compiles with every warning as an error (so a call the headers do not declare stops it here), links the
plugin so the core can place it anywhere in memory, lists the core's names it calls (the core looks each one up when
it loads the plugin), and writes `build/hello.opmod`. If something is wrong
it says `REFUSED:` and why. Read the file back as the core will:

```
python plugin_tool.py show build/hello.opmod
```

## 3. On the OP-1

1. **Developer mode** (once): open Opus's menu (hold **synth + drum + tape + mixer** together), **T1** for the
   system settings, and switch **developer mode** on. While it is on, a plugin that crashes is not left out at the
   next start and one that uses too much CPU is not switched off, so you see what yours really does. Switch it off
   when you are done.
2. **Disk mode:** press **shift + COM**, then **T3**, and connect the OP-1. Copy `build/hello.opmod` into the disk's
   **mods** folder, eject the disk, and switch the OP-1 off and on.
3. **The mods list:** the menu again, then **T2**. Turn **blue** to your plugin's row: it says "loaded", or why not
   ("needs opus_something": it calls a name this core does not have; "not loaded: no room": out of memory). Push
   **blue** for its settings, **green** for its controls, **orange** for its details (its memory, its worst audio
   block in CPU cycles, its version).

The hello plugin draws its level on the right of the mods list while its row is selected; turn green to change it.

**If something goes wrong:** a plugin that crashes the OP-1 is left out at the next start ("skipped: crashed";
outside developer mode), and a new file or a **white** push on its row tries it again. Holding **Stop** while
switching the OP-1 on starts it with no plugins at all; then delete the plugin from the mods folder in disk mode.
Nothing a plugin does can touch te-boot, the OP-1's update and recovery mode: you can always install Opus again
(hold **COM** while switching on).

**Reading a crash:** after a crash, the next start writes a crash log, and disk mode shows it in the **crash** folder
on the USB disk (`/crash/N.txt`, the last eight; delete one there and it is gone from the OP-1 too). Its first lines
say what stopped the OP-1 (the OS's assert, or the processor's exception) and which plugins were loaded; the rest is
the whole record. `python tools/crashlog.py N.txt` reads it: every address in a plugin shows as `id+offset`, and
`--plugin ID=build/NAME.build/plugin_0.elf` (your build's linked file) names your plugin's functions there. Attach the
log when you report a crash.

## 4. Learn from the examples

Build any of them the same way (`python plugin_tool.py build examples/opus.bitcrush`). Each is the exact source of
the plugin in Opus 2's starter pack.

- **`opus.halloween`**, a theme: no code at all, one `theme` line in its manifest. A plugin can be just a manifest.
- **`opus.zoom`**, a tape tool: about 50 lines. It takes shift + white on the tape screen (declared with a `uses`
  line) and changes a value inside the OS (the tape view's zoom) at an address the firmware keeps it at.
- **`opus.bitcrush`**, an effect: an effect type in the OS's own effect list, its sound code in the audio block, its
  own page with four values in the encoders' colours, and a measured CPU figure (`cpu.txt`).
- **`opus.outatime`**, a synth engine with a designed screen. Its sound: three oscillators a voice, every curve a
  table (`make_tables.py` writes `ota_tables.h`; fixed-point, no floating point), in the OS's own engine list. Its T1
  page is an **SVG design** with live instruments over it: `art/page.js` draws the parts that never move, and
  `node art/build.js` (Node.js) turns them into `art/outatime_t1.svg`, to look at and check, and `ota_art.h`, the
  same SVG as a C string with the places the moving parts go. At start the plugin writes the SVG into its folder
  (`opus_art_text`); `ota_page.c` has it drawn once, keeps its pixels, recolours it to the user's theme and draws the
  instruments over it from the sound's knobs every frame. Check page art with
  `python plugin_tool.py svg examples/opus.outatime/art/outatime_t1.svg`: the OP-1's own SVG parser draws only part
  of SVG and stops on a few things, and `svg` says which.

## 5. Next

- [rules.md](rules.md) before you write audio code: what may and may not happen inside the audio block, how the core
  holds a plugin to its CPU figure, and how to measure yours on the OP-1.
- [sharing.md](sharing.md) before you share a plugin.
