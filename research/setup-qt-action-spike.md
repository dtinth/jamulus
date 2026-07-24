# Spike: a `setup-qt` composite action for autobuild.yml + unit-tests.yml

Branch: `spike/setup-qt-action` (off `origin/unit-test-harness` @ `3b4612ae`).
This is exploratory, not a PR to merge as-is -- see "Recommendation" at the
end.

## The question

`unit-test-harness` (the current unit-test PR) shares its macOS/Windows Qt
setup with `autobuild.yml` by parsing `QT_VERSION` out of autobuild.yml's
text at runtime and hand-keeping cache keys byte-identical across two
workflow files. The owner wants to compare two ways to get rid of that
duplication via a composite action `.github/actions/setup-qt`:

1. **Follow-up shape**: land the current unit-test PR first, then a
   follow-up PR introduces the action and migrates both workflows.
2. **Sibling-PR-first shape**: a PR introduces the action and migrates
   `autobuild.yml` first; the unit-test PR (this branch) then consumes it.

Structurally the two shapes produce the same end state and the same total
diff -- the only difference is which commit/PR autobuild.yml's migration
rides in on. This spike builds the change as five commits so both framings
can be read directly off the branch's history:

| Commit | What it is | Models |
|---|---|---|
| `27732846` | add `.github/actions/setup-qt`, migrate autobuild.yml's macOS/Windows jobs | The sibling PR |
| `639d9657` | migrate unit-tests.yml to consume the action | Either shape's "consumption" side |
| `e36e8e01` | fix a Windows arg-passing bug found by the unit-tests CI run | spike bugfix (see Verification) |
| `fd49148e` | make PATH export opt-in, fixing a real break in autobuild.yml's Windows entries | spike bugfix (see Verification) -- this is the one a real sibling PR would need from day one |

The last two commits aren't part of either shape's "real" diff in the sense
of being deliberate design choices -- they're fixes for bugs this spike's
own CI runs caught. They're included in the branch history (rather than
squashed away) specifically so the diff-stat numbers below, and the
Verification section, honestly reflect what got exercised, not a
retroactively-cleaned-up version of events.

## Diff stats for the two PR shapes

**Shape 2, sibling PR** (`3b4612ae..27732846`, i.e. before the two bugfixes):
action + autobuild.yml migration, two files besides the new action:

```
.github/actions/setup-qt/action.yml | 109 ++++++++++++++++++++++++++++++++++++ (new file)
.github/autobuild/windows.ps1       |   8 +++
.github/workflows/autobuild.yml     |  55 ++++++++++++------
3 files changed, 154 insertions(+), 18 deletions(-)
```

**Consumption side, either shape** (`27732846..HEAD`, unit-tests.yml only,
action already exists -- includes both bugfixes' one-line follow-on change
to unit-tests.yml, the `add-to-path: 'true'` opt-in):

```
.github/workflows/unit-tests.yml | 86 +++++++---------------------------------
1 file changed, 14 insertions(+), 72 deletions(-)
```

That -58 net lines out of unit-tests.yml is the "Determine Qt version from
autobuild.yml" grep/sed step plus the four cache/install/PATH steps it fed,
replaced by one `uses: ./.github/actions/setup-qt` call (see
`.github/workflows/unit-tests.yml` around the `Setup Qt (macOS/Windows)`
step).

**Total, either shape, same end state** (`3b4612ae..HEAD`, i.e. including
both bugfixes -- this is the number that actually matters, since a real
sibling PR needs the `add-to-path` fix from the start):

```
.github/actions/setup-qt/action.yml | 133 ++++++++++++++++++++++++++++++++++
.github/autobuild/windows.ps1       |   8 +++
.github/workflows/autobuild.yml     |  61 +++++++++++------
.github/workflows/unit-tests.yml    |  86 ++++----------------------
4 files changed, 197 insertions(+), 91 deletions(-)
```

## The single-source-of-truth endgame

`.github/actions/setup-qt/action.yml`'s `qt-version` input default
(`6.10.2`) is the one place a caller that doesn't care about a specific Qt
line points to. Concretely, after this refactor:

- **autobuild.yml's macOS main entry** and **unit-tests.yml** (all
  platforms) consume the default -- they don't mention a Qt version at all
  anymore.
- **autobuild.yml's macOS Legacy entry** (Qt5) and **iOS entry** still need
  an explicit override, because the whole point of that part of the matrix
  is to build against a *different* Qt version in the same job. No amount
  of "single source of truth" removes that -- a build matrix that legitimately
  builds two Qt lines has to name both somewhere. iOS wasn't touched by this
  spike (out of scope; still carries its own inline `QT_VERSION=5.15.2`).
- **windows.ps1** keeps a fallback literal (`$Qt64Version = "6.10.2"`) for
  the case where it's invoked standalone/manually, bypassing the action, but
  `if ($Env:QT_VERSION) { $Qt64Version = $Env:QT_VERSION }` means the
  action's export wins whenever it's used through the action (see "Two
  places, not zero" below for why this fallback couldn't just be deleted).

### Why autobuild.yml's macOS entry needed *two* steps, not one

The obvious design -- one `Setup Qt` step per platform, with
`qt-version: ${{ matrix.config.qt_version }}` always passed -- doesn't
actually achieve "action's default is the source of truth" for the main
entry. GitHub Actions composite-action inputs only fall back to their
`default:` when the caller's `with:` block omits the key entirely; passing
an *empty string* is a real value, not "unset", so `qt-version: ${{
matrix.config.qt_version }}` evaluating to `''` for the main entry (which
doesn't set `qt_version`) would pass Qt an empty version string, not
`6.10.2`. There is no way to conditionally omit a `with:` key from a single
step based on matrix data.

The spike resolves this with two mutually-exclusive steps for macOS
(`if: ... && !matrix.config.qt_version` / `if: ... && matrix.config.qt_version`)
-- only the Legacy entry's step passes `qt-version`, and the main entry's
step doesn't mention it at all, genuinely deferring to the action's
default. Windows doesn't need this split: both Windows entries use the
default Qt version, and only `windows-build-option` (which *is* safe to
pass as an always-present, possibly-empty string, since the action's own
default for that input is also `''`) varies between them.

This is worth calling out as a real cost of the "single default, no
literal repeated" goal: it's two extra conditional steps in autobuild.yml,
not zero. An alternative that accepts a small, harmless duplication (always
pass `qt-version: ${{ matrix.config.qt_version || '6.10.2' }}`, i.e. keep
the literal in the workflow too) would be simpler at the cost of the exact
thing this spike is meant to remove. Worth a second opinion before the real
PR: is the two-step split worth it, or is repeating `6.10.2` once in
autobuild.yml's matrix (in exchange for one Setup Qt step instead of two)
the more maintainable trade?

### Two places, not zero: `windows.ps1`'s hardcode

`windows.ps1` always installs *two* Qt versions (`Qt32Version` = 5.15.2,
`Qt64Version` = 6.10.2), unconditionally, regardless of which
`build_command` invoked it -- unlike mac.sh, which installs whatever single
`QT_VERSION` it's given. Only `Qt64Version` corresponds to "the Qt version"
that autobuild.yml's matrix and unit-tests.yml care about; `Qt32Version` is
windows.ps1-internal and isn't modeled by the action's `qt-version` input at
all (out of scope for this spike, flagged here as a gap: if Qt32Version ever
needs to move, it isn't reachable through the action).

The one-line(ish) change implemented in this spike:

```powershell
$Qt64Version = "6.10.2"
if ($Env:QT_VERSION) { $Qt64Version = $Env:QT_VERSION }
```

makes `Qt64Version` honor the action's `QT_VERSION` export when present,
while keeping a literal fallback for anyone invoking windows.ps1 directly
(local repro, or a future caller that doesn't go through the action). This
was deliberately *not* written as a single-line ternary
(`$(if (...) {...} else {...})`) -- see "bump-dependencies.yml" below for
why keeping `$Qt64Version = "6.10.2"` as its own matchable line matters.

### The action must export `QT_VERSION`, not just consume it

The action exports `QT_VERSION` via `GITHUB_ENV` as its first step. This
matters for two things beyond the install step itself:

- autobuild.yml's macOS `base_command` no longer needs `QT_VERSION=6.10.2 `
  / `QT_VERSION=5.15.2 ` prefixed -- the later `build`/`get-artifacts`/
  `notarize`/`staple` steps that still shell out to `mac.sh` pick up
  `QT_VERSION` from the job environment instead. This is what let the
  literal disappear from `autobuild.yml`'s matrix entirely for the main
  entry (see stats above).
- windows.ps1 sees `$Env:QT_VERSION` set whenever it's invoked through the
  action (see above).

## Cache-key compatibility: does the shared cache survive, or repopulate?

**It repopulates once**, for both platforms, the moment autobuild.yml first
adopts the action. This was a deliberate choice, not an oversight:

- **Old key** (both workflows, hand-kept identical):
  `${platform}-${hashFiles(autobuild.yml, script, deploy_script)}-${base_command text}`
  -- included the *whole* `base_command` string (e.g.
  `QT_VERSION=6.10.2 SIGN_IF_POSSIBLE=1 TARGET_ARCHS="x86_64 arm64" ...`),
  meaning a change to `TARGET_ARCHS` or `SIGN_IF_POSSIBLE` -- which affect
  signing/build, not the Qt download -- busted the Qt cache for no reason.
  It also hashed `autobuild.yml` itself, so *any* unrelated edit to that
  600-line file (e.g. touching the Android matrix) invalidated the macOS/
  Windows Qt caches too.
- **New key** (owned once, inside the action):
  `${platform}-qt-${hashFiles(action.yml, script, deploy_script)}-${qt-version}[-${windows-build-option}]`
  -- hashes only the action's own file + the scripts it calls, and keys
  purely on the Qt version (+ build option, for Windows) actually consumed.
  A change to `TARGET_ARCHS` no longer busts the cache; a change to
  `autobuild.yml` unrelated to Qt no longer busts it either.

Because both the file being hashed (`autobuild.yml` -> `action.yml`) and the
text embedded in the key (`base_command` -> `qt-version` alone) change, the
computed key changes unconditionally at the moment of adoption -- there is
no way to preserve the literal string across this refactor. Concretely, for
the runs in this spike:

- Old Windows key would have been:
  `windows-<hash of old autobuild.yml>-powershell .\.github\autobuild\windows.ps1 -Stage`
- New Windows key observed in CI (final action.yml, after both bugfixes):
  `windows-qt-37cb3fc0fafed2158e2eed30ebaf89709bb751ce7d229fd6b6be421cb528af54-6.10.2-`
  (trailing `-` is the empty `windows-build-option` for the non-JACK entry;
  note this hash changed again each time `action.yml` itself changed --
  see the note at the end of this section)

First run under the new key is necessarily a cache **miss** (see
Verification below -- confirmed: `Cache not found for input keys:
windows-qt-...-6.10.2-`). After that first population, autobuild.yml and
unit-tests.yml share the *same* new key (both call the same action with the
same effective inputs for their "default" entries), so the sharing property
this refactor is about is preserved going forward -- it's a one-time
repopulation, not a permanent fork into two caches.

This spike's own history demonstrates the flip side of "own the key inside
the action": the two bugfixes (`e36e8e01`, `fd49148e`) both edited
`action.yml` itself, so the cache key's hash segment changed on *every*
commit that touched the action, for *both* platforms -- three different
key hashes show up across this document's log excerpts for that reason,
even though one of those two fixes was Windows-only. This isn't a new
coupling introduced by the refactor, though: the old design already hashed
the *same* `autobuild.yml` file for both platforms' keys, so a change
anywhere in that 600-line file already busted both caches together. The
action just makes that coupling smaller in scope (one ~130-line file
instead of the whole workflow) rather than removing it.

One more repopulation-adjacent fact worth flagging: because the old key for
*unit-tests.yml* hashed `autobuild.yml`'s text too, landing **just the
sibling PR** (autobuild.yml migrates, unit-tests.yml doesn't yet) changes
`hashFiles('.github/workflows/autobuild.yml', ...)`'s output and therefore
busts *unit-tests.yml's old-style cache* as an incidental side effect,
before unit-tests.yml has even adopted the action. This is a pre-existing
fragility of "hash the whole calling workflow file" that the action-owned
key design (hash only the action + scripts) eliminates going forward, but it
does mean: landing the sibling PR alone causes one extra unplanned
unit-tests.yml cache miss on top of the one caused by unit-tests.yml's own
eventual migration. Two repopulations instead of one, if the shapes land
as separate PRs with a gap between them.

## Risks and open questions

1. **`bump-dependencies.yml`'s Qt6 auto-bump job doesn't see
   `.github/actions/setup-qt/action.yml` at all**, and even if it did, its
   regex wouldn't match a YAML `default: '6.10.2'` line. Verified directly:

   - The job's `files` glob is `.github/{autobuild,workflows}/* windows/*.ps1
     mac/*.sh` -- brace-expands to direct children of `.github/autobuild/`
     and `.github/workflows/`, never `.github/actions/**`.
   - Its `local_version_regex` for Qt6 is
     `(.*QT[0-9_]+VERSION\s*=\s*"?)(6\.[0-9.]+)("?.*)`, which requires a
     literal `=` (shell/PowerShell assignment syntax); a YAML `key: value`
     line can't match it regardless of file glob. Confirmed by running the
     regex against both forms:
     `$Qt64Version = "6.10.2"` -> matches, captures `6.10.2`.
     `default: '6.10.2'` -> no match.

   Net effect: **after this refactor, the only Qt6-line the automated bump
   job can find and rewrite is windows.ps1's fallback** (kept deliberately
   matchable, see above) -- `action.yml`'s default, which is the version
   every non-overriding caller (unit-tests.yml, autobuild.yml's macOS main
   entry) actually uses, goes stale on the next auto-bump unless
   `bump-dependencies.yml` is updated in the same change. This is **the
   single biggest risk of this refactor** and should be treated as a
   required companion change, not a follow-up: either (a) extend the Qt6
   job's `files` list to include `.github/actions/setup-qt/action.yml` and
   loosen its regex to also accept `key:` (e.g.
   `(.*QT[0-9_-]*VERSION\s*[:=]\s*"?)(6\.[0-9.]+)("?.*)`), or (b) keep
   windows.ps1's fallback as the bump target and have the action's default
   point at it somehow (not really possible -- an action.yml default can't
   read another file at "compose" time) -- (a) is the only real fix.

2. **The macOS default/override two-step split** (see above) is a genuine
   design cost of the "true single source of truth" goal; worth a second
   opinion on whether it's worth it versus one step + a small literal
   duplication.

3. **Xcode pinning stays outside the action deliberately.** autobuild.yml's
   iOS entry needs a different Xcode version (26.2) than the macOS entries
   (16.3.0) in the *same* job template, so "the Xcode version this job
   needs" isn't a fact the action can own without either becoming
   iOS-aware (out of its stated scope: it's a Qt action) or gaining an
   `xcode-version` passthrough input that duplicates what
   `matrix.config.xcode_version` already expresses perfectly well next to
   the existing `Select Xcode version` step. Both workflows keep their own
   `Select Xcode version` step unchanged.

4. **`windows.ps1`'s setup stage is broader than "Qt".** It also installs
   `jom` and, conditionally, JACK. The action inherits and wraps that whole
   stage rather than narrowing it -- consistent with what both workflows
   already relied on (they call `windows.ps1 setup`, not "install just
   Qt"), but worth naming so "setup-qt" doesn't overpromise on scope.

5. **The Windows JACK entry needed its own input** (`windows-build-option`)
   threaded through to keep `Ensure-JACK` (part of windows.ps1's setup
   stage) running for that entry, and to keep its cache key distinct from
   the plain Windows entry's (they install different toolsets). This is a
   real (if narrow) case of the action's surface not being purely about Qt
   -- it's really "the Windows build-dependency setup stage", Qt included.

6. **A bug this spike found and fixed** (commit `e36e8e01`): initially the
   Windows install step always passed `-BuildOption '<value-or-empty>'`;
   the nested `powershell.exe` invocation silently drops a trailing
   empty-string argument, which surfaced as `Missing an argument for
   parameter 'BuildOption'` -- caught by the unit-tests.yml CI run (see
   Verification). Fixed by only including `-BuildOption` in the splatted
   argument list when non-empty. Flagging because it's the kind of subtle
   nested-shell quoting bug that's easy to reintroduce if this pattern gets
   copied elsewhere.

7. **A second, more consequential bug this spike found and fixed** (commit
   `fd49148e`): the action's first draft always added the installed Qt's
   `bin/` to `PATH` unconditionally, for every caller. Caught by the
   *push-triggered* autobuild.yml run (branch `autobuild/spike-setup-qt`,
   which actually builds real artifacts, unlike the workflow_dispatch runs
   used for faster iteration): **both** `Windows (artifact+codeQL)` and
   `Windows JACK (artifact)` failed with
   `Unable to find dependent libraries of
   C:\Qt\6.10.2\msvc2022_64\bin\Qt5Widgets.dll` from `windeployqt`. Root
   cause: `windows/deploy_windows.ps1`'s `Build-App-Variants` always builds
   *both* the `x86` and `x86_64` variants for every Windows entry (`foreach
   ($_ in ("x86_64", "x86"))`), so `windeployqt` from *both* the 32-bit
   Qt5.15.2 and 64-bit Qt6.10.2 installs runs on every build -- and neither
   `mac.sh`'s nor `deploy_windows.ps1`'s build steps ever relied on `PATH`
   for Qt in the first place (both resolve Qt via explicit absolute paths
   internally). Only `unit-tests.yml`'s `run-unit-tests.py`, which invokes
   `qmake` bare, needs Qt on `PATH`. Forcing it on for autobuild.yml too
   put the wrong-version `bin/` on `PATH` and broke `windeployqt`'s
   dependent-library resolution for every Windows entry. Fixed by making
   PATH export an opt-in `add-to-path` input (default `'false'`);
   unit-tests.yml requests it, autobuild.yml doesn't. This is the
   headline argument for the "commit as a sibling PR, verify against a
   real (non-workflow_dispatch) autobuild trigger" approach recommended
   below -- the faster workflow_dispatch iteration loop used throughout this
   spike did *not* catch this; only the genuine `autobuild/*`-branch push
   build did, because only that one actually exercises
   `deploy_windows.ps1`'s real x86+x86_64 build-and-package path end to end.

## Verification

All runs below are on `dtinth/jamulus` (fork), branch `spike/setup-qt-action`
unless noted. `claw exec --repo dtinth/jamulus -- gh ...` was used throughout.

### unit-tests.yml (consumption side)

- **First run after adopting the action** (workflow_dispatch):
  https://github.com/dtinth/jamulus/actions/runs/30086329224 -- caught a
  real bug: the Windows job failed with `Missing an argument for parameter
  'BuildOption'` because the initial action always passed
  `-BuildOption '<value>'` even when empty, and the nested `powershell.exe`
  call drops a trailing empty-string argument. Fixed in commit `e36e8e01`
  (only include `-BuildOption` in the splatted args when non-empty).
- **Re-run after the fix**: https://github.com/dtinth/jamulus/actions/runs/30086795361
  -- all 6 jobs green (`macOS (Qt 6)`, `Linux (Qt 6, ubuntu-24.04)`,
  `Linux (Qt 6, ASan/UBSan)`, `Linux (Qt 6, coverage)`,
  `Windows (Qt 6, MSVC)`, `Linux (Qt 5, ubuntu-22.04)`). This is the first
  population of the new action-owned cache key, confirmed by the logs:
  ```
  # macOS job (89460745004):
  key: macos-qt-56a1655403b9dcf97c0d22c9c740536f0eede6e163c952d870e63caf5f6f315d-6.10.2
  Cache not found for input keys: macos-qt-...-6.10.2
  ...
  Cache saved with key: macos-qt-...-6.10.2

  # Windows job (89460745084):
  key: windows-qt-00d550182de2eb80fc016b60f73d085c2ed3605e6ab28337f42357cd81df4e70-6.10.2-
  Cache not found for input keys: windows-qt-...-6.10.2-
  ...
  Cache saved with key: windows-qt-...-6.10.2-
  ```
- **Second run, to confirm a genuine cache HIT under the new key**:
  https://github.com/dtinth/jamulus/actions/runs/30087007955 -- all 6 jobs
  green again, and this time both platforms hit:
  ```
  # macOS job (89461427951):
  Cache hit for: macos-qt-56a1655403b9dcf97c0d22c9c740536f0eede6e163c952d870e63caf5f6f315d-6.10.2
  Cache restored successfully
  ...
  Cache hit occurred on the primary key macos-qt-...-6.10.2, not saving cache.

  # Windows job (89461427921):
  Cache hit for: windows-qt-00d550182de2eb80fc016b60f73d085c2ed3605e6ab28337f42357cd81df4e70-6.10.2-
  Cache restored successfully
  ...
  Cache hit occurred on the primary key windows-qt-...-6.10.2-, not saving cache.
  ```

### autobuild.yml (sibling-PR side)

Three autobuild.yml runs matter here, in order:

1. **workflow_dispatch, pre-`add-to-path`-fix** (main build targets only):
   https://github.com/dtinth/jamulus/actions/runs/30086807384 (commit
   `e36e8e01`) -- `MacOS (artifacts)` and `MacOS Legacy (artifacts+CodeQL)`
   succeeded; `Windows (artifact+codeQL)` **failed**:
   `Unable to find dependent libraries of
   C:\Qt\6.10.2\msvc2022_64\bin\Qt5Widgets.dll` from `windeployqt` -- the
   PATH bug described above (risk #7), caught here for the first time.
   `MacOS (artifacts)`'s cache step ran *concurrently* with unit-tests.yml's
   first run (both started ~10:37), raced for the same key, and lost the
   save gracefully:
   ```
   key: macos-qt-56a1655403b9dcf97c0d22c9c740536f0eede6e163c952d870e63caf5f6f315d-6.10.2
   Cache not found for input keys: macos-qt-...-6.10.2
   ...
   Failed to save: Unable to reserve cache with key macos-qt-...-6.10.2, another job may be creating this cache.
   ```
   This is itself useful evidence: autobuild.yml and unit-tests.yml computed
   the **exact same key string** independently (each calls the same
   action), which is the property the refactor is meant to guarantee. The
   "run setup once, not twice" change also confirmed: `Set up build
   dependencies for Windows (artifact+codeQL)` shows `skipped` in every
   run, not re-running mac.sh/windows.ps1's setup stage after the action
   already did.

2. **push-triggered, still pre-fix** (branch `autobuild/spike-setup-qt`,
   commit `e36e8e01`): https://github.com/dtinth/jamulus/actions/runs/30087052637
   -- a genuine `event: push` run (confirmed via
   `gh api repos/dtinth/jamulus/actions/workflows/12449886/runs`), building
   **all** targets per the `autobuild**` branch-name convention. Both
   `Windows (artifact+codeQL)` **and** `Windows JACK (artifact)` failed with
   the same `windeployqt`/`Qt5Widgets.dll` error -- confirming the bug hits
   every Windows entry (`deploy_windows.ps1` always builds both x86/x86_64
   variants, so the wrong-version `bin/` on `PATH` breaks all of them, not
   just the JACK one).

3. **push-triggered, with the `add-to-path` fix** (same branch, force-pushed,
   commit `fd49148e`): https://github.com/dtinth/jamulus/actions/runs/30087696720
   -- **every** target built (`build_all_targets` via the `autobuild**`
   push convention): Android, both Linux .deb entries (amd64 + arm64 +
   armhf), iOS, both macOS entries, and both Windows entries. In
   particular:
   - `Windows JACK (artifact)` succeeded, producing
     `jamulus_3.12.2dev-fd49148_win_jack.exe`, with its own distinct cache
     key (`windows-build-option` folded in):
     ```
     key: windows-qt-37cb3fc0...-6.10.2-jackonwindows
     Cache not found for input keys: windows-qt-37cb3fc0...-6.10.2-jackonwindows
     ...
     Setting Github step output name=artifact_1 to jamulus_3.12.2dev-fd49148_win_jack.exe
     ...
     Cache saved with key: windows-qt-37cb3fc0...-6.10.2-jackonwindows
     ```
   - `Windows (artifact+codeQL)` -- the exact entry that failed pre-fix --
     succeeded end to end: build, artifact upload, **and** the CodeQL
     analysis this entry runs (unlike JACK):
     ```
     key: windows-qt-37cb3fc0...-6.10.2-
     Cache not found for input keys: windows-qt-37cb3fc0...-6.10.2-
     ...
     Setting Github step output name=artifact_1 to jamulus_3.12.2dev-fd49148_win.exe
     Artifact jamulus_3.12.2dev-fd49148_win.exe has been successfully uploaded! Final size is 71441729 bytes.
     ...
     Successfully uploaded results
     CodeQL job status was success.
     ...
     Cache saved with key: windows-qt-37cb3fc0...-6.10.2-
     ```
     This is the headline confirmation: the same failure mode from run #2
     above is gone, on the exact same entry, with only the `add-to-path`
     fix changed.
   - `MacOS Legacy (artifacts+CodeQL)` also succeeded, using the
     `qt-version` override (5.15.2) end to end -- confirming the "two-step"
     macOS default/override split (risk #2) actually works for a real
     CodeQL build, not just at the setup-step level:
     ```
     qt-version: 5.15.2
     key: macos-qt-56a1655403b9dcf97c0d22c9c740536f0eede6e163c952d870e63caf5f6f315d-5.15.2
     ```
     (Note this key differs from the main entry's `...6.10.2` key only by
     the trailing version token -- exactly the "one formula, two cache
     slots" property the action is meant to provide.)

   `Android .apk (artifact+codeQL)` also finished successfully (irrelevant
   to this spike's verification either way: `target_os == 'android'` never
   touches any `Setup Qt` step, all skipped) -- with every other job above,
   this run ended **10/10 jobs green**, the full autobuild matrix, on the
   fully-fixed commit.

   A subsequent run against the final commit (`efee48ca`, docs-only) was
   also verified for unit-tests.yml: https://github.com/dtinth/jamulus/actions/runs/30089613789
   -- all 6 jobs green, confirming the doc-only tweak to `action.yml`'s
   `add-to-path` description didn't disturb anything (it does bust the
   cache once more, per the "Cache-key compatibility" section above, since
   any edit to `action.yml` changes the hash -- expected, not a regression).

Note on the push trigger itself: the **first** push of a brand-new branch
named `autobuild/spike-setup-qt` (and, separately, a no-slash
`autobuild-spike-setup-qt` test branch) did **not** trigger `autobuild.yml`
at all -- confirmed by polling `actions/runs` for several minutes with
nothing appearing. A subsequent force-push updating the same branch ref
**did** trigger it immediately, every time. Root cause wasn't tracked down
(branch-name-pattern matching itself is not the issue -- both slash and
no-slash variants failed to trigger on first push); the working theory is a
first-push/webhook-registration quirk for this fork+token combination, not
an `autobuild**` pattern problem. Anyone relying on this convention for the
real PR should push once, then verify the run appears before assuming a
branch's absence of activity means "no build changes detected" -- it may
just mean the first push didn't register.

## Recommendation

**Land shape 2 (sibling PR first), not shape 1 (follow-up after merge) --
and only with the `add-to-path` fix included from the start.** Reasoning:

- The two shapes are structurally identical in the end (same 4-file diff,
  same action). The only real decision is *sequencing*, and sequencing
  matters here for one concrete reason this spike surfaced directly: **the
  composite action cannot be verified safely without exercising a real
  autobuild.yml build**, and the fast iteration loop (workflow_dispatch,
  main-targets-only) that's cheap to run against a solo unit-test PR did
  *not* catch the PATH-export bug -- only a genuine `autobuild/*`-branch
  push, which builds and packages every target for real, did. A follow-up
  PR that lands after the unit-test PR, written and CI-verified the same
  way this spike started (workflow_dispatch loop, no full autobuild push),
  would have shipped that bug to `main`'s next real autobuild run. Doing
  the autobuild.yml migration as its own PR forces exactly the right kind
  of verification (a real `autobuild/*` push) *before* unit-tests.yml ever
  touches the action, de-risking the eventual unit-test-side change too.
- The consumption-side diff (`unit-tests.yml`, -58 net lines) is small and
  mechanical either way; there's no meaningful review-burden argument for
  bundling it with the action's introduction. Splitting lets each PR carry
  a tight, reviewable story: "extract the action + prove autobuild.yml
  still builds everything" vs. "unit-tests.yml adopts the thing that
  already works."

**Before opening the real sibling PR**, resolve these (in priority order):

1. Update `bump-dependencies.yml`'s Qt6 job (`files` glob + regex) so the
   auto-bump automation can still find and rewrite `action.yml`'s
   `qt-version` default -- otherwise it silently goes stale on the next Qt6
   release (see risk #1). This is the single most important companion
   change; without it, the "single source of truth" this refactor creates
   quietly stops being kept up to date by the existing automation.
2. Get a second opinion on the macOS default/override two-step split
   (risk/open-question #2) -- it's a real, if small, verbosity cost, and a
   maintainer may prefer the simpler one-step-plus-duplicated-literal
   alternative.
3. Decide whether `Qt32Version` (windows.ps1-internal, not modeled by the
   action) is worth exposing too, or is fine left as a known gap (risk #4).

**Confidence in the design otherwise**: high. The cache-key ownership move
(action-owned, hashing only the action + scripts + qt-version, not the
whole calling workflow file) is a genuine improvement over hand-kept-
identical keys, verified end to end (miss -> save -> hit, and a real
same-key race between the two workflows). The `windows-build-option`
threading and the macOS override split both proved correct against real
CodeQL-instrumented builds, not just the setup step. The two bugs this
spike caught (empty `-BuildOption` arg, unconditional PATH export) are
exactly the class of thing a spike is for: cheap to fix once found, easy to
miss without actually running the real build both workflows depend on.
