/*
 * SingleThread - Task-Centric Wayland Compositor
 * rules.c - Window rule matching engine
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "rules.h"
#include "server.h"
#include "view.h"
#include "task.h"

void stw_rules_init(struct stw_server *server) {
	wl_list_init(&server->rules);
}

void stw_rules_finish(struct stw_server *server) {
	struct stw_rule *rule, *tmp;
	wl_list_for_each_safe(rule, tmp, &server->rules, link) {
		stw_rule_destroy(rule);
	}
}

struct stw_rule *stw_rule_create(struct stw_server *server) {
	struct stw_rule *rule = calloc(1, sizeof(*rule));
	if (!rule) return NULL;

	rule->action.workspace = -1;
	wl_list_insert(server->rules.prev, &rule->link);
	return rule;
}

void stw_rule_destroy(struct stw_rule *rule) {
	if (rule->match.title_regex_compiled) {
		regfree(&rule->match.title_regex);
	}
	free(rule->match.app_id);
	free(rule->match.title_pattern);
	free(rule->match.class_name);
	free(rule->action.task_name);
	wl_list_remove(&rule->link);
	free(rule);
}

/* Load rules from the config (called after config is loaded) */
void stw_rules_load(struct stw_server *server) {
	/* Rules are loaded during config parsing via stw_config_load().
	 * This function is called to finalize/compile any rule patterns. */
	struct stw_rule *rule;
	wl_list_for_each(rule, &server->rules, link) {
		if (rule->match.title_pattern && !rule->match.title_regex_compiled) {
			int rc = regcomp(&rule->match.title_regex,
				rule->match.title_pattern,
				REG_EXTENDED | REG_NOSUB | REG_ICASE);
			if (rc == 0) {
				rule->match.title_regex_compiled = true;
			} else {
				wlr_log(WLR_ERROR, "Invalid title regex: %s",
					rule->match.title_pattern);
			}
		}
	}
}

/* ─── Rule matching ────────────────────────────────────────────── */

static bool rule_matches(struct stw_rule *rule, struct stw_view *view) {
	const char *app_id = stw_view_get_app_id(view);
	const char *title = stw_view_get_title(view);

	/* app_id match (RULE-1: app ID / desktop file ID) */
	if (rule->match.app_id) {
		if (!app_id || strcmp(rule->match.app_id, app_id) != 0) {
			return false;
		}
	}

	/* class_name match (RULE-1: WM_CLASS for XWayland) */
	if (rule->match.class_name) {
		if (!app_id || strcmp(rule->match.class_name, app_id) != 0) {
			return false;
		}
	}

	/* title regex match (RULE-1: window title regex) */
	if (rule->match.title_regex_compiled) {
		if (!title || regexec(&rule->match.title_regex, title,
				0, NULL, 0) != 0) {
			return false;
		}
	}

	return true;
}

static void apply_rule_action(struct stw_rule *rule, struct stw_view *view) {
	struct stw_server *server = view->server;

	/* Task assignment (RULE-2) */
	if (rule->action.task_name) {
		struct stw_task *task = stw_task_find_by_name(server,
			rule->action.task_name);
		if (!task) {
			/* Create the task if it doesn't exist */
			task = stw_task_create(server, rule->action.task_name);
		}
		if (task) {
			stw_view_assign_task(view, task, STW_ASSIGN_RULE);
			wlr_log(WLR_DEBUG, "Rule: assigned '%s' to task '%s'",
				view->app_id ? view->app_id : "(null)",
				task->name);
		}
	} else if (rule->action.assign_current) {
		if (server->active_task) {
			stw_view_assign_task(view, server->active_task,
				STW_ASSIGN_RULE);
		}
	}

	/* Global flag (RULE-2) */
	if (rule->action.has_global) {
		stw_view_set_global(view, rule->action.set_global);
	}

	/* Floating/tiled (RULE-2) */
	if (rule->action.has_floating) {
		stw_view_set_floating(view, rule->action.set_floating);
	}

	/* Size (RULE-2) */
	if (rule->action.has_size) {
		stw_view_set_size(view, rule->action.width, rule->action.height);
	}

	/* Position (RULE-2) */
	if (rule->action.has_position) {
		stw_view_set_position(view, rule->action.x, rule->action.y);
	}

	/* Workspace (RULE-2) */
	if (rule->action.workspace >= 0) {
		view->workspace_idx = rule->action.workspace;
	}
}

bool stw_rules_apply(struct stw_server *server, struct stw_view *view) {
	/* Rule precedence (RULE-3):
	 * 1. explicit manual override (per-window) - checked via assignment_source
	 * 2. specific window rule (title match)
	 * 3. application rule (app_id match)
	 * 4. default behavior
	 */
	if (view->assignment_source == STW_ASSIGN_MANUAL) {
		return false; /* Manual override takes precedence */
	}

	bool matched = false;
	struct stw_rule *rule;
	wl_list_for_each(rule, &server->rules, link) {
		if (rule_matches(rule, view)) {
			apply_rule_action(rule, view);
			matched = true;
			wlr_log(WLR_DEBUG,
				"Rule matched for '%s' (title='%s')",
				view->app_id ? view->app_id : "(null)",
				view->title ? view->title : "(null)");
			break; /* First matching rule wins */
		}
	}

	return matched;
}

void stw_rules_trace(struct stw_server *server, struct stw_view *view) {
	wlr_log(WLR_INFO, "=== Rule trace for view ===");
	wlr_log(WLR_INFO, "  app_id: %s",
		view->app_id ? view->app_id : "(null)");
	wlr_log(WLR_INFO, "  title:  %s",
		view->title ? view->title : "(null)");
	wlr_log(WLR_INFO, "  pid:    %d", view->pid);

	int i = 0;
	struct stw_rule *rule;
	wl_list_for_each(rule, &server->rules, link) {
		bool match = rule_matches(rule, view);
		wlr_log(WLR_INFO, "  rule[%d]: %s (app_id=%s title=%s) => %s",
			i,
			match ? "MATCH" : "no match",
			rule->match.app_id ? rule->match.app_id : "*",
			rule->match.title_pattern ? rule->match.title_pattern : "*",
			match ? "APPLIED" : "skipped");
		i++;
	}
}
