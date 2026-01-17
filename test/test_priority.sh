#!/bin/bash
# test_priority.sh - Test priority scheduling
# Runs 4 CPU burners, marks one as render priority, measures CPU usage

echo "=== GameSched Priority Test ==="
echo "Starting 4 CPU burners for 20 seconds..."

# Start 4 cpu burners
./test/cpu_burner.sh 20 &
P1=$!
./test/cpu_burner.sh 20 &
P2=$!
./test/cpu_burner.sh 20 &
P3=$!
./test/cpu_burner.sh 20 &
GAME=$!

echo "PIDs: normal=$P1,$P2,$P3  game=$GAME"

sleep 2

echo "Marking PID $GAME as 'render' priority..."
sudo ./build/scx_gamesched add --pid $GAME --priority render

echo ""
echo "Waiting 5 seconds for scheduling to stabilize..."
sleep 5

echo ""
echo "=== CPU Usage (via ps) ==="
echo "PID $GAME should have higher CPU% than others"
ps -p $P1,$P2,$P3,$GAME -o pid,%cpu,comm 2>/dev/null

sleep 3

echo ""
echo "=== Final Check ==="
ps -p $P1,$P2,$P3,$GAME -o pid,%cpu,comm 2>/dev/null

wait
echo ""
echo "Test complete!"
