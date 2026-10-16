"""Carry Beads writes from cloud sessions to the canonical database (repo process).

Cloud sessions cannot push the Beads Dolt remote. A cloud agent runs every
Beads write through this tool instead of calling bd directly:

    python3 host/tools/bd_outbox.py bd update <id> --append-notes "..."

The tool runs bd against the session's own copy of the database, so the session
sees its writes, and appends the command to an outbox file,
.beads/outbox/<branch>.jsonl, which is committed and pushed with the branch.

On the dev machine, after pulling the branch:

    python host/tools/bd_outbox.py apply --dry-run
    python host/tools/bd_outbox.py apply
    bd dolt push

apply replays each outbox in order against the canonical database. An issue
the session created gets whatever ID the canonical database assigns, and later
commands that name the session's ID are rewritten to it. Progress is kept in a
<outbox>.applied.json file next to the outbox (not committed), so a rerun skips
what was applied and resumes after a failure. Remove the outbox file once it is
applied and pushed.

Only writes are recorded. Read-only commands run as given. Commands that sync
or rebuild the database are refused, because a cloud session must not sync.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

OUTBOX_DIR = Path(".beads") / "outbox"
FORMAT_VERSION = 1

# Commands that only read; they run as given and are not recorded.
READ_ONLY = {
    "blocked", "children", "count", "diff", "export", "find-duplicates", "graph",
    "help", "history", "info", "lint", "list", "memories", "orphans", "preflight",
    "prime", "query", "ready", "recall", "schema", "search", "show", "stale", "status",
    "statuses", "types", "version", "where",
}
# Subcommands that only read, for commands that also have writing subcommands.
READ_ONLY_SUB = {
    "dep": {"list", "tree", "cycles"},
    "label": {"list", "list-all"},
}
# Commands that sync, rebuild or hand-edit the database: never from the outbox.
REFUSED = {
    "backup", "batch", "bootstrap", "branch", "compact", "create-form", "dolt", "edit",
    "federation", "gc", "import", "init", "merge-slot", "migrate", "migrate-personal",
    "prune", "purge", "rename-prefix", "restore", "sql", "sync", "upgrade", "vc",
}
# Commands that create an issue; their new ID is captured.
CREATES = {"create", "q"}

# Global flags that take a value, so the subcommand can be found after them.
VALUE_FLAGS = {"--actor", "--database", "--db", "-C", "--directory", "--dolt-auto-commit"}


class OutboxError(Exception):
    pass


def find_bd():
    path = shutil.which("bd")
    if path is None:
        raise OutboxError("bd is not on PATH")
    return path


def run_bd(argv):
    """Runs bd with argv; returns (exit code, stdout, stderr)."""
    result = subprocess.run([find_bd()] + list(argv), capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return result.returncode, result.stdout, result.stderr


def command_words(argv):
    """The subcommand and the word after it, skipping global flags."""
    words = []
    skip = False
    for token in argv:
        if skip:
            skip = False
            continue
        if token in VALUE_FLAGS:
            skip = True
            continue
        if token.startswith("-"):
            continue
        words.append(token)
        if len(words) == 2:
            break
    return (words + [None, None])[:2]


def classify(argv):
    """'read', 'write', 'create' or 'refused'."""
    command, sub = command_words(argv)
    if command is None or command in READ_ONLY or "--dry-run" in argv or "--help" in argv \
            or "-h" in argv:
        return "read"
    if command in REFUSED:
        return "refused"
    if command in READ_ONLY_SUB and sub in READ_ONLY_SUB[command]:
        return "read"
    if command == "comments" and sub != "add":
        return "read"
    if command == "create" and any(t == "--graph" or t.startswith("--graph=") or t == "--file"
                                   or t.startswith("--file=") for t in argv):
        return "refused"
    if command in CREATES:
        return "create"
    return "write"


def creating_argv(argv):
    """argv for a create that prints the new issue as JSON (q prints only the ID)."""
    command, _ = command_words(argv)
    if command == "q":
        return list(argv)
    return [t for t in argv if t != "--silent"] + ([] if "--json" in argv else ["--json"])


def created_id(argv, stdout):
    command, _ = command_words(argv)
    text = stdout.strip()
    if command == "q":
        return text.splitlines()[-1].strip() if text else ""
    try:
        data = json.loads(text)
    except ValueError:
        raise OutboxError("bd create printed no JSON: %r" % text[:200])
    if isinstance(data, list):
        data = data[0] if data else {}
    return str(data.get("id", ""))


def git(*args):
    result = subprocess.run(["git"] + list(args), capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def actor():
    return os.environ.get("BEADS_ACTOR") or git("config", "user.name") or \
        os.environ.get("USER") or os.environ.get("USERNAME") or "unknown"


def outbox_path(root, branch):
    name = re.sub(r"[^A-Za-z0-9._-]+", "__", branch or "detached")
    return Path(root) / OUTBOX_DIR / (name + ".jsonl")


def read_outbox(path):
    entries = []
    with Path(path).open(encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if line.strip():
                entry = json.loads(line)
                if entry.get("v") != FORMAT_VERSION:
                    raise OutboxError("%s line %d: unknown format %r" % (path, number, entry.get("v")))
                entries.append(entry)
    return entries


def record(argv, path, runner=run_bd, who=None, now=None):
    """Runs a bd command; records it in the outbox at path if it wrote and succeeded.

    Returns (exit code, stdout, stderr) as bd gave them.
    """
    kind = classify(argv)
    if kind == "refused":
        raise OutboxError("'bd %s' is not allowed through the outbox: a cloud session does not "
                          "sync or rebuild the database, and creates one issue at a time"
                          % " ".join(command_words(argv)[:1]))
    if kind == "read":
        return runner(argv)
    run_argv = creating_argv(argv) if kind == "create" else list(argv)
    code, stdout, stderr = runner(run_argv)
    if code != 0:
        return code, stdout, stderr
    entry = {"v": FORMAT_VERSION, "seq": 0, "at": now or datetime.now(timezone.utc).isoformat(
        timespec="seconds"), "actor": who or actor(), "argv": list(argv)}
    if kind == "create":
        entry["created"] = created_id(run_argv, stdout)
        if not entry["created"]:
            raise OutboxError("bd created an issue but printed no ID; it is NOT in the outbox")
        stdout = "Created %s (recorded in the outbox)\n" % entry["created"]
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    ignore = path.parent / ".gitignore"
    if not ignore.exists():
        ignore.write_text("# Progress of bd_outbox.py apply on the dev machine.\n*.applied.json\n",
                          encoding="utf-8")
    entry["seq"] = len(read_outbox(path)) + 1 if path.exists() else 1
    with path.open("a", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(entry, ensure_ascii=False) + "\n")
    return code, stdout, stderr


def with_short_ids(ids):
    """ids plus the short form of each (v7l.17 for proj-v7l.17) where the prefixes match."""
    both = dict(ids)
    for old, new in ids.items():
        prefix = old.rsplit("-", 1)[0] + "-" if "-" in old else None
        if prefix and new.startswith(prefix):
            both.setdefault(old[len(prefix):], new[len(prefix):])
    return both


def rewrite(token, ids):
    """Replaces each session ID in token with its canonical ID, whole IDs only."""
    ids = with_short_ids(ids)
    for old in sorted(ids, key=len, reverse=True):
        if old != ids[old]:
            token = re.sub(r"(?<![\w.-])" + re.escape(old) + r"(?![\w-])(?!\.\w)",
                           lambda _, new=ids[old]: new, token)
    return token


def state_path(path):
    path = Path(path)
    return path.with_name(path.name[:-len(".jsonl")] + ".applied.json")


def load_state(path):
    state = state_path(path)
    if state.exists():
        return json.loads(state.read_text(encoding="utf-8"))
    return {"applied": [], "ids": {}}


def save_state(path, state):
    state_path(path).write_text(json.dumps(state, indent=1) + "\n", encoding="utf-8")


def apply(path, runner=run_bd, dry_run=False, say=print):
    """Replays one outbox. Returns the number of commands applied; stops at a failure."""
    state = load_state(path)
    done = set(state["applied"])
    ids = dict(state["ids"])
    count = 0
    for entry in read_outbox(path):
        if entry["seq"] in done:
            continue
        argv = [rewrite(token, ids) for token in entry["argv"]]
        if "--actor" not in argv and not any(t.startswith("--actor=") for t in argv):
            argv = ["--actor", entry["actor"]] + argv
        created = entry.get("created")
        if created:
            argv = creating_argv(argv)
        if dry_run:
            say("would run: bd " + " ".join(argv))
            if created:
                ids[created] = "<new id for %s>" % created
            continue
        code, stdout, stderr = runner(argv)
        if code != 0:
            raise OutboxError("%s #%d failed (exit %d): bd %s\n%s%s" % (
                path, entry["seq"], code, " ".join(argv), stdout, stderr))
        if created:
            ids[created] = created_id(argv, stdout)
            if not ids[created]:
                raise OutboxError("%s #%d created an issue but bd printed no ID" % (path, entry["seq"]))
            say("#%d created %s (was %s in the session)" % (entry["seq"], ids[created], created))
        else:
            say("#%d bd %s" % (entry["seq"], " ".join(argv)[:120]))
        state["applied"].append(entry["seq"])
        state["ids"] = ids
        save_state(path, state)
        count += 1
    return count


def pending(root):
    """Outbox files with commands not yet applied here, oldest first."""
    found = []
    for path in sorted((Path(root) / OUTBOX_DIR).glob("*.jsonl")):
        applied = set(load_state(path)["applied"])
        left = [e for e in read_outbox(path) if e["seq"] not in applied]
        if left:
            found.append((left[0]["at"], path, len(left)))
    return [(path, left) for _, path, left in sorted(found)]


def main(args=None):
    args = list(sys.argv[1:] if args is None else args)
    root = Path(git("rev-parse", "--show-toplevel") or ".")
    try:
        if args and args[0] == "bd":
            if not os.environ.get("CLAUDE_CODE_REMOTE"):
                print("bd_outbox: not a cloud session; recording anyway", file=sys.stderr)
            path = outbox_path(root, git("rev-parse", "--abbrev-ref", "HEAD"))
            code, stdout, stderr = record(args[1:], path)
            sys.stdout.write(stdout)
            sys.stderr.write(stderr)
            return code
        parser = argparse.ArgumentParser(description=__doc__,
                                         formatter_class=argparse.RawDescriptionHelpFormatter)
        commands = parser.add_subparsers(dest="command", required=True)
        apply_parser = commands.add_parser("apply", help="replay outboxes into this database")
        apply_parser.add_argument("files", nargs="*", help="outbox files (default: all pending)")
        apply_parser.add_argument("--dry-run", action="store_true")
        commands.add_parser("pending", help="list outboxes not yet applied here")
        options = parser.parse_args(args)
        if options.command == "pending":
            for path, left in pending(root):
                print("%s: %d command(s) to apply" % (path, left))
            return 0
        if os.environ.get("CLAUDE_CODE_REMOTE"):
            raise OutboxError("apply runs on the dev machine, against the canonical database")
        files = [Path(f) for f in options.files] or [path for path, _ in pending(root)]
        total = sum(apply(path, dry_run=options.dry_run) for path in files)
        if not options.dry_run:
            print("%d command(s) applied. Next: bd dolt push, then remove the applied outbox "
                  "file(s) and commit." % total)
        return 0
    except OutboxError as error:
        print("bd_outbox: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
