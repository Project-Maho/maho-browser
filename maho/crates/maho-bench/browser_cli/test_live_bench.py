import concurrent.futures
import json
import subprocess
import sys
import unittest
from unittest.mock import patch

import live_bench as bench


class LiveBenchTests(unittest.TestCase):
    def test_w1_submits_required_fields_and_checks_goal(self):
        class Form:
            def __init__(self):
                self.values = {}
                self.terms = False
                self.submitted = False
                self.verified = False

            def navigate(self, url):
                pass

            def type(self, value, name, *role):
                self.values[name] = value

            def select(self, value, name, *role):
                self.values[name] = value

            def click(self, name, *role):
                if name == "I accept the terms of service":
                    self.terms = not self.terms
                elif name == "Submit Application":
                    self.submitted = all(self.values.get(field) for field in
                                         ("Full Name", "Email Address", "Phone Number", "Primary Role")) and self.terms

            def wait_for_selector(self, selector):
                if selector == "#validation-message.success":
                    if not self.submitted:
                        raise RuntimeError("form validation prevented submission")
                    self.verified = True

        form = Form()
        bench.run_workload(form, "w1", "http://fixture/w1")
        self.assertTrue(form.submitted, "required role/terms were not completed")
        self.assertTrue(form.verified, "sample accepted without checking submission")

    def test_v1_navigation_uses_readiness_without_fixed_sleep(self):
        calls = []
        def tool(cap, args):
            calls.append((cap, args))
            return {"text": json.dumps({"result": {"found": True, "navigated": True}}), "wire": 1}
        with patch.object(bench, "tool_run", side_effect=tool), patch.object(bench.time, "sleep") as sleep:
            bench.V1Driver(bench.new_metrics(), 7).navigate("http://fixture/w1")
        self.assertFalse(sleep.called, "V1 adds an artificial delay")
        self.assertIn("page.wait_for_selector", [cap for cap, _ in calls])

    def test_pipe_timeout_covers_partial_line(self):
        # The real child reads the request, emits an incomplete JSON line, then
        # blocks on input. No sleep or scheduling race determines its response.
        child = subprocess.Popen([sys.executable, "-u", "-c",
            "import sys; sys.stdin.readline(); sys.stdout.write('{'); sys.stdout.flush(); sys.stdin.readline()"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert child.stdin is not None and child.stdout is not None and child.stderr is not None
        with patch.object(bench.subprocess, "Popen", return_value=child):
            driver = bench.V2Driver(bench.new_metrics(), 7)
        pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        future = pool.submit(driver.op, {"op": "observe"}, 0.05)
        try:
            with self.assertRaises(TimeoutError):
                try:
                    future.result(timeout=1)
                except concurrent.futures.TimeoutError:
                    if not future.done():
                        self.fail("pipe op remained blocked beyond its deadline")
                    raise
        finally:
            child.kill()
            child.wait(timeout=5)
            pool.shutdown(wait=True)
            child.stdin.close()
            child.stdout.close()
            child.stderr.close()


if __name__ == "__main__":
    unittest.main()
