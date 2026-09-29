#!/usr/bin/env python3
"""Install (or remove) the Clawdmeter Claude Code state hooks.

Merges hook entries that call daemon/claude-state-hook.sh into a Claude Code
settings.json (default ~/.claude/settings.json). Idempotent: any existing
entry pointing at claude-state-hook.sh is replaced, every other hook is left
untouched. A backup of the original file is written next to it.

    install-claude-hooks.py [--remove] [settings.json ...]
"""

import json
import shutil
import sys
from pathlib import Path

HOOK_SCRIPT = (Path(__file__).resolve().parent / "claude-state-hook.sh")
MARKER = "claude-state-hook.sh"

# (event, matcher or None, argument passed to the hook script)
HOOKS = [
    ("UserPromptSubmit", None, "working"),
    ("PostToolUse", None, "working"),
    ("PostToolUseFailure", None, "working"),
    ("PreToolUse", "AskUserQuestion", "waiting"),
    ("PermissionRequest", None, "waiting"),
    ("Notification", None, "notify"),
    ("Stop", None, "idle"),
    ("StopFailure", None, "idle"),
    ("SessionEnd", None, "end"),
]


def strip_ours(groups):
    """Drop our command from every matcher group; drop groups left empty."""
    out = []
    for group in groups:
        hooks = [h for h in group.get("hooks", [])
                 if MARKER not in str(h.get("command", ""))]
        if hooks:
            out.append({**group, "hooks": hooks})
        elif not group.get("hooks"):
            out.append(group)   # not ours, keep as-is
    return out


def update(path: Path, remove: bool) -> None:
    settings = {}
    if path.exists():
        text = path.read_text()
        if text.strip():
            settings = json.loads(text)
        shutil.copy2(path, path.with_suffix(path.suffix + ".clawdmeter.bak"))

    hooks = settings.setdefault("hooks", {})
    for event in list(hooks):
        hooks[event] = strip_ours(hooks[event])
        if not hooks[event]:
            del hooks[event]

    if not remove:
        for event, matcher, arg in HOOKS:
            group = {"hooks": [{
                "type": "command",
                "command": f"{HOOK_SCRIPT} {arg}",
                "timeout": 5,
            }]}
            if matcher:
                group = {"matcher": matcher, **group}
            hooks.setdefault(event, []).append(group)

    if not hooks:
        del settings["hooks"]

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(settings, indent=2) + "\n")
    print(f"  {'Removed hooks from' if remove else 'Installed hooks in'} {path}")


def main(argv):
    remove = "--remove" in argv
    paths = [Path(a).expanduser() for a in argv if a != "--remove"]
    if not paths:
        paths = [Path.home() / ".claude" / "settings.json"]
    for p in paths:
        update(p, remove)


if __name__ == "__main__":
    main(sys.argv[1:])
