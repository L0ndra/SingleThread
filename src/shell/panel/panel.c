/*
 * SingleThread - Task-Centric Wayland Compositor
 * panel.c - Top/bottom panel with task indicators, tray, clock
 *
 * This is a Wayland layer-shell surface using GTK4.
 * It communicates with the compositor via the IPC socket.
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

static int ipc_fd = -1;

static int ipc_connect(void) {
	const char *socket_path = getenv("SINGLETHREAD_SOCKET");
	if (!socket_path) return -1;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static char *ipc_request(const char *json_str) {
	int fd = ipc_connect();
	if (fd < 0) return NULL;

	uint32_t len = (uint32_t)strlen(json_str);
	write(fd, &len, 4);
	write(fd, json_str, len);

	uint32_t resp_len;
	if (read(fd, &resp_len, 4) != 4) {
		close(fd);
		return NULL;
	}

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
	GtkWidget *task_box;
	GtkWidget *title_label;
	GtkWidget *clock_label;
	GtkWidget *workspace_box;
} PanelState;

static PanelState panel = {0};

/* ─── Task buttons ─────────────────────────────────────────────── */

static void on_task_clicked(GtkButton *button, gpointer user_data) {
	int task_id = GPOINTER_TO_INT(user_data);
	char json[256];
	snprintf(json, sizeof(json),
		"{\"command\": \"task/switch\", \"id\": %d}", task_id);
	char *resp = ipc_request(json);
	free(resp);
}

static void update_tasks(void) {
	/* Clear existing buttons */
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(panel.task_box))) {
		gtk_box_remove(GTK_BOX(panel.task_box), child);
	}

	/* Fetch task list */
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
	for (int i = 0; i < len; i++) {
		struct json_object *task = json_object_array_get_idx(tasks_arr, i);
		struct json_object *name_obj, *id_obj, *active_obj, *archived_obj;

		json_object_object_get_ex(task, "name", &name_obj);
		json_object_object_get_ex(task, "id", &id_obj);
		json_object_object_get_ex(task, "active", &active_obj);
		json_object_object_get_ex(task, "archived", &archived_obj);

		if (archived_obj && json_object_get_boolean(archived_obj)) {
			continue;
		}

		const char *name = json_object_get_string(name_obj);
		int id = json_object_get_int(id_obj);
		gboolean active = active_obj ?
			json_object_get_boolean(active_obj) : FALSE;

		GtkWidget *btn = gtk_button_new_with_label(name);
		if (active) {
			gtk_widget_add_css_class(btn, "active-task");
		}
		gtk_widget_add_css_class(btn, "task-button");
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
		gtk_label_set_text(GTK_LABEL(panel.title_label), "");
		return;
	}

	struct json_object *root = json_tokener_parse(resp);
	free(resp);
	if (!root) return;

	struct json_object *window_obj;
	if (json_object_object_get_ex(root, "window", &window_obj)) {
		struct json_object *title_obj;
		if (json_object_object_get_ex(window_obj, "title", &title_obj)) {
			const char *title = json_object_get_string(title_obj);
			/* Truncate long titles */
			if (strlen(title) > 60) {
				char truncated[64];
				snprintf(truncated, sizeof(truncated),
					"%.57s...", title);
				gtk_label_set_text(GTK_LABEL(panel.title_label),
					truncated);
			} else {
				gtk_label_set_text(GTK_LABEL(panel.title_label),
					title);
			}
		}
	} else {
		gtk_label_set_text(GTK_LABEL(panel.title_label), "");
	}

	json_object_put(root);
}

/* ─── Clock ────────────────────────────────────────────────────── */

static gboolean update_clock(gpointer data) {
	(void)data;
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char buf[64];
	strftime(buf, sizeof(buf), "%H:%M", tm);
	gtk_label_set_text(GTK_LABEL(panel.clock_label), buf);
	return G_SOURCE_CONTINUE;
}

/* ─── Periodic refresh ─────────────────────────────────────────── */

static gboolean refresh_panel(gpointer data) {
	(void)data;
	update_tasks();
	update_title();
	return G_SOURCE_CONTINUE;
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *panel_css =
	"window {"
	"  background-color: #1a1b26;"
	"  color: #c0caf5;"
	"  font-family: monospace;"
	"  font-size: 13px;"
	"}"
	".task-button {"
	"  padding: 2px 10px;"
	"  margin: 2px 1px;"
	"  border-radius: 4px;"
	"  background-color: #24283b;"
	"  color: #a9b1d6;"
	"  border: none;"
	"  min-height: 0;"
	"}"
	".task-button:hover {"
	"  background-color: #343b58;"
	"}"
	".active-task {"
	"  background-color: #7aa2f7;"
	"  color: #1a1b26;"
	"  font-weight: bold;"
	"}"
	".title-label {"
	"  padding: 0 12px;"
	"  color: #787c99;"
	"}"
	".clock-label {"
	"  padding: 0 8px;"
	"  color: #7aa2f7;"
	"  font-weight: bold;"
	"}"
	".separator {"
	"  background-color: #3b4261;"
	"  min-width: 1px;"
	"  margin: 4px 4px;"
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
	gtk_layer_set_exclusive_zone(panel.window, 32);
	gtk_layer_set_namespace(panel.window, "stw-panel");

	/* Main layout */
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_start(hbox, 4);
	gtk_widget_set_margin_end(hbox, 4);

	/* Task buttons (left) */
	panel.task_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(hbox), panel.task_box);

	/* Separator */
	GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_widget_add_css_class(sep1, "separator");
	gtk_box_append(GTK_BOX(hbox), sep1);

	/* Window title (center, expanding) */
	panel.title_label = gtk_label_new("");
	gtk_widget_add_css_class(panel.title_label, "title-label");
	gtk_label_set_ellipsize(GTK_LABEL(panel.title_label),
		PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(panel.title_label, TRUE);
	gtk_label_set_xalign(GTK_LABEL(panel.title_label), 0.0);
	gtk_box_append(GTK_BOX(hbox), panel.title_label);

	/* Separator */
	GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_widget_add_css_class(sep2, "separator");
	gtk_box_append(GTK_BOX(hbox), sep2);

	/* Clock (right) */
	panel.clock_label = gtk_label_new("");
	gtk_widget_add_css_class(panel.clock_label, "clock-label");
	gtk_box_append(GTK_BOX(hbox), panel.clock_label);

	gtk_window_set_child(panel.window, hbox);

	/* Initial update */
	update_tasks();
	update_title();
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
