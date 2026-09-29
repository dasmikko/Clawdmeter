#!/bin/bash
# Regression test for the Claude Code state path: claude-state-hook.sh (run by
# Claude Code hooks, one file per session) and read_claude_state() /
# send_payload() in claude-usage-daemon.sh, which aggregate the sessions
# (waiting > working > idle) and append "cc" to the device payload.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
DAEMON="$HERE/../claude-usage-daemon.sh"
HOOK="$HERE/../claude-state-hook.sh"

extract() { awk -v fn="$1" '$0 ~ "^"fn"\\(\\) \\{"{f=1} f{print} f&&/^\}/{exit}' "$DAEMON"; }
eval "$(extract session_interrupted)"
eval "$(extract read_claude_state)"
eval "$(extract send_payload)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export CLAWDMETER_STATE_DIR="$TMP/state"
CLAUDE_STATE_DIR="$CLAWDMETER_STATE_DIR"
CLAUDE_WORKING_STALE_S=900
CLAUDE_WAITING_STALE_S=3600

fail=0
check() {  # check <label> <got> <want>
    if [ "$2" = "$3" ]; then echo "PASS: $1"; else echo "FAIL: $1: got '$2' want '$3'"; fail=1; fi
}
hook() {  # hook <session> <arg> [extra json]
    printf '{"session_id":"%s","hook_event_name":"x"%s}' "$1" "${3:-}" | "$HOOK" "$2"
}

check "no hooks ever ran -> no state"      "$(read_claude_state)" ""

hook s1 working
check "prompt submitted -> working"        "$(read_claude_state)" "working"
hook s2 idle
check "working beats idle"                 "$(read_claude_state)" "working"
hook s2 notify ',"notification_type":"permission_prompt"'
check "permission prompt -> waiting"       "$(read_claude_state)" "waiting"
hook s2 notify ',"notification_type":"auth_success"'
check "unrelated notification ignored"     "$(cat "$CLAUDE_STATE_DIR/s2")" "waiting"
hook s2 notify ',"notification_type":"idle_prompt"'
check "idle_prompt -> idle (Esc'd turn)"   "$(cat "$CLAUDE_STATE_DIR/s2")" "idle"
hook s1 idle
check "all sessions idle -> idle"          "$(read_claude_state)" "idle"
hook s1 end
check "SessionEnd removes the file"        "$(ls "$CLAUDE_STATE_DIR")" "s2"
hook '../evil' working
check "session id is sanitized"            "$(ls "$CLAUDE_STATE_DIR" | sort | tr '\n' ' ')" "evil s2 "

echo working > "$CLAUDE_STATE_DIR/evil"; touch -d '20 min ago' "$CLAUDE_STATE_DIR/evil"
check "stale working -> idle"              "$(read_claude_state)" "idle"
echo waiting > "$CLAUDE_STATE_DIR/evil"; touch -d '20 min ago' "$CLAUDE_STATE_DIR/evil"
check "20-min-old waiting still waiting"   "$(read_claude_state)" "waiting"
touch -d '2 hours ago' "$CLAUDE_STATE_DIR/evil"
check "2-hour-old waiting -> idle"         "$(read_claude_state)" "idle"

# Esc interrupt: no hook fires, but the transcript gets a top-level entry.
rm -f "$CLAUDE_STATE_DIR"/*
TR="$TMP/transcript.jsonl"
iso() { date -u -d "@$1" +%Y-%m-%dT%H:%M:%S.000Z; }
printf '{"type":"user","message":{"role":"user","content":"hi"}}\n' > "$TR"
printf '{"session_id":"s3","transcript_path":"%s"}' "$TR" | "$HOOK" working
check "hook stores transcript path"        "$(sed -n 2p "$CLAUDE_STATE_DIR/s3")" "$TR"
check "working before interrupt"           "$(read_claude_state)" "working"
# Tool output quoting the interrupt text is escaped JSON: must not match.
printf '{"type":"user","message":{"content":[{"type":"tool_result","content":"{\\"content\\":[{\\"type\\":\\"text\\",\\"text\\":\\"[Request interrupted by user]\\"}]}"}]},"timestamp":"%s"}\n' \
    "$(iso $(( $(date +%s) + 5 )))" >> "$TR"
check "quoted interrupt text ignored"      "$(read_claude_state)" "working"
printf '{"parentUuid":"x","type":"user","message":{"role":"user","content":[{"type":"text","text":"[Request interrupted by user]"}]},"timestamp":"%s"}\n' \
    "$(iso $(( $(date +%s) + 5 )))" >> "$TR"
check "Esc interrupt -> idle"              "$(read_claude_state)" "idle"
printf '{"session_id":"s3","transcript_path":"%s"}' "$TR" | "$HOOK" waiting
touch -d '+10 seconds' "$CLAUDE_STATE_DIR/s3"
check "hook after interrupt wins"          "$(read_claude_state)" "waiting"
printf '{"parentUuid":"x","type":"user","message":{"role":"user","content":[{"type":"text","text":"[Request interrupted by user for tool use]"}]},"timestamp":"%s"}\n' \
    "$(iso $(( $(date +%s) + 20 )))" >> "$TR"
touch -d '+20 seconds' "$TR"
check "Esc on a permission prompt -> idle" "$(read_claude_state)" "idle"

# send_payload appends "cc" before the closing brace.
RX_CHAR_PATH=/fake
write_gatt() { SENT="$2"; }
log() { :; }
hook s2 working
send_payload '{"s":13,"ok":true}'
check "payload gets cc"                    "$SENT" '{"s":13,"ok":true,"cc":"working"}'
check "LAST_CC tracks what was sent"       "$LAST_CC" "working"
rm -rf "$CLAUDE_STATE_DIR"
send_payload '{"s":13,"ok":true}'
check "no hooks -> payload unchanged"      "$SENT" '{"s":13,"ok":true}'

exit $fail
