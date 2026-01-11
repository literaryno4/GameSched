# GameSched - Gaming-Optimized Linux Scheduler

A sched_ext-based Linux scheduler optimized for gaming workloads, focusing on render thread prioritization and CPU isolation.

## Features

- **Priority Scheduling**: Game threads get higher priority than system tasks
  - `PRIO_GAME_RENDER` - Main render threads (highest)
  - `PRIO_GAME_OTHER` - Secondary game threads
  - `PRIO_NORMAL` - Regular system tasks
  - `PRIO_BACKGROUND` - Low priority tasks

- **CPU Isolation**: Dedicate specific CPUs exclusively to game threads
  - Normal tasks are steered away from isolated CPUs
  - Only game threads, RT tasks, and kernel threads run on isolated CPUs

## Requirements

- Linux kernel 6.12+ with `CONFIG_SCHED_CLASS_EXT=y`
- clang, libbpf, bpftool
- Root privileges to run

## Building

```bash
# Set LINUX_SRC to your kernel source directory
export LINUX_SRC=/path/to/linux-source

# Build
make

# Or specify inline
make LINUX_SRC=/path/to/linux-source
```

## Usage

```bash
# Run the scheduler
sudo ./build/scx_gamesched

# With CPU isolation enabled
sudo ./build/scx_gamesched -i

# Add a game thread
sudo ./build/scx_gamesched add --pid 12345 --priority render

# Isolate CPUs 2 and 3
sudo ./build/scx_gamesched isolate --cpus 2,3

# Pin a thread to a specific CPU
sudo ./build/scx_gamesched pin --pid 12345 --cpu 2

# Show status
sudo ./build/scx_gamesched status
```

## CLI Commands

| Command | Description |
|---------|-------------|
| `add --pid PID --priority render\|game` | Add a game thread |
| `remove --pid PID` | Remove a game thread |
| `isolate --cpus CPU_LIST` | Isolate CPUs (e.g., 2,3) |
| `isolate --clear` | Clear CPU isolation |
| `pin --pid PID --cpu CPU` | Pin thread to CPU |
| `status` | Show current configuration |

## Project Structure

```
GameSched/
├── src/
│   ├── scx_gamesched.h       # Shared definitions
│   ├── scx_gamesched.bpf.c   # BPF scheduler logic
│   └── scx_gamesched.c       # Userspace CLI
├── Makefile
└── README.md
```

## License

GPL-2.0
