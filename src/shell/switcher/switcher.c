/*
 * SingleThread - Task-Centric Wayland Compositor
 * switcher.c - Beautiful task switcher overlay
 *
 * Design: centered card with task cards showing name, window count,
 * color accent bar, active indicator with glow, keyboard shortcuts.
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
	int active_workspace;
} TaskInfo;

static GList *task_list = NULL;
static GList *filtered_tasks = NULL;

/* Accent colors for tasks (cycle through) */
static const char *accent_colors[] = {
	"#7aa2f7", /* Blue */
	"#bb9af7", /* Purple */
	"#7dcfff", /* Cyan */
	"#e0af68", /* Yellow */
	"#9ece6a", /* Green */
	"#f7768e", /* Red */
	"#ff9e64", /* Orange */
	"#2ac3de", /* Teal */
};
#define N_ACCENTS (sizeof(accent_colors) / sizeof(accent_colors[0]))

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

		struct json_object *archived;
		if (json_object_object_get_ex(task_obj, "archived", &archived) &&
				json_object_get_boolean(archived))
			continue;

		TaskInfo *ti = g_new0(TaskInfo, 1);
		if (json_object_object_get_ex(task_obj, "id", &tmp))
			ti->id = json_object_get_int(tmp);
		if (json_object_object_get_ex(task_obj, "name", &tmp))
			ti->name = g_strdup(json_object_get_string(tmp));
		if (json_object_object_get_ex(task_obj, "active", &tmp))
			ti->active = json_object_get_boolean(tmp);
		if (json_object_object_get_ex(task_obj, "window_count", &tmp))
			ti->window_count = json_object_get_int(tmp);
		if (json_object_object_get_ex(task_obj, "active_workspace", &tmp))
			ti->active_workspace = json_object_get_int(tmp);

		task_list = g_list_append(task_list, ti);
	}

	json_object_put(root);
}

/* ─── Switcher UI ──────────────────────────────────────────────── */

typedef struct {
	GtkWindow *window;
	GtkWidget *search_entry;
	GtkWidget *task_list_box;
	GtkWidget *header_label;
	int selected_index;
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

/* ─── Task card builder ────────────────────────────────────────── */

static GtkWidget *create_task_card(TaskInfo *ti, int index) {
	const char *accent = accent_colors[index % N_ACCENTS];

	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(card, "task-card");
	if (ti->active) {
		gtk_widget_add_css_class(card, "task-card-active");
	}

	/* Color accent bar (left edge) */
	GtkWidget *accent_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(accent_bar, "accent-bar");
	/* Apply per-task color via inline style */
	char accent_css[128];
	snprintf(accent_css, sizeof(accent_css),
		"min-width: 4px; border-radius: 2px; margin: 4px 0 4px 4px; "
		"background-color: %s;", accent);
	/* Use a CSS provider per-widget for the accent color */
	GtkCssProvider *bar_css = gtk_css_provider_new();
	char bar_css_str[256];
	snprintf(bar_css_str, sizeof(bar_css_str),
		".accent-%d { min-width: 4px; border-radius: 2px; "
		"margin: 4px 0 4px 4px; background-color: %s; "
		"transition: min-width 200ms ease; }",
		index, accent);
	gtk_css_provider_load_from_string(bar_css, bar_css_str);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(bar_css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	char class_name[32];
	snprintf(class_name, sizeof(class_name), "accent-%d", index);
	gtk_widget_add_css_class(accent_bar, class_name);
	gtk_box_append(GTK_BOX(card), accent_bar);

	/* Content area */
	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_margin_start(content, 12);
	gtk_widget_set_margin_end(content, 12);
	gtk_widget_set_margin_top(content, 10);
	gtk_widget_set_margin_bottom(content, 10);
	gtk_widget_set_hexpand(content, TRUE);

	/* Keyboard shortcut number (large) */
	char num_str[8];
	snprintf(num_str, sizeof(num_str), "%d", index + 1);
	GtkWidget *num = gtk_label_new(num_str);
	gtk_widget_add_css_class(num, "task-shortcut");

	/* Apply accent color to the number */
	GtkCssProvider *num_css = gtk_css_provider_new();
	char num_css_str[256];
	snprintf(num_css_str, sizeof(num_css_str),
		".task-num-%d { color: %s; }",
		index, accent);
	gtk_css_provider_load_from_string(num_css, num_css_str);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(num_css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	char num_class[32];
	snprintf(num_class, sizeof(num_class), "task-num-%d", index);
	gtk_widget_add_css_class(num, num_class);
	gtk_box_append(GTK_BOX(content), num);

	/* Text column */
	GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_hexpand(text_box, TRUE);
	gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

	GtkWidget *name = gtk_label_new(ti->name);
	gtk_widget_add_css_class(name, "task-card-name");
	gtk_label_set_xalign(GTK_LABEL(name), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(text_box), name);

	/* Status line: window count + workspace */
	char status_str[64];
	snprintf(status_str, sizeof(status_str),
		"%d window%s \302\267 workspace %d",
		ti->window_count,
		ti->window_count == 1 ? "" : "s",
		ti->active_workspace + 1);
	GtkWidget *status = gtk_label_new(status_str);
	gtk_widget_add_css_class(status, "task-card-status");
	gtk_label_set_xalign(GTK_LABEL(status), 0.0);
	gtk_box_append(GTK_BOX(text_box), status);

	gtk_box_append(GTK_BOX(content), text_box);

	/* Active indicator */
	if (ti->active) {
		GtkWidget *active_badge = gtk_label_new("active");
		gtk_widget_add_css_class(active_badge, "active-badge");
		gtk_widget_set_valign(active_badge, GTK_ALIGN_CENTER);
		gtk_box_append(GTK_BOX(content), active_badge);
	}

	gtk_box_append(GTK_BOX(card), content);

	return card;
}

/* ─── Update task list ─────────────────────────────────────────── */

static void update_task_list(void) {
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(
			GTK_WIDGET(switcher.task_list_box)))) {
		gtk_list_box_remove(GTK_LIST_BOX(switcher.task_list_box), child);
	}

	g_list_free(filtered_tasks);
	filtered_tasks = NULL;

	const char *query = gtk_editable_get_text(
		GTK_EDITABLE(switcher.search_entry));

	int index = 0;
	for (GList *l = task_list; l; l = l->next) {
		TaskInfo *ti = l->data;

		/* Filter by search query */
		if (query && *query) {
			char *name_lower = g_utf8_strdown(ti->name, -1);
			char *query_lower = g_utf8_strdown(query, -1);
			gboolean match = (strstr(name_lower, query_lower) != NULL);
			g_free(name_lower);
			g_free(query_lower);
			if (!match) continue;
		}

		filtered_tasks = g_list_append(filtered_tasks, ti);

		GtkWidget *card = create_task_card(ti, index);
		gtk_list_box_append(GTK_LIST_BOX(switcher.task_list_box), card);
		index++;
	}

	/* Update header */
	char header_str[64];
	snprintf(header_str, sizeof(header_str), "Switch Task (%d)", index);
	gtk_label_set_text(GTK_LABEL(switcher.header_label), header_str);
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
		GtkListBoxRow *row = gtk_list_box_get_selected_row(
			GTK_LIST_BOX(switcher.task_list_box));
		if (row) {
			int idx = gtk_list_box_row_get_index(row);
			GList *item = g_list_nth(filtered_tasks, idx);
			if (item) {
				TaskInfo *ti = item->data;
				switch_to_task(ti->id);
			}
		} else if (filtered_tasks) {
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
	/* ── Background ──────────────────────────────────────────── */
	"window {"
	"  background-color: rgba(15, 15, 22, 0.75);"
	"}"

	/* ── Main card ───────────────────────────────────────────── */
	".switcher-card {"
	"  background-color: rgba(26, 27, 38, 0.97);"
	"  border: 1px solid rgba(122, 162, 247, 0.15);"
	"  border-radius: 16px;"
	"  padding: 4px;"
	"}"

	/* ── Header ──────────────────────────────────────────────── */
	".switcher-header {"
	"  font-size: 12px;"
	"  font-weight: 600;"
	"  color: rgba(169, 177, 214, 0.4);"
	"  text-transform: uppercase;"
	"  letter-spacing: 1.5px;"
	"  padding: 12px 16px 4px 16px;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"

	/* ── Search ──────────────────────────────────────────────── */
	".switcher-search {"
	"  font-size: 16px;"
	"  padding: 10px 14px;"
	"  background-color: rgba(36, 40, 59, 0.6);"
	"  color: #c0caf5;"
	"  border: 1px solid rgba(122, 162, 247, 0.2);"
	"  border-radius: 10px;"
	"  margin: 6px 12px;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"  caret-color: #7aa2f7;"
	"  transition: border-color 200ms ease;"
	"}"
	".switcher-search:focus {"
	"  border-color: rgba(122, 162, 247, 0.5);"
	"}"

	/* ── Task cards ──────────────────────────────────────────── */
	".task-card {"
	"  background-color: rgba(36, 40, 59, 0.4);"
	"  border: 1px solid transparent;"
	"  border-radius: 10px;"
	"  margin: 2px 8px;"
	"  transition: all 200ms ease;"
	"}"
	".task-card:hover {"
	"  background-color: rgba(52, 59, 88, 0.6);"
	"  border-color: rgba(86, 95, 137, 0.3);"
	"}"
	".task-card-active {"
	"  background-color: rgba(122, 162, 247, 0.1);"
	"  border-color: rgba(122, 162, 247, 0.2);"
	"}"
	".task-card-active:hover {"
	"  background-color: rgba(122, 162, 247, 0.15);"
	"}"

	/* ── ListBox styling ─────────────────────────────────────── */
	"listbox {"
	"  background-color: transparent;"
	"}"
	"listbox row {"
	"  background-color: transparent;"
	"  padding: 0;"
	"  border-radius: 10px;"
	"}"
	"listbox row:selected {"
	"  background-color: transparent;"
	"}"
	"listbox row:selected .task-card {"
	"  background-color: rgba(122, 162, 247, 0.12);"
	"  border-color: rgba(122, 162, 247, 0.3);"
	"}"

	/* ── Task card elements ──────────────────────────────────── */
	".task-shortcut {"
	"  font-size: 20px;"
	"  font-weight: 700;"
	"  min-width: 28px;"
	"  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
	"  opacity: 0.7;"
	"}"
	".task-card-name {"
	"  font-size: 15px;"
	"  font-weight: 600;"
	"  color: #c0caf5;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"
	".task-card-active .task-card-name {"
	"  color: #e0e8ff;"
	"}"
	".task-card-status {"
	"  font-size: 11.5px;"
	"  color: rgba(169, 177, 214, 0.4);"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"
	".active-badge {"
	"  font-size: 10px;"
	"  font-weight: 700;"
	"  text-transform: uppercase;"
	"  letter-spacing: 0.5px;"
	"  color: #7aa2f7;"
	"  background-color: rgba(122, 162, 247, 0.15);"
	"  border: 1px solid rgba(122, 162, 247, 0.25);"
	"  border-radius: 6px;"
	"  padding: 2px 8px;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"

	/* ── Footer hints ────────────────────────────────────────── */
	".switcher-footer {"
	"  padding: 8px 16px;"
	"  margin-top: 4px;"
	"  border-top: 1px solid rgba(86, 95, 137, 0.15);"
	"}"
	".hint-text {"
	"  font-size: 11px;"
	"  color: rgba(86, 95, 137, 0.4);"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"
	".hint-key {"
	"  font-size: 10px;"
	"  font-family: 'JetBrains Mono', monospace;"
	"  background-color: rgba(36, 40, 59, 0.8);"
	"  border: 1px solid rgba(86, 95, 137, 0.3);"
	"  border-radius: 3px;"
	"  padding: 1px 5px;"
	"  color: rgba(169, 177, 214, 0.6);"
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

	/* Full-screen overlay */
	switcher.window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_title(switcher.window, "stw-switcher");

	gtk_layer_init_for_window(switcher.window);
	gtk_layer_set_layer(switcher.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_anchor(switcher.window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(switcher.window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(switcher.window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(switcher.window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_keyboard_mode(switcher.window,
		GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_layer_set_namespace(switcher.window, "stw-switcher");

	/* Dismiss on background click */
	GtkGesture *bg_click = gtk_gesture_click_new();
	g_signal_connect_swapped(bg_click, "pressed",
		G_CALLBACK(gtk_window_close), switcher.window);
	gtk_widget_add_controller(GTK_WIDGET(switcher.window),
		GTK_EVENT_CONTROLLER(bg_click));

	GtkEventController *key_ctrl = gtk_event_controller_key_new();
	g_signal_connect(key_ctrl, "key-pressed",
		G_CALLBACK(on_key_pressed), NULL);
	gtk_widget_add_controller(GTK_WIDGET(switcher.window), key_ctrl);

	/* Centered card */
	GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(center, 480, -1);

	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(card, "switcher-card");

	/* Header */
	switcher.header_label = gtk_label_new("Switch Task");
	gtk_widget_add_css_class(switcher.header_label, "switcher-header");
	gtk_label_set_xalign(GTK_LABEL(switcher.header_label), 0.0);
	gtk_box_append(GTK_BOX(card), switcher.header_label);

	/* Search */
	switcher.search_entry = gtk_search_entry_new();
	gtk_widget_add_css_class(switcher.search_entry, "switcher-search");
	g_signal_connect(switcher.search_entry, "search-changed",
		G_CALLBACK(on_search_changed), NULL);
	gtk_box_append(GTK_BOX(card), switcher.search_entry);

	/* Task list */
	switcher.task_list_box = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(switcher.task_list_box),
		GTK_SELECTION_BROWSE);
	g_signal_connect(switcher.task_list_box, "row-activated",
		G_CALLBACK(on_task_row_activated), NULL);

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
		switcher.task_list_box);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_widget_set_size_request(scroll, -1, 340);
	gtk_box_append(GTK_BOX(card), scroll);

	/* Footer with keyboard hints */
	GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_add_css_class(footer, "switcher-footer");
	gtk_widget_set_halign(footer, GTK_ALIGN_CENTER);

	const char *hints[][2] = {
		{"1-9", "quick switch"},
		{"\342\206\265", "select"},
		{"Esc", "cancel"},
		{NULL, NULL}
	};
	for (int i = 0; hints[i][0]; i++) {
		GtkWidget *key = gtk_label_new(hints[i][0]);
		gtk_widget_add_css_class(key, "hint-key");
		gtk_box_append(GTK_BOX(footer), key);
		GtkWidget *text = gtk_label_new(hints[i][1]);
		gtk_widget_add_css_class(text, "hint-text");
		gtk_box_append(GTK_BOX(footer), text);
	}
	gtk_box_append(GTK_BOX(card), footer);

	gtk_box_append(GTK_BOX(center), card);
	gtk_window_set_child(switcher.window, center);

	update_task_list();
	gtk_widget_grab_focus(switcher.search_entry);

	/* Select active task's row */
	GtkListBoxRow *first = gtk_list_box_get_row_at_index(
		GTK_LIST_BOX(switcher.task_list_box), 0);
	if (first) {
		gtk_list_box_select_row(GTK_LIST_BOX(switcher.task_list_box),
			first);
	}

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
