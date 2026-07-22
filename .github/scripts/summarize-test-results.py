#!/usr/bin/env python3
"""Parses test-results.xml (the junit-flavoured QtTest report written by
run-unit-tests.py's "run" subcommand) and:

  - appends a compact per-suite pass/fail table (plus any failing test names
    and messages) to $GITHUB_STEP_SUMMARY
  - IS the job's test-count gate: exits nonzero unless the suite reports at
    least one test AND zero failures AND zero errors. There is no hardcoded
    expected test count anywhere in this script or the workflow -- the gate
    is "some tests ran and none of them failed", not "exactly N tests ran",
    so a build that silently exits 0 having run nothing still fails the job,
    while the suite itself is free to grow without editing CI at all.

Meant to run as its own workflow step with `if: always()`, so the summary
table (and this gate) still show up/run even when the build or run step
before it failed outright -- rendering always happens before the gate check,
so a failing job's summary is not silently swallowed by an early exit.
"""

import os
import sys
import xml.etree.ElementTree as ET

RESULTS_PATH = "test-results.xml"


def read_suites ( path ):
    root = ET.parse ( path ).getroot()
    return [ root ] if root.tag == "testsuite" else list ( root.findall ( "testsuite" ) )


def write_summary ( summary_path, lines ):
    if not summary_path:
        sys.stdout.writelines ( lines )
        return

    with open ( summary_path, "a", encoding="utf-8" ) as f:
        f.writelines ( lines )


def main():
    job_name = os.environ.get ( "JOB_NAME", "unit tests" )
    summary_path = os.environ.get ( "GITHUB_STEP_SUMMARY" )

    lines = [ "### JUnit results: {}\n\n".format ( job_name ) ]

    suites = None
    if not os.path.exists ( RESULTS_PATH ):
        lines.append ( "_No {} was produced (the build or test run likely failed before it could be written)._\n\n".format ( RESULTS_PATH ) )
    else:
        try:
            suites = read_suites ( RESULTS_PATH )
        except ET.ParseError as e:
            lines.append ( "_{} could not be parsed ({})._\n\n".format ( RESULTS_PATH, e ) )

    if suites is None:
        write_summary ( summary_path, lines )
        sys.exit ( "gate failed: no usable {} -- treating as 0 tests run".format ( RESULTS_PATH ) )

    lines.append ( "| Suite | Tests | Passed | Failed | Skipped | Time (s) |\n" )
    lines.append ( "|---|---|---|---|---|---|\n" )

    total_tests = 0
    total_failed = 0
    failures = []

    for suite in suites:
        tests = int ( suite.get ( "tests", 0 ) )
        failed = int ( suite.get ( "failures", 0 ) ) + int ( suite.get ( "errors", 0 ) )
        skipped = int ( suite.get ( "skipped", 0 ) )
        passed = tests - failed - skipped
        status = "PASS" if failed == 0 else "FAIL"

        total_tests += tests
        total_failed += failed

        lines.append (
            "| {} {} | {} | {} | {} | {} | {} |\n".format ( status, suite.get ( "name" ), tests, passed, failed, skipped, suite.get ( "time", "?" ) )
        )

        for testcase in suite.findall ( "testcase" ):
            node = testcase.find ( "failure" )
            if node is None:
                node = testcase.find ( "error" )
            if node is not None:
                failures.append ( ( testcase.get ( "name" ), ( node.get ( "message" ) or "" ).strip() ) )

    if failures:
        lines.append ( "\n<details><summary>Failed tests</summary>\n\n" )
        for name, message in failures:
            lines.append ( "- `{}`: {}\n".format ( name, message ) )
        lines.append ( "\n</details>\n" )

    lines.append ( "\n" )

    write_summary ( summary_path, lines )

    # Gate strictly after rendering: a failing job still gets its summary.
    if total_tests == 0:
        sys.exit ( "gate failed: {} reported 0 tests -- a build that silently runs nothing must not pass".format ( RESULTS_PATH ) )

    if total_failed != 0:
        sys.exit ( "gate failed: {} of {} tests failed".format ( total_failed, total_tests ) )

    print ( "gate passed: {} tests, 0 failures".format ( total_tests ) )


if __name__ == "__main__":
    main()
