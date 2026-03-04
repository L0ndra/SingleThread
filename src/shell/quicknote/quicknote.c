/*
 * SingleThread - Task-Centric Wayland Compositor
 * quicknote.c - Quick note capture overlay
 *
 * ADHD-friendly: capture thoughts instantly without leaving context.
 * Triggered by hotkey, appears as a floating input, saves to current task.
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

/* ─── IPC ──────────────────────────────────────────────────────── */

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

/* ─── State ────────────────────────────────────────────────────── */

typedef struct {
	GtkWindow *window;
	GtkWidget *entry;
	GtkWidget *task_label;
	GtkWidget *hint_label;
	char current_task_name[128];
} QuickNoteState;

static QuickNoteState state = {0};

/* ─── Submit note ──────────────────────────────────────────────── */

static void submit_note(void) {
	const char *text = gtk_editable_get_text(GTK_EDITABLE(state.entry));
	if (!text || !*text) {
		/* Empty: just close */
		GtkApplication *app = gtk_window_get_application(state.window);
		g_application_quit(G_APPLICATION(app));
		return;
	}

	/* Escape the text for JSON */
	struct json_object *req = json_object_new_object();
	json_object_object_add(req, "command",
		json_object_new_string("task/add-note"));
	json_object_object_add(req, "text",
		json_object_new_string(text));

	const char *json = json_object_to_json_string(req);
	char *resp = ipc_request(json);
	free(resp);
	json_object_put(req);

	/* Close after submit */
	GtkApplication *app = gtk_window_get_application(state.window);
	g_application_quit(G_APPLICATION(app));
}

static void on_entry_activate(GtkEntry *entry, gpointer data) {
	(void)entry; (void)data;
	submit_note();
}

static gboolean on_key_pressed(GtkEventControllerKey *controller,
		guint keyval, guint keycode, GdkModifierType mods,
		gpointer data) {
	(void)controller; (void)keycode; (void)mods; (void)data;
	if (keyval == GDK_KEY_Escape) {
		GtkApplication *app = gtk_window_get_application(state.window);
		g_application_quit(G_APPLICATION(app));
		return TRUE;
	}
	return FALSE;
}

/* ─── Get current task name ────────────────────────────────────── */

static void fetch_task_name(void) {
	char *resp = ipc_request("{\"command\": \"get/active-task\"}");
	if (!resp) {
		snprintf(state.current_task_name,
			sizeof(state.current_task_name), "No task");
		return;
	}

	struct json_object *root = json_tokener_parse(resp);
	free(resp);
	if (!root) return;

	struct json_object *task_obj;
	if (json_object_object_get_ex(root, "task", &task_obj)) {
		struct json_object *name_obj;
		if (json_object_object_get_ex(task_obj, "name", &name_obj)) {
			snprintf(state.current_task_name,
				sizeof(state.current_task_name), "%s",
				json_object_get_string(name_obj));
		}
	}
	json_object_put(root);
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *quicknote_css =
	"window {"
	"  background-color: transparent;"
	"}"

	".note-overlay {"
	"  background-color: rgba(10, 10, 18, 0.7);"
	"}"

	".note-card {"
	"  background-color: rgba(22, 22, 30, 0.97);"
	"  border: 1px solid rgba(122, 162, 247, 0.3);"
	"  border-radius: 16px;"
	"  padding: 24px;"
	"  margin: 0;"
	"}"

	".note-header {"
	"  font-size: 11px;"
	"  font-weight: 600;"
	"  color: #7aa2f7;"
	"  letter-spacing: 1.5px;"
	"  text-transform: uppercase;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"

	".note-task {"
	"  font-size: 12px;"
	"  color: rgba(169, 177, 214, 0.6);"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"

	".note-entry {"
	"  font-size: 16px;"
	"  color: #c0caf5;"
	"  background-color: rgba(36, 40, 59, 0.8);"
	"  border: 1px solid rgba(122, 162, 247, 0.2);"
	"  border-radius: 10px;"
	"  padding: 12px 16px;"
	"  caret-color: #7aa2f7;"
	"  min-width: 400px;"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}"
	".note-entry:focus {"
	"  border-color: rgba(122, 162, 247, 0.5);"
	"  box-shadow: 0 0 0 2px rgba(122, 162, 247, 0.1);"
	"}"

	".note-hint {"
	"  font-size: 11px;"
	"  color: rgba(86, 95, 137, 0.6);"
	"  font-family: 'Inter', 'Cantarell', sans-serif;"
	"}";

/* ─── Activation ───────────────────────────────────────────────── */

static void activate(GtkApplication *app, gpointer data) {
	(void)data;

	/* Load CSS */
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, quicknote_css);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	fetch_task_name();

	/* Create window */
	state.window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_title(state.window, "stw-quicknote");

	/* Layer shell: centered overlay */
	gtk_layer_init_for_window(state.window);
	gtk_layer_set_layer(state.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_anchor(state.window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(state.window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(state.window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(state.window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_keyboard_mode(state.window,
		GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_layer_set_namespace(state.window, "stw-quicknote");

	/* Full-screen overlay background */
	GtkWidget *overlay_bg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(overlay_bg, "note-overlay");
	gtk_widget_set_halign(overlay_bg, GTK_ALIGN_FILL);
	gtk_widget_set_valign(overlay_bg, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand(overlay_bg, TRUE);
	gtk_widget_set_vexpand(overlay_bg, TRUE);

	/* Centered card */
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_add_css_class(card, "note-card");
	gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(card, GTK_ALIGN_CENTER);

	/* Header row */
	GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(header_box, GTK_ALIGN_START);

	GtkWidget *header = gtk_label_new("QUICK NOTE");
	gtk_widget_add_css_class(header, "note-header");
	gtk_box_append(GTK_BOX(header_box), header);

	GtkWidget *dot = gtk_label_new("\302\267");
	gtk_widget_add_css_class(dot, "note-task");
	gtk_box_append(GTK_BOX(header_box), dot);

	char task_text[160];
	snprintf(task_text, sizeof(task_text), "saving to \"%s\"",
		state.current_task_name);
	state.task_label = gtk_label_new(task_text);
	gtk_widget_add_css_class(state.task_label, "note-task");
	gtk_box_append(GTK_BOX(header_box), state.task_label);

	gtk_box_append(GTK_BOX(card), header_box);

	/* Text entry */
	state.entry = gtk_entry_new();
	gtk_widget_add_css_class(state.entry, "note-entry");
	gtk_entry_set_placeholder_text(GTK_ENTRY(state.entry),
		"What's on your mind?");
	g_signal_connect(state.entry, "activate",
		G_CALLBACK(on_entry_activate), NULL);
	gtk_box_append(GTK_BOX(card), state.entry);

	/* Hint */
	state.hint_label = gtk_label_new(
		"Enter to save  \302\267  Esc to cancel");
	gtk_widget_add_css_class(state.hint_label, "note-hint");
	gtk_widget_set_halign(state.hint_label, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(card), state.hint_label);

	gtk_box_append(GTK_BOX(overlay_bg), card);
	gtk_window_set_child(state.window, overlay_bg);

	/* Keyboard controller for Escape */
	GtkEventController *key_ctrl = gtk_event_controller_key_new();
	g_signal_connect(key_ctrl, "key-pressed",
		G_CALLBACK(on_key_pressed), NULL);
	gtk_widget_add_controller(GTK_WIDGET(state.window), key_ctrl);

	gtk_window_present(state.window);

	/* Focus the entry */
	gtk_widget_grab_focus(state.entry);
}

int main(int argc, char *argv[]) {
	GtkApplication *app = gtk_application_new(
		"org.singlethread.quicknote", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
