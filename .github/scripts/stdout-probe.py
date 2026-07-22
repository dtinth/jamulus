#!/usr/bin/env python3
"""One-shot diagnostic: does jamulus-test.exe's stdout actually arrive when
launched via Python's subprocess with piped stdout on Windows, or is it 0
bytes like it was observed to be with cmd/pwsh capture methods?

Not part of the real test-running pipeline -- see
.github/workflows/stdout-probe.yml (experiment/windows-stdout-probe branch).
"""

import os
import platform
import subprocess

BUILD_DIR = "build-test"


def test_binary_path():
    name = "jamulus-test.exe" if platform.system() == "Windows" else "jamulus-test"
    return os.path.join(BUILD_DIR, name)


def main():
    binary = os.path.abspath(test_binary_path())
    print("Binary: {}".format(binary))
    print("Exists: {}".format(os.path.exists(binary)))

    # (a) subprocess.run with capture_output=True (pipes), default QtTest txt
    # output straight to stdout/stderr -- no -o writer arg at all.
    result_a = subprocess.run([binary], capture_output=True)
    print(
        "PROBE a: rc={} stdout_bytes={} stderr_bytes={}".format(
            result_a.returncode, len(result_a.stdout), len(result_a.stderr)
        )
    )
    if len(result_a.stdout) > 0:
        preview = result_a.stdout[:200]
        print("PROBE a: stdout preview: {!r}".format(preview))
    if len(result_a.stderr) > 0:
        preview = result_a.stderr[:200]
        print("PROBE a: stderr preview: {!r}".format(preview))

    # (b) same, but with stdout/stderr as a direct file handle instead of a
    # pipe -- rules out "pipe" specifically vs. "any redirection".
    probe_b_path = os.path.abspath("probe-b-stdout.txt")
    with open(probe_b_path, "wb") as f:
        result_b = subprocess.run([binary], stdout=f, stderr=subprocess.STDOUT)
    size_b = os.path.getsize(probe_b_path) if os.path.exists(probe_b_path) else -1
    print(
        "PROBE b: rc={} file_bytes={} path={}".format(
            result_b.returncode, size_b, probe_b_path
        )
    )
    if size_b > 0:
        with open(probe_b_path, "rb") as f:
            print("PROBE b: file preview: {!r}".format(f.read(200)))

    # (c) control: QTest's own file-based writer (the current production
    # workaround), captured with default (inherited) stdio.
    probe_c_path = os.path.abspath("probe-c-output.txt")
    result_c = subprocess.run(
        [binary, "-o", probe_c_path + ",txt"], capture_output=True
    )
    size_c = os.path.getsize(probe_c_path) if os.path.exists(probe_c_path) else -1
    print(
        "PROBE c: rc={} file_bytes={} inherited_stdout_bytes={} path={}".format(
            result_c.returncode, size_c, len(result_c.stdout), probe_c_path
        )
    )

    print("PROBE SUMMARY: a_stdout={} b_file={} c_file={}".format(
        len(result_a.stdout), size_b, size_c
    ))


if __name__ == "__main__":
    main()
