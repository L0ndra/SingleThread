/*
 * SingleThread - Task-Centric Wayland Compositor
 * notify.c - Beautiful notification daemon (layer-shell popups)
 *
 * Design: slide-in cards with app icon, colored accent strip,
 * progress bar support, dismiss-on-click, urgency-based styling,
 * smooth fade transitions.
 */
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ─── Urgency levels ───────────────────────────────────────────── */
typedef enum {
	URGENCY_LOW,
	URGENCY_NORMAL,
	URGENCY_CRITICAL,
} Urgency;

/* ─── Notification data ────────────────────────────────────────── */

typedef struct {
	guint id;
	char *summary;
	char *body;
	char *app_name;
	char *icon_name;
	int timeout;
	Urgency urgency;
	double progress; /* 0.0 - 1.0, negative = no progress bar */
	GtkWidget *card;
	GtkWidget *progress_bar;
	GtkWidget *time_label;
	guint timer_id;
	time_t created_at;
} Notification;

static GList *notifications = NULL;
static guint next_id = 1;
static GtkWidget *container = NULL;
static GtkWindow *overlay_window = NULL;

#define MAX_VISIBLE 5

/* ─── Time formatting ──────────────────────────────────────────── */

static const char *time_ago(time_t then) {
	static char buf[32];
	time_t now = time(NULL);
	int diff = (int)(now - then);
	if (diff < 5) {
		snprintf(buf, sizeof(buf), "just now");
	} else if (diff < 60) {
		snprintf(buf, sizeof(buf), "%ds ago", diff);
	} else if (diff < 3600) {
		snprintf(buf, sizeof(buf), "%dm ago", diff / 60);
	} else {
		snprintf(buf, sizeof(buf), "%dh ago", diff / 3600);
	}
	return buf;
}

/* ─── Update relative times ────────────────────────────────────── */

static gboolean update_times(gpointer data) {
	(void)data;
	for (GList *l = notifications; l; l = l->next) {
		Notification *n = l->data;
		if (n->time_label) {
			gtk_label_set_text(GTK_LABEL(n->time_label),
				time_ago(n->created_at));
		}
	}
	return G_SOURCE_CONTINUE;
}

/* ─── Dismiss ──────────────────────────────────────────────────── */

static void really_remove(gpointer data) {
	Notification *n = data;
	if (n->card && container) {
		gtk_box_remove(GTK_BOX(container), n->card);
	}
	notifications = g_list_remove(notifications, n);
	g_free(n->summary);
	g_free(n->body);
	g_free(n->app_name);
	g_free(n->icon_name);
	g_free(n);

	if (!notifications && overlay_window) {
		gtk_widget_set_visible(GTK_WIDGET(overlay_window), FALSE);
	}
}

static gboolean deferred_remove(gpointer data) {
	really_remove(data);
	return G_SOURCE_REMOVE;
}

static gboolean dismiss_notification(gpointer data) {
	Notification *n = data;
	n->timer_id = 0;

	/* Add fade-out class, then remove after animation */
	if (n->card) {
		gtk_widget_add_css_class(n->card, "notify-fade-out");
	}
	g_timeout_add(250, deferred_remove, n);

	return G_SOURCE_REMOVE;
}

static void on_dismiss_clicked(GtkGestureClick *gesture, int n_press,
		double x, double y, gpointer data) {
	(void)gesture; (void)n_press; (void)x; (void)y;
	Notification *n = data;
	if (n->timer_id) {
		g_source_remove(n->timer_id);
		n->timer_id = 0;
	}
	dismiss_notification(n);
}

/* ─── Show notification ────────────────────────────────────────── */

static void show_notification(const char *app_name, const char *icon_name,
		const char *summary, const char *body,
		int timeout, Urgency urgency, double progress) {
	Notification *n = g_new0(Notification, 1);
	n->id = next_id++;
	n->summary = g_strdup(summary ? summary : "");
	n->body = g_strdup(body ? body : "");
	n->app_name = g_strdup(app_name ? app_name : "");
	n->icon_name = g_strdup(icon_name ? icon_name : "");
	n->timeout = timeout > 0 ? timeout : 5000;
	n->urgency = urgency;
	n->progress = progress;
	n->created_at = time(NULL);

	/* ─── Build notification card ──────────────────────────── */
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(card, "notify-card");
	gtk_widget_set_size_request(card, 360, -1);

	switch (urgency) {
	case URGENCY_LOW:
		gtk_widget_add_css_class(card, "notify-low");
		break;
	case URGENCY_NORMAL:
		gtk_widget_add_css_class(card, "notify-normal");
		break;
	case URGENCY_CRITICAL:
		gtk_widget_add_css_class(card, "notify-critical");
		break;
	}

	/* Dismiss on click */
	GtkGesture *click = gtk_gesture_click_new();
	g_signal_connect(click, "pressed",
		G_CALLBACK(on_dismiss_clicked), n);
	gtk_widget_add_controller(card, GTK_EVENT_CONTROLLER(click));

	/* Accent strip */
	GtkWidget *accent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(accent, "notify-accent");
	gtk_box_append(GTK_BOX(card), accent);

	/* Content */
	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start(content, 12);
	gtk_widget_set_margin_end(content, 12);
	gtk_widget_set_margin_top(content, 10);
	gtk_widget_set_margin_bottom(content, 10);
	gtk_widget_set_hexpand(content, TRUE);

	/* Header: icon + app name + time + close */
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

	GtkWidget *icon_widget;
	if (icon_name && *icon_name) {
		icon_widget = gtk_image_new_from_icon_name(icon_name);
	} else {
		icon_widget = gtk_image_new_from_icon_name(
			"dialog-information");
	}
	gtk_image_set_pixel_size(GTK_IMAGE(icon_widget), 16);
	gtk_widget_add_css_class(icon_widget, "notify-icon");
	gtk_box_append(GTK_BOX(header), icon_widget);

	if (app_name && *app_name) {
		GtkWidget *app_label = gtk_label_new(app_name);
		gtk_widget_add_css_class(app_label, "notify-app");
		gtk_label_set_xalign(GTK_LABEL(app_label), 0.0);
		gtk_widget_set_hexpand(app_label, TRUE);
		gtk_box_append(GTK_BOX(header), app_label);
	} else {
		GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_hexpand(spacer, TRUE);
		gtk_box_append(GTK_BOX(header), spacer);
	}

	n->time_label = gtk_label_new("just now");
	gtk_widget_add_css_class(n->time_label, "notify-time");
	gtk_box_append(GTK_BOX(header), n->time_label);

	GtkWidget *close_label = gtk_label_new("\303\227");
	gtk_widget_add_css_class(close_label, "notify-close");
	gtk_box_append(GTK_BOX(header), close_label);

	gtk_box_append(GTK_BOX(content), header);

	/* Summary */
	GtkWidget *sum_label = gtk_label_new(summary);
	gtk_widget_add_css_class(sum_label, "notify-summary");
	gtk_label_set_xalign(GTK_LABEL(sum_label), 0.0);
	gtk_label_set_wrap(GTK_LABEL(sum_label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(sum_label), 40);
	gtk_box_append(GTK_BOX(content), sum_label);

	/* Body */
	if (body && *body) {
		GtkWidget *body_label = gtk_label_new(body);
		gtk_widget_add_css_class(body_label, "notify-body");
		gtk_label_set_xalign(GTK_LABEL(body_label), 0.0);
		gtk_label_set_wrap(GTK_LABEL(body_label), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(body_label), 45);
		gtk_box_append(GTK_BOX(content), body_label);
	}

	/* Progress bar */
	if (progress >= 0.0) {
		n->progress_bar = gtk_progress_bar_new();
		gtk_progress_bar_set_fraction(
			GTK_PROGRESS_BAR(n->progress_bar), progress);
		gtk_widget_add_css_class(n->progress_bar, "notify-progress");
		gtk_widget_set_margin_top(n->progress_bar, 4);
		gtk_box_append(GTK_BOX(content), n->progress_bar);
	}

	gtk_box_append(GTK_BOX(card), content);
	n->card = card;

	/* Slide in */
	gtk_widget_add_css_class(card, "notify-slide-in");
	gtk_box_prepend(GTK_BOX(container), card);
	notifications = g_list_prepend(notifications, n);

	gtk_widget_set_visible(GTK_WIDGET(overlay_window), TRUE);

	/* Auto-dismiss (not for critical) */
	if (urgency != URGENCY_CRITICAL) {
		n->timer_id = g_timeout_add(n->timeout,
			dismiss_notification, n);
	}

	/* Remove oldest */
	while (g_list_length(notifications) > MAX_VISIBLE) {
		GList *last = g_list_last(notifications);
		if (last) {
			Notification *old = last->data;
			if (old->timer_id) {
				g_source_remove(old->timer_id);
				old->timer_id = 0;
			}
			dismiss_notification(old);
		}
	}
}

/* ─── CSS ──────────────────────────────────────────────────────── */

static const char *notify_css =
	"window {"
	"  background-color: transparent;"
	"}"

	".notify-card {"
	"  background-color: rgba(26, 27, 38, 0.96);"
	"  border: 1px solid rgba(86, 95, 137, 0.25);"
	"  border-radius: 12px;"
	"  margin: 4px 0;"
	"  color: #c0caf5;"
	"  font-family: 'Inter', 'Cantarell', 'Noto Sans', sans-serif;"
	"  transition: opacity 300ms ease, margin-top 300ms ease;"
	"}"
	".notify-card:hover {"
	"  border-color: rgba(122, 162, 247, 0.3);"
	"}"

	".notify-normal .notify-accent {"
	"  min-width: 4px; border-radius: 2px;"
	"  margin: 6px 0 6px 6px; background-color: #7aa2f7;"
	"}"
	".notify-low .notify-accent {"
	"  min-width: 4px; border-radius: 2px;"
	"  margin: 6px 0 6px 6px; background-color: #565f89;"
	"}"
	".notify-critical .notify-accent {"
	"  min-width: 4px; border-radius: 2px;"
	"  margin: 6px 0 6px 6px; background-color: #f7768e;"
	"}"
	".notify-critical {"
	"  border-color: rgba(247, 118, 142, 0.3);"
	"}"

	".notify-icon { opacity: 0.7; }"
	".notify-app {"
	"  font-size: 11px; font-weight: 600;"
	"  color: rgba(169, 177, 214, 0.5);"
	"  text-transform: uppercase; letter-spacing: 0.5px;"
	"}"
	".notify-time {"
	"  font-size: 10px; color: rgba(86, 95, 137, 0.6);"
	"  font-family: 'JetBrains Mono', monospace; padding-right: 4px;"
	"}"
	".notify-close {"
	"  font-size: 14px; color: rgba(86, 95, 137, 0.3);"
	"  padding: 0 2px; transition: color 150ms ease;"
	"}"
	".notify-card:hover .notify-close {"
	"  color: rgba(169, 177, 214, 0.6);"
	"}"

	".notify-summary {"
	"  font-size: 13.5px; font-weight: 600;"
	"  color: #e0e8ff; margin-top: 2px;"
	"}"
	".notify-body {"
	"  font-size: 12px; color: rgba(169, 177, 214, 0.7);"
	"  margin-top: 1px;"
	"}"

	".notify-progress { min-height: 4px; border-radius: 2px; }"
	".notify-progress trough {"
	"  min-height: 4px; border-radius: 2px;"
	"  background-color: rgba(36, 40, 59, 0.8);"
	"}"
	".notify-progress progress {"
	"  min-height: 4px; border-radius: 2px;"
	"  background-color: #7aa2f7;"
	"}"
	".notify-critical .notify-progress progress {"
	"  background-color: #f7768e;"
	"}"

	".notify-slide-in {"
	"  animation: slide-in 300ms ease-out;"
	"}"
	"@keyframes slide-in {"
	"  from { opacity: 0; margin-top: -20px; }"
	"  to { opacity: 1; margin-top: 4px; }"
	"}"
	".notify-fade-out {"
	"  opacity: 0; margin-top: -40px;"
	"  transition: opacity 200ms ease-in, margin-top 200ms ease-in;"
	"}";

/* ─── Activation ───────────────────────────────────────────────── */

static void show_demo_low(void) {
	show_notification("System", "software-update-available",
		"System Update Available",
		"3 packages can be upgraded.",
		8000, URGENCY_LOW, 0.72);
}

static gboolean show_demo_delayed(gpointer data) {
	(void)data;
	show_demo_low();
	return G_SOURCE_REMOVE;
}

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
	gtk_layer_set_anchor(overlay_window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(overlay_window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_margin(overlay_window, GTK_LAYER_SHELL_EDGE_TOP, 44);
	gtk_layer_set_margin(overlay_window, GTK_LAYER_SHELL_EDGE_RIGHT, 12);
	gtk_layer_set_namespace(overlay_window, "stw-notify");
	gtk_layer_set_exclusive_zone(overlay_window, -1);

	container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_window_set_child(overlay_window, container);

	g_timeout_add(10000, update_times, NULL);

	/* Demo notifications */
	show_notification("SingleThread", "dialog-information",
		"Welcome to SingleThread",
		"Your task-centric workspace is ready. "
		"Press Super+D to launch apps.",
		6000, URGENCY_NORMAL, -1.0);

	g_timeout_add(800, show_demo_delayed, NULL);
}

int main(int argc, char *argv[]) {
	GtkApplication *app = gtk_application_new(
		"org.singlethread.notify", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
