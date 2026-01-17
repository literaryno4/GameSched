#!/bin/bash
# render_thread.sh - Emulates a game render thread
# Runs at ~60fps (16ms per frame), doing CPU work then sleeping for vsync
#
# Usage: ./render_thread.sh [duration_seconds]

DURATION=${1:-30}
FRAME_TIME_MS=16  # ~60fps

echo "PID: $$ - Render thread emulator (~60fps) for ${DURATION}s"

end=$((SECONDS + DURATION))
frame=0

while [ $SECONDS -lt $end ]; do
    # Simulate frame work (~10ms of CPU work)
    for i in $(seq 1 10000); do
        : # busy work
    done
    
    # Simulate vsync wait (~6ms)
    sleep 0.006
    
    frame=$((frame + 1))
done

echo "Done after $frame frames"
