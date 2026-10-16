#!/bin/bash
# SessionStart hook: Beads context for every session (bd prime), and in a cloud
# session a fresh copy of the Beads database first. A cloud session cannot push
# Beads; its writes go through host/tools/bd_outbox.py
# (docs/procedures/cloud-sessions.md).

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
command -v bd >/dev/null 2>&1 || exit 0

if [ "$CLAUDE_CODE_REMOTE" = "true" ] && [ ! -d .beads/embeddeddolt ]; then
  bd bootstrap >/dev/null 2>&1 || echo "session-start: bd bootstrap failed" >&2
  # bootstrap rewrites these two tracked files in a form that changes nothing
  # for this session; restore them so they are not committed by mistake.
  git checkout -- .beads/.gitignore .beads/config.yaml 2>/dev/null
fi

exec bd prime --hook-json
