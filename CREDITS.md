# Credits

Opus borrows ideas, and says where they came from. Nothing here is copied one for one: every feature is rethought
and built from scratch for Opus. But an idea came from somewhere, and that deserves a thank-you.

## Ideas in Opus 1 (now Opus 2's starter pack)

- **Scales:** the idea comes from stfj's note-quantization patch for 246
  ([OP-1-Note-Quantization-PublicGit](https://github.com/stfj/OP-1-Note-Quantization-PublicGit)). Opus's scales
  were written from the ground up.
- **The hidden filter:** wavi showed in 2016 that the OP-1 has a finished filter effect hidden inside it. Opus puts
  it in the effect list.
- **Iter:** the hidden synth engine people in the custom firmware thread found in 2016. Opus 2 puts it back in the
  engine list.
- **The OP-1 field:** Teenage Engineering's OP-1 field inspired the dimension and amp engines, the menu's style, the field
  theme and tape undo. (Opus isn't affiliated with Teenage Engineering.)
- **Getting in at all:** TabascoEye, who opened up the OP-1's update format in 2016, and op1repacker by wavi and
  op1hacks.

## Code in Opus 2

- **dx:** opus.dx contains code derived from Google's
  [Music Synthesizer for Android](https://github.com/google/music-synthesizer-for-android) (Apache License 2.0); its
  LICENSE and NOTICE come with it in the starter pack. Its voices are Opus's own (no Yamaha data).

## Ideas in Opus 2

- **The plugin system:** VST3, CLAP, Fabric, Forge, Harmony and Mixin, for how plugins are named, described, loaded
  and kept working ([docs/design.md](docs/design.md)).
- **Memory first:** Felucca's plugin experiment on the M-VAVE FM-1, for the lesson that memory needs planning first.
- **The bridge:** Sinytra Connector, which runs Fabric mods on NeoForge as a mod of its own, for the shape of loading
  another loader's plugins ([Sinytra/Connector](https://github.com/Sinytra/Connector)); Rackes, who asked for op1r's
  mods on Opus (op-forums); and the formats it loads, op1r's plugin files (`.op1p`) by popcorp and Hundekraft's
  kennel files (`.knl`) by Hundekraft. The bridge is Opus's own code, written from their published formats. Every
  bridged plugin stays its author's work.
- **The flash fix:** op1r's release notes also pointed at recording wearing out good flash.
- **The FN key:** help as a modifier for plugins' controls. Thanks to katerlouis, who suggested it too.
- **The trigger guard:** u/Andromax_sussybaka's idea on r/OP1Mods, a guard against keys that fire twice. Opus's
  plugin is its own take on it.

## Thanks

popcorp, whose op1r brought the scene back, and everyone in the Custom Firmware thread on op-forums.

Missing someone? Tell us in [Discussions](https://github.com/LizardMan245/opus/discussions).
