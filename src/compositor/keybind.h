/*
 * SingleThread - Task-Centric Wayland Compositor
 * keybind.h - Keybinding dispatch
 */
#ifndef STW_KEYBIND_H
#define STW_KEYBIND_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct stw_server;

/* Try to handle a key press via keybinding; returns true if consumed */
bool stw_keybind_handle(struct stw_server *server,
	uint32_t modifiers, xkb_keysym_t keysym);

/* Execute an action string (e.g. "task:create", "window:close") */
bool stw_keybind_execute(struct stw_server *server, const char *action);

/* Spawn a command */
void stw_spawn(const char *command);

#endif /* STW_KEYBIND_H */
