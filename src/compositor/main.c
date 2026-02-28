/*
 * SingleThread - Task-Centric Wayland Compositor
 * main.c - Entry point
 */
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>

#include "server.h"
#include "stw_config.h"

static void usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  -c, --config <path>   Configuration file path\n"
		"  -d, --debug           Enable debug logging\n"
		"  -v, --version         Show version and exit\n"
		"  -h, --help            Show this help\n"
		"\n"
		"SingleThread v%s - Task-Centric Wayland Compositor\n",
		prog, STW_VERSION);
}

static void handle_signal(int sig) {
	(void)sig;
	/* Handled by the event loop */
}

int main(int argc, char *argv[]) {
	enum wlr_log_importance log_level = WLR_INFO;
	char *config_path = NULL;

	static const struct option long_options[] = {
		{"config",  required_argument, NULL, 'c'},
		{"debug",   no_argument,       NULL, 'd'},
		{"version", no_argument,       NULL, 'v'},
		{"help",    no_argument,       NULL, 'h'},
		{NULL, 0, NULL, 0},
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "c:dvh", long_options, NULL)) != -1) {
		switch (opt) {
		case 'c':
			config_path = strdup(optarg);
			break;
		case 'd':
			log_level = WLR_DEBUG;
			break;
		case 'v':
			printf("SingleThread v%s\n", STW_VERSION);
			return 0;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	wlr_log_init(log_level, NULL);
	wlr_log(WLR_INFO, "SingleThread v%s starting", STW_VERSION);

	/* Set up signal handlers */
	struct sigaction sa = {0};
	sa.sa_handler = handle_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	/* Load configuration */
	if (!config_path) {
		config_path = stw_config_find_path();
	}

	struct stw_server server = {0};
	server.config = stw_config_create();

	if (config_path) {
		wlr_log(WLR_INFO, "Loading config from: %s", config_path);
		if (!stw_config_load(server.config, config_path)) {
			wlr_log(WLR_ERROR, "Failed to load config, using defaults");
		}
		server.config_path = config_path;
	} else {
		wlr_log(WLR_INFO, "No config file found, using defaults");
		stw_config_defaults(server.config);
	}

	/* Initialize server */
	if (!stw_server_init(&server)) {
		wlr_log(WLR_ERROR, "Failed to initialize server");
		stw_config_destroy(server.config);
		free(config_path);
		return 1;
	}

	/* Run the compositor */
	stw_server_run(&server);

	/* Cleanup */
	stw_server_finish(&server);
	stw_config_destroy(server.config);

	wlr_log(WLR_INFO, "SingleThread shutting down cleanly");
	return 0;
}
