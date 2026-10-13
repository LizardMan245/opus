# Opus

Mods for the original Teenage Engineering OP-1, firmware 246. By LizardMan.

**Opus 1** is out: new synth engines (dimension, and a guitar amp with a tuner), effects (bitcrush,
chorus, plate, and a second effect behind every slot), scales and chords, tape undo and redo, preset
rename and overwrite save, 100 sampler presets, your own colours, themes and start-up picture, and a
menu of its own. Make it from your own stock firmware with the patcher at
**https://opus-op1.pages.dev**: it runs in your browser and nothing is uploaded.

**Opus 2** is where this is going: not a bigger pack, but a plugin manager for the OP-1. Mods become
files you copy onto the OP-1's disk, and Opus loads them at start-up. Add one, remove one, swap them,
no reflashing.

[Report a bug](https://github.com/LizardMan245/opus/issues/new?template=bug.yml) · [Ideas and questions](https://github.com/LizardMan245/opus/discussions) ·
[Changelog](https://opus-op1.pages.dev/changelog/)

## Moving in and out

Opus is built so you can try it without risk and leave without losing anything.

- **Coming from stock or another mod:** Opus starts on the data you already have. A sound it can't
  play gets a stand-in and shows "incompatible preset", and is never rewritten, so it's all still
  there if you go back.
- **Leaving:** the cleaner (Opus's menu, system) makes your data ready for stock or another mod, and
  keeps a copy of everything it changes. Install Opus again and it all comes back.
- **Updating** keeps your sounds, presets, tape and settings.
- **The way back is always there:** Opus never changes te-boot, the OP-1's own updater and recovery.

## How Opus 2 will work

- **IDs nobody has to hand out.** Every plugin is named by its author and itself, like `opus.plate` or
  `yourname.delay`, so names don't collide and nobody has to keep a list.
- **A small description file per plugin:** its ID, its version, the Opus version it needs, and the
  plugins it depends on or doesn't work with. Opus loads plugins in the order their dependencies
  need, not by file name.
- **Sharing a place.** Where it's safe, several plugins can attach to the same place in the firmware,
  in a set order. Where it isn't, Opus says no, with the reason, before anything breaks.
- **Costs up front.** Each plugin says how much memory, flash and CPU it needs, and Opus keeps one
  shared budget, so you know before adding a plugin whether it fits.
- **Features by name.** Opus offers what it can do by name and version, and a plugin asks for what it
  needs, so plugins written today keep working on a newer Opus.
- **Your sounds stay.** A sound that needs a plugin you've removed is kept exactly as it is, and plays
  again when the plugin comes back. Every preset saved with Opus 1 already names the parts it needs.

These ideas come from VST3, CLAP, Fabric, Forge, Harmony and Mixin: see [docs/design.md](docs/design.md).

## Plugin IDs in Opus 1

| id | what |
|---|---|
| `opus.dimension` | synth engine |
| `opus.amp` | guitar amp engine |
| `opus.bitcrush` | effect |
| `opus.chorus` | effect |
| `opus.plate` | reverb |
| `opus.overdrive` | a second effect behind every slot |

Until mods are plugins, the names and numbers that 246 mods share inside the firmware are kept in the
[OP-1 mod registry](https://github.com/LizardMan245/op1-mod-registry).

## Roadmap

1. ~~The core and its menu~~ (Opus 1)
2. ~~Every feature as a part that registers with the core~~ (Opus 1)
3. Reading plugin files from the OP-1's disk at start-up (in progress)
4. The loader, with the first Opus 1 feature moved out into a plugin file
5. Every feature as a file, the description file, and a plugin template for anyone

No dates: each step ships when it has run on a real OP-1.

## Thanks

TabascoEye, who started it all in 2016, and wavi and op1hacks for op1repacker; popcorp, whose op1r
brought the whole scene back to life; RoutineDiscount for the first entry in the mod registry; and
everyone in the Custom Firmware thread on op-forums.

## Licence

What's in this repository (the docs now, the plugin interface and template as they come) is
[GPL-3.0](LICENSE): use it, change it and share it, and what you build on it stays open too. The Opus 1
firmware update and the patcher's data are © LizardMan, all rights reserved for now: please don't share
them, link to the website instead.

Opus is an independent project, not affiliated with or endorsed by Teenage Engineering. OP-1 is a
trademark of Teenage Engineering AB. There is no firmware in this repository.
