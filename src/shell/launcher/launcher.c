/*
 * SingleThread - Task-Centric Wayland Compositor
 * launcher.c - Beautiful application launcher overlay
 *
 * Design: centered floating card with frosted glass background,
 * large search bar, app results with icons + name + description,
 * keyboard shortcut hints, smooth transitions.
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
	char *generic_name;
	char **categories;
	gboolean no_display;
} DesktopEntry;

static GList *entries = NULL;

static char *strip_exec_codes(const char *exec) {
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

static int entry_compare(gconstpointer a, gconstpointer b) {
	const DesktopEntry *ea = a;
	const DesktopEntry *eb = b;
	return g_utf8_collate(ea->name, eb->name);
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
		entry->generic_name = g_key_file_get_locale_string(kf,
			"Desktop Entry", "GenericName", NULL, NULL);
		entry->no_display = g_key_file_get_boolean(kf, "Desktop Entry",
			"NoDisplay", NULL);

		g_key_file_free(kf);

		if (!entry->name || !entry->exec || entry->no_display) {
			g_free(entry->name);
			g_free(entry->exec);
			g_free(entry->icon);
			g_free(entry->desktop_id);
			g_free(entry->comment);
			g_free(entry->generic_name);
			g_free(entry);
			continue;
		}

		entries = g_list_prepend(entries, entry);
	}

	closedir(d);
}

static void load_all_entries(void) {
	const gchar * const *dirs = g_get_system_data_dirs();
	for (int i = 0; dirs[i]; i++) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/applications", dirs[i]);
		load_desktop_entries(path);
	}

	const char *data_home = g_get_user_data_dir();
	if (data_home) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/applications", data_home);
		load_desktop_entries(path);
	}

	entries = g_list_sort(entries, entry_compare);
}

/* ─── IPC ──────────────────────────────────────────────────────── */

static void ipc_launch(const char *exec) {
	const char *socket_path = getenv("SINGLETHREAD_SOCKET");
	if (!socket_path) {
		g_spawn_command_line_async(exec, NULL);
		return;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) { g_spawn_command_line_async(exec, NULL); return; }

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
	GtkWidget *results_box;
	GtkWidget *hint_label;
	GtkWidget *count_label;
	int selected_index;
} LauncherState;

static LauncherState launcher = {0};
static GList *filtered_entries = NULL;

static void launch_entry(DesktopEntry *entry) {
	if (entry && entry->exec) {
		ipc_launch(entry->exec);
	}
	gtk_window_close(launcher.window);
}

/* ─── Fuzzy match scoring ──────────────────────────────────────── */

static int fuzzy_score(const char *haystack, const char *needle) {
	if (!needle || !*needle) return 100;
	if (!haystack) return 0;

	char *h = g_utf8_strdown(haystack, -1);
	char *n = g_utf8_strdown(needle, -1);
	int score = 0;

	/* Exact prefix match: highest */
	if (g_str_has_prefix(h, n)) {
		score = 100;
	}
	/* Contains substring */
	else if (strstr(h, n)) {
		score = 60;
	}
	/* Fuzzy: all chars present in order */
	else {
		const char *hp = h;
		const char *np = n;
		int matched = 0;
		int total = (int)strlen(n);
		while (*hp && *np) {
			if (*hp == *np) {
				np++;
				matched++;
			}
			hp++;
		}
		if (matched == total) {
			score = 30;
		}
	}

	g_free(h);
	g_free(n);
	return score;
}

/* ─── Result row builder ──────────────────────────────────────── */

static GtkWidget *create_result_row(DesktopEntry *entry, int index,
		gboolean selected) {
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_add_css_class(row, "result-row");
	if (selected) {
		gtk_widget_add_css_class(row, "result-selected");
	}
	gtk_widget_set_margin_start(row, 8);
	gtk_widget_set_margin_end(row, 8);
	gtk_widget_set_margin_top(row, 2);
	gtk_widget_set_margin_bottom(row, 2);

	/* App icon */
	GtkWidget *icon_widget;
	if (entry->icon) {
		GtkIconTheme *theme = gtk_icon_theme_get_for_display(
			gdk_display_get_default());
		GtkIconPaintable *paintable = gtk_icon_theme_lookup_icon(
			theme, entry->icon, NULL, 32, 1,
			GTK_TEXT_DIR_LTR, 0);
		if (paintable) {
			icon_widget = gtk_image_new_from_paintable(
				GDK_PAINTABLE(paintable));
			g_object_unref(paintable);
		} else {
			icon_widget = gtk_image_new_from_icon_name(
				"application-x-executable");
		}
	} else {
		icon_widget = gtk_image_new_from_icon_name(
			"application-x-executable");
	}
	gtk_image_set_pixel_size(GTK_IMAGE(icon_widget), 32);
	gtk_widget_add_css_class(icon_widget, "result-icon");
	gtk_box_append(GTK_BOX(row), icon_widget);

	/* Text column: name + description */
	GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
	gtk_widget_set_hexpand(text_box, TRUE);
	gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

	GtkWidget *name_label = gtk_label_new(entry->name);
	gtk_widget_add_css_class(name_label, "result-name");
	gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(text_box), name_label);

	/* Description: prefer comment, fall back to generic name */
	const char *desc = entry->comment ? entry->comment :
		entry->generic_name;
	if (desc && *desc) {
		GtkWidget *desc_label = gtk_label_new(desc);
		gtk_widget_add_css_class(desc_label, "result-desc");
		gtk_label_set_xalign(GTK_LABEL(desc_label), 0.0);
		gtk_label_set_ellipsize(GTK_LABEL(desc_label),
			PANGO_ELLIPSIZE_END);
		gtk_box_append(GTK_BOX(text_box), desc_label);
	}

	gtk_box_append(GTK_BOX(row), text_box);

	/* Keyboard shortcut hint (for top results) */
	if (index < 9) {
		char hint[16];
		/* Show as subtle keyboard shortcut */
		snprintf(hint, sizeof(hint), "\342\206\265"); /* return symbol */
		if (index == 0 && selected) {
			GtkWidget *hint_label = gtk_label_new(hint);
			gtk_widget_add_css_class(hint_label, "result-hint");
			gtk_box_append(GTK_BOX(row), hint_label);
		}
	}

	return row;
}

/* ─── Update results ───────────────────────────────────────────── */

static void update_results(void) {
	const char *query = gtk_editable_get_text(
		GTK_EDITABLE(launcher.search_entry));

	/* Clear results */
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(launcher.results_box))) {
		gtk_box_remove(GTK_BOX(launcher.results_box), child);
	}

	g_list_free(filtered_entries);
	filtered_entries = NULL;
	launcher.selected_index = 0;

	/* Score and filter entries */
	typedef struct { DesktopEntry *entry; int score; } Scored;
	GList *scored = NULL;

	for (GList *l = entries; l; l = l->next) {
		DesktopEntry *entry = l->data;
		int score = fuzzy_score(entry->name, query);

		/* Also match against comment and desktop_id */
		if (score < 30 && entry->comment) {
			int cs = fuzzy_score(entry->comment, query);
			if (cs > score) score = cs / 2; /* Discount */
		}
		if (score < 30 && entry->desktop_id) {
			int ds = fuzzy_score(entry->desktop_id, query);
			if (ds > score) score = ds / 2;
		}

		if (score > 0) {
			Scored *s = g_new(Scored, 1);
			s->entry = entry;
			s->score = score;
			scored = g_list_prepend(scored, s);
		}
	}

	/* Sort by score descending */
	scored = g_list_sort(scored, (GCompareFunc)(void *)(
		int (*)(const void *, const void *))
		+[](const Scored *a, const Scored *b) -> int {
			return b->score - a->score;
		});

	/* Build result rows */
	int count = 0;
	for (GList *l = scored; l && count < 12; l = l->next) {
		Scored *s = l->data;
		filtered_entries = g_list_append(filtered_entries, s->entry);

		GtkWidget *row = create_result_row(s->entry, count,
			count == launcher.selected_index);

		/* Click handler */
		GtkGesture *click = gtk_gesture_click_new();
		g_signal_connect_swapped(click, "pressed",
			G_CALLBACK(launch_entry), s->entry);
		gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));

		gtk_box_append(GTK_BOX(launcher.results_box), row);
		count++;
	}

	/* Free scored list */
	g_list_free_full(scored, g_free);

	/* Update count label */
	char count_text[64];
	snprintf(count_text, sizeof(count_text),
		"%d result%s", count, count == 1 ? "" : "s");
	gtk_label_set_text(GTK_LABEL(launcher.count_label), count_text);
}

static void update_selection(void) {
	int i = 0;
	GtkWidget *child = gtk_widget_get_first_child(launcher.results_box);
	while (child) {
		if (i == launcher.selected_index) {
			gtk_widget_add_css_class(child, "result-selected");
		} else {
			gtk_widget_remove_css_class(child, "result-selected");
		}
		child = gtk_widget_get_next_sibling(child);
		i++;
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
	(void)controller; (void)keycode; (void)state; (void)data;

	if (keyval == GDK_KEY_Escape) {
		gtk_window_close(launcher.window);
		return TRUE;
	}

	if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
		GList *item = g_list_nth(filtered_entries,
			launcher.selected_index);
		if (item) {
			launch_entry(item->data);
		}
		return TRUE;
	}

	if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Tab) {
		int count = (int)g_list_length(filtered_entries);
		if (count > 0) {
			launcher.selected_index =
				(launcher.selected_index + 1) % count;
			update_selection();
		}
		return TRUE;
	}

	if (keyval == GDK_KEY_Up ||
			(keyval == GDK_KEY_Tab &&
			 (state & GDK_SHIFT_MASK))) {
		int count = (int)g_list_length(filtered_entries);
		if (count > 0) {
			launcher.selected_index =
				(launcher.selected_index - 1 + count) % count;
			update_selection();
		}
		return TRUE;
	}

	return FALSE;
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *launcher_css =
	/* ── Background overlay ──────────────────────────────────── */
	"window {"
	"  background-color: rgba(15, 15, 22, 0.75);"
	"}"

	/* ── Main card ───────────────────────────────────────────── */
	".launcher-card {"
	"  background-color: rgba(26, 27, 38, 0.97);"
	"  border: 1px solid rgba(122, 162, 247, 0.2);"
	"  border-radius: 16px;"
	"  margin: 0;"
	"  padding: 4px;"
	"}"

	/* ── Search bar ──────────────────────────────────────────── */
	".launcher-search {"
	"  font-size: 20px;"
	"  padding: 14px 18px;"
	"  background-color: rgba(36, 40, 59, 0.8);"
	"  color: #c0caf5;"
	"  border: 2px solid rgba(122, 162, 247, 0.3);"
	"  border-radius: 12px;"
	"  margin: 12px 12px 8px 12px;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"  font-weight: 400;"
	"  transition: border-color 200ms ease;"
	"  caret-color: #7aa2f7;"
	"}"
	".launcher-search:focus {"
	"  border-color: rgba(122, 162, 247, 0.6);"
	"  outline: none;"
	"}"
	/* Search icon styling */
	".launcher-search image {"
	"  color: rgba(122, 162, 247, 0.5);"
	"  -gtk-icon-size: 18px;"
	"}"

	/* ── Result rows ─────────────────────────────────────────── */
	".result-row {"
	"  padding: 8px 12px;"
	"  border-radius: 10px;"
	"  transition: background-color 150ms ease;"
	"}"
	".result-row:hover {"
	"  background-color: rgba(52, 59, 88, 0.5);"
	"}"
	".result-selected {"
	"  background-color: rgba(122, 162, 247, 0.15);"
	"  border: 1px solid rgba(122, 162, 247, 0.25);"
	"}"
	".result-selected:hover {"
	"  background-color: rgba(122, 162, 247, 0.2);"
	"}"

	/* ── Result icon ─────────────────────────────────────────── */
	".result-icon {"
	"  margin-right: 4px;"
	"  opacity: 0.9;"
	"}"
	".result-selected .result-icon {"
	"  opacity: 1.0;"
	"}"

	/* ── Result text ─────────────────────────────────────────── */
	".result-name {"
	"  font-size: 14px;"
	"  font-weight: 500;"
	"  color: #c0caf5;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"
	".result-selected .result-name {"
	"  color: #e0e8ff;"
	"  font-weight: 600;"
	"}"
	".result-desc {"
	"  font-size: 11.5px;"
	"  color: rgba(169, 177, 214, 0.5);"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"  margin-top: 1px;"
	"}"
	".result-selected .result-desc {"
	"  color: rgba(169, 177, 214, 0.7);"
	"}"

	/* ── Shortcut hint ───────────────────────────────────────── */
	".result-hint {"
	"  font-size: 12px;"
	"  color: rgba(122, 162, 247, 0.4);"
	"  font-family: 'JetBrains Mono', monospace;"
	"  padding: 2px 8px;"
	"  border-radius: 4px;"
	"  background-color: rgba(122, 162, 247, 0.08);"
	"}"

	/* ── Footer ──────────────────────────────────────────────── */
	".launcher-footer {"
	"  padding: 6px 16px;"
	"  margin-top: 4px;"
	"  border-top: 1px solid rgba(86, 95, 137, 0.2);"
	"}"
	".result-count {"
	"  font-size: 11px;"
	"  color: rgba(86, 95, 137, 0.6);"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
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
	gtk_css_provider_load_from_string(css, launcher_css);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	load_all_entries();

	/* Window (full screen overlay) */
	launcher.window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_title(launcher.window, "stw-launcher");
	gtk_window_set_default_size(launcher.window, 560, 500);

	gtk_layer_init_for_window(launcher.window);
	gtk_layer_set_layer(launcher.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_anchor(launcher.window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(launcher.window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(launcher.window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(launcher.window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_keyboard_mode(launcher.window,
		GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_layer_set_namespace(launcher.window, "stw-launcher");

	/* Click on background to dismiss */
	GtkGesture *bg_click = gtk_gesture_click_new();
	g_signal_connect_swapped(bg_click, "pressed",
		G_CALLBACK(gtk_window_close), launcher.window);
	gtk_widget_add_controller(GTK_WIDGET(launcher.window),
		GTK_EVENT_CONTROLLER(bg_click));

	GtkEventController *key_ctrl = gtk_event_controller_key_new();
	g_signal_connect(key_ctrl, "key-pressed",
		G_CALLBACK(on_key_pressed), NULL);
	gtk_widget_add_controller(GTK_WIDGET(launcher.window), key_ctrl);

	/* Centered card container */
	GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(center, 560, -1);

	/* Card */
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(card, "launcher-card");

	/* Search */
	launcher.search_entry = gtk_search_entry_new();
	gtk_widget_add_css_class(launcher.search_entry, "launcher-search");
	g_signal_connect(launcher.search_entry, "search-changed",
		G_CALLBACK(on_search_changed), NULL);
	gtk_box_append(GTK_BOX(card), launcher.search_entry);

	/* Results */
	launcher.results_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(launcher.results_box, 4);
	gtk_widget_set_margin_end(launcher.results_box, 4);

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
		launcher.results_box);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_widget_set_size_request(scroll, -1, 380);
	gtk_box_append(GTK_BOX(card), scroll);

	/* Footer: result count + hints */
	GtkWidget *footer = gtk_center_box_new();
	gtk_widget_add_css_class(footer, "launcher-footer");

	launcher.count_label = gtk_label_new("");
	gtk_widget_add_css_class(launcher.count_label, "result-count");
	gtk_center_box_set_start_widget(GTK_CENTER_BOX(footer),
		launcher.count_label);

	/* Keyboard hints */
	GtkWidget *hints = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

	GtkWidget *esc_key = gtk_label_new("Esc");
	gtk_widget_add_css_class(esc_key, "hint-key");
	gtk_box_append(GTK_BOX(hints), esc_key);
	GtkWidget *esc_text = gtk_label_new("close");
	gtk_widget_add_css_class(esc_text, "hint-text");
	gtk_box_append(GTK_BOX(hints), esc_text);

	GtkWidget *enter_key = gtk_label_new("\342\206\265");
	gtk_widget_add_css_class(enter_key, "hint-key");
	gtk_box_append(GTK_BOX(hints), enter_key);
	GtkWidget *enter_text = gtk_label_new("launch");
	gtk_widget_add_css_class(enter_text, "hint-text");
	gtk_box_append(GTK_BOX(hints), enter_text);

	GtkWidget *arrow_key = gtk_label_new("\342\206\221\342\206\223");
	gtk_widget_add_css_class(arrow_key, "hint-key");
	gtk_box_append(GTK_BOX(hints), arrow_key);
	GtkWidget *arrow_text = gtk_label_new("navigate");
	gtk_widget_add_css_class(arrow_text, "hint-text");
	gtk_box_append(GTK_BOX(hints), arrow_text);

	gtk_center_box_set_end_widget(GTK_CENTER_BOX(footer), hints);

	gtk_box_append(GTK_BOX(card), footer);

	gtk_box_append(GTK_BOX(center), card);
	gtk_window_set_child(launcher.window, center);

	/* Initial results */
	update_results();
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
