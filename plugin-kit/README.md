# Opus 2 plugin kit

Everything you need to write plugins for **Opus 2**, the firmware mod for the original Teenage Engineering OP-1
(firmware #246; not the OP-1 field). Opus 2's core is small and does one job: it loads **plugins**, small files
(`.opmod`) you put in the mods folder of the OP-1's USB disk, and gives them what they need to fit in as if the OP-1
had always had them: synth engines and effects in the OS's own lists with their own pages, settings, controls,
colours, files and folders, and a measured share of the CPU and memory. Every feature of Opus is a plugin, built
with the same tool and the same headers as the ones in this kit.

**Quality over quantity.** The kit is made for plugins that are checked and tested before they are shared: a plugin
runs inside the OP-1's own firmware, next to the user's music, so its limits are real. [docs/rules.md](docs/rules.md)
says what every plugin keeps to.

## What is in it

| Folder or file | What |
|---|---|
| `docs/getting-started.md` | The compiler, your first plugin, putting it on the OP-1. **Start here.** |
| `docs/rules.md` | The rules every plugin keeps: threads, the audio block, CPU, memory, the screen, files. |
| `docs/api.md` | Every call the core offers a plugin (`opus_*`), by service. |
| `docs/manifest.md` | Every line of a plugin's `manifest.txt`. |
| `docs/sharing.md` | The checks before you share a plugin. |
| `docs/orbit3d.md` | The orbit3d library: 3D line art on a plugin's page. |
| `sdk/opus_plugin.h` | The core's calls. |
| `sdk/op1fw.h` | The firmware's own functions by name (`fw_*`), for advanced plugins. |
| `sdk/orbit3d.h` | The orbit3d library's calls (`o3_*`), for a plugin that `needs opus.orbit3d`. |
| `plugin_tool.py` | The command: `doctor` (is this computer ready), `new`, `build`, `show`, and `svg` (checks page art for the OP-1). |
| `examples/` | Four plugins from Opus 2's starter pack: bitcrush (an effect), zoom (a tape tool), halloween (a theme, no code) and outatime (a synth engine with a designed SVG screen); and globe, a page drawn with orbit3d. |
| `tools/` | The build tool's code and the script that builds the Blackfin compiler. |
| `AGENTS.md` | Instructions for AI coding agents (Claude Code, Codex, Cursor, Copilot and others). |

## What you can build

Synth engines, effects and themes, as the examples show, and much more ([api.md](docs/api.md) has every service):

- **Plugins that run other plugins.** A plugin can list every installed effect, make its own instances of any of them
  and run them where it likes: an effect rack (three effects in series or parallel on one sound), a morpher fading
  between two effects on one knob, an effect randomiser. Opus's Fx overdrive is one.
- **Libraries.** A plugin can export functions other plugins build on (`exports`, `needs`): one tuning library every
  engine uses for microtuning, shared filters and oscillators.
- **Importers.** A plugin gets its own folder on the USB disk and takes what the user drops in, in its own form, as
  Opus's dx takes `.syx` banks: tuning files, wavetables, patches from other synths.
- **Displays on the OP-1's own screens.** Draw over or under any stock screen every frame: a CPU or level meter in a
  corner, a scope, a clock, the tape position in bars, the current scale's name.
- **Watching what happens.** Every key and turn after the OS handled it, every tape write before it lands: a tape
  version history, a practice log with the date. Opus's tape undo is built this way.
- **The whole mix.** The final stereo master every block: a master limiter, a loudness meter, an analyser.
- **Small apps.** A plugin can be just a panel in the mods list or a page of its own over T1: a BPM-to-delay
  calculator, setlist notes, stats.
- **Changes to how the OS behaves**, which the user switches on and off live from the plugin's settings.
- **LFOs.** A row of your own in the OS's LFO list (shift + T4), with its own T4 page: it moves an engine's knobs
  (a plugin engine's too), the envelope, the effect, the volume and the pitch, at a rate in Hz or locked to the
  tempo. Opus's matrix is one.
- **Sequencers.** A row of your own in the OS's sequencer list: it takes finger's place for the synth or the drum,
  plays notes from the sequencer clock and has its own page.
- **Drum engines.** A row of your own in the drum's engine list, its kits saved, loaded and moved by the LFO like the
  OS's own.

## Quick start

```
python plugin_tool.py doctor                    # is this computer ready: Python, the compiler
python plugin_tool.py new yourname.hello        # a working plugin in plugins/yourname.hello
python plugin_tool.py build plugins/yourname.hello
python plugin_tool.py show build/hello.opmod
```

Then copy `build/hello.opmod` into the OP-1's mods folder in disk mode, restart the OP-1 and find it in Opus's mods
list. You need Python 3.8 or later and the Blackfin compiler first: [docs/getting-started.md](docs/getting-started.md).
The compiler is set up once per computer and serves every plugin: keep one copy of the kit and make each new plugin
in its `plugins/` folder.

## Working with an AI agent

Open this folder in your agent (Claude Code, Codex, Cursor, Copilot, ...) and describe the plugin you want. The agent
reads [AGENTS.md](AGENTS.md): how plugins are built here, the rules they keep, and what to check before calling a
plugin done. It still needs you for the OP-1 itself: you copy the file, play it and tell it what you see and hear.

## What the kit does not have yet

Opus 2 is a beta, and so is this kit. Still to come: the emulator that runs the OP-1's own firmware on a computer
with its test files (how every Opus plugin is tested before it reaches an OP-1, and how CPU figures are measured), the
USB dev port's tools (pushing a plugin without disk mode, reading memory and the screen, decoding a crash), and the
check that tells whether a set of plugins loads together.

## Licence

GPL-3.0 ([LICENSE](LICENSE)): the tools, the headers, the docs and the examples. Opus is by LizardMan.

Two additional terms (GPL-3.0 section 7, points c and e) apply to the kit, its examples and the plugins made with it:

- A modified version has to be marked as different from the original.
- It can't use the original plugin's name.

So a plugin you share keeps its name: someone who changes it shares the change under a name of their own, marked as
a modified version of yours.
