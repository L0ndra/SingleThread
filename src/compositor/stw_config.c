/*
 * SingleThread - Task-Centric Wayland Compositor
 * stw_config.c - Configuration system (TOML parsing)
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "stw_config.h"
#include "server.h"
#include "rules.h"

/* ─── Simple TOML-subset parser ────────────────────────────────── */
/* This handles the subset of TOML we need:
 * - key = "value" / key = number / key = bool
 * - [section] / [section.subsection]
 * - [[array_of_tables]]
 * - # comments
 */

#define MAX_LINE 2048
#define MAX_KEY 256
#define MAX_VAL 1024

struct toml_state {
	char section[MAX_KEY];
	struct stw_config *config;
	struct stw_server *server; /* For rules */
};

static char *trim(char *s) {
	while (isspace((unsigned char)*s)) s++;
	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
	return s;
}

static char *unquote(char *s) {
	s = trim(s);
	size_t len = strlen(s);
	if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
		s[len - 1] = '\0';
		return s + 1;
	}
	return s;
}

static bool parse_bool(const char *val) {
	return strcmp(val, "true") == 0 || strcmp(val, "yes") == 0 ||
		strcmp(val, "1") == 0;
}

static void config_set_general(struct stw_config *config,
		const char *key, const char *val) {
	if (strcmp(key, "default_layout") == 0) {
		free(config->default_layout);
		config->default_layout = strdup(val);
	} else if (strcmp(key, "master_count") == 0) {
		config->master_count = atoi(val);
	} else if (strcmp(key, "master_ratio") == 0) {
		config->master_ratio = atof(val);
	} else if (strcmp(key, "gaps_inner") == 0) {
		config->gaps_inner = atoi(val);
	} else if (strcmp(key, "gaps_outer") == 0) {
		config->gaps_outer = atoi(val);
	} else if (strcmp(key, "focus_follows_mouse") == 0) {
		config->focus_follows_mouse = parse_bool(val);
	} else if (strcmp(key, "cursor_warp") == 0) {
		config->cursor_warp = parse_bool(val);
	}
}

static void config_set_tasks(struct stw_config *config,
		const char *key, const char *val) {
	if (strcmp(key, "create_default") == 0) {
		config->task_create_default = parse_bool(val);
	} else if (strcmp(key, "default_name") == 0) {
		free(config->task_default_name);
		config->task_default_name = strdup(val);
	} else if (strcmp(key, "confirm_delete") == 0) {
		config->task_confirm_delete = parse_bool(val);
	} else if (strcmp(key, "restore_focus") == 0) {
		config->task_restore_focus = parse_bool(val);
	} else if (strcmp(key, "max_tasks") == 0) {
		config->task_max_tasks = atoi(val);
	}
}

static void config_set_appearance(struct stw_config *config,
		const char *key, const char *val) {
	if (strcmp(key, "border_width") == 0) {
		config->appearance.border_width = atoi(val);
	} else if (strcmp(key, "border_focused") == 0) {
		config->appearance.border_focused = stw_config_parse_color(val);
	} else if (strcmp(key, "border_unfocused") == 0) {
		config->appearance.border_unfocused = stw_config_parse_color(val);
	} else if (strcmp(key, "border_urgent") == 0) {
		config->appearance.border_urgent = stw_config_parse_color(val);
	} else if (strcmp(key, "background") == 0) {
		config->appearance.background = stw_config_parse_color(val);
	}
}

static void config_set_shell_panel(struct stw_config *config,
		const char *key, const char *val) {
	struct stw_shell_config *shell = &config->shell;
	if (strcmp(key, "position") == 0) {
		free(shell->panel_position);
		shell->panel_position = strdup(val);
	} else if (strcmp(key, "height") == 0) {
		shell->panel_height = atoi(val);
	} else if (strcmp(key, "show_tasks") == 0) {
		shell->show_tasks = parse_bool(val);
	} else if (strcmp(key, "show_workspaces") == 0) {
		shell->show_workspaces = parse_bool(val);
	} else if (strcmp(key, "show_tray") == 0) {
		shell->show_tray = parse_bool(val);
	} else if (strcmp(key, "show_clock") == 0) {
		shell->show_clock = parse_bool(val);
	} else if (strcmp(key, "clock_format") == 0) {
		free(shell->clock_format);
		shell->clock_format = strdup(val);
	} else if (strcmp(key, "show_title") == 0) {
		shell->show_title = parse_bool(val);
	}
}

static void config_set_keybinding(struct stw_config *config,
		const char *key_spec, const char *action) {
	uint32_t modifiers;
	xkb_keysym_t keysym;
	if (!stw_config_parse_keybind(key_spec, &modifiers, &keysym)) {
		wlr_log(WLR_ERROR, "Invalid keybinding: %s", key_spec);
		return;
	}

	struct stw_keybind *kb = calloc(1, sizeof(*kb));
	if (!kb) return;

	kb->modifiers = modifiers;
	kb->keysym = keysym;
	kb->action = strdup(action);
	wl_list_insert(config->keybindings.prev, &kb->link);
}

/* Current rule being built in [[rule]] parsing */
static struct stw_rule *current_rule = NULL;

static void config_set_rule(struct toml_state *state,
		const char *key, const char *val) {
	if (!current_rule && state->server) {
		current_rule = stw_rule_create(state->server);
	}
	if (!current_rule) return;

	if (strcmp(key, "app_id") == 0) {
		free(current_rule->match.app_id);
		current_rule->match.app_id = strdup(val);
	} else if (strcmp(key, "title") == 0) {
		free(current_rule->match.title_pattern);
		current_rule->match.title_pattern = strdup(val);
	} else if (strcmp(key, "class") == 0) {
		free(current_rule->match.class_name);
		current_rule->match.class_name = strdup(val);
	} else if (strcmp(key, "task") == 0) {
		free(current_rule->action.task_name);
		current_rule->action.task_name = strdup(val);
	} else if (strcmp(key, "global") == 0) {
		current_rule->action.has_global = true;
		current_rule->action.set_global = parse_bool(val);
	} else if (strcmp(key, "floating") == 0) {
		current_rule->action.has_floating = true;
		current_rule->action.set_floating = parse_bool(val);
	} else if (strcmp(key, "workspace") == 0) {
		current_rule->action.workspace = atoi(val);
	}
	/* size = { width = N, height = N } handled separately */
}

static void parse_line(struct toml_state *state, char *line) {
	line = trim(line);

	/* Skip empty lines and comments */
	if (*line == '\0' || *line == '#') return;

	/* Array of tables: [[section]] */
	if (line[0] == '[' && line[1] == '[') {
		char *end = strstr(line + 2, "]]");
		if (end) {
			*end = '\0';
			char *name = trim(line + 2);
			snprintf(state->section, MAX_KEY, "[[%s]]", name);

			if (strcmp(name, "rule") == 0) {
				current_rule = NULL; /* Will be created on first key */
			}
		}
		return;
	}

	/* Section: [section] */
	if (line[0] == '[') {
		char *end = strchr(line + 1, ']');
		if (end) {
			*end = '\0';
			snprintf(state->section, MAX_KEY, "%s", trim(line + 1));
		}
		return;
	}

	/* Key = value */
	char *eq = strchr(line, '=');
	if (!eq) return;

	*eq = '\0';
	char *key = trim(line);
	char *val = unquote(eq + 1);

	/* Dispatch based on current section */
	if (strcmp(state->section, "general") == 0) {
		config_set_general(state->config, key, val);
	} else if (strcmp(state->section, "tasks") == 0) {
		config_set_tasks(state->config, key, val);
	} else if (strcmp(state->section, "appearance") == 0) {
		config_set_appearance(state->config, key, val);
	} else if (strcmp(state->section, "shell.panel") == 0) {
		config_set_shell_panel(state->config, key, val);
	} else if (strcmp(state->section, "keybindings") == 0) {
		config_set_keybinding(state->config, key, val);
	} else if (strncmp(state->section, "[[rule]]", 8) == 0) {
		config_set_rule(state, key, val);
	}
}

/* ─── Config lifecycle ─────────────────────────────────────────── */

struct stw_config *stw_config_create(void) {
	struct stw_config *config = calloc(1, sizeof(*config));
	if (!config) return NULL;

	wl_list_init(&config->keybindings);
	wl_list_init(&config->output_configs);
	config->watch_fd = -1;

	stw_config_defaults(config);
	return config;
}

void stw_config_defaults(struct stw_config *config) {
	config->default_layout = strdup("master-stack");
	config->master_count = 1;
	config->master_ratio = 0.55;
	config->gaps_inner = 6;
	config->gaps_outer = 6;
	config->focus_follows_mouse = true;
	config->cursor_warp = false;

	config->task_create_default = true;
	config->task_default_name = strdup("General");
	config->task_confirm_delete = true;
	config->task_restore_focus = true;
	config->task_max_tasks = 0;

	config->appearance.border_width = 2;
	config->appearance.border_focused = 0x7aa2f7ff;
	config->appearance.border_unfocused = 0x565f89ff;
	config->appearance.border_urgent = 0xf7768eff;
	config->appearance.background = 0x1a1b26ff;

	config->shell.panel_position = strdup("top");
	config->shell.panel_height = 32;
	config->shell.show_tasks = true;
	config->shell.show_workspaces = true;
	config->shell.show_tray = true;
	config->shell.show_clock = true;
	config->shell.clock_format = strdup("%H:%M");
	config->shell.show_title = true;
	config->shell.launcher_show_recent = true;
	config->shell.launcher_max_results = 20;
	config->shell.notifications_enabled = true;
	config->shell.notification_timeout = 5000;
	config->shell.notification_max_visible = 5;
	config->shell.notification_position = strdup("top-right");
}

void stw_config_destroy(struct stw_config *config) {
	if (!config) return;

	/* Clean up keybindings */
	struct stw_keybind *kb, *kb_tmp;
	wl_list_for_each_safe(kb, kb_tmp, &config->keybindings, link) {
		wl_list_remove(&kb->link);
		free(kb->action);
		free(kb);
	}

	/* Clean up output configs */
	struct stw_output_config *oc, *oc_tmp;
	wl_list_for_each_safe(oc, oc_tmp, &config->output_configs, link) {
		wl_list_remove(&oc->link);
		free(oc->name);
		free(oc->mode);
		free(oc);
	}

	if (config->watch_fd >= 0) {
		close(config->watch_fd);
	}

	free(config->default_layout);
	free(config->task_default_name);
	free(config->shell.panel_position);
	free(config->shell.clock_format);
	free(config->shell.notification_position);
	free(config->path);
	free(config);
}

bool stw_config_load(struct stw_config *config, const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		wlr_log(WLR_ERROR, "Cannot open config: %s: %s",
			path, strerror(errno));
		return false;
	}

	free(config->path);
	config->path = strdup(path);

	struct toml_state state = {0};
	state.config = config;

	char line[MAX_LINE];
	int line_num = 0;
	while (fgets(line, sizeof(line), f)) {
		line_num++;
		/* Remove newline */
		line[strcspn(line, "\n\r")] = '\0';
		parse_line(&state, line);
	}

	fclose(f);

	wlr_log(WLR_INFO, "Config loaded: %s (%d lines)", path, line_num);
	return true;
}

/* ─── Config path resolution ───────────────────────────────────── */

char *stw_config_find_path(void) {
	char path[PATH_MAX];

	/* Check XDG_CONFIG_HOME first */
	const char *xdg_config = getenv("XDG_CONFIG_HOME");
	if (xdg_config) {
		snprintf(path, sizeof(path), "%s/singlethread/config.toml",
			xdg_config);
		if (access(path, R_OK) == 0) {
			return strdup(path);
		}
	}

	/* Check ~/.config */
	const char *home = getenv("HOME");
	if (home) {
		snprintf(path, sizeof(path),
			"%s/.config/singlethread/config.toml", home);
		if (access(path, R_OK) == 0) {
			return strdup(path);
		}
	}

	/* Check system config */
	snprintf(path, sizeof(path), "%s/singlethread/config.toml",
		STW_SYSCONFDIR);
	if (access(path, R_OK) == 0) {
		return strdup(path);
	}

	return NULL;
}

/* ─── Live reload ──────────────────────────────────────────────── */

static int config_watch_handler(int fd, uint32_t mask, void *data) {
	struct stw_server *server = data;
	(void)mask;

	/* Read inotify events */
	char buf[4096];
	ssize_t n = read(fd, buf, sizeof(buf));
	if (n <= 0) return 0;

	wlr_log(WLR_INFO, "Config file changed, reloading...");
	stw_config_reload(server);
	return 0;
}

bool stw_config_watch(struct stw_config *config, struct stw_server *server) {
	if (!config->path) return false;

	config->watch_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	if (config->watch_fd < 0) {
		wlr_log(WLR_ERROR, "Failed to init inotify for config watch");
		return false;
	}

	int wd = inotify_add_watch(config->watch_fd, config->path,
		IN_MODIFY | IN_CLOSE_WRITE);
	if (wd < 0) {
		wlr_log(WLR_ERROR, "Failed to watch config file: %s",
			strerror(errno));
		close(config->watch_fd);
		config->watch_fd = -1;
		return false;
	}

	config->watch_source = wl_event_loop_add_fd(
		server->wl_event_loop, config->watch_fd,
		WL_EVENT_READABLE, config_watch_handler, server);

	wlr_log(WLR_INFO, "Watching config file for changes: %s", config->path);
	return true;
}

void stw_config_reload(struct stw_server *server) {
	if (!server->config->path) return;

	/* Create a new config and load into it */
	struct stw_config *new_config = stw_config_create();
	if (!new_config) return;

	if (!stw_config_load(new_config, server->config->path)) {
		wlr_log(WLR_ERROR,
			"Config reload failed, keeping current config");
		stw_config_destroy(new_config);
		return;
	}

	/* Preserve watch state */
	new_config->watch_fd = server->config->watch_fd;
	new_config->watch_source = server->config->watch_source;
	server->config->watch_fd = -1;
	server->config->watch_source = NULL;

	/* Swap configs */
	stw_config_destroy(server->config);
	server->config = new_config;

	/* Re-apply layout */
	stw_layout_arrange_all(server);

	wlr_log(WLR_INFO, "Config reloaded successfully");
}

/* ─── Keybinding helpers ───────────────────────────────────────── */

struct stw_keybind *stw_config_find_keybind(struct stw_config *config,
		uint32_t modifiers, xkb_keysym_t keysym) {
	struct stw_keybind *kb;
	wl_list_for_each(kb, &config->keybindings, link) {
		if (kb->modifiers == modifiers && kb->keysym == keysym) {
			return kb;
		}
	}
	return NULL;
}

bool stw_config_parse_keybind(const char *spec, uint32_t *modifiers,
		xkb_keysym_t *keysym) {
	*modifiers = 0;
	*keysym = XKB_KEY_NoSymbol;

	char buf[256];
	snprintf(buf, sizeof(buf), "%s", spec);

	char *saveptr;
	char *token = strtok_r(buf, "+", &saveptr);
	char *last_token = NULL;

	while (token) {
		token = trim(token);
		char *next = strtok_r(NULL, "+", &saveptr);

		if (next) {
			/* This is a modifier */
			if (strcasecmp(token, "Super") == 0 ||
					strcasecmp(token, "Mod4") == 0 ||
					strcasecmp(token, "Logo") == 0) {
				*modifiers |= WLR_MODIFIER_LOGO;
			} else if (strcasecmp(token, "Alt") == 0 ||
					strcasecmp(token, "Mod1") == 0) {
				*modifiers |= WLR_MODIFIER_ALT;
			} else if (strcasecmp(token, "Ctrl") == 0 ||
					strcasecmp(token, "Control") == 0) {
				*modifiers |= WLR_MODIFIER_CTRL;
			} else if (strcasecmp(token, "Shift") == 0) {
				*modifiers |= WLR_MODIFIER_SHIFT;
			} else {
				wlr_log(WLR_ERROR, "Unknown modifier: %s", token);
				return false;
			}
		} else {
			last_token = token;
		}

		token = next;
	}

	if (!last_token) return false;

	*keysym = xkb_keysym_from_name(last_token, XKB_KEYSYM_CASE_INSENSITIVE);
	if (*keysym == XKB_KEY_NoSymbol) {
		wlr_log(WLR_ERROR, "Unknown keysym: %s", last_token);
		return false;
	}

	return true;
}

/* ─── Color parsing ────────────────────────────────────────────── */

uint32_t stw_config_parse_color(const char *hex) {
	if (!hex) return 0;
	if (*hex == '#') hex++;

	uint32_t color = 0;
	size_t len = strlen(hex);

	if (len == 6) {
		color = (uint32_t)strtoul(hex, NULL, 16);
		color = (color << 8) | 0xFF; /* Add full alpha */
	} else if (len == 8) {
		color = (uint32_t)strtoul(hex, NULL, 16);
	}

	return color;
}
