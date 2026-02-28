/*
 * SingleThread - Task-Centric Wayland Compositor
 * launcher.c - Application launcher (layer-shell overlay)
 *
 * Reads .desktop files, provides search, launches into current task.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* ─── Desktop entry ────────────────────────────────────────────── */

typedef struct {
	char *name;
	char *exec;
	char *icon;
	char *desktop_id;
	char *comment;
	gboolean no_display;
} DesktopEntry;

static GList *entries = NULL;

static char *strip_exec_codes(const char *exec) {
	/* Remove %f, %F, %u, %U etc. from Exec lines */
	char *result = g_strdup(exec);
	char *p;
	while ((p = strchr(result, '%')) != NULL) {
		if (p[1]) {
			memmove(p, p + 2, strlen(p + 2) + 1);
		} else {
			*p = '\0';
		}
	}
	g_strstrip(result);
	return result;
}

static void load_desktop_entries(const char *dir) {
	DIR *d = opendir(dir);
	if (!d) return;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (!g_str_has_suffix(ent->d_name, ".desktop")) continue;

		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

		GKeyFile *kf = g_key_file_new();
		if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
			g_key_file_free(kf);
			continue;
		}

		char *type = g_key_file_get_string(kf, "Desktop Entry",
			"Type", NULL);
		if (!type || strcmp(type, "Application") != 0) {
			g_free(type);
			g_key_file_free(kf);
			continue;
		}
		g_free(type);

		DesktopEntry *entry = g_new0(DesktopEntry, 1);
		entry->name = g_key_file_get_locale_string(kf, "Desktop Entry",
			"Name", NULL, NULL);
		char *raw_exec = g_key_file_get_string(kf, "Desktop Entry",
			"Exec", NULL);
		entry->exec = raw_exec ? strip_exec_codes(raw_exec) : NULL;
		g_free(raw_exec);
		entry->icon = g_key_file_get_string(kf, "Desktop Entry",
			"Icon", NULL);
		entry->desktop_id = g_strdup(ent->d_name);
		entry->comment = g_key_file_get_locale_string(kf, "Desktop Entry",
			"Comment", NULL, NULL);
		entry->no_display = g_key_file_get_boolean(kf, "Desktop Entry",
			"NoDisplay", NULL);

		g_key_file_free(kf);

		if (!entry->name || !entry->exec || entry->no_display) {
			g_free(entry->name);
			g_free(entry->exec);
			g_free(entry->icon);
			g_free(entry->desktop_id);
			g_free(entry->comment);
			g_free(entry);
			continue;
		}

		entries = g_list_prepend(entries, entry);
	}

	closedir(d);
}

static void load_all_entries(void) {
	/* XDG data directories */
	const char *data_dirs = g_get_system_data_dirs()[0] ?
		NULL : "/usr/share";
	const gchar * const *dirs = g_get_system_data_dirs();
	for (int i = 0; dirs[i]; i++) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/applications", dirs[i]);
		load_desktop_entries(path);
	}

	/* User applications */
	const char *data_home = g_get_user_data_dir();
	if (data_home) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/applications", data_home);
		load_desktop_entries(path);
	}

	/* Sort alphabetically */
	entries = g_list_sort(entries, (GCompareFunc)(void *)strcmp);
	(void)data_dirs;
}

/* ─── IPC ──────────────────────────────────────────────────────── */

static void ipc_launch(const char *exec) {
	const char *socket_path = getenv("SINGLETHREAD_SOCKET");
	if (!socket_path) {
		/* Fall back to direct exec */
		g_spawn_command_line_async(exec, NULL);
		return;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		g_spawn_command_line_async(exec, NULL);
		return;
	}

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		g_spawn_command_line_async(exec, NULL);
		return;
	}

	char json[2048];
	snprintf(json, sizeof(json),
		"{\"command\": \"launch\", \"exec\": \"%s\"}", exec);

	uint32_t len = (uint32_t)strlen(json);
	write(fd, &len, 4);
	write(fd, json, len);
	close(fd);
}

/* ─── Launcher UI ──────────────────────────────────────────────── */

typedef struct {
	GtkWindow *window;
	GtkWidget *search_entry;
	GtkWidget *results_list;
	GtkStringList *model;
} LauncherState;

static LauncherState launcher = {0};
static GList *filtered_entries = NULL;

static void launch_entry(DesktopEntry *entry) {
	if (entry && entry->exec) {
		ipc_launch(entry->exec);
	}
	/* Close the launcher */
	gtk_window_close(launcher.window);
}

static void on_row_activated(GtkListView *list_view, guint position,
		gpointer data) {
	(void)list_view;
	(void)data;

	GList *item = g_list_nth(filtered_entries, position);
	if (item) {
		launch_entry(item->data);
	}
}

static gboolean match_entry(DesktopEntry *entry, const char *query) {
	if (!query || !*query) return TRUE;

	char *name_lower = g_utf8_strdown(entry->name, -1);
	char *query_lower = g_utf8_strdown(query, -1);
	gboolean match = (strstr(name_lower, query_lower) != NULL);

	if (!match && entry->comment) {
		char *comment_lower = g_utf8_strdown(entry->comment, -1);
		match = (strstr(comment_lower, query_lower) != NULL);
		g_free(comment_lower);
	}

	if (!match && entry->desktop_id) {
		char *id_lower = g_utf8_strdown(entry->desktop_id, -1);
		match = (strstr(id_lower, query_lower) != NULL);
		g_free(id_lower);
	}

	g_free(name_lower);
	g_free(query_lower);
	return match;
}

static void update_results(void) {
	const char *query = gtk_editable_get_text(
		GTK_EDITABLE(launcher.search_entry));

	/* Clear model */
	while (g_list_model_get_n_items(G_LIST_MODEL(launcher.model)) > 0) {
		gtk_string_list_remove(launcher.model, 0);
	}

	g_list_free(filtered_entries);
	filtered_entries = NULL;

	int count = 0;
	GList *l;
	for (l = entries; l && count < 20; l = l->next) {
		DesktopEntry *entry = l->data;
		if (match_entry(entry, query)) {
			gtk_string_list_append(launcher.model, entry->name);
			filtered_entries = g_list_append(filtered_entries, entry);
			count++;
		}
	}
}

static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
	(void)entry;
	(void)data;
	update_results();
}

static gboolean on_key_pressed(GtkEventControllerKey *controller,
		guint keyval, guint keycode, GdkModifierType state,
		gpointer data) {
	(void)controller;
	(void)keycode;
	(void)state;
	(void)data;

	if (keyval == GDK_KEY_Escape) {
		gtk_window_close(launcher.window);
		return TRUE;
	}

	if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
		/* Launch first result */
		if (filtered_entries) {
			launch_entry(filtered_entries->data);
		}
		return TRUE;
	}

	return FALSE;
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *launcher_css =
	"window {"
	"  background-color: rgba(26, 27, 38, 0.95);"
	"  color: #c0caf5;"
	"  font-family: monospace;"
	"}"
	".launcher-search {"
	"  font-size: 18px;"
	"  padding: 12px;"
	"  background-color: #24283b;"
	"  color: #c0caf5;"
	"  border: 2px solid #7aa2f7;"
	"  border-radius: 8px;"
	"  margin: 8px;"
	"}"
	".launcher-list {"
	"  background-color: transparent;"
	"}"
	".launcher-list row {"
	"  padding: 8px 12px;"
	"  border-radius: 4px;"
	"  margin: 1px 8px;"
	"}"
	".launcher-list row:selected {"
	"  background-color: #7aa2f7;"
	"  color: #1a1b26;"
	"}";

/* ─── Activation ───────────────────────────────────────────────── */

static void activate(GtkApplication *app, gpointer data) {
	(void)data;

	/* Load CSS */
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, launcher_css);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	/* Load desktop entries */
	load_all_entries();

	/* Create window */
	launcher.window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_title(launcher.window, "stw-launcher");
	gtk_window_set_default_size(launcher.window, 500, 400);

	/* Layer shell setup */
	gtk_layer_init_for_window(launcher.window);
	gtk_layer_set_layer(launcher.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_keyboard_mode(launcher.window,
		GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_layer_set_namespace(launcher.window, "stw-launcher");

	/* Key handler for Escape */
	GtkEventController *key_ctrl =
		gtk_event_controller_key_new();
	g_signal_connect(key_ctrl, "key-pressed",
		G_CALLBACK(on_key_pressed), NULL);
	gtk_widget_add_controller(GTK_WIDGET(launcher.window), key_ctrl);

	/* Main layout */
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	/* Search entry */
	launcher.search_entry = gtk_search_entry_new();
	gtk_widget_add_css_class(launcher.search_entry, "launcher-search");
	g_signal_connect(launcher.search_entry, "search-changed",
		G_CALLBACK(on_search_changed), NULL);
	gtk_box_append(GTK_BOX(vbox), launcher.search_entry);

	/* Results list */
	launcher.model = gtk_string_list_new(NULL);

	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(
		+[](GtkSignalListItemFactory *f, GtkListItem *item, gpointer d) {
			(void)f; (void)d;
			GtkWidget *label = gtk_label_new("");
			gtk_label_set_xalign(GTK_LABEL(label), 0.0);
			gtk_list_item_set_child(item, label);
		}), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(
		+[](GtkSignalListItemFactory *f, GtkListItem *item, gpointer d) {
			(void)f; (void)d;
			GtkWidget *label = gtk_list_item_get_child(item);
			GtkStringObject *obj = gtk_list_item_get_item(item);
			gtk_label_set_text(GTK_LABEL(label),
				gtk_string_object_get_string(obj));
		}), NULL);

	GtkNoSelection *selection = gtk_no_selection_new(
		G_LIST_MODEL(launcher.model));
	launcher.results_list = gtk_list_view_new(
		GTK_SELECTION_MODEL(selection), factory);
	gtk_widget_add_css_class(launcher.results_list, "launcher-list");
	g_signal_connect(launcher.results_list, "activate",
		G_CALLBACK(on_row_activated), NULL);

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
		launcher.results_list);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_append(GTK_BOX(vbox), scroll);

	gtk_window_set_child(launcher.window, vbox);

	/* Initial results */
	update_results();

	/* Focus search entry */
	gtk_widget_grab_focus(launcher.search_entry);

	gtk_window_present(launcher.window);
}

int main(int argc, char *argv[]) {
	GtkApplication *app = gtk_application_new(
		"org.singlethread.launcher", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
