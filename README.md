# Opus

Mods for the original Teenage Engineering OP-1, firmware 246. By LizardMan.

A hobby project, built in free time and at its own pace. Help is always welcome, and if you'd like to support it, there's [Ko-fi](https://ko-fi.com/lizardman245).

**Opus 2** is out as a beta: not a bigger pack, but a plugin manager for the OP-1. Its small core loads plugins,
`.opmod` files you copy into a mods folder on the OP-1's disk. Add one, remove one, swap them, no reflashing. The
starter pack has all of Opus 1's features as plugins, plus dx (a DX7 voice player for your own `.syx` banks),
outatime (a time-travel synth engine), iter (the OP-1's own hidden engine) and a Halloween colour theme. The bridge,
an alpha, runs op1r's and Hundekraft's synths and effects on Opus.

- **The core:** make the update from your own stock firmware with the patcher at
  **https://opus-op1.pages.dev/patcher/**. It runs in your browser and nothing is uploaded.
- **The plugins:** the starter pack, each plugin on its own and the bridge are in
  [Releases](https://github.com/LizardMan245/opus/releases).
- **Your own plugins:** the [plugin kit](plugin-kit/) (GPL-3.0).

**Opus 1** (1.0.1) stays on the site as legacy, one pack built and tested together: https://opus-op1.pages.dev/opus1/

[Report a bug](https://github.com/LizardMan245/opus/issues/new?template=bug.yml) · [Ideas and questions](https://github.com/LizardMan245/opus/discussions) ·
[Releases](https://github.com/LizardMan245/opus/releases)

## Moving in and out

Opus is built so you can try it without risk and leave without losing anything.

- **Coming from stock, Opus 1, op1r or Hundekraft:** your sounds, presets and tape stay. Opus 1's sounds play with
  the starter pack in the mods folder. op1r's and Hundekraft's synths and effects play with the bridge.
- **A plugin removed:** a sound that needs it is kept exactly as it is, shows "needs" with the plugin's name, and
  plays again when the plugin comes back.
- **Leaving:** the cleaner (Opus's menu, system) makes your data ready for stock or another mod, and keeps a copy of
  everything it changes. Install Opus again and it all comes back. Going back to Opus 1 needs no cleaner.
- **The way back is always there:** Opus never changes te-boot, the OP-1's own updater and recovery.

## How Opus 2 works

- **IDs nobody has to hand out.** Every plugin is named by its author and itself, like `opus.plate` or
  `yourname.delay`, so names don't collide and nobody has to keep a list.
- **A small description file per plugin:** its ID, name, author, version, the core version it needs and what it
  adds.
- **Libraries.** A plugin can be a library that others need, by ID and version. Opus loads a library before the
  plugins that need it, and refuses a plugin whose library is missing or too old, with the reason. The mods list
  shows what each plugin needs and what uses it.
- **Clashes are refused, not forced.** A place in the firmware holds one plugin. When two plugins want the same place
  or the same keys, the one later by file name is refused, and the mods list says which plugin has them.
- **Costs up front.** The mods list shows each plugin's memory and CPU. Every plugin has a CPU budget in the audio
  block, and its worst block is measured before release. One that uses far more than it should is switched off, and
  one that crashes the OP-1 is left out at the next start.
- **Your sounds stay.** Every preset saved with Opus 1 or 2 names the plugins it needs, by their IDs.

These ideas come from VST3, CLAP, Fabric, Forge, Harmony and Mixin: see [docs/design.md](docs/design.md) and
[CREDITS.md](CREDITS.md).

## The starter pack

| id | what |
|---|---|
| `opus.dimension` | synth engine |
| `opus.amp` | guitar amp engine, with a tuner |
| `opus.dx` | DX7 voice player |
| `opus.outatime` | time-travel synth engine |
| `opus.iter` | the OP-1's hidden synth engine |
| `opus.bitcrush` | effect |
| `opus.chorus` | effect |
| `opus.plate` | reverb |
| `opus.filter` | the OP-1's hidden filter effect |
| `opus.overdrive` | a second effect behind every slot |
| `opus.scales` | scales and chords |
| `opus.bend` | pitch bend |
| `opus.dice` | a random sound, and undo |
| `opus.tapeundo` | tape undo and redo |
| `opus.zoom` | tape zoom |
| `opus.playpause` | play as pause |
| `opus.rename` | preset rename |
| `opus.overwrite` | overwrite save |
| `opus.splash` | your own start-up picture |
| `opus.halloween` | colour theme |

Outside the pack: `opus.bridge` (alpha), which runs op1r's `.op1p` and Hundekraft's `.knl` synths and effects.
Versions, keys, CPU and memory for each: https://opus-op1.pages.dev

The names and numbers that 246 mods share inside the firmware are kept in the
[OP-1 mod registry](https://github.com/LizardMan245/op1-mod-registry).

## Roadmap

1. ~~The core and its menu~~ (Opus 1)
2. ~~Every feature as a part that registers with the core~~ (Opus 1)
3. ~~Reading plugin files from the OP-1's disk at start-up~~ (Opus 2 beta)
4. ~~The loader, with Opus 1's features moved out into plugin files~~ (Opus 2 beta)
5. ~~Every feature as a file, the description file and a plugin kit for anyone~~ (Opus 2 beta)
6. Out of beta, after enough stress and stability testing

No dates: each step ships when it has run on a real OP-1.

## Thanks

TabascoEye, who started it all in 2016, and wavi and op1hacks for op1repacker; popcorp, whose op1r brought the whole
scene back to life; RoutineDiscount for the first entry in the mod registry; and everyone in the Custom Firmware
thread on op-forums. Where ideas came from: [CREDITS.md](CREDITS.md).

## Licence

What's in this repository (the docs and the plugin kit) is [GPL-3.0](LICENSE): use it, change it and share it,
and what you build on it stays open too. The plugins in the releases are © LizardMan, free to use and share. Their
source is not published yet, apart from the kit's four examples. dx contains code under the Apache License 2.0
(its LICENSE and NOTICE are in the starter pack). The firmware updates and the patchers' data are © LizardMan, all
rights reserved: please don't share them, link to the website instead.

Opus is an independent project, not affiliated with or endorsed by Teenage Engineering. OP-1 is a trademark of
Teenage Engineering AB. There is no firmware in this repository.
