/* Opus 2's plugin API: the core's names a plugin calls (opus_*). The
 * firmware's functions by name are in op1fw.h (the deep names, written by `op1mod plugin`); a library's
 * exports in the library's own header. Every name is an import: the core gives its address when it loads
 * the plugin, and refuses a plugin naming one it does not have ("needs opus_on_post_fx").
 * Licence: GPL-3.0, with two additional terms (section 7 c and e; the kit's README, Licence): a modified version has
 * to be marked as different from the original, and it can't use the original plugin's name. */
#ifndef OPUS_PLUGIN_H
#define OPUS_PLUGIN_H

#define OPUS_PLUGIN_API 1                  /* this header's version: the manifest's `core` line says the core's */

/* What the core tells a plugin about itself. A newer core may make it longer: read `size` first. */
struct opl_self {
    unsigned size;                         /* sizeof (struct opl_self) as the core knows it */
    const char *id;                        /* the manifest's id: "opus.bitcrush" */
    const char *folder;                    /* the plugin's own folder: /yaffs2/mod/<id> (made when asked) */
    int slot;                              /* its place in the core's list (its CPU charge, the mods list) */
};

/* The plugin's entry, in OP_Main after the OS opened its databases and before it loads any sound:
 * register handlers, then return 0; a negative number refuses ("failed to start (-n)"). */
int opl_init(struct opl_self *self);

/* Events (opus.h's, the same threads and limits): each adds a handler after the core's own and those of
 * the plugins loaded before. key and knob return 1 to take the event (no one later, not the OS). The audio
 * events (pre_fx, post_fx, post_master, block_end) run inside the audio block (IVG11): nothing that waits,
 * allocates or takes long; each is timed and charged to the plugin. block runs in the sequencer thread, once
 * per audio block before the MIDI drain, outside the interrupt. */
void opus_on_key(struct opl_self *self, int (*fn)(int key, int pressed));
/* A key event of the plugin's own (core 2.12): the OS and every key handler (the plugin's own too) take it as a real
 * one, after the events already queued (the dev port's injection). The OS's key codes below 0x40. 1 queued, 0
 * not (no such key, the queue full). Any thread. */
int opus_key_post(int key, int pressed);
/* A key filter (core 2.13): every key event first, as it came, before the core, Opus's menu, every key handler and the
 * OS; 1 drops it (it never happened: no one sees it). For a plugin that cleans the keys themselves (the trigger guard);
 * a filter that holds an event back and sends it later with opus_key_post sees it come back here. KeyHandler. */
void opus_on_key_filter(struct opl_self *self, int (*fn)(int key, int pressed));
/* A key filter that comes before every other plugin's (core 2.25): for a plugin that cleans the keys for everyone (the
 * trigger guard), so every other filter and handler sees the cleaned keys, each event once, the cleaner's late ones
 * included. Several such run in their plugins' load order, all before the plain filters. KeyHandler. */
void opus_on_key_filter_first(struct opl_self *self, int (*fn)(int key, int pressed));
/* A timer in the KeyHandler thread (core 2.14): fn once, ms or a little more from now (the next audio block's check,
 * then the key queue), where key handlers run, so it may do what they do (fw_ui_dispatch, opus_key_post). One a
 * plugin: a call replaces the one waiting; ms 0 cancels. For a gesture that waits (a key held a moment). */
void opus_key_timer(struct opl_self *self, unsigned ms, void (*fn)(void));
/* FN (the OP-1's help key, the core's since 2026-10-17): 1 while held. A key handler also sees it as key 0; the OS
 * never does. A control with FN held is declared `uses INPUT fn ...` (nofn: without it). */
int opus_fn_held(void);
void opus_on_knob(struct opl_self *self, int (*fn)(int index, short *delta));
/* A frame handler runs on each frame the core draws: while the screen stays the same, every 4th (still screens, since
 * 2026-11-04: a frame not drawn calls no handler, its buffer already the panel's picture); a key, a turn, MIDI coming in
 * or a frame that differs brings every frame back. So a handler's calls are no clock: time comes from the uptime or
 * the cycles. The display thread. */
void opus_on_frame(struct opl_self *self, void (*fn)(unsigned short *buf));
/* Under the OS's screen (2026-10-23: Opus's drawing on the same layer as the stock effects): on the frame the OS
 * just cleared, before it draws its screen and its popups over it, with the
 * opus_page_* names; what the OS draws there covers it. For what goes beside or over an OS screen yet under its
 * popups (the effect list's second column, an effect page's dots). The display thread. */
void opus_on_frame_under(struct opl_self *self, void (*fn)(unsigned short *buf));
void opus_on_main_pass(struct opl_self *self, void (*fn)(void));
/* After the OS's own handling (2026-10-21): every key and turn the key and knob handlers saw, taken or not (a
 * taken one the OS never saw), with the delta the OS had (the KeyHandler thread); the clip queue's run once per
 * OP_Main pass, ran nonzero when a queued command ran (a lift, a drop, a move). For a plugin that compares what
 * the OS changed, as tape undo does. */
void opus_on_key_after(struct opl_self *self, void (*fn)(int key, int pressed));
void opus_on_knob_after(struct opl_self *self, void (*fn)(int index, short delta));
void opus_on_queue_after(struct opl_self *self, void (*fn)(int ran));
/* OP_Main, before each of its writes of tape audio (the core holds the three yaffs_write sites, and writes
 * from a copy): fd, the buffer, its bytes; the tape still holds what the write will overwrite. */
void opus_on_tape_write(struct opl_self *self, void (*fn)(int fd, const void *buf, unsigned n));
/* From a main pass handler: this pass's clip queue run waits for the next pass (queue_after hears ran 0), as
 * tape undo's does while the key thread is between its two looks at the tape. */
void opus_queue_hold(void);
/* The OS's own dry input skipped this block (2026-10-21, opus.amp): an engine that plays the input itself
 * calls it from its block handler, inside the audio interrupt only (a block a thread renders has the last
 * interrupt's input), so the input is heard once. */
void opus_skip_dry_input(void);
/* A quick page (2026-10-21, Opus 1's quick menus over the synth's T1): the plugin opens a page of its own in place of
 * the screen (from its key handler: page >= 0 opens or switches, -1 closes) and draws it with the opus_page_* names
 * in its draw handler (the display thread); while it shows, the four turns go to its knob handler (encoder 1-4,
 * steps in detents; the OS sees none), the notes, shift, left and right, the transport, FN and the pushes pass on
 * (a push is the plugin's own to switch or close with), any other key closes it and goes on. The menu comes first. */
void opus_on_quick(struct opl_self *self, void (*draw)(int page), int (*knob)(int page, int encoder, int steps));
void opus_quick_show(struct opl_self *self, int page);
int opus_quick_page(struct opl_self *self);              /* the page showing, -1 when none of this plugin's */
/* Files (fs.c, 2026-10-21; Opus 1's): a real rename of a file or folder (one new header page, the data stays;
 * a power cut leaves exactly one name), within /yaffs2/user/audio/ and /yaffs2/mod/ only, plain names; any thread
 * that may take the file system's lock. And whether the OS's background file job is idle (its queue empty, none
 * running: a preset loading or a slot row being written is not). */
#define OPUS_RENAME_OK          0
#define OPUS_RENAME_BAD        (-1)        /* a path outside those folders, or not a plain name */
#define OPUS_RENAME_NOT_FOUND  (-2)        /* no such file, or no such target folder */
#define OPUS_RENAME_EXISTS     (-3)        /* the target folder has that name already */
#define OPUS_RENAME_REFUSED    (-4)        /* a special folder, a link, a read-only disk, a folder into itself */
#define OPUS_RENAME_FAILED     (-5)        /* the header write failed: nothing changed */
int opus_rename(const char *old_path, const char *new_path);
/* Journals (journal.c, 2026-10-23: the core's recovery service): a change in several steps handed to the core, which
 * writes it down, carries the steps out one at a time, strikes each off once done and removes it after the last; what
 * is left of it the core finishes at the next start, before the plugins load and before the OS reads a preset, whether
 * this plugin loads or not, so a power cut never leaves the change half done. The steps, a line each, the word and its
 * fields parted by a tab: `rename A B` (A to B when A is there and B is not; copied then removed when the rename is
 * refused), `copy A B` (B written whole from A when A is there), `unlink A` (when there), `sql S` (op1.db; no '%'),
 * then `end`; paths in /yaffs2/user/audio/ and /yaffs2/mod/ only. On the USB disk (2026-10-31, core 2.16), in /synth/
 * and /drum/ only: `frename A B` (A to B when A is there and B is not: FAT renames over nothing, so `funlink B` first)
 * and `funlink A` (when there; its read-only mark cleared first); they wait while the disk is not the OP-1's (at the
 * start before the OS mounts it, in disk mode), the rest of the journal with them. opus_journal_run: the steps done
 * (all of them: the change made), short of them when one failed (that one and the rest stay for the next start: the plugin may undo what
 * was done and call opus_journal_done instead), -1 when not taken (a step the core does not take, too long, the write
 * failed: nothing done). opus_journal only writes one down (what undoes the plugin's own work in progress, done with
 * opus_journal_done); a later one of the plugin's replaces it. OP_Main's thread (the file system). */
#define OPUS_JOURNAL_MAX 2048
int opus_journal_run(struct opl_self *self, const char *steps);
int opus_journal(struct opl_self *self, const char *steps);
void opus_journal_done(struct opl_self *self);
int opus_bgfile_idle(void);
/* The synth's sound presets as the browser lists them (presets.c: the core's own array, 0x38 bytes an
 * entry), for a plugin that renames or overwrites a preset. */
extern unsigned char opus_presets[];
extern unsigned char opus_drums[];         /* the drum kits', the same (core 2.19: moved from stock's 0x01DE20F8) */
/* Events the menu and the save have (opus.h's): the start of each frame (the display thread, before the OS draws),
 * and OP_Main's hold-to-save pass: 1 takes the pass (the OS's does not run), 0 leaves it the OS's. */
void opus_on_frame_begin(struct opl_self *self, void (*fn)(void));
void opus_on_save_pass(struct opl_self *self, int (*fn)(void));
/* How many of the last 8 frames came late (core 2.29): two display periods (70 ms) or more after the one before, as
 * the OS's frames come when a frame's drawing takes longer than one period (the audio comes first: under a heavy sound
 * the drawing gets what it leaves, and the rate halves at once). An animated page may draw lighter while they do, and
 * back once they come on time again. 0-8. */
int opus_frames_late(void);
/* The flash's bytes a plugin may still write (room.c: the free space less what the tape, the sounds and a
 * margin will need); a plugin keeps its files inside it, as tape undo's scratch does (2026-10-21). */
int opus_flash_headroom(void);
/* The theme's accent colours (blue, green, white, orange: 0xRRGGBB with OPUS_ACCENT_SET, or 0 for stock). */
#define OPUS_ACCENT_SET 0x01000000u
unsigned opus_accent(int i);
void opus_on_block(struct opl_self *self, void (*fn)(void));
void opus_on_pre_fx(struct opl_self *self, void (*fn)(int instrument, short *buf));
void opus_on_post_fx(struct opl_self *self, void (*fn)(int instrument, short *buf));
void opus_on_post_master(struct opl_self *self, void (*fn)(short *l, short *r));
void opus_on_block_end(struct opl_self *self, void (*fn)(void));

/* Memory from the core's pool: 32-byte aligned, zeroed, the plugin's for this start (never freed); 0 when
 * the pool has no room left (the plugin goes on without it, or refuses). Core 2.8: from the audio pool while it has
 * room beyond what it keeps for effects (below), else the main pool. */
void *opus_alloc(struct opl_self *self, unsigned bytes);
/* Which of your registered structs the core is calling now (core 2.9): from inside one of its functions, the
 * opus_engine, opus_effect, opus_hosted_effect or opus_sequencer you registered it with (the audio interrupt's calls
 * and each thread's apart); 0 outside them. For a plugin that registers several with one set of functions (the bridge,
 * for other firmwares' files: 2026-10-29), as a VST host hands each call its instance. */
const void *opus_called(void);
/* A synth slot's sample (core 2.10, the sampler service): its sound, 16-bit mono at 44.1 kHz, and *frames its length
 * (264,600: 6 s). Every slot has one whatever its engine; an engine with OPUS_ENGINE_SAMPLE finds its sound's sample
 * there (the slot sounding now: opus_sound_slot(0)). Read only, from any thread: the OS writes it while it loads a
 * sample or records one. 0 for a slot outside 0-7. */
const short *opus_sample(int slot, unsigned *frames);
/* 1 when synth slot 0-7's sound keeps its sample with it (core 2.10): a sampler's, or a sample engine's sound. A
 * plugin that saves a sound itself (an overwrite save) saves the slot's synth_N.raw with it then, as the OS's own
 * save does. */
int opus_sample_sound(int slot);
/* The key synth slot 0-7's sound was sampled at, as the OS's base_freq in milli-Hz (174.614 Hz x 2^(k/12), k the key
 * that ended the recording, 0-23 from the left: 523,251 for the C), and *end the recorded end in frames, as mic or
 * shift + mic and a held note left them for this sound (core 2.10: the sampler service; they travel with the sound
 * and its presets). 0 (and *end 0) when this sound was never sampled. Any thread (the core's copy, as
 * opus_engine_data). */
unsigned opus_sample_base(int slot, unsigned *end);
/* Memory the audio reads every block (a delay line, a voice's tables; core 2.8): as opus_alloc, but it may take the
 * audio pool's last 512 KB too, which plain asks leave to effects' instances (the pool: 1.3 MB the display's DMA does
 * not share; in the main pool such memory costs about twice as much while the display sends one of its two
 * frames). Effects' instances get it without asking. */
void *opus_alloc_audio(struct opl_self *self, unsigned bytes);
/* Fast memory (L1 data: no cache cost): 32-byte aligned, not cleared, never freed; 0 when none is left
 * (about 25 KB for all plugins, first come; keep the SDRAM buffer then). Only from opl_init: an effect's init
 * also runs in the audio interrupt. Worth it for what an audio block touches every time (a short delay line, a
 * block's settings): each 32-byte line of SDRAM costs about 107 cycles a block on the device. */
void *opus_fast(struct opl_self *self, unsigned bytes);
/* Fast memory on lease (core 2.21, 2026-11-01): the fast memory goes to what plays.
 * A part takes a lease from opl_init with its own copy in SDRAM (slow: bytes, 4-byte aligned, always valid), and each
 * audio block it works asks where its bytes are now (opus_fast_use: the fast copy or its own). The core gives a lease
 * in use fast memory when there is room, or takes it from a lease of a plugin of lower priority (the manifest's
 * `priority high|normal|low`), and takes it back from a lease unused for about two seconds; moves happen only at an
 * audio block's end. kind: what a move keeps. Never keep the pointer past the block. */
#define OPUS_FAST_SCRATCH 0                /* nothing kept: the part fills its bytes every block */
#define OPUS_FAST_CONST 1                  /* set once in its own copy, never written after: copied in */
#define OPUS_FAST_STATE 2                  /* kept: copied in, and back out when it goes */
struct opus_fast_lease;
struct opus_fast_lease *opus_fast_lease(struct opl_self *self, unsigned bytes, int kind, void *slow);   /* 0 none */
void *opus_fast_use(struct opus_fast_lease *l);   /* the audio thread, each block it works: where its bytes are */

/* Audio work the OS runs for a plugin, outside the core's events (an effect in the OS's effect slot): its
 * cycles, charged to the plugin for the mods list (IVG11 or any thread). */
void opus_cpu_charge(struct opl_self *self, unsigned cycles);

/* The core's figures (core 2.28): the CPU, memory, threads and drawing of the whole OP-1 and of every plugin, as one
 * record (opus_stats.h has its layout) copied into buf. The core makes a fresh record about four times a second while
 * it is asked for (any thread; asking costs a copy), none otherwise: the first call after a quiet while finds the last
 * one made, or none. -> its bytes; 0 none made yet (ask again in a moment); -(its bytes) when max is too small. */
int opus_stats(void *buf, unsigned max);

/* A line in the boot log (the dev port's log). */
void opus_log(struct opl_self *self, const char *text);

/* The settings the manifest offers (`setting depth 1 16 8 "depth"`, `setting curve linear,exp,log linear "curve"`):
 * the user changes them on the mods list's settings page (the blue push); the core keeps them in the plugin's
 * folder, apart from its file. A setting's value now (a choice: its index), from opl_init on; the handler is told
 * each change (the KeyHandler thread). */
int opus_setting(struct opl_self *self, const char *name);
void opus_on_setting(struct opl_self *self, void (*fn)(const char *name, int value));
/* The plugin's own change of a setting (a control of its own sets it), from any thread: kept inside its range, shown
 * on the settings page, written to its folder once the changes have been quiet 2 s; its handler is not told. 1, or
 * 0 for a name the manifest does not offer. */
int opus_set_setting(struct opl_self *self, const char *name, int value);

/* A switched place (core 2.1; the manifest's `place ... as NAME switched`, a call, a word or a patch of 2 or 4
 * bytes): checked and held at load like any place but left stock; on installs it, off writes the expected bytes
 * back, at once, from any thread (so a setting can switch it live, tape undo's loop jump). 1, or 0 for a name it
 * does not hold or bytes no longer stock. */
int opus_place_set(struct opl_self *self, const char *name, int on);

/* A folder on the USB disk (the core's folder service, folders.c): /mods/NAME on the disk shows the files of
 * /yaffs2/mod/NAME here. At disk mode's start the core copies them out; at its end a file the computer left there
 * (new or changed, with the ending, at most max bytes) is copied whole to `incoming` and handed to take, which keeps
 * it in the folder in its own form (writing /yaffs2/mod/NAME/file) or refuses it, 1 when the folder changed; a file
 * deleted on the computer is deleted here; then changed runs. Both in the KeyHandler thread. The name: 1-8 lower
 * case letters, digits, '_' or '-' (the disk's 8.3 names), not one another part has; eight folders in all (the core
 * has two: room for six plugins', 2026-10-26). The struct stays the core's to read (static). 1, or 0 refused (the boot log says
 * why). From opl_init. */
#ifndef OPUS_FOLDER_DEFINED                /* the core's opus.h has the same */
#define OPUS_FOLDER_DEFINED
struct opus_folder {
    const char *name;                      /* "dx": /yaffs2/mod/dx, /mods/dx on the disk */
    const char *ext;                       /* the files' ending, ".syx" (case aside); 0 any */
    unsigned max;                          /* the largest file taken from the disk, in bytes */
    int (*take)(const char *file, const char *incoming);
    void (*changed)(void);
};
#endif
int opus_on_folder(struct opl_self *self, const struct opus_folder *f);
/* Inside take, before refusing (core 2.20): why, shown as a notice with the file's name ("splash/me.png: not a png");
 * short (the line holds about 48 characters with the name), a string that stays (a literal). */
void opus_folder_refused(const char *why);
/* (core 2.32) The folder adopts files of an ending the computer leaves in /mods itself: at disk mode's end the core moves
 * them into this folder on the disk, and take gets them as any of its files, with a notice saying so ("gin.op1p: moved
 * to mods/bridge") and the load report's line. Another firmware's plugin files for a bridge, banks for a synth. The
 * ending: a dot and 1-6 letters or digits (".syx"), not .opmod, not one another loaded plugin's folder adopts; eight
 * in all. It counts while the plugin is loaded; with the plugin missing or off, such a file is refused as before.
 * Your own registered folder only (opus_on_folder first). 1, or 0 refused (the boot log says why). From opl_init. */
int opus_folder_adopt(struct opl_self *self, const struct opus_folder *f, const char *ending);

/* (core 2.33) The boot picture: shown while the OP-1 starts, in place of TE's logo, from the next start on and while
 * this plugin is installed (the start comes before any plugin runs). picture: a file in your own folder in the core's
 * form ("OPSP" as a word, then width | height << 16, then the rows of RGB565 pixels, at most the panel's rows wide and
 * 168/188 of its columns high: opus_panel_size); name: a text file whose first line is drawn below it, or 0. picture
 * 0: none (when the picture shown is yours). The core writes its record only when it changes. From opl_init, or a
 * folder's take or changed. 1, or 0 (a path of 96 characters or more). */
int opus_boot_picture(struct opl_self *self, const char *picture, const char *name);
/* This OP-1's panel (core 2.20; three types: 376 x 188, 320 x 156, 400 x 200): its rows, the frame's x, and its
 * columns into *cols when given; 0 on boards whose boot picture Opus does not draw on. A frame's pixel (x, y) is the
 * halfword at frame + 256 (rows - 1 - x) + y; the OS's 320 x 160 drawing is scaled by rows / 320. */
int opus_panel_size(int *cols);
/* Reading a file yourself (fw_yaffs_read, op1fw.h): read into a buffer aligned to 32 bytes (`static unsigned char
 * buf[N] __attribute__((aligned(32)))`): a read of a whole flash page into one at an odd address stopped the OP-1
 *. */

/* The plugin's panel: the right of the mods list while its row is selected (2026-10-15), the
 * 320 x 160 drawing's x OPUS_PANEL_X0-X1 and y OPUS_PANEL_Y0-Y1 (below it the core says what each push opens). The draw handler
 * runs in the display thread when the page is drawn (on a change, and twice a second); it draws with the
 * opus_panel_* names below, which keep to the panel. The knob handler gets the green, white and orange turns
 * (encoder 1, 2, 3; steps, + clockwise) while the row is selected, and returns 1 to have the panel drawn again.
 * Without a draw handler the core shows the plugin's version, memory and audio CPU. Colours: by id (below). */
#define OPUS_PANEL_X0   168
#define OPUS_PANEL_X1   312
#define OPUS_PANEL_Y0   28
#define OPUS_PANEL_Y1   134                  /* below it, on the screen's last line, the core's push legend */

/* Colours by id (2026-10-17: a fixed blue, green, white and orange became arbitrary once the user's colours could
 * change them, so a colour is an id). A
 * plugin names a colour by what it is for; the core draws whatever the user's colours and theme make it. Every
 * `colour` argument of the opus_panel_* names and the effect page takes these. */
#define OPUS_COLOR_E1   (-10)                /* the first encoder's colour now (stock: blue) */
#define OPUS_COLOR_E2   (-11)                /* the second's (stock: green) */
#define OPUS_COLOR_E3   (-12)                /* the third's (stock: lavender, the white encoder's) */
#define OPUS_COLOR_E4   (-13)                /* the fourth's (stock: the orange encoder's red) */
#define OPUS_COLOR_E(i) (-10 - (i))          /* encoder i, 0-3, as above */
#define OPUS_COLOR_TEXT 14                   /* values, names: white */
#define OPUS_COLOR_DIM  12                   /* labels, what is off or unavailable: grey */
#define OPUS_COLOR_ALERT 2                   /* refusals, warnings: red (the palette's, not the fourth encoder's) */
#define OPUS_COLOR_BACK 1                    /* under text, the background: black */
#define OPUS_COLOR_OFF  4                    /* unlit: an off segment, tab or light (the palette's offgrey) */
/* The first names (2026-10-15), kept: the same ids. */
#define OPUS_ACCENT(i)  OPUS_COLOR_E(i)
#define OPUS_WHITE      OPUS_COLOR_TEXT
#define OPUS_GREY       OPUS_COLOR_DIM
#define OPUS_RED        OPUS_COLOR_ALERT
#define OPUS_BLACK      OPUS_COLOR_BACK
void opus_on_panel(struct opl_self *self, void (*draw)(void));
/* The panel's turns: green (encoder 1) only since core 2.23; white is the list's order and orange the plugin's switch
 * (plan point 55). Before it, white and orange came too (2, 3). */
void opus_on_panel_knob(struct opl_self *self, int (*knob)(int encoder, int steps));
/* Text in the OP-1's font: slot 0-7, each with one place and size (its first), the colour and text each call. */
void opus_panel_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
void opus_panel_fill(int x0, int y0, int x1, int y1, int colour);        /* a filled rectangle, inclusive */
void opus_panel_frame(int x0, int y0, int x1, int y1, int colour);       /* its outline, one pixel */
void opus_panel_bar(int x0, int y0, int x1, int y1, unsigned value, unsigned max, int colour);   /* lit from the left */
/* Rows of the plugin's own on its settings page (the mods list's blue push), after its manifest's settings: what it
 * holds now, read-only (core 2.7, 2026-10-27; the op1r bridge: each foreign plugin it found). fn(i, &label, value)
 * points label at row i's name (the plugin's own memory, kept while the page shows), writes its value (up to 39
 * characters and the 0) and returns 1, or returns 0 past the last row; it runs in the KeyHandler thread as the page
 * opens, so keep it short. The page holds 40 rows in all, the manifest's settings first. */
void opus_on_settings_rows(struct opl_self *self, int (*fn)(int i, const char **label, char *value));

/* A module's own page (core 2.31, 2026-11-07): the same in every module type that has one,
 * effects, synth and drum engines, LFO types and sequencers. Each type's struct ends with the same optional members:
 *     const char *svg;
 *     void (*screen_init)(void *scr);
 *     void (*screen_draw)(void *scr, int dt);
 *     int (*page_key)(struct opus_page *pg, int key, int pressed);
 *     int (*page_turn)(struct opus_page *pg, int encoder, int detents);
 * svg: its own OS screen on its page (an effect's, an engine's T1, a drum engine's T1, an LFO type's T4, a sequencer's):
 * the file whose front is the screen's SVG, a full path, checked as art is and loaded once at the start; screen_init(scr)
 * once after that; screen_draw(scr, dt) every frame it shows (the display thread): it draws the document itself
 * (fw_screen_draw) and what moves, and asks opus_page_now what to show. Without one, or with its file refused (the boot
 * log says why), the type's drawn page shows (an effect's view, an engine's or LFO type's page). page_key: a key on any of
 * its pages (FN + an encoder's push passes it by); page_turn: a turn there (0 blue .. 3 orange), in detents (0 when less
 * than one). Both in the KeyHandler thread, both told what the page shows; change pg->par in place and return 1: the core
 * keeps the change and saves it with the sound, in whichever slot it is (an effect's params, an engine's knobs, the drum
 * key's numbers, an LFO type's settings); return 0 to leave the event as it was going (the OS's, or the type's older key
 * and turn). When set, they come instead of the type's older key and turn. Since 2.31 a turn's or a key's page call is
 * the same for every type; the older forms stay for the plugins built before. */
#define OPUS_PAGE_EFFECT 0
#define OPUS_PAGE_ENGINE 1                 /* a synth engine's T1, T2 or T4 */
#define OPUS_PAGE_DRUM  2                  /* a drum engine's T1 */
#define OPUS_PAGE_LFO   3                  /* an LFO type's T4 */
#define OPUS_PAGE_SEQ   4                  /* a sequencer's page (the sequencer key) */
struct opus_page {
    short kind;                            /* OPUS_PAGE_* */
    short t;                               /* the T key whose page it is, 1-4; a sequencer's 0 */
    short where;                           /* 0 synth, 1 drum, 2 master */
    short slot;                            /* the sound slot, 0-7 (the master's: 0) */
    short on;                              /* an effect's or an LFO's on; 1 for the others */
    short n;                               /* values in par: 8, an LFO type's nparams, or 0 (a sequencer: its own data) */
    short key;                             /* a drum engine's current key (0-23: the one T1's encoders turn), else -1 */
    short live[8];                         /* par's first eight as the sound hears them now (an LFO's moves in), else
                                              as stored */
    volatile short *par;                   /* what the page edits: an effect's params, an engine's knobs, the drum key's
                                              numbers, an LFO type's settings; 0 when n is 0 */
    void *inst;                            /* the instance: an effect's struct opus_fx *, an LFO's struct opus_lfo *; 0
                                              for the others, and when nothing runs it now (touch its state only) */
    const void *module;                    /* the struct the plugin registered (its opus_effect, opus_engine,
                                              opus_lfo_type or opus_sequencer): compare with yours */
};
/* What a module's page shows now: 1 and pg filled while a plugin's module's page shows (an effect page of any type, the
 * OS's own included: then module and inst are 0), else 0. Any thread but the audio. It never says whether an effect is a
 * sound's first or a manager's (Fx overdrive's second): a type does not know who runs it. */
int opus_page_now(struct opus_page *pg);

/* Effects:the core's effect service (effects.c). An effect is a type in the OS's own effect list (synth, drum
 * and the master each choose it) with its own page; a sound naming it loads with it while its plugin is there,
 * and a saved one is stamped with the plugin's id. */

/* Its page (Opus 1's look), as the effect's `view` fills it: four values in the encoders' colours, label over
 * value in the corners, a picture in the middle (up to three lines of OPUS_FX_WAVE samples), a caption under
 * it. */
#define OPUS_FX_WAVE    128
struct opus_fx_view {
    const char *label[4], *value[4];       /* blue, green, white, orange */
    const short *wave[3];
    int waves;
    const char *caption;                   /* in blue, or 0 */
};

/* An effect type (version 2, 2026-10-23: the module model; Opus itself
 * manages effects, so they can be assigned to an extra slot). A type says what one instance needs; the core
 * allocates it for every instance any manager makes (the core's own effect slots, a plugin's: Fx overdrive's second
 * slots), so the type keeps nothing of an instance in its own statics and never knows who runs it. Data every
 * instance shares (tables) may stay the plugin's, made once in opl_init. */
struct opus_need {                         /* what one instance needs, for mono (synth, drum) or stereo (master) */
    unsigned state;                        /* bytes in SDRAM, cleared: the instance's variables */
    unsigned bulk;                         /* bytes in SDRAM, cleared: delay lines, a reverb's tank */
    unsigned fast;                         /* bytes wanted in fast memory (L1, opus_fast's), else SDRAM */
    unsigned fast_moves;                   /* core 2.21: 0 its fast part stays where it was made (an older type: it
                                              may keep the pointer); OPUS_FAST_SCRATCH + 1 or OPUS_FAST_STATE + 1: the
                                              type reads fx->fast at every call, and its fast part goes where the
                                              playing instances need it most (scratch: nothing kept; state: kept;
                                              OPUS_FAST_CONST + 1 is taken as OPUS_FAST_STATE + 1) */
};
/* An instance as the type's calls get it. The core's synth and drum slots never run in one block and share
 * bulk and fast memory (a type keeps in state whatever tells it where it is in them: a write position). */
struct opus_fx {
    void *state, *bulk, *fast;             /* its memory (0 where the need was 0) */
    int stereo;                            /* 1 the master's kind, 0 mono */
};

/* An effect type. It starts with its own size: the core refuses one shorter than the struct it knows (-1). */
struct opus_effect {
    unsigned size;                         /* sizeof (struct opus_effect) */
    const char *name;                      /* as sounds and the effect list hold it: lower case, up to 15 */
    short defaults[8];                     /* its params when chosen (its fx_types row) */
    short limits[8][3];                    /* {min, max, step} per param: step 0, no knob or LFO moves it */
    void (*need)(int stereo, struct opus_need *n);                        /* pure and cheap: one instance's needs */
    void (*init)(struct opus_fx *fx);      /* at every factory call: build or reset the instance in its memory */
    void (*mono)(struct opus_fx *fx, short *buf, const short *par);             /* synth, drum: 64 samples (IVG11) */
    void (*stereo)(struct opus_fx *fx, short *l, short *r, const short *par);   /* the master */
    void (*view)(const short *par, struct opus_fx_view *v);              /* its page (the display thread) */
    /* The shift layer (2026-10-17): on the effect's own page with shift held, each encoder's turn (0 blue, 1
     * green, 2 white, 3 orange) comes here first, in detents (4 OS units, as the menu counts them; 0 when the turn
     * was less): the effect sets its params in par and returns 1, or returns 0 to leave that encoder the OS's
     * own shift behaviour (fine steps). Its turn then goes on with no movement: the OS changes nothing but still
     * saves the sound's eight params. KeyHandler thread. 0 for none. */
    int (*shift_knob)(short *par, int encoder, int detents);
    /* From here on (2026-11-07, core 2.31): its own page, as every module type's (opus_page above). On an effect's page
     * pg->par is the page's eight params (the sound's, or a manager's for its extra slot) and pg->inst the instance the
     * page shows. A turn with shift held goes to shift_knob when it takes it; page_turn is not called while the effect is
     * off. Keep whatever a sound should recall in its params, and nothing of an instance in statics: the same type can be
     * a sound's effect and its second at once, each page with its own params and instance. The core takes a struct from
     * version 2 (ending at shift_knob) up. */
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    int (*page_key)(struct opus_page *pg, int key, int pressed);
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);
    /* core 2.33, optional: other names this type answers to, separated by spaces: names it had before, or the form an
     * older Opus wrote it in ("opus1:8": Opus 1's effect number 8). The core keeps no list of them (R61). */
    const char *aliases;
};

/* Registers an effect type (from opl_init) -> its type (8-15), or < 0: a bad struct or no room (-1; 8 types in all),
 * no pool for the core's slots' instances (-2), no row (-3), the service could not start (-4: a plugin loaded before
 * holds one of its places; the boot log names it). The core's slots' instances (synth and drum sharing bulk and fast,
 * the master) are made now, their state from the plugin's pool; their bulk has a home of its own and, while the type
 * is a sound's effect at that slot, is lent memory the display does not slow, given back when another effect takes
 * the slot (by the plugin's priority: high may take what normal and low leave). Each init gets the bulk where it is
 * now: take pointers into it at init, not before. Their process calls are timed and charged to the plugin. (Version 1, opus_effect_add with a state size and init(state, pos), until 2026-10-23.) */
int opus_effect_register(struct opl_self *self, const struct opus_effect *e);

/* Another firmware's effect (2026-10-26, core 2.6: the op1r bridge), carried in the OS's filter type (5) by a mark in
 * params[4], as its own firmware carries it, so any number of them fit beside the 16 types. name: what sounds and the
 * effect list hold ("clouds": such a sound loads as the filter with this mark); mark: 0x5301-0x53FF to claim (op1r's
 * 0x5300 + order) or 0 for the core's own; defaults: its row's params (params[4] the core's). init(pos) at its first
 * block at pos after a change (0 synth, 1 drum, 2 master); render(pos, buf, par): 64 samples in place (IVG11; timed and
 * charged); render_stereo(2, l, r, par) the master's, or 0 (the master then runs it on the mix's middle and adds
 * back what it changed). Its own OS screen (svg, screen_init, screen_draw: as an engine's) in the filter's screen slot
 * while its page shows; key(pos, key, pressed) for the pushes 0x3B-0x3E and turn(pos, par, encoder, detents) on its
 * page (1 takes it; par the page's params, written in place). Not a type managers run (Fx overdrive's are the 16): for
 * a plugin's own effect with its own screen, pushes and turns, a type with svg, page_key and page_turn (core 2.31). */
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
/* -> its mark, or < 0: a bad struct or no room (-1; 32 in all), its name or mark taken (-3), the service could not
 * start (-4). From opl_init. */
int opus_effect_host(struct opl_self *self, const struct opus_hosted_effect *f);


/* A plugin's own sound presets in the synth preset list (core 2.8), past the OS's op1.db rows: the browser and the
 * engine list show them like the OS's. The OS rebuilds the list from op1.db at the start and after every save; each
 * time, the source's list(kind) (kind 0, synth; kind 1 the drum kits, since core 2.19, after the OS's drum rows; OP_Main's
 * thread, FatFs mounted and free at the start's) adds its entries of that kind again with opus_preset_add, which works
 * only inside it and for that kind (a source with none of a kind adds nothing). An entry is light: its name and type ("sampler",
 * "fm", ...) and its folder; tag is the plugin's own number for it. Since core 2.15 every plugin may be a source (up
 * to eight, listed in the order they registered; a chosen entry is filled by the source that added it); a second call
 * replaces the plugin's own struct. opus_preset_source: 0, or -1 a bad struct, -2 eight sources already, -3 a place
 * is taken, -4 no room for the tags. opus_preset_add: the entry's index,
 * or -1 not now or bad, -2 the list is full, -3 an op1.db preset of the user's has that folder and name (the OS's
 * own export of it on the disk). opus_presets_relist: the entries again after the plugin's own list
 * changed (disk mode's end), while no list is open. */
struct opus_preset_source {
    unsigned size;                         /* sizeof (struct opus_preset_source) */
    void (*list)(int kind);
    /* An entry chosen (the browser's load job, its BgFile thread; FatFs free there outside disk mode): its whole patch
     * JSON into json (at most max bytes; -> its length, or <= 0: it cannot be loaded and nothing happens), and for
     * a sampler its sound (the bytes of its SSND) written to raw_path, where the OS's job reads it. */
    int (*fill)(unsigned tag, char *json, int max, const char *raw_path);
};
int opus_preset_source(struct opl_self *self, const struct opus_preset_source *s);
int opus_preset_add(struct opl_self *self, int kind, const char *folder, const char *name, const char *type, unsigned tag);
void opus_presets_relist(struct opl_self *self, int kind);
/* The user's presets as files (core 2.17: the user's synth presets are files on the USB disk, /synth/FOLDER/NAME.aif,
 * moved there from op1.db by the core): opus_preset_file says whether the synth preset array's entry at
 * index is one of them (1) or an op1.db row (0: a factory preset, a row the mover left, another source's entry).
 * opus_preset_overwrite writes synth slot slot's sound (its patch, a sampler's or a sample engine's sound) over the
 * file preset at index, whole and through the core's journal: 1 done, -1 nothing changed. opus_preset_rename writes the
 * file preset at index again as to (the patch's "name" too), removes the old file and, for slot >= 0, renames that
 * synth slot's row, all in one journal: 1 done, -2 a file of that name in the folder, -1 nothing changed; the array's
 * JSON stays the caller's to set. opus_presets_reload reads the presets again as after a save (kind 0 synth, 1 drum),
 * the sources' entries with them (the OS's loader alone drops them). OP_Main's thread. */
int opus_preset_file(int index);
int opus_preset_overwrite(struct opl_self *self, int index, int slot);
int opus_preset_rename(struct opl_self *self, int index, const char *to, int slot);
int opus_preset_delete(struct opl_self *self, int index);   /* core 2.18: the file preset removed (journal), 1 done, -1 not */
/* The drum kits as files (core 2.19: the user's drum kits are files too, /drum/FOLDER/NAME.aif): the
 * same four for the drum kits' array (opus_drums) and the drum slots, a sampled kit's sound from drum_N.raw. */
int opus_kit_file(int index);
int opus_kit_overwrite(struct opl_self *self, int index, int slot);
int opus_kit_rename(struct opl_self *self, int index, const char *to, int slot);
int opus_kit_delete(struct opl_self *self, int index);
void opus_presets_reload(int kind);

/* Managers (core 2.2, 2026-10-25; the module model): any plugin may run
 * instances of any effect type (the OS's eight and every plugin's) wherever it wants, without the type knowing (Fx
 * overdrive: a second effect behind every slot). A type is a number (0-7 the OS's, 8-15 plugins'), by name in sounds.
 *
 * opus_fx_manager, from opl_init: the service's places (the effect pages); 1, or -4 a place another plugin holds.
 * opus_types: every type of a kind, sorted by name as the effect list sorts them, with its defaults: the count.
 * opus_fx_new (OP_Main, the key or the sequencer thread; never the audio): an instance of type for where it runs,
 * mono for the synth or the drum, stereo for the master, made and initialised now (a stock grid's init takes 1.65 M
 * cycles: ask from a block handler), its memory the manager's; 0 and the reason (*why) when it cannot be: no room, a
 * stock type already running beside the OS's slots (one mono and one stereo at most: the OS's effects share their
 * memory), or a master grid, phone or spring held back while the master's first is the same (opus_fx_held: ask again).
 * opus_fx_run, from the manager's audio handler (post_fx: mono, r 0; post_master: stereo): the instance on the same
 * samples, its params kept inside its type's limits; a stale handle, a freed one, or a mono stock instance whose
 * instrument does not play this block: nothing. Its time is the manager's (a manifest's `cpu user`: measured, never
 * switched off: the user chose the effects and hears an overload). */
#define OPUS_KIND_EFFECT 0
#define OPUS_FX_SYNTH   0                  /* where an instance runs */
#define OPUS_FX_DRUM    1
#define OPUS_FX_MASTER  2
#define OPUS_FX_OWNERS  17                 /* an effect slot's owner: synth slots 0-7, drum slots 8-15, the master */
#define OPUS_FX_MASTER_OWNER 16
typedef unsigned opus_handle;              /* 0: none */
struct opus_type_info {
    const char *name;
    short type, defaults[8];
    short limits[8][3];                    /* each param's {min, max, step} (valid once opus_fx_manager ran) */
};
int opus_fx_manager(struct opl_self *self);
int opus_types(int kind, struct opus_type_info *out, int max);
opus_handle opus_fx_new(struct opl_self *mgr, int type, int where, const char **why);
void opus_fx_free(opus_handle h);
void opus_fx_reset(opus_handle h);         /* its state cleared, as made */
void opus_fx_run(opus_handle h, short *l, short *r, const short *par);
int opus_fx_type_of(opus_handle h);        /* -1 a stale handle */
int opus_fx_held(int type, int where);     /* 1: held back now (the master's first is the same grid, phone, spring) */
int opus_fx_owner(void);                   /* the effect page's or list's owner now (FX mode and slot), 0-16 */
int opus_instrument_now(void);             /* the instrument the audio block plays: 0 synth, 1 drum */
int opus_sound_slot(int instrument);       /* its current sound slot, 0-7 */
int opus_fx_list_shown(void);              /* the effect list shows (the browser's screen with the effect types) */
int opus_fx_page_shown(void);              /* an effect page shows (any type's) */
/* An effect page in place of the OS's effect (§6): the core asks the manager's shown(owner, v) whenever it needs to
 * (any thread but the audio; cheap); 1 with v filled: the page shows v's type, its screen (the OS's own for a stock
 * type) reading the manager's params and on, every turn changing them as the OS's own (a plugin type's shift layer
 * too), t3 switching on alone, and after each change changed(owner) (the KeyHandler thread): the manager keeps it.
 * h: the instance running it, for its meters (0 none). 0: the OS's own effect shows. Up to 4 managers; 1, 0 no room. */
struct opus_fx_shown {
    short type;
    volatile short *par;                   /* the manager's eight */
    volatile short *on;                    /* the manager's: 1 on */
    opus_handle h;
};
int opus_fx_pages(struct opl_self *mgr, int (*shown)(int owner, struct opus_fx_shown *v), void (*changed)(int owner));
/* A manager's records in sounds (§7): per sound slot, an instance's type by name, its on and eight params, kept in the
 * sound's "opus" member under the manager's keys PREFIX_type, PREFIX_active, PREFIX_params (Fx overdrive's prefix is
 * fx2, Opus 1's own), so they go with the sound through the OS's save, Opus's overwrite and the disk. fn is told each
 * slot's record as its sound loads (owner 0-15; type "" none, a param not kept -32768), and once the master's that
 * Opus 1 kept in the core's settings, when its prefix is fx2 (owner 16; the master's is the manager's to keep, in its
 * folder). A type this start does not know comes by its name: keep it, play nothing, write it back as it was.
 * opus_fx_record_put (the KeyHandler thread, at a change): the slot's record into its sound, and the slot's row written
 * as the OS writes it after a knob turn. From opl_init; one prefix a manager; 1, 0 a bad or taken prefix. */
struct opus_fx_record {
    char type[16];                         /* by name, "" none */
    short on;                              /* 1 on */
    short par[8];
};
int opus_fx_records(struct opl_self *self, const char *prefix, void (*fn)(int owner, const struct opus_fx_record *r));
int opus_fx_record_put(struct opl_self *self, int owner, const struct opus_fx_record *r);
/* A stock effect the OS has but hides (2026-10-21: "filter", type 5): listed in the effect list with these
 * params while the plugin is loaded, gone when it goes (a sound using it plays either way). 1, or 0 for a name the
 * OS does not hide. From opl_init. */
int opus_effect_show(struct opl_self *self, const char *name, const short defaults[8]);
/* A stock synth engine the OS has but hides (2026-10-26: "iter"; Opus 1 listed it): its row in the engine list,
 * a new sound's knobs these (knob 4 below 0x100: no engine's mark), while the plugin is loaded; a sound of it plays as
 * the OS's either way. 1, or 0 (a name the OS does not hide, a mark in knob 4, a place taken). From opl_init (core
 * 2.5). */
int opus_engine_show(struct opl_self *self, const char *name, const short defaults[8]);
/* The params of instrument 0 (synth) or 1 (drum) as its effect last heard them (the LFO's copy), and whether
 * they are recent (8 blocks): for a page. */
int opus_effect_live(int instrument, short par[8]);

/* Engines: the core's engine service (engines.c; Opus 1's engine core, made general). An engine is a row in the
 * OS's engine list (shift + T1) with its own T1 page (and T2, if it wants one); its sounds are the OS's "iter" with
 * the engine's mark in knob 4, so they save, load and copy as any sound. A sound whose engine is not here does not
 * load (the current sound plays on, a notice names the plugin it needs). The OS plays six voices; around each
 * voice's render it does the envelope (T2), the velocity and the mix, so an engine renders one note's raw sound. */
struct opus_engine {
    unsigned size;                         /* sizeof (struct opus_engine) */
    const char *name;                      /* the list's label and a new sound's name: lower case, up to 15 */
    short knobs[8];                        /* a new sound's knobs, 0-32767 (knob 4 is the core's: the mark) */
    unsigned voice;                        /* bytes of state per voice (six of them, in fast memory while it lasts) */
    /* The OS built the voice again (an instrument or slot change, every sound change; the audio interrupt or a
     * thread): make it silent, ready for a note. The state's bytes are as the engine left them, or anything at
     * the first build. index: the voice, 0-5. */
    void (*init)(void *voice, int index);
    /* A note starts, or the voice is let go (KeyHandler; a retrigger: the audio interrupt). knobs: the sound's eight. */
    void (*note_on)(void *voice, const short *knobs);
    void (*release)(void *voice, const short *knobs);
    /* A busy voice's block (the audio interrupt): 128 samples at 88.2 kHz into out (written, not added). par: the
     * sound's knobs as this voice hears them, the LFOs in (an LFO at 100 sweeps a knob end to end); pitch: a phase
     * step a sample (f = step x 88200 / 2^32), glide, bend, octave and tune in it. Timed, charged to the plugin;
     * switched off by its budget, the voices are silent. */
    void (*render)(void *voice, short *out, const short *par, const unsigned *pitch);
    /* The voices' sum each block before the synth's effect, while the current sound is this engine's (64 samples,
     * 44.1 kHz, in place), or 0. Timed and charged. */
    void (*block)(short *buf);
    /* Its T1 page, and its T2 page or 0 for the OS's envelope (the display thread, every frame while it shows,
     * under the OS's overlays): drawn with the opus_page_* names below, on a cleared frame. */
    void (*page)(void);
    void (*page_t2)(void);
    /* A turn on its page (0 blue, 1 green, 2 white, 3 orange; in detents, 0 when less than one; page: 0 T1, 1 T2,
     * 3 T4), or a key (the OS's key code, 1 pressed): 1 takes it, the OS never sees it (opus_engine_knob_set and
     * opus_engine_data_set keep a change); 0 leaves it the OS's (on T1 the encoders turn knobs 0-3). KeyHandler. 0
     * for none. */
    int (*turn)(int encoder, int detents, int page);
    int (*key)(int key, int pressed, int page);
    /* From here on (2026-10-17: an older plugin's shorter struct has none of them). */
    unsigned flags;                        /* OPUS_ENGINE_* */
    void (*page_t4)(void);                 /* its T4 page, or 0 for the OS's LFO pages */
    /* From here on (2026-10-18). With OPUS_ENGINE_OWN_ENVELOPE: a let-go voice's sound has ended (the audio
     * interrupt, once a block from its release until it says so, before the render): nonzero frees the voice (no
     * more renders until a note). Timed and charged. 0 for none. */
    int (*finished)(void *voice);
    /* From here on (2026-10-26, core 2.6: another firmware's engines, the op1r bridge). The "type" that firmware's
     * sounds of this engine carry (op1r's "fmbell"), or 0: such a sound loads as this engine (iter, its mark)
     * instead of being refused. And the mark to claim, or 0 for the core's own: the mark sounds made elsewhere carry
     * in knob 4, so they find this engine, in 0x5301-0x53FF (op1r's and kennel's 0x5300 + order) or, from core 2.33,
     * 0x4F01-0x4FFF (an older Opus's: Opus 1's dimension 0x4F01, amp 0x4F02). The core keeps no list of them. */
    const char *type;
    int mark;
    /* Its own OS screen on T1 (2026-10-26, core 2.6), or 0: svg the file whose front is the screen's SVG (a full path:
     * "/yaffs2/plugins/fmbell.op1p"; checked as art is, loaded once at the start), screen_init(scr) once after that
     * (find its nodes), screen_draw(scr, dt) every frame it shows (the display thread): it draws the document itself
     * (the OS's 0x01A3B4C8(scr, dt)) and what moves. scr is an OS screen object. Instead of page. */
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    /* From here on (2026-11-01, core 2.21). Its shift layer: the knob each encoder (blue, green, white, orange) turns
     * with shift held, 5-7 (knob 4 is the core's mark), or -1 for none (a choice kept in its data, say), so an LFO
     * type can move it (OPUS_LFO_ENGINE_SHIFT). An older plugin's: none. */
    signed char shift_knobs[4];
    /* From here on (2026-11-03, core 2.24: the drum, OPUS_ENGINE_DRUM). A new kit's numbers, its 24 keys' eight each
     * (0: knobs on every key). A note with its key (on the drum 0-23, the key's numbers as knobs) and its velocity
     * (1-127; the OP-1's keys give 127), called instead of note_on when set. The key's choke group (1-15, or 0 for
     * none): a note in a group stops the busy voices of the other keys in it through choke (cut now: a few
     * milliseconds' fade), as an open hi-hat by a closed one. 0 for any of them: none. */
    const short (*keys)[8];
    void (*note_key)(void *voice, const short *knobs, int key, int velocity);
    int (*group)(const short *knobs, int key);
    void (*choke)(void *voice);
    /* From here on (2026-11-07, core 2.31): its pages' keys and turns, as every module type's (opus_page above): pg->t
     * says T1, T2 or T4; pg->par the sound's knobs (knob 4 is the core's: a change to it is not kept), or on the drum the
     * current key's eight numbers. svg, screen_init and screen_draw above are its T1 screen, on the drum's T1 too. */
    int (*page_key)(struct opus_page *pg, int key, int pressed);
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);
};
/* The engine shapes the volume itself, its release and the velocity included (its T2 page). With finished, the OS's
 * envelope does not run for its voices (no cost; the gain stays at a held key's) and a let-go voice is freed when
 * finished says so. Without it (an older plugin), the OS's envelope runs held neutral (fastest attack, full sustain,
 * the slowest release, at every load and choice) and keeps a let-go voice busy, rendering, until that release ends.
 * The OS's play mode stays (shift + T2). The hold is the engine's sounds' only: the sound's own envelope and LFO are
 * kept in its member and come back when the slot changes to an engine that does not hold them. */
#define OPUS_ENGINE_OWN_ENVELOPE 1
/* The engine's sounds have the OS's LFO held off (its active flag cleared at every load and choice): the engine has
 * its own (its T4 page; its key handler takes T4's on/off key). */
#define OPUS_ENGINE_OWN_LFO 2
/* The engine's voices are iter's own objects (2026-10-26, core 2.6): 0x24 bytes at 0xFFB00000 + 0x24 v, the
 * first word the OS's (the vtable pointer: never write it), the other 32 bytes the engine's. Its init, note_on,
 * release, render and finished get that object (voice 0: the core keeps no state for it), render the knobs as the
 * OS gives them (no LFO reach). For engines written for iter's layout: op1r's and kennel's find their voice and its
 * velocity from the object's address. */
#define OPUS_ENGINE_OS_VOICES 4
/* The engine plays the slot's sample (core 2.10, the sampler service; 2026-10-29): the core makes the OS load the slot's
 * sample for its sounds as it does for the sampler's (the start, the browser, the engine's choice), save it with them
 * (the OS's save and the overwrite save), and sample into it (mic or shift + mic, a held note: the sound stays the
 * engine's). Its voices read it with opus_sample; the key it was sampled at with opus_sample_base. */
#define OPUS_ENGINE_SAMPLE 8
/* A drum engine (core 2.24, 2026-11-03; SIM): a row of the drum's engine list (shift + T1 in drum mode), never the
 * synth's. Its kits are the OS's dbox kits (type "dbox": 24 keys of eight numbers, the member naming the engine), so
 * the OS saves, loads, copies and turns them as dbox's: on T1 the four encoders turn the current key's numbers 0-3 and
 * with shift 4-7 (number 0 -24320..24832, the others 0..32767), and the drum's LFO moves them. Twelve voices (state
 * for twelve); note_key gets the key; render's knobs are the playing key's eight as the LFO left them, its pitch the
 * voice's ratio x 65536 (65536 as played; the drum octave and a sequencer's transposition in it, 2^(semitones / 12):
 * the endless sequencer transposes a drum pattern by the held key) since core 2.26, 0 (no pointer) before.
 * finished is required: on the drum it is asked every block from the note on (a hit ends on its own), and a voice it
 * frees renders no more until a note. Its page is the drum's T1 (page; T2 and T4 stay the OS's). */
#define OPUS_ENGINE_DRUM 16

/* Adds an engine (from opl_init) -> its mark (1-32767, the same on every OP-1: from the plugin's id and the
 * engine's name, or the one it claims), or < 0: a bad struct (-1; a claimed mark outside 0x5301-0x53FF; since core
 * 2.9 no fixed count of engines), no memory for its voices (-2), its mark or type taken by an
 * engine loaded before (-3), the service could not start (-4: a place is a plugin's; the boot log). */
int opus_engine_add(struct opl_self *self, const struct opus_engine *e);
/* The current sound's knobs as its newest note's voice last heard them (the LFOs in) and 1 while a voice plays,
 * else the stored knobs and 0: for a page. */
int opus_engine_live(short par[8]);
/* The drum's current key (0-23: the last note key played, the one T1's encoders turn) and its eight numbers as the
 * current kit stores them (core 2.24): for a drum engine's page. Any thread. */
int opus_drum_key(short par[8]);
/* The current sound's knobs for a page, and 1 when it is an engine's: since core 2.8 as opus_engine_live (the LFO in
 * while a note sounds), so every value a page shows follows the modulation as the stock screens' do; core 2.3-2.7 gave
 * the stored knobs. Any thread. */
int opus_engine_knobs(short par[8]);
/* 1 when synth slot s's (0-7) sound is an engine's with its own T2 page (the amp's tuner): a turn never reaches its
 * envelope there (opus.dice leaves it alone). */
int opus_engine_own_t2(int s);
/* Sets knob k (0-3, 5-7) of the current sound, its engine's, and keeps it (the sound's row written): 1, or 0 when
 * the current sound is no engine's or k is 4. KeyHandler (a turn or key handler). */
int opus_engine_knob_set(int k, int value);
/* The current sound's own data (up to OPUS_ENGINE_DATA bytes: an edited voice, an envelope), kept inside the sound,
 * so it travels with it: the OS's save makes a user preset holding it, a load brings it back. The read: a copy of
 * the current slot's, as its last load or set left it (any thread), its length (0: none); data_set: KeyHandler (a
 * turn or key handler), 0 bytes removes it; 1, or 0 when the current sound is no engine's. The sound's name (as the
 * OS keeps it: the engine's name when chosen, a user preset's after the OS's save or a load), any thread. */
#define OPUS_ENGINE_DATA 256
/* Core 2.21: the engine keeps no pointer to a voice's state past a call (it uses the one each call gives), so the
 * voices' states may move between fast memory and SDRAM between blocks (fast memory on lease, by priority). */
#define OPUS_ENGINE_STATE_MOVES 512
int opus_engine_data(void *buf, int max);
int opus_engine_data_set(const void *buf, int n);
int opus_engine_sound_name(char *buf, int max);

/* A plugin's own page (an engine's T1, T2 and T4): the whole 320 x 160 drawing, cut to it (the OP-1's window shows y
 * 2-146). Only inside the page's draw call; colours by id. Text slots 0-31, each with one place and size (its first). */
void opus_page_text(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
void opus_page_text_right(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);
void opus_page_text_centre(struct opl_self *self, int slot, int x, int y, int size, int colour, const char *text);   /* core 2.21: centred on x */
void opus_page_fill(int x0, int y0, int x1, int y1, int colour);         /* a filled rectangle, inclusive */
void opus_page_frame(int x0, int y0, int x1, int y1, int colour);        /* its outline, one pixel */
void opus_page_line(int x0, int y0, int x1, int y1, int colour);         /* a unit wide, smoothed (2026-10-23) */
/* A line in units x 256 (finer than a pixel), smoothed by the core as the OS's renderer covers a pixel (round ends; a
 * line that goes on from the last one's end in its colour joins it round): for lines drawn every frame. Its width x 256
 * is in the panel's pixels, as a path's stroke is (the OS's renderer scales places with the panel, not widths).
 * Colours by id or OPUS_RGB. */
void opus_page_stroke_line(int x0, int y0, int x1, int y1, int width, int colour);
void opus_page_dot(int x, int y, int r, int filled, int colour);         /* a circle of radius r around (x, y), smoothed */
void opus_page_bar(int x0, int y0, int x1, int y1, unsigned value, unsigned max, int colour);   /* lit from the left */
/* Everything drawn so far at half brightness, as the OP-1's own help darkens the page before drawing over it (core
 * 2.28): for an overlay over a page that goes on underneath. What is drawn after it stays bright. Its cost is about
 * the OS's own dimming's (some hundreds of thousands of cycles a frame): only while the overlay shows. */
void opus_page_dim(void);
/* SVG art behind a page (page 0 T1, 1 T2, 3 T4): the file (a full path) is checked for what would stop the OP-1 in the
 * OS's parser (tools/op1mod/svgcheck.py says the same on the PC), loaded the first time the page shows, drawn once
 * and kept; the page's draw call then draws over it. Again after the file changed (its size); path 0: none any more.
 * 1, or 0 for a bad page or no room. Any time from opl_init on (a folder's changed handler). */
int opus_page_art(struct opl_self *self, int page, const char *path);

/* Drawing as the OS draws (2026-10-23: Opus's drawing looks like the stock rendering, so the two blend): a path of lines and cubic curves, stroked or filled by the OS's own anti-aliased renderer
 * (the one its envelope and LFO screens draw their curves with), which also turns and scales it to the panel
 * (rev 2's 376, rev 1's 400, the FE's 320): smooth edges, widths, caps and joins as stock's. Places in the drawing's
 * units x 256 (x 0-81,920 across, y 0-40,960 down: finer than a pixel), kept to a margin around the drawing.
 * Colours by id, or OPUS_RGB(0xRRGGBB) for one of the plugin's own.
 * opus_page_path starts a path (the last one gone); moves start its subpaths (each stroked as a shape of its own since
 * 2026-11-04: until then a stroke joined a move to the point before it); a line or curve with no move before
 * starts at its own end. A stroke or a fill draws the path and keeps it (both may: the fill first). Up to
 * OPUS_PATH_POINTS points a path (a curve takes three); past that they are dropped. Width x 256 (pixels, below); colours by
 * id; alpha 0 (none) to 255 (opaque), blended over what is there. A stroke's width is the panel's pixels, not units (the
 * OS's renderer scales places, not widths: 1.5 is about 1.5 pixels on every panel). A long stroke is drawn in pieces cut at its points,
 * each small enough for the renderer to draw whole: with round caps a cut shows no seam. Only inside the page's
 * draw call (the display thread). Its cost is the OS's: about 2 M cycles for a 128-point figure 140 x 120 units. */
#define OPUS_PATH_POINTS 1024
#define OPUS_RGB(rgb)   (0x01000000 | ((rgb) & 0xFFFFFF))   /* a colour of the plugin's own, for a stroke or fill */
#define OPUS_CAP_BUTT   0
#define OPUS_CAP_ROUND  1
#define OPUS_CAP_SQUARE 2
#define OPUS_JOIN_MITER 0
#define OPUS_JOIN_ROUND 1
#define OPUS_JOIN_BEVEL 2
void opus_page_path(void);
void opus_page_move(int x, int y);
void opus_page_line_to(int x, int y);
void opus_page_curve_to(int x1, int y1, int x2, int y2, int x, int y);   /* two controls, then the end */
void opus_page_close(void);
void opus_page_stroke(int width, int colour, int alpha, int cap, int join);
void opus_page_fill_path(int colour, int alpha);   /* each subpath closed */
/* An encoder's accent as it shows now (0 blue .. 3 orange): 0xRRGGBB, the user's pick or stock's (a page's own text). */
unsigned opus_accent_now(int i);
/* The OP-1's date and time as the user set it (the system menu's "date and time", shift + help's screen in
 * stock): month 1-12, day 1-31, weekday 0 Sunday. Any thread (core 2.4). */
struct opus_clock {
    short year;
    unsigned char month, day, hour, minute, second, weekday;
};
void opus_clock(struct opus_clock *c);
/* The tempo service (core 2.21, 2026-11-01): the OP-1's tempo and where the music is, for a plugin
 * that follows the beat (an LFO in note lengths, a tempo-synced delay). The sequencers' clock: 96 ticks a beat, paid
 * out once an audio block (64 samples at 44.1 kHz, about 689 a second) whether the tape plays or not; tied to the
 * tape in the tempo screen's tape modes, to a MIDI clock in its MIDI modes. ticks counts from the last reset (the play
 * key, MIDI START: a beat's start), frac its fraction; step the ticks an audio block (16.16: measured when the clock
 * is the tape's or a MIDI clock's). mbpm the set tempo, or under a MIDI clock its tempo measured over the last beats (0
 * until two beats came). Any thread; size set by the caller. */
struct opus_tempo {
    unsigned size;                         /* sizeof (struct opus_tempo) */
    unsigned mbpm;                         /* thousandths of a BPM */
    unsigned ticks;                        /* 96 a beat, since the reset */
    unsigned frac;                         /* 0-65535 of a tick */
    unsigned step;                         /* ticks an audio block, 16.16 */
    unsigned char external;                /* 1: following a MIDI clock */
    unsigned char playing;                 /* the tape: 0 stopped, 1 recording, 2 playing */
};
void opus_tempo(struct opus_tempo *t);
/* A cycle n ticks long (96 a beat, 24 a sixteenth, 384 a bar of four beats; 1-65535) at the music's position: its
 * 32-bit phase (2^32 a cycle, 0 at each reset), and its phase's step an audio block. For an LFO locked to the beat. */
unsigned opus_tempo_phase(unsigned n);
unsigned opus_tempo_phase_step(unsigned n);

/* LFO types (core 2.21, 2026-11-01): a plugin's LFO
 * as a row of the OS's LFO list (shift + T4) and its page on T4. Its settings live in the sound as Opus's record
 * (nparams numbers in the sound's "opus" member); the sound's own LFO fields say a stock MIDI LFO at rest, so a stock
 * OP-1 or another firmware plays the sound with no LFO and never refuses it. While the sound is in its instrument's
 * current slot and its LFO is on (T4), the audio thread calls the type: tick once an audio block (64 samples at
 * 44.1 kHz), apply at each of the instrument's passes (synth: 1 then 0 for each sounding voice, then 2; drum: 3 for
 * each sounding voice, then 4, then 5), where opus_lfo_move turns a target on the pass's copy of the sound (the OS's
 * own turn, inside its ranges; the sound itself is never written), trigger at a note's start (for its voice, then for
 * the instrument). A sound whose type's plugin is not loaded keeps its record and plays with no LFO. */
struct opus_lfo {
    unsigned char instrument;              /* 0 synth, 1 drum */
    unsigned char slot;                    /* 0-7 */
    unsigned char voice;                   /* a voice pass's or trigger's voice (synth 0-5, drum 0-11); 255 the
                                              instrument's own (passes 2, 4, 5) */
    unsigned char pass;                    /* in apply: 0-5 */
    short *params;                         /* the slot's settings (nparams), its sound's record */
    void *state;                           /* the type's memory for the slot (state_size bytes, zero at the slot's
                                              load and at a pick) */
};
struct opus_lfo_type {
    unsigned size;                         /* sizeof (struct opus_lfo_type) */
    const char *name;                      /* the list's row and the record's type: permanent, 1-15 characters */
    unsigned short nparams;                /* 1-64 */
    unsigned short state_size;             /* bytes a slot */
    const short *defaults;                 /* nparams: a pick's settings */
    void (*tick)(struct opus_lfo *l);      /* the audio thread, once a block (before the block's last pass) */
    int (*apply)(struct opus_lfo *l);      /* the audio thread, each pass: 1 when it moved something */
    void (*trigger)(struct opus_lfo *l);   /* the audio thread: a note started */
    /* Its page on the synth's T4 (the core's page service, as an engine's T4 page: opus_page_*; opus_lfo_page() for its
       slot). Since core 2.27 the drum's T4 shows it too (before, the stock MIDI LFO's page). */
    void (*page)(void);                    /* the display thread: the whole drawing */
    int (*turn)(int encoder, int detents); /* KeyHandler: 0 blue .. 3 orange, in detents; after a change,
                                              opus_lfo_changed; 1 taken */
    int (*key)(int key, int pressed);      /* KeyHandler: a key on its page (the encoders' pushes 0x3B-0x3E); 1 taken */
    /* From here on (2026-11-07, core 2.31): its own page, as every module type's (opus_page above): its T4 screen, and
     * keys and turns with pg->par its settings (pg->n of them) and pg->inst the slot's struct opus_lfo; a change taken
     * is kept as opus_lfo_changed keeps it. Not called while the slot's LFO is off (T4's toggle). */
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    int (*page_key)(struct opus_page *pg, int key, int pressed);
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);
};
int opus_lfo_register(struct opl_self *self, const struct opus_lfo_type *t);   /* from opl_init; 1, 0 a bad type or
                                                                                   no room */
/* In apply: a turn of delta on the pass's copy, the OS's own turn kept inside the parameter's range: an engine's knob
 * (OPUS_LFO_ENGINE, param its encoder 0-3: passes 0 synth, 3 drum), its shift layer's (OPUS_LFO_ENGINE_SHIFT, param
 * the encoder: the knob the engine says it turns with shift, opus_engine.shift_knobs; the synth's pass 0), an envelope
 * setting (OPUS_LFO_ENVELOPE, 0-3 attack, decay, sustain, release: passes 1 synth, 4 drum), the effect's parameter
 * (OPUS_LFO_EFFECT, 0-7, its shift layer 4-7 too: passes 2 synth, 5 drum), the sound itself (OPUS_LFO_SOUND, core 2.22,
 * the stock element LFO's last group: 0 the voice's volume, its gain x (1 + delta), and 1 its pitch, x 2^delta (an
 * octave either way at full): the synth's pass 0; 2 ring, the instrument's sound x delta, and 3 duck, the sound less
 * its delta share: passes 2 synth, 5 drum; delta Q15). 1 done, 0 not this pass or no such target. */
#define OPUS_LFO_ENGINE 0
#define OPUS_LFO_ENVELOPE 1
#define OPUS_LFO_EFFECT 2
#define OPUS_LFO_ENGINE_SHIFT 3
#define OPUS_LFO_SOUND 4
int opus_lfo_move(const struct opus_lfo *l, int group, int param, int delta);
struct opus_lfo *opus_lfo_page(void);      /* the LFO page's slot (its settings, its state), 0 none */
void opus_lfo_changed(const struct opus_lfo *l);   /* KeyHandler: its settings into the slot's sound and row */

/* A plugin's own SVG art, drawn as its page needs (2026-10-23: Opus 1's svgpage, so its engine pages port as they were:
 * the amp's knob pointers turned, its label rows lit one at a time). opus_art_open names the file (checked and loaded
 * as opus_page_art's, the first time opus_art_ready asks, inside a page's draw) -> art >= 0, or -1 (no room; any time
 * from opl_init). Then, inside the page's draw: ready (1 loaded, 0 not: refused, the boot log says why), draw (the
 * document over the frame as shown now, by the OS's renderer: about 3 M cycles for a page of 250 shapes, so keep what
 * it drew), show or hide an element by id, turn one (1/4096 of a turn, clockwise on the screen, about a point in units
 * x 256: the drawing's own shape at any angle), the places of a placeholder colour and new colours into them (the next
 * draw shows them). opus_art_colours moves when the user's colours change (a pick, a theme): keep again then. */
int opus_art_open(struct opl_self *self, const char *path);
/* The art carried in the plugin itself (a C string from the .svg, so the plugin stays one .opmod): written to its
 * folder (/yaffs2/mod/<id>/<name>) when missing or different, inside the flash's headroom, then opened as above.
 * From opl_init (the file system's thread); -1 also when the flash has no room or the write failed (the boot log). */
int opus_art_text(struct opl_self *self, const char *name, const char *svg, unsigned len);
/* An OS screen's drawing replaced by the plugin's (core 2.11; content layering):
 * from opl_init (the OS parses its screens after the plugins' inits). os_path: the screen's file,
 * "/yaffs2/content/display/NAME.svg" (only screens' drawings yet); data, len: the plugin's SVG, kept in its folder
 * (written when it changed). The OS's file stays as it was: a start without the plugin draws the stock screen. Refused
 * (the boot log says why) when the OS's parser would stop on it, when it lacks an element the stock drawing has (by
 * id, as the parser reads ids, and of the same kind: the OS finds its elements by id), or when another plugin replaced
 * that screen first. 0, -1 refused, -2 taken. */
int opus_replace(struct opl_self *self, const char *os_path, const char *data, unsigned len);
/* A screen Opus 2 ships reduced, drawn as stock again (core 2.14): the install has such a screen's drawing reduced to
 * its names (the OS builds every screen at the start; a screen Opus never shows costs no memory then) and keeps the
 * stock drawing aside, copied from the OP-1's own firmware. For a plugin that brings that screen's function back:
 * opus.stockhelp, "help.svg". From opl_init. 0, -1 none kept (the boot log), -2 another plugin has the screen. */
int opus_stock_screen(struct opl_self *self, const char *name);
int opus_art_ready(int art);
void opus_art_draw(int art);
int opus_art_show(int art, const char *id, int on);
int opus_art_turn(int art, const char *id, int turn4096, int cx, int cy);
int opus_art_colour_slots(int art, unsigned rgb, unsigned char **slots, int max);
void opus_art_set_colour(unsigned char **slots, int n, unsigned rgb);
unsigned opus_art_colours(void);
void opus_sincos(int turn4096, int *s, int *c);    /* x 4096, a in 1/4096 of a turn (-2048..2048) */
/* The frame being drawn, for a page's own pixels (inside its draw only, else 0): pixel (x, y), x across 0..rows-1, y
 * down 0..rows/2-1, is [256 (rows - 1 - x) + y], RGB565; rows 376 (rev 2), 400 (rev 1) or 320 (FE): scale the drawing's
 * units by rows / 320. keep: the lit (non-black) pixels of columns x0..x1, rows y0..y1 as runs into out (cap words):
 * the words used, or -1 when they do not fit; put: them back into the frame (a drawing kept costs a copy a frame). */
unsigned short *opus_page_pixels(int *rows);
int opus_page_keep(unsigned short *out, unsigned cap, int x0, int x1, int y0, int y1);
void opus_page_put(const unsigned short *runs, int used);
/* A page's still part, kept by the core (core 2.8): draw what stays the same between frames first, named by a key
 * that changes whenever it would (a hash of the values it shows, the mode, the page), then what moves:
 *     if (!opus_page_still(self, key)) { ...the still part...; opus_page_still_done(self); }
 *     ...what moves...
 * opus_page_still puts the part back (1) when the frame before had the same key and the core kept it; the same key
 * two frames in a row keeps it. The core's memory, not the plugin's; the panel and the colours are in the key
 * already. Inside a page's draw only. */
int opus_page_still(struct opl_self *self, unsigned key);
void opus_page_still_done(struct opl_self *self);

/* Sequencers (core 2.8): a plugin's sequencer as a row of the OS's sequencer list (shift + the
 * sequencer key), hosted in finger's type as op1r and kennel host theirs: chosen for the synth or the drum, finger's
 * player and page give way to the plugin's for that instrument (finger itself while none is chosen), and op1.db's
 * sequencer table names it, so it comes back at the next start. The player is an object in the OS's player layout
 * (its vtable at +0, entries of 8 bytes: a 16-bit this-adjust, then the function): +0x14 key_down(this, key
 * index 0-23, shift) (non-zero: taken; 0: the key plays as a note), +0x1C key_up(this, index), +0x24 the instrument
 * changed, +0x2C tick(this, pos) (96 a beat, pos 0-47, the current instrument's only), +0x34 a frame, +0x3C
 * stop; the OS calls them in its threads. On its page (the sequencer key) key(mode, code, pressed) and turn(mode,
 * encoder, delta) come first (1: taken; else finger's handling; the octave keys 0x24, 0x25 never: they move the
 * keyboard's octave as on the stock sequencers' pages), and its own OS screen (svg, screen_init, screen_draw: as an
 * engine's) shows in finger's place. midi_start: MIDI START came. mode 0 the synth, 1 the drum. alias: an older name that also chooses it (a saved sound's, op1.db's;
 * written back as name). Returns its index, or -1. A sequencer saves its own data (op1.db through opus_db or a
 * file). */
struct opus_os_vfn {                       /* an OS vtable's entry */
    short adjust, pad;
    void *fn;
};
struct opus_sequencer {
    unsigned size;                         /* sizeof (struct opus_sequencer) */
    const char *name;                      /* its row and its name in op1.db's sequencer table: 1-15 letters */
    void *(*player)(int mode);             /* its player for mode, in the OS's layout */
    int (*key)(int mode, int code, int pressed);
    int (*turn)(int mode, int encoder, int delta);
    void (*midi_start)(int mode);
    void (*chosen)(int mode, int on);      /* chosen for mode, or left for another */
    const char *svg;
    void (*screen_init)(void *scr);
    void (*screen_draw)(void *scr, int dt);
    const char *alias;                     /* or 0 */
    /* From here on (2026-11-07, core 2.31): its page's keys and turns, as every module type's (opus_page above; pg->n is
     * 0: a sequencer keeps its own data). page_turn gets detents, where the older turn got the OS's raw delta. */
    int (*page_key)(struct opus_page *pg, int key, int pressed);
    int (*page_turn)(struct opus_page *pg, int encoder, int detents);
};
int opus_seq_register(struct opl_self *self, const struct opus_sequencer *s);
int opus_seq_mode(void);                   /* the instrument the OS's sequencer calls are for now: 0 synth, 1 drum */

#endif
