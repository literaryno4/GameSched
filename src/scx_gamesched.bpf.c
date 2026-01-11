/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scx_gamesched - A gaming-optimized sched_ext scheduler
 *
 * Priority scheduling:
 * - Tasks registered as "game render" get highest priority (DSQ 0)
 * - Tasks registered as "game other" get high priority (DSQ 1)
 * - Normal tasks go to DSQ 2, background to DSQ 3
 * - Dispatch consumes from lower-numbered DSQs first
 *
 * CPU Isolation:
 * - User can mark specific CPUs as "isolated"
 * - Only pinned game threads (and RT/percpu kthreads) run on isolated CPUs
 * - Normal tasks are steered away from isolated CPUs
 *
 * Copyright (c) 2026 GameSched Project
 */
#include <scx/common.bpf.h>
#include "scx_gamesched.h"

char _license[] SEC("license") = "GPL";

/*
 * User-configurable parameters (set from userspace before load)
 */
const volatile bool isolation_enabled;
const volatile u64 slice_ns = SCX_SLICE_DFL;

UEI_DEFINE(uei);

/*
 * DSQ IDs for each priority level
 */
#define DSQ_PRIO_BASE	0

/*
 * Map: game_threads - tracks which PIDs are game threads and their priority
 * Key: pid (u32)
 * Value: priority level (enum gamesched_priority)
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_GAME_THREADS);
	__type(key, u32);
	__type(value, u32);
} game_threads SEC(".maps");

/*
 * Map: isolated_cpus - marks which CPUs are isolated for game threads
 * Key: cpu id (u32)
 * Value: 1 if isolated, 0 otherwise
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, MAX_CPUS);
	__type(key, u32);
	__type(value, u32);
} isolated_cpus SEC(".maps");

/*
 * Map: pinned_threads - which game threads are pinned to which CPU
 * Key: pid (u32)
 * Value: cpu id (s32), -1 if not pinned
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_GAME_THREADS);
	__type(key, u32);
	__type(value, s32);
} pinned_threads SEC(".maps");

/*
 * Statistics
 */
u64 nr_game_dispatched;
u64 nr_normal_dispatched;
u64 nr_isolated_violations;  /* times we prevented normal task on isolated CPU */

/*
 * Get the priority level for a task.
 * Returns PRIO_NORMAL for unregistered tasks.
 */
static u32 get_task_priority(struct task_struct *p)
{
	u32 pid = p->pid;
	u32 *prio;

	prio = bpf_map_lookup_elem(&game_threads, &pid);
	if (prio)
		return *prio;

	return PRIO_NORMAL;
}

/*
 * Check if a CPU is isolated.
 */
static bool is_cpu_isolated(s32 cpu)
{
	u32 cpu_key = cpu;
	u32 *isolated;

	if (!isolation_enabled || cpu < 0)
		return false;

	isolated = bpf_map_lookup_elem(&isolated_cpus, &cpu_key);
	return isolated && *isolated;
}

/*
 * Check if a task is allowed on an isolated CPU.
 * Game threads (any priority) are allowed on isolated CPUs.
 */
static bool task_allowed_on_isolated(struct task_struct *p)
{
	u32 pid = p->pid;
	u32 prio = get_task_priority(p);

	/* Game threads are allowed */
	if (prio == PRIO_GAME_RENDER || prio == PRIO_GAME_OTHER)
		return true;

	/* Kernel threads are allowed (RT, percpu, etc.) */
	if (p->flags & PF_KTHREAD)
		return true;

	return false;
}

/*
 * Get the pinned CPU for a task, or -1 if not pinned.
 */
static s32 get_pinned_cpu(struct task_struct *p)
{
	u32 pid = p->pid;
	s32 *cpu;

	cpu = bpf_map_lookup_elem(&pinned_threads, &pid);
	if (cpu)
		return *cpu;

	return -1;
}

/*
 * Select CPU for a task.
 * - Pinned game threads go to their pinned CPU
 * - Normal tasks avoid isolated CPUs
 */
s32 BPF_STRUCT_OPS(gamesched_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	s32 pinned_cpu;
	bool is_idle = false;
	s32 cpu;

	/* Check if this task is pinned to a specific CPU */
	pinned_cpu = get_pinned_cpu(p);
	if (pinned_cpu >= 0) {
		/* Try to dispatch directly if the pinned CPU is idle */
		if (scx_bpf_test_and_clear_cpu_idle(pinned_cpu)) {
			scx_bpf_dispatch(p, SCX_DSQ_LOCAL_ON | pinned_cpu,
					 slice_ns, 0);
		}
		return pinned_cpu;
	}

	/* For non-pinned tasks, use default CPU selection */
	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);

	/* If selected CPU is isolated and task is not allowed, find another */
	if (isolation_enabled && is_cpu_isolated(cpu) &&
	    !task_allowed_on_isolated(p)) {
		s32 nr_cpus = scx_bpf_nr_cpu_ids();
		s32 i;

		/* Linear scan for a non-isolated CPU */
		bpf_for(i, 0, nr_cpus) {
			if (!is_cpu_isolated(i) &&
			    bpf_cpumask_test_cpu(i, p->cpus_ptr)) {
				cpu = i;
				is_idle = scx_bpf_test_and_clear_cpu_idle(cpu);
				break;
			}
		}
		__sync_fetch_and_add(&nr_isolated_violations, 1);
	}

	/* Dispatch directly if idle */
	if (is_idle)
		scx_bpf_dispatch(p, SCX_DSQ_LOCAL_ON | cpu, slice_ns, 0);

	return cpu;
}

/*
 * Enqueue task to appropriate priority DSQ.
 */
void BPF_STRUCT_OPS(gamesched_enqueue, struct task_struct *p, u64 enq_flags)
{
	u32 prio = get_task_priority(p);
	u64 dsq_id = DSQ_PRIO_BASE + prio;

	/* Dispatch to the priority-based DSQ */
	scx_bpf_dispatch(p, dsq_id, slice_ns, enq_flags);

	if (prio <= PRIO_GAME_OTHER)
		__sync_fetch_and_add(&nr_game_dispatched, 1);
	else
		__sync_fetch_and_add(&nr_normal_dispatched, 1);
}

/*
 * Dispatch: consume from DSQs in priority order.
 */
void BPF_STRUCT_OPS(gamesched_dispatch, s32 cpu, struct task_struct *prev)
{
	u32 prio;

	/* Consume from DSQs in priority order (0 = highest) */
	bpf_for(prio, 0, NR_PRIO_LEVELS) {
		if (scx_bpf_consume(DSQ_PRIO_BASE + prio))
			return;
	}
}

/*
 * Initialize the scheduler.
 */
s32 BPF_STRUCT_OPS_SLEEPABLE(gamesched_init)
{
	s32 ret;
	u32 i;

	/* Create DSQs for each priority level */
	bpf_for(i, 0, NR_PRIO_LEVELS) {
		ret = scx_bpf_create_dsq(DSQ_PRIO_BASE + i, -1);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Cleanup on exit.
 */
void BPF_STRUCT_OPS(gamesched_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

/*
 * Scheduler operations definition.
 */
SCX_OPS_DEFINE(gamesched_ops,
	       .select_cpu		= (void *)gamesched_select_cpu,
	       .enqueue			= (void *)gamesched_enqueue,
	       .dispatch		= (void *)gamesched_dispatch,
	       .init			= (void *)gamesched_init,
	       .exit			= (void *)gamesched_exit,
	       .name			= "gamesched");
