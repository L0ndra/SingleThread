/*
 * SingleThread - Task-Centric Wayland Compositor
 * focus_mode.h - Focus mode (DND) and break reminders
 *
 * ADHD-friendly features:
 *   - Focus mode: suppress notifications, dim non-active windows
 *   - Break reminders: gentle periodic nudges to take breaks
 *   - Task timer: track how long on each task
 *   - Per-task app blocklist (future)
 */
#ifndef STW_FOCUS_MODE_H
#define STW_FOCUS_MODE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>

struct stw_server;

/* ─── Focus mode ───────────────────────────────────────────────── */

struct stw_focus_mode {
	bool active;

	/* Focus timer */
	time_t started_at;          /* when focus mode was activated */
	int duration_minutes;       /* 0 = indefinite */
	struct wl_event_source *end_timer; /* auto-end timer */

	/* Break reminders */
	bool break_reminders;
	int break_interval_minutes; /* default: 25 (Pomodoro) */
	int break_duration_minutes; /* default: 5 */
	struct wl_event_source *break_timer;
	bool on_break;

	/* Visual settings during focus */
	float dim_amount;           /* how much to dim inactive windows */
	bool suppress_notifications;
};

/* Focus mode API */
void stw_focus_mode_init(struct stw_server *server);
void stw_focus_mode_finish(struct stw_server *server);
void stw_focus_mode_toggle(struct stw_server *server);
void stw_focus_mode_start(struct stw_server *server, int duration_minutes);
void stw_focus_mode_stop(struct stw_server *server);
bool stw_focus_mode_is_active(struct stw_server *server);
int stw_focus_mode_elapsed_minutes(struct stw_server *server);

/* Break reminders */
void stw_focus_mode_set_breaks(struct stw_server *server,
	int interval_minutes, int duration_minutes);
void stw_focus_mode_dismiss_break(struct stw_server *server);

/* ─── Task timer ───────────────────────────────────────────────── */
/* Tracks cumulative time spent on each task (per session) */

struct stw_task_timer {
	time_t switch_in_time;     /* when we switched TO this task */
	uint64_t total_seconds;    /* cumulative time spent on this task */
};

void stw_task_timer_start(struct stw_task_timer *timer);
void stw_task_timer_pause(struct stw_task_timer *timer);
uint64_t stw_task_timer_get_seconds(struct stw_task_timer *timer);

/* ─── Breadcrumb trail (task switch history) ───────────────────── */

#define STW_BREADCRUMB_MAX 32

struct stw_breadcrumb {
	uint32_t task_id;
	char task_name[64];
	time_t timestamp;
};

struct stw_breadcrumb_trail {
	struct stw_breadcrumb entries[STW_BREADCRUMB_MAX];
	int count;
	int head; /* circular buffer index */
};

void stw_breadcrumb_init(struct stw_breadcrumb_trail *trail);
void stw_breadcrumb_record(struct stw_breadcrumb_trail *trail,
	uint32_t task_id, const char *task_name);
int stw_breadcrumb_get_recent(struct stw_breadcrumb_trail *trail,
	struct stw_breadcrumb *out, int max);

/* ─── Quick notes (per-task) ───────────────────────────────────── */

#define STW_NOTE_MAX_LEN 4096

struct stw_quick_note {
	struct wl_list link;    /* stw_task.notes */
	char *text;
	time_t created_at;
};

#endif /* STW_FOCUS_MODE_H */
