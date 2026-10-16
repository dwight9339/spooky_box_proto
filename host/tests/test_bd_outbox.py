"""Offline tests for host/tools/bd_outbox.py: recording, ID remapping and replay.

A fake bd stands in for the real one; no Beads database is needed.
"""

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bd_outbox as ob  # noqa: E402


class FakeBd:
    """Assigns IDs from a list and logs every command it runs."""

    def __init__(self, ids, fail_on=None):
        self.ids = list(ids)
        self.calls = []
        self.fail_on = fail_on

    def __call__(self, argv):
        self.calls.append(list(argv))
        if self.fail_on and self.fail_on in argv:
            return 1, "", "boom\n"
        command, _ = ob.command_words(argv)
        if command == "create":
            assert "--json" in argv and "--silent" not in argv
            return 0, json.dumps({"id": self.ids.pop(0), "title": "t"}), "warning\n"
        if command == "q":
            return 0, self.ids.pop(0) + "\n", ""
        return 0, "ok %s\n" % command, ""


class BdOutboxTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.path = ob.outbox_path(self.root, "claude/p04.16-slicer-steps")

    def tearDown(self):
        self._tmp.cleanup()

    def record(self, bd, *argv):
        return ob.record(list(argv), self.path, runner=bd, who="Claude", now="2026-10-16T00:00:00+00:00")

    def test_classify(self):
        self.assertEqual(ob.classify(["show", "p-1"]), "read")
        self.assertEqual(ob.classify(["--actor", "A", "list"]), "read")
        self.assertEqual(ob.classify(["update", "p-1", "--dry-run"]), "read")
        self.assertEqual(ob.classify(["dep", "tree", "p-1"]), "read")
        self.assertEqual(ob.classify(["comments", "p-1"]), "read")
        self.assertEqual(ob.classify(["comments", "add", "p-1", "x"]), "write")
        self.assertEqual(ob.classify(["dep", "add", "p-1", "p-2"]), "write")
        self.assertEqual(ob.classify(["close", "p-1"]), "write")
        self.assertEqual(ob.classify(["create", "--title=x"]), "create")
        self.assertEqual(ob.classify(["q", "x"]), "create")
        self.assertEqual(ob.classify(["dolt", "push"]), "refused")
        self.assertEqual(ob.classify(["bootstrap"]), "refused")
        self.assertEqual(ob.classify(["create", "--graph=plan.json"]), "refused")

    def test_outbox_name_follows_the_branch(self):
        self.assertEqual(self.path.name, "claude__p04.16-slicer-steps.jsonl")
        self.assertEqual(self.path.parent, self.root / ".beads" / "outbox")

    def test_reads_run_and_are_not_recorded(self):
        bd = FakeBd([])
        self.assertEqual(self.record(bd, "show", "p-1")[0], 0)
        self.assertFalse(self.path.exists())

    def test_refused_commands_do_not_run(self):
        bd = FakeBd([])
        with self.assertRaises(ob.OutboxError):
            self.record(bd, "dolt", "push")
        self.assertEqual(bd.calls, [])

    def test_failed_writes_are_not_recorded(self):
        bd = FakeBd([], fail_on="p-9")
        self.assertEqual(self.record(bd, "close", "p-9")[0], 1)
        self.assertFalse(self.path.exists())

    def test_record_captures_created_ids(self):
        bd = FakeBd(["proj-a1", "proj-a1.1"])
        code, stdout, _ = self.record(bd, "create", "--title=x", "--silent")
        self.assertEqual(code, 0)
        self.assertIn("proj-a1", stdout)
        self.record(bd, "q", "child", "--parent=proj-a1")
        self.record(bd, "update", "proj-a1", "--claim")
        entries = ob.read_outbox(self.path)
        self.assertEqual([e["seq"] for e in entries], [1, 2, 3])
        self.assertEqual(entries[0]["created"], "proj-a1")
        self.assertEqual(entries[0]["argv"], ["create", "--title=x", "--silent"])
        self.assertEqual(entries[1]["created"], "proj-a1.1")
        self.assertNotIn("created", entries[2])
        self.assertEqual(entries[2]["actor"], "Claude")
        self.assertTrue((self.path.parent / ".gitignore").exists())

    def test_rewrite_replaces_whole_ids_only(self):
        ids = {"proj-v7l.17": "proj-v7l.19"}
        self.assertEqual(ob.rewrite("--parent=proj-v7l.17", ids), "--parent=proj-v7l.19")
        self.assertEqual(ob.rewrite("see proj-v7l.17.", ids), "see proj-v7l.19.")
        self.assertEqual(ob.rewrite("(v7l.17) and v7l.17,", ids), "(v7l.19) and v7l.19,")
        self.assertEqual(ob.rewrite("proj-v7l.170 proj-v7l.17.1 xv7l.17", ids),
                         "proj-v7l.170 proj-v7l.17.1 xv7l.17")

    def test_apply_remaps_ids_and_resumes(self):
        session = FakeBd(["proj-a1", "proj-a1.1"])
        self.record(session, "create", "--title=x")
        self.record(session, "create", "--title=child", "--parent=proj-a1")
        self.record(session, "update", "proj-a1.1", "--append-notes", "child of proj-a1 (a1)")
        self.record(session, "dep", "add", "proj-a1.1", "proj-old")
        self.record(session, "close", "proj-a1")

        canonical = FakeBd(["proj-b7", "proj-b7.1"], fail_on="proj-old")
        with self.assertRaises(ob.OutboxError):
            ob.apply(self.path, runner=canonical, say=lambda _: None)
        self.assertEqual(canonical.calls[1], ["--actor", "Claude", "create", "--title=child",
                                              "--parent=proj-b7", "--json"])
        self.assertEqual(canonical.calls[2], ["--actor", "Claude", "update", "proj-b7.1",
                                              "--append-notes", "child of proj-b7 (b7)"])
        self.assertEqual(ob.load_state(self.path)["applied"], [1, 2, 3])

        canonical.fail_on = None
        self.assertEqual(ob.apply(self.path, runner=canonical, say=lambda _: None), 2)
        self.assertEqual(canonical.calls[-1], ["--actor", "Claude", "close", "proj-b7"])
        self.assertEqual(ob.apply(self.path, runner=canonical, say=lambda _: None), 0)
        self.assertEqual(ob.pending(self.root), [])

    def test_dry_run_changes_nothing(self):
        session = FakeBd(["proj-a1"])
        self.record(session, "create", "--title=x")
        self.record(session, "close", "proj-a1")
        lines = []
        canonical = FakeBd([])
        self.assertEqual(ob.apply(self.path, runner=canonical, dry_run=True, say=lines.append), 0)
        self.assertEqual(canonical.calls, [])
        self.assertIn("<new id for proj-a1>", lines[1])
        self.assertFalse(ob.state_path(self.path).exists())
        self.assertEqual(ob.pending(self.root), [(self.path, 2)])

    def test_unknown_format_is_rejected(self):
        self.path.parent.mkdir(parents=True)
        self.path.write_text('{"v": 9, "seq": 1, "argv": []}\n', encoding="utf-8")
        with self.assertRaises(ob.OutboxError):
            ob.read_outbox(self.path)


if __name__ == "__main__":
    unittest.main()
