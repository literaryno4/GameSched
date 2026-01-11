/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scx_gamesched - A gaming-optimized sched_ext scheduler
 *
 * This scheduler prioritizes gaming threads (especially render threads) over
 * normal system tasks and supports CPU isolation for critical game threads.
 *
 * Features:
 * - Priority levels: RENDER > GAME > NORMAL > BACKGROUND
 * - CPU isolation: Designated CPUs only run game threads (+ RT/percpu kthreads)
 * - Configurable via userspace CLI
 *
 * Copyright (c) 2026 GameSched Project
 */
#ifndef __SCX_GAMESCHED_H
#define __SCX_GAMESCHED_H

/* Priority levels for task scheduling */
enum gamesched_priority {
	PRIO_GAME_RENDER = 0,	/* Highest: main render threads */
	PRIO_GAME_OTHER  = 1,	/* Secondary game threads */
	PRIO_NORMAL      = 2,	/* Regular system tasks */
	PRIO_BACKGROUND  = 3,	/* Low priority background */
	NR_PRIO_LEVELS   = 4,
};

/* Maximum number of game threads we can track */
#define MAX_GAME_THREADS	1024

/* Maximum number of CPUs we can isolate */
#define MAX_CPUS		256

/* Configuration flags */
#define GAMESCHED_FLAG_ENABLED		(1 << 0)
#define GAMESCHED_FLAG_ISOLATION	(1 << 1)

#endif /* __SCX_GAMESCHED_H */
