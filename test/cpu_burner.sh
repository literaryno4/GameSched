#!/bin/bash
# cpu_burner.sh - Simple CPU burner for testing
# Usage: ./cpu_burner.sh [seconds]

DURATION=${1:-30}
echo "PID: $$ - Burning CPU for ${DURATION}s"
end=$((SECONDS + DURATION))
while [ $SECONDS -lt $end ]; do
    : # busy loop
done
echo "Done"
