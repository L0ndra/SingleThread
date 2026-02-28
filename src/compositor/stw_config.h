/*
 * SingleThread - Task-Centric Wayland Compositor
 * stw_config.h - Configuration system
 */
#ifndef STW_CONFIG_H
#define STW_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct stw_server;

/* ─── Keybinding ───────────────────────────────────────────────── */
struct stw_keybind {
	struct wl_list link;   /* stw_config.keybindings */
	uint32_t modifiers;    /* WLR_MODIFIER_* bitmask */
	xkb_keysym_t keysym;
	char *action;          /* action string e.g. "task:create" */
};

/* ─── Output config ────────────────────────────────────────────── */
struct stw_output_config {
	struct wl_list link;
	char *name;
	float scale;
	int transform;
	bool has_position;
	int x, y;
	char *mode;            /* "preferred" or "WxH@R" */
};

/* ─── Appearance config ────────────────────────────────────────── */
struct stw_appearance {
	int border_width;
	uint32_t border_focused;
	uint32_t border_unfocused;
	uint32_t border_urgent;
	uint32_t background;
};

/* ─── Shell config ─────────────────────────────────────────────── */
struct stw_shell_config {
	/* Panel */
	char *panel_position;  /* "top" or "bottom" */
	int panel_height;
	bool show_tasks;
	bool show_workspaces;
	bool show_tray;
	bool show_clock;
	char *clock_format;
	bool show_title;

	/* Launcher */
	bool launcher_show_recent;
	int launcher_max_results;

	/* Notifications */
	bool notifications_enabled;
	int notification_timeout;
	int notification_max_visible;
	char *notification_position;
};

/* ─── Core configuration ──────────────────────────────────────── */
struct stw_config {
	/* General */
	char *default_layout;
	int master_count;
	double master_ratio;
	int gaps_inner;
	int gaps_outer;
	bool focus_follows_mouse;
	bool cursor_warp;

	/* Tasks */
	bool task_create_default;
	char *task_default_name;
	bool task_confirm_delete;
	bool task_restore_focus;
	int task_max_tasks;

	/* Keybindings */
	struct wl_list keybindings; /* stw_keybind.link */

	/* Output configs */
	struct wl_list output_configs; /* stw_output_config.link */

	/* Appearance */
	struct stw_appearance appearance;

	/* Shell */
	struct stw_shell_config shell;

	/* Config file path (for reload) */
	char *path;
	int watch_fd; /* inotify fd for live reload */
	struct wl_event_source *watch_source;
};

/* Config lifecycle */
struct stw_config *stw_config_create(void);
void stw_config_destroy(struct stw_config *config);

/* Load from file */
bool stw_config_load(struct stw_config *config, const char *path);

/* Apply defaults */
void stw_config_defaults(struct stw_config *config);

/* Live reload */
bool stw_config_watch(struct stw_config *config, struct stw_server *server);
void stw_config_reload(struct stw_server *server);

/* Config path resolution */
char *stw_config_find_path(void);

/* Keybinding helpers */
struct stw_keybind *stw_config_find_keybind(struct stw_config *config,
	uint32_t modifiers, xkb_keysym_t keysym);
bool stw_config_parse_keybind(const char *spec, uint32_t *modifiers,
	xkb_keysym_t *keysym);

/* Color parsing */
uint32_t stw_config_parse_color(const char *hex);

#endif /* STW_CONFIG_H */
