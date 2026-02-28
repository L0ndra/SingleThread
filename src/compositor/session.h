/*
 * SingleThread - Task-Centric Wayland Compositor
 * session.h - Session persistence and restore
 */
#ifndef STW_SESSION_H
#define STW_SESSION_H

#include <stdbool.h>

struct stw_server;

/* Save current session state to disk */
bool stw_session_save(struct stw_server *server);

/* Restore session state from disk */
bool stw_session_restore(struct stw_server *server);

/* Get session file path */
char *stw_session_path(void);

/* Auto-save timer setup */
bool stw_session_start_autosave(struct stw_server *server, int interval_sec);
void stw_session_stop_autosave(struct stw_server *server);

#endif /* STW_SESSION_H */
