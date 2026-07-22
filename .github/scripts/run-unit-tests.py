#!/usr/bin/env python3
"""Cross-platform build+run driver for the protocol unit test suite.

Replaces the separate "Build unit tests (Unix)" / "Build unit tests
(Windows)" / "Run unit tests (Unix)" / "Run unit tests (Windows)" steps that
used to live inline in .github/workflows/unit-tests.yml, plus (on Windows)
the MSVC environment bootstrap that used to be its own workflow step: that
bootstrap is folded in here rather than kept as a separate step, because
"build" and "run" are now two separate GitHub Actions steps (each a fresh
shell) -- an MSVC environment set up via
[System.Environment]::SetEnvironmentVariable in one PowerShell step does not
survive into the next step anyway, so it would have to be re-plumbed through
$GITHUB_ENV. Doing the same vcvarsall diff-and-apply from inside this
process, immediately before invoking qmake/nmake, is simpler and keeps every
platform special case for building/running in one file instead of splitting
it across YAML and two languages.

Usage:
  python3 run-unit-tests.py build   -- qmake + make/nmake, out-of-tree in build-test/
  python3 run-unit-tests.py run     -- run the built binary, write test-output.txt
                                        and test-results.xml, print the text
                                        report, exit with the binary's own
                                        exit code (NOT a test-count gate --
                                        that is summarize-test-results.py's
                                        job, reading test-results.xml in its
                                        own separate step)

Reads from the environment (set by the workflow, from the job's matrix):
  QMAKE_BIN         -- qmake executable name (default "qmake")
  QMAKE_EXTRA_ARGS  -- extra qmake command line arguments, shell-quoted as one
                       string (e.g. QMAKE_CXXFLAGS+="--coverage"); split with
                       shlex so quoting behaves the same as it did when the
                       workflow used to interpolate this directly onto a
                       shell command line
"""

import os
import platform
import re
import shlex
import subprocess
import sys

BUILD_DIR = "build-test"


def qmake_extra_args():
    return shlex.split ( os.environ.get ( "QMAKE_EXTRA_ARGS", "" ) )


def run ( cmd, **kwargs ):
    print ( "+ " + " ".join ( cmd ) )
    subprocess.run ( cmd, check=True, **kwargs )


def msvc_environment():
    """Returns the environment variables vcvarsall.bat x64 adds/changes, by
    diffing `cmd /c set` before and after calling it -- the same technique
    ilammy/msvc-dev-cmd uses under the hood, proven to work on this runner (a
    plain nested "call" left VCToolsInstallDir unset even though vcvarsall.bat
    itself reported success)."""
    vswhere = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    vs_path = subprocess.run (
        [
            vswhere,
            "-latest",
            "-prerelease",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    vcvarsall = os.path.join ( vs_path, "VC", "Auxiliary", "Build", "vcvarsall.bat" )

    def snapshot ( cmd ):
        out = subprocess.run ( cmd, shell=True, capture_output=True, text=True ).stdout
        env = {}
        for line in out.splitlines():
            m = re.match ( r"^([^=]+)=(.*)$", line )
            if m:
                env[m.group ( 1 )] = m.group ( 2 )
        return env

    before = snapshot ( "cmd /c set" )
    after = snapshot ( 'cmd /c "call \\"{}\\" x64 >nul && set"'.format ( vcvarsall ) )

    return { name: value for name, value in after.items() if before.get ( name ) != value }


def apply_msvc_environment():
    for name, value in msvc_environment().items():
        os.environ[name] = value

    print ( "VCToolsInstallDir={}".format ( os.environ.get ( "VCToolsInstallDir", "" ) ) )


def cmd_build():
    os.makedirs ( BUILD_DIR, exist_ok=True )
    qmake_bin = os.environ.get ( "QMAKE_BIN", "qmake" )

    if platform.system() == "Windows":
        apply_msvc_environment()
        run (
            [ qmake_bin, "..\\src\\test\\test.pro", "CONFIG-=debug_and_release", "CONFIG+=release", "DESTDIR=." ],
            cwd=BUILD_DIR,
        )
        run ( [ "nmake" ], cwd=BUILD_DIR )
    else:
        run ( [ qmake_bin, "../src/test/test.pro" ] + qmake_extra_args(), cwd=BUILD_DIR )
        run ( [ "make", "-j{}".format ( os.cpu_count() or 1 ) ], cwd=BUILD_DIR )


def test_binary_path():
    name = "jamulus-test.exe" if platform.system() == "Windows" else "jamulus-test"
    return os.path.join ( BUILD_DIR, name )


def pick_junit_format ( binary ):
    # QtTest's junit-flavoured logger is called "xunitxml" on Qt 5.15 and was
    # renamed "junitxml" on Qt 6.x -- ask the binary itself via -help instead
    # of hardcoding it by Qt version, so this keeps working if the name
    # changes again. Verified empirically against this workflow's own Qt 5
    # job log: both names are handled here, but only "junitxml" is what
    # actually gets used on every job this workflow currently runs.
    help_text = subprocess.run ( [ binary, "-help" ], capture_output=True, text=True ).stdout
    return "junitxml" if "junitxml" in help_text else "xunitxml"


def cmd_run():
    binary = test_binary_path()
    junit_format = pick_junit_format ( binary )
    print ( "Using QtTest JUnit logger format: " + junit_format )

    # "-o file,txt" (rather than "-o -,txt" straight to stdout) on *both*
    # platforms: on the Windows runner this process's stdout came back
    # completely empty (0 bytes) no matter how it was captured -- cmd
    # redirection, pwsh Tee-Object, pwsh *> -- even though the process
    # demonstrably ran, so QtTest's own file based logger is what's proven to
    # work there. Using the same approach on Unix keeps this function
    # platform-independent instead of needing a branch here too.
    result = subprocess.run ( [ binary, "-o", "test-output.txt,txt", "-o", "test-results.xml," + junit_format ] )

    if os.path.exists ( "test-output.txt" ):
        with open ( "test-output.txt", encoding="utf-8", errors="replace" ) as f:
            sys.stdout.write ( f.read() )

    # Only the exit code is gated here -- e.g. a crash or a sanitizer abort.
    # Whether the *tests themselves* passed is summarize-test-results.py's
    # job, reading test-results.xml in its own, separate step.
    return result.returncode


def main():
    if len ( sys.argv ) != 2 or sys.argv[1] not in ( "build", "run" ):
        sys.exit ( "usage: run-unit-tests.py <build|run>" )

    if sys.argv[1] == "build":
        cmd_build()
    else:
        sys.exit ( cmd_run() )


if __name__ == "__main__":
    main()
