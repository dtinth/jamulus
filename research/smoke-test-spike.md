# Spike: headless client/server smoke test in CI (jamulussoftware/jamulus#2428)

Status: prototype spike, now wired into CI as `.github/workflows/smoke-test.yml`
on this branch (see that file's own NOTE comment about reshaping triggers
before upstreaming). Answers the questions from dtinth/jamulus#9.

**Update:** the original spike shipped the checker as two files,
`tools/smoke-test.sh` (bash orchestration) + `tools/smoke-test-jrpc.py`
(JSON-RPC helper), so it could run on Linux only. Per the CI follow-up
task, these were merged into a single cross-platform `tools/smoke-test.py`
(pure standard library: `subprocess`/`socket`/`json`/`argparse`/`tempfile`
-- no pip install needed in CI) so the same script drives the smoke test
on Linux, macOS and Windows, including Windows-appropriate process-tree
teardown via `taskkill /T /F` instead of POSIX signals. Verification
semantics are unchanged: client `connected:true` AND server
`connections>=1`, both polled over JSON-RPC. A `--skip-jackd` flag was
added so the same script also drives builds that don't need an external
JACK daemon (e.g. a native CoreAudio/ASIO client). See the "CI follow-up"
section near the end of this document for per-platform results.

## TL;DR

Yes — a real headless Jamulus client and server can connect to each other
in a container with no sound hardware and no X display, using JACK's
`dummy` audio driver as a stand-in sound device. The whole connect +
verify cycle is well under two seconds and was 5/5 stable across repeated
runs in this environment. "Connection verified" is implemented as: the
client's JSON-RPC `jamulusclient/getClientInfo` reports `connected: true`
**and** the server's JSON-RPC `jamulusserver/getClients` reports
`connections >= 1`.

## Environment

- Debian 13 (trixie), arm64, no sound hardware, no X display.
- Qt 6.8.2 (`qmake6`), already present.
- Packages installed for this spike: `jackd2` (pulls in `jackd`,
  `jack-example-tools` which ships `jack_dummy.so`), `libjack-jackd2-dev`
  (headers, for `PKGCONFIG += jack` at build time), `pkg-config`,
  `qt6-multimedia-dev`, and `qt6-l10n-tools` (provides `lrelease`, needed
  by the `embed_translations` qmake feature even for a headless build).

## Q1: Can the full Jamulus binary build headless on this box?

Yes, cleanly, with vendored Opus included.

```sh
qmake6 Jamulus.pro CONFIG+=headless
make -j8
```

Notes on `Jamulus.pro`:
- `CONFIG+=headless` alone (no `serveronly`) builds **both** client and
  server code paths, just with `QT -= gui` (`DEFINES += HEADLESS`). This
  is what #2428 wants — it's the same binary/behavior a real headless
  client deployment would use, not a stub.
- On Linux, unless `CONFIG+=serveronly` is also given, the `.pro` file
  unconditionally adds `PKGCONFIG += jack` and compiles in
  `src/sound/jack/sound.cpp` (`DEFINES += WITH_JACK`). So a Linux client
  build — headless or not — needs the JACK client library
  (`libjack-jackd2-dev`) at build time, confirming the spike's premise.
- One build wrinkle unrelated to headless/JACK: `CONFIG += embed_translations`
  always runs `lrelease` regardless of `headless`, so `qt6-l10n-tools`
  (or whichever package ships `lrelease` for the Qt6 toolchain) is a
  build dependency even for a "no GUI" build. Without it, the first
  `make` attempt fails late (~20s in) with:
  ```
  make[1]: /usr/lib/qt6/bin/lrelease: No such file or directory
  make[1]: *** [Makefile.Release:1166: release/translation_nb_NO.qm] Error 127
  ```
- Build time on this 8-core arm64 box: ~21s wall-clock for a from-scratch
  build (opus + Qt sources + moc/rcc/lrelease), verified with `time make -j8`.
  This included compiling all of vendored Opus. Real CI runners (often
  2-4 vCPU, x86_64) should expect this to take longer, plus qmake/apt
  setup time on top.
- Resulting binary links `libjack.so.0` as expected (`ldd` confirms) and
  is a normal dynamically-linked ELF executable — nothing exotic.

## Q2: Headless server

```sh
./Jamulus -s -n -l server.log --jsonrpcport 22222 --jsonrpcsecretfile secret.txt --serverinfo "Name;;"
```

- `-s`/`--server` selects server mode, `-n`/`--nogui` is the flag that
  actually means "headless" at runtime (`--help` even parenthesizes it as
  `("headless")`); `CONFIG+=headless` only controls what gets *compiled*.
- `-l/--log <file>` enables a human-readable connect/disconnect log. It
  only writes on state changes (empty until the first client connects),
  format: `2026-07-22 21:42:06, 127.0.0.1, connected (1)`. Usable as a
  secondary/backup signal, but not chosen as primary (see Q4).
- `--jsonrpcport` / `--jsonrpcsecretfile` / `--jsonrpcbindip` enable the
  JSON-RPC control surface (`docs/JSON-RPC.md`), binds to 127.0.0.1 by
  default.
- `--directoryaddress`/`--directoryfile` exist for registering with or
  running a directory server; not needed for a simple client-connects-
  directly-to-server smoke test, so unused here (matches "Linux first,
  keep it simple").
- Server started, bound its ports, and logged its banner within well
  under a second in every run observed.

## Q3: Headless client — the JACK question

**JACK's `dummy` backend works and is the cheapest option**, exactly as
hypothesized. No workaround or plan B was needed.

```sh
jackd -d dummy -r 48000 -p 512 &
./Jamulus -n -c 127.0.0.1 --jsonrpcport 22223 --jsonrpcsecretfile secret.txt --clientname foo
```

- `libjack-jackd2-dev`'s runtime counterpart `jackd2` package installs
  `jackd` itself plus `jack-example-tools`, which provides
  `/usr/lib/aarch64-linux-gnu/jack/jack_dummy.so` — the dummy driver.
- `jackd -d dummy` starts fine in this container despite it being
  unprivileged: it logs two harmless warnings (`Cannot lock down ... byte
  memory area (Cannot allocate memory)`, `Cannot use real-time scheduling
  ... (Operation not permitted)`) and then is fully usable — `jack_lsp`
  lists `system:capture_1/2`, `system:playback_1/2` normally. These
  warnings are expected in any container without `CAP_SYS_NICE`/`mlock`
  rlimits raised (i.e. essentially all CI runners) and are not fatal.
- The Jamulus client connects to JACK as `"default"` with no extra flags
  needed and logs the same two harmless RT warnings, then proceeds
  normally.
- No fallback (built-in nosound option, or a scripted protocol-level
  client) was needed — option (a) from the spike brief worked
  out-of-the-box. There is no built-in "nosound"/dummy client mode in the
  Jamulus client itself; `CONFIG+=nosound` at build time is actually
  deprecated shorthand for `serveronly` (server, no client at all), not a
  dummy audio client — so JACK-dummy is the way to get a *runnable
  client* rather than just a server.

## Q4: What "connection verified" concretely means, and how it's checked

Chosen signal: **both sides' JSON-RPC state agree the connection is up**,
polled until true or a timeout:

- `jamulusclient/getClientInfo` → `result.connected == true`
- `jamulusserver/getClients` → `result.connections >= 1`

Why this over the alternatives listed in the brief:
- It's exactly the state Jamulus itself computes and already exposes
  through a documented, versioned API (`docs/JSON-RPC.md`) — not an
  inference from log text or packet counts. Concretely, once connected,
  the server also confirms channel details (`clients[0].id`, `.address`,
  `.channels`, etc.), so this signal is rich enough to assert more later
  if wanted (e.g. exact address, channel count) without changing the
  detection mechanism.
- Checking *both* sides (not just one) is a cheap way to rule out a
  half-open/one-sided false positive (e.g. client claims connected but
  server hasn't registered the channel yet, or vice versa).
- The server's `--log` file is a reasonable fallback/secondary signal
  (`grep "connected (" server.log`) — simpler, no RPC/Python dependency —
  but was not made primary because it's a free-text log line versus a
  documented structured API field, and matching on log wording is more
  brittle to future log-message changes.
- Raw UDP traffic volume (weakest option in the brief) was not
  implemented; JSON-RPC gave an unambiguous, already-available signal, so
  there was no need to fall back to it.

The JSON-RPC polling is implemented via a small helper,
`tools/smoke-test-jrpc.py`, since Jamulus's JSON-RPC transport is
newline-delimited JSON over a plain TCP socket that must first be
authenticated with `jamulus/apiAuth` — that's a few more steps than `nc`
one-liners handle comfortably across repeated polls, and Python's stdlib
`socket`+`json` cover it with no extra dependencies (Debian ships
`python3` by default; no `pip install` needed).

## Q5: Timing and flakiness

- One-time build (`qmake6` + `make -j8`, from-scratch, includes vendored
  Opus): **~21s** wall-clock on this 8-core arm64 box (`time make -j8`
  reported `real 0m20.764s` for the run that got furthest before hitting
  the missing-`lrelease` issue; the remainder completed in a further
  `~5s` after installing `qt6-l10n-tools`). Call it **~25-30s total** for
  a genuinely clean build including that dependency fix.
- Smoke test itself (`tools/smoke-test.sh`, excluding build): verification
  succeeded in **0.86-0.88s** internally (from JACK-dummy start to
  confirmed connection) across 5 consecutive runs; **~1.17-1.18s** total
  wall-clock per run including shell/process startup overhead measured
  from outside the script. All 5/5 runs passed with the same timing,
  no observed flakiness in this sample.
- Failure-path check: pointing the client at a port nothing listens on
  correctly makes the script time out (tested with a 4s timeout),
  print both processes' stdout for diagnosis, exit `1`, and still clean
  up jackd/server/client with no leaked processes — confirmed via
  `ps aux` before/after.
- Sample size caveat: 5 runs on one idle box is not enough to rule out
  rare flakiness (e.g. under CI runner contention, slower disks, or a
  cold JACK dummy driver on first load). A CI rollout should watch the
  first few dozen real runs before trusting the default 20s timeout as
  tight.

## What a real CI job would additionally need

- **New apt packages** beyond whatever the existing Jamulus CI build
  already installs for a headless build: `jackd2`, `libjack-jackd2-dev`
  (if not already pulled in for the existing headless-build CI job —
  worth checking `.github/workflows/*.yml`), `qt6-l10n-tools` (or
  whatever ships `lrelease` on the target distro/Qt version — this may
  already be present in CI images that build the GUI variant, but a
  headless-only build job might not otherwise need it and could hit the
  same missing-`lrelease` failure this spike hit).
- **Build time budget**: ~20-30s for a from-scratch `qmake+make` on a fast
  box; call it "a minute or so" as a conservative CI estimate on typical
  shared runners, versus the ~30s ballpark for the project's existing
  unit-test jobs. If Jamulus CI already builds a headless server/client
  binary for other purposes (e.g. package builds), reusing that artifact
  instead of a dedicated build in the smoke-test job would avoid paying
  for the build twice.
- **Caching**: the dominant build cost here is vendored Opus (many small
  `.c` files) plus Qt's moc/rcc/lrelease steps; a ccache/sccache layer
  keyed on `libs/opus/**` and `src/**` would likely make repeat runs (PRs
  that don't touch Opus) much cheaper. Not implemented/measured in this
  spike — flagged as a follow-up if this becomes a real recurring CI job.
- **No X server / display needed at all** — confirmed; `QT -= gui` in
  headless mode means no `xvfb-run` or similar is required, unlike a full
  GUI Jamulus build/test.
- **No real audio hardware needed** — confirmed; JACK's dummy driver is
  sufficient and is available from Debian/Ubuntu's normal `jackd2`
  packaging (should be equally available on Ubuntu, matching the "Linux
  first" / Ubuntu-or-Debian framing in the original issue).
- **Container/runner privileges**: the dummy JACK driver's two RT
  warnings (`Cannot lock down ... memory area`, `Cannot use real-time
  scheduling`) appeared in this unprivileged environment and were
  harmless — worth a quick confirmation on the actual GitHub-hosted (or
  self-hosted) runner type used, but nothing here suggests elevated
  container privileges would be required.
- **Docker-in-Docker not needed**: the original issue sketch proposed two
  Docker containers; this spike shows plain localhost processes (server +
  client + jackd, all as sibling processes in one job) are sufficient —
  matching commenter @ann0see's question on the issue ("Would a localhost
  test be sufficient?"). Simpler and faster than spinning up containers.

## Open questions for a human

1. Where should this actually live/run in CI — a dedicated job on every
   push/PR (cheap enough now, ~1-2 min total incl. build), or restricted
   to (pre-)releases as originally sketched in #2428? Given the low cost
   measured here, a per-PR job seems affordable, but that's a project
   policy call, not a technical one.
2. Should the real per-platform matrix from #2428 (macOS/Windows,
   old-client-vs-new-server compatibility) be scoped into a first PR, or
   is Linux-only + same-version enough for v1 (issue explicitly says
   "let's start with the simple stuff")?
3. `tools/smoke-test.sh` currently assumes the caller already built
   Jamulus and passes/exports the binary path — should the eventual CI
   integration build it inline in the same job, or reuse an artifact from
   an existing build job?
4. Final location/name of the script (`tools/smoke-test.sh` is
   provisional) and whether the JSON-RPC ports/timeout should be
   configurable via workflow inputs vs. hardcoded, given `--jsonrpcport`
   is explicitly documented as "EXPERIMENTAL, APIs might still change."
5. Is relying on the experimental JSON-RPC interface for CI gating
   acceptable long-term, given its own docs disclaim it's not covered by
   semantic versioning? (It's still the best signal available today, but
   worth flagging since a CI process might now implicitly depend on Q4/Q5
   sub-fields it exposes.)
