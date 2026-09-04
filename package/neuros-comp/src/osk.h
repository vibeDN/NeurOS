/*
 * NeurOS compositor - built-in on-screen keyboard.
 *
 * A scene overlay drawn by the compositor (glass keys, like the shell chrome).
 * Taps are hit-tested and injected as real key events through the seat, so the
 * client (foot / the agent CLI) sees ordinary keystrokes. No layer-shell, no
 * external OSK process. MIT.
 */
#ifndef NG_OSK_H
#define NG_OSK_H

#include <stdbool.h>

struct cg_server;
struct ng_osk;

struct ng_osk *ng_osk_create(struct cg_server *server);
void ng_osk_destroy(struct ng_osk *osk);

/* Recompute geometry for a `w`x`h` screen and redraw. */
void ng_osk_layout(struct ng_osk *osk, int w, int h);

void ng_osk_set_visible(struct ng_osk *osk, bool visible);
bool ng_osk_is_visible(struct ng_osk *osk);

/* Layout coords. Returns true if the tap landed on the keyboard (consume it). */
bool ng_osk_tap(struct ng_osk *osk, double lx, double ly);

/* Y of the keyboard's top edge when visible (screen coords), or `h` when
 * hidden - the shell shrinks the client above this. */
int ng_osk_top(struct ng_osk *osk);

#endif
