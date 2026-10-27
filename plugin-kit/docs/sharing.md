# Before you share a plugin

Opus is about plugins people can trust next to their music: fewer, better ones. Go through this list before you
share yours, and say in its README what you checked.

## It builds and reads back right

- [ ] `python plugin_tool.py build DIR` builds with no warnings (they are errors here; fix the cause).
- [ ] `python plugin_tool.py show FILE.opmod` lists what you expect: its id, name and version, its settings, its
      controls (`uses`), the core's names it calls, and no `fw_*` or `place` you did not mean.
- [ ] Its id is yours (`yourname.plugin`, not `opus.`), and its engine or effect name is short, lowercase and not
      one the OP-1 or Opus already uses.
- [ ] Its `version` went up since the last file you gave anyone.

## It works on an OP-1

- [ ] It loads: the mods list's row says "loaded" (with developer mode off as well as on).
- [ ] It does what its README says, with the controls its manifest declares, and nothing else changes.
- [ ] It survives what users do: switching sounds and presets, saving, recording to tape, disk mode, switching the
      OP-1 off and on, and its settings at their lowest and highest.
- [ ] It was tried next to the other plugins people commonly use (the starter pack), and they all still load.
- [ ] Text on its pages is lowercase and its content stays within y 2 to 146 of the 320 x 160 page.

## Its CPU and memory are known

- [ ] For audio code: its worst block measured in the heaviest case (all voices, chords, its heaviest settings) and
      written to `cpu.txt` as `cycles N` (docs/rules.md, "CPU"). A plugin that costs a large share of the 580,525-cycle
      block says so in its README, so users can choose.
- [ ] Its memory (the mods list's details) is what you expect, and it asks for fast memory only if its audio needs it.

## Its record is honest

- [ ] `checked device DATE what you played` in `manifest.txt`, for the OP-1 tests above (`show` prints it).
- [ ] A README beside it: what it does, its controls, its settings, its CPU, what it does not do yet, and how to reach
      you.
- [ ] Its licence: the examples and the kit are GPL-3.0, so a plugin built from an example's code is GPL-3.0 too and
      its source goes with it.
