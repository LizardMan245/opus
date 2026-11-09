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
safe start). A plugin that crashed the OP-1 is skipped at the next start ("skipped: crashed") until its file changes
or the user switches it on again in the mods list.

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

Each plugin's audio work is measured block by block against its CPU figure: the manifest's `cpu` line, which the
build writes from a measurement, or, for an unmeasured plugin, a tenth of a block (58,052 cycles). Going over the
figure is shown (the mods list, the monitor, `opus_stats`), not acted on (since core 2.33). What the core acts on is the
audio itself: a block that came too late to play (a click). A few late blocks within about two seconds bring up a small
warning at the top right of the screen, naming the plugin that spiked in them (the user can switch it off in the
system menu's "audio warning"). 50 late blocks within about two seconds switch off the heaviest plugin for the rest of
that start: its audio handlers stop, its effects pass the sound through unchanged and its engines' voices are silent,
and a notice says so. A plugin whose one block takes a whole block by itself is switched off at once. Nothing is
marked: the next start, or turning it on in the mods list, tries again. Two exceptions: a manifest with `cpu user` (a
manager that runs what the user chose) is measured and named but never switched off, and in the OP-1's developer mode
(a row of the system menu) no plugin is switched off.

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
| 2.8 | `opus_alloc_audio`, `opus_page_still`, sequencers: `opus_seq_register` and the rest of the Sequencers section; preset sources: `opus_preset_source`, `opus_preset_add`, `opus_presets_relist` |
| 2.9 | `opus_called`; no fixed counts of engines, effect types, hosted effects, instances or sequencers |
| 2.10 | the sampler service: `OPUS_ENGINE_SAMPLE` and the Samples section |
| 2.11 | `opus_replace`: an OS screen's drawing replaced |
| 2.12 | `opus_key_post` |
| 2.13 | `opus_on_key_filter` |
| 2.14 | `opus_key_timer`, `opus_stock_screen` |
| 2.15 | every plugin may be a preset source (up to eight; before, one) |
| 2.16 | the journal's USB disk steps, `frename` and `funlink` |
| 2.17 | presets as files: `opus_preset_file`, `opus_preset_overwrite`, `opus_preset_rename`, `opus_presets_reload` |
| 2.18 | `opus_preset_delete` |
| 2.19 | `opus_drums`; drum kits as files: `opus_kit_file`, `opus_kit_overwrite`, `opus_kit_rename`, `opus_kit_delete`; a preset source's drum kits (`list(1)`) |
| 2.20 | `opus_folder_refused`, `opus_panel_size` |
| 2.21 | fast memory on lease, the tempo service, LFO types, `opus_page_text_centre` (below the table) |
| 2.22 | `OPUS_LFO_SOUND` |
| 2.24 | drum engines: `OPUS_ENGINE_DRUM`, the engine fields `keys`, `note_key`, `group` and `choke`, `opus_drum_key` |
| 2.25 | `opus_on_key_filter_first` |
| 2.26 | a drum engine's `render` gets its voice's pitch |
| 2.27 | an LFO type's page on the drum's T4 too |
| 2.28 | `opus_stats`, `opus_page_dim` |
| 2.29 | `opus_frames_late` |
| 2.30 | `uses fn_hold` in the manifest: FN held alone as a control (manifest.md) |
| 2.31 | a module's own page, the same in every module type: `svg`, `screen_init`, `screen_draw`, `page_key`, `page_turn` on effects, engines (drum engines' screens too), LFO types and sequencers; `struct opus_page`, `opus_page_now` |
| 2.32 | `opus_folder_adopt`: a folder's file endings left in `/mods` itself, moved into it |
| 2.33 | an engine's `mark` claimed in 0x4F01-0x4FFF too; an effect's `aliases`; `opus_boot_picture` |

Core 2.6's additions: `OPUS_ENGINE_OS_VOICES`; the engine fields `type`, `mark`, `svg`, `screen_init` and
`screen_draw`; `struct opus_hosted_effect` and `opus_effect_host`.

Core 2.21's additions: fast memory on lease (`opus_fast_lease`, `opus_fast_use`, `struct opus_need`'s `fast_moves`,
`OPUS_ENGINE_STATE_MOVES` and the manifest's `priority` line); the engine field `shift_knobs`; the tempo service
(`opus_tempo`, `opus_tempo_phase`, `opus_tempo_phase_step`); LFO types (`opus_lfo_register` and the rest of the LFO
types section); `opus_page_text_centre`.

### Words used in this reference

- **The drawing**: the screen as plugins see it, 320 x 160 **units**, x across from the left, y down from the top. The
  core scales it to the panel the OP-1 has. On the device only y 2-146 is visible through the OP-1's window: keep what
  matters inside it.
- **The panels**: the OP-1 has three screen types: rev 2 (376 x 188 pixels), rev 1 (400 x 200) and FE (320 x 160).
  Everything drawn in units looks the same on all three.
- **A detent**: one click of an encoder. The OS counts 4 units per detent; some handlers get OS units, some detents.
- **The mods list**: Opus's list of plugins in its menu. A plugin's row has a panel on the right; the blue push opens
  its settings page, the green push its controls, the orange push its details, and the white push offers to delete
  it (a page asks first). White turns the list's order (name, date added, size, cost); orange turns the selected
  plugin off or on: an off plugin is not loaded from the next start. A plugin skipped after a crash shows as off;
  switching it on tries it again at the next start.
- **The boot log**: a 2,048-byte text log in the core's memory (not a file): the core notes there why it refused
  something, and plugins add their own lines with `opus_log`. It is read over USB with Opus's development tools.

## Contents

- [The plugin's entry](#the-plugins-entry): `OPUS_PLUGIN_API`, `struct opl_self`, `opl_init`
- [Events](#events): keys, encoders, FN, frames, the main pass, the clip queue, tape writes, saving, audio
- [Quick pages](#quick-pages): a plugin's own page in place of the screen
- [Files](#files): rename, the background file job, the flash's headroom, reading files
- [Journals](#journals): changes in several steps that survive a power cut
- [Presets](#presets): the synth presets and drum kits as the browser lists them, presets as files, preset sources
- [Memory](#memory): the pool, the audio pool, fast memory and its leases
- [Which one is called](#which-one-is-called)
- [CPU and the log](#cpu-and-the-log)
- [The core's figures](#the-cores-figures): what the CPU, memory, threads and drawing cost, as one record
- [Settings](#settings): the manifest's settings, a plugin's own rows on the settings page
- [Switched places](#switched-places)
- [Folders on the USB disk](#folders-on-the-usb-disk)
- [The boot picture](#the-boot-picture)
- [The panel](#the-panel): a plugin's panel on the mods list
- [Colours](#colours): colours by id, the theme's accents
- [A module's own page](#a-modules-own-page): one screen, key and turn shape for every module type
- [Effects](#effects): effect types in the OS's effect list
- [Hidden stock effect and engine](#hidden-stock-effect-and-engine)
- [Another firmware's effects](#another-firmwares-effects)
- [Managers](#managers): running instances of any effect type
- [Engines](#engines): synth engines in the OS's engine list
- [Samples](#samples): a slot's sample, the sampler service for an engine that plays it
- [Sequencers](#sequencers): sequencers in the OS's sequencer list
- [LFO types](#lfo-types): LFOs in the OS's LFO list
- [Tempo](#tempo): the OP-1's tempo and where the music is
- [Pages](#pages): drawing a plugin's own page
- [Paths](#paths): lines and curves drawn by the OS's renderer
- [Art](#art): SVG art on a page
- [Pixels](#pixels): a page's own pixels, the panel's size
- [3D line art: orbit3d](#3d-line-art-orbit3d): the library for 3D drawing on a page ([orbit3d.md](orbit3d.md))
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
int opus_key_post(int key, int pressed);
```

Sends a key event of the plugin's own (core 2.12): it joins the OS's key queue after the events already there, and
every key handler (the plugin's own too) and the OS take it as a real press or release. Key codes below 0x40. Returns
1 when queued, 0 when not (no such key, the queue full: try again later). Any thread. A plugin that holds a key event
back (takes it) and sends it later uses this: opus.triggerguard sends a release once its window ran out, and lets the
event it sent through when it comes back to its own filter.

```c
void opus_on_key_filter(struct opl_self *self, int (*fn)(int key, int pressed));
```

A key filter (core 2.13): your `fn` gets every key event first, as it came from the keys, before the core's own
handling, Opus's menu, every `opus_on_key` handler and the OS. Return 1 to drop the event: it never happened, no one
sees it (no `opus_on_key_after` either). Return 0 to pass it on. For a plugin that cleans the keys themselves, as the
trigger guard does. A key handler that sits behind the menu and other plugins is wrong for that: they would see the
events you hold back. Keep presses and releases paired for everyone behind you: drop a press, drop its release too.
An event a filter sends with `opus_key_post` comes back through every filter, the sender's too.

```c
void opus_on_key_filter_first(struct opl_self *self, int (*fn)(int key, int pressed));
```

A key filter that comes before every plain one (core 2.25): the same as `opus_on_key_filter`, but all such filters run
first, in their plugins' load order, and only then the filters of `opus_on_key_filter`. For a plugin that cleans the
keys for everyone, as opus.triggerguard does: every other filter and handler then sees the cleaned keys, each event
once, the events the cleaner sends late included. A plain filter loaded before the cleaner would see a key's raw
release and then the cleaner's late copy of it. Use it only for such a cleaner. A plugin may register one filter of
each kind.

```c
void opus_key_timer(struct opl_self *self, unsigned ms, void (*fn)(void));
```

A timer in the key thread (core 2.14): `fn` runs once, `ms` or a little more from now (the next audio block's check,
then the key queue), in the thread where key handlers run, so it may do what they do: give the OS a key with
`fw_ui_dispatch`, post one with `opus_key_post`. One timer a plugin: a call replaces the one waiting, and `ms` 0
cancels it. For a gesture that waits, such as a key held a moment: opus.stockhelp shows the OS's help screen when FN
has been held alone for its hold time, and stops the timer when another key comes first.

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

The OS draws a frame every display period, about 35 ms (28.5 frames a second) when it keeps up. While the screen
stays the same (three frames in a row alike), the core draws only every fourth frame, until a key, a turn, MIDI
coming in (a note, a controller, a bend; not its clock) or a frame that differs brings every frame back. A frame
that is not drawn calls no frame handler and no page's draw. So the calls are no clock: take time from the CPU's
cycle counter (400 cycles a microsecond), not from counting frames. A change no key or turn starts (a meter, a blink)
may show up to three frames late.

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

```c
int opus_frames_late(void);
```

How many of the last 8 frames came late, 0-8 (core 2.29). A frame is late when it comes two display periods or more
after the one before: the OS starts the next frame only when the last one is done, so a frame whose drawing takes
longer than a period halves the frame rate at once. The audio comes first: under a heavy sound the drawing gets only
what the audio leaves. An animated page may draw a lighter version while frames come late (leave a glow or a trail
out) and go back once they come on time again. Any thread.

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
| `frename A B` | on the USB disk: A becomes B, when A is there and B is not (the disk renames over nothing: `funlink B` first) |
| `funlink A` | on the USB disk: A removed (its read-only mark cleared first), when it is there |

Paths only in `/yaffs2/user/audio/` and `/yaffs2/mod/` (the same rules as `opus_rename`). Each step either finds its
condition false when run again or does no harm, so a step interrupted by a power cut can run again safely.

The two disk steps (core 2.16) take paths on the OP-1's USB disk, in `/synth/` and `/drum/` only (the same rules for
the parts after the folder). They wait while the disk is not the OP-1's: early in the start, before the OS mounts it,
and in disk mode. The waiting step and the rest of the journal stay; at a start, the core carries them out as soon as
the OS has mounted the disk.

```c
#define OPUS_JOURNAL_MAX 2048
```

The longest journal, in bytes, `end\n` included.

```c
int opus_journal_run(struct opl_self *self, const char *steps);
```

Writes the journal down, then carries it out. Returns the number of steps done: all of them means the change is made.
Fewer means one step failed, or a disk step waits: that step and the rest stay in the journal for the next start (the
plugin may instead undo what was done and call `opus_journal_done`). -1 means the journal was not taken and nothing
was done: a step the core does not know or allow, too long, or the write failed (the boot log says so). Main thread.

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
1,024 entries). Each entry is 0x38 bytes. The OS's rows come first (the factory's, then any of the user's still in
the OS's database), then the preset sources' entries ([below](#preset-sources)), the user's preset files first. For
a plugin that renames or overwrites a preset. The entry layout and the count are firmware #246's own data (for
example +0x04 the preset's JSON value, +0x14 its folder name, +0x30 a byte that is 1 for a factory preset): this is
deep work, see [Firmware functions](#firmware-functions-fw_).

```c
extern unsigned char opus_drums[];
```

The drum kits as the drum's preset browser lists them (core 2.19): the OS's own array, which the core holds (with
room for 256 entries), laid out as `opus_presets`.

### Presets as files

Since core 2.17 the user's saved synth presets are files on the OP-1's USB disk, `/synth/FOLDER/NAME.aif`, and since
core 2.19 the user's drum kits too, `/drum/FOLDER/NAME.aif`: after each save, the core moves the OS's new database row
out to its file; the browser lists the files and loads each from its file when it is chosen. The factory presets stay
the OS's rows. A plugin that overwrites, renames or deletes one of the user's presets (as opus.presettools does) asks
the core, which does the file work through its journal ([Journals](#journals)), so a power cut never leaves it half
done.

```c
int opus_preset_file(int index);
int opus_preset_overwrite(struct opl_self *self, int index, int slot);
int opus_preset_rename(struct opl_self *self, int index, const char *to, int slot);
int opus_preset_delete(struct opl_self *self, int index);
int opus_kit_file(int index);
int opus_kit_overwrite(struct opl_self *self, int index, int slot);
int opus_kit_rename(struct opl_self *self, int index, const char *to, int slot);
int opus_kit_delete(struct opl_self *self, int index);
void opus_presets_reload(int kind);
```

`index` is the entry's place in `opus_presets` (or `opus_drums` for the `opus_kit_*` calls). All of them run in the
main thread, while the USB disk is the OP-1's (not in disk mode). Wait for `opus_bgfile_idle()` first: a preset
loading uses the same files.

- `opus_preset_file(index)`: 1 when the entry is one of the user's preset files, 0 when it is not (a factory preset,
  a row still in the OS's database, another source's entry). The other calls work on file presets only.
- `opus_preset_overwrite(self, index, slot)`: synth slot `slot`'s (0-7) sound written over the file preset, whole:
  its patch and, for a sampler or a sample engine, its sample. The entry keeps its name and type, so the list needs
  no reload. Returns 1 when done, -1 when nothing changed.
- `opus_preset_rename(self, index, to, slot)`: the file preset written again under the name `to` (the patch's
  `"name"` too) and the old file removed; with `slot` 0-7, that synth slot's sound is renamed with it (pass the slot
  that holds the preset), else -1. All in one journal. `to` may not hold `'`, `"`, `%`, `\` or control characters.
  Returns 1 when done, -2 when the folder has a file of that name already, -1 when nothing changed. The entry's JSON
  value in `opus_presets` is the caller's to update.
- `opus_preset_delete(self, index)`: the file preset removed (core 2.18). Returns 1 when done, -1 when not. Choosing
  the entry from a list still showing does nothing; the list loses it at its next reload.
- `opus_kit_file`, `opus_kit_overwrite`, `opus_kit_rename`, `opus_kit_delete` (core 2.19): the same four for the drum
  kits and the drum slots; a sampled kit's sound goes with it.
- `opus_presets_reload(kind)`: the presets read again as after a save, `kind` 0 the synth's, 1 the drum kits', with
  every source's entries (the OS's own loader alone would drop them). For a plugin that changed them. The browser
  never rebuilds a list while it shows: reload once it has closed.

```c
/* in a main-pass handler, once the user has chosen the new name: */
if (opus_bgfile_idle() && opus_preset_file(i)) {
    int r = opus_preset_rename(me, i, "warm pad", -1);
    /* r: 1 renamed, -2 the name is taken, -1 nothing changed */
}
```

### Preset sources

A preset source is a plugin with presets of its own in the browser's lists, after the OS's rows (core 2.8; since
core 2.15 any plugin may be one, up to eight). The browser and the engine list show its entries like the OS's. The
core's own preset files are the first source.

```c
struct opus_preset_source {
    unsigned size;
    void (*list)(int kind);
    int (*fill)(unsigned tag, char *json, int max, const char *raw_path);
};
int opus_preset_source(struct opl_self *self, const struct opus_preset_source *s);
int opus_preset_add(struct opl_self *self, int kind, const char *folder, const char *name, const char *type, unsigned tag);
void opus_presets_relist(struct opl_self *self, int kind);
```

- `size`: `sizeof (struct opus_preset_source)`.
- `list(kind)`: the OS builds its list again from its database at the start and after every save; each time, the
  core then calls every source's `list`, in the order they registered, in the main thread with the USB disk mounted.
  `kind` 0 is the synth presets; 1 the drum kits (core 2.19). Add your entries of that kind with `opus_preset_add`
  (none, for a kind you do not have). Required.
- `fill(tag, json, max, raw_path)`: the user chose one of your entries (the OS's background file thread, the USB disk
  free to read). Write the entry's whole patch JSON into `json` (at most `max` bytes) and return its length, or return
  0 or less when it cannot be loaded (then nothing happens). For a sampler, also write its sound to the file
  `raw_path` (the bytes of an AIFF's `SSND` chunk), where the OS reads it.

`opus_preset_source` registers the source, from `opl_init`; the struct must stay valid (`static const`). A second
call replaces the plugin's own struct. Returns 0, or -1 a bad struct, -2 eight sources already, -3 a place it needs is
another plugin's, -4 no memory.

`opus_preset_add(self, kind, folder, name, type, tag)` adds one entry, only from inside the plugin's own `list(kind)`
and for that kind: its folder and name as the browser shows them (`'` and `%` are left out), its type as a patch
names it (`"sampler"`, `"fm"`, ...), and `tag`, the plugin's own number for it, which `fill` gets back. An entry is
light: the core keeps only these. Returns the entry's index in `opus_presets` (or `opus_drums`), or -1 (not inside
`list`, or a bad argument), -2 (the list is full) or -3 (the user has a preset of that folder and name in the OS's
database: the OS's own export of it on the disk).

`opus_presets_relist(self, kind)` lists every source's entries again after the plugin's own list changed (at disk
mode's end, say), in the plugin's thread, while no browser list is open.

## Memory

```c
void *opus_alloc(struct opl_self *self, unsigned bytes);
```

Memory from the core's pool: 32-byte aligned, cleared to zero, the plugin's for this start. It is never freed: take
what you need once (in `opl_init`) and keep it. Returns 0 when the pool has no room left: go on without it, or refuse
from `opl_init`. The bytes count in the plugin's memory on the mods list. Any thread, but not the audio block.

```c
void *opus_alloc_audio(struct opl_self *self, unsigned bytes);
```

Memory the audio reads every block (a delay line, a voice's tables; core 2.8): as `opus_alloc`, from the audio pool,
1.3 MB of RAM the display does not share. Ordinary pool memory costs about twice as much to read while the display
sends one of its two frames; the audio pool does not. Plain `opus_alloc` takes from the audio pool too while it has
room, but leaves its last 512 KB to `opus_alloc_audio` and to effects' instances (which get it without asking).

```c
void *opus_fast(struct opl_self *self, unsigned bytes);
```

Fast memory: the CPU's L1 data memory, which costs no cache misses. 32-byte aligned, **not** cleared, never freed.
There is about 25 KB for all plugins together, first come, first served; returns 0 when none is left (then keep using
a pool buffer). Only from `opl_init`. Worth it for what an audio block touches every time (a short delay line, a
block's settings): each 32-byte line of ordinary memory an audio block touches costs about 107 cycles a block on the
device. Each call writes a line to the boot log. What `opus_fast` gives is gone for good, whether its plugin plays or
not: prefer a lease (below).

```c
#define OPUS_FAST_SCRATCH 0
#define OPUS_FAST_CONST   1
#define OPUS_FAST_STATE   2
struct opus_fast_lease;
struct opus_fast_lease *opus_fast_lease(struct opl_self *self, unsigned bytes, int kind, void *slow);
void *opus_fast_use(struct opus_fast_lease *l);
```

Fast memory on lease (core 2.21): the fast memory goes to what plays now. A plugin takes a lease from `opl_init` for
`bytes` it keeps in memory of its own, `slow` (from `opus_alloc` or `opus_alloc_audio`, 4-byte aligned, valid for the
whole start). In each audio block that works on those bytes, it first calls `opus_fast_use(lease)` and works where it
points: the fast copy while the lease holds fast memory, else `slow`. Never keep that pointer past the block.

The core gives a lease in use fast memory when there is room, or takes it from a lease of a plugin of lower priority
(the manifest's `priority` line: `high`, `normal` or `low`; `normal` without one), and takes it back from a lease
unused for about two seconds. It moves the bytes only at an audio block's end, between your calls. `kind` says what a
move keeps:

- `OPUS_FAST_SCRATCH`: nothing: the part fills its bytes every block before it reads them.
- `OPUS_FAST_CONST`: bytes set once in `slow` (tables) and never written after: copied in.
- `OPUS_FAST_STATE`: bytes the part changes (a delay line, a filter's state): copied in, and back to `slow` when the
  fast memory goes.

`opus_fast_lease` returns the lease, or 0 (`bytes` or `slow` 0, `slow` not 4-byte aligned, a bad `kind`, no memory
left): then work in `slow` alone. `opus_fast_use` belongs in the audio block (a sound call or an audio handler); it
returns 0 for a lease of 0. The kit's `examples/opus.outatime` keeps its wave tables on a lease this way:

```c
static short *tables;                          /* its own copy */
static struct opus_fast_lease *lease;

/* in opl_init: */
tables = opus_alloc(self, BYTES);
if (tables) {
    make_tables(tables);
    lease = opus_fast_lease(self, BYTES, OPUS_FAST_CONST, tables);
}

/* in each render: */
const short *t = lease ? opus_fast_use(lease) : tables;
```

Effects and engines can have the core lease their memory for them: `fast_moves` in `struct opus_need`
([Effects](#effects)) and `OPUS_ENGINE_STATE_MOVES` ([Engines](#engines)).

## Which one is called

```c
const void *opus_called(void);
```

From inside one of your engine's, effect's or sequencer's functions, the struct you registered it with (`struct
opus_engine`, `opus_effect`, `opus_hosted_effect` or `opus_sequencer`); 0 outside them. The audio block's calls and
each thread's are kept apart, so it is right in every thread. A plugin that registers several with one set of
functions tells them apart this way, as a VST host hands each call its instance: put your struct first in a record
of your own and cast `opus_called()` back to the record. Core 2.9.

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

## The core's figures

```c
int opus_stats(void *buf, unsigned max);
```

The core's figures (core 2.28): what the CPU, the memory, the threads and the drawing cost, for the whole OP-1 and
for every plugin, as one record copied into `buf` (at most `max` bytes). Returns the record's bytes; 0 when none has
been made yet (ask again a moment later); minus the record's bytes when `max` is too small. Any thread; asking costs a
copy. Opus's opus.monitor shows these figures over any page while FN is held.

The core makes a fresh record about four times a second while someone asks for one, and none otherwise, so a plugin
that stops asking stops the cost. The first call after a pause finds the last record made, or none. Every figure "so
far" counts from the OP-1's start: take two records and divide the difference by the time between them. Cycles are
the CPU's, 400 a microsecond; an audio block is `block_cycles` (580,525). Counts of cycles are 64-bit, in two
halves (`_lo`, `_hi`).

The record is a header, then `sections` sections one after another, each at an 8-byte boundary: a section header,
then `count` entries of `entry_size` bytes. Its last 4 bytes repeat the header's `seq`. A newer core may add fields at
an entry's end (its `entry_size` grows) and new sections: find a section by its tag, step through its entries by
`entry_size`, read a field only when `entry_size` covers it, and skip what you do not know. A record with 40 plugins
is about 7 KB. The layout below is not in `opus_plugin.h`: copy the declarations you need into your plugin.

```c
#define OPUS_STATS_MAGIC    0x5453504Fu      /* "OPST" */
#define OPUS_STATS_VERSION  1
struct opus_stats_head {
    unsigned magic;
    unsigned short version, head_size;
    unsigned size;
    unsigned seq;
    unsigned cycles_lo, cycles_hi;
    unsigned uptime_ms;
    unsigned block_cycles;
    char opus[24];
    unsigned char core_major, core_minor;
    unsigned char developer_mode;
    unsigned char sections;
    unsigned short panel_rows;
    unsigned short parts, plugins;
    unsigned short pad;
};
struct opus_stats_section {
    unsigned short tag;
    unsigned short entry_size;
    unsigned short count;
    unsigned short pad;
};
```

- `magic`: `OPUS_STATS_MAGIC`; `version`: `OPUS_STATS_VERSION`; `head_size`: this header's bytes; `size`: the whole
  record's bytes, the closing `seq` included; `seq`: the record's number.
- `cycles_lo`, `cycles_hi`: the CPU's cycle counter when the record was made; `uptime_ms`: milliseconds since the
  OP-1 started; `block_cycles`: an audio block's cycles.
- `opus`: Opus's version, as text; `core_major`, `core_minor`: the core's version; `developer_mode`: 1 while it is on.
- `sections`: how many sections follow; `panel_rows`: the panel's long side, 376, 320 or 400 (0 unknown); `parts`,
  `plugins`: the entries in the parts and plugins sections.
- `struct opus_stats_section`: `tag` (`OPUS_STATS_*` below), then its entries' size and count.

```c
#define OPUS_STATS_AUDIO    1
struct opus_stats_audio {
    unsigned block_lo, block_hi;
    unsigned blocks;
    unsigned late;
    unsigned late_ms;
    unsigned pad;
    struct { unsigned lo, hi, count; } stage[4];
};
```

The audio, one entry: `block`, every audio block's cycles so far (the OS's and every plugin's); `blocks`, the audio
blocks so far; `late`, the blocks that came late so far (each played the block before again: a click); `late_ms`,
`uptime_ms` at the last late one (0 none); `stage`, the parts of the block so far (their cycles and how often they
ran): 0 the instruments (the synth's or the drum's voices), 1 their effects, 2 and 3 the master.

```c
#define OPUS_STATS_HISTORY  2
struct opus_stats_history {
    unsigned short load;
    unsigned short worst;
    unsigned char late;
    unsigned char pad;
};
```

The audio's load twice a second over the last minute, oldest first, an entry a reading: `load`, the average block in
those 500 ms in tenths of a percent of a block; `worst`, the worst block (its wait for the interrupt included), in
tenths of a percent; `late`, the late blocks.

```c
#define OPUS_STATS_PARTS    3
struct opus_stats_part {
    unsigned audio_lo, audio_hi;
    struct { unsigned lo, hi; } work[4];
    unsigned budget;
    unsigned worst;
    unsigned over;
    unsigned off_at;
    unsigned char most;
    unsigned char off;
    unsigned char unlimited;
    unsigned char pad;
    short plugin;
    unsigned short name;
};
```

Every part that runs code, the core's own parts first, then each loaded plugin:

- `audio`: its audio work so far (its audio handlers, its effects' and engines' sound calls).
- `work`: its other work so far, by thread: 0 its block handlers (the sequencer thread), 1 keys and turns, 2 drawing
  (its page, panel or overlay; audio interrupts during the draw included), 3 the main pass.
- `budget`: its CPU figure, the cycles a block (0: none); `worst`: its worst audio block so far; `over`: the audio
  blocks over its figure by more than a quarter so far; `most`: the most such blocks in any 64 so far (shown, not acted
  on since core 2.33); `off`: 1 when its audio is switched off this start (late blocks, or a block alone); `off_at`:
  its cycles when it was switched off (0: not off); `unlimited`: 1 for a manifest's `cpu user` (measured, never switched off).
- `plugin`: its entry in the plugins section, -1 for the core's own part; `name`: a core part's name (a string), 0xFFFF
  for a plugin's.

```c
#define OPUS_STATS_PLUGINS  4
#define OPUS_STATS_LOADED   0
#define OPUS_STATS_REFUSED  1
#define OPUS_STATS_SKIPPED  2
#define OPUS_STATS_OFF      3
#define OPUS_STATS_FAILED   4
struct opus_stats_plugin {
    unsigned ram;
    unsigned base, image;
    short part;
    unsigned char state;
    unsigned char bank;
    unsigned short id, name, version, why;
};
```

Every plugin file the core found, loaded or not: `ram`, its memory (its code, its zero-filled data, its
allocations); `base` and `image`, where its code lies and its bytes (a crash's address less `base` is an offset into
its build); `part`, its entry in the parts section (-1: not loaded); `state`, loaded, refused, skipped (after a
crash), off (switched off in the mods list) or failed (its `opl_init` refused); `bank`, its memory's bank: 1 the near
pool, 2 the far pool, which the display shares (slower while it sends a frame); `id`, `name`, `version` and `why` (the
reason it is not loaded), strings.

```c
#define OPUS_STATS_THREADS  5
struct opus_stats_thread {
    unsigned cycles_lo, cycles_hi;
    unsigned stack;
    unsigned used;
    unsigned char priority, pad;
    unsigned short name;
    unsigned pad2;
};
```

The OS's threads: `cycles`, the thread's run time so far (the interrupts that came while it ran included); `stack`,
its stack's bytes, and `used`, the most of it ever used; `priority`; `name`, a string.

```c
#define OPUS_STATS_DRAW     6
struct opus_stats_draw {
    unsigned engine_page_worst;
    unsigned cost[9];
};
```

The drawing services so far, one entry: `engine_page_worst`, the slowest draw of an engine's page; `cost`, the
cycles of every page's, menu's and overlay's drawing by kind: text, line, path, dot, fill, art, keep, put, dim.

```c
#define OPUS_STATS_CRASH    7
struct opus_stats_crash {
    unsigned number;
    unsigned uptime_ms;
    unsigned char kind;
    unsigned char fresh;
    unsigned short plugin;
    unsigned short text;
    unsigned short pad;
};
```

The last crash, when there is one: `number`, its log on the USB disk, `crash/NUMBER.txt`; `uptime_ms`, how long the
OP-1 had run; `kind`, 1 the OS stopped on an assert, 2 an exception, 3 it froze; `fresh`, 1 when this start found it
(the run before this one crashed); `plugin`, the id of the plugin whose code it was in (a string; 0xFFFF none);
`text`, its first line (a string).

```c
#define OPUS_STATS_STRINGS  8
```

Text, each string ending in a 0 byte (`count` is its bytes). The other sections name a string by its offset here.

```c
#define OPUS_STATS_FRAME    9
struct opus_stats_frame {
    unsigned frames;
    unsigned os_worst;
    unsigned os_lo, os_hi;
    unsigned drawn, skipped;
    unsigned compare_lo, compare_hi;
    unsigned short worst_screen, worst_mode;
    unsigned worst_ms;
};
```

The screen, one entry: `frames`, the frames drawn so far; `os_worst`, the OS's slowest frame; `os`, the OS's own
drawing so far (every stock screen, overlay and popup; the audio interrupts during it and what plugins drew inside it
taken out: those are in each part's `work[2]`); `drawn` and `skipped`, frames drawn and frames left as they were (a
still screen, [Frames](#frames)); `compare`, the core's comparing of each frame with the last; `worst_screen` and
`worst_mode`, the slowest frame's screen id and the OS's mode then (0 synth, 1 drum, 2 tape, 3 mixer, 8 a list, 9
help); `worst_ms`, when it was, in milliseconds since the start. Frames a second and a frame's cost come from two
records.

Finding a section (opus.monitor's way):

```c
static unsigned char rec[16384] __attribute__((aligned(8)));
static int rec_size;                           /* rec_size = opus_stats(rec, sizeof rec); */

static const unsigned char *section(int tag, unsigned *count, unsigned *size)
{
    const struct opus_stats_head *h = (const struct opus_stats_head *)rec;
    unsigned at = ((unsigned)h->head_size + 7) & ~7u;
    for (int i = 0; i < h->sections; ++i) {
        at = (at + 7) & ~7u;
        if (at + sizeof (struct opus_stats_section) > (unsigned)rec_size)
            break;
        const struct opus_stats_section *s = (const struct opus_stats_section *)(rec + at);
        at += sizeof *s;
        if (s->tag == tag) {
            *count = s->count;
            *size = s->entry_size;
            return rec + at;
        }
        at += (unsigned)s->entry_size * s->count;
    }
    *count = *size = 0;
    return 0;
}
```

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

A disk mode that never ends (the OP-1 switched off or unplugged in it, no eject) is caught at the next start: the
core hands the files to `take` the same way, before the OS empties the disk, deletes nothing, runs no `changed`, and
restarts the OP-1 once so every plugin starts with them (the boot picture's bar shows it).

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

```c
void opus_folder_refused(const char *why);
```

From inside `take`, before it returns 0 (core 2.20): the reason it refuses the file, shown to the user as a notice
with the file's name ("splash/me.png: not a png"). Keep it short: the line holds about 48 characters, the name
included. `why` must stay valid after `take` returns: use a string literal.

```c
if (!is_png(incoming)) {
    opus_folder_refused("not a png");
    return 0;
}
```

```c
int opus_folder_adopt(struct opl_self *self, const struct opus_folder *f, const char *ending);
```

Your folder adopts files of an ending that the user leaves in `/mods` itself instead of in `/mods/NAME` (core 2.32).
At disk mode's end the core moves each such file into your folder on the disk, before any folder reads it; your `take`
then gets it like any of its files, and the user sees a notice saying where it went ("bank1.syx: moved to mods/dx")
and a line in the load report. Without it, a file in the wrong place is refused, and if the user moved it out of your
folder, the folder's copy is gone too. Examples: a synth adopts its `.syx` banks; the bridge adopts op1r's `.op1p` and
Hundekraft's `.knl` files.

- `f`: your own folder, registered with `opus_on_folder` first.
- `ending`: a dot and 1-6 letters or digits, such as `".syx"` (upper or lower case alike on the disk). Not `.opmod`
  (the plugins' own), and not an ending another loaded plugin's folder adopts; eight endings in all.
- It counts while your plugin is loaded. With your plugin missing or turned off, such a file is refused as before
  ("not a .opmod file"): the core knows no plugin's file types of its own.
- From `opl_init`, once an ending. Returns 1, or 0 refused (the boot log says why).

```c
/* in opl_init: */
if (opus_on_folder(self, &banks))
    opus_folder_adopt(self, &banks, ".syx");
```

## The boot picture

```c
int opus_boot_picture(struct opl_self *self, const char *picture, const char *name);
```

The picture the OP-1 shows while it starts, in place of Teenage Engineering's logo (core 2.33). The start comes before
any plugin runs, so the core records what you give it and shows it from the next start on, as long as your plugin is
installed (its file in `/mods`); without your plugin, TE's logo shows again.

- `picture`: the full path of a file in your own folder, in the core's form: the word `"OPSP"` (0x5053504F), then
  `width | height << 16`, then `height` rows of `width` RGB565 pixels. At most the panel's long side wide and 168/188 of
  its short side high (`opus_panel_size` gives both): the core centres it and leaves the rest black.
- `name`: the full path of a text file whose first line (up to 23 characters) is drawn below the picture in TE's
  console font, or 0.
- `picture` 0 takes yours away (only when the picture shown is yours).

The core writes its record only when it changes. From `opl_init`, or a folder's `take` or `changed`. Returns 1, or 0
(a path of 96 characters or more). opus.splash is the example: it fits a PNG from the disk's `mods/splash` to this
OP-1's panel and hands it over.

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

While the plugin's row is selected, your `knob` gets the green turns in the key thread: `encoder` 1 (green);
`steps` in OS units (4 per detent), positive clockwise. Return 1 to have the panel drawn again. Blue moves the list,
white picks the list's order and orange turns the plugin on or off (from core 2.23; before it, your `knob` got white
and orange too, as `encoder` 2 and 3).

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

## A module's own page

Since core 2.31 every module type a plugin adds has the same optional page members, with the same names and meaning: an
effect, a synth engine, a drum engine, an LFO type and a sequencer. They come last in the type's struct:

```c
const char *svg;
void (*screen_init)(void *scr);
void (*screen_draw)(void *scr, int dt);
int (*page_key)(struct opus_page *pg, int key, int pressed);
int (*page_turn)(struct opus_page *pg, int encoder, int detents);
```

- `svg`, `screen_init`, `screen_draw`: its own screen on its page, an SVG design the OS draws, with what moves drawn over
  it. `svg` is the full path of the file whose front is the screen's SVG (check it with `plugin_tool.py svg`); it is
  loaded once at the start, `screen_init(scr)` runs once after that, and `screen_draw(scr, dt)` every frame the page shows,
  in the display thread: it draws the document itself (`fw_screen_draw(scr)`) and what moves, and asks `opus_page_now`
  what to show. If the file is refused (the boot log says why), the type's plain page shows instead (an effect's `view`,
  an engine's or LFO type's drawn `page`): keep one, for that and for older cores.
- `page_key(pg, key, pressed)`: a key on any of its pages. FN + an encoder's push passes it by.
- `page_turn(pg, encoder, detents)`: a turn there (0 blue .. 3 orange), in detents (0 when less than one).
- Both run in the key thread and are told what the page shows (`pg`, below). Change `pg->par` in place and return 1: the
  core keeps the change and saves it with the sound, wherever the module is. Return 0 to leave the event as it was going
  (the OS's own handling). When set they come instead of the type's older `key` and `turn`, which stay for plugins built
  before 2.31.

```c
struct opus_page {
    short kind;        /* OPUS_PAGE_EFFECT, _ENGINE, _DRUM, _LFO or _SEQ */
    short t;           /* the T key whose page it is, 1-4; a sequencer's 0 */
    short where;       /* 0 synth, 1 drum, 2 master */
    short slot;        /* the sound slot, 0-7 */
    short on;          /* an effect's or an LFO's on; 1 for the others */
    short n;           /* values in par */
    short key;         /* a drum engine's current key (0-23), else -1 */
    short live[8];     /* par's first eight as the sound hears them now */
    volatile short *par;
    void *inst;
    const void *module;
};
int opus_page_now(struct opus_page *pg);
```

`opus_page_now` fills `pg` and returns 1 while a page of a plugin's module shows (an effect page of any type, the OS's own
included), else 0. Any thread but the audio: `screen_draw` asks it, and so can a plugin's own key or frame hooks. `module`
is the struct you registered (compare it with yours); `par` what the page edits:

| page | `kind` | `t` | `par` (`n`) | kept as | `inst` | its screen's place |
|---|---|---|---|---|---|---|
| an effect's | `OPUS_PAGE_EFFECT` | 3 | its eight params: the sound's, or a manager's for its extra slot | a knob turn's save, in any slot | its `struct opus_fx *` | the filter's |
| a synth engine's T1, T2, T4 | `OPUS_PAGE_ENGINE` | 1, 2, 4 | the sound's eight knobs (knob 4 is the core's: a change to it is not kept) | the knobs' save | 0 | iter's (T1) |
| a drum engine's T1 | `OPUS_PAGE_DRUM` | 1 | the current key's eight numbers, kept inside dbox's limits | the kit's save | 0 | dbox's |
| an LFO type's T4 | `OPUS_PAGE_LFO` | 4 | its settings (`nparams`) | `opus_lfo_changed` | the slot's `struct opus_lfo *` | the MIDI LFO's |
| a sequencer's | `OPUS_PAGE_SEQ` | 0 | none (`n` 0): it keeps its own data | | 0 | finger's |

An effect's page never says whether the effect is a sound's own or a manager's second (Fx overdrive): an effect type does
not know who runs it. The same type can be both at once, each page with its own `par` and `inst`, so keep what a sound
should recall in its parameters, and nothing of one instance in your statics; touch `inst`'s state only (its bulk and fast
memory belong to the audio). `inst` is 0 when nothing runs it at that moment.

Name the fields when you fill a module's struct (`.page_turn = turn,`), as every example does: a struct filled in order
stops compiling when the SDK adds members, since the build treats a missing initializer as an error. Needs core 2.31.

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
    unsigned fast_moves;
};
```

What one instance needs, filled in by the type's `need` call:

- `state`: bytes of ordinary memory, cleared: the instance's variables.
- `bulk`: bytes of ordinary memory, cleared: delay lines, a reverb's tank.
- `fast`: bytes wanted in fast memory (as `opus_fast`), else ordinary memory. Not cleared.
- `fast_moves` (core 2.21): 0 (an older type's `need` leaves it so): the `fast` part stays where it was made, for good,
  and the type may keep pointers into it. `OPUS_FAST_SCRATCH + 1` or `OPUS_FAST_STATE + 1`: the type reads `fx->fast`
  at every call and keeps no pointer into it past the call, so the core may move it: the fast memory then goes to the
  instances that play, by the plugin's priority, as a lease ([Memory](#memory)) of that kind (scratch: nothing kept
  across a move; state: its bytes kept). opus.plate and opus.chorus use `OPUS_FAST_STATE + 1`.

The OS's synth and drum slots never run in the same block, so their two instances share one `bulk` and one `fast`
area (each has its own `state`): keep in `state` whatever says where you are in them (a write position).

```c
struct opus_fx {
    void *state, *bulk, *fast;
    int stereo;
};
```

An instance as the type's calls get it: its memory (0 where the need was 0), and `stereo` 1 for the master's kind,
0 for mono (synth, drum). Take `bulk` at each `init`: after the next one it may lie elsewhere (below). With
`fast_moves`, read `fast` at every call.

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
    /* core 2.31, each optional: its own page (A module's own page) */
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    int (*page_key)(struct opus_page *pg, int key, int pressed);
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);
    /* core 2.33, optional */
    const char *aliases;
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
- `svg`, `screen_init`, `screen_draw`, `page_key`, `page_turn` (core 2.31): its own page, in whichever slot it runs
  ([A module's own page](#a-modules-own-page)): its screen instead of `view`'s page, its pushes and turns with
  `pg->par` the page's eight parameters and `pg->inst` the instance. A turn with shift held goes to `shift_knob` when it
  takes it; `page_turn` is not called while the effect is off. Without `page_turn`, or for a turn it leaves, the encoder
  moves its parameter by `limits`.
- `aliases` (core 2.33): other names the type answers to, separated by spaces, or 0: a name it had before a rename, or
  the form an older Opus saved it in (`"opus1:8"`: Opus 1's effect number 8, in its settings). Where an older name turns
  up, the core gives it to the type that lists it; it keeps no list of its own. An older core ignores the field.

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

The OS's three slots' instances (synth and drum sharing `bulk` and `fast`, and the master's) are made now, their
`state` from the plugin's memory. Their `bulk` has a home of its own and, while the type is a sound's effect in that
slot, is lent memory from the audio pool (which the display does not slow), given back when another effect takes
the slot; a plugin of higher `priority` comes first. Each `init` gets `bulk` where it is now: take pointers into it
in `init`, never before. Their `fast` is fast memory for good while it lasts, or on lease with `fast_moves`.

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
run, so it never shows in a second slot (Fx overdrive). Needs core 2.6. For an effect of your own with its own screen,
pushes and turns, use an effect type with `svg`, `page_key` and `page_turn` ([Effects](#effects), core 2.31): it runs in every
slot, with its full parameter ranges.

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
    signed char shift_knobs[4];
    const short (*keys)[8];
    void (*note_key)(void *voice, const short *knobs, int key, int velocity);
    int (*group)(const short *knobs, int key);
    void (*choke)(void *voice);
    int (*page_key)(struct opus_page *pg, int key, int pressed);       /* core 2.31 */
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);
};
```

An engine. The struct starts with its size; the fields from `flags` on are optional (a shorter struct has none of
them).

- `size`: `sizeof (struct opus_engine)`.
- `name`: the engine list's label and a new sound's name: lower case, up to 15 characters.
- `knobs`: a new sound's eight knobs, 0-32767. Knob 4 is the core's (the mark): its value here is not used.
- `voice`: bytes of state per voice (six voices; a drum engine's twelve). They come from fast memory while it lasts,
  else from the pool; with `OPUS_ENGINE_STATE_MOVES`, from a lease.
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
- `mark`: the mark to claim, or 0 for the core's own: the number sounds made elsewhere carry in knob 4, so they find
  this engine: 0x5301-0x53FF (another firmware's; core 2.6) or 0x4F01-0x4FFF (an older Opus's, such as Opus 1's
  dimension 0x4F01; core 2.33). The core keeps no list of who used which: the engine that claims a number knows it.
- `svg`, `screen_init(scr)`, `screen_draw(scr, dt)`: its own OS screen on T1 instead of `page`, or 0. `svg` is a full
  path to a file whose start is the screen's SVG (checked as art is, loaded once at the start); `screen_init` runs once
  after that (find the document's nodes); `screen_draw` every frame it shows, in the display thread: it draws the
  document itself through the firmware's screen code, and what moves. `scr` is an OS screen object. Advanced: for
  porting engines written as OS screens. Needs core 2.6.
- `shift_knobs`: the knob each encoder (blue, green, white, orange) turns on T1 with shift held, 5-7, or -1 for none
  (a choice kept in the engine's data, say); any other value means none. It tells LFO types which knob
  `OPUS_LFO_ENGINE_SHIFT` moves ([LFO types](#lfo-types)); the engine's `turn` handler still does the turning.
  examples/opus.outatime sets `{ spread, tone, flux, -1 }`. Needs core 2.21.
- `keys`, `note_key`, `group`, `choke`: a drum engine's ([Drum engines](#drum-engines)); 0 for a synth engine. Needs
  core 2.24.

```c
#define OPUS_ENGINE_OWN_ENVELOPE 1
#define OPUS_ENGINE_OWN_LFO 2
#define OPUS_ENGINE_OS_VOICES 4
#define OPUS_ENGINE_SAMPLE 8
#define OPUS_ENGINE_DRUM 16
#define OPUS_ENGINE_STATE_MOVES 512
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
- `OPUS_ENGINE_SAMPLE`: the engine plays the slot's sample: the OS treats its sounds' sample as the sampler's (see
  [Samples](#samples)). Needs core 2.10.
- `OPUS_ENGINE_DRUM`: a drum engine, in the drum's engine list instead of the synth's ([Drum engines](#drum-engines)).
  Needs core 2.24.
- `page_key`, `page_turn` (core 2.31): its pages' keys and turns ([A module's own page](#a-modules-own-page)): `pg->t`
  says T1, T2 or T4, `pg->par` the sound's knobs, or on the drum the current key's numbers. They also reach its own T1
  screen (`svg`), which before 2.31 got no turns or keys from the core, and a drum engine's `svg` is its T1 screen since
  2.31.
- `OPUS_ENGINE_STATE_MOVES`: the engine keeps no pointer to a voice's state past a call (it uses the one each call
  gives), so the core may move the voices' states between fast and ordinary memory between blocks: they go on one
  lease of the `OPUS_FAST_STATE` kind ([Memory](#memory)), fast while the engine plays, by the plugin's priority.
  Without it the states stay where they were made. examples/opus.outatime sets it. Needs core 2.21.

```c
int opus_engine_add(struct opl_self *self, const struct opus_engine *e);
```

Adds an engine, from `opl_init`. The struct must stay valid: make it `static const`. Returns its mark (from 0x100 to
32767, the same on every OP-1: made from the plugin's id and the engine's name, or the one it claims), or:

- -1: a bad struct (too short, no name, no `render`) or a claimed mark outside 0x4F01-0x4FFF and 0x5301-0x53FF (since core 2.9 there
  is no fixed count of engines; core 2.0-2.8 also refused a 33rd, or a 9th with a `block` handler);
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

The current sound's knobs for a page, and 1 when the current sound is an engine's. Since core 2.8 these are
`opus_engine_live`'s (the LFO in while a note sounds), so every value a page shows follows the modulation, as the
stock screens do; core 2.3-2.7 gave the stored knobs (what the user set). Any thread. Needs core 2.3.

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

### Drum engines

A drum engine (core 2.24) is a row of the drum's engine list (shift + T1 in drum mode), never the synth's: a `struct
opus_engine` with `OPUS_ENGINE_DRUM` in `flags`. Its kits are the OS's dbox kits (the stock drum engine's: 24 keys of
eight numbers each), with a member naming the engine, so the OS saves, loads, copies and turns them as dbox's: on T1
the four encoders turn the current key's numbers 0-3, with shift 4-7 (number 0 from -24320 to 24832, the others
0-32767), and the drum's LFO moves them. Its page is the drum's T1 (`page`); T2 and T4 stay the OS's.

Where a drum engine's fields differ from a synth engine's:

- twelve voices: `voice` bytes of state for each, `init`'s `index` 0-11.
- `keys`: a new kit's numbers, the 24 keys' eight each (a `static const short kit[24][8]`), or 0 for `knobs` on every
  key.
- `note_key(voice, knobs, key, velocity)`: a note, called instead of `note_on`: `key` 0-23, `knobs` that key's eight
  numbers, `velocity` 1-127 (127 from the OP-1's own keys). Key thread. Or 0 for `note_on`.
- `render(voice, out, par, pitch)`: 128 samples into `out`, as a synth engine's. `par` is the playing key's eight
  numbers as the LFO left them; `pitch` points at one value, the voice's pitch ratio x 65536 (65536 as played; the
  drum octave and a sequencer's transposition in it, 2^(semitones / 12)). Before core 2.26 `pitch` is 0.
- `finished(voice)`: required. On the drum it is asked every block from the note on (a hit ends on its own); a voice
  it frees renders no more until a note.
- `group(knobs, key)`: the key's choke group, 1-15, or 0 for none; or 0 for no groups at all. A note in a group stops
  the busy voices of the other keys in that group through `choke`, as a closed hi-hat stops an open one.
- `choke(voice)`: cut the voice now (a fade of a few milliseconds). Or 0.

`opus_engine_add` refuses (-1) a drum engine without `finished`, or with `OPUS_ENGINE_OS_VOICES`, `OPUS_ENGINE_SAMPLE`,
a claimed `mark` or a `type`.

```c
int opus_drum_key(short par[8]);
```

The drum's current key, 0-23 (the last note key played: the one T1's encoders turn), returned, and its eight numbers
as the current kit stores them, into `par` (core 2.24). For a drum engine's page. Any thread.

```c
static const short KIT[24][8] = { /* ... */ };
static const struct opus_engine SIM = {            /* opus.sim */
    .size = sizeof (struct opus_engine),
    .name = "sim",
    .knobs = { 0, 16000, 0, 0, 0, 0, 0, 0 },
    .voice = sizeof (struct voice),
    .init = init,
    .render = render,
    .page = page,
    .flags = OPUS_ENGINE_DRUM,
    .finished = finished,
    .keys = KIT,
    .note_key = note_key,
    .group = group,
    .choke = choke,
};
```

## Samples

Every synth slot has a sample buffer, whatever its engine: 264,600 frames (6 seconds) of 16-bit mono at 44.1 kHz. The
stock OS fills it, saves it and samples into it only for a sampler's sound. An engine whose struct sets
`OPUS_ENGINE_SAMPLE` (a sample engine) gets the same for its sounds (core 2.10):

- loaded: at the start, when the browser loads one of its presets, and when it is chosen in the engine list (the
  slot's last saved sample; chosen over a sampler's sound, the buffer keeps that sample, its key and its end);
- saved: with the OS's save (hold a slot key) and the overwrite save, in the preset's `.raw`, and the slot's own
  sample whenever it changes;
- on the USB disk: its presets' and its slot's `.aif` carry the sample, and an imported one brings it back;
- sampled: mic or shift + mic opens the input menu with sampling on, as for the sampler; a held note records into the
  slot's buffer, and when it ends the sound stays the engine's (the stock finish would make it a sampler), with the
  key it was sampled at and the recorded end kept in it.

In the engine list a sample engine's row is a folder with the sampler's samples as its column, as SAMPLER's is: a
sample chosen there loads into the slot and the sound stays the engine's (its knobs kept when it already was), named
after the sample, with its key and end. Moving onto the folder from another row chooses the engine and keeps the
slot's sample.

```c
const short *opus_sample(int slot, unsigned *frames);
int opus_sample_sound(int slot);
unsigned opus_sample_base(int slot, unsigned *end);
```

- `opus_sample(slot, &frames)`: synth slot `slot`'s (0-7) sample, and its length in `frames` (264,600). The slot
  sounding now is `opus_sound_slot(0)`. Read only, from any thread; the OS writes it while it loads or records a
  sample. 0 for a slot outside 0-7.
- `opus_sample_sound(slot)`: 1 when the slot's sound keeps a sample with it (a sampler's or a sample engine's). A
  plugin that saves a sound itself saves the slot's `synth_N.raw` with it then, as the OS's own save does.
- `opus_sample_base(slot, &end)`: the key the slot's sound was sampled at, as the OS's `base_freq` in milli-Hz
  (174.614 Hz x 2^(k/12), k the key that ended the recording, 0-23 from the left: 523,251 for the C), and in `end` the
  recorded end in frames. They travel with the sound and its presets. 0 (and `end` 0) when the sound was never
  sampled. Any thread (the core's copy, as `opus_engine_data`).

A voice that plays the sample at the played key's pitch steps through it at played Hz / base Hz frames a frame (44.1
kHz both ways).

## Sequencers

The core's sequencer service (core 2.8) adds a sequencer to the OS's sequencer list (shift + the sequencer key).
Chosen for the synth or the drum, it takes finger's place for that instrument: the OS asks it for its player, the
sequencer key shows its page, and op1.db remembers the choice, so it comes back at the next start. With none chosen,
finger is finger. Opus's bridge runs op1r's and Hundekraft's sequencers this way.

```c
struct opus_os_vfn {
    short adjust, pad;
    void *fn;
};
struct opus_sequencer {
    unsigned size;
    const char *name;
    void *(*player)(int mode);
    int (*key)(int mode, int code, int pressed);
    int (*turn)(int mode, int encoder, int delta);
    void (*midi_start)(int mode);
    void (*chosen)(int mode, int on);
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    const char *alias;
    int (*page_key)(struct opus_page *pg, int key, int pressed);       /* core 2.31 */
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);  /* in detents: turn had the OS's raw delta */
};
int opus_seq_register(struct opl_self *self, const struct opus_sequencer *s);
int opus_seq_mode(void);
```

`opus_seq_register` from `opl_init`; it returns the sequencer's index, or -1 (a bad struct, a name taken, or 16
sequencers already). `mode` is 0 for the synth, 1 for the drum; `opus_seq_mode()` says which instrument the OS's
sequencer calls are for at the moment (the player reads it too).

- `name`: its row in the list and its name in op1.db: 1-15 characters, lowercase letters, digits, space or `-`. Not a
  stock sequencer's (pattern, endless, tombola, finger, sketch, arpeggio) nor another plugin's. Never rename it once
  people use it: their saved choice names it. `alias`: an older name that also chooses it, or 0; the next save writes
  `name`.
- `player(mode)`: the player object for the instrument, in the OS's own player layout. Its first word points at a
  table of `struct opus_os_vfn` entries; the OS calls an entry as `fn(this + adjust, ...)`. The entries the OS uses, by
  offset in the table: +0x14 `key_down(this, key, shift)` for the keys 0-23 (non-zero: taken; 0: the key plays its
  note), +0x1C `key_up(this, key)`, +0x24 the instrument changed, +0x2C `tick(this, pos)` 96 times a beat while the
  sequencer runs (the current instrument's player only), +0x34 once a frame, +0x3C stop. They run in the OS's threads
  (the tick in the sequencer thread): short work, no files, no drawing.
- `key(mode, code, pressed)` and `turn(mode, encoder, delta)`: on its page, every key and encoder comes here first;
  return 1 to take it, 0 to leave it to finger's handling. The octave keys (left and right arrows) never come here:
  they move the keyboard's octave, as on the stock sequencers' pages. `encoder` 0-3 is blue to orange (4-7 with shift held); `delta` is the
  OS's count for the turn, positive to the right.
- `midi_start(mode)`: MIDI START came in over USB (restart at the first step). `chosen(mode, on)`: it was chosen for
  the instrument, or another was.
- `svg`, `screen_init`, `screen_draw`: its own page, as an engine's (see [Engines](#engines)); without them finger's
  page shows.

A sequencer plays notes the way the OS's own sequencers do: `fw_make_midinote` for a key's event, then
`fw_vc_note_on(ev, 0)` and later `fw_vc_note_off(ev)` (`op1fw.h`), from its tick. It saves its own data (a file in
its folder, or its own op1.db table): the core keeps only which sequencer is chosen.

## LFO types

The core's LFO service (core 2.21) adds a plugin's LFO to the OS's LFO list (shift + T4), as a row beside the stock
ones, with its own page on T4. Opus's matrix (opus.matrix) is one. The manifest names it with an `adds lfo NAME` line
([manifest.md](manifest.md)).

A sound keeps the LFO's settings as Opus's record: the type's name and `nparams` numbers in the sound's `"opus"`
member, so they save, load and copy with the sound. The sound's own LFO fields say a stock MIDI LFO at rest, so a
stock OP-1 or another firmware plays the sound with no LFO and never refuses it. A sound whose LFO type's plugin is
not loaded keeps its record and plays with no LFO.

While a sound is in its instrument's current slot and its LFO is on (T4), the audio block calls the type: `tick`
once a block; `apply` at each of the instrument's passes, where it moves its targets with `opus_lfo_move`; `trigger`
when a note starts (for its voice, then for the instrument). A pass works on one copy of the sound: one voice's, or
the instrument's own (`voice` 255). The passes of a block, in order:

| Pass | Instrument | For | What `opus_lfo_move` reaches there |
|---|---|---|---|
| 1, then 0 | synth | each sounding voice | 1: the envelope; 0: the engine's knobs, their shift layer, the voice's volume and pitch |
| 2 | synth | the instrument | the effect's parameters, ring and duck |
| 3 | drum | each sounding voice | the engine's knobs |
| 4 | drum | the instrument | the envelope |
| 5 | drum | the instrument | the effect's parameters, ring and duck |

`tick` comes once a block, just before the apply of pass 2 (synth) or 4 (drum). An LFO type's page shows on the
synth's T4, and since core 2.27 on the drum's T4 too.

```c
struct opus_lfo {
    unsigned char instrument;
    unsigned char slot;
    unsigned char voice;
    unsigned char pass;
    short *params;
    void *state;
};
```

What the type's calls get:

- `instrument`: 0 synth, 1 drum; `slot`: the sound slot, 0-7.
- `voice`: the voice of a voice's pass or trigger (synth 0-5, drum 0-11); 255 for the instrument's own.
- `pass`: in `apply`, 0-5 (the table above).
- `params`: the slot's settings, `nparams` numbers: its sound's record.
- `state`: the type's memory for this slot, `state_size` bytes, cleared when the slot's sound loads and when the user
  picks the type.

```c
struct opus_lfo_type {
    unsigned size;
    const char *name;
    unsigned short nparams;
    unsigned short state_size;
    const short *defaults;
    void (*tick)(struct opus_lfo *l);
    int (*apply)(struct opus_lfo *l);
    void (*trigger)(struct opus_lfo *l);
    void (*page)(void);
    int (*turn)(int encoder, int detents);
    int (*key)(int key, int pressed);
    /* core 2.31: its own T4 screen, and the unified page calls (A module's own page) */
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    int (*page_key)(struct opus_page *pg, int key, int pressed);
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);
};
int opus_lfo_register(struct opl_self *self, const struct opus_lfo_type *t);
```

- `size`: `sizeof (struct opus_lfo_type)`.
- `name`: its row in the list and the type its sounds' records name: 1-15 characters, without `'`, `"`, `%` or `\`.
  Never rename it once sounds use it.
- `nparams`: its settings, 1-64 numbers. `defaults`: `nparams` numbers, the settings when the user picks the type.
- `state_size`: bytes of memory for each slot (`l->state`), or 0.
- `tick(l)`: the audio block, once a block; or 0.
- `apply(l)`: the audio block, each pass: move what it moves with `opus_lfo_move` and return 1 when it moved
  something. Required.
- `trigger(l)`: the audio block, when a note starts; or 0.
- `page()`: its T4 page, in the display thread every frame while it shows, as an engine's page: the whole drawing,
  with the `opus_page_*` functions; `opus_lfo_page()` gives the slot it shows. Or 0: the stock MIDI LFO's page shows.
- `turn(encoder, detents)`: a turn on its page, key thread: `encoder` 0 blue to 3 orange, in detents. After it
  changes the settings, call `opus_lfo_changed`. Return 1 to take the turn. Or 0.
- `key(key, pressed)`: a key on its page (the encoders' pushes, 0x3B-0x3E), key thread; return 1 to take it. Or 0.

The audio calls run inside the audio block: keep them short. `opus_lfo_register` registers the type, from `opl_init`;
the struct must stay valid (`static const`). Returns 1, or 0 for a bad struct (too short, a bad name, `nparams` 0 or
above 64, no `defaults` or no `apply`), a name another type has, or no memory.

```c
#define OPUS_LFO_ENGINE 0
#define OPUS_LFO_ENVELOPE 1
#define OPUS_LFO_EFFECT 2
#define OPUS_LFO_ENGINE_SHIFT 3
#define OPUS_LFO_SOUND 4
int opus_lfo_move(const struct opus_lfo *l, int group, int param, int delta);
```

Only inside `apply`, with the `l` it was given: moves one target of this pass's copy of the sound by `delta`. The
sound itself is never written. For the OS's own values the move is the OS's own turn, as its LFOs make it, kept inside
the value's range. `delta` is -32768 to 32767 (a wider value is clamped): a stock LFO at full depth moves by up to
32767 either way. Returns 1 when done, 0 when the target is not in this pass or does not exist. The targets:

- `OPUS_LFO_ENGINE`: `param` 0-3, the knob each encoder turns on T1 (blue to orange): synth pass 0, drum pass 3.
- `OPUS_LFO_ENGINE_SHIFT`: `param` 0-3, the encoder whose shift-layer knob the current engine names in its
  `shift_knobs` (a plugin engine's; none for others): synth pass 0.
- `OPUS_LFO_ENVELOPE`: `param` 0-3, attack, decay, sustain, release: synth pass 1, drum pass 4.
- `OPUS_LFO_EFFECT`: `param` 0-7, the effect's parameters (4-7, a plugin effect's shift layer, too): synth pass 2, drum
  pass 5.
- `OPUS_LFO_SOUND` (core 2.22), the stock element LFO's last group, `delta` as a fraction (32768 is 1): `param` 0 the
  voice's volume (its gain times 1 + delta) and 1 its pitch (times 2^delta: an octave either way at full), synth pass
  0; 2 ring (the instrument's sound times delta) and 3 duck (the sound less its delta share), synth pass 2, drum pass 5.

```c
struct opus_lfo *opus_lfo_page(void);
void opus_lfo_changed(const struct opus_lfo *l);
```

`opus_lfo_page()`: the slot the T4 page shows, for the type's `page`, `turn` and `key`: its `instrument`, `slot`,
`params` and `state` (`voice` and `pass` 255); 0 when no LFO type's page shows. `opus_lfo_changed(l)`: after a turn or
key changed `l->params`, writes the settings into the slot's sound and keeps it, as the OS keeps a turned knob. Key
thread.

A small LFO type, a sine on the engine's first knob at a rate locked to the beat ([Tempo](#tempo)):

```c
struct wob { unsigned phase; };
static const short DEFAULTS[2] = { 50, 96 };      /* depth 0-99, a cycle's ticks (96: a beat) */

static void tick(struct opus_lfo *l)
{
    ((struct wob *)l->state)->phase = opus_tempo_phase((unsigned short)l->params[1]);
}

static int apply(struct opus_lfo *l)
{
    if (l->pass != 0 && l->pass != 3)               /* the engine's knobs: synth 0, drum 3 */
        return 0;
    int s, c;
    opus_sincos((int)(((struct wob *)l->state)->phase >> 20) - 2048, &s, &c);   /* -4096..4096 */
    return opus_lfo_move(l, OPUS_LFO_ENGINE, 0, s * 8 * l->params[0] / 99);
}

static void page(void)
{
    static char text[16];
    struct opus_lfo *l = opus_lfo_page();
    if (!l)
        return;
    fw_snprintf(text, sizeof text, "depth %d", l->params[0]);
    opus_page_text(me, 0, 40, 60, 12, OPUS_COLOR_E1, text);
}

static int turn(int encoder, int detents)
{
    struct opus_lfo *l = opus_lfo_page();
    if (!l || encoder != 0)
        return 0;
    int depth = l->params[0] + detents;
    l->params[0] = (short)(depth < 0 ? 0 : depth > 99 ? 99 : depth);
    opus_lfo_changed(l);
    return 1;
}

static const struct opus_lfo_type WOBBLE = {
    .size = sizeof (struct opus_lfo_type),
    .name = "wobble",
    .nparams = 2,
    .state_size = sizeof (struct wob),
    .defaults = DEFAULTS,
    .tick = tick,
    .apply = apply,
    .page = page,
    .turn = turn,
};

/* in opl_init: */
if (!opus_lfo_register(self, &WOBBLE))
    return -1;
```

## Tempo

```c
struct opus_tempo {
    unsigned size;
    unsigned mbpm;
    unsigned ticks;
    unsigned frac;
    unsigned step;
    unsigned char external;
    unsigned char playing;
};
void opus_tempo(struct opus_tempo *t);
```

The OP-1's tempo and where the music is (core 2.21), for a plugin that follows the beat: an LFO in note lengths, a
delay in time with the music. Set `size` to `sizeof (struct opus_tempo)` first; the core fills in the rest (a `size`
below the core's struct gets nothing). Any thread, the audio block included.

The clock is the sequencers': 96 ticks a beat, counted once an audio block (about 689 times a second) whether the tape
plays or not. In the tempo screen's tape modes it follows the tape, in its MIDI modes a MIDI clock coming in.

- `mbpm`: the tempo in thousandths of a BPM (120,000 is 120 BPM): the tempo set, or under a MIDI clock the tempo
  measured over its last beats (0 until two beats came).
- `ticks`: ticks since the last reset (the play key, MIDI START: each starts a beat); `frac`: how far into the
  current tick, 0-65535.
- `step`: ticks an audio block, 16.16 fixed point (measured when the clock follows the tape or a MIDI clock).
- `external`: 1 while it follows a MIDI clock.
- `playing`: the tape: 0 stopped, 1 recording, 2 playing.

```c
unsigned opus_tempo_phase(unsigned n);
unsigned opus_tempo_phase_step(unsigned n);
```

For an LFO locked to the beat: a cycle `n` ticks long (1-65535: 96 a beat, 24 a sixteenth, 384 a bar of four beats).
`opus_tempo_phase(n)` is where the music is in that cycle now, as a 32-bit phase (2^32 a whole cycle, 0 at each
reset); `opus_tempo_phase_step(n)` is how far that phase moves in one audio block. Both 0 for `n` outside 1-65535. Any
thread, the audio block included. opus.matrix sets a free-running LFO's phase from `opus_tempo_phase` every block, so
it stays with the music, and steps a note-started one by `opus_tempo_phase_step`, so it runs at the tempo's speed
from its note.

```c
struct opus_tempo t = { sizeof t, 0, 0, 0, 0, 0, 0 };
opus_tempo(&t);
fw_snprintf(text, sizeof text, "%u.%u bpm", t.mbpm / 1000, t.mbpm % 1000 / 100);
```

## Pages

A plugin's own page is the whole 320 x 160 drawing: an engine's T1, T2 and T4 pages, a quick page, or what a frame
handler draws. The `opus_page_*` functions draw only inside such a draw call (an engine's page, a quick page's `draw`,
an `opus_on_frame` or `opus_on_frame_under` handler); anywhere else they draw nothing. Places and sizes are in units,
cut to the drawing; the OP-1's window shows y 2-146. Colours by id ([Colours](#colours)).

```c
void opus_page_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
void opus_page_text_right(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
void opus_page_text_centre(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
```

Text in the OP-1's font, `x`, `y` its top left (`opus_page_text_right`: `x` is its right end; `opus_page_text_centre`,
core 2.21: `x` is its middle). Each plugin has 32 text slots (0-31), shared by the three functions; a slot keeps the
place, size and alignment of its first call, and each call sets its colour and text, so use one slot for each place.
`size`: its height, 6-48; the text must start and end inside the drawing. Write in lower case: the OP-1's font draws
upper-case letters as icons.

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
void opus_page_dim(void);
```

Everything drawn on the frame so far at half brightness, as the OP-1's own help screen darkens the page under it (core
2.28): for an overlay over a page that goes on underneath. What is drawn after it stays bright. It costs about what
the OS's own dimming does (some hundreds of thousands of cycles a frame): call it only while the overlay shows.
opus.monitor dims the page this way from an `opus_on_frame` handler, then writes its figures over it.

```c
int opus_page_art(struct opl_self *self, int page, const char *path);
```

SVG art behind an engine's page: `page` 0 T1, 1 T2, 3 T4; `path` the file's full path (up to 71 characters, at most
128 KB; 64 KB before core 2.8), or 0 for no art any more. The first time the page shows, the file is checked for what would stop the OP-1 in
the OS's SVG parser, then loaded, drawn once and kept; every frame after, the kept picture is put back cheaply and
the page's draw call draws over it. The file loads again after it changed (its size). Returns 1, or 0 for a bad page
or no room (16 art files in all, with `opus_art_open`'s). Any time from `opl_init` on (for example from a folder's
`changed` handler).

The check refuses (and the boot log says why) a missing file, an XML error, a root other than `<svg>`, a number with
a point but no digits after it, more than four decimals, or an exponent, the path commands Q and T, and an id with
`_x` not followed by two characters and `_`. A refused file is never parsed; the page draws without it.

```c
int opus_page_still(struct opl_self *self, unsigned key);
void opus_page_still_done(struct opl_self *self);
```

A page's still part, kept by the core (core 2.8): draw what stays the same between frames first, named by a key that
changes whenever it would (a hash of the values it shows, the mode, the page), then what moves:

```c
if (!opus_page_still(self, key)) {
    /* ...the still part... */
    opus_page_still_done(self);
}
/* ...what moves... */
```

`opus_page_still` puts the part back cheaply and returns 1 when the frame before had the same key and the core kept
it; the same key two frames in a row keeps it. The kept pixels are the core's memory, not the plugin's; the panel and
the colours are in the key already. Inside a page's draw only. A page that redraws many lines or text every frame
costs the display thread much less this way.

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
int opus_replace(struct opl_self *self, const char *os_path, const char *data, unsigned len);
```

Replaces one of the OS's own screens' drawings with the plugin's (core 2.11): `os_path` names the screen's file,
`"/yaffs2/content/display/NAME.svg"` (only screens' drawings so far); `data`, `len` the plugin's SVG, carried as
`opus_art_text`'s is and kept in its folder. Call it from `opl_init`: the OS draws its screens from their files, read
after the plugins start. The OS's own file is never changed, so the OP-1 draws the stock screen again once the plugin
is gone. The core refuses a drawing the OS's parser would stop on, one that lacks an element of the stock drawing (by
its id, read as the parser reads ids: `ok_1_` is `ok`) or makes it another kind of element (the OS finds the parts it
animates by id), and a screen another plugin replaced first; the boot log says why. So start from the stock file and
add to it. Returns 0, -1 (refused) or -2 (taken). opus.overwrite's save screen (OVERWRITING) is one.

```c
int opus_stock_screen(struct opl_self *self, const char *name);
```

Has the OS draw one of its screens as stock again, where Opus 2 ships it reduced (core 2.14). The OS builds every
screen's drawing at the start and keeps it in memory, shown or not, so a screen Opus never shows without a plugin is
installed reduced to its element names, with the stock drawing kept aside beside it, copied from the user's own
firmware. A plugin that brings the screen's function back calls this from `opl_init` with the screen's file name:
opus.stockhelp's `"help.svg"` (the help screen, which Opus's FN key otherwise never shows). Returns 0, -1 (no stock
drawing kept for that name: the boot log says so) or -2 (another plugin has the screen).

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
int opus_panel_size(int *cols);
```

This OP-1's panel (core 2.20): returns its rows, the long side (376, 400 or 320), and writes its columns into `*cols`
when `cols` is not 0 (188, 200 or 156). 0 on an OP-1 board the core does not know. For pixel work outside a page's
draw call, where `opus_page_pixels` does not answer: opus.splash fits its start picture to the panel this way. Any
thread.

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

## 3D line art: orbit3d

Not a core call but a library plugin, `opus.orbit3d`, in the starter pack: 3D line art for a page in the look of an
OP-1 plate (a camera, spheres that hide the lines behind them, constant line weights, a halo under lines in front),
several inks, filled faces, a 2D overlay and a pixel font, drawn into the page's own pixels for a few hundred cycles
a line. A plugin names it in its manifest (`needs opus.orbit3d 0.2.5`), includes `sdk/orbit3d.h` and calls its `o3_*`
names inside a page's draw call. Every call, its numbers and costs: [orbit3d.md](orbit3d.md); the example:
`examples/opus.globe`.

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
| `opus_alloc_audio` | [Memory](#memory) |
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
| `opus_boot_picture` | [The boot picture](#the-boot-picture) |
| `opus_called` | [Which one is called](#which-one-is-called) |
| `opus_clock` | [The clock](#the-clock) |
| `opus_cpu_charge` | [CPU and the log](#cpu-and-the-log) |
| `opus_drum_key` | [Engines](#drum-engines) |
| `opus_drums` | [Presets](#presets) |
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
| `opus_fast_lease` | [Memory](#memory) |
| `opus_fast_use` | [Memory](#memory) |
| `opus_flash_headroom` | [Files](#files) |
| `opus_fn_held` | [Events](#keys-and-encoders) |
| `opus_folder_adopt` | [Folders on the USB disk](#folders-on-the-usb-disk) |
| `opus_folder_refused` | [Folders on the USB disk](#folders-on-the-usb-disk) |
| `opus_frames_late` | [Events](#frames) |
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
| `opus_kit_delete` | [Presets](#presets-as-files) |
| `opus_kit_file` | [Presets](#presets-as-files) |
| `opus_kit_overwrite` | [Presets](#presets-as-files) |
| `opus_kit_rename` | [Presets](#presets-as-files) |
| `opus_lfo_changed` | [LFO types](#lfo-types) |
| `opus_lfo_move` | [LFO types](#lfo-types) |
| `opus_lfo_page` | [LFO types](#lfo-types) |
| `opus_lfo_register` | [LFO types](#lfo-types) |
| `opus_log` | [CPU and the log](#cpu-and-the-log) |
| `opus_on_block` | [Events](#audio-events) |
| `opus_on_block_end` | [Events](#audio-events) |
| `opus_on_folder` | [Folders on the USB disk](#folders-on-the-usb-disk) |
| `opus_on_frame` | [Events](#frames) |
| `opus_on_frame_begin` | [Events](#frames) |
| `opus_on_frame_under` | [Events](#frames) |
| `opus_key_post` | [Events](#keys-and-encoders) |
| `opus_on_key` | [Events](#keys-and-encoders) |
| `opus_on_key_after` | [Events](#keys-and-encoders) |
| `opus_on_key_filter` | [Events](#keys-and-encoders) |
| `opus_key_timer` | [Events](#keys-and-encoders) |
| `opus_on_key_filter_first` | [Events](#keys-and-encoders) |
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
| `opus_page_dim` | [Pages](#pages) |
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
| `opus_page_still` | [Pages](#pages) |
| `opus_page_still_done` | [Pages](#pages) |
| `opus_page_stroke_line` | [Pages](#pages) |
| `opus_page_text` | [Pages](#pages) |
| `opus_page_text_centre` | [Pages](#pages) |
| `opus_page_text_right` | [Pages](#pages) |
| `opus_panel_bar` | [The panel](#the-panel) |
| `opus_panel_fill` | [The panel](#the-panel) |
| `opus_panel_frame` | [The panel](#the-panel) |
| `opus_panel_size` | [Pixels](#pixels) |
| `opus_panel_text` | [The panel](#the-panel) |
| `opus_place_set` | [Switched places](#switched-places) |
| `opus_preset_add` | [Presets](#preset-sources) |
| `opus_preset_delete` | [Presets](#presets-as-files) |
| `opus_preset_file` | [Presets](#presets-as-files) |
| `opus_preset_overwrite` | [Presets](#presets-as-files) |
| `opus_preset_rename` | [Presets](#presets-as-files) |
| `opus_preset_source` | [Presets](#preset-sources) |
| `opus_presets` | [Presets](#presets) |
| `opus_presets_relist` | [Presets](#preset-sources) |
| `opus_presets_reload` | [Presets](#presets-as-files) |
| `opus_queue_hold` | [Events](#the-main-pass-the-clip-queue-and-tape-writes) |
| `opus_quick_page` | [Quick pages](#quick-pages) |
| `opus_quick_show` | [Quick pages](#quick-pages) |
| `opus_rename` | [Files](#files) |
| `opus_replace` | [Art](#art) |
| `opus_stats` | [The core's figures](#the-cores-figures) |
| `opus_stock_screen` | [Art](#art) |
| `opus_sample` | [Samples](#samples) |
| `opus_sample_base` | [Samples](#samples) |
| `opus_sample_sound` | [Samples](#samples) |
| `opus_seq_mode` | [Sequencers](#sequencers) |
| `opus_seq_register` | [Sequencers](#sequencers) |
| `opus_set_setting` | [Settings](#settings) |
| `opus_setting` | [Settings](#settings) |
| `opus_sincos` | [Art](#art) |
| `opus_skip_dry_input` | [Events](#audio-events) |
| `opus_sound_slot` | [Managers](#managers) |
| `opus_tempo` | [Tempo](#tempo) |
| `opus_tempo_phase` | [Tempo](#tempo) |
| `opus_tempo_phase_step` | [Tempo](#tempo) |
| `opus_types` | [Managers](#managers) |

Structs and types: `struct opl_self` ([The plugin's entry](#the-plugins-entry)); `struct opus_preset_source`
([Presets](#preset-sources)); `struct opus_fast_lease` ([Memory](#memory)); `struct opus_folder`
([Folders](#folders-on-the-usb-disk)); `struct opus_need`, `struct opus_fx`, `struct opus_fx_view`,
`struct opus_effect` ([Effects](#effects)); `struct opus_hosted_effect`
([Another firmware's effects](#another-firmwares-effects)); `opus_handle`, `struct opus_type_info`,
`struct opus_fx_shown`, `struct opus_fx_record` ([Managers](#managers)); `struct opus_engine`
([Engines](#engines)); `struct opus_lfo`, `struct opus_lfo_type` ([LFO types](#lfo-types)); `struct opus_tempo`
([Tempo](#tempo)); `struct opus_clock` ([The clock](#the-clock)). The figures record's structs, `struct
opus_stats_head` and the rest, are not in `opus_plugin.h` ([The core's figures](#the-cores-figures)).
