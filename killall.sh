#!/usr/bin/env bash
# Kill all processes associated with the elara.cpp project.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

killed=0

kill_matching() {
    local label="$1"
    shift
    local pids
    pids=$(pgrep -f "$1" 2>/dev/null || true)
    if [ -n "$pids" ]; then
        echo "Killing $label: $pids"
        kill -9 $pids 2>/dev/null || true
        killed=$((killed + $(echo $pids | wc -w)))
    fi
}

# C++ server binary
kill_matching "elaraui-server"        "elaraui-server"

# EPA IDE (app.py and any workers it spawns)
kill_matching "epa-ide app.py"        "$PROJECT_DIR/epa-ide/app.py"
kill_matching "epa-ide workers"       "$PROJECT_DIR/epa-ide/workers/"

# Any python process running from within the project directory
kill_matching "python (project)"      "$PROJECT_DIR/epa-ide"

# Orange exterminator demo (C++ binary + EPA debug server)
kill_matching "orange-exterminator"           "$PROJECT_DIR/demos/orange-exterminator/cpp/build/orange-exterminator"
kill_matching "orange-exterminator-epa-debug" "$PROJECT_DIR/demos/orange-exterminator/cpp/build/orange-exterminator-epa-debug"

# Build tools that might be stuck
kill_matching "elara-project-builder" "elara-project-builder"
kill_matching "elara-unit-tests"      "elara-unit-tests"

if [ "$killed" -eq 0 ]; then
    echo "No elara processes found."
else
    echo "Done — killed $killed process(es)."
fi
