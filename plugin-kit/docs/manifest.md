# The plugin manifest (`manifest.txt`)

Every plugin folder holds a `manifest.txt` beside its sources: a few lines of text that say what the plugin is, what
it needs and what it takes. The build (`plugin_tool.py build`) checks it, adds the lines it measures, and puts the
whole manifest into the `.opmod` file. At every start the core reads each plugin's manifest before any of its code
runs: it refuses a plugin whose manifest is wrong, whose controls clash with a plugin already loaded, or that needs a
newer core, and the mods list says why.

This page lists every line a manifest may hold. The C side is in [api.md](api.md).

## A first example

The kit's `examples/opus.zoom`:

```
# opus.zoom: on the tape screen, shift + white zooms the tape view (clockwise in).
opmod     1
id        opus.zoom
name      Zoom
author    opus
version   1.0.0
core      2.0
adds      the tape view's zoom on shift + white
uses      white_turn shift tape "zoom the tape view"
```

## The format

- Plain text, one field a line: a **key** (lower-case letters `a-z` only), at least one space, then the **value**.
  Use spaces, not a tab, after the key. More spaces are fine (the examples line the values up); spaces at the line's
  end are dropped.
- A `#` at the start of a line, or after a space or a tab, starts a comment to the end of the line. A `#` inside a
  word is not a comment, so a choice like `c#` works.
- Blank lines are ignored.
- Lines up to 159 characters: the core cuts a longer line there, so the build refuses one whose words (not its
  comment) reach past it.
- The file is read as UTF-8, but write plain ASCII: the OP-1's font has no other characters.
- A key may be given once, except the repeatable keys (marked below), which may be given as often as their limits
  allow. A single key given twice is refused by the build.
- Leave a line out rather than writing `none`. Only `uses none` is accepted (and ignored): the build refuses
  `needs none`, `theme none` and `setting none`.

## All keys

| Key | Required | Repeatable | Who writes it | What it is |
|---|---|---|---|---|
| `opmod` | yes | | you | the manifest format: `1` |
| `id` | yes | | you | the plugin's permanent id |
| `name` | yes | | you | its name in the mods list |
| `author` | yes | | you | who made it |
| `version` | yes | | you | its version |
| `core` | yes | | you | the lowest core it needs |
| `needs` | | yes (4) | you | a library it needs |
| `exports` | | | you | makes it a library: the names it exports |
| `uses` | | yes (12) | you | a control it takes |
| `setting` | | yes (12) | you | a setting the user changes |
| `theme` | | yes (4) | you | a colour theme it brings |
| `place` | | yes | you | a hook into the firmware's code |
| `cpu` | | | you write only `cpu user`; else the build | its audio CPU figure |
| `adds` | | yes | you | what it adds, for people |
| `checked` | | yes | you | where and when it was seen working |
| `breaks`, `removes`, `replace`, `pack` | | yes | you | accepted, no effect (below) |
| `deep`, `ram`, `imports` | | | the build, never you | measured lines (below) |

## The required keys

### `opmod`

```
opmod     1
```

The manifest format. Always `1`; the build refuses anything else.

### `id`

```
id        opus.bitcrush
```

The plugin's permanent id, `author.plugin`: two or more parts of `a-z`, `0-9` and `_`, joined by dots, up to 47
characters. Ids starting with `opus.` are Opus's own plugins; use your own name or handle as the first part.

Never change a released plugin's id. The id names the plugin's folder on the OP-1 (`/yaffs2/mod/<id>`, where its
settings live), it is stamped into sounds saved with the plugin's effects and engines, and an engine's mark (which its
sounds carry) is made from the id and the engine's name: a new id is a new plugin, and the old one's sounds would no
longer find it. Two files with the same id: the one with the newer `version` loads, the other is refused ("an older
copy").

### `name`

```
name      Bitcrush
```

The plugin's name as people read it. The mods list shows it in lower case; the core keeps up to 31 characters.

### `author`

```
author    opus
```

Who made it: a name or handle, up to 31 characters. Shown in the plugin's details (the orange push on its row).

### `version`

```
version   1.0.0
```

The plugin's version: numbers like `1.0.0` or `1.0` (`MAJOR.MINOR` or `MAJOR.MINOR.PATCH`), up to 15 characters.
Raise it for every changed file you give out: the core keeps the newer of two files with one id, and a plugin that
`needs` a library names the library's lowest version.

### `core`

```
core      2.4
```

The lowest core version whose services the plugin uses: `MAJOR.MINOR` (a third number is accepted and ignored). The
core loads the plugin when the major number is its own (2) and the minor number is not above its own; otherwise the
mods list says "needs core 2.6". Set it to the highest version among the services you use:

| Core | Adds |
|---|---|
| 2.0 | the API's first services (most of it) |
| 2.1 | switched places (`place ... switched`, `opus_place_set`) |
| 2.2 | managers (`opus_fx_manager` and the rest of api.md's Managers section) |
| 2.3 | `opus_engine_knobs` |
| 2.4 | `opus_clock` |
| 2.5 | `opus_engine_show` |
| 2.6 | another firmware's engines and effects (api.md, Core versions) |
| 2.7 | `opus_on_settings_rows` |

The build does not check this line against the functions the plugin calls: a plugin that calls a newer function than
its `core` line says is still refused on an older core, but with "needs NAME" instead.

## Libraries

### `needs` (repeatable)

```
needs     author.quantize 1.1
```

A library the plugin calls into: its id and its lowest version (`MAJOR.MINOR` or `MAJOR.MINOR.PATCH`). The core loads
libraries before the plugins that need them and refuses a plugin whose library is missing, refused, skipped or too
old ("needs author.quantize 1.1"), or a loop of plugins that need each other ("needs a plugin that needs it"). At
most 4 `needs` lines (the core reads 4; the build refuses more).

### `exports`

```
exports   quantize_note quantize_chord
```

Makes the plugin a library: the names (functions or data defined in its sources, not `static`) that other plugins may
call. They declare them from the library's own header and name the library in a `needs` line. One line, the names
parted by spaces; 128 exported names in all, across every loaded library. Needs code (a code-free plugin cannot
export).

## Controls

### `uses` (repeatable, up to 12)

```
uses      INPUT [shift | noshift] [fn | nofn] [with KEY]
          [synth | drum | tape | mixer] [t1 | t2 | t3 | t4] [modal] "what it does"
```

(One line in the manifest; shown on two here.)

A control the plugin takes. Every key or encoder a plugin acts on is declared, so two plugins never take the same
one: a plugin whose control meets one a loaded plugin already takes is refused before any of its code runs, and the
mods list names the other plugin ("shift white turn is taken by zoom"). The mods list shows each plugin's controls
(the green push on its row). The declaration does not route anything: the plugin's key and knob handlers see every
event and must check the same conditions themselves.

`INPUT`, the first word, is one of:

| Input | What |
|---|---|
| `tempo`, `synth`, `drum`, `tape`, `mixer` | the mode keys and the tempo key |
| `t1`, `t2`, `t3`, `t4` | the keys T1-T4 |
| `lift`, `drop`, `split` | the tape keys |
| `s1` ... `s8` | the eight sound keys |
| `seq`, `rec`, `play`, `stop`, `left`, `right`, `mic`, `com` | the other keys |
| `blue_push`, `green_push`, `white_push`, `orange_push` | an encoder's push |
| `blue_turn`, `green_turn`, `white_turn`, `orange_turn` | an encoder's turn |
| `keyboard` | the keyboard's note keys |

`fn` and `help` are not inputs: FN (the help key) is the core's, used as a modifier. Shift is not an input either.

The words after the input, in any order, narrow the control:

- `shift`: only with shift held; `noshift`: only without. Neither: either way.
- `fn`: only with FN held; `nofn`: only without. Neither: either way.
- `with KEY`: only while KEY is held (tape undo takes `left` while the tape key is held). KEY is another input that is
  not a turn, not `keyboard`, and not the input itself.
- `synth`, `drum`, `tape` or `mixer`: only on that mode's screens.
- `t1`, `t2`, `t3` or `t4` after the input: only on that page of the mode (the first word `t1` is the T1 key; a later
  `t1` is the page).
- `modal`: only while the plugin's own page is open (a quick page, its engine's page, its panel). A modal control
  never clashes.

`"what it does"`: in double quotes at the end of the line, 1-27 characters, shown on the controls page. Write it in
lower case.

Two controls clash when neither is `modal`, they have the same input, and for each of shift, FN, the held key, the
screen and the page, either one of them leaves it open or both say the same. So `white_turn shift tape` and
`white_turn noshift tape` do not clash; `white_turn shift tape` and `white_turn shift` do. Plugins load in file-name
order; the later one is refused.

`uses none` is accepted and ignored.

Examples from Opus's plugins:

```
uses      white_turn shift tape "zoom the tape view"
uses      left noshift tape with tape "undo"
uses      blue_push synth t3 "on T3: fx 1 or fx 2"
uses      blue_turn fn synth modal "T2 peak, T4 wave"
uses      orange_turn shift synth t1 "nothing under shift"
```

## Settings

### `setting` (repeatable, up to 12)

```
setting   NAME LOW HIGH DEFAULT "label"
setting   NAME CHOICE,CHOICE,... DEFAULT "label"
```

A setting the user changes on the plugin's settings page (the blue push on its row in the mods list; the green
encoder changes the selected one). The core draws the page from these lines without running the plugin's code, keeps
the values in the plugin's folder (by name, apart from the plugin's file, kept when the file goes) and reads them
before `opl_init`. The plugin reads a value with `opus_setting`, hears the user's changes with `opus_on_setting` and
changes one itself with `opus_set_setting` (api.md, Settings).

- `NAME`: 1-15 characters of `a-z`, `0-9` and `_`; the name the plugin asks for.
- A number setting: `LOW HIGH DEFAULT`, whole numbers from -32768 to 32767 with `LOW <= DEFAULT <= HIGH`.
- A choice: the choices parted by commas, no spaces, up to 95 characters in all; each choice 1 or more of `a-z`, `0-9`,
  `_`, `#`, `/`, `+` and `-` (so `c#` and `6/9` work); `DEFAULT` must be one of them. The plugin gets the index of the
  chosen one, 0 for the first.
- `"label"`: in double quotes at the end of the line, up to 23 characters, shown on the settings page (empty: the name
  shows). Write it in lower case.

A new version of a plugin keeps the user's values that still fit: a name it no longer offers, or a value outside its
new range, is dropped and the default used.

```
setting   depth 1 16 8 "depth"
setting   curve linear,smooth,ease_out,ease_in linear "bend curve"
setting   root c,c#,d,d#,e,f,f#,g,g#,a,a#,b c "root"
setting   drive off,on off "always driving"
```

## Themes

### `theme` (repeatable, up to 4)

```
theme     NAME RRGGBB RRGGBB RRGGBB RRGGBB
```

A colour theme. `NAME`: 1-31 characters, spaces allowed (everything before the four colours). Then the encoders'
four colours, in their order blue, green, white, orange, each six hex digits without a `#` (a `#` after a space would
start a comment). While the plugin is loaded, its themes are listed in the menu's colours row after the built-in
themes; a name a built-in theme or an earlier plugin already has is skipped. A theme needs no code: a plugin folder
with only a manifest is enough (see [Code-free plugins](#code-free-plugins)).

```
theme     halloween 9b30ff 7fff00 e3dac9 ff7518
```

## Places

### `place` (repeatable)

```
place     KIND ADDRESS SYMBOL expect HEX [as NAME] [switched]
```

A hook into the OP-1 firmware's own code: deep work, tied to firmware #246. At load the core checks that the firmware
holds exactly the expected bytes at the address and that no one else (the core or a plugin loaded before) holds them;
then, after `opl_init` returned 0, it writes the hook with interrupts off and flushes the CPU's caches over it. A
plugin never writes code bytes itself. Use places only when no `opus_*` service does the job, and only with addresses
and bytes you can verify.

- `KIND`: what is written:

| Kind | Expected bytes | What the core writes |
|---|---|---|
| `call` | 4 | the CALL instruction at ADDRESS now calls SYMBOL |
| `jump` | 4, 6 or 8 | a function's entry: its first 4 bytes jump to SYMBOL (the rest: for the way back) |
| `ldimm` | 8 | a pair of instructions loading one register's low and high halves now loads SYMBOL's address |
| `word` | 4 | the 32-bit data word at ADDRESS becomes SYMBOL's address |
| `patch` | 2, 4, 6 or 8 | as many bytes as expected, copied from SYMBOL's data in the plugin |

- `ADDRESS`: the firmware address in hex (`0x01A50970`), even.
- `SYMBOL`: a function or data in the plugin's sources. For a `patch`, nothing in the plugin refers to the data, so
  keep the compiler from dropping it: not `static`, or `__attribute__((used))`.
- `expect HEX`: the stock bytes at the address, as hex digits without spaces (`01e382ab`), as many as the kind takes.
- `as NAME`: the place's name in the mods list's messages; without it, `KIND_ADDRESS` (`call_01a50970`).
- `switched`: the place is checked and held at load but left stock; the plugin turns it on and off with
  `opus_place_set(self, NAME, on)`. Only a `call`, a `word`, or a `patch` of 2 or 4 bytes can be switched (one
  instruction or word, so each write is safe for every thread); its name is up to 23 characters. 8 switched places in
  all, across every plugin. Needs core 2.1.

**A jump place's way back.** A plugin that replaces a function's entry and still wants the stock function declares and
calls `orig_<address>` (8 lower-case hex digits: `int orig_019aae44(const void *name);`), and the build writes it: the
bytes the place expects, then a jump on to the rest of the function. The build refuses it unless those bytes are whole
instructions that can run elsewhere (nothing relative to the program counter, no other change of flow, nothing naming
register P0); then write the way back by hand. An `orig_` name with no jump place at its address is refused.

The mods list's refusals: "place NAME is taken" (the bytes differ, or someone holds them), "place NAME out of reach"
(a call or jump cannot reach the plugin's code), "place NAME is no address load" (an `ldimm` that is not such a pair),
"not loaded: too many switched places". Places need code: a code-free plugin cannot have them.

```
place     call 0x01A50970 bend_down_entry expect 01e382ab as down_1
place     patch 0x01A57314 tu_nop expect 209b as loop_move_right switched
```

## CPU

### `cpu user`

```
cpu       user
```

The only `cpu` line you write. It is for a manager: a plugin that runs effects the user chose (api.md, Managers). Its
audio work is measured and shown but never switched off by the CPU budget: the user chose the effects and hears an
overload. Every other `cpu` line is the build's.

The build writes a plugin's measured figure from `cpu.txt` in the plugin folder: a line `cycles N`, the plugin's worst
audio block in CPU cycles (lines starting with `#` are comments). Without `cpu.txt` the build writes
`cpu unmeasured`, and the core holds the plugin to a tenth of the audio block (58,052 cycles). A `cpu.txt` with no
`cycles N` line is refused. How the CPU budget works: api.md, The CPU budget.

## Notes for people

### `adds` (repeatable)

```
adds      effect bitcrush
```

What the plugin adds, in a few words, for people reading the manifest (`effect bitcrush`, `engine outatime`,
`theme halloween`, `tape undo and redo`). Any text; neither the build nor the core acts on it.

### `checked` (repeatable)

```
checked   emulator|device YYYY-MM-DD [what was run]
```

Where and when you saw the plugin work: `emulator` or `device`, a real date, then (optionally) what was run, up to 60
characters. The build's `show` lists the latest line of each kind, or that none is recorded; the core passes over
them. Write a `device` line only after the plugin was tried on an OP-1.

```
checked   device 2026-10-16 chords through it on the synth, drum and master
```

### `breaks`, `removes`, `replace`, `pack` (repeatable)

The build accepts these lines with any text, and the current core and build do nothing with them. Leave them out.

## Lines the build writes

The build adds these after a comment line of its own; they are in the `.opmod`, never in your `manifest.txt`. Writing
one yourself is refused ("deep is written by the build"), except `cpu user`.

| Line | Meaning |
|---|---|
| `deep NAME NAME ...` or `deep none` | the firmware functions (`fw_*`) the plugin imports |
| `ram image N zero N` | the bytes of its code and data, and of its zero-filled data |
| `imports N` | how many names it imports (the names themselves are in the file) |
| `cpu N` | its CPU figure: the cycles measured in `cpu.txt` |
| `cpu unmeasured` | no `cpu.txt`: the core holds it to a tenth of the audio block |
| `cpu user` | your own line, kept: a manager |
| `cpu 0` | a code-free plugin |

A plugin's code and data are at most 1 MB, and its zero-filled data at most 1 MB.

## Other keys

The build accepts any other lower-case key once, and the core ignores it. Do not invent keys: a later core may give a
key a meaning.

## Code-free plugins

A plugin folder with only a `manifest.txt` (no sources) builds a code-free `.opmod`: no code is loaded and no
`opl_init` runs. What it brings is its manifest: its `theme` lines, and `setting` lines. `place` and `exports` lines
need code and are refused. The kit's `examples/opus.halloween` is one.

## At load: what the mods list says

The core reads each `.opmod` in the disk's `mods` folder in file-name order (a plugin that needs a library loads
after it) and checks everything before any code runs. A refused plugin shows the reason on its row:

| Message | Meaning |
|---|---|
| `its manifest` | no `id`, `version` or `core` line |
| `its id`, `its version`, `its core line` | that line is malformed |
| `a needs line` | a `needs` line without an id and a version |
| `a uses line`, `a uses line's input` | a `uses` line is malformed, or its input unknown |
| `a uses line's word`, `a uses line's with` | a word after the input, or the `with` key, is unknown |
| `too many uses lines` | more than 12 |
| `fn is the core's: a uses line's modifier` | `fn` or `help` used as the input |
| `a setting line`, `a setting line's numbers` | a `setting` line is malformed, or its numbers out of order |
| `a setting line's default` | a choice's default is not one of its choices |
| `too many setting lines` | more than 12 |
| `a theme line`, `too many theme lines` | a `theme` line is malformed, or there are more than 4 |
| `damaged: ...` | the file is damaged (its checksum, an offset, a size) |
| `needs core 2.6` | the plugin's `core` line is newer than the core |
| `an older copy` | another file with the same id has a newer version |
| `needs author.lib 1.1` | a library it needs is missing, refused, skipped or older |
| `needs a plugin that needs it` | plugins needing each other in a loop |
| `fn shift white turn is taken by NAME` | a control clashes with a loaded plugin's |
| `needs NAME` | it calls a function the core does not have |
| `not loaded: no room`, `not loaded: too many plugins` | no memory left, or 30 plugins already loaded |
| `place NAME is taken` (and the other place messages) | see [Places](#places) |
| `failed to start (-N)` | its `opl_init` returned -N |
| `skipped: crashed` | it crashed the OP-1 last time; skipped until its file changes (developer mode loads it) |
| `switched off: N of M cycles` | its audio went over its CPU figure this start (not a refusal: it is loaded) |

The build checks the same manifest rules first, so most of these show up on the computer, as `REFUSED:` and the
reason.

## The kit's examples

### `examples/opus.halloween`: a theme, no code

```
# opus.halloween: the Halloween colour theme, a code-free plugin.
opmod     1
id        opus.halloween
name      Halloween
author    opus
version   1.0.0
core      2.0
adds      theme halloween
theme     halloween 9b30ff 7fff00 e3dac9 ff7518
```

### `examples/opus.zoom`: a tool on an encoder

```
# opus.zoom: on the tape screen, shift + white zooms the tape view (clockwise in).
opmod     1
id        opus.zoom
name      Zoom
author    opus
version   1.0.0
core      2.0
adds      the tape view's zoom on shift + white
uses      white_turn shift tape "zoom the tape view"
```

### `examples/opus.bitcrush`: an effect with its own page

```
# opus.bitcrush: an effect for the synth, the drum and the master: blue rate, green bits, white tone, orange mix.
opmod     1
id        opus.bitcrush
name      Bitcrush
author    opus
version   2.0.0
core      2.0
adds      effect bitcrush
```

It declares no `uses` line: the encoders turn its parameters on its effect page, as they do a stock effect's. Its
`cpu.txt` holds its measured figure.

### `examples/opus.outatime`: a synth engine with a designed page

```
# opus.outatime: a synth engine; its T1 page is SVG art with live instruments drawn over it.
opmod     1
id        opus.outatime
name      Outatime
author    opus
version   1.0.0
core      2.4
adds      engine outatime
uses      blue_turn shift synth t1 "spread: the arms detune"
uses      green_turn shift synth t1 "tone"
uses      white_turn shift synth t1 "flux: the ensemble"
uses      orange_turn shift synth t1 "nothing under shift"
setting   drive off,on off "always driving"
```

`core 2.4` because it calls `opus_clock`. Its plain turns on T1 stay the OS's (they turn knobs 0-3); its engine's
`turn` handler takes the shifted ones and sets its other knobs with `opus_engine_knob_set`, so it declares them, and
no other plugin takes shift + an encoder on the synth's T1.

### Lines from Opus's other plugins

```
cpu       user                                              # a manager: Fx overdrive
uses      green_turn t3 modal "fx 2, in the effect list"
uses      left noshift tape with tape "undo"                # tape undo
setting   looplock off,on off "loop lock"
place     call 0x01A54B6C clear_entry expect 02e3182a as tape_clear
place     patch 0x01A57314 tu_nop expect 209b as loop_move_right switched
setting   up 1 12 2 "bend up"                               # bend
place     call 0x01A50970 bend_down_entry expect 01e382ab as down_1
```
