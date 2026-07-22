# What a test/CI contribution to jamulussoftware/jamulus must satisfy to be mergeable

Research date: 2026-07-22. Baseline: upstream `main` at 49f4f8d6.
All file references are to this repo at that commit; all URLs are to
https://github.com/jamulussoftware/jamulus unless stated otherwise.

## 0. Process gate: agree on a spec before coding

- CONTRIBUTING.md (lines 5–8): if an issue exists, comment there that you want to
  work on it; otherwise post in GitHub Discussions first. "Please begin coding only
  after we have agreed on a specification to avoid putting a lot of effort into
  something that may not be accepted later."
- For a testing PR, the natural anchor issues are #1413 (closed as duplicate) and
  #2428 (open) — see section 5.
- CONTRIBUTING.md ("Merging pull requests", line 118): `main` is protected and a PR
  needs **at least two positive reviews from the main developers** before merge.
- CONTRIBUTING.md ("Ownership", lines 103–108): the submitter is responsible for the
  PR's "care and feeding" — answering questions and making agreed changes.

## 1. License headers for new files

Source: CONTRIBUTING.md "Licensing" (lines 49–65); living examples: top of
`src/protocol.h`, `src/testbench.h`, `tools/update-copyright-notices.sh`.

- Since Jamulus 3.12.1dev (commit eb172d47), **all new source code contributions
  must be licensed AGPL 3.0 or any later version**.
- A **completely new file** must include a header with (CONTRIBUTING.md lines 57–62):
  - Copyright line(s)
  - Author(s)/copyright holder(s)
  - License declaration: *"Licensed under AGPL 3.0 or any later version. See COPYING
    for details."*
  - The long warranty-disclaimer boilerplate from existing files is optional.
- Existing files carry a dual GPL/AGPL explanation block because they contain
  pre-eb172d47 code; a brand-new test file does **not** need the GPL section — the
  short AGPL declaration above is what CONTRIBUTING.md requires.
- Practical formatting note: `tools/update-copyright-notices.sh` (run yearly by
  `.github/workflows/update-copyright-notices.yml`) rewrites lines matching
  `(\*|#).*Copyright.*YYYY(-YYYY)?` in files found under
  `android ios linux mac src windows tools .github` (script lines 53–60). Use a
  `Copyright (c) 2026` line in a `*`- or `#`-prefixed comment so the bot can bump it,
  and note the **directory list does not include a new top-level `tests/` dir** —
  either place tests under one of those dirs (e.g. `src/`) or extend the script.
- No CLA and no DCO/sign-off requirement: nothing in CONTRIBUTING.md, no CLA/DCO
  files under `.github/`, and no DCO check-run appears on upstream commits
  (checked check-runs on 49f4f8d6 via the GitHub API: only Auto-Build, coding style,
  copyright-notice and CodeQL jobs).

## 2. Qt version floor and API implications for QtTest code

- `Jamulus.pro` lines 3–7: hard qmake error below Qt 5.12
  ("Jamulus requires at least Qt5.12", introduced by PR #3288 for
  lrelease/embed_translations).
- CONTRIBUTING.md line 79: "Check to see if any newly introduced Qt calls,
  parameters, properties or constants are available in the minimum supported Qt
  version, which is currently **5.12.2**."
- CONTRIBUTING.md line 80: "Maintain **C++11** compatibility throughout the code."
  (`Jamulus.pro` line ~294 sets `CONFIG += c++11` on unix; Android uses C++17 only
  because liboboe needs it, line ~256.) So no C++14/17-isms in test code.
- CI actually builds a **spread of Qt versions** (`.github/workflows/autobuild.yml`
  matrix): Linux .deb builds use the distro Qt on ubuntu-22.04/22.04-arm
  (Qt 5.15.x via `qtbase5-dev`, see `.github/autobuild/linux_deb.sh` line 108),
  macOS main target uses `QT_VERSION=6.10.2`, macOS legacy and iOS use
  `QT_VERSION=5.15.2` (autobuild.yml lines 227, 237, 249). **Test code must
  therefore compile against both Qt 5.12-era API and Qt 6.x.**
- QtTest implications (Qt docs, https://doc.qt.io/qt-5/qtest.html and
  https://doc.qt.io/qt-6/qtest.html):
  - Safe in 5.12: `QT += testlib`, `QTEST_MAIN`/`QTEST_GUILESS_MAIN`, `QVERIFY`,
    `QCOMPARE`, `QTRY_COMPARE`, data-driven tests (`QTest::addColumn`/`newRow`),
    `QSignalSpy`, `QTest::qWait`, `QBENCHMARK`, `QVERIFY_EXCEPTION_THROWN`.
  - **Qt6-only APIs to avoid**: `QCOMPARE_EQ/NE/GT/GE/LT/LE` (6.4+),
    `QVERIFY_THROWS_EXCEPTION` / `QVERIFY_THROWS_NO_EXCEPTION` (6.3+; keep the
    5.x-era `QVERIFY_EXCEPTION_THROWN`), `QTest::failOnWarning` (6.3+),
    `QTest::throwOnFail/throwOnSkip` (6.8+).
  - General Qt-6 migration traps to avoid in test code: `qsizetype` assumptions,
    `Qt::endl`/`Qt::SkipEmptyParts` (5.14+ — fine for CI but violates the 5.12.2
    floor stated in CONTRIBUTING.md), `QStringView`-flavored overloads, and
    `QRandomGenerator` APIs added after 5.12.
- For a headless test binary, mirror the `headless`/`serveronly` qmake CONFIG
  pattern (`Jamulus.pro` lines 39–51, COMPILING.md "Compile time arguments" table):
  the server-only build needs only `qtcore qtnetwork qtconcurrent qtxml`
  (COMPILING.md line 58) — a testlib target that avoids widgets/multimedia keeps CI
  dependencies minimal.

## 3. Style rules as they apply to test code

- CONTRIBUTING.md lines 23–42: run `clang-format` **before committing**, using the
  version pinned in CI. `.github/workflows/coding-style-check.yml` line 37 pins
  **clangFormatVersion: 14** (DoozyX/clang-format-lint-action).
- The CI style check triggers on `**.cpp`, `**.h`, `**.mm`, `**.sh`, `**.py`
  **anywhere in the repo** (coding-style-check.yml `paths:` lines 7–20 and
  `extensions:` line 40) — so a new `tests/` directory would be clang-format-linted
  automatically, with exclusions only via `.clang-format-ignore` (currently just
  `./libs`).
- However, the local convenience target `make clang_format` only formats files under
  `android|ios|mac|linux|src|windows` (`Jamulus.pro` lines 1190–1197), and the
  comment there says: "When extending the list of file extensions **or when adding
  new code directories**, be sure to update `.github/workflows/coding-style-check.yml`
  and `.clang-format-ignore` as well." A new top-level `tests/` dir must update that
  regex; putting tests under `src/` avoids touching three tool configs
  (Jamulus.pro glob, update-copyright-notices.sh find list — see section 1 — and
  nothing else).
- Style itself: `.clang-format` (LLVM-based, IndentWidth 4, ColumnLimit 150,
  `SpaceBeforeParens: NonEmptyParentheses`, `SpacesInParentheses: true` — the
  distinctive `Foo ( bar )` Jamulus look). Braces on separate lines for all control
  bodies (CONTRIBUTING.md line 41).
- Shell scripts: shellcheck (`--shell=bash`) + `shfmt -d` over every `*.sh` outside
  `libs/` (coding-style-check.yml lines 42–53). Python: pylint, but the CI job only
  scans `./tools` (line 63).
- Qt string style: use `.arg()` substitution, never concatenation with `tr()`
  (CONTRIBUTING.md line 42).

## 4. Workflow conventions (how a new small workflow should look)

Current workflows (`.github/workflows/`): autobuild.yml (487 lines),
bump-dependencies.yml (188), coding-style-check.yml (63),
update-copyright-notices.yml (59), translation-check.yml (28),
check-json-rpcs-docs.yml (21). Conventions visible across them:

- **One small single-purpose workflow per concern** — `check-json-rpcs-docs.yml` is
  the best template for a test workflow: 21 lines, `permissions: {}`, triggered by
  `pull_request` against `main` with a narrow `paths:` filter, one ubuntu job,
  plain `run:` steps, non-zero exit fails the check.
- **Minimal permissions is a house rule**: top-level `permissions: {}` or
  `contents: read`, with job-level escalation only where needed
  (autobuild.yml line 60 `permissions: {}`; PRs #2953, #2962, #2963 exist solely to
  minimize workflow permissions).
- Third-party actions other than `actions/*` are **pinned by commit SHA**
  (coding-style-check.yml line 35; PR #2779 "CI: Pin Github action dependencies").
  Dependabot watches workflow deps (`.github/dependabot.yml`, PR #2778).
- Doc-only changes are excluded from expensive CI via `paths-ignore`
  (autobuild.yml lines 34–56; PR #2532 "Do not run CI on documentation changes").
- Developer escape hatch: branches named `autobuild/**` get full builds on push
  (autobuild.yml lines 29–30; CONTRIBUTING.md line 101). PR builds only cover main
  platforms unless the PR description contains the
  `AUTOBUILD: Please build all targets` tag (CONTRIBUTING.md line 84,
  pull_request_template.md line 37).
- Note for new contributors: "GitHub doesn't run these checks for new contributors
  automatically" — a maintainer must approve the first workflow runs
  (pull_request_template.md line 33).

## 5. Upstream precedent and reception

- **No unit tests exist today.** The only test artifact is `src/testbench.h`, a
  protocol fuzzer wired into `src/main.cpp` behind a commented-out line
  (`src/main.cpp` lines 61, 930). Per background discussion on issue #1413
  (comment by atsampson, 2021-04-01), it is a random-message fuzzer, considered
  unsuited to deterministic CI as-is, and outdated for newer message types.
- **Issue #1413** "Run tests as part of build/CI" (opened 2021-03-31 by hoffie):
  closed 2022-04-22 by ann0see as a duplicate in favor of
  **issue #2428 "Automatic Client/Server Smoke Test in CI"**, which is still
  **open** with labels `feature request`, `tooling`
  (https://github.com/jamulussoftware/jamulus/issues/2428). Its body (by hoffie,
  seconding pljones/ann0see from #2423) sketches an agreed direction: start a
  headless server + headless client, verify the client connects, exit; run at least
  on (pre-)releases; keep it simple, Linux first. This is the closest thing to a
  pre-agreed specification for a first test/CI PR.
- **Reception of CI PRs** (GitHub API, jamulussoftware/jamulus pulls):
  - Small focused workflow PRs merge smoothly: #2635 "CI: Run shellcheck & shfmt"
    (+27/−6, 1 file, 0 review comments, ~2 weeks to merge); #3797 "CI build Linux
    directly on Ubuntu 22" (1 file, −23, 0 review comments, merged within days,
    2026-07-18); #3625 "Enable checkkeys in CI" (1 file, 4 review comments).
  - Larger CI-plus-code PRs draw heavy review: #3052 "Add pylint to CI and fix all
    Python" (+178/−97, 10 files, **98 review comments**, ~3 weeks).
  - Conclusion: keep a first test PR small (one workflow + minimal test target),
    and split any source refactoring needed for testability into separate PRs.
- CONTRIBUTING.md general principles (lines 13–19) frame reviews: stability first,
  KISS, "Do one thing and do it well" — a test harness PR should visibly serve
  stability and avoid new dependencies (line 84: new dependencies must be
  "discussed and approved" and available on all supported platforms).

## 6. PR targeting: main (there is no `next`)

- All PR-triggered workflows run on `pull_request` against `main` only
  (autobuild.yml lines 44–46, coding-style-check.yml lines 13–14,
  check-json-rpcs-docs.yml lines 5–7).
- CONTRIBUTING.md does not mention any `next` branch; the upstream branch list
  (GitHub API `/branches`) contains `main`, `release/3_12`, archive/* and tooling
  branches — **no `next` branch exists**. (Recent commits like "Merge pull request
  #3811 from ann0see/fix/next" refer to a fork's branch named `fix/next`, not an
  upstream target branch.)
- Therefore: **target `main`.** `release/3_12` is the maintenance branch for the
  3.12.x fix releases; backports are tracked separately by maintainers and are not
  where a new test harness would land.

## 7. Checklist: a mergeable first test/CI PR

1. **Before coding**: comment on issue #2428 (or open a Discussion) proposing the
   concrete spec; wait for maintainer agreement (CONTRIBUTING.md lines 5–8).
2. **Scope small**: one narrow deliverable (e.g. headless client/server smoke test
   or a single QtTest binary for `protocol.cpp`), one workflow file of ~20–60 lines
   modeled on `check-json-rpcs-docs.yml`. No new dependencies beyond Qt testlib.
3. **New files**: AGPL 3.0-or-later header with copyright line, author(s), and
   "Licensed under AGPL 3.0 or any later version. See COPYING for details."
   Prefer placing C++ test files under `src/` (or update Jamulus.pro's
   CLANG_FORMAT_SOURCES regex, `.clang-format-ignore` guidance and
   `tools/update-copyright-notices.sh` find list for a new dir).
4. **Code constraints**: C++11 only; Qt API available in 5.12.2; must also compile
   under Qt 6.10 (CI macOS) — avoid Qt 6.3+/6.4+ QtTest macros; keep the test
   target buildable headless (core/network/concurrent/xml only).
5. **Style**: run clang-format 14 (`make clang_format` after qmake, or by hand);
   shellcheck + shfmt-clean if any `.sh` is added.
6. **Workflow hygiene**: `permissions: {}` (escalate per job only if needed),
   `pull_request` on `main` with tight `paths:` filters, SHA-pin any non-GitHub
   action, keep runtime short.
7. **PR mechanics**: fill the PR template; include a `CHANGELOG:` line (probably
   `CHANGELOG: SKIP` for pure CI) (CONTRIBUTING.md line 112, template line 7);
   reference the spec issue ("Fixes #…" or context); tick the checklist; add
   `AUTOBUILD: Please build all targets` if the .pro file changes; first-time
   contributors add themselves to `CAboutDlg` in `src/util.cpp`
   (CONTRIBUTING.md line 114).
8. **Verify before review**: push to a branch named `autobuild/<name>` on your fork
   to pre-run full builds (CONTRIBUTING.md line 101); confirm all checks green,
   remembering maintainers must approve first-time contributors' workflow runs.
9. **After opening**: expect two required approvals from main developers; respond
   promptly and keep the PR description updated (CONTRIBUTING.md lines 103–118).
