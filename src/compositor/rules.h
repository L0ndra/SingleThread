/*
 * SingleThread - Task-Centric Wayland Compositor
 * rules.h - Window rule matching engine
 */
#ifndef STW_RULES_H
#define STW_RULES_H

#include <stdbool.h>
#include <stdint.h>
#include <regex.h>
#include <wayland-server-core.h>

struct stw_server;
struct stw_view;

/* ─── Rule match criteria ──────────────────────────────────────── */
struct stw_rule_match {
	char *app_id;          /* exact match on app_id / WM_CLASS */
	char *title_pattern;   /* regex match on window title */
	regex_t title_regex;   /* compiled title regex */
	bool title_regex_compiled;
	char *class_name;      /* XWayland WM_CLASS (instance) */
};

/* ─── Rule actions ─────────────────────────────────────────────── */
struct stw_rule_action {
	/* Task assignment */
	char *task_name;       /* assign to named task (NULL = no override) */
	bool assign_current;   /* assign to currently active task */

	/* Global flag */
	bool set_global;       /* true = force global */
	bool has_global;       /* whether global was specified */

	/* Layout */
	bool set_floating;     /* true = force floating */
	bool has_floating;     /* whether floating was specified */

	/* Geometry */
	bool has_size;
	int width, height;
	bool has_position;
	int x, y;

	/* Workspace */
	int workspace;         /* -1 = no override */
};

/* ─── Rule ─────────────────────────────────────────────────────── */
struct stw_rule {
	struct wl_list link;   /* stw_server.rules */
	int priority;          /* lower = higher priority */

	struct stw_rule_match match;
	struct stw_rule_action action;
};

/* Rules engine */
void stw_rules_init(struct stw_server *server);
void stw_rules_finish(struct stw_server *server);

/* Add/remove rules */
struct stw_rule *stw_rule_create(struct stw_server *server);
void stw_rule_destroy(struct stw_rule *rule);

/* Load rules from config */
void stw_rules_load(struct stw_server *server);

/* Apply rules to a view - returns true if any rule matched */
bool stw_rules_apply(struct stw_server *server, struct stw_view *view);

/* Debug: trace rule matching for a view */
void stw_rules_trace(struct stw_server *server, struct stw_view *view);

#endif /* STW_RULES_H */
