"""Offline fixture tests; no serial connection or third-party packages."""

from copy import deepcopy
from html.parser import HTMLParser
import io
import json
from pathlib import Path
import re
from tempfile import TemporaryDirectory
import unittest
from contextlib import redirect_stdout, redirect_stderr

import trace_viewer as viewer


FIXTURE = Path(__file__).parent / "fixtures" / "cooperative_38.jsonl"


def fixture():
    return [json.loads(line) for line in FIXTURE.read_text().splitlines()]


def edit(rows, prefix, old, new, occurrence=0):
    matches = [row for row in rows if row.get("event") == "line" and row["text"].startswith(prefix)]
    matches[occurrence]["text"] = matches[occurrence]["text"].replace(old, new)


class TraceTests(unittest.TestCase):
    def test_normal_38(self):
        report = viewer.analyze(fixture())
        self.assertEqual(report["validation"], "PASS")
        self.assertEqual(report["trace"], "COMPLETE", report["issues"])
        self.assertEqual((report["count"], report["handoffs"], report["marks"], report["overwritten"]),
                         (38, 8, "ADGBEHCFI", 0))
        self.assertEqual(len(report["links"]), 8)
        self.assertTrue(report["simulation"])
        # Receive timestamps are deliberately all identical in the fixture.
        self.assertEqual(report["records"][0]["us"], 5)
        self.assertEqual(report["records"][-1]["us"], 190)

    def test_pointer_domains_and_commit(self):
        report = viewer.analyze(fixture())
        first = report["records"][0]
        self.assertEqual(first["active_name"], "A")
        self.assertEqual(first["current_name"], "A")
        self.assertNotEqual(first["active"], first["current"])
        commit = report["records"][4]
        self.assertEqual((commit["subject_name"], commit["active_name"], commit["value"]), ("A", "D", 4))

    def test_missing_record_duplicate_sequence_and_count(self):
        rows = fixture()
        for change in ("missing", "duplicate", "count"):
            mutated = deepcopy(rows)
            if change == "missing":
                mutated = [row for row in mutated if not row.get("text", "").startswith("PICO_TRACE seq=9 ")]
            elif change == "duplicate":
                edit(mutated, "PICO_TRACE seq=6 ", "seq=6", "seq=5")
            else:
                edit(mutated, "PICO_TRACE_BEGIN ", "count=38", "count=37")
            report = viewer.analyze(mutated)
            self.assertEqual(report["trace"], "INCOMPLETE", change)
            self.assertEqual(report["validation"], "PASS")
            self.assertGreater(len(report["records"]), 0)

    def test_missing_or_mismatched_begin_end(self):
        for prefix in ("PICO_TRACE_BEGIN ", "PICO_TRACE_END "):
            rows = [row for row in fixture() if not row.get("text", "").startswith(prefix)]
            report = viewer.analyze(rows)
            self.assertEqual(report["trace"], "INCOMPLETE")
            self.assertEqual(len(report["records"]), 38)
        rows = fixture()
        edit(rows, "PICO_TRACE_END ", viewer.PHASE, "wrong_phase")
        self.assertEqual(viewer.analyze(rows)["trace"], "INCOMPLETE")

    def test_request_references_missing_wrong_target_and_consumed(self):
        for old, new in (("value=4", "value=999"), ("value=4", "value=3"),
                         ("active=0x20000120", "active=0x20000140")):
            rows = fixture()
            edit(rows, "PICO_TRACE seq=5 ", old, new)
            report = viewer.analyze(rows)
            self.assertEqual(report["trace"], "INCOMPLETE")
            self.assertTrue(any("reference" in issue for issue in report["issues"]))
        rows = fixture()
        edit(rows, "PICO_TRACE seq=10 ", "value=9", "value=4")
        self.assertEqual(viewer.analyze(rows)["trace"], "INCOMPLETE")

    def test_time_invalid_decreasing_negative_and_equal(self):
        for old, new in (("us=30", "us=0"), ("us=30", "us=-1"), ("us=30", "us=4294967296")):
            rows = fixture()
            edit(rows, "PICO_TRACE seq=6 ", old, new)
            report = viewer.analyze(rows)
            self.assertEqual(report["trace"], "INCOMPLETE")
            self.assertEqual(len(report["records"]), 38)
        rows = fixture()
        edit(rows, "PICO_TRACE seq=6 ", "us=30", "us=25")
        self.assertEqual(viewer.analyze(rows)["trace"], "COMPLETE")

    def test_overwritten_overflow_and_time_valid(self):
        for name, old, new in (("overwritten", "0", "1"), ("seq_overflow", "0", "1"), ("time_valid", "1", "0")):
            rows = fixture()
            edit(rows, "PICO_TRACE_BEGIN ", name + "=" + old, name + "=" + new)
            if name == "overwritten":
                edit(rows, "PICO_TRACE_END ", "overwritten=0", "overwritten=1")
            report = viewer.analyze(rows)
            self.assertEqual(report["trace"], "INCOMPLETE")
            self.assertEqual(report["validation"], "PASS")

    def test_validation_fail_independent(self):
        rows = fixture()
        edit(rows, "KARQRI_PICO_VALIDATION_PASS", "PASS", "FAIL count=1")
        report = viewer.analyze(rows)
        self.assertEqual(report["validation"], "FAIL")
        self.assertEqual(report["trace"], "COMPLETE")

    def test_missing_validation_start_does_not_invalidate_trace(self):
        rows = [row for row in fixture() if row.get("text") != "KARQRI_PICO_VALIDATION_START"]
        report = viewer.analyze(rows)
        self.assertEqual(report["validation"], "FAIL")
        self.assertEqual(report["trace"], "COMPLETE")

    def test_malformed_fields_mapping_and_unknown_event(self):
        for prefix, old, new in (("PICO_TRACE seq=1 ", "value=0", "value=nope"),
                                 ("PICO_TRACE seq=1 ", "flags=0x00", "flags=bad"),
                                 ("PICO_TRACE seq=1 ", "active=0x20000100", "active=0x99999999"),
                                 ("PICO_TRACE seq=1 ", "event=THREAD_BOOTSTRAP", "event=UNSUPPORTED"),
                                 ("PICO_TRACE_TASK name=D", "name=D", "name=A")):
            rows = fixture()
            edit(rows, prefix, old, new)
            report = viewer.analyze(rows)
            self.assertEqual(report["trace"], "INCOMPLETE")
            self.assertIn("<table>", viewer.render(report, "mock"))

    def test_reconnect_and_run_ids_not_combined(self):
        rows = fixture()
        for row in rows:
            if row.get("text", "").startswith("PICO_TRACE seq=20 "):
                row["attempt"] = 2
        report = viewer.analyze(rows)
        self.assertEqual(report["trace"], "INCOMPLETE")
        self.assertEqual(report["count"], 37)
        other = deepcopy(fixture())
        for row in other:
            row["attempt"] = 2
        self.assertEqual(viewer.analyze(fixture() + other)["trace"], "INCOMPLETE")
        self.assertEqual(viewer.analyze(fixture() + other, attempt=2)["trace"], "COMPLETE")

    def test_empty_input(self):
        report = viewer.analyze([])
        self.assertEqual(report["trace"], "INCOMPLETE")
        self.assertEqual(report["validation"], "TIMEOUT")
        self.assertIn("Trace <b>INCOMPLETE", viewer.render(report, "empty"))


class HtmlTests(unittest.TestCase):
    def test_xss_escaping_and_inline_only(self):
        rows = fixture()
        attack = '</script><script>alert("xss")</script>'
        edit(rows, "PICO_TRACE seq=1 ", "event=THREAD_BOOTSTRAP", "event=" + attack)
        page = viewer.render(viewer.analyze(rows), '<img src=x onerror="alert(1)">')
        self.assertNotIn(attack, page)
        self.assertNotIn('<img src=x', page)
        self.assertIn("\\u003c/script\\u003e", page)
        self.assertNotIn("innerHTML", page)
        parsed = []

        class Tags(HTMLParser):
            def handle_starttag(self, tag, attrs):
                parsed.append((tag, dict(attrs)))

        Tags().feed(page)
        self.assertFalse(any(tag in ("iframe", "img", "link") or "src" in attrs
                             for tag, attrs in parsed))
        self.assertEqual(sum(tag == "script" for tag, _ in parsed), 2)
        self.assertIn("connect-src 'none'", page)

    def test_embedded_data_and_table(self):
        report = viewer.analyze(fixture())
        page = viewer.render(report, "fixture")
        match = re.search(r'<script id="trace-data" type="application/json">(.*?)</script>', page, re.S)
        self.assertEqual(json.loads(match[1])["count"], 38)
        self.assertEqual(page.count('data-index="'), 38)
        self.assertIn("exception return直前", page)
        self.assertIn("createElementNS", page)
        self.assertIn("REQUEST_SET", page)

    def test_file_cli_and_no_overwrite(self):
        with TemporaryDirectory() as directory, redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            output = Path(directory) / "trace.html"
            self.assertEqual(viewer.main([str(FIXTURE), "-o", str(output)]), 0)
            self.assertTrue(output.exists())
            self.assertEqual(viewer.main([str(FIXTURE), "-o", str(output)]), 2)
            bad = Path(directory) / "bad.jsonl"
            bad.write_text('{"bad"\n' + FIXTURE.read_text())
            report = viewer.read_log(bad)
            self.assertEqual(report["trace"], "INCOMPLETE")
            self.assertEqual(report["validation"], "UNKNOWN")
            self.assertEqual(viewer.main([str(bad), "-o", str(Path(directory) / "bad.html")]), 1)
            self.assertEqual(viewer.main([str(bad), "-o", str(bad)]), 2)


if __name__ == "__main__":
    unittest.main()
