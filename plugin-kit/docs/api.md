# Opus 2 plugin API reference

This is the complete reference for the C interface of an Opus 2 plugin: every function, struct, constant and callback
declared in `sdk/opus_plugin.h`. The manifest (`manifest.txt`) has its own reference, [manifest.md](manifest.md).

Opus 2 is a firmware mod for the original Teenage Engineering OP-1 (firmware #246, a Blackfin BF524 CPU). Its core is
small: it loads plugins from the OP-1's USB disk and gives them services (events, memory, drawing, settings, files,
effects, synth engines). A plugin is a `.opmod` file: your compiled C code plus its manifest.

## Overview

### How a plugin starts

Every plugin defines one function, `opl_init`. At each start of the OP-1 the core:

1. reads every `.opmod` file in the `mods` folder of the USB disk, in file-name order, and checks each file and its
   manifest before any code runs (a bad or clashing plugin is refused, and the mods list says why);
2. for each accepted plugin in turn (a plugin that needs a library loads after it): copies its code into memory,
   resolves its imports (below), checks its places, fills in its `struct opl_self`, reads its settings, and calls
   `opl_init(self)`;
3. when `opl_init` returns 0, installs the plugin's places (its hooks into firmware code, declared in the manifest).

This happens in the OS's main thread, after the OS opened its databases and before it loads any sound. The audio is
not running yet. In `opl_init` a plugin registers its handlers, effects, engines and so on, and takes its memory.

A plugin is never unloaded while the OP-1 runs. What it registers stays until the OP-1 switches off; a new, changed or
removed plugin file takes effect at the next start. Holding Stop while the OP-1 starts loads no plugin at all (the
safe start). A plugin that crashed the OP-1 is skipped at the next start ("skipped: crashed") until its file changes.

### Every name is an import

Every `opus_*` name (and every `fw_*` name, below) a plugin uses is an import: the build lists it in the `.opmod`
file, and the core gives its address when it loads the plugin. There is no table to keep in order. A plugin naming a
function the core does not have is refused, and the mods list says which one: "needs opus_clock". So a plugin built
for a newer core fails cleanly on an older one. A name the core adds later costs older plugins nothing.

### Threads

The OS runs several threads, and every callback says in which one it runs. The thread is the callback's limit:

- **The main thread** (the OS's `OP_Main`): `opl_init`; the main-pass, save-pass, queue and tape-write handlers. It
  may use the file system. Nothing that waits long: the OS's main loop stops meanwhile.
- **The key thread** (the OS's `KeyHandler`): the key, knob, setting, panel-knob and quick-page knob handlers; engine
  and effect turns; a folder's `take`. It may use the file system. Keep it short: every key and turn waits for it.
- **The display thread**: the frame, panel, page and quick-page draw handlers; an effect's `view`. It draws one frame
  at a time. No file system (its lock may be held by another thread). Short.
- **The sequencer thread**: `opus_on_block` handlers, once per audio block, outside the audio interrupt. Short.
- **The audio block** (an interrupt): the `pre_fx`, `post_fx`, `post_master` and `block_end` handlers; effect, engine
  and hosted-effect sound code. The strictest rules: see below.

"Any thread" in this reference means any of the first four. Functions that may not be called from the audio block say
so; when in doubt, do not call a function from the audio block.

### The audio block

The OS computes sound in blocks of 64 samples at 44.1 kHz (about 1.45 ms). The CPU runs 580,525 cycles in that time,
and the OS's own sound work takes much of it. Code called inside the audio block runs in an interrupt: it must not
wait (no file, no lock, no sleep), must not allocate memory, and must be short. Samples are signed 16-bit (`short`),
processed in place. The OS sometimes renders a block from a thread instead of the interrupt; the same rules hold.

Every audio handler and every effect or engine sound call is timed and charged to its plugin. The mods list shows each
plugin's audio CPU.

### The CPU budget

Each plugin is held to a CPU figure for its audio work: the manifest's `cpu` line, which the build writes from a
measurement, or, for an unmeasured plugin, a tenth of a block (58,052 cycles). A plugin whose audio work goes over its
figure by more than a quarter in 8 of the last 64 blocks is switched off for the rest of that start: its audio
handlers stop, its effects pass the sound through unchanged and its engines' voices are silent. The mods list says
"switched off" with the figures. Nothing is marked: the next start tries again. Two exceptions: a manifest with
`cpu user` (a manager that runs what the user chose) is measured but never switched off, and in the OP-1's developer
mode (a row of the system menu) no plugin is switched off.

### The OP-1's controls

The four encoders, left to right: **blue** (0), **green** (1), **white** (2), **orange** (3). Pushing an encoder is a
key event (the pushes below). The help key is Opus's **FN** key: the OS never sees it.

Key handlers get the OS's key codes:

| Key | Code | Key | Code | Key | Code |
|---|---|---|---|---|---|
| FN (help) | 0x00 | s1 (sound key 1) | 0x2D | rec | 0x21 |
| tempo | 0x01 | s2 | 0x2E | play | 0x22 |
| synth | 0x02 | s3 | 0x2F | stop | 0x23 |
| drum | 0x03 | s4 | 0x10 | left | 0x24 |
| tape | 0x04 | s5 | 0x11 | right | 0x25 |
| mixer | 0x05 | s6 | 0x12 | shift | 0x26 |
| T1 | 0x06 | s7 | 0x13 | mic | 0x2B |
| T2 | 0x07 | s8 | 0x14 | com | 0x2C |
| T3 | 0x08 | seq | 0x15 | blue push | 0x3B |
| T4 | 0x09 | lift | 0x0A | green push | 0x3C |
| split | 0x0C | drop | 0x0B | white push | 0x3D |
|  |  |  |  | orange push | 0x3E |

The keyboard's 24 note keys have the codes 0x0D-0x0F, 0x16-0x20, 0x27-0x2A and 0x30-0x35.

The manifest declares the controls a plugin takes (`uses` lines), so two plugins never take the same control; see
manifest.md.

### Core versions

The manifest's `core` line names the lowest core version a plugin needs; an older core refuses it ("needs core 2.6").
Most of the API is in core 2.0. These came later; set `core` to the highest version among those you use:

| Core | Adds |
|---|---|
| 2.1 | switched places: `opus_place_set` |
| 2.2 | managers: `opus_fx_manager` and the rest of the Managers section |
| 2.3 | `opus_engine_knobs` |
| 2.4 | `opus_clock` |
| 2.5 | `opus_engine_show` |
| 2.6 | another firmware's engines and effects (below the table) |
| 2.7 | `opus_on_settings_rows` |

Core 2.6's additions: `OPUS_ENGINE_OS_VOICES`; the engine fields `type`, `mark`, `svg`, `screen_init` and
`screen_draw`; `struct opus_hosted_effect` and `opus_effect_host`.

### Words used in this reference

- **The drawing**: the screen as plugins see it, 320 x 160 **units**, x across from the left, y down from the top. The
  core scales it to the panel the OP-1 has. On the device only y 2-146 is visible through the OP-1's window: keep what
  matters inside it.
- **The panels**: the OP-1 has three screen types: rev 2 (376 x 188 pixels), rev 1 (400 x 200) and FE (320 x 160).
  Everything drawn in units looks the same on all three.
- **A detent**: one click of an encoder. The OS counts 4 units per detent; some handlers get OS units, some detents.
- **The mods list**: Opus's list of plugins in its menu. A plugin's row has a panel on the right; the blue push opens
  its settings page, the green push its controls, the orange push its details.
- **The boot log**: a 2,048-byte text log in the core's memory (not a file): the core notes there why it refused
  something, and plugins add their own lines with `opus_log`. It is read over USB with Opus's development tools.

## Contents

- [The plugin's entry](#the-plugins-entry): `OPUS_PLUGIN_API`, `struct opl_self`, `opl_init`
- [Events](#events): keys, encoders, FN, frames, the main pass, the clip queue, tape writes, saving, audio
- [Quick pages](#quick-pages): a plugin's own page in place of the screen
- [Files](#files): rename, the background file job, the flash's headroom, reading files
- [Journals](#journals): changes in several steps that survive a power cut
- [Presets](#presets): the synth presets as the browser lists them
- [Memory](#memory): the pool, fast memory
- [CPU and the log](#cpu-and-the-log)
- [Settings](#settings): the manifest's settings, a plugin's own rows on the settings page
- [Switched places](#switched-places)
- [Folders on the USB disk](#folders-on-the-usb-disk)
- [The panel](#the-panel): a plugin's panel on the mods list
- [Colours](#colours): colours by id, the theme's accents
- [Effects](#effects): effect types in the OS's effect list
- [Hidden stock effect and engine](#hidden-stock-effect-and-engine)
- [Another firmware's effects](#another-firmwares-effects)
- [Managers](#managers): running instances of any effect type
- [Engines](#engines): synth engines in the OS's engine list
- [Pages](#pages): drawing a plugin's own page
- [Paths](#paths): lines and curves drawn by the OS's renderer
- [Art](#art): SVG art on a page
- [Pixels](#pixels): a page's own pixels
- [The clock](#the-clock)
- [Firmware functions (`fw_*`)](#firmware-functions-fw_)
- [Index](#index)

## The plugin's entry

```c
#define OPUS_PLUGIN_API 1
```

The version of `opus_plugin.h` itself. It is not the core's version; the manifest's `core` line names that.

```c
struct opl_self {
    unsigned size;
    const char *id;
    const char *folder;
    int slot;
};
```

What the core tells a plugin about itself. The core fills it in before `opl_init` and keeps it for the whole start.

- `size`: `sizeof (struct opl_self)` as the core knows it. A newer core may make the struct longer: check `size`
  before reading a field that a newer header adds.
- `id`: the manifest's id, for example `"opus.bitcrush"`.
- `folder`: the plugin's own folder on the OP-1's flash, `/yaffs2/mod/<id>`. Its settings live there, apart from the
  plugin's file, and stay when the plugin is removed. The core creates the folder when it first needs it (settings,
  art); create it yourself (`fw_yaffs_mkdir`) before writing your own files there.
- `slot`: the plugin's place in the core's list (its CPU charge, its row). You do not need it.

Most `opus_*` functions take the `self` pointer: keep it in a static variable.

```c
int opl_init(struct opl_self *self);
```

The plugin's entry. Called once per start, in the main thread, after the OS opened its databases and before it loads
any sound (the audio is not running yet). Register your handlers, effects, engines, folders and art here, take your
memory, read your settings. Return 0 to start.

A negative return refuses: the mods list shows "failed to start (-N)" with your number, the core removes the event
handlers you registered and leaves your places stock. Memory you took stays taken until the OP-1 switches off. Refuse
early, before you register effects or engines. Every plugin's init runs while the OP-1 starts: keep it short.

```c
static struct opl_self *me;

int opl_init(struct opl_self *self)
{
    me = self;
    opus_on_knob(self, my_knob);
    return 0;
}
```

## Events

Each `opus_on_*` function registers a handler. Handlers run after the core's own and those of the plugins loaded
before. Register them in `opl_init`.

- Key, knob, frame, main-pass and the other non-audio events keep one handler per plugin: a second call replaces the
  first. Every handler is timed and charged to its plugin (the mods list's details).
- The audio events (`opus_on_block`, `opus_on_pre_fx`, `opus_on_post_fx`, `opus_on_post_master`,
  `opus_on_block_end`) keep a list: each call adds one more handler, so call each once. The lists are built before
  the first audio block: register from `opl_init` only. When a plugin is switched off by its CPU budget, all its audio
  handlers stop.

### Keys and encoders

```c
void opus_on_key(struct opl_self *self, int (*fn)(int key, int pressed));
```

Your `fn` gets every key event in the key thread, before the OS: `key` is the key code (table above), `pressed` 1 for
down and 0 for up. Return 1 to take the event: no later handler and not the OS sees it. Return 0 to pass it on. The
FN key (the help key, code 0) comes here too, but never reaches the OS.

```c
int opus_fn_held(void);
```

1 while FN (the help key) is held, else 0. Any thread. A plugin control that needs FN held is declared
`uses INPUT fn ...` in the manifest (`nofn`: only without it).

```c
void opus_on_knob(struct opl_self *self, int (*fn)(int index, short *delta));
```

Your `fn` gets every encoder turn in the key thread, before the OS, with shift held or not. `index`: the encoder, 0 blue
to 3 orange (use `index & 3`). `*delta`: the turn, positive clockwise, in 1/1024 OS units: `*delta >> 10` is OS
units, and 4 OS units are one detent. Return 1 to take the turn: no later handler and not the OS sees it. A handler
that returns 0 may change `*delta` first; later handlers and the OS then see the changed turn.

```c
static int acc;
static int my_knob(int index, short *delta)
{
    if ((index & 3) != 2 || !fw_shift_held())
        return 0;                          /* not shift + white: the OS's */
    acc += *delta >> 10;
    int detents = acc / 4;
    acc -= detents * 4;
    /* ... use detents ... */
    return 1;
}
```

```c
void opus_on_key_after(struct opl_self *self, void (*fn)(int key, int pressed));
void opus_on_knob_after(struct opl_self *self, void (*fn)(int index, short delta));
```

After the OS's own handling, in the key thread: every key and every turn the key and knob handlers saw, taken or not
(a taken one the OS never saw), and the turn's `delta` as the OS had it. A "before" and an "after" always pair. For a
plugin that compares what the OS changed, as tape undo does.

### Frames

```c
void opus_on_frame_begin(struct opl_self *self, void (*fn)(void));
```

The start of each frame, in the display thread, before anything is drawn. No drawing here.

```c
void opus_on_frame_under(struct opl_self *self, void (*fn)(unsigned short *buf));
```

Drawing under the OS's screen: on the frame the OS just cleared, before it draws its screen and its popups, in the
display thread. Draw with the `opus_page_*` functions ([Pages](#pages)); what the OS draws there covers it. For what
goes beside or over an OS screen yet under its popups (a second column beside a list, dots on an effect page).
`buf` is the frame (see [Pixels](#pixels) for its layout).

```c
void opus_on_frame(struct opl_self *self, void (*fn)(unsigned short *buf));
```

Drawing over the OS's screen: after the OS drew its frame, popups included, in the display thread. Draw with the
`opus_page_*` functions. `buf` is the frame.

### The main pass, the clip queue and tape writes

```c
void opus_on_main_pass(struct opl_self *self, void (*fn)(void));
```

Once per pass of the OS's main loop, in the main thread, before the clip queue runs (the queue of tape commands: lift,
drop, move). The place for file work and other slow jobs, done in small steps. Nothing that waits long.

```c
void opus_on_queue_after(struct opl_self *self, void (*fn)(int ran));
```

After the clip queue's run, once per main pass, in the main thread. `ran` is nonzero when a queued command ran (a
lift, a drop, a move), 0 when none did or the run was held.

```c
void opus_queue_hold(void);
```

From a main-pass handler: this pass's clip queue run waits for the next pass (the queue-after handlers hear `ran` 0).
For a plugin that must look at the tape twice without a queued command running in between.

```c
void opus_on_tape_write(struct opl_self *self, void (*fn)(int fd, const void *buf, unsigned n));
```

Before each of the OS's writes of tape audio, in the main thread: `fd` the tape file, `buf` the bytes, `n` their
count. The tape still holds what the write will overwrite, so it can be read first (tape undo keeps it).

### Saving

```c
void opus_on_save_pass(struct opl_self *self, int (*fn)(void));
```

Once per main pass, in the main thread, where the OS runs its hold-to-save check (holding a sound key saves the
sound as a preset). Return 1 to take the pass (the OS's check does not run this pass), 0 to leave it to the OS. For a
plugin with its own way of saving.

### Audio events

```c
void opus_on_block(struct opl_self *self, void (*fn)(void));
```

Once per audio block, in the sequencer thread (not in the audio interrupt), before the OS hands out its MIDI. Timed
and charged to the plugin as thread time, not counted in its audio budget; stopped with the plugin's audio when it is
switched off. The place for work that must keep pace with the audio but may take a little longer, such as preparing
an effect instance (see `opus_fx_new`).

```c
void opus_on_pre_fx(struct opl_self *self, void (*fn)(int instrument, short *buf));
```

In the audio block, before the instrument's effect slot: after the voices' sum and the OS's own filtering. Only the
synth's (instrument 0) at present. `buf`: the instrument's 64 samples, changed in place.

```c
void opus_on_post_fx(struct opl_self *self, void (*fn)(int instrument, short *buf));
```

In the audio block, after the instrument's effect slot, whether the effect is on or off. `instrument`: 0 the synth,
1 the drum, 2 another of the OS's sources. `buf`: the instrument's 64 samples, changed in place.

```c
void opus_on_post_master(struct opl_self *self, void (*fn)(short *l, short *r));
```

In the audio block, after the master effect slot, on or off: the master bus, `l` and `r`, 64 samples each, changed in
place.

```c
void opus_on_block_end(struct opl_self *self, void (*fn)(void));
```

In the audio block, after every rendered block: the output is complete.

```c
void opus_skip_dry_input(void);
```

Skips the OS's own dry input for this block. An engine that plays the audio input itself (an amp engine) calls it
every block from its block handler, inside the audio interrupt only (a block rendered from a thread has the last
interrupt's input), so the input is heard once.

## Quick pages

A quick page is a plugin's own page shown in place of the screen, opened from its key handler (for example a scale
picker over the synth's T1). While it shows, the four encoder turns go to the plugin and the OS sees none. The notes,
shift, left and right, the transport (rec, play, stop), FN and the four pushes pass on as usual (a push is the
plugin's own to switch pages or close with); any other key closes the page and goes on. Opus's menu, when open, comes
first.

```c
void opus_on_quick(struct opl_self *self, void (*draw)(int page), int (*knob)(int page, int encoder, int steps));
```

Registers the quick page's handlers; call it in `opl_init`, before any `opus_quick_show`.

- `draw(page)`: the display thread, every frame while the page shows, on a cleared (black) frame. Draw with the
  `opus_page_*` functions. `page` is the number you gave `opus_quick_show`.
- `knob(page, encoder, steps)`: the key thread, for each turn of at least one detent: `encoder` 1 blue, 2 green,
  3 white, 4 orange; `steps` in detents, positive clockwise. The return value is not used.

```c
void opus_quick_show(struct opl_self *self, int page);
```

From the key handler: `page` 0 or more opens the quick page or switches it to that page; -1 closes it (only when it is
this plugin's). Nothing happens without `opus_on_quick` first.

```c
int opus_quick_page(struct opl_self *self);
```

The page showing, or -1 when none of this plugin's shows. Any thread.

## Files

The OP-1's flash holds a file system (paths start with `/yaffs2/`). The user's audio lives in `/yaffs2/user/audio/`,
Opus's and the plugins' files in `/yaffs2/mod/`. File functions take the file system's lock: call them from the main
thread or the key thread, never from the display thread or the audio block.

```c
#define OPUS_RENAME_OK          0
#define OPUS_RENAME_BAD        (-1)
#define OPUS_RENAME_NOT_FOUND  (-2)
#define OPUS_RENAME_EXISTS     (-3)
#define OPUS_RENAME_REFUSED    (-4)
#define OPUS_RENAME_FAILED     (-5)
int opus_rename(const char *old_path, const char *new_path);
```

A real rename of a file or folder, which the firmware's own calls lack. It writes one new header page; the data stays
where it is, so it costs about the same at any size (a copy of a large sample costs hundreds of times more). A power
cut leaves exactly one name, the old or the new. It may also move the object into another folder.

Both paths must lie in `/yaffs2/user/audio/` or `/yaffs2/mod/`, and every part after that folder must be a plain name
(1-64 characters, not `.` or `..`, a single `/` between parts). The target folder must exist. Returns:

- `OPUS_RENAME_OK` (0): renamed, or it already had that name;
- `OPUS_RENAME_BAD`: a path outside those folders, or not plain names;
- `OPUS_RENAME_NOT_FOUND`: no such file, or no such target folder;
- `OPUS_RENAME_EXISTS`: the target folder already has that name;
- `OPUS_RENAME_REFUSED`: a special folder, a link, a read-only disk, or a folder moved into itself;
- `OPUS_RENAME_FAILED`: the header write failed; nothing changed.

```c
int r = opus_rename("/yaffs2/mod/opus.dx/bank.tmp", "/yaffs2/mod/opus.dx/bank.syx");
```

```c
int opus_bgfile_idle(void);
```

1 when the OS's background file job is idle (its queue empty, no job running), else 0. A preset loading or a sound
slot being written is such a job; wait for idle before changing the files it uses. Any thread.

```c
int opus_flash_headroom(void);
```

The bytes of the flash a plugin may still write: the free space less what the tape, the sounds and a safety margin
will need. It may be negative. Keep your files inside it. Main or key thread (it takes the file system's lock).

**Reading a file yourself** (with the firmware's `fw_yaffs_open` and `fw_yaffs_read`): read into a buffer aligned to
32 bytes, `static unsigned char buf[N] __attribute__((aligned(32)));`. Reading a whole flash page into a buffer at an
odd address has stopped the OP-1.

## Journals

A journal is the core's recovery service: a change to the flash's files and the user's database in several steps,
handed to the core, which writes it down, carries the steps out one at a time, strikes each off once done and removes
the journal after the last. Whatever is left of a journal the core finishes at the next start, before any plugin loads
and before the OS reads a preset, whether the plugin loads or not. So a power cut never leaves the change half done
(a preset row naming a missing file would stop the OS when it loads).

The steps are text: one step a line, each line ending in `\n`, the step's word and each of its fields parted by a tab
(`\t`); names may hold spaces, never a tab. Then a last line `end\n`, with nothing after it.

| Step | Meaning |
|---|---|
| `rename A B` | A becomes B, when A is there and B is not (`opus_rename`; if refused, a copy, then A removed) |
| `copy A B` | B written whole from A, when A is there (and the flash's headroom holds it) |
| `unlink A` | A removed, when it is there |
| `sql S` | the SQL statement S run on the user's database (`op1.db`); S must not contain `%` |

Paths only in `/yaffs2/user/audio/` and `/yaffs2/mod/` (the same rules as `opus_rename`). Each step either finds its
condition false when run again or does no harm, so a step interrupted by a power cut can run again safely.

```c
#define OPUS_JOURNAL_MAX 2048
```

The longest journal, in bytes, `end\n` included.

```c
int opus_journal_run(struct opl_self *self, const char *steps);
```

Writes the journal down, then carries it out. Returns the number of steps done: all of them means the change is made.
Fewer means one step failed: that step and the rest stay in the journal for the next start (the plugin may instead
undo what was done and call `opus_journal_done`). -1 means the journal was not taken and nothing was done: a step the
core does not know or allow, too long, or the write failed (the boot log says so). Main thread.

```c
static const char steps[] =
    "rename\t/yaffs2/user/audio/synth/old.aif\t/yaffs2/user/audio/synth/new.aif\n"
    "end\n";
int done = opus_journal_run(me, steps);
```

```c
int opus_journal(struct opl_self *self, const char *steps);
```

Only writes the journal down, without running it: 1 written, 0 refused (the same checks as above). For what undoes the
plugin's own work in progress: if the OP-1 loses power before the work is done, the next start runs the journal. Each
plugin has one journal: a later `opus_journal` or `opus_journal_run` replaces it. Main thread.

```c
void opus_journal_done(struct opl_self *self);
```

Removes the plugin's journal (its work finished). Main thread.

## Presets

```c
extern unsigned char opus_presets[];
```

The synth's sound presets as the preset browser lists them: the OS's own array, which the core holds (with room for
385 entries). Each entry is 0x38 bytes; factory rows come first, then the user's. For a plugin that renames or
overwrites a preset. The entry layout and the count are firmware #246's own data (for example +0x04 the preset's
JSON value, +0x14 its folder name, +0x30 a byte that is 1 for a factory preset): this is deep work, see
[Firmware functions](#firmware-functions-fw_).

## Memory

```c
void *opus_alloc(struct opl_self *self, unsigned bytes);
```

Memory from the core's pool: 32-byte aligned, cleared to zero, the plugin's for this start. It is never freed: take
what you need once (in `opl_init`) and keep it. Returns 0 when the pool has no room left: go on without it, or refuse
from `opl_init`. The bytes count in the plugin's memory on the mods list. Any thread, but not the audio block.

```c
void *opus_fast(struct opl_self *self, unsigned bytes);
```

Fast memory: the CPU's L1 data memory, which costs no cache misses. 32-byte aligned, **not** cleared, never freed.
There is about 25 KB for all plugins together, first come, first served; returns 0 when none is left (then keep using
a pool buffer). Only from `opl_init`. Worth it for what an audio block touches every time (a short delay line, a
block's settings): each 32-byte line of ordinary memory an audio block touches costs about 107 cycles a block on the
device. Each call writes a line to the boot log.

## CPU and the log

```c
void opus_cpu_charge(struct opl_self *self, unsigned cycles);
```

Charges `cycles` of audio work to the plugin, for the mods list and its CPU budget: for work the OS runs for the
plugin outside the core's events (a function the plugin's place hooks into the audio code). Read the CPU's cycle
counter before and after, and charge the difference. Any thread, the audio block included.

```c
void opus_log(struct opl_self *self, const char *text);
```

Adds a line to the boot log, prefixed with the plugin's id. The log holds 2,048 bytes for the core and all plugins;
lines past that are dropped, so log what helps, not every frame. The log is read over the OP-1's USB dev port, whose
tools are not in this kit yet; until then, show what you need on the plugin's panel or page.

## Settings

A plugin's settings are declared in its manifest (`setting` lines, see manifest.md). The user changes them on the
mods list's settings page (select the plugin's row, push blue; green changes the selected setting). The core keeps
the values in the plugin's folder, apart from its file, and reads them before `opl_init`.

```c
int opus_setting(struct opl_self *self, const char *name);
```

A setting's value now: the number for a number setting, the index (0 the first) for a choice. 0 for a name the
manifest does not offer. From `opl_init` on, any thread.

```c
int depth = opus_setting(me, "depth");     /* setting depth 1 16 8 "depth" */
```

```c
void opus_on_setting(struct opl_self *self, void (*fn)(const char *name, int value));
```

Your `fn` is told each change the user makes on the settings page (with the green encoder), in the key thread:
the setting's name and its new value (a choice's index). It is not told the plugin's own changes
(`opus_set_setting`).

```c
int opus_set_setting(struct opl_self *self, const char *name, int value);
```

The plugin's own change of a setting (a control of its own sets it), from any thread: the value is kept inside the
setting's range, shown on the settings page, and written to the plugin's folder once the changes have been quiet for
2 seconds (and at a clean power-off). The plugin's `opus_on_setting` handler is not told. Returns 1, or 0 for a name
the manifest does not offer.

```c
void opus_on_settings_rows(struct opl_self *self, int (*fn)(int i, const char **label, char *value));
```

Rows of the plugin's own on its settings page, after the manifest's settings: read-only lines that show what the
plugin holds now (for example a list of what it found). Needs core 2.7.

Your `fn(i, &label, value)` runs in the key thread as the page opens, once for each row `i` from 0: point `*label` at
row `i`'s name (in the plugin's own memory, kept while the page shows), write its value into `value` (up to 39
characters and the terminating 0) and return 1; return 0 past the last row. Keep it short. The page holds 40 rows in
all, the manifest's settings first. The green encoder changes only the manifest's settings.

## Switched places

A place is a plugin's hook into the firmware's code, declared in the manifest (`place` lines, see manifest.md). The
core checks and installs places at load. A switched place (`place ... as NAME switched`) is checked and held at load
but left stock until the plugin turns it on, so a setting can switch a hook live. Needs core 2.1.

```c
int opus_place_set(struct opl_self *self, const char *name, int on);
```

Turns the plugin's switched place `name` on (installs it, if its bytes are still the stock ones) or off (writes the
stock bytes back), at once. From `opl_init` or any thread. Returns 1, or 0 for a name the plugin does not hold or
bytes that are no longer stock.

```c
opus_place_set(me, "loop_move_left", opus_setting(me, "looplock"));
```

## Folders on the USB disk

The OS empties its USB disk at every start and every disk mode and puts back only its own content. A plugin that wants
files from the user's computer (sound banks, pictures) asks the core for a folder: the files live on the flash in
`/yaffs2/mod/NAME`, and while the OP-1 is in disk mode the USB disk shows a copy in `/mods/NAME`.

At disk mode's start the core copies the folder's files out to the disk. At its end, each file the computer left
there that is new or changed (with the folder's ending, not empty, at most `max` bytes) is copied whole to a staging
file and handed to the plugin's `take`, which keeps it in its own form or refuses it; a file deleted on the computer
is deleted from the folder; then, if anything changed, `changed` runs. Up to 64 files a folder, names up to 63
characters. Names starting with `.`, files without the ending, subfolders, hidden and system files are left alone.

```c
#ifndef OPUS_FOLDER_DEFINED
#define OPUS_FOLDER_DEFINED
struct opus_folder {
    const char *name;
    const char *ext;
    unsigned max;
    int (*take)(const char *file, const char *incoming);
    void (*changed)(void);
};
#endif
```

`OPUS_FOLDER_DEFINED` only keeps the struct from being defined twice.

- `name`: the folder's name, 1-8 characters of `a-z`, `0-9`, `_` and `-` (the disk's short names). `"dx"` means
  `/yaffs2/mod/dx` on the flash and `/mods/dx` on the disk. It is not the plugin's own folder `/yaffs2/mod/<id>`.
- `ext`: the files' ending, for example `".syx"` (upper or lower case alike), or 0 for any file.
- `max`: the largest file taken from the disk, in bytes.
- `take(file, incoming)`: the key thread, at disk mode's end. `file` is the file's name (no path), `incoming` the
  staging file's full path, which holds the computer's file. Write `/yaffs2/mod/NAME/<file>` yourself in your own form
  and return 1 (the folder changed), or return 0 to refuse it. The core deletes the staging file afterwards.
- `changed()`: the key thread, after the files changed (taken or deleted), or 0 for none.

```c
int opus_on_folder(struct opl_self *self, const struct opus_folder *f);
```

Asks for the folder. From `opl_init`. The struct stays the core's to read: make it `static const`. Returns 1, or 0
refused (the boot log says why): a bad name, a name another part has, or no room (eight folders in all, the core
itself uses two).

```c
static int take(const char *file, const char *incoming) { /* convert, write, return 1 */ return 1; }
static const struct opus_folder banks = { "dx", ".syx", 8192, take, 0 };
/* in opl_init: */ opus_on_folder(self, &banks);
```

## The panel

A plugin's panel is the right side of the mods list while the plugin's row is selected. Without a panel of its own,
the core shows the plugin's version, memory and audio CPU there. Below the panel, on the screen's last line, the core
says what each push opens.

```c
#define OPUS_PANEL_X0   168
#define OPUS_PANEL_X1   312
#define OPUS_PANEL_Y0   28
#define OPUS_PANEL_Y1   134
```

The panel's area in the 320 x 160 drawing: x 168-312, y 28-134 (inclusive).

```c
void opus_on_panel(struct opl_self *self, void (*draw)(void));
```

Your `draw` runs in the display thread when the mods list is drawn with the plugin's row selected (on a change, and
twice a second). Draw with the `opus_panel_*` functions below, which keep to the panel; call them only from `draw`.

```c
void opus_on_panel_knob(struct opl_self *self, int (*knob)(int encoder, int steps));
```

While the plugin's row is selected, your `knob` gets the green, white and orange turns (blue moves the list) in the
key thread: `encoder` 1 green, 2 white, 3 orange; `steps` in OS units (4 per detent), positive clockwise. Return 1 to
have the panel drawn again.

```c
void opus_panel_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
```

Text in the OP-1's font. Each plugin has 8 text slots (0-7); a slot keeps the place and size of its first call, and
each call sets its colour and text, so use one slot for each place on the panel. `x`, `y`: the text's top left, in
units; `size`: its height, 6-24; the text must start and end inside the panel (x from `OPUS_PANEL_X0` to
`OPUS_PANEL_X1 - 4`, y from `OPUS_PANEL_Y0`, `y + size` at most `OPUS_PANEL_Y1 + 2`), else it is not drawn. The
panel's text is shown in lower case, and characters the font lacks (`$ ' ( ) @ [ \ ] ^ { | } ~` and the backquote)
are left out; `&` shows as `+`. `colour`: an id ([Colours](#colours)).

```c
void opus_panel_fill(int x0, int y0, int x1, int y1, int colour);
void opus_panel_frame(int x0, int y0, int x1, int y1, int colour);
void opus_panel_bar(int x0, int y0, int x1, int y1, unsigned value, unsigned max, int colour);
```

A filled rectangle; its one-pixel outline; a bar lit from the left (`value` of `max` of its width in `colour`, the
rest dark grey). Corners inclusive, in units, cut to the panel. `colour`: an id.

```c
opus_panel_text(me, 0, 172, 30, 9, OPUS_COLOR_DIM, "level");
opus_panel_bar(172, 72, 300, 80, (unsigned)level, 16, OPUS_COLOR_E2);
```

## Colours

A plugin names a colour by what it is for, not by its value: the user's colour choices and themes decide what the
core draws. Every `colour` argument of the panel, page and effect-page functions takes these ids.

```c
#define OPUS_COLOR_E1   (-10)
#define OPUS_COLOR_E2   (-11)
#define OPUS_COLOR_E3   (-12)
#define OPUS_COLOR_E4   (-13)
#define OPUS_COLOR_E(i) (-10 - (i))
#define OPUS_COLOR_TEXT 14
#define OPUS_COLOR_DIM  12
#define OPUS_COLOR_ALERT 2
#define OPUS_COLOR_BACK 1
#define OPUS_COLOR_OFF  4
```

- `OPUS_COLOR_E1` to `OPUS_COLOR_E4`: each encoder's colour now (stock: blue, green, the white encoder's lavender, the
  orange encoder's red-orange); `OPUS_COLOR_E(i)` the same for encoder `i`, 0-3.
- `OPUS_COLOR_TEXT`: values and names (white).
- `OPUS_COLOR_DIM`: labels, what is off or unavailable (grey).
- `OPUS_COLOR_ALERT`: refusals and warnings (the palette's red, not the fourth encoder's).
- `OPUS_COLOR_BACK`: under text, the background (black).
- `OPUS_COLOR_OFF`: unlit things: an off segment, tab or light (the palette's off-grey).

Other numbers from 0 to 19 pick the OS palette's colours; any other value draws white. Lines, dots, strokes and fills
also take a colour of the plugin's own, `OPUS_RGB(0xRRGGBB)` ([Paths](#paths)).

```c
#define OPUS_ACCENT(i)  OPUS_COLOR_E(i)
#define OPUS_WHITE      OPUS_COLOR_TEXT
#define OPUS_GREY       OPUS_COLOR_DIM
#define OPUS_RED        OPUS_COLOR_ALERT
#define OPUS_BLACK      OPUS_COLOR_BACK
```

Older names for the same ids. They work; new code uses the `OPUS_COLOR_*` names.

```c
#define OPUS_ACCENT_SET 0x01000000u
unsigned opus_accent(int i);
```

The colour the user chose for encoder `i` (0 blue, 1 green, 2 white, 3 orange): `OPUS_ACCENT_SET | 0xRRGGBB`, or 0
while it is stock. Any thread.

```c
unsigned opus_accent_now(int i);
```

Encoder `i`'s colour as it shows now, the user's choice or stock's: `0xRRGGBB` (0 for `i` outside 0-3). For a page
that colours its own pixels. Any thread.

## Effects

The core's effect service lets a plugin add effect types to the OS's own effect list: the synth, the drum and the
master each choose one, like a stock effect, and each has its own page. A sound that names a plugin's effect loads
with it while the plugin is there, and a sound saved with it is stamped with the plugin's id; without the plugin, the
sound loads with its effect switched off.

An effect type says what one instance needs; the core allocates the memory for every instance anyone runs (the OS's
three slots, and any manager's, see [Managers](#managers)). So a type keeps nothing of an instance in its own static
variables and never knows who runs it. Data every instance shares (tables) may stay the plugin's, made once in
`opl_init`.

```c
struct opus_need {
    unsigned state;
    unsigned bulk;
    unsigned fast;
};
```

What one instance needs, filled in by the type's `need` call:

- `state`: bytes of ordinary memory, cleared: the instance's variables.
- `bulk`: bytes of ordinary memory, cleared: delay lines, a reverb's tank.
- `fast`: bytes wanted in fast memory (as `opus_fast`), else ordinary memory. Not cleared.

The OS's synth and drum slots never run in the same block, so their two instances share one `bulk` and one `fast`
area (each has its own `state`): keep in `state` whatever says where you are in them (a write position).

```c
struct opus_fx {
    void *state, *bulk, *fast;
    int stereo;
};
```

An instance as the type's calls get it: its memory (0 where the need was 0), and `stereo` 1 for the master's kind,
0 for mono (synth, drum).

```c
#define OPUS_FX_WAVE    128
struct opus_fx_view {
    const char *label[4], *value[4];
    const short *wave[3];
    int waves;
    const char *caption;
};
```

The effect's page, as its `view` call fills it; the core draws it like a stock effect's page:

- `label[i]` over `value[i]` in the corners, in the encoders' order: blue top left, green bottom left, white top
  right, orange bottom right; labels small in the encoder's colour, values large in white.
- `wave[0..waves-1]`: a picture in the middle, up to three lines of `OPUS_FX_WAVE` (128) samples each, -32768 to
  32767 drawn around the middle line.
- `caption`: a line under the picture, in blue, or 0.

The core draws after `view` returns: every string and wave must stay valid then (static buffers, not local arrays).
When the effect is switched off, the OS's own "off" overlay covers the page.

```c
struct opus_effect {
    unsigned size;
    const char *name;
    short defaults[8];
    short limits[8][3];
    void (*need)(int stereo, struct opus_need *n);
    void (*init)(struct opus_fx *fx);
    void (*mono)(struct opus_fx *fx, short *buf, const short *par);
    void (*stereo)(struct opus_fx *fx, short *l, short *r, const short *par);
    void (*view)(const short *par, struct opus_fx_view *v);
    int (*shift_knob)(short *par, int encoder, int detents);
};
```

An effect type. An effect has eight parameters (`short` each); the four encoders turn parameters 0-3 and the OS saves
all eight with the sound.

- `size`: `sizeof (struct opus_effect)`.
- `name`: as sounds and the effect list hold it: lower case, up to 15 characters.
- `defaults`: its eight parameters when the user chooses it.
- `limits`: `{min, max, step}` for each parameter. With step 0, no encoder or LFO moves it.
- `need(stereo, n)`: fills in one instance's needs, mono (`stereo` 0) or the master's (1). Pure and cheap: it runs
  whenever an instance is made.
- `init(fx)`: builds or resets the instance in its memory. It runs at every call of the OS's effect factory (a sound
  loading, the instrument or the slot changing), sometimes inside the audio interrupt: quick, nothing that waits or
  allocates.
- `mono(fx, buf, par)`: the synth's or the drum's sound: 64 samples in `buf`, in place, in the audio block. `par`: the
  eight parameters as the LFO moved them.
- `stereo(fx, l, r, par)`: the master's: 64 samples each in `l` and `r`, in place, in the audio block.
- `view(par, v)`: fills the page (above), in the display thread, every frame while the page shows. `par`: the eight
  parameters, as the LFO moved them when recent.
- `shift_knob(par, encoder, detents)`: the shift layer, or 0 for none. On the effect's own page with shift held and
  the effect on, each turn comes here first, in the key thread: `encoder` 0 blue, 1 green, 2 white, 3 orange;
  `detents` (0 when the turn was less than one). Set the parameters you want in `par` (often 4-7, which no encoder
  turns otherwise) and return 1; the turn then goes on to the OS with no movement, so the OS changes nothing but saves
  the sound's eight parameters. Return 0 to leave that encoder the OS's own shift behaviour (fine steps).

The process calls (`mono`, `stereo`) are timed and charged to the plugin; when it is switched off by its CPU budget,
the sound passes through unchanged.

```c
int opus_effect_register(struct opl_self *self, const struct opus_effect *e);
```

Registers an effect type, from `opl_init`. The struct must stay valid: make it `static const`. Returns its type number
(8-15, in the order this start registers them; sounds store the name, so never store the number), or:

- -1: a bad struct (`size` below `sizeof (struct opus_effect)`, no name) or no room (8 plugin types in all);
- -2: no memory in the pool for the OS's slots' instances;
- -3: the effect list has no row for it;
- -4: the effect service could not start (a plugin loaded before holds one of its places; the boot log names it).

The OS's three slots' instances (synth and drum sharing `bulk` and `fast`, and the master's) are made now from the
plugin's memory, fast memory going to them first.

```c
int opus_effect_live(int instrument, short par[8]);
```

The parameters of instrument 0 (synth) or 1 (drum) as its effect last heard them (the LFO's copy), into `par`, for a
page. Returns 1 when that copy is recent (within the last 8 blocks), else 0. For another instrument it returns 0 and
leaves `par` alone. Any thread.

## Hidden stock effect and engine

The OS has an effect and a synth engine it does not list. A plugin may bring them back into the lists while it is
loaded. Sounds using them play either way: the OS knows them.

```c
int opus_effect_show(struct opl_self *self, const char *name, const short defaults[8]);
```

Lists the hidden stock effect `name` in the effect list with these default parameters, while the plugin is loaded.
Only `"filter"` (the OS's type 5) is hidden. From `opl_init`. Returns 1, or 0 for a name the OS does not hide.

```c
int opus_engine_show(struct opl_self *self, const char *name, const short defaults[8]);
```

Lists the hidden stock synth engine `name` in the engine list, with these knobs for a new sound, while the plugin is
loaded. Only `"iter"` is hidden. `defaults[4]` must be below 0x100 (knob 4 above that marks a plugin engine's sound).
From `opl_init`. Returns 1, or 0 (a name the OS does not hide, a mark in knob 4, or the engine service could not
start). Needs core 2.5.

## Another firmware's effects

For a plugin that hosts effects written for another OP-1 firmware, which carries them inside the OS's filter type
(5) with a mark in parameter 4. Any number of them fit beside the 16 types. A hosted effect is not a type managers
run. Needs core 2.6.

```c
struct opus_hosted_effect {
    unsigned size;
    const char *name;
    int mark;
    short defaults[8];
    void (*init)(int pos);
    void (*render)(int pos, short *buf, const short *par);
    void (*render_stereo)(int pos, short *l, short *r, const short *par);
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    int (*key)(int pos, int key, int pressed);
    int (*turn)(int pos, short *par, int encoder, int detents);
};
```

- `size`: `sizeof (struct opus_hosted_effect)`.
- `name`: what sounds and the effect list hold (`"clouds"`): such a sound loads as the filter with this mark.
- `mark`: 0x5301-0x53FF to claim the number the other firmware uses, or 0 for one the core makes from the plugin's id
  and the name.
- `defaults`: its row's parameters (`defaults[4]` is the core's: the mark).
- `init(pos)`: at the effect's first block at position `pos` after a change (0 synth, 1 drum, 2 master); audio block.
- `render(pos, buf, par)`: 64 samples in place, in the audio block; timed and charged. Required.
- `render_stereo(2, l, r, par)`: the master's, or 0: the master then runs `render` on the mix's middle and adds back
  what it changed.
- `svg`, `screen_init`, `screen_draw`: its own OS screen in the filter's screen slot while its page shows, as an
  engine's (see `struct opus_engine`), or 0.
- `key(pos, key, pressed)`: the pushes 0x3B-0x3E on its page, key thread; return 1 to take one.
- `turn(pos, par, encoder, detents)`: a turn on its page, key thread; `par` the page's parameters, written in place;
  return 1 to take it.

```c
int opus_effect_host(struct opl_self *self, const struct opus_hosted_effect *f);
```

Adds a hosted effect, from `opl_init`. Returns its mark, or -1 (a bad struct, a claimed mark outside 0x5301-0x53FF, or
no room: 32 in all), -3 (its name or mark taken), -4 (the effect service could not start).

## Managers

Any plugin may be a manager: it runs instances of any effect type (the OS's eight and every plugin's) wherever it
wants, without the type knowing (for example a second effect behind every slot). A type is a number: 0-7 the OS's,
8-15 plugins'. Sounds store types by name. Needs core 2.2.

```c
#define OPUS_KIND_EFFECT 0
#define OPUS_FX_SYNTH   0
#define OPUS_FX_DRUM    1
#define OPUS_FX_MASTER  2
#define OPUS_FX_OWNERS  17
#define OPUS_FX_MASTER_OWNER 16
typedef unsigned opus_handle;
```

- `OPUS_KIND_EFFECT`: the kind `opus_types` lists.
- `OPUS_FX_SYNTH`, `OPUS_FX_DRUM`, `OPUS_FX_MASTER`: where an instance runs (mono for the synth or the drum, stereo
  for the master).
- `OPUS_FX_OWNERS`: the number of effect slot owners: 0-7 the synth's eight sound slots, 8-15 the drum's, 16
  (`OPUS_FX_MASTER_OWNER`) the master.
- `opus_handle`: an instance; 0 is none. A freed instance's handle goes stale and does nothing.

```c
struct opus_type_info {
    const char *name;
    short type, defaults[8];
    short limits[8][3];
};
```

A type as `opus_types` lists it: its name, its number, its default parameters and each parameter's
`{min, max, step}` (valid once `opus_fx_manager` ran).

```c
int opus_fx_manager(struct opl_self *self);
```

Makes the plugin a manager: installs the service's places (the effect pages). From `opl_init`. Returns 1, or -4 when
another plugin holds one of its places.

```c
int opus_types(int kind, struct opus_type_info *out, int max);
```

Every type of `kind` (only `OPUS_KIND_EFFECT`), sorted by name as the effect list sorts them, with its defaults and
limits, into `out` (at most `max` entries; 16 cover all). Returns the count.

```c
opus_handle opus_fx_new(struct opl_self *mgr, int type, int where, const char **why);
```

A new instance of `type` for `where` (`OPUS_FX_SYNTH`, `OPUS_FX_DRUM` or `OPUS_FX_MASTER`), made and initialised now,
in the caller's thread, its memory the manager's. Call it from the main thread, the key thread or the sequencer
thread, never the audio block. A stock type's init can be slow (grid's takes 1.65 M cycles): a block handler
(`opus_on_block`) is a good place to ask. Up to 16 instances live at once.

Returns the handle, or 0 with a short reason in `*why` (`why` may be 0): `"not a type"`, `"too many instances"`,
`"no room"`, `"no room for its line"`, `"this type runs once more at most"`, `"the master runs this type once more at
most"`, or `"held back: the master's first is the same"`. The OS's own effects share memory between instances, so
beside the OS's slots a stock type runs at most one more mono and one more stereo instance; a master grid, phone or
spring is held back while the master's first effect is the same type (`opus_fx_held`: ask again later). A plugin's
type runs in any number, memory allowing.

```c
void opus_fx_free(opus_handle h);
```

Frees the instance (its memory is kept for the type's next instance). Same threads as `opus_fx_new`.

```c
void opus_fx_reset(opus_handle h);
```

Clears the instance's state, as when it was made. Same threads as `opus_fx_new`.

```c
void opus_fx_run(opus_handle h, short *l, short *r, const short *par);
```

Runs the instance on 64 samples in place, from the manager's audio handler: a mono instance from a `post_fx` handler
(`l` the instrument's buffer, `r` 0), the master's from a `post_master` handler (`l` and `r`). `par`: eight
parameters, kept inside the type's limits. Does nothing for a stale or freed handle, a held-back instance, a mono
stock instance whose instrument does not play this block, or a type whose plugin is switched off by its CPU budget.
Its time is the manager's: a manager whose manifest says `cpu user` is measured but never switched off (the user chose
the effects and hears an overload).

```c
int opus_fx_type_of(opus_handle h);
```

The instance's type, or -1 for a stale handle. Any thread.

```c
int opus_fx_held(int type, int where);
```

1 when an instance of `type` at `where` would be held back now (a master grid, phone or spring while the master's
first effect is the same type), else 0.

```c
int opus_fx_owner(void);
```

The owner of the effect page or effect list showing now: 0-7 the current synth slot, 8-15 the drum's (8 + slot), 16
the master.

```c
int opus_instrument_now(void);
```

The instrument the audio block plays: 0 synth, 1 drum. Inside the audio block, the instrument of this block.

```c
int opus_sound_slot(int instrument);
```

The current sound slot, 0-7, of instrument 0 (synth) or 1 (drum).

```c
int opus_fx_list_shown(void);
int opus_fx_page_shown(void);
```

1 while the effect list shows (the browser with the effect types), and 1 while an effect page shows (any type's).

```c
struct opus_fx_shown {
    short type;
    volatile short *par;
    volatile short *on;
    opus_handle h;
};
int opus_fx_pages(struct opl_self *mgr, int (*shown)(int owner, struct opus_fx_shown *v), void (*changed)(int owner));
```

An effect page that shows the manager's instance in place of the OS's effect. The core asks your `shown(owner, v)`
whenever it needs to (any thread but the audio block; keep it cheap). Return 1 with `v` filled in:

- `type`: the type the page shows;
- `par`: the manager's eight parameters for it;
- `on`: the manager's on flag (1 on);
- `h`: the instance running it, for the page's meters, or 0.

The page then shows that type's screen (the OS's own for a stock type) reading the manager's parameters and on flag;
every turn there changes them as the OS changes its own (a plugin type's shift layer too), T3 switches the manager's
effect on and off alone, and after each change your `changed(owner)` runs in the key thread, so the manager keeps the
change. Return 0 to let the OS's own effect show. Up to 4 managers; returns 1, or 0 when there is no room.

```c
struct opus_fx_record {
    char type[16];
    short on;
    short par[8];
};
int opus_fx_records(struct opl_self *self, const char *prefix, void (*fn)(int owner, const struct opus_fx_record *r));
int opus_fx_record_put(struct opl_self *self, int owner, const struct opus_fx_record *r);
```

A manager's records in sounds: for each sound slot, an instance's type by name, its on flag and its eight parameters,
kept in the sound's `"opus"` member under the manager's keys `PREFIX_type`, `PREFIX_active` and `PREFIX_params`. So
they go with the sound through the OS's save, the disk and every copy.

- `struct opus_fx_record`: `type` the type's name (`""` for none), `on` 1 on, `par` the eight parameters.
- `opus_fx_records(self, prefix, fn)`: from `opl_init`, once per manager; `prefix` 1-7 characters of `a-z` and `0-9`.
  Your `fn(owner, r)` is told each slot's record as its sound loads (`owner` 0-15; `type` `""` for none; a parameter
  the sound does not keep is -32768). With the prefix `fx2` it is also told once, as owner 16, the master's record
  that Opus 1 kept; the master's record is otherwise the manager's to keep, in its own folder. Up to 4 managers.
  Returns 1, or 0 for a bad or taken prefix.
- `opus_fx_record_put(self, owner, r)`: the key thread, at a change: writes slot `owner`'s (0-15) record into its
  sound and saves the slot as the OS does after a knob turn. Returns 1, or 0 (no records registered, a bad owner, no
  sound in the slot).

A type this start does not know comes by its name: keep the record, play nothing, and write it back as it was.

## Engines

The core's engine service adds synth engines to the OS's engine list (shift + T1). An engine has its own T1 page (and
T2 and T4 pages if it wants them). Its sounds are the OS's `iter` engine with the engine's mark in knob 4, so they
save, load and copy as any sound; the sound's `"opus"` member names the plugin. A sound whose engine is not loaded does
not load: the current sound plays on and a notice names the plugin it needs.

The OS plays six voices. Around each voice's render it does the envelope (T2), the velocity and the mix, so an engine
renders one note's raw sound.

The kit's `examples/opus.outatime` is a complete engine: its voices, a `block` handler, a T1 page drawn over SVG art
(see [Art](#art)), a `turn` handler, `opus_engine_live` and `opus_engine_knobs` for its page, and `opus_clock`.

```c
struct opus_engine {
    unsigned size;
    const char *name;
    short knobs[8];
    unsigned voice;
    void (*init)(void *voice, int index);
    void (*note_on)(void *voice, const short *knobs);
    void (*release)(void *voice, const short *knobs);
    void (*render)(void *voice, short *out, const short *par, const unsigned *pitch);
    void (*block)(short *buf);
    void (*page)(void);
    void (*page_t2)(void);
    int (*turn)(int encoder, int detents, int page);
    int (*key)(int key, int pressed, int page);
    unsigned flags;
    void (*page_t4)(void);
    int (*finished)(void *voice);
    const char *type;
    int mark;
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
};
```

An engine. The struct starts with its size; the fields from `flags` on are optional (a shorter struct has none of
them).

- `size`: `sizeof (struct opus_engine)`.
- `name`: the engine list's label and a new sound's name: lower case, up to 15 characters.
- `knobs`: a new sound's eight knobs, 0-32767. Knob 4 is the core's (the mark): its value here is not used.
- `voice`: bytes of state per voice (six voices). They come from fast memory while it lasts, else from the pool.
- `init(voice, index)`: the OS built the voice again (an instrument or slot change, every sound change; the audio
  interrupt or a thread). Make it silent, ready for a note. The state's bytes are as the engine left them, or anything
  at the first build. `index`: the voice, 0-5.
- `note_on(voice, knobs)`: a note starts (key thread; a retrigger: the audio interrupt). `knobs`: the sound's eight.
- `release(voice, knobs)`: the voice is let go (key thread; a retrigger: the audio interrupt).
- `render(voice, out, par, pitch)`: a busy voice's block, in the audio block: 128 samples at 88.2 kHz into `out`
  (written, not added). `par`: the sound's knobs as this voice hears them, the LFOs in (an LFO at 100 sweeps a knob
  from end to end). `pitch`: 128 phase steps, one per sample (frequency = step x 88200 / 2^32), with glide, bend,
  octave and tune in them. Timed and charged; when the plugin is switched off by its budget, the voices are silent.
  Required.
- `block(buf)`: the voices' sum each block, before the synth's effect, while the current sound is this engine's: 64
  samples at 44.1 kHz in place, in the audio block; or 0. Timed and charged. At most 8 engines in all have one.
- `page()`: its T1 page; `page_t2()`: its T2 page, or 0 for the OS's envelope. In the display thread, every frame
  while the page shows, on a cleared frame and under the OS's popups. Draw with the `opus_page_*` functions.
- `turn(encoder, detents, page)`: a turn on its page, in the key thread: `encoder` 0 blue, 1 green, 2 white, 3 orange;
  `detents` (0 when the turn was less than one); `page` 0 T1, 1 T2, 3 T4. Return 1 to take it (the OS never sees it;
  `opus_engine_knob_set` and `opus_engine_data_set` keep a change); return 0 to leave it to the OS (on T1 the
  encoders turn knobs 0-3). 0 for none.
- `key(key, pressed, page)`: a key while its page shows (the OS's key code, `pressed` 1 down), key thread; 1 takes it.
  0 for none.
- `flags`: `OPUS_ENGINE_*` below.
- `page_t4()`: its T4 page, or 0 for the OS's LFO pages.
- `finished(voice)`: with `OPUS_ENGINE_OWN_ENVELOPE`: asked once a block, in the audio block, from a voice's release
  until it says so, before the render: nonzero means the sound has ended and frees the voice (no more renders until a
  note). Timed and charged. 0 for none.
- `type`: another firmware's engine: the `"type"` that firmware's sounds of this engine carry (`"fmbell"`), so such a
  sound loads as this engine instead of being refused; or 0. Needs core 2.6.
- `mark`: the mark to claim, 0x5301-0x53FF only (another firmware's numbers), or 0 for the core's own. Needs core 2.6.
- `svg`, `screen_init(scr)`, `screen_draw(scr, dt)`: its own OS screen on T1 instead of `page`, or 0. `svg` is a full
  path to a file whose start is the screen's SVG (checked as art is, loaded once at the start); `screen_init` runs once
  after that (find the document's nodes); `screen_draw` every frame it shows, in the display thread: it draws the
  document itself through the firmware's screen code, and what moves. `scr` is an OS screen object. Advanced: for
  porting engines written as OS screens. Needs core 2.6.

```c
#define OPUS_ENGINE_OWN_ENVELOPE 1
#define OPUS_ENGINE_OWN_LFO 2
#define OPUS_ENGINE_OS_VOICES 4
```

- `OPUS_ENGINE_OWN_ENVELOPE`: the engine shapes the volume itself, its release and the velocity included (its T2
  page). With `finished`, the OS's envelope does not run for its voices (no cost; the gain stays at a held key's), and
  a released voice is freed when `finished` says so. Without `finished`, the OS's envelope runs held neutral (fastest
  attack, full sustain, slowest release) and keeps a released voice rendering until that release ends. The OS's play
  mode stays (shift + T2).
- `OPUS_ENGINE_OWN_LFO`: the engine's sounds have the OS's LFO held off; the engine has its own (its T4 page; its key
  handler takes T4's on/off key).
- The hold is the engine's sounds' only: a sound's own envelope and LFO settings are kept in its member and come back
  when the slot changes to an engine that does not hold them.
- `OPUS_ENGINE_OS_VOICES`: the engine's voices are `iter`'s own objects in L1 memory, 0x24 bytes each at
  0xFFB00000 + 0x24 x voice: the first word is the OS's (never write it), the other 32 bytes the engine's. `init`,
  `note_on`, `release`, `render` and `finished` get that object (`voice` 0: the core keeps no state), and `render` the
  knobs as the OS gives them (no LFO reach). For engines written for `iter`'s layout. Needs core 2.6.

```c
int opus_engine_add(struct opl_self *self, const struct opus_engine *e);
```

Adds an engine, from `opl_init`. The struct must stay valid: make it `static const`. Returns its mark (from 0x100 to
32767, the same on every OP-1: made from the plugin's id and the engine's name, or the one it claims), or:

- -1: a bad struct (too short, no name, no `render`), a claimed mark outside 0x5301-0x53FF, or no room (32 engines in
  all, 8 of them with a `block` handler);
- -2: no memory for its voices;
- -3: its mark or type is taken by an engine loaded before;
- -4: the engine service could not start (one of its places is a plugin's; see the boot log).

```c
int opus_engine_live(short par[8]);
```

The current sound's knobs as its newest note's voice last heard them (the LFOs in) and 1, while a voice plays; else
the stored knobs and 0. For a page.

```c
int opus_engine_knobs(short par[8]);
```

The current sound's knobs as stored (what the user set, no LFO), and 1 when the current sound is an engine's. A page
shows a set value steady from these, and what the sound does from `opus_engine_live`. Any thread. Needs core 2.3.

```c
int opus_engine_own_t2(int s);
```

1 when synth slot `s`'s (0-7) sound is an engine's with its own T2 page: a turn there never reaches the OS's envelope.

```c
int opus_engine_knob_set(int k, int value);
```

Sets knob `k` (0-3 or 5-7) of the current sound, which must be an engine's, to `value` (kept within 0-32767), and
keeps it (the sound's row is written). Returns 1, or 0 when the current sound is no engine's or `k` is 4. Key thread
(a turn or key handler).

```c
#define OPUS_ENGINE_DATA 256
int opus_engine_data(void *buf, int max);
int opus_engine_data_set(const void *buf, int n);
int opus_engine_sound_name(char *buf, int max);
```

The current sound's own data (an edited voice, an envelope), up to `OPUS_ENGINE_DATA` (256) bytes, kept inside the
sound, so it travels with it: the OS's save makes a user preset holding it, and loading the preset brings it back.

- `opus_engine_data(buf, max)`: a copy of the current slot's data, as its last load or `data_set` left it, at most
  `max` bytes. Returns its length (0: none). Any thread.
- `opus_engine_data_set(buf, n)`: replaces it with `n` bytes (0-256; 0 removes it). Returns 1, or 0 when the current
  sound is no engine's or `n` is out of range. Key thread (a turn or key handler).
- `opus_engine_sound_name(buf, max)`: the sound's name as the OS keeps it (the engine's name when chosen, a user
  preset's after the OS's save or a load), up to 23 characters, 0-terminated in `buf`. Returns its length. Any thread.

## Pages

A plugin's own page is the whole 320 x 160 drawing: an engine's T1, T2 and T4 pages, a quick page, or what a frame
handler draws. The `opus_page_*` functions draw only inside such a draw call (an engine's page, a quick page's `draw`,
an `opus_on_frame` or `opus_on_frame_under` handler); anywhere else they draw nothing. Places and sizes are in units,
cut to the drawing; the OP-1's window shows y 2-146. Colours by id ([Colours](#colours)).

```c
void opus_page_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
void opus_page_text_right(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
```

Text in the OP-1's font, `x`, `y` its top left (`opus_page_text_right`: `x` is its right end). Each plugin has 32 text
slots (0-31), shared by both functions; a slot keeps the place, size and alignment of its first call, and each call
sets its colour and text, so use one slot for each place. `size`: its height, 6-48; the text must start and end inside
the drawing. Write in lower case: the OP-1's font draws upper-case letters as icons.

```c
void opus_page_fill(int x0, int y0, int x1, int y1, int colour);
void opus_page_frame(int x0, int y0, int x1, int y1, int colour);
void opus_page_bar(int x0, int y0, int x1, int y1, unsigned value, unsigned max, int colour);
```

A filled rectangle; its one-pixel outline; a bar lit from the left (`value` of `max` of its width in `colour`, the
rest dark grey). Corners inclusive, in units. `colour`: an id.

```c
void opus_page_line(int x0, int y0, int x1, int y1, int colour);
```

A line one unit wide, smoothed, with round ends, in units. `colour`: an id or `OPUS_RGB(...)`.

```c
void opus_page_stroke_line(int x0, int y0, int x1, int y1, int width, int colour);
```

A line in units x 256 (finer than a pixel), smoothed by the core the way the OS's renderer covers a pixel, with round
ends; a line that continues from the last one's end in the same colour joins it smoothly. `width` x 256 is in the
panel's pixels, as a path's stroke is (256 = 1 pixel). Much cheaper than a path: for lines drawn every frame.
`colour`: an id or `OPUS_RGB(...)`.

```c
void opus_page_dot(int x, int y, int r, int filled, int colour);
```

A circle of radius `r` (0-40 units) around (`x`, `y`), smoothed: filled (`filled` 1) or its outline one unit wide. It
must lie inside the drawing with a unit to spare, else it is not drawn. `colour`: an id or `OPUS_RGB(...)`.

```c
int opus_page_art(struct opl_self *self, int page, const char *path);
```

SVG art behind an engine's page: `page` 0 T1, 1 T2, 3 T4; `path` the file's full path (up to 71 characters, at most
64 KB), or 0 for no art any more. The first time the page shows, the file is checked for what would stop the OP-1 in
the OS's SVG parser, then loaded, drawn once and kept; every frame after, the kept picture is put back cheaply and
the page's draw call draws over it. The file loads again after it changed (its size). Returns 1, or 0 for a bad page
or no room (16 art files in all, with `opus_art_open`'s). Any time from `opl_init` on (for example from a folder's
`changed` handler).

The check refuses (and the boot log says why) a missing file, an XML error, a root other than `<svg>`, a number with
a point but no digits after it, more than four decimals, or an exponent, the path commands Q and T, and an id with
`_x` not followed by two characters and `_`. A refused file is never parsed; the page draws without it.

## Paths

Drawing as the OS draws: a path of lines and cubic curves, stroked or filled by the OS's own anti-aliased renderer
(the one its envelope and LFO screens use), which also scales it to the panel. Smooth edges, widths, caps and joins
as stock's. Only inside a page's draw call (the display thread).

Places are in units x 256 (finer than a pixel): x 0-81,920 across, y 0-40,960 down; places beyond a margin around the
drawing (64 units) are pulled in to it. The cost is the OS's: about 2 M cycles for a 128-point figure 140 x 120 units.

```c
#define OPUS_PATH_POINTS 1024
#define OPUS_RGB(rgb)   (0x01000000 | ((rgb) & 0xFFFFFF))
#define OPUS_CAP_BUTT   0
#define OPUS_CAP_ROUND  1
#define OPUS_CAP_SQUARE 2
#define OPUS_JOIN_MITER 0
#define OPUS_JOIN_ROUND 1
#define OPUS_JOIN_BEVEL 2
```

- `OPUS_PATH_POINTS`: the most points a path holds (a curve takes three); points past that are dropped.
- `OPUS_RGB(0xRRGGBB)`: a colour of the plugin's own, for strokes, fills, lines and dots.
- `OPUS_CAP_*`: a stroke's ends; `OPUS_JOIN_*`: its corners. Another value means round.

```c
void opus_page_path(void);
```

Starts a new path (the last one is gone).

```c
void opus_page_move(int x, int y);
```

Starts a subpath at (`x`, `y`).

```c
void opus_page_line_to(int x, int y);
```

A straight line to (`x`, `y`). As the path's first point, it is a move.

```c
void opus_page_curve_to(int x1, int y1, int x2, int y2, int x, int y);
```

A cubic curve: two control points, then the end. As the path's first point, it is a move to the end.

```c
void opus_page_close(void);
```

Closes the current subpath (a line back to its start).

```c
void opus_page_stroke(int width, int colour, int alpha, int cap, int join);
```

Draws the path's outline and keeps the path. `width` x 256 is in the panel's pixels, not units (the renderer scales
places, not widths): 384 is about 1.5 pixels on every panel; at most 32 pixels. `colour`: an id or `OPUS_RGB(...)`.
`alpha`: 0 (draws nothing) to 255 (opaque), blended over what is there. `cap`, `join`: as above. A long stroke is
drawn in pieces cut at its points, each small enough for the renderer to draw whole; with round caps no seam shows.

```c
void opus_page_fill_path(int colour, int alpha);
```

Fills the path (each subpath closed; at least three points) and keeps it. A path may be filled and then stroked.

```c
opus_page_path();
opus_page_move(40 * 256, 100 * 256);
opus_page_curve_to(80 * 256, 20 * 256, 120 * 256, 20 * 256, 160 * 256, 100 * 256);
opus_page_stroke(384, OPUS_COLOR_E1, 255, OPUS_CAP_ROUND, OPUS_JOIN_ROUND);
```

## Art

A plugin's own SVG art, drawn as its page needs: a document loaded once, its elements shown, hidden or turned by id,
drawn by the OS's renderer, and the result kept as pixels (see [Pixels](#pixels)) while what moves is drawn over it.
Files are checked and loaded as for `opus_page_art`; check every SVG on the computer first with the kit's `svg`
command. Except where noted, these run inside a page's draw call.

The kit's `examples/opus.outatime` (`ota_page.c`) shows the whole way: its T1 art travels inside the plugin
(`opus_art_text`), takes the user's encoder colours (`opus_art_colour_slots`, `opus_art_set_colour`), is drawn and
kept once (`opus_art_draw`, `opus_page_keep`), put back every frame (`opus_page_put`), and its moving instruments are
drawn over it in code.

```c
int opus_art_open(struct opl_self *self, const char *path);
```

Names an art file (a full path, up to 71 characters, at most 64 KB); it is checked and loaded the first time
`opus_art_ready` asks. Returns the art's number (0 or more), or -1 when there is no room (16 art files in all). Any
time from `opl_init` on.

```c
int opus_art_text(struct opl_self *self, const char *name, const char *svg, unsigned len);
```

Art carried inside the plugin itself (a C string made from the `.svg` file, so the plugin stays one `.opmod`): written
to the plugin's folder as `/yaffs2/mod/<id>/<name>` when missing or different (within the flash's headroom), then
opened as `opus_art_open`. `len`: the text's length, at most 64 KB. From `opl_init`. Returns the art's number, or -1
(no room in the table or on the flash, or the write failed: the boot log says which).

```c
int opus_art_ready(int art);
```

1 when the art is loaded (loading it now, the first time), 0 when it is not (refused: the boot log says why).

```c
void opus_art_draw(int art);
```

Draws the whole document over the frame as shown now, by the OS's renderer. About 3 M cycles for a page of 250
shapes: draw it when it changes and keep the pixels ([Pixels](#pixels)), not every frame.

```c
int opus_art_show(int art, const char *id, int on);
```

Shows (`on` 1) or hides (0) the element with this id at the next draw. Returns 1, or 0 when there is no such element
or the art is not loaded.

```c
int opus_art_turn(int art, const char *id, int turn4096, int cx, int cy);
```

Turns the element with this id by `turn4096` (1/4096 of a full turn, clockwise on the screen) around (`cx`, `cy`) in
units x 256, replacing its last turn: the next draw draws the drawing's own shape at that angle. Returns 1, or 0 when
there is no such element or the art is not loaded.

```c
int opus_art_colour_slots(int art, unsigned rgb, unsigned char **slots, int max);
void opus_art_set_colour(unsigned char **slots, int n, unsigned rgb);
```

Recolouring: `opus_art_colour_slots` finds the places in the document's fills and strokes that hold the placeholder
colour `rgb` (0xRRGGBB), up to `max`, into `slots`, and returns the count; `opus_art_set_colour` writes a new colour
`rgb` into `n` of them. The next draw shows it.

```c
unsigned opus_art_colours(void);
```

A count that changes whenever the user's colours change (a pick, a theme). Compare it with the value you last saw:
when it moved, set your colours and draw and keep the art again. Any thread.

```c
void opus_sincos(int turn4096, int *s, int *c);
```

The sine and cosine of `turn4096` (1/4096 of a turn, -2048 to 2048), times 4096, into `*s` and `*c`. Integer only;
any thread, the audio block included.

## Pixels

```c
unsigned short *opus_page_pixels(int *rows);
```

The frame being drawn, for a page's own pixels: only inside a page's draw call, else it returns 0. `*rows` gets the
panel's long side: 376 (rev 2), 400 (rev 1) or 320 (FE). The frame is RGB565 and stored turned: pixel (x, y), x
across from 0 to rows - 1 and y down from 0 to rows / 2 - 1, is the word `[256 * (rows - 1 - x) + y]`. Scale units to
pixels by `rows / 320`.

```c
int opus_page_keep(unsigned short *out, unsigned cap, int x0, int x1, int y0, int y1);
```

Keeps the lit (non-black) pixels of the frame's columns `x0`-`x1` and rows `y0`-`y1` (panel pixels, as above) as runs
in `out`, which holds `cap` words. Returns the words used, or -1 when they do not fit (or outside a draw call).

```c
void opus_page_put(const unsigned short *runs, int used);
```

Puts kept runs back into the frame. A drawing kept this way costs a copy a frame instead of drawing it again.

```c
static unsigned short kept[8192];
static int used = -1;
static void page(void)
{
    int rows;
    if (used < 0 && opus_page_pixels(&rows) && opus_art_ready(art)) {
        opus_art_draw(art);
        used = opus_page_keep(kept, 8192, 0, rows - 1, 0, rows / 2 - 1);
    }
    if (used > 0)
        opus_page_put(kept, used);
    /* ... draw what moves ... */
}
```

## The clock

```c
struct opus_clock {
    short year;
    unsigned char month, day, hour, minute, second, weekday;
};
void opus_clock(struct opus_clock *c);
```

The OP-1's date and time as the user set it (the system menu's "date and time"): `year` (for example 2026), `month`
1-12, `day` 1-31, `hour` 0-23, `minute` and `second` 0-59, `weekday` 0 Sunday to 6 Saturday. Any thread. Needs core
2.4.

## Firmware functions (`fw_*`)

Besides the core's `opus_*` names, a plugin may call the OP-1 firmware's own functions by name: the `fw_*` names
declared in `sdk/op1fw.h` (for example `fw_snprintf`, `fw_shift_held`, `fw_yaffs_open`). Each is an import like the
core's names, resolved when the plugin loads; the build lists them in the manifest's built `deep` line.

A `fw_*` call runs stock firmware code directly, with no core in between. It is advanced: the names, arguments and
behaviour are those of firmware #246 as researched, with no guarantee beyond that; the thread rules of the firmware
apply (the file functions take the file system's lock, the drawing functions belong to the display thread, nothing of
the OS's belongs in the audio block unless you know it is safe). Prefer an `opus_*` service where one exists. The
firmware functions are not listed here; `op1fw.h` documents each.

## Index

Every declaration in `opus_plugin.h`, by name.

| Name | Section |
|---|---|
| `opl_init` | [The plugin's entry](#the-plugins-entry) |
| `opus_accent` | [Colours](#colours) |
| `opus_accent_now` | [Colours](#colours) |
| `opus_alloc` | [Memory](#memory) |
| `opus_art_colour_slots` | [Art](#art) |
| `opus_art_colours` | [Art](#art) |
| `opus_art_draw` | [Art](#art) |
| `opus_art_open` | [Art](#art) |
| `opus_art_ready` | [Art](#art) |
| `opus_art_set_colour` | [Art](#art) |
| `opus_art_show` | [Art](#art) |
| `opus_art_text` | [Art](#art) |
| `opus_art_turn` | [Art](#art) |
| `opus_bgfile_idle` | [Files](#files) |
| `opus_clock` | [The clock](#the-clock) |
| `opus_cpu_charge` | [CPU and the log](#cpu-and-the-log) |
| `opus_effect_host` | [Another firmware's effects](#another-firmwares-effects) |
| `opus_effect_live` | [Effects](#effects) |
| `opus_effect_register` | [Effects](#effects) |
| `opus_effect_show` | [Hidden stock effect and engine](#hidden-stock-effect-and-engine) |
| `opus_engine_add` | [Engines](#engines) |
| `opus_engine_data` | [Engines](#engines) |
| `opus_engine_data_set` | [Engines](#engines) |
| `opus_engine_knob_set` | [Engines](#engines) |
| `opus_engine_knobs` | [Engines](#engines) |
| `opus_engine_live` | [Engines](#engines) |
| `opus_engine_own_t2` | [Engines](#engines) |
| `opus_engine_show` | [Hidden stock effect and engine](#hidden-stock-effect-and-engine) |
| `opus_engine_sound_name` | [Engines](#engines) |
| `opus_fast` | [Memory](#memory) |
| `opus_flash_headroom` | [Files](#files) |
| `opus_fn_held` | [Events](#keys-and-encoders) |
| `opus_fx_free` | [Managers](#managers) |
| `opus_fx_held` | [Managers](#managers) |
| `opus_fx_list_shown` | [Managers](#managers) |
| `opus_fx_manager` | [Managers](#managers) |
| `opus_fx_new` | [Managers](#managers) |
| `opus_fx_owner` | [Managers](#managers) |
| `opus_fx_page_shown` | [Managers](#managers) |
| `opus_fx_pages` | [Managers](#managers) |
| `opus_fx_record_put` | [Managers](#managers) |
| `opus_fx_records` | [Managers](#managers) |
| `opus_fx_reset` | [Managers](#managers) |
| `opus_fx_run` | [Managers](#managers) |
| `opus_fx_type_of` | [Managers](#managers) |
| `opus_instrument_now` | [Managers](#managers) |
| `opus_journal` | [Journals](#journals) |
| `opus_journal_done` | [Journals](#journals) |
| `opus_journal_run` | [Journals](#journals) |
| `opus_log` | [CPU and the log](#cpu-and-the-log) |
| `opus_on_block` | [Events](#audio-events) |
| `opus_on_block_end` | [Events](#audio-events) |
| `opus_on_folder` | [Folders on the USB disk](#folders-on-the-usb-disk) |
| `opus_on_frame` | [Events](#frames) |
| `opus_on_frame_begin` | [Events](#frames) |
| `opus_on_frame_under` | [Events](#frames) |
| `opus_on_key` | [Events](#keys-and-encoders) |
| `opus_on_key_after` | [Events](#keys-and-encoders) |
| `opus_on_knob` | [Events](#keys-and-encoders) |
| `opus_on_knob_after` | [Events](#keys-and-encoders) |
| `opus_on_main_pass` | [Events](#the-main-pass-the-clip-queue-and-tape-writes) |
| `opus_on_panel` | [The panel](#the-panel) |
| `opus_on_panel_knob` | [The panel](#the-panel) |
| `opus_on_post_fx` | [Events](#audio-events) |
| `opus_on_post_master` | [Events](#audio-events) |
| `opus_on_pre_fx` | [Events](#audio-events) |
| `opus_on_queue_after` | [Events](#the-main-pass-the-clip-queue-and-tape-writes) |
| `opus_on_quick` | [Quick pages](#quick-pages) |
| `opus_on_save_pass` | [Events](#saving) |
| `opus_on_setting` | [Settings](#settings) |
| `opus_on_settings_rows` | [Settings](#settings) |
| `opus_on_tape_write` | [Events](#the-main-pass-the-clip-queue-and-tape-writes) |
| `opus_page_art` | [Pages](#pages) |
| `opus_page_bar` | [Pages](#pages) |
| `opus_page_close` | [Paths](#paths) |
| `opus_page_curve_to` | [Paths](#paths) |
| `opus_page_dot` | [Pages](#pages) |
| `opus_page_fill` | [Pages](#pages) |
| `opus_page_fill_path` | [Paths](#paths) |
| `opus_page_frame` | [Pages](#pages) |
| `opus_page_keep` | [Pixels](#pixels) |
| `opus_page_line` | [Pages](#pages) |
| `opus_page_line_to` | [Paths](#paths) |
| `opus_page_move` | [Paths](#paths) |
| `opus_page_path` | [Paths](#paths) |
| `opus_page_pixels` | [Pixels](#pixels) |
| `opus_page_put` | [Pixels](#pixels) |
| `opus_page_stroke` | [Paths](#paths) |
| `opus_page_stroke_line` | [Pages](#pages) |
| `opus_page_text` | [Pages](#pages) |
| `opus_page_text_right` | [Pages](#pages) |
| `opus_panel_bar` | [The panel](#the-panel) |
| `opus_panel_fill` | [The panel](#the-panel) |
| `opus_panel_frame` | [The panel](#the-panel) |
| `opus_panel_text` | [The panel](#the-panel) |
| `opus_place_set` | [Switched places](#switched-places) |
| `opus_presets` | [Presets](#presets) |
| `opus_queue_hold` | [Events](#the-main-pass-the-clip-queue-and-tape-writes) |
| `opus_quick_page` | [Quick pages](#quick-pages) |
| `opus_quick_show` | [Quick pages](#quick-pages) |
| `opus_rename` | [Files](#files) |
| `opus_set_setting` | [Settings](#settings) |
| `opus_setting` | [Settings](#settings) |
| `opus_sincos` | [Art](#art) |
| `opus_skip_dry_input` | [Events](#audio-events) |
| `opus_sound_slot` | [Managers](#managers) |
| `opus_types` | [Managers](#managers) |

Structs and types: `struct opl_self` ([The plugin's entry](#the-plugins-entry)); `struct opus_folder`
([Folders](#folders-on-the-usb-disk)); `struct opus_need`, `struct opus_fx`, `struct opus_fx_view`,
`struct opus_effect` ([Effects](#effects)); `struct opus_hosted_effect`
([Another firmware's effects](#another-firmwares-effects)); `opus_handle`, `struct opus_type_info`,
`struct opus_fx_shown`, `struct opus_fx_record` ([Managers](#managers)); `struct opus_engine`
([Engines](#engines)); `struct opus_clock` ([The clock](#the-clock)).
