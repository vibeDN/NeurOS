/*
 * NeurOS compositor - the on-screen shell (4-zone layout + gradient wallpaper).
 *
 * Fork of cage; this file is NeurOS-specific. MIT.
 */
#ifndef NG_SHELL_H
#define NG_SHELL_H

#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

struct cg_server;

#define NG_GRADIENT_BANDS 48
#define NG_FRAME_PX       6

struct ng_shell {
	struct cg_server *server;

	/* chrome lives under here, below the views tree */
	struct wlr_scene_tree *tree;

	/* wallpaper: a vertical stack of solid bands approximating a 2-stop lerp */
	struct wlr_scene_rect *band[NG_GRADIENT_BANDS];
	float top_color[4];
	float bottom_color[4];

	/* frame borders (each: bg rect + we inset the content) */
	struct wlr_scene_rect *top_frame;
	struct wlr_scene_rect *center_frame;
	struct wlr_scene_rect *bottom_frame;
	struct wlr_scene_rect *center_inner;

	/* computed zone boxes in layout coords */
	struct wlr_box strip_box;   /* time/date/battery */
	struct wlr_box top_box;     /* agent name + model */
	struct wlr_box center_box;  /* the embedded client goes here */
	struct wlr_box bottom_box;  /* state + activity */

	int width, height;
};

struct ng_shell *ng_shell_create(struct cg_server *server);
void ng_shell_destroy(struct ng_shell *shell);

/* Recompute zone geometry for a W x H output and reposition every node. */
void ng_shell_layout(struct ng_shell *shell, int width, int height);

/* Set the wallpaper stops (RGBA, 0..1, straight alpha) and rebuild bands. */
void ng_shell_set_colors(struct ng_shell *shell, const float top[4], const float bottom[4]);

#endif
