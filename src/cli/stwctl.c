/*
 * SingleThread - Task-Centric Wayland Compositor
 * stwctl.c - CLI tool for IPC control
 *
 * Usage:
 *   stwctl task list
 *   stwctl task create [name]
 *   stwctl task switch <id|name>
 *   stwctl task rename <id> <new-name>
 *   stwctl task close <id>
 *   stwctl window list
 *   stwctl window close
 *   stwctl window to-task <task-id>
 *   stwctl window toggle-global
 *   stwctl window toggle-floating
 *   stwctl get active-task
 *   stwctl get active-window
 *   stwctl get version
 *   stwctl reload
 *   stwctl launch <command>
 *   stwctl exit
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <json-c/json.h>

static int connect_ipc(void) {
	const char *socket_path = getenv("SINGLETHREAD_SOCKET");
	if (!socket_path) {
		fprintf(stderr, "Error: SINGLETHREAD_SOCKET not set. "
			"Is SingleThread running?\n");
		return -1;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Error: Cannot connect to SingleThread at %s: %s\n",
			socket_path, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

static int send_message(int fd, const char *json_str) {
	uint32_t len = (uint32_t)strlen(json_str);
	if (write(fd, &len, 4) < 0) return -1;
	if (write(fd, json_str, len) < 0) return -1;
	return 0;
}

static char *recv_message(int fd) {
	uint32_t len;
	ssize_t n = read(fd, &len, 4);
	if (n != 4) return NULL;

	if (len > 10 * 1024 * 1024) {
		fprintf(stderr, "Response too large\n");
		return NULL;
	}

	char *buf = malloc(len + 1);
	if (!buf) return NULL;

	size_t total = 0;
	while (total < len) {
		n = read(fd, buf + total, len - total);
		if (n <= 0) {
			free(buf);
			return NULL;
		}
		total += n;
	}
	buf[len] = '\0';
	return buf;
}

static void print_json_pretty(const char *json_str) {
	struct json_object *obj = json_tokener_parse(json_str);
	if (obj) {
		printf("%s\n", json_object_to_json_string_ext(obj,
			JSON_C_TO_STRING_PRETTY));
		json_object_put(obj);
	} else {
		printf("%s\n", json_str);
	}
}

static int do_command(const char *json_str) {
	int fd = connect_ipc();
	if (fd < 0) return 1;

	if (send_message(fd, json_str) < 0) {
		perror("send");
		close(fd);
		return 1;
	}

	char *response = recv_message(fd);
	close(fd);

	if (!response) {
		fprintf(stderr, "Error: No response from compositor\n");
		return 1;
	}

	/* Check for success */
	struct json_object *resp = json_tokener_parse(response);
	int ret = 0;
	if (resp) {
		struct json_object *success;
		if (json_object_object_get_ex(resp, "success", &success)) {
			if (!json_object_get_boolean(success)) {
				struct json_object *error;
				if (json_object_object_get_ex(resp, "error", &error)) {
					fprintf(stderr, "Error: %s\n",
						json_object_get_string(error));
				}
				ret = 1;
			}
		}
		json_object_put(resp);
	}

	/* Print the response */
	print_json_pretty(response);
	free(response);
	return ret;
}

static void usage(void) {
	fprintf(stderr,
		"Usage: stwctl <category> <command> [args...]\n"
		"\n"
		"Categories:\n"
		"  task      - Task management\n"
		"  window    - Window management\n"
		"  get       - Query state\n"
		"  reload    - Reload configuration\n"
		"  launch    - Launch application\n"
		"  exit      - Exit compositor\n"
		"  version   - Show version\n"
		"\n"
		"Task commands:\n"
		"  stwctl task list\n"
		"  stwctl task create [name]\n"
		"  stwctl task switch <id|name>\n"
		"  stwctl task rename <id> <new-name>\n"
		"  stwctl task close <id>\n"
		"\n"
		"Window commands:\n"
		"  stwctl window list\n"
		"  stwctl window close\n"
		"  stwctl window to-task <task-id>\n"
		"  stwctl window toggle-global\n"
		"  stwctl window toggle-floating\n"
		"\n"
		"Query commands:\n"
		"  stwctl get active-task\n"
		"  stwctl get active-window\n"
		"  stwctl get version\n"
		"\n"
		"General:\n"
		"  stwctl reload\n"
		"  stwctl launch <command>\n"
		"  stwctl exit\n"
	);
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		usage();
		return 1;
	}

	char json_buf[4096];
	const char *category = argv[1];

	/* ─── Task commands ────────────────────────────────────────── */
	if (strcmp(category, "task") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: stwctl task <command>\n");
			return 1;
		}
		const char *cmd = argv[2];

		if (strcmp(cmd, "list") == 0) {
			return do_command("{\"command\": \"task/list\"}");
		}

		if (strcmp(cmd, "create") == 0) {
			const char *name = argc > 3 ? argv[3] : "New Task";
			snprintf(json_buf, sizeof(json_buf),
				"{\"command\": \"task/create\", \"name\": \"%s\"}", name);
			return do_command(json_buf);
		}

		if (strcmp(cmd, "switch") == 0) {
			if (argc < 4) {
				fprintf(stderr, "Usage: stwctl task switch <id|name>\n");
				return 1;
			}
			/* Try as numeric ID first, then as name */
			char *endptr;
			long id = strtol(argv[3], &endptr, 10);
			if (*endptr == '\0') {
				snprintf(json_buf, sizeof(json_buf),
					"{\"command\": \"task/switch\", \"id\": %ld}", id);
			} else {
				snprintf(json_buf, sizeof(json_buf),
					"{\"command\": \"task/switch\", \"name\": \"%s\"}",
					argv[3]);
			}
			return do_command(json_buf);
		}

		if (strcmp(cmd, "rename") == 0) {
			if (argc < 5) {
				fprintf(stderr,
					"Usage: stwctl task rename <id> <new-name>\n");
				return 1;
			}
			snprintf(json_buf, sizeof(json_buf),
				"{\"command\": \"task/rename\", \"id\": %s, \"name\": \"%s\"}",
				argv[3], argv[4]);
			return do_command(json_buf);
		}

		if (strcmp(cmd, "close") == 0) {
			if (argc < 4) {
				fprintf(stderr, "Usage: stwctl task close <id>\n");
				return 1;
			}
			snprintf(json_buf, sizeof(json_buf),
				"{\"command\": \"task/close\", \"id\": %s}", argv[3]);
			return do_command(json_buf);
		}

		fprintf(stderr, "Unknown task command: %s\n", cmd);
		return 1;
	}

	/* ─── Window commands ──────────────────────────────────────── */
	if (strcmp(category, "window") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: stwctl window <command>\n");
			return 1;
		}
		const char *cmd = argv[2];

		if (strcmp(cmd, "list") == 0) {
			return do_command("{\"command\": \"window/list\"}");
		}

		if (strcmp(cmd, "close") == 0) {
			return do_command("{\"command\": \"window/close\"}");
		}

		if (strcmp(cmd, "to-task") == 0) {
			if (argc < 4) {
				fprintf(stderr,
					"Usage: stwctl window to-task <task-id>\n");
				return 1;
			}
			snprintf(json_buf, sizeof(json_buf),
				"{\"command\": \"window/to-task\", \"task_id\": %s}",
				argv[3]);
			return do_command(json_buf);
		}

		if (strcmp(cmd, "toggle-global") == 0) {
			return do_command("{\"command\": \"window/toggle-global\"}");
		}

		if (strcmp(cmd, "toggle-floating") == 0) {
			return do_command(
				"{\"command\": \"window/toggle-floating\"}");
		}

		fprintf(stderr, "Unknown window command: %s\n", cmd);
		return 1;
	}

	/* ─── Query commands ───────────────────────────────────────── */
	if (strcmp(category, "get") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: stwctl get <query>\n");
			return 1;
		}
		const char *query = argv[2];

		if (strcmp(query, "active-task") == 0) {
			return do_command("{\"command\": \"get/active-task\"}");
		}
		if (strcmp(query, "active-window") == 0) {
			return do_command("{\"command\": \"get/active-window\"}");
		}
		if (strcmp(query, "version") == 0) {
			return do_command("{\"command\": \"get/version\"}");
		}

		fprintf(stderr, "Unknown query: %s\n", query);
		return 1;
	}

	/* ─── General commands ─────────────────────────────────────── */
	if (strcmp(category, "reload") == 0) {
		return do_command("{\"command\": \"reload-config\"}");
	}

	if (strcmp(category, "launch") == 0) {
		if (argc < 3) {
			fprintf(stderr, "Usage: stwctl launch <command>\n");
			return 1;
		}
		snprintf(json_buf, sizeof(json_buf),
			"{\"command\": \"launch\", \"exec\": \"%s\"}", argv[2]);
		return do_command(json_buf);
	}

	if (strcmp(category, "exit") == 0) {
		return do_command("{\"command\": \"exit\"}");
	}

	if (strcmp(category, "version") == 0) {
		return do_command("{\"command\": \"get/version\"}");
	}

	if (strcmp(category, "-h") == 0 || strcmp(category, "--help") == 0 ||
			strcmp(category, "help") == 0) {
		usage();
		return 0;
	}

	fprintf(stderr, "Unknown category: %s\n", category);
	usage();
	return 1;
}
