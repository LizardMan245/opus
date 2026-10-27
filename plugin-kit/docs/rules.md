# The rules every plugin keeps

A plugin is compiled C (and, if you like, Blackfin assembly) that runs inside the OP-1's own firmware, on its one
processor, next to the OS and the user's music. The core enforces some of these rules itself; the others are yours.

## Where your code runs

The OS has several threads, and each of your handlers runs on one of them ([api.md](api.md) says which for every
call):

| Thread | What runs there | What it may do |
|---|---|---|
| **start** (`opl_init`, the main thread) | once, when the OP-1 starts, before any sound loads | register handlers, allocate memory, read settings and files |
| **the main thread** (`OP_Main`) | the main pass, the save pass, journals | files (in the plugin's own folder); nothing that waits long: the OS's main loop stops meanwhile |
| **the key thread** (`KeyHandler`) | keys, encoder turns, settings changes, quick pages, folder events | change your state, files; keep it short: every key and turn waits for it |
| **the display thread** | drawing: pages, panels, the start of each frame | draw with the core's drawing calls; no files, no long work |
| **the sequencer thread** | `opus_on_block`: once per audio block, before the MIDI is handled, outside the interrupt | short work in step with the audio (a clock, a sequencer) |
| **the audio interrupt** | `opus_on_pre_fx`, `post_fx`, `post_master`, `block_end`; an effect's render, an engine's voices | sound only (below) |

Data shared between threads (a value a turn sets and the audio reads) is best kept to single words written in one
place; the audio side reads, the control side writes.

## The audio block

The OS computes sound in **blocks of 64 samples** (about 1.45 ms at 44.1 kHz), **580,525 CPU cycles** each, shared by
the OS, the core and every plugin. Code that runs in the audio interrupt (an effect's `mono` or `render`, an engine's
voice, the `pre_fx`, `post_fx`, `post_master` and `block_end` events):

- never waits, never allocates, never touches files, never draws, never logs in a loop;
- finishes fast, every time: its worst case is what counts, not its average;
- uses **fixed-point integers**: the BF524 has no floating-point unit, so `float` is emulated in software and slow;
- reads parameters from what the core hands it, and prepares everything heavy (tables, buffers) in `opl_init`.

## CPU: measured and held

Every plugin's audio work is timed by the core, block by block. The manifest's `cpu` line (written by the build from
the plugin folder's `cpu.txt`) is its figure: its worst block in cycles. **A plugin that goes 25 % over its figure in
8 of the last 64 blocks is switched off for the rest of that start**: an effect passes the sound dry, an engine goes
silent, and the mods list says "switched off: N of M cycles". The next start tries it again. A plugin with no figure
is held to a tenth of the block (58,052 cycles). Developer mode never switches anything off.

**Measure yours on the OP-1:** developer mode on, play the heaviest case (all voices, chords, its heaviest settings,
the other plugins you expect beside it), then the details of its row in the mods list (orange push) show
"audio: worst N". Put that in `cpu.txt`:

```
cycles 18274
measured 2026-10-27 on my OP-1: six-voice chords, its heaviest settings
```

and build again. For scale: the starter pack's bitcrush costs 18,274 cycles, chorus 73,496, the outatime engine
120,554, the plate reverb 189,636 (a third of the block). The user chooses what to run together; a heavy plugin says
so in its README.

A plugin that runs other code the user chooses (a manager of effects, a host of other plugins) declares `cpu user`:
it is measured, shown, and never switched off, because its cost is the user's choice.

## Memory

- **The pools.** A plugin's code, data and allocations come from pools the core shares among all plugins; the mods
  list's details show each plugin's share. The starter pack's 20 plugins together use about 1.3 MB. Ask for what you
  need once, in `opl_init`, with `opus_alloc`: it is zeroed, 32-byte aligned and never freed. If it returns 0 the
  pool is full: go on without it or refuse to start (return a negative number from `opl_init`).
- **Fast memory** (`opus_fast`: the chip's own RAM, no cache misses) is scarce, about 25 KB for all plugins, first
  come. Ask for it only for what the audio touches every block (a short delay line, a voice's state), and work
  without it when it is gone.
- **A plugin's image** (code and data) is at most 1 MB.
- **No C library.** No `malloc`, `printf` or `string.h`. Text formatting: `fw_snprintf` (`op1fw.h`). If the compiler
  emits a call to `memcpy` or `memset` (a struct copy, an array cleared at once), define them in your plugin (the
  core exports none).

## The screen

- A page is **320 x 160**, but the OP-1's window shows it only to about **y 147**: keep everything within y 2 to 146.
  Across, the whole width shows.
- The OP-1's own font draws **a-z as capitals; capital letters A-Z draw icons** (an envelope, a drum, a tape...).
  Write text lowercase. Digits and most punctuation draw as they are; anything else draws `?`.
- Draw with the core's calls (`opus_panel_*`, the page and drawing services in [api.md](api.md)): they work on all
  three OP-1 screen types and keep to the OS's look. The encoders' colours are `OPUS_COLOR_E1` to `E4` (blue, green,
  white, orange as the user's theme sets them).

## Controls

Every key or encoder a plugin takes is declared with a `uses` line in its manifest (where, which control, with or
without shift or FN, and what it does). The core checks every plugin's controls against the others' at start and
refuses a clash, and the mods list's green push shows each plugin's controls to the user. Take a stock control's job
only when that is the plugin's whole point, and say so in its README.

## Files and folders

A plugin writes only in its own folder (`self->folder`, on the OP-1's flash) or in a folder of its own on the USB disk
(the core's folder service), on the main or the key thread (never the display thread or the audio), small and
seldom. The flash also holds the user's tape and sounds:
keep within `opus_flash_headroom()`. A change in several steps goes through the core's journals, so a crash or the
power switch cannot leave it half done.

## Names

- The plugin's **id** is `author.name` (lowercase a-z, 0-9, `_`, dots; up to 47 characters). `opus.` is Opus's own.
- An engine's or effect's **name** shows in the OS's lists and is stored in the user's sounds: short, lowercase, and
  not one the OP-1 or another plugin uses (two plugins with one name leave the user's sounds unsure which to load).
- The manifest's `core` line is the oldest core your plugin works with; raise it when you use a newer service.

## When things go wrong

- A plugin that crashes the OP-1 is named by the core's crash record and **left out at the next start**
  ("skipped: crashed"); a changed file or a white push on its row tries it again. Developer mode turns this off.
- **Holding Stop while switching on** starts with no plugins at all.
- te-boot, the OP-1's update and recovery mode, is never touched: Opus can always be installed again.
- **Deep plugins** (`fw_*` calls, `place` lines patching the firmware's code, raw addresses) work on firmware #246's
  insides directly. The core checks a place's bytes before it patches and refuses a mismatch, but what your code does
  there is yours: use them only when no `opus_*` service does the job.
