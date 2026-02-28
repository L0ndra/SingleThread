/*
 * SingleThread - Task-Centric Wayland Compositor
 * xwayland.h - XWayland surface management
 */
#ifndef STW_XWAYLAND_H
#define STW_XWAYLAND_H

#include "config.h"

#if STW_HAS_XWAYLAND

#include <wlr/xwayland.h>

struct stw_server;

void stw_xwayland_handle_new_surface(struct stw_server *server,
	struct wlr_xwayland_surface *surface);

#endif /* STW_HAS_XWAYLAND */
#endif /* STW_XWAYLAND_H */
