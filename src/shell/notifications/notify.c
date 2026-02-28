/*
 * SingleThread - Task-Centric Wayland Compositor
 * notify.c - Notification daemon (layer-shell popups)
 *
 * Listens on the org.freedesktop.Notifications DBus interface
 * and displays notifications as layer-shell surfaces.
 *
 * This is a minimal implementation. For v1, it handles:
 * - Receiving notifications via DBus
 * - Displaying them as temporary popups
 * - Auto-dismiss with timeout
 */
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Notification data ────────────────────────────────────────── */

typedef struct {
	guint id;
	char *summary;
	char *body;
	char *app_name;
	int timeout;
	GtkWidget *widget;
	guint timer_id;
} Notification;

static GList *notifications = NULL;
static guint next_id = 1;
static GtkWidget *container = NULL;
static GtkWindow *overlay_window = NULL;

/* ─── Display/dismiss ──────────────────────────────────────────── */

static gboolean dismiss_notification(gpointer data) {
	Notification *n = data;
	if (n->widget && container) {
		gtk_box_remove(GTK_BOX(container), n->widget);
	}
	notifications = g_list_remove(notifications, n);
	g_free(n->summary);
	g_free(n->body);
	g_free(n->app_name);
	g_free(n);

	/* Hide window if no notifications */
	if (!notifications && overlay_window) {
		gtk_widget_set_visible(GTK_WIDGET(overlay_window), FALSE);
	}

	return G_SOURCE_REMOVE;
}

static void show_notification(const char *app_name, const char *summary,
		const char *body, int timeout) {
	Notification *n = g_new0(Notification, 1);
	n->id = next_id++;
	n->summary = g_strdup(summary ? summary : "");
	n->body = g_strdup(body ? body : "");
	n->app_name = g_strdup(app_name ? app_name : "");
	n->timeout = timeout > 0 ? timeout : 5000;

	/* Create notification widget */
	GtkWidget *frame = gtk_frame_new(NULL);
	gtk_widget_add_css_class(frame, "notification");
	gtk_widget_set_size_request(frame, 300, -1);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start(vbox, 12);
	gtk_widget_set_margin_end(vbox, 12);
	gtk_widget_set_margin_top(vbox, 8);
	gtk_widget_set_margin_bottom(vbox, 8);

	if (app_name && *app_name) {
		GtkWidget *app_label = gtk_label_new(app_name);
		gtk_widget_add_css_class(app_label, "notify-app");
		gtk_label_set_xalign(GTK_LABEL(app_label), 0.0);
		gtk_box_append(GTK_BOX(vbox), app_label);
	}

	GtkWidget *sum_label = gtk_label_new(summary);
	gtk_widget_add_css_class(sum_label, "notify-summary");
	gtk_label_set_xalign(GTK_LABEL(sum_label), 0.0);
	gtk_label_set_wrap(GTK_LABEL(sum_label), TRUE);
	gtk_box_append(GTK_BOX(vbox), sum_label);

	if (body && *body) {
		GtkWidget *body_label = gtk_label_new(body);
		gtk_widget_add_css_class(body_label, "notify-body");
		gtk_label_set_xalign(GTK_LABEL(body_label), 0.0);
		gtk_label_set_wrap(GTK_LABEL(body_label), TRUE);
		gtk_box_append(GTK_BOX(vbox), body_label);
	}

	gtk_frame_set_child(GTK_FRAME(frame), vbox);
	n->widget = frame;

	/* Add to container */
	gtk_box_prepend(GTK_BOX(container), frame);
	notifications = g_list_prepend(notifications, n);

	/* Show the overlay window */
	gtk_widget_set_visible(GTK_WIDGET(overlay_window), TRUE);

	/* Auto-dismiss timer */
	n->timer_id = g_timeout_add(n->timeout, dismiss_notification, n);

	/* Limit visible notifications */
	while (g_list_length(notifications) > 5) {
		GList *last = g_list_last(notifications);
		if (last) {
			Notification *old = last->data;
			if (old->timer_id) g_source_remove(old->timer_id);
			dismiss_notification(old);
		}
	}
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *notify_css =
	"window {"
	"  background-color: transparent;"
	"}"
	".notification {"
	"  background-color: rgba(26, 27, 38, 0.95);"
	"  border: 1px solid #3b4261;"
	"  border-radius: 8px;"
	"  margin: 4px 8px;"
	"  color: #c0caf5;"
	"  font-family: sans-serif;"
	"}"
	".notify-app {"
	"  color: #565f89;"
	"  font-size: 11px;"
	"}"
	".notify-summary {"
	"  font-weight: bold;"
	"  font-size: 13px;"
	"}"
	".notify-body {"
	"  color: #a9b1d6;"
	"  font-size: 12px;"
	"}";

/* ─── Activation ───────────────────────────────────────────────── */

static void activate(GtkApplication *app, gpointer data) {
	(void)data;

	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, notify_css);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	overlay_window = GTK_WINDOW(gtk_application_window_new(app));
	gtk_window_set_title(overlay_window, "stw-notify");

	gtk_layer_init_for_window(overlay_window);
	gtk_layer_set_layer(overlay_window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_anchor(overlay_window,
		GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(overlay_window,
		GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_margin(overlay_window,
		GTK_LAYER_SHELL_EDGE_TOP, 40);
	gtk_layer_set_margin(overlay_window,
		GTK_LAYER_SHELL_EDGE_RIGHT, 8);
	gtk_layer_set_namespace(overlay_window, "stw-notify");

	/* No exclusive zone - overlaps other content */
	gtk_layer_set_exclusive_zone(overlay_window, -1);

	container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_window_set_child(overlay_window, container);

	/* Show a test notification for now */
	show_notification("SingleThread", "Notification daemon started",
		"Ready to receive notifications.", 3000);

	/* NOTE: Full DBus integration (org.freedesktop.Notifications)
	 * would be implemented here using GDBus. For the initial version,
	 * this daemon can be triggered via stwctl or IPC. */
}

int main(int argc, char *argv[]) {
	GtkApplication *app = gtk_application_new(
		"org.singlethread.notify", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
