#!/bin/bash
# test_isolation.sh - Test CPU isolation
# Verifies that normal tasks cannot run on isolated CPUs

echo "=== GameSched CPU Isolation Test ==="

# Get number of CPUs
NCPUS=$(nproc)
echo "System has $NCPUS CPUs"

# We'll isolate CPU 2
ISOLATED_CPU=2

echo ""
echo "Step 1: Isolating CPU $ISOLATED_CPU..."
sudo ./build/scx_gamesched isolate --cpus $ISOLATED_CPU

echo ""
echo "Step 2: Starting 16 CPU burners (more than CPUs) to cause contention..."
PIDS=""
for i in $(seq 1 16); do
    ./test/cpu_burner.sh 15 &
    PIDS="$PIDS $!"
done
echo "Started 16 burners"

sleep 3

echo ""
echo "Step 3: Checking which processes ran on CPU $ISOLATED_CPU..."
echo "(Using ps to see CPU affinity)"

# Check what's running on each CPU using mpstat or /proc
echo ""
echo "=== Process distribution (ps -eo pid,psr,comm) ==="
echo "PSR column shows which CPU the process last ran on"
ps -eo pid,psr,comm | grep cpu_burner | head -20

echo ""
echo "=== Count processes per CPU ==="
ps -eo psr,comm | grep cpu_burner | awk '{count[$1]++} END {for (cpu in count) print "CPU " cpu ": " count[cpu] " burners"}'

echo ""
echo "Step 4: Now pinning one burner to isolated CPU..."
GAME_PID=$(echo $PIDS | awk '{print $1}')
echo "Pinning PID $GAME_PID to isolated CPU $ISOLATED_CPU"
sudo ./build/scx_gamesched add --pid $GAME_PID --priority render
sudo ./build/scx_gamesched pin --pid $GAME_PID --cpu $ISOLATED_CPU

sleep 2

echo ""
echo "=== After pinning - Process distribution ==="
ps -eo psr,comm | grep cpu_burner | awk '{count[$1]++} END {for (cpu in count) print "CPU " cpu ": " count[cpu] " burners"}'

echo ""
echo "Step 5: Verify game process runs on isolated CPU"
ps -p $GAME_PID -o pid,psr,comm

# Cleanup
echo ""
echo "Step 6: Clearing isolation..."
sudo ./build/scx_gamesched isolate --clear

# Wait for burners to finish
wait 2>/dev/null

echo ""
echo "Test complete!"
echo ""
echo "EXPECTED: Normal burners should avoid CPU $ISOLATED_CPU"
echo "EXPECTED: Pinned game process should run on CPU $ISOLATED_CPU"
