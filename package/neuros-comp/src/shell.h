/*
 * NeurOS compositor - the on-screen shell (4-zone layout + gradient wallpaper
 * + FIGlet status text).
 *
 * Fork of cage; this file is NeurOS-specific. MIT.
 */
#ifndef NG_SHELL_H
#define NG_SHELL_H

#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include "figlet.h"

struct cg_server;
struct fcft_font;

#define NG_GRADIENT_BANDS 48
#define NG_FRAME_PX       6
#define NG_FONT_PATH      "/usr/share/neuros/fonts/neuros-standard.flf"

struct ng_shell {
	struct cg_server *server;
	struct wlr_scene_tree *tree;    /* chrome, below the views */
	struct wlr_scene_tree *overlay; /* raised above the views (overlay buttons) */

	struct flf_font *font;      /* the .flf */
	struct fcft_font *big_font; /* monospace, sized to the panes */
	int big_size;              /* current big_font px size (0 = none) */

	struct wlr_scene_rect *band[NG_GRADIENT_BANDS];
	float top_color[4];
	float bottom_color[4];

	/* frosted-glass panels (rounded-rect pixman buffers) */
	struct wlr_scene_buffer *top_panel;
	struct wlr_scene_buffer *center_panel;
	struct wlr_scene_buffer *bottom_panel;

	/* big text (Doto via fcft, scaled to the pane) */
	struct wlr_scene_buffer *agent_node;
	char *agent_text;
	struct wlr_scene_buffer *status_node;
	char *status_text;
	struct wlr_scene_buffer *model_node; /* model-name pill */
	char *model_text;

	/* 6-agent scaffold dots */
	struct wlr_scene_buffer *dot[6];
	int active_dot;

	struct wlr_scene_buffer *home_node; /* home indicator bar */

	/* centre-panel overlay buttons */
	struct wlr_scene_buffer *cam_node;
	struct wlr_scene_buffer *mic_node;
	struct wlr_box cam_box;
	struct wlr_box mic_box;
	int mic_on;

	/* small mono text via fcft: top strip (clock) + bottom activity sub-line */
	struct fcft_font *strip_font;
	struct wlr_scene_buffer *strip_node;       /* left: clock / date */
	char *strip_text;
	struct wlr_scene_buffer *strip_right_node; /* right: battery */
	char *strip_right_text;
	struct wlr_scene_buffer *activity_node;
	char *activity_text;

	struct wlr_box strip_box;
	struct wlr_box top_box;
	struct wlr_box center_box;
	struct wlr_box bottom_box;

	int width, height;
	int frame_t;   /* hairline border */
	int panel_rad; /* glass panel corner radius - view.c insets the client by it */
};

struct ng_shell *ng_shell_create(struct cg_server *server);
void ng_shell_destroy(struct ng_shell *shell);

void ng_shell_layout(struct ng_shell *shell, int width, int height);
void ng_shell_set_colors(struct ng_shell *shell, const float top[4], const float bottom[4]);

/* Big Doto text in the top / bottom panes. */
void ng_shell_set_agent(struct ng_shell *shell, const char *name);
void ng_shell_set_status(struct ng_shell *shell, const char *state);
void ng_shell_set_model(struct ng_shell *shell, const char *model);

/* Small mono text: top strip (left clock/date, right battery) + activity line. */
void ng_shell_set_strip(struct ng_shell *shell, const char *text);
void ng_shell_set_strip_right(struct ng_shell *shell, const char *text);
void ng_shell_set_activity(struct ng_shell *shell, const char *text);

/* Centre-panel overlay buttons. */
void ng_shell_set_mic(struct ng_shell *shell, int on);

/* Hit-test the overlay buttons at layout coords: returns 1 = camera, 2 = mic,
 * 0 = neither. On a hit, the compositor consumes the press and runs the action. */
int ng_shell_button_at(struct ng_shell *shell, double lx, double ly);
void ng_shell_press_button(struct ng_shell *shell, int which);

/* Keep the overlay buttons above the client; call when a view maps. */
void ng_shell_raise_overlay(struct ng_shell *shell);

#endif
