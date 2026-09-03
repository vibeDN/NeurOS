# neuros-comp fonts

`neuros-banner.flf` - the FIGlet block font for the big status text (agent name
in the top pane, state word in the bottom pane). Installed to
`/usr/share/neuros/fonts/`.

This is the classic FIGlet **`banner`** font (banner.flf v2 by Ryan Youck, 1994,
merged by John Cowan, modified by Paul Burton). It ships with the standard
FIGlet distribution and is freely redistributable ("I am not responsible for use
of this font"). Chosen for its solid, even, highly legible block glyphs at
full-width (no smushing), which is how `figlet.c` renders.

Swap this file to change the block font - `figlet.c` parses any `flf2a` font.
