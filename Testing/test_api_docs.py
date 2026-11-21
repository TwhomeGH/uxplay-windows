"""Behavioral tests for incremental documentation and failure preservation.

Fixtures stay in ignored build directories; these tests do not delete sites.
"""
import argparse
from contextlib import redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("build_api_docs", ROOT / "scripts/build_api_docs.py")
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)


class ApiDocsTests(unittest.TestCase):
    """Verify real cache/publication behavior using a minimal deterministic generator."""
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="api-docs-test-", dir=ROOT / "build"))
        for project in [self.root, self.root / "libuxplay"]:
            project.mkdir(exist_ok=True)
            (project / "Doxyfile").write_text("INPUT = api.h\nFILE_PATTERNS = *.h\n", encoding="utf-8")
            (project / "api.h").write_text("int tick(int value);\n", encoding="utf-8")
        (self.root / "scripts").mkdir()
        (self.root / "scripts/build_docs.ps1").write_text("# entry\n", encoding="utf-8")
        self.args = argparse.Namespace(check=False, force=False, docker=False,
                                       doxygen="test-doxygen", target="All")

    def generate(self, root, project, name, run, tool, engine_tag):
        """Produce valid sibling entry pages and a constant declaration snapshot."""
        folder = run / name
        folder.mkdir()
        other = "app" if name == "libuxplay" else "libuxplay"
        (folder / "index.html").write_text(f'<a href="../{other}/index.html">API</a>', encoding="utf-8")
        (folder / (name + ".tag")).write_text(
            '<tagfile><compound kind="file"><name>api.h</name><member kind="function">'
            '<name>tick</name><type>int</type><arglist>(int value)</arglist>'
            '</member></compound></tagfile>', encoding="utf-8")
        return folder

    def invoke(self, generator=None):
        """Run against an isolated workspace, capturing expected Problems diagnostics."""
        with patch.object(docs, "ROOT", self.root), \
                patch.object(docs.subprocess, "check_output", return_value=b"1.16.1\n"), \
                patch.object(docs, "generate", side_effect=generator or self.generate) as build, \
                redirect_stdout(io.StringIO()):
            result = docs.run(self.args)
        return result, build.call_count

    def test_check_is_read_only_without_baseline(self):
        before = docs.tree_files(self.root)
        self.args.check = True
        self.assertEqual(self.invoke(), (1, 0))
        self.assertEqual(docs.tree_files(self.root), before)

    def test_unchanged_and_timestamp_only_skip(self):
        self.assertEqual(self.invoke(), (0, 2))
        os.utime(self.root / "api.h", None)
        self.assertEqual(self.invoke(), (0, 0))
        self.args.check = True
        self.assertEqual(self.invoke(), (0, 0))

    def test_engine_comment_change_does_not_rebuild_app_when_tag_is_identical(self):
        self.invoke()
        source = self.root / "libuxplay/api.h"
        source.write_text(source.read_text(encoding="utf-8") + "// explanation\n", encoding="utf-8")
        self.assertEqual(self.invoke(), (0, 1))

    def test_missing_entry_is_outdated(self):
        self.invoke()
        page = self.root / "build/doxygen/app/index.html"
        os.replace(page, page.with_suffix(".saved"))
        self.args.check = True
        self.assertEqual(self.invoke(), (1, 0))

    def test_failed_second_build_preserves_successful_outputs_and_state(self):
        self.invoke()
        output = self.root / "build/doxygen"
        before = docs.tree_files(output / "app"), docs.tree_files(output / "libuxplay")
        state = (output / "state.json").read_bytes()
        self.args.force = True

        def failing(*arguments):
            if arguments[2] == "app":
                raise ValueError("simulated Doxygen warning")
            return self.generate(*arguments)

        with self.assertRaises(ValueError):
            self.invoke(failing)
        self.assertEqual(before, (docs.tree_files(output / "app"), docs.tree_files(output / "libuxplay")))
        self.assertEqual(state, (output / "state.json").read_bytes())

    def test_publication_failure_rolls_back_both_sites(self):
        run = self.root / "build/doxygen/runs/rollback"
        run.mkdir(parents=True)
        sites = []
        for name in ["app", "libuxplay"]:
            old = self.root / "build/doxygen" / name
            old.mkdir()
            (old / "index.html").write_text("old", encoding="utf-8")
            candidate = run / name
            candidate.mkdir()
            (candidate / "index.html").write_text("new", encoding="utf-8")
            sites.append((candidate, old))
        replace = os.replace
        calls = 0

        def fail_once(source, destination):
            nonlocal calls
            calls += 1
            if calls == 4:
                raise OSError("simulated activation failure")
            return replace(source, destination)

        with patch.object(docs.os, "replace", side_effect=fail_once), self.assertRaises(OSError):
            docs.publish(sites, run, self.root)
        for _, destination in sites:
            self.assertEqual((destination / "index.html").read_text(encoding="utf-8"), "old")

    def test_signature_change_and_output_boundary(self):
        self.assertEqual(docs.api_diff({"tick": ["int (int)"]}, {"tick": ["int (double)"], "new": ["void ()"]}),
                         {"added": ["new"], "removed": [], "changed": ["tick"]})
        with self.assertRaises(ValueError):
            docs.safe_path(self.root.parent / "outside", self.root)

    def test_concurrent_writer_is_rejected_and_lock_is_reusable(self):
        directory = self.root / "build/doxygen"
        with docs.build_lock(directory):
            with self.assertRaises(ValueError):
                with docs.build_lock(directory):
                    self.fail("A second writer acquired the same lock")
        with docs.build_lock(directory):
            self.assertTrue((directory / ".build.lock").is_file())

    def test_unchanged_output_cache_does_not_read_html_content(self):
        site = self.root / "html"
        site.mkdir()
        (site / "index.html").write_text("content", encoding="utf-8")
        hashes, metadata = docs.tree_files(site), docs.output_metadata(site)
        with patch.object(Path, "read_bytes", side_effect=AssertionError("Unexpected HTML content read")):
            self.assertEqual(docs.cached_output_files(site, hashes, metadata), hashes)

    def test_dynamic_inputs_are_rejected_but_documentation_comments_are_not(self):
        config = self.root / "Doxyfile"
        text = config.read_text(encoding="utf-8")
        config.write_text(text + "# @INCLUDE and $(ENV) are not supported\n", encoding="utf-8")
        self.assertIn("api.h", docs.input_files(self.root))
        config.write_text(text + "INPUT = $(EXTRA_INPUT)\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            docs.input_files(self.root)

    def test_temporary_permission_error_is_retried(self):
        source, destination = self.root / "candidate", self.root / "published"
        source.write_text("valid", encoding="utf-8")
        replace = os.replace
        calls = 0

        def temporarily_locked(old, new):
            nonlocal calls
            calls += 1
            if calls < 3:
                raise PermissionError("Temporary scanner lock")
            return replace(old, new)

        with patch.object(docs.os, "replace", side_effect=temporarily_locked), \
                patch.object(docs.time, "sleep"):
            docs.replace_generated(source, destination)
        self.assertEqual(calls, 3)
        self.assertEqual(destination.read_text(encoding="utf-8"), "valid")


if __name__ == "__main__":
    unittest.main()
