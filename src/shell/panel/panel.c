/*
 * SingleThread - Task-Centric Wayland Compositor
 * panel.c - Polished top panel with task pills, workspace dots,
 *           system tray area, date/clock, window title
 *
 * Design: glassmorphism-inspired bar with smooth transitions,
 * proportional fonts, icon integration, and visual hierarchy.
 */
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ─── IPC client ───────────────────────────────────────────────── */

static char *ipc_request(const char *json_str) {
	const char *socket_path = getenv("SINGLETHREAD_SOCKET");
	if (!socket_path) return NULL;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return NULL;

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return NULL;
	}

	uint32_t len = (uint32_t)strlen(json_str);
	write(fd, &len, 4);
	write(fd, json_str, len);

	uint32_t resp_len;
	if (read(fd, &resp_len, 4) != 4) { close(fd); return NULL; }

	char *buf = malloc(resp_len + 1);
	if (!buf) { close(fd); return NULL; }

	size_t total = 0;
	while (total < resp_len) {
		ssize_t n = read(fd, buf + total, resp_len - total);
		if (n <= 0) { free(buf); close(fd); return NULL; }
		total += n;
	}
	buf[resp_len] = '\0';
	close(fd);
	return buf;
}

/* ─── Panel state ──────────────────────────────────────────────── */

typedef struct {
	GtkWindow *window;

	/* Left section */
	GtkWidget *logo_label;
	GtkWidget *task_box;

	/* Center section */
	GtkWidget *title_icon;
	GtkWidget *title_label;

	/* Right section */
	GtkWidget *workspace_box;
	GtkWidget *separator_tray;
	GtkWidget *tray_box;
	GtkWidget *date_label;
	GtkWidget *clock_label;
} PanelState;

static PanelState panel = {0};

/* ─── Task buttons ─────────────────────────────────────────────── */

static void on_task_clicked(GtkButton *button, gpointer user_data) {
	(void)button;
	int task_id = GPOINTER_TO_INT(user_data);
	char json[256];
	snprintf(json, sizeof(json),
		"{\"command\": \"task/switch\", \"id\": %d}", task_id);
	char *resp = ipc_request(json);
	free(resp);
}

static void update_tasks(void) {
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(panel.task_box))) {
		gtk_box_remove(GTK_BOX(panel.task_box), child);
	}

	char *resp = ipc_request("{\"command\": \"task/list\"}");
	if (!resp) return;

	struct json_object *root = json_tokener_parse(resp);
	free(resp);
	if (!root) return;

	struct json_object *tasks_arr;
	if (!json_object_object_get_ex(root, "tasks", &tasks_arr)) {
		json_object_put(root);
		return;
	}

	int len = json_object_array_length(tasks_arr);
	int task_num = 0;
	for (int i = 0; i < len; i++) {
		struct json_object *task = json_object_array_get_idx(tasks_arr, i);
		struct json_object *name_obj, *id_obj, *active_obj,
			*archived_obj, *wcount_obj;

		json_object_object_get_ex(task, "name", &name_obj);
		json_object_object_get_ex(task, "id", &id_obj);
		json_object_object_get_ex(task, "active", &active_obj);
		json_object_object_get_ex(task, "archived", &archived_obj);
		json_object_object_get_ex(task, "window_count", &wcount_obj);

		if (archived_obj && json_object_get_boolean(archived_obj))
			continue;

		task_num++;
		const char *name = json_object_get_string(name_obj);
		int id = json_object_get_int(id_obj);
		int wcount = wcount_obj ? json_object_get_int(wcount_obj) : 0;
		gboolean active = active_obj ?
			json_object_get_boolean(active_obj) : FALSE;

		/* Build task pill: [number icon] name [window count badge] */
		GtkWidget *btn = gtk_button_new();
		GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

		/* Keyboard shortcut number */
		char num_str[8];
		snprintf(num_str, sizeof(num_str), "%d", task_num);
		GtkWidget *num_label = gtk_label_new(num_str);
		gtk_widget_add_css_class(num_label, "task-num");
		gtk_box_append(GTK_BOX(btn_box), num_label);

		/* Task name */
		GtkWidget *name_label = gtk_label_new(name);
		gtk_widget_add_css_class(name_label, "task-name-label");
		gtk_box_append(GTK_BOX(btn_box), name_label);

		/* Window count badge (only if > 0) */
		if (wcount > 0) {
			char badge_str[16];
			snprintf(badge_str, sizeof(badge_str), "%d", wcount);
			GtkWidget *badge = gtk_label_new(badge_str);
			gtk_widget_add_css_class(badge, "task-badge");
			gtk_box_append(GTK_BOX(btn_box), badge);
		}

		gtk_button_set_child(GTK_BUTTON(btn), btn_box);

		gtk_widget_add_css_class(btn, "task-pill");
		if (active) {
			gtk_widget_add_css_class(btn, "task-active");
		}

		g_signal_connect(btn, "clicked",
			G_CALLBACK(on_task_clicked), GINT_TO_POINTER(id));
		gtk_box_append(GTK_BOX(panel.task_box), btn);
	}

	json_object_put(root);
}

/* ─── Active window title ──────────────────────────────────────── */

static void update_title(void) {
	char *resp = ipc_request("{\"command\": \"get/active-window\"}");
	if (!resp) {
		gtk_label_set_text(GTK_LABEL(panel.title_label), "SingleThread");
		gtk_widget_add_css_class(panel.title_label, "title-placeholder");
		return;
	}

	struct json_object *root = json_tokener_parse(resp);
	free(resp);
	if (!root) return;

	struct json_object *window_obj;
	if (json_object_object_get_ex(root, "window", &window_obj)) {
		struct json_object *title_obj, *app_id_obj, *global_obj;
		json_object_object_get_ex(window_obj, "title", &title_obj);
		json_object_object_get_ex(window_obj, "app_id", &app_id_obj);
		json_object_object_get_ex(window_obj, "global", &global_obj);

		const char *title = title_obj ?
			json_object_get_string(title_obj) : "";
		gboolean is_global = global_obj ?
			json_object_get_boolean(global_obj) : FALSE;

		gtk_widget_remove_css_class(panel.title_label,
			"title-placeholder");

		/* Show global indicator */
		if (is_global) {
			char decorated[256];
			snprintf(decorated, sizeof(decorated),
				"\342\214\220  %s", title); /* pin icon */
			gtk_label_set_text(GTK_LABEL(panel.title_label),
				decorated);
		} else {
			gtk_label_set_text(GTK_LABEL(panel.title_label), title);
		}
	} else {
		gtk_label_set_text(GTK_LABEL(panel.title_label), "SingleThread");
		gtk_widget_add_css_class(panel.title_label, "title-placeholder");
	}

	json_object_put(root);
}

/* ─── Workspace dots ───────────────────────────────────────────── */

static void update_workspaces(void) {
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(panel.workspace_box))) {
		gtk_box_remove(GTK_BOX(panel.workspace_box), child);
	}

	/* Get active task workspace info */
	char *resp = ipc_request("{\"command\": \"get/active-task\"}");
	if (!resp) return;

	struct json_object *root = json_tokener_parse(resp);
	free(resp);
	if (!root) return;

	struct json_object *task_obj;
	if (json_object_object_get_ex(root, "task", &task_obj)) {
		struct json_object *active_ws_obj;
		int active_ws = 0;
		if (json_object_object_get_ex(task_obj, "active_workspace",
				&active_ws_obj)) {
			active_ws = json_object_get_int(active_ws_obj);
		}

		/* Create workspace dots (default 3) */
		for (int i = 0; i < 3; i++) {
			GtkWidget *dot = gtk_label_new(NULL);
			gtk_widget_add_css_class(dot, "ws-dot");
			if (i == active_ws) {
				gtk_widget_add_css_class(dot, "ws-active");
			}
			gtk_box_append(GTK_BOX(panel.workspace_box), dot);
		}
	}

	json_object_put(root);
}

/* ─── Clock & Date ─────────────────────────────────────────────── */

static gboolean update_clock(gpointer data) {
	(void)data;
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);

	char time_buf[16];
	strftime(time_buf, sizeof(time_buf), "%H:%M", tm);
	gtk_label_set_text(GTK_LABEL(panel.clock_label), time_buf);

	char date_buf[32];
	strftime(date_buf, sizeof(date_buf), "%a %b %d", tm);
	gtk_label_set_text(GTK_LABEL(panel.date_label), date_buf);

	return G_SOURCE_CONTINUE;
}

/* ─── Tray indicators ──────────────────────────────────────────── */

static void build_tray(void) {
	/* Static system indicators using Unicode symbols */
	const char *tray_items[] = {
		"\342\227\211",   /* filled circle - network */
		"\342\231\252",   /* music note - audio */
		"\342\226\256",   /* battery-like */
		NULL
	};

	for (int i = 0; tray_items[i]; i++) {
		GtkWidget *icon = gtk_label_new(tray_items[i]);
		gtk_widget_add_css_class(icon, "tray-icon");
		gtk_box_append(GTK_BOX(panel.tray_box), icon);
	}
}

/* ─── Periodic refresh ─────────────────────────────────────────── */

static gboolean refresh_panel(gpointer data) {
	(void)data;
	update_tasks();
	update_title();
	update_workspaces();
	return G_SOURCE_CONTINUE;
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *panel_css =
	/* ── Global window ───────────────────────────────────────── */
	"window {"
	"  background-color: rgba(22, 22, 30, 0.92);"
	"  border-bottom: 1px solid rgba(122, 162, 247, 0.15);"
	"  color: #c0caf5;"
	"  font-family: 'Inter', 'Cantarell', 'Noto Sans', sans-serif;"
	"  font-size: 13px;"
	"}"

	/* ── Logo ────────────────────────────────────────────────── */
	".panel-logo {"
	"  font-size: 15px;"
	"  font-weight: 800;"
	"  color: #7aa2f7;"
	"  padding: 0 12px 0 8px;"
	"  letter-spacing: -0.5px;"
	"}"

	/* ── Task pills ──────────────────────────────────────────── */
	".task-pill {"
	"  padding: 2px 10px;"
	"  margin: 4px 2px;"
	"  border-radius: 8px;"
	"  border: 1px solid transparent;"
	"  background-color: rgba(36, 40, 59, 0.6);"
	"  color: #8c8fa8;"
	"  min-height: 0;"
	"  transition: all 200ms ease;"
	"}"
	".task-pill:hover {"
	"  background-color: rgba(52, 59, 88, 0.8);"
	"  border-color: rgba(122, 162, 247, 0.2);"
	"  color: #c0caf5;"
	"}"
	".task-active {"
	"  background: linear-gradient(135deg, "
	"    rgba(122, 162, 247, 0.25), rgba(125, 174, 255, 0.15));"
	"  border-color: rgba(122, 162, 247, 0.4);"
	"  color: #c0caf5;"
	"  font-weight: 600;"
	"}"
	".task-active:hover {"
	"  background: linear-gradient(135deg, "
	"    rgba(122, 162, 247, 0.35), rgba(125, 174, 255, 0.25));"
	"  border-color: rgba(122, 162, 247, 0.6);"
	"}"

	/* ── Task pill inner elements ────────────────────────────── */
	".task-num {"
	"  font-size: 10px;"
	"  font-weight: 700;"
	"  color: rgba(122, 162, 247, 0.5);"
	"  min-width: 12px;"
	"  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
	"}"
	".task-active .task-num {"
	"  color: #7aa2f7;"
	"}"
	".task-name-label {"
	"  font-size: 12.5px;"
	"}"
	".task-badge {"
	"  font-size: 9px;"
	"  font-weight: 700;"
	"  background-color: rgba(122, 162, 247, 0.2);"
	"  color: #7aa2f7;"
	"  border-radius: 6px;"
	"  padding: 0 5px;"
	"  min-height: 14px;"
	"  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
	"}"

	/* ── Separators ──────────────────────────────────────────── */
	".panel-sep {"
	"  background-color: rgba(86, 95, 137, 0.3);"
	"  min-width: 1px;"
	"  margin: 7px 8px;"
	"}"

	/* ── Window title (center) ───────────────────────────────── */
	".title-label {"
	"  padding: 0 16px;"
	"  color: #a9b1d6;"
	"  font-size: 12.5px;"
	"  font-weight: 500;"
	"}"
	".title-placeholder {"
	"  color: rgba(86, 95, 137, 0.6);"
	"  font-style: italic;"
	"  font-weight: 400;"
	"}"

	/* ── Workspace dots ──────────────────────────────────────── */
	".ws-dot {"
	"  min-width: 6px;"
	"  min-height: 6px;"
	"  border-radius: 3px;"
	"  background-color: rgba(86, 95, 137, 0.4);"
	"  margin: 0 2px;"
	"  transition: all 200ms ease;"
	"}"
	".ws-active {"
	"  min-width: 18px;"
	"  background-color: #7aa2f7;"
	"  border-radius: 3px;"
	"}"

	/* ── System tray ─────────────────────────────────────────── */
	".tray-icon {"
	"  font-size: 13px;"
	"  color: rgba(169, 177, 214, 0.6);"
	"  padding: 0 4px;"
	"  transition: color 200ms ease;"
	"}"
	".tray-icon:hover {"
	"  color: #c0caf5;"
	"}"

	/* ── Date & Clock ────────────────────────────────────────── */
	".date-label {"
	"  font-size: 11.5px;"
	"  color: rgba(169, 177, 214, 0.5);"
	"  padding-right: 6px;"
	"  font-weight: 400;"
	"}"
	".clock-label {"
	"  font-size: 13px;"
	"  color: #c0caf5;"
	"  font-weight: 600;"
	"  padding: 0 8px 0 0;"
	"  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
	"  letter-spacing: 0.5px;"
	"}";

/* ─── Activation ───────────────────────────────────────────────── */

static void activate(GtkApplication *app, gpointer data) {
	(void)data;

	/* Load CSS */
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, panel_css);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	/* Create window */
	panel.window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_title(panel.window, "stw-panel");

	/* Layer shell setup */
	gtk_layer_init_for_window(panel.window);
	gtk_layer_set_layer(panel.window, GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_anchor(panel.window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(panel.window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(panel.window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_margin(panel.window, GTK_LAYER_SHELL_EDGE_TOP, 0);
	gtk_layer_set_exclusive_zone(panel.window, 36);
	gtk_layer_set_namespace(panel.window, "stw-panel");

	/* ─── Main horizontal layout ───────────────────────────── */
	GtkWidget *main_box = gtk_center_box_new();
	gtk_widget_set_margin_start(main_box, 4);
	gtk_widget_set_margin_end(main_box, 4);

	/* ─── LEFT: Logo + Tasks ───────────────────────────────── */
	GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(left_box, GTK_ALIGN_CENTER);

	/* Logo */
	panel.logo_label = gtk_label_new("ST");
	gtk_widget_add_css_class(panel.logo_label, "panel-logo");
	gtk_box_append(GTK_BOX(left_box), panel.logo_label);

	/* Separator after logo */
	GtkWidget *sep_logo = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_widget_add_css_class(sep_logo, "panel-sep");
	gtk_box_append(GTK_BOX(left_box), sep_logo);

	/* Task pills */
	panel.task_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(panel.task_box, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(left_box), panel.task_box);

	gtk_center_box_set_start_widget(GTK_CENTER_BOX(main_box), left_box);

	/* ─── CENTER: Window title ─────────────────────────────── */
	GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_valign(center_box, GTK_ALIGN_CENTER);
	gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);

	panel.title_label = gtk_label_new("SingleThread");
	gtk_widget_add_css_class(panel.title_label, "title-label");
	gtk_widget_add_css_class(panel.title_label, "title-placeholder");
	gtk_label_set_ellipsize(GTK_LABEL(panel.title_label),
		PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(panel.title_label), 50);
	gtk_box_append(GTK_BOX(center_box), panel.title_label);

	gtk_center_box_set_center_widget(GTK_CENTER_BOX(main_box), center_box);

	/* ─── RIGHT: Workspaces + Tray + Clock ─────────────────── */
	GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(right_box, GTK_ALIGN_CENTER);

	/* Workspace indicator dots */
	panel.workspace_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_valign(panel.workspace_box, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_end(panel.workspace_box, 4);
	gtk_box_append(GTK_BOX(right_box), panel.workspace_box);

	/* Separator */
	GtkWidget *sep_ws = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_widget_add_css_class(sep_ws, "panel-sep");
	gtk_box_append(GTK_BOX(right_box), sep_ws);

	/* System tray icons */
	panel.tray_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(panel.tray_box, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(right_box), panel.tray_box);
	build_tray();

	/* Separator */
	GtkWidget *sep_tray = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_widget_add_css_class(sep_tray, "panel-sep");
	gtk_box_append(GTK_BOX(right_box), sep_tray);

	/* Date */
	panel.date_label = gtk_label_new("");
	gtk_widget_add_css_class(panel.date_label, "date-label");
	gtk_box_append(GTK_BOX(right_box), panel.date_label);

	/* Clock */
	panel.clock_label = gtk_label_new("");
	gtk_widget_add_css_class(panel.clock_label, "clock-label");
	gtk_box_append(GTK_BOX(right_box), panel.clock_label);

	gtk_center_box_set_end_widget(GTK_CENTER_BOX(main_box), right_box);

	gtk_window_set_child(panel.window, main_box);

	/* Initial update */
	update_tasks();
	update_title();
	update_workspaces();
	update_clock(NULL);

	/* Timers */
	g_timeout_add(1000, update_clock, NULL);
	g_timeout_add(500, refresh_panel, NULL);

	gtk_window_present(panel.window);
}

int main(int argc, char *argv[]) {
	GtkApplication *app = gtk_application_new(
		"org.singlethread.panel", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
