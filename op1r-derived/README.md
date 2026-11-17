# Opus 2: the parts derived from op1r

Earlier Opus 2 releases contained code derived from **op1r** by **pcppcp** (https://github.com/pcppcp/op1r, its
sources at https://github.com/pcppcp/op1r-src), which is licensed under the GNU General Public License, version 3.
These files are those parts, as they shipped, published under the same licence (GPL-3.0,
https://www.gnu.org/licenses/gpl-3.0.html). Each file holds only the code and data derived from op1r, with the types
and definitions needed to read them; the comments of the shipped source are left out.

Every one of these parts has been replaced: the core's tape writing and the op1r bridge's helpers in Opus 2 beta 4.2,
dx's label font in opus.dx 0.5.0. The releases that carried them no longer offer those files.

| File | What it is | Shipped in | Derived from (op1r-src) |
|---|---|---|---|
| `tapesafe-beta1-to-beta3.2.c` | the core's tape writing (`tape_write`) | Opus 2 beta 1, 2, 3, 3.1 and 3.2 | `synth_sdk/tape/safewrite.c` (`tape_safe_write`) |
| `tapesafe-beta4-to-beta4.1.c` | the same, its later version | Opus 2 beta 4 and 4.1 | `synth_sdk/tape/safewrite.c` (`tape_safe_write`) |
| `bridge-0.7.0.c` | the op1r bridge's stroke font, its text drawing and helpers (`sq_text`, `sq_num`, `sq_cat`, `sq_node_rotate`) | opus.bridge 0.7.0, with Opus 2 beta 1 | `synth_sdk/screens/stroke_font.h`, `synth_sdk/seq/seq_text.h`, `synth_sdk/op1_plugin.h` |
| `bridge-0.8.0.c` | the same, with `sq_dot`, `sq_join`, `sq_split` and the sequencer store (`sq_store_load`, its save) | opus.bridge 0.8.0, with Opus 2 beta 2 | the above and `synth_sdk/seq/euclid.c` |
| `bridge-0.9.0-and-0.9.1.c` | the same, its later version | opus.bridge 0.9.0 (beta 3, 3.1, 3.2) and 0.9.1 (beta 4) | the above |
| `bridge-0.10.0.c` | the same, its later version | opus.bridge 0.10.0, with Opus 2 beta 4.1 | the above |
| `dx_font-0.3.3-to-0.4.1.c` | opus.dx's label font (`dx_font.c`, whole) | opus.dx 0.3.3 (beta 1), 0.4.0 (beta 2 to 3.2), 0.4.1 (beta 4 to 4.2) | `synth_sdk/screens/stroke_font.h` |

`SHA256SUMS.txt` holds each file's SHA-256.
