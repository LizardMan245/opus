# What Opus 2 takes from other plugin systems

Plugin systems for music software and games have solved most of Opus 2's problems before. This is
what Opus takes from them, and why.

## IDs: from VST2 to VST3

VST2 plugins carried a four-letter ID, registered by hand, and with thousands of plugins they
collided. VST3 replaced them with long IDs that nobody hands out.

**Opus:** a plugin's ID is its author's name and its own, like `opus.plate`. Nobody hands them out
and they can't collide, as long as authors use their own name. The [mod registry](https://github.com/LizardMan245/op1-mod-registry)
stays for what mods share inside the firmware today, while each mod is its own firmware.

## A description file: Fabric and Forge

Every Fabric or Forge mod has a small file that says what it is: its ID, its version, the loader
version it needs, the mods it depends on and the ones it breaks with. The loader reads those files
before it loads anything.

**Opus:** each plugin file comes with the same: ID, version, the Opus version it needs, `depends` and
`breaks`. Opus loads plugins in the order their dependencies need, not by file name, and tells you
plainly when one can't load and why.

## Sharing a place: Harmony and Mixin

Harmony (C# games) and Mixin (Minecraft) let several mods attach to the same function, in a set order,
instead of the first one locking everyone else out.

**Opus:** Opus 1 refuses a second claim on the same place in the firmware. Opus 2 will let plugins
share a place in a set order where that's safe (an event several plugins can listen to, like a key
press), and keep refusing where it isn't (two plugins wanting to replace the same thing).

## Features by name: CLAP

A CLAP host offers its features by name, and a plugin asks for the ones it needs. A plugin built for
an older host keeps working on a newer one, because the names it asks for stay.

**Opus:** the core offers what it can do by name and version (the effect list, the engine list, the
menu, settings, files), and a plugin asks for what it needs. A newer Opus keeps the old names, so
plugins keep working.

## Your content stays

Many mod loaders remove a missing mod's content when a world loads (Minecraft and Factorio do).

**Opus:** a sound that needs a plugin you've removed is kept exactly as it was. It plays a stand-in
and says "incompatible preset" until the plugin comes back, then plays as before. Opus 1 already works
this way for sounds from other firmware, and every preset saved with Opus 1 names the Opus parts it
needs, so Opus 2 can tell what each sound is waiting for.

## Costs and a memory budget: what the FM-1 scene found

The M-VAVE FM-1's modding scene tried the same idea in October 2026. Felucca's plugin experiments
([discussion #159](https://github.com/hugelton/Felucca/discussions/159)) ran engine code from its own
flash slot and relocated it at install time, on a real FM-1. The code was small (a few KB); memory was
the wall (about 50 KB for one engine), and the plan went on hold.

**Opus:** memory gets planned before the plugin interface. The core keeps one shared budget, and each
plugin's description file says what it costs: RAM, flash, CPU and the room its saved data takes. Opus
checks the sum before it loads anything, and says which plugin doesn't fit. Opus 1 already measures
two of these on the device (the CPU meter and the storage room).

And a lesson about pace: Opus 2's first version will be small, and the roadmap has no dates.
