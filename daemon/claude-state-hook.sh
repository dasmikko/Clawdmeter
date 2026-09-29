#!/bin/sh
# Clawdmeter: records what Claude Code is doing, one file per session, so the
# daemon can show it on the device (working / waiting for you / idle).
#
# Wired into ~/.claude/settings.json by daemon/install-claude-hooks.py; each
# hook passes one argument:
#
#   UserPromptSubmit, PostToolUse, PostToolUseFailure -> working
#   PreToolUse[AskUserQuestion], PermissionRequest    -> waiting
#   Notification                                      -> notify (reads the type)
#   Stop, StopFailure                                 -> idle
#   SessionEnd                                        -> end (removes the file)
#
# Known gaps, both Claude Code's (no event fires):
# - Interrupting a turn with Esc fires nothing. Claude Code does write a
#   "[Request interrupted by user]" entry to the session transcript, so the
#   file stores the transcript path and the daemon watches it for one newer
#   than the last hook (see session_interrupted in the daemon). The daemon's
#   staleness cutoff covers a terminal killed outright.
# - Approving a permission prompt fires nothing until the approved tool
#   finishes (PostToolUse), so "waiting" lingers for the length of that tool.
#
# The daemon reads $STATE_DIR/<session_id>: line 1 is the state
# (working|waiting|idle), line 2 the session's transcript path.
# Writes go through a temp file + rename so a reader never sees an empty file.

set -u

STATE_DIR="${CLAWDMETER_STATE_DIR:-$HOME/.cache/claude-usage-monitor/claude-state}"

# stdin is the hook's JSON payload. `dd count=1` is a single read(2): it
# returns as soon as the payload arrives instead of waiting for EOF or a
# trailing newline (neither is guaranteed). Claude Code writes the payload in
# one go, a few hundred bytes against a 64K pipe, so one read is all of it.
payload=$( (dd bs=65536 count=1 2>/dev/null || true) )

json_str() {
    printf '%s' "$payload" \
        | sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" \
        | head -n 1
}

session=$(json_str session_id | tr -cd 'A-Za-z0-9_-')
[ -n "$session" ] || session=default

action=${1:-idle}
if [ "$action" = notify ]; then
    case "$(json_str notification_type)" in
        permission_prompt | agent_needs_input | elicitation_dialog) action=waiting ;;
        idle_prompt) action=idle ;;
        *) exit 0 ;;   # e.g. auth_success: says nothing about the turn
    esac
fi

mkdir -p "$STATE_DIR" 2>/dev/null || exit 0
file="$STATE_DIR/$session"

case "$action" in
    working | waiting | idle) ;;
    end) rm -f "$file"; exit 0 ;;
    *) exit 0 ;;
esac

tmp="$STATE_DIR/.$session.$$"
printf '%s\n%s\n' "$action" "$(json_str transcript_path)" >"$tmp" && mv -f "$tmp" "$file"
exit 0
