#!/bin/bash
# test_priority_contention.sh - Test priority with CPU contention
# Runs 16 CPU burners on 8 CPUs to create contention

echo "=== GameSched Priority Contention Test ==="
echo "Starting 16 CPU burners on $(nproc) CPUs to create contention..."

PIDS=""
for i in $(seq 1 15); do
    ./test/cpu_burner.sh 25 &
    PIDS="$PIDS $!"
done

# The game process
./test/cpu_burner.sh 25 &
GAME=$!
echo "GAME PID: $GAME"
echo "Normal PIDs: $PIDS"

sleep 3

echo ""
echo "Marking PID $GAME as 'render' priority..."
sudo ./build/scx_gamesched add --pid $GAME --priority render

echo ""
echo "Waiting 5 seconds for scheduling to stabilize..."
sleep 5

echo ""
echo "=== CPU Usage Comparison ==="
echo "Game process ($GAME) should show higher CPU% than normal processes"
echo ""

# Show game process CPU
echo "GAME PROCESS:"
ps -p $GAME -o pid,%cpu,comm 2>/dev/null

echo ""
echo "NORMAL PROCESSES (sample of 5):"
for p in $PIDS; do
    ps -p $p -o pid,%cpu,comm --no-headers 2>/dev/null
done | head -5

# Calculate averages
echo ""
echo "=== Summary ==="
GAME_CPU=$(ps -p $GAME -o %cpu --no-headers 2>/dev/null | tr -d ' ')
echo "Game process CPU%: $GAME_CPU"

NORMAL_AVG=$(for p in $PIDS; do ps -p $p -o %cpu --no-headers 2>/dev/null; done | awk '{sum+=$1; count++} END {if(count>0) print sum/count; else print 0}')
echo "Normal processes avg CPU%: $NORMAL_AVG"

echo ""
echo "If priority is working, Game CPU% should be significantly higher than Normal avg"

# Wait for all to finish
wait
echo ""
echo "Test complete!"
