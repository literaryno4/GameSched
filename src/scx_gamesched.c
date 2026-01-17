/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scx_gamesched - Userspace loader and CLI
 *
 * Commands:
 *   scx_gamesched                    - Run scheduler with defaults
 *   scx_gamesched add --pid PID --priority render|game
 *   scx_gamesched remove --pid PID
 *   scx_gamesched isolate --cpus 2,3
 *   scx_gamesched pin --pid PID --cpu N
 *   scx_gamesched status
 *
 * Copyright (c) 2026 GameSched Project
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <errno.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_gamesched.h"
#include "scx_gamesched.bpf.skel.h"

static const char help_fmt[] =
"scx_gamesched - A gaming-optimized sched_ext scheduler\n"
"\n"
"Usage: %s [command] [options]\n"
"\n"
"Commands:\n"
"  (none)                      Run the scheduler\n"
"  add --pid PID --priority PRIO   Add game thread (PRIO: render, game)\n"
"  remove --pid PID            Remove game thread\n"
"  isolate --cpus CPU_LIST     Isolate CPUs (e.g., 2,3)\n"
"  isolate --clear             Clear CPU isolation\n"
"  pin --pid PID --cpu CPU     Pin thread to CPU\n"
"  status                      Show current configuration\n"
"\n"
"Options:\n"
"  -i            Enable CPU isolation mode\n"
"  -v            Verbose output\n"
"  -h            Display this help\n";

static volatile int exit_req;
static bool verbose;

static void sigint_handler(int sig)
{
	exit_req = 1;
}

static int libbpf_print_fn(enum libbpf_print_level level,
			   const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

/*
 * Parse a comma-separated list of CPU IDs.
 * Returns the number of CPUs parsed, or -1 on error.
 */
static int parse_cpu_list(const char *str, int *cpus, int max_cpus)
{
	char *copy, *token, *saveptr;
	int count = 0;

	copy = strdup(str);
	if (!copy)
		return -1;

	token = strtok_r(copy, ",", &saveptr);
	while (token && count < max_cpus) {
		cpus[count++] = atoi(token);
		token = strtok_r(NULL, ",", &saveptr);
	}

	free(copy);
	return count;
}

/*
 * Add a game thread to the scheduler.
 */
static int cmd_add(struct scx_gamesched *skel, int pid, const char *priority)
{
	int game_threads_fd = bpf_map__fd(skel->maps.game_threads);
	u32 prio;
	u32 key = pid;

	if (strcmp(priority, "render") == 0)
		prio = PRIO_GAME_RENDER;
	else if (strcmp(priority, "game") == 0)
		prio = PRIO_GAME_OTHER;
	else {
		fprintf(stderr, "Invalid priority: %s (use 'render' or 'game')\n",
			priority);
		return -1;
	}

	if (bpf_map_update_elem(game_threads_fd, &key, &prio, BPF_ANY) < 0) {
		fprintf(stderr, "Failed to add PID %d: %s\n", pid, strerror(errno));
		return -1;
	}

	printf("Added PID %d with priority '%s'\n", pid, priority);
	return 0;
}

/*
 * Remove a game thread from the scheduler.
 */
static int cmd_remove(struct scx_gamesched *skel, int pid)
{
	int game_threads_fd = bpf_map__fd(skel->maps.game_threads);
	int pinned_threads_fd = bpf_map__fd(skel->maps.pinned_threads);
	u32 key = pid;

	bpf_map_delete_elem(game_threads_fd, &key);
	bpf_map_delete_elem(pinned_threads_fd, &key);

	printf("Removed PID %d\n", pid);
	return 0;
}

/*
 * Set CPU isolation.
 */
static int cmd_isolate(struct scx_gamesched *skel, const char *cpu_list)
{
	int isolated_cpus_fd = bpf_map__fd(skel->maps.isolated_cpus);
	int cpus[MAX_CPUS];
	int count, i;
	u32 value = 1;

	if (strcmp(cpu_list, "--clear") == 0 || strcmp(cpu_list, "clear") == 0) {
		/* Clear all isolation */
		value = 0;
		for (i = 0; i < MAX_CPUS; i++) {
			u32 key = i;
			bpf_map_update_elem(isolated_cpus_fd, &key, &value, BPF_ANY);
		}
		printf("Cleared CPU isolation\n");
		return 0;
	}

	count = parse_cpu_list(cpu_list, cpus, MAX_CPUS);
	if (count < 0) {
		fprintf(stderr, "Failed to parse CPU list\n");
		return -1;
	}

	for (i = 0; i < count; i++) {
		u32 key = cpus[i];
		if (bpf_map_update_elem(isolated_cpus_fd, &key, &value, BPF_ANY) < 0) {
			fprintf(stderr, "Failed to isolate CPU %d: %s\n",
				cpus[i], strerror(errno));
			return -1;
		}
	}

	printf("Isolated CPUs: %s\n", cpu_list);
	return 0;
}

/*
 * Pin a thread to a specific CPU.
 */
static int cmd_pin(struct scx_gamesched *skel, int pid, int cpu)
{
	int pinned_threads_fd = bpf_map__fd(skel->maps.pinned_threads);
	u32 key = pid;
	s32 value = cpu;

	if (bpf_map_update_elem(pinned_threads_fd, &key, &value, BPF_ANY) < 0) {
		fprintf(stderr, "Failed to pin PID %d to CPU %d: %s\n",
			pid, cpu, strerror(errno));
		return -1;
	}

	printf("Pinned PID %d to CPU %d\n", pid, cpu);
	return 0;
}

/*
 * Show current status.
 */
static int cmd_status(struct scx_gamesched *skel)
{
	int game_threads_fd = bpf_map__fd(skel->maps.game_threads);
	int isolated_cpus_fd = bpf_map__fd(skel->maps.isolated_cpus);
	int pinned_threads_fd = bpf_map__fd(skel->maps.pinned_threads);
	u32 key, next_key;
	u32 prio;
	s32 cpu;
	u32 isolated;
	int i;

	printf("=== GameSched Status ===\n\n");

	/* Game threads */
	printf("Game Threads:\n");
	key = 0;
	while (bpf_map_get_next_key(game_threads_fd, &key, &next_key) == 0) {
		if (bpf_map_lookup_elem(game_threads_fd, &next_key, &prio) == 0) {
			const char *prio_str = prio == PRIO_GAME_RENDER ? "render" :
					       prio == PRIO_GAME_OTHER ? "game" : "normal";
			printf("  PID %u: priority=%s", next_key, prio_str);

			/* Check if pinned */
			if (bpf_map_lookup_elem(pinned_threads_fd, &next_key, &cpu) == 0 && cpu >= 0)
				printf(" (pinned to CPU %d)", cpu);
			printf("\n");
		}
		key = next_key;
	}

	/* Isolated CPUs */
	printf("\nIsolated CPUs: ");
	int first = 1;
	for (i = 0; i < MAX_CPUS && i < 64; i++) {  /* Limit to 64 for display */
		u32 cpu_key = i;
		if (bpf_map_lookup_elem(isolated_cpus_fd, &cpu_key, &isolated) == 0 && isolated) {
			if (!first) printf(",");
			printf("%d", i);
			first = 0;
		}
	}
	if (first)
		printf("(none)");
	printf("\n");

	/* Statistics */
	printf("\nStatistics:\n");
	printf("  Game dispatches:     %lu\n", skel->bss->nr_game_dispatched);
	printf("  Normal dispatches:   %lu\n", skel->bss->nr_normal_dispatched);
	printf("  Isolation redirects: %lu\n", skel->bss->nr_isolated_violations);

	return 0;
}

/*
 * Run the scheduler main loop.
 */
static int run_scheduler(struct scx_gamesched *skel)
{
	struct bpf_link *link;

	link = SCX_OPS_ATTACH(skel, gamesched_ops, scx_gamesched);

	printf("GameSched running. Press Ctrl+C to exit.\n");
	printf("Use 'scx_gamesched add --pid PID --priority render' to add game threads.\n\n");

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		printf("game=%lu normal=%lu isolated_redirects=%lu\n",
		       skel->bss->nr_game_dispatched,
		       skel->bss->nr_normal_dispatched,
		       skel->bss->nr_isolated_violations);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	return 0;
}

int main(int argc, char **argv)
{
	struct scx_gamesched *skel;
	int opt;
	bool isolation_mode = false;
	const char *cmd = NULL;
	int cmd_argc = 0;
	char **cmd_argv = NULL;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	skel = SCX_OPS_OPEN(gamesched_ops, scx_gamesched);

	/* Check if first non-option arg is a subcommand */
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			/* Found a command */
			cmd = argv[i];
			cmd_argc = argc - i;
			cmd_argv = &argv[i];
			argc = i;  /* Limit getopt to args before command */
			break;
		}
	}

	/* Parse global options (before command) */
	while ((opt = getopt(argc, argv, "ivh")) != -1) {
		switch (opt) {
		case 'i':
			isolation_mode = true;
			break;
		case 'v':
			verbose = true;
			break;
		case 'h':
			fprintf(stdout, help_fmt, basename(argv[0]));
			return 0;
		}
	}

	skel->rodata->isolation_enabled = isolation_mode;

	SCX_OPS_LOAD(skel, gamesched_ops, scx_gamesched, uei);

	/* Handle subcommands */
	if (cmd) {
		if (strcmp(cmd, "add") == 0) {
			int pid = 0;
			const char *priority = NULL;

			for (int i = 1; i < cmd_argc; i++) {
				if (strcmp(cmd_argv[i], "--pid") == 0 && i + 1 < cmd_argc)
					pid = atoi(cmd_argv[++i]);
				else if (strcmp(cmd_argv[i], "--priority") == 0 && i + 1 < cmd_argc)
					priority = cmd_argv[++i];
			}

			if (pid <= 0 || !priority) {
				fprintf(stderr, "Usage: %s add --pid PID --priority render|game\n",
					basename(argv[0]));
				return 1;
			}
			return cmd_add(skel, pid, priority);

		} else if (strcmp(cmd, "remove") == 0) {
			int pid = 0;

			for (int i = 1; i < cmd_argc; i++) {
				if (strcmp(cmd_argv[i], "--pid") == 0 && i + 1 < cmd_argc)
					pid = atoi(cmd_argv[++i]);
			}

			if (pid <= 0) {
				fprintf(stderr, "Usage: %s remove --pid PID\n",
					basename(argv[0]));
				return 1;
			}
			return cmd_remove(skel, pid);

		} else if (strcmp(cmd, "isolate") == 0) {
			const char *cpu_list = NULL;

			for (int i = 1; i < cmd_argc; i++) {
				if (strcmp(cmd_argv[i], "--cpus") == 0 && i + 1 < cmd_argc)
					cpu_list = cmd_argv[++i];
				else if (strcmp(cmd_argv[i], "--clear") == 0)
					cpu_list = "clear";
			}

			if (!cpu_list) {
				fprintf(stderr, "Usage: %s isolate --cpus CPU_LIST | --clear\n",
					basename(argv[0]));
				return 1;
			}
			return cmd_isolate(skel, cpu_list);

		} else if (strcmp(cmd, "pin") == 0) {
			int pid = 0, cpu = -1;

			for (int i = 1; i < cmd_argc; i++) {
				if (strcmp(cmd_argv[i], "--pid") == 0 && i + 1 < cmd_argc)
					pid = atoi(cmd_argv[++i]);
				else if (strcmp(cmd_argv[i], "--cpu") == 0 && i + 1 < cmd_argc)
					cpu = atoi(cmd_argv[++i]);
			}

			if (pid <= 0 || cpu < 0) {
				fprintf(stderr, "Usage: %s pin --pid PID --cpu CPU\n",
					basename(argv[0]));
				return 1;
			}
			return cmd_pin(skel, pid, cpu);

		} else if (strcmp(cmd, "status") == 0) {
			return cmd_status(skel);

		} else {
			fprintf(stderr, "Unknown command: %s\n", cmd);
			fprintf(stderr, help_fmt, basename(argv[0]));
			return 1;
		}
	}

	/* No command - run the scheduler */
	run_scheduler(skel);

	UEI_REPORT(skel, uei);
	scx_gamesched__destroy(skel);
	return 0;
}

