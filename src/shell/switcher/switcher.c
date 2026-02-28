/*
 * SingleThread - Task-Centric Wayland Compositor
 * switcher.c - Task switcher overlay (keyboard-driven with search)
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
#include <unistd.h>

/* ─── IPC helpers ──────────────────────────────────────────────── */

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

/* ─── Task data ────────────────────────────────────────────────── */

typedef struct {
	int id;
	char *name;
	gboolean active;
	int window_count;
} TaskInfo;

static GList *task_list = NULL;
static GList *filtered_tasks = NULL;

static void free_task_list(void) {
	for (GList *l = task_list; l; l = l->next) {
		TaskInfo *ti = l->data;
		g_free(ti->name);
		g_free(ti);
	}
	g_list_free(task_list);
	task_list = NULL;
	g_list_free(filtered_tasks);
	filtered_tasks = NULL;
}

static void load_tasks(void) {
	free_task_list();

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
		struct json_object *task_obj =
			json_object_array_get_idx(tasks_arr, i);
		struct json_object *tmp;

		TaskInfo *ti = g_new0(TaskInfo, 1);
		if (json_object_object_get_ex(task_obj, "id", &tmp))
			ti->id = json_object_get_int(tmp);
		if (json_object_object_get_ex(task_obj, "name", &tmp))
			ti->name = g_strdup(json_object_get_string(tmp));
		if (json_object_object_get_ex(task_obj, "active", &tmp))
			ti->active = json_object_get_boolean(tmp);
		if (json_object_object_get_ex(task_obj, "window_count", &tmp))
			ti->window_count = json_object_get_int(tmp);

		struct json_object *archived;
		if (json_object_object_get_ex(task_obj, "archived", &archived) &&
				json_object_get_boolean(archived)) {
			g_free(ti->name);
			g_free(ti);
			continue;
		}

		task_list = g_list_append(task_list, ti);
	}

	json_object_put(root);
}

/* ─── Switcher UI ──────────────────────────────────────────────── */

typedef struct {
	GtkWindow *window;
	GtkWidget *search_entry;
	GtkWidget *task_list_box;
} SwitcherState;

static SwitcherState switcher = {0};

static void switch_to_task(int task_id) {
	char json[256];
	snprintf(json, sizeof(json),
		"{\"command\": \"task/switch\", \"id\": %d}", task_id);
	char *resp = ipc_request(json);
	free(resp);
	gtk_window_close(switcher.window);
}

static void on_task_row_activated(GtkListBox *box, GtkListBoxRow *row,
		gpointer data) {
	(void)box;
	(void)data;
	int idx = gtk_list_box_row_get_index(row);
	GList *item = g_list_nth(filtered_tasks, idx);
	if (item) {
		TaskInfo *ti = item->data;
		switch_to_task(ti->id);
	}
}

static void update_task_list(void) {
	/* Remove all children */
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(
			GTK_WIDGET(switcher.task_list_box)))) {
		gtk_list_box_remove(GTK_LIST_BOX(switcher.task_list_box), child);
	}

	g_list_free(filtered_tasks);
	filtered_tasks = NULL;

	const char *query = gtk_editable_get_text(
		GTK_EDITABLE(switcher.search_entry));

	for (GList *l = task_list; l; l = l->next) {
		TaskInfo *ti = l->data;

		/* Filter */
		if (query && *query) {
			char *name_lower = g_utf8_strdown(ti->name, -1);
			char *query_lower = g_utf8_strdown(query, -1);
			gboolean match = (strstr(name_lower, query_lower) != NULL);
			g_free(name_lower);
			g_free(query_lower);
			if (!match) continue;
		}

		filtered_tasks = g_list_append(filtered_tasks, ti);

		/* Create row */
		GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_margin_start(hbox, 8);
		gtk_widget_set_margin_end(hbox, 8);
		gtk_widget_set_margin_top(hbox, 6);
		gtk_widget_set_margin_bottom(hbox, 6);

		/* Task number */
		int idx = g_list_length(filtered_tasks);
		char num_str[8];
		snprintf(num_str, sizeof(num_str), "%d", idx);
		GtkWidget *num = gtk_label_new(num_str);
		gtk_widget_add_css_class(num, "task-num");
		gtk_box_append(GTK_BOX(hbox), num);

		/* Task name */
		GtkWidget *name = gtk_label_new(ti->name);
		gtk_widget_set_hexpand(name, TRUE);
		gtk_label_set_xalign(GTK_LABEL(name), 0.0);
		if (ti->active) {
			gtk_widget_add_css_class(name, "active-task-name");
		}
		gtk_box_append(GTK_BOX(hbox), name);

		/* Window count */
		char count_str[16];
		snprintf(count_str, sizeof(count_str), "%d win", ti->window_count);
		GtkWidget *count = gtk_label_new(count_str);
		gtk_widget_add_css_class(count, "window-count");
		gtk_box_append(GTK_BOX(hbox), count);

		gtk_list_box_append(GTK_LIST_BOX(switcher.task_list_box), hbox);
	}
}

static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
	(void)entry;
	(void)data;
	update_task_list();
}

static gboolean on_key_pressed(GtkEventControllerKey *controller,
		guint keyval, guint keycode, GdkModifierType state,
		gpointer data) {
	(void)controller; (void)keycode; (void)state; (void)data;

	if (keyval == GDK_KEY_Escape) {
		gtk_window_close(switcher.window);
		return TRUE;
	}

	if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
		if (filtered_tasks) {
			TaskInfo *ti = filtered_tasks->data;
			switch_to_task(ti->id);
		}
		return TRUE;
	}

	/* Number keys 1-9 for quick select */
	if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
		int idx = keyval - GDK_KEY_1;
		GList *item = g_list_nth(filtered_tasks, idx);
		if (item) {
			TaskInfo *ti = item->data;
			switch_to_task(ti->id);
			return TRUE;
		}
	}

	return FALSE;
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *switcher_css =
	"window {"
	"  background-color: rgba(26, 27, 38, 0.95);"
	"  color: #c0caf5;"
	"  font-family: monospace;"
	"}"
	".switcher-search {"
	"  font-size: 16px;"
	"  padding: 10px;"
	"  background-color: #24283b;"
	"  color: #c0caf5;"
	"  border: 2px solid #7aa2f7;"
	"  border-radius: 8px;"
	"  margin: 8px;"
	"}"
	".task-num {"
	"  color: #7aa2f7;"
	"  font-weight: bold;"
	"  min-width: 20px;"
	"}"
	".active-task-name {"
	"  color: #7aa2f7;"
	"  font-weight: bold;"
	"}"
	".window-count {"
	"  color: #565f89;"
	"  font-size: 12px;"
	"}"
	"listbox row:selected {"
	"  background-color: #343b58;"
	"  border-radius: 4px;"
	"}";

/* ─── Activation ───────────────────────────────────────────────── */

static void activate(GtkApplication *app, gpointer data) {
	(void)data;

	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, switcher_css);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	load_tasks();

	switcher.window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_title(switcher.window, "stw-switcher");
	gtk_window_set_default_size(switcher.window, 400, 300);

	gtk_layer_init_for_window(switcher.window);
	gtk_layer_set_layer(switcher.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_keyboard_mode(switcher.window,
		GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_layer_set_namespace(switcher.window, "stw-switcher");

	GtkEventController *key_ctrl = gtk_event_controller_key_new();
	g_signal_connect(key_ctrl, "key-pressed",
		G_CALLBACK(on_key_pressed), NULL);
	gtk_widget_add_controller(GTK_WIDGET(switcher.window), key_ctrl);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	switcher.search_entry = gtk_search_entry_new();
	gtk_widget_add_css_class(switcher.search_entry, "switcher-search");
	g_signal_connect(switcher.search_entry, "search-changed",
		G_CALLBACK(on_search_changed), NULL);
	gtk_box_append(GTK_BOX(vbox), switcher.search_entry);

	switcher.task_list_box = gtk_list_box_new();
	g_signal_connect(switcher.task_list_box, "row-activated",
		G_CALLBACK(on_task_row_activated), NULL);

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
		switcher.task_list_box);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_append(GTK_BOX(vbox), scroll);

	gtk_window_set_child(switcher.window, vbox);

	update_task_list();
	gtk_widget_grab_focus(switcher.search_entry);
	gtk_window_present(switcher.window);
}

int main(int argc, char *argv[]) {
	GtkApplication *app = gtk_application_new(
		"org.singlethread.switcher", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
