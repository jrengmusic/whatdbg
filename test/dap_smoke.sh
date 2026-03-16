#!/usr/bin/env bash
set -euo pipefail

WHATDBG="/c/Users/jreng/Documents/Poems/dev/whatdbg/Builds/Ninja/whatdbg.exe"
STDOUT_LOG="/tmp/whatdbg_test_stdout.txt"
STDERR_LOG="/tmp/whatdbg_test_stderr.txt"

# DAP messages (JSON — backslashes already escaped for JSON string values)
INIT='{"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"test","linesStartAt1":true,"columnsStartAt1":true,"pathFormat":"path"}}'
LAUNCH='{"seq":2,"type":"request","command":"launch","arguments":{"program":"C:\\Users\\jreng\\Documents\\Poems\\dev\\whatdbg\\test\\hello.exe","cwd":"C:\\Users\\jreng\\Documents\\Poems\\dev\\whatdbg\\test","stopAtEntry":false}}'
SET_BP='{"seq":3,"type":"request","command":"setBreakpoints","arguments":{"source":{"path":"C:\\Users\\jreng\\Documents\\Poems\\dev\\whatdbg\\test\\hello.cpp"},"breakpoints":[{"line":7}]}}'
CONFIG_DONE='{"seq":4,"type":"request","command":"configurationDone","arguments":{}}'

# Frame a DAP message with Content-Length header
frame_msg() {
    local msg="$1"
    local len=${#msg}
    printf "Content-Length: %d\r\n\r\n%s" "$len" "$msg"
}

echo "=== Sending DAP sequence to whatdbg ==="
echo "  initialize -> launch -> setBreakpoints(line 7) -> configurationDone"
echo ""

{
    frame_msg "$INIT"
    sleep 1
    frame_msg "$LAUNCH"
    sleep 2
    frame_msg "$SET_BP"
    sleep 1
    frame_msg "$CONFIG_DONE"
    sleep 8
} | timeout 20 "$WHATDBG" 2>"$STDERR_LOG" >"$STDOUT_LOG" || true

echo "=== STDERR (whatdbg diagnostics) ==="
cat "$STDERR_LOG"
echo ""

echo "=== STDOUT raw size ==="
wc -c < "$STDOUT_LOG"
echo ""

echo "=== STDOUT: DAP events/responses (filtered) ==="
# Extract readable JSON from the DAP framed output
# DAP frames: "Content-Length: N\r\n\r\n{...}"
# Use strings to pull out JSON blobs, then grep for interesting fields
strings "$STDOUT_LOG" | grep -E '"type"|"event"|"command"|"success"|"reason"|"verified"|"stopped"|"breakpoint"|"error"|"message"' || echo "(no matching lines)"

echo ""
echo "=== STDOUT: Full JSON objects (one per line attempt) ==="
# Try to extract complete JSON objects
strings "$STDOUT_LOG" | grep -E '^\{' | python3 -c "
import sys, json
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        obj = json.loads(line)
        t = obj.get('type','?')
        if t == 'response':
            print(f'  RESPONSE  cmd={obj.get(\"command\",\"?\")} success={obj.get(\"success\",\"?\")}')
        elif t == 'event':
            print(f'  EVENT     event={obj.get(\"event\",\"?\")} body={json.dumps(obj.get(\"body\",{}))}')
        elif t == 'request':
            print(f'  REQUEST   cmd={obj.get(\"command\",\"?\")}')
        else:
            print(f'  OTHER     {line[:120]}')
    except:
        print(f'  RAW       {line[:120]}')
" 2>/dev/null || echo "(python parse failed, raw strings below)"

echo ""
echo "=== Check: was hello.exe launched? ==="
tasklist.exe 2>/dev/null | grep -i hello || echo "(hello.exe not in tasklist — already exited or never launched)"
