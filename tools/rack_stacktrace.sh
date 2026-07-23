#!/bin/bash
# rack_stacktrace.sh - grab a full all-threads stacktrace from a running (possibly hung) Rack.exe
#
# Run this from a REAL interactive MSYS2 MinGW64 terminal (not a sandboxed/agent shell - gdb's
# process-attach needs privileges a sandboxed shell typically doesn't have, confirmed the hard
# way: it fails even attaching to a process the SAME shell just spawned, error 5/Access Denied).
#
# Usage:
#   ./tools/rack_stacktrace.sh
#
# What it does (see CLAUDE.md's "Debugging a Rack freeze" pitfall for the full reasoning):
#   1. Finds the running Rack.exe PID.
#   2. Attaches gdb, dumps `thread apply all bt` (ALL threads, not just one - a real engine
#      deadlock always involves two: one thread holding/awaiting the exclusive lock, one stuck on
#      a nested share lock, and they're almost never the same thread gdb attaches to by default).
#   3. Detaches cleanly (`detach` + `quit`, not killing the process) so Rack keeps running/hanging
#      exactly as before - you can re-run this script again later, or resume/kill Rack yourself.
#   4. Saves the output to a timestamped log file (in this same tools/ dir) so it can be pasted
#      back into a conversation with Claude for analysis, in addition to printing it live.

set -u

PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"

if ! command -v gdb >/dev/null 2>&1; then
	echo "gdb not found on PATH - are you in a real MSYS2 MinGW64 terminal? (see CLAUDE.md's Build section)" >&2
	exit 1
fi

# ps -W (MSYS2's own Windows-process-listing ps) - matches CLAUDE.md's documented lookup exactly.
# Column 1 is the MSYS-internal PID, NOT what gdb (a native Windows debugger) needs - gdb requires
# the real Windows PID, which ps -W reports separately as WINPID in column 4 (confirmed the hard
# way: using column 1 produces "error 87: The parameter is incorrect" from gdb, since that PID
# doesn't correspond to any real Windows process at all).
PIDS=$(ps -W | grep -i "rack.exe" | grep -v grep | awk '{print $4}')

if [ -z "$PIDS" ]; then
	echo "No running Rack.exe process found (ps -W | grep -i rack.exe turned up nothing)." >&2
	exit 1
fi

PID_COUNT=$(echo "$PIDS" | wc -l)
if [ "$PID_COUNT" -gt 1 ]; then
	echo "More than one matching process found - pick one and re-run with it as an argument:" >&2
	ps -W | grep -i "rack.exe" | grep -v grep >&2
	exit 1
fi

PID="$PIDS"
echo "Attaching gdb to Rack.exe (PID $PID)..."

OUT_FILE="$(dirname "$0")/rack_stacktrace_$(date +%Y%m%d_%H%M%S).log"

gdb -p "$PID" -batch \
	-ex "thread apply all bt" \
	-ex "detach" \
	-ex "quit" 2>&1 | tee "$OUT_FILE"

echo
echo "Saved to: $OUT_FILE"
echo "Rack was only detached from, not killed - it's still running (or still hung) exactly as before."
