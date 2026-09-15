#!/usr/bin/env python3
"""Refuse the commands that can silently destroy uncommitted work.

Not a security boundary -- the agent writes files all day and has to. This
blocks the narrow class that bit us: commands that throw away work with no
copy left behind and no prompt. `git checkout <file>` is the one that did it;
it reverts to HEAD and says nothing.

Only the agent's Bash tool goes through here. Tony's own shell is untouched,
which is the point: the rule costs the human nothing.

A command is matched at COMMAND POSITION -- start of line, or after ; && || |
-- so `grep -n "rm "` and a comment mentioning git checkout still run.
"""
import json
import re
import sys

HEREDOC = re.compile(r"<<-?\s*'?\"?(\w+)'?\"?\n.*?\n\1\s*$",
                     re.S | re.M)


def strip_heredocs(cmd):
    """Drop heredoc BODIES before matching.

    A heredoc carries DATA, not commands: a file being written, a script fed
    to python. Without this the guard reads prose about the commands it
    refuses as an attempt to run them -- which is what it did the first time
    it fired, on the note explaining why it exists.
    """
    return HEREDOC.sub("<<BODY", cmd)


# after a separator, or at the start
POS = r'(?:^|[\n;|&]\s*|\b(?:then|else|do)\s+)'
RULES = [
    (POS + r'git\s+(?:checkout|restore|reset|stash|clean|rm)\b',
     "git checkout/restore/reset/stash/clean discards uncommitted work with no\n"
     "copy left behind. It cost this project a session's worth of a generator\n"
     "once. Copy the file to the scratchpad first and restore from there;\n"
     "leave git to Tony."),
    (POS + r'rm\s',
     "rm is not yours to run -- see the standing rule. If a generated file is\n"
     "in the way, overwrite it or ask Tony to remove it."),
]


def main():
    try:
        ev = json.load(sys.stdin)
    except Exception:
        return 0
    cmd = strip_heredocs((ev.get("tool_input") or {}).get("command") or "")
    for pat, why in RULES:
        if re.search(pat, cmd):
            print(json.dumps({
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "deny",
                    "permissionDecisionReason": why,
                }
            }))
            return 0
    return 0


sys.exit(main())
