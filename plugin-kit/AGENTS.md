# Instructions for AI agents: writing Opus 2 plugins

You are helping someone write a plugin for **Opus 2**, a firmware mod for the original Teenage Engineering OP-1
(firmware #246, an Analog Devices BF524 Blackfin processor, 64 MB of RAM, a 320 x 160 screen). These instructions
are for any coding agent (Claude Code, Codex, Cursor, Copilot, Gemini and others). Read them before you write code,
and keep to them: a plugin runs inside the OP-1's own firmware, next to the user's music and their only copy of
their tape, so a careless plugin costs them more than a crash.

## The person you work with

Often a musician who owns an OP-1, not always a programmer. They are your hands and ears on the device: you cannot
run the OP-1, they can. So:

- Explain in plain words, with the OP-1's own names: the four encoders **blue, green, white, orange** (left to
  right), the keys **T1-T4**, **shift**, **synth / drum / tape / mixer**, **COM**; the help key is Opus's **FN** key.
- Give device steps they can follow one by one, and say what they should see or hear at each.
- Never say a plugin works until they have tried it on their OP-1 and told you so. A clean build only means it
  compiles.
- Quality over quantity: one plugin that is right is worth more than three quick ones. If a request would make the
  OP-1 worse (too heavy for the CPU, taking controls other plugins need), say so and offer a better shape.

## The kit

- `plugin_tool.py new AUTHOR.NAME` makes a working plugin folder in `plugins/`; `build DIR` writes
  `build/NAME.opmod`; `show FILE.opmod` reads it back as the core will; `svg FILE.svg` checks page art against what
  the OP-1's own SVG parser draws (run it on every SVG a plugin ships). A failure prints `REFUSED:` and why.
- `docs/getting-started.md` (the compiler, the device steps), `docs/rules.md` (the rules below in full),
  `docs/api.md` (every `opus_*` call), `docs/manifest.md` (every manifest line), `docs/sharing.md` (the checks).
- `sdk/opus_plugin.h` (the core's calls) and `sdk/op1fw.h` (the firmware's own functions, `fw_*`).
- `examples/`: four real plugins from Opus 2's starter pack: `opus.halloween` (a theme, no code), `opus.zoom` (a
  tape tool), `opus.bitcrush` (an effect with its own page), `opus.outatime` (a synth engine whose page is an SVG
  design with live instruments drawn over it: the way to build a designed screen). Start from the closest one or from
  `new`.

## How to work

1. **Pin the plugin down first.** What kind is it (an effect, a synth engine, a page, a tool on a key or encoder, a
   theme)? Which core services does it need? Find each call in `docs/api.md` before you plan around it. If what they
   want needs something the API does not offer, say so plainly; do not invent calls, and do not reach into the
   firmware's memory at addresses you cannot verify.
2. **Start from `new` or an example**, keep its structure, and change one thing at a time.
3. **Build after every change** (`python plugin_tool.py build plugins/ID`). Warnings are errors; fix the cause,
   never silence it.
4. **Hand it to the person with exact steps:** developer mode on (Opus's menu: synth + drum + tape + mixer, T1),
   disk mode (COM, then T1), copy the `.opmod` into the disk's mods folder, eject, restart, the mods list (the menu,
   T2): its row says "loaded" or why not. Then what to press and what to listen for.
5. **Ask what they saw and heard**, and fix from that. If the OP-1 crashed: the next start leaves the plugin out
   ("skipped: crashed") unless developer mode is on; holding Stop while switching on starts with no plugins.
6. **Its CPU figure** (any plugin with audio code): have them play the heaviest case (all voices, chords, its
   heaviest settings) with developer mode on, then read "audio: worst N" in its details (orange push on its row in
   the mods list), and write `cycles N` into the plugin folder's `cpu.txt` (see `examples/opus.bitcrush/cpu.txt`).
   The build writes it into the manifest; without it the core holds the plugin to a tenth of the audio block.
7. **Record what was tested:** a `checked device DATE what was played` line in `manifest.txt`, only after they
   confirmed it on their OP-1.

## The rules (in full: docs/rules.md)

- **The audio block.** The OS computes sound in blocks of 580,525 CPU cycles, shared by the OS, every plugin and the
  core. Code that runs in the audio block (an effect's or engine's render, the audio events) must never wait, never
  allocate, never touch files, never draw, and must finish fast. Everything heavy is prepared in `opl_init` or on the
  main thread.
- **No floating point in audio code.** The BF524 has no floating-point unit; `float` works but is emulated in software
  and slow. Use fixed-point integers (the examples show how).
- **No C library.** The code is freestanding: no `malloc`, no `printf`, no `string.h`. Memory comes from
  `opus_alloc` (ask in `opl_init`, never in the audio block; it is never freed), text formatting from `fw_snprintf`
  (`op1fw.h`). If the compiler calls
  `memcpy` or `memset` (a struct copy, an array cleared at once), the plugin defines its own.
- **Only declared names.** Call only what `sdk/opus_plugin.h` and `sdk/op1fw.h` declare. The core resolves each
  `opus_*` name when it loads the plugin and refuses a plugin that calls one it lacks ("needs NAME").
- **Controls are declared.** Every key or encoder a plugin takes is a `uses` line in its manifest; the core refuses a
  plugin whose controls clash with another's. Do not take a stock control's job away silently.
- **The screen.** A page is 320 x 160, but the OP-1's window shows it only to about y 147: keep content within y 2 to
  146. Text in the OP-1's own font is written **lowercase** (it draws a-z as capitals; capital letters A-Z draw icons).
  Draw with the core's drawing calls (`docs/api.md`): they work on all three OP-1 screen types.
- **Files.** Write only in the plugin's own folder, on the main or key thread (never in the display thread or the
  audio block), small and seldom,
  and keep within the flash's room (`opus_flash_headroom`): the user's tape and sounds come first. A change in several
  steps goes through the core's journals (`docs/api.md`), so a crash or the power switch cannot leave it half done.
- **Ids and names.** A plugin's id is `author.name` (lowercase, digits, `_`, dots); `opus.` is Opus's own. Engine and
  effect names show in the OS's lists: keep them short and lowercase.
- **Deep plugins** (`fw_*` calls, `place` lines that patch the firmware's code, raw addresses like zoom's) tie a
  plugin to firmware #246's insides. Use them only when no `opus_*` service does the job, only with addresses and
  bytes taken from the docs or the examples, and say so to the person.
- **te-boot is never touched.** Nothing a plugin does may write outside what the OS itself writes; the update and
  recovery mode must stay as Teenage Engineering made it.

## Before you call it done

Go through `docs/sharing.md` with the person: it builds without warnings; `show` lists what you expect (its imports,
its settings, its controls); it loaded on their OP-1 and did what it should; its CPU figure is measured; it was tried
with the other plugins they use; its manifest's `version` went up for every changed file; its README says what it
does and which controls it takes.
