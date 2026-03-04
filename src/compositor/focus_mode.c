/*
 * SingleThread - Task-Centric Wayland Compositor
 * focus_mode.c - Focus mode, break reminders, task timer, breadcrumb trail
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/util/log.h>

#include "focus_mode.h"
#include "server.h"
#include "task.h"
#include "ipc.h"

/* ─── Focus mode timer callbacks ───────────────────────────────── */

static int focus_mode_end_callback(void *data) {
	struct stw_server *server = data;
	wlr_log(WLR_INFO, "Focus mode timer expired");
	stw_focus_mode_stop(server);
	return 0;
}

static int break_reminder_callback(void *data) {
	struct stw_server *server = data;
	struct stw_focus_mode *fm = &server->focus_mode;

	if (!fm->active) return 0;

	fm->on_break = true;
	wlr_log(WLR_INFO, "Break reminder: time to take a %d-minute break!",
		fm->break_duration_minutes);

	/* Notify shell components via IPC broadcast */
	/* Shell panel will show break indicator */

	/* Schedule end of break */
	if (fm->break_timer) {
		wl_event_source_timer_update(fm->break_timer,
			fm->break_duration_minutes * 60 * 1000);
	}

	return 0;
}

static int break_end_callback(void *data) {
	struct stw_server *server = data;
	struct stw_focus_mode *fm = &server->focus_mode;

	if (!fm->active) return 0;

	fm->on_break = false;
	wlr_log(WLR_INFO, "Break over, back to focus");

	/* Restart the break interval timer */
	if (fm->break_timer) {
		wl_event_source_timer_update(fm->break_timer,
			fm->break_interval_minutes * 60 * 1000);
	}

	return 0;
}

/* ─── Focus mode API ───────────────────────────────────────────── */

void stw_focus_mode_init(struct stw_server *server) {
	struct stw_focus_mode *fm = &server->focus_mode;
	memset(fm, 0, sizeof(*fm));
	fm->dim_amount = 0.3f;
	fm->suppress_notifications = true;
	fm->break_interval_minutes = 25;
	fm->break_duration_minutes = 5;
	fm->break_reminders = true;

	/* Initialize breadcrumb trail */
	stw_breadcrumb_init(&server->breadcrumbs);
}

void stw_focus_mode_finish(struct stw_server *server) {
	struct stw_focus_mode *fm = &server->focus_mode;
	if (fm->end_timer) {
		wl_event_source_remove(fm->end_timer);
		fm->end_timer = NULL;
	}
	if (fm->break_timer) {
		wl_event_source_remove(fm->break_timer);
		fm->break_timer = NULL;
	}
}

void stw_focus_mode_toggle(struct stw_server *server) {
	if (server->focus_mode.active) {
		stw_focus_mode_stop(server);
	} else {
		stw_focus_mode_start(server, 0);
	}
}

void stw_focus_mode_start(struct stw_server *server, int duration_minutes) {
	struct stw_focus_mode *fm = &server->focus_mode;

	if (fm->active) return;

	fm->active = true;
	fm->started_at = time(NULL);
	fm->duration_minutes = duration_minutes;
	fm->on_break = false;

	wlr_log(WLR_INFO, "Focus mode started%s",
		duration_minutes > 0 ? " (timed)" : " (indefinite)");

	/* Set up auto-end timer if duration specified */
	if (duration_minutes > 0) {
		fm->end_timer = wl_event_loop_add_timer(
			server->wl_event_loop, focus_mode_end_callback, server);
		wl_event_source_timer_update(fm->end_timer,
			duration_minutes * 60 * 1000);
	}

	/* Set up break reminder timer */
	if (fm->break_reminders) {
		fm->break_timer = wl_event_loop_add_timer(
			server->wl_event_loop, break_reminder_callback, server);
		wl_event_source_timer_update(fm->break_timer,
			fm->break_interval_minutes * 60 * 1000);
	}
}

void stw_focus_mode_stop(struct stw_server *server) {
	struct stw_focus_mode *fm = &server->focus_mode;

	if (!fm->active) return;

	fm->active = false;
	fm->on_break = false;

	if (fm->end_timer) {
		wl_event_source_remove(fm->end_timer);
		fm->end_timer = NULL;
	}
	if (fm->break_timer) {
		wl_event_source_remove(fm->break_timer);
		fm->break_timer = NULL;
	}

	wlr_log(WLR_INFO, "Focus mode stopped (was active for %d minutes)",
		stw_focus_mode_elapsed_minutes(server));
}

bool stw_focus_mode_is_active(struct stw_server *server) {
	return server->focus_mode.active;
}

int stw_focus_mode_elapsed_minutes(struct stw_server *server) {
	struct stw_focus_mode *fm = &server->focus_mode;
	if (!fm->active || fm->started_at == 0) return 0;
	return (int)(difftime(time(NULL), fm->started_at) / 60.0);
}

void stw_focus_mode_set_breaks(struct stw_server *server,
		int interval_minutes, int duration_minutes) {
	struct stw_focus_mode *fm = &server->focus_mode;
	fm->break_interval_minutes = interval_minutes > 0 ? interval_minutes : 25;
	fm->break_duration_minutes = duration_minutes > 0 ? duration_minutes : 5;

	/* If currently in focus mode, restart break timer */
	if (fm->active && fm->break_timer) {
		wl_event_source_timer_update(fm->break_timer,
			fm->break_interval_minutes * 60 * 1000);
	}
}

void stw_focus_mode_dismiss_break(struct stw_server *server) {
	struct stw_focus_mode *fm = &server->focus_mode;
	if (!fm->on_break) return;

	fm->on_break = false;
	wlr_log(WLR_DEBUG, "Break dismissed early");

	/* Restart break interval */
	if (fm->break_timer) {
		wl_event_source_timer_update(fm->break_timer,
			fm->break_interval_minutes * 60 * 1000);
	}
}

/* ─── Task timer ───────────────────────────────────────────────── */

void stw_task_timer_start(struct stw_task_timer *timer) {
	timer->switch_in_time = time(NULL);
}

void stw_task_timer_pause(struct stw_task_timer *timer) {
	if (timer->switch_in_time > 0) {
		timer->total_seconds += (uint64_t)difftime(time(NULL),
			timer->switch_in_time);
		timer->switch_in_time = 0;
	}
}

uint64_t stw_task_timer_get_seconds(struct stw_task_timer *timer) {
	uint64_t total = timer->total_seconds;
	if (timer->switch_in_time > 0) {
		total += (uint64_t)difftime(time(NULL), timer->switch_in_time);
	}
	return total;
}

/* ─── Breadcrumb trail ─────────────────────────────────────────── */

void stw_breadcrumb_init(struct stw_breadcrumb_trail *trail) {
	memset(trail, 0, sizeof(*trail));
}

void stw_breadcrumb_record(struct stw_breadcrumb_trail *trail,
		uint32_t task_id, const char *task_name) {
	int idx = trail->head;
	trail->entries[idx].task_id = task_id;
	snprintf(trail->entries[idx].task_name,
		sizeof(trail->entries[idx].task_name), "%s", task_name);
	trail->entries[idx].timestamp = time(NULL);

	trail->head = (trail->head + 1) % STW_BREADCRUMB_MAX;
	if (trail->count < STW_BREADCRUMB_MAX) {
		trail->count++;
	}
}

int stw_breadcrumb_get_recent(struct stw_breadcrumb_trail *trail,
		struct stw_breadcrumb *out, int max) {
	int n = trail->count < max ? trail->count : max;
	for (int i = 0; i < n; i++) {
		int idx = (trail->head - 1 - i + STW_BREADCRUMB_MAX)
			% STW_BREADCRUMB_MAX;
		out[i] = trail->entries[idx];
	}
	return n;
}
