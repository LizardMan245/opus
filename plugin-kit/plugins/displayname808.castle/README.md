# castle (displayname808.castle)

A drone synth engine for Opus 2 that sustains for ever. Its oscillator and detune are the Lode port's
(displayname808.lode, after Lode by Fors, MIT licence): Lode's PM saw and its detuned twin, sample for sample.

- **Play a key:** the drone fades in over 2 seconds and keeps sounding after you let go. Nothing stops it by itself.
- **Play another key:** the old drone fades out over 3 seconds while the new one fades in. One drone at a time.
- **Push orange (the red encoder) on T1:** drone mode on or off. Off, the engine plays like a normal synth: a note
  sounds while its key is down (chords too) and fades over 3 seconds when you let go. Turning drone mode off lets a
  drone that is still sounding fade out. The mode is kept with the sound and its presets; a new sound starts with it on.
  The keep's flagpole shows it: a pennant flies in drone mode, the pole stands bare out of it.

On T1, the encoders:

| Encoder | Knob | What it does |
|---|---|---|
| blue | detune | Lode's detune: a second saw, in Hz up to 5 % of the knob (slow beating, about 6 Hz), in semitones above 10 % (up to 12); at 0 only one saw plays, 3 dB quieter |
| green | drift | a slow random wander of the pitch (up to 12 cents) and the filter (up to an octave) |
| white | crackle | hiss, crackles and short drop-outs |
| orange | heat | opens the filter and drives a soft overdrive; the level is kept the same from cold to hot |

The engine shapes its own level, so the T2 envelope does nothing for its sounds, and velocity is ignored.
Controls taken: the orange push on its own T1 page (drone mode; modal, so it never clashes with another plugin).

## The page (T1)

The dotted castle on its island, drawn every frame straight into the screen's pixels, 1:1 on every OP-1 panel,
centred in what the screen shows (about 224 x 130 pixels; water and leaves fall past the bottom edge):

- **blue:** water pours off the platform's front edges, a stream at a time as you turn it up
- **green:** leaves fall from the trees and pile up under them (the pile keeps growing while green is up and clears
  slowly when it is down), and tumble off the platform's edge
- **white:** the castle flickers, and lightning strikes the towers, the keep and the ground (more often as you turn up)
- **orange:** the windows glow, then flames come out of them, the towers, then the keep

No values are shown: the scene is the display. Every colour is the encoder colour your theme sets, or a grey.
`art/make_art.py` writes `castle_art.h` (the scene's pixels and particles) from `art/scene.py`.
`host/page_preview.c` draws the page on the computer from the same C.

## Build

From the kit's folder (needs the Blackfin compiler, see docs/getting-started.md):

```
python plugin_tool.py build plugins/displayname808.castle
python plugin_tool.py show build/castle.opmod
```

## Hear it on the computer

`host/render.c` plays the same sound code (the C form of `castle_saw.S`'s loop) the way the OS does and writes a WAV:

```
cd plugins/displayname808.castle/host
cc -O2 -Wall -Wextra -Werror -I.. render.c ../castle_dsp.c -o render -lm
./render demo demo.wav        # each knob swept, a second note crossfading, the orange push
./render sustain sus.wav      # ten minutes of one note, the key let go at 0.3 s
./render modes modes.wav      # drone mode off (a note, a chord, let go), then on (a note, sustaining)
```

`host/lodecheck.c` checks castle's oscillator against the Lode port's `lode_dsp.c` sample by sample (needs that
file with a one-line hook; see its top).

## Speed (0.3.5)

Measured in the Blackfin simulator (GDB's `bfin-elf-run`, built from the same binutils as the compiler), with the
kit's compiler flags. The simulator counts branches and 32-bit multiplies but not pipeline stalls or memory waits,
which the device adds, so read these as before/after:

| a steady drone, one voice, an audio block | 0.3.1 | 0.3.2 | 0.3.5 |
|---|---|---|---|
| default (detune 3, drift 25, heat 25) | 42,400 | 33,700 | 22,100 cycles |
| detune 0 (one saw) | | 27,100 | 16,900 |
| crackle 50 | | 40,200 | 27,200 |
| everything up | | 39,800 | 27,300 |

| the page | 0.3.1 | 0.3.2 |
|---|---|---|
| at rest, a frame | 108,000 | 20,000 cycles |
| all four effects at 85, a frame | 710,000 (worst 874,000) | 273,000 (worst 306,000) |

0.3.5 is the same sound sample for sample: a hash of 1.5 million samples in the simulator (random knob jumps, glides,
vibrato every sample, both modes, same-note re-presses) and 9 million on the computer match 0.3.4 exactly. What it
uses of the Blackfin:

- **castle_saw.S**, the two saws in assembly: a hardware loop (no counter or branch per sample); both products of
  the first operator's output in one dual 16 x 16 multiply; each sine interpolation as a truncating fractional
  multiply into a half register and a 16-bit add (two instructions for four); the fraction cut out with one
  EXTRACT; loads issued in the same cycle as arithmetic; every value in a register. 25 cycles a sample a saw
  (the compiler's loop: about 45, with stack traffic). The next sample's first lookup is computed beside this
  sample's second, and each address is moved to its pointer register a few instructions before it is used, which
  hides the Blackfin's data-to-pointer stall on the device.
- The filter, heat, crackle and level **a stage at a time** over the block (their settings hold for a block), so each
  stage's loop fits the eight data registers; the compiler then gives each a hardware loop.
- The **soft clip without branches** (MIN, MAX, a conditional move) and its signed x unsigned product in one
  mixed-mode multiply; checked equal to the old soft clip for every input it can get.
- The block's work buffer in **fast memory** beside the sine pairs.

The page looks the same as 0.3.1; particles land a pixel or a phase step apart, as its divides became multiplies.

## Not done yet

- Lode's pulse, shape and sub are not used: only its saw and detune.
- The CPU figure: measure "audio: worst N" on the OP-1 and write it to `cpu.txt`.
- Not yet tried on an OP-1.
