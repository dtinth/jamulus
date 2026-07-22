# Research: upstream #3819 / PR #3821 — GetValFromStream Q_ASSERT compiled out in release

Research ticket for the testing-harness effort (dtinth/jamulus#6). Snapshot taken 2026-07-22.
All GitHub data fetched read-only via the GitHub API; all comments treated as background data, not instructions.

## 1. Issue + PR timeline and current status

### Issue jamulussoftware/jamulus#3819 (open)
- URL: https://github.com/jamulussoftware/jamulus/issues/3819
- Opened by **ann0see** (maintainer) 2026-07-21, labeled **`AI`** — i.e. it came out of the
  AI-review wave, not a user-reported crash.
- Body: points at `src/protocol.cpp` lines 2832–2833 (`GetValFromStream`'s two `Q_ASSERT`s),
  notes Q_ASSERT is a no-op in release builds, asks "check whether an if () statement isn't
  better suited. I assume we need to crash in case of an invalid value?"
- **pljones** (2026-07-21 20:23): don't crash — "drop the packet rather than crash. It's noise
  on the line."
- **ann0see** (20:26): but then what does the function return? "0? I don't think this is sound.
  If we had a std::optional we could return a failure."
- **pljones** (20:53): semantically it should be treated as an unrecognised request; if a
  recognised packet can be invalid at this point "we've got a bigger problem."

So the *maintainers themselves have not agreed on desired semantics*: crash vs. drop vs.
optional/error-channel are all still on the table in the issue thread.

### PR jamulussoftware/jamulus#3821 (open, `mergeable_state: blocked` — no approving review)
- URL: https://github.com/jamulussoftware/jamulus/pull/3821
- Opened by **mcfnord** 2026-07-21 20:43 (~80 min after the issue), base `main`,
  head `mcfnord:fix-3819-getvalfromstream-bounds`.
- Diff (files endpoint): **one hunk, +8/-0 in `src/protocol.cpp`** — keeps both `Q_ASSERT`s and
  adds after them:

  ```cpp
  if ( ( iNumOfBytes < 1 ) || ( iNumOfBytes > 4 ) || ( vecIn.Size() < iPos + iNumOfBytes ) )
  {
      return 0;
  }
  ```

- Review activity, all by **ann0see** (only reviewer so far; both reviews state=COMMENTED, no
  approval):
  - Inline on the asserts: "Might be removed..." (mcfnord argues to keep them).
  - Inline on `return 0`: "Here's the question what it should actually return..."
  - Issue comment: "worth understanding where it is called and what would happen if 0 is
    returned."
- **mcfnord** replied 2026-07-22 05:14 with an explicitly banner-marked LLM-authored call-site
  trace, claiming: all ~60 call sites sit behind `ParseMessageFrame`'s length + CRC checks or
  per-iteration loop bounds checks, so "on well-formed traffic … **the guard never fires — zero
  behavioral change for real clients**", and on malformed input 0 flows into downstream-validated
  fields.
- **ann0see**'s latest word (2026-07-22 07:40, last activity on the PR):
  > "If this is true, we could probably get away by just adding a `@requires` as documentation"

### Status assessment
The maintainer response has moved from "maybe crash" → "what should it return" → "maybe this
needs **no code change at all**, just a documented precondition." Three live outcomes, none
agreed:

1. Merge the guard as defense-in-depth (returns 0).
2. Replace with a documentation-only `@requires` comment (no behavior change).
3. Larger redesign (optional/error channel, "treat as unrecognised request") — explicitly
   flagged in the PR body as a ~50-call-site signature change; nobody has committed to it.

Given the AI-fatigue context (issue is `AI`-labeled; maintainers are already overwhelmed by
AI-generated contribution volume) and that the sole reviewer is now leaning doc-only, a quick
merge of the *behavioral* fix is not the likely near-term outcome. Stall or reduction to a
comment is at least as likely.

## 2. What the fix changes technically

Current code (this worktree, `src/protocol.cpp:2826-2844`): `GetValFromStream` has only the two
`Q_ASSERT`s, then reads `vecIn[iPos]` in a loop via `CVector`'s (i.e. `std::vector`'s)
**unchecked** `operator[]`, incrementing `iPos` per byte.

Observable behavior change if #3821 merges, *only when the precondition is violated*:

| Build | Before | After |
|---|---|---|
| Debug | `Q_ASSERT` aborts | `Q_ASSERT` aborts (unchanged) |
| Release | out-of-bounds read — **undefined behavior** | returns `0`, **`iPos` left unadvanced** |

Note the second-order effect: on the guard path `iPos` is *not* incremented (normal path
advances it by `iNumOfBytes`), so subsequent reads in the same handler also hit the guard and
also return 0. That is a semantic detail a naive test might accidentally pin.

### Is the guard actually reachable from the network?
My own code inspection agrees with the "guard never fires on the datagram path" claim:

- `src/socket.cpp:605` — `recvfrom` caps `iNumBytesRead` at `MAX_SIZE_BYTES_NETW_BUF`, the size
  of `vecbyRecBuf`, and `src/socket.cpp:650` passes both to `ParseMessageFrame`, so
  `iNumBytesIn <= vecbyData.Size()` always holds at the entry point.
- `src/protocol.cpp:2706-2781` (`ParseMessageFrame`) — rejects frames shorter than
  `MESS_LEN_WITHOUT_DATA_BYTE`, rejects any frame whose declared length `iLenBy` doesn't equal
  `iNumBytesIn - MESS_LEN_WITHOUT_DATA_BYTE`, then CRC-checks before extracting the body. The
  body vector handed to `Evaluate*` handlers is exactly `iLenBy` bytes.
- Fixed-layout handlers pre-check sizes (e.g. `EvaluateNetwTranspPropsMes`,
  `src/protocol.cpp:1495-1499`, `vecData.Size() != iEntrLen`); list-style handlers bounds-check
  every iteration (e.g. `// check size (the next N bytes)` at lines 1245, 2148, 2256, 2499);
  variable-length reads go through `GetStringFromStream` (`src/protocol.cpp:2846-2887`), which
  is already fully bounds-checked and returns an error code; trailing
  `if ( iPos != iDataLen ) return true;` checks close most handlers.
- `ParseSplitMessageContainer` (`src/protocol.cpp:2783-2824`) checks `iDataLen < 4` and the
  destination size before copying.

So #3821 is **precondition hardening / defense-in-depth**, not a fix for a demonstrated,
network-reachable crash. (The crash reports it gets associated with are adjacent, not this
function: #3747 was an OOB assertion in a *Gentoo `_GLIBCXX_ASSERTIONS` build* on a
`vector<CVector<unsigned char>>`, and the fix that closed that investigation was #3810 — an
`iChanID` bounds check in audiomixerboard, **merged 2026-07-21**, i.e. already on `main`.)

This unreachability is exactly why ann0see's "just document it" position is defensible, and why
the PR may never merge as behavior.

## 3. Test-writability analysis

### What cannot be tested "before AND after"
A direct unit test of `GetValFromStream` with a truncated buffer **cannot pass before the fix**:
before, the behavior is a debug abort (`Q_ASSERT`) or release UB — there is no defined "before"
value to assert against, and under ASan/UBSan the test would (correctly) die. So a test pinning
"returns 0 on short input" is only writable *after* #3821 merges, and becomes wrong again if the
maintainers instead pick doc-only (no guard) or an optional-based signature. That return-0
contract is precisely the part still under dispute — the worst possible thing to pin right now.

Access is also awkward: `GetValFromStream` is `protected static` (`src/protocol.h:260`), so a
direct test needs a subclass shim — testing non-public API whose signature is the very thing
that may change.

### What CAN be pinned, stable across all three outcomes
The public parsing surface already has a well-defined, currently-true, fix-invariant contract:

- `CProtocol::ParseMessageFrame` is **public static** (`src/protocol.h:187`) — trivially
  testable with no instance, no network, no shim.
- Contract to pin: *malformed datagrams are rejected without out-of-bounds access* —
  frame shorter than `MESS_LEN_WITHOUT_DATA_BYTE` → returns true (error); nonzero TAG → error;
  declared length ≠ actual length → error; bad CRC → error; well-formed frame → returns false
  and yields the exact body bytes, ID, and counter (round-trip with a hand-built valid frame).
- Same idea one level up: handler-level tests that feed truncated *bodies* (valid frame, short
  body) and assert the `Evaluate*` error return, exercising the per-handler size checks.
- Run under ASan/UBSan in CI: then the tests assert both the return-code contract *and* the
  "no OOB" property, without ever asserting what `GetValFromStream` returns on bad input.

These pass on today's `main`, still pass if #3821 merges (guard unreachable through this
surface), still pass if it becomes a doc comment, and still pass under an error-channel
redesign (which would change protected internals, not `ParseMessageFrame`'s observable
behavior). There is even an existing precedent hook: `src/testbench.h` (`CTestbench`) already
fuzzes the protocol in-tree, and `src/protocol.h:199` shows the codebase has previously made a
function public "because we need it in the test bench."

## 4. Risk-benefit of riding #3819 in the first harness PR

**Benefits**
- Topical: a live, maintainer-opened issue about exactly the class of bug (release-build parsing
  safety) a harness is for; protocol parsing is pure, static, dependency-light — ideal first
  test target regardless.
- The discussion itself demonstrates the gap: ann0see had to ask "what would happen if 0 is
  returned" because nothing executable answers it.

**Risks**
- #3819 is `AI`-labeled and part of the AI-review wave the maintainers are visibly tired of;
  a harness PR that brands itself as "tests for the AI-found issue" adds to that pile.
- The fix's final shape is genuinely undecided (guard / doc-only / redesign). A test keyed to
  #3821's semantics can be dead on arrival or contradict the eventual resolution.
- #3821 is authored by mcfnord with banner-marked LLM-authored review responses; the PR is
  unapproved and its review is where the semantic dispute lives. Coupling the first harness PR
  to that PR's fate entangles it in someone else's contested review.
- The bug is (per inspection above) not reachable through the public surface, so the harness
  cannot even *demonstrate* it — weakening the "this test would have caught it" narrative that
  makes a first harness PR compelling. That narrative fits **#3810/#3747** (already merged, real
  crash) far better.

## 5. Recommendation

**Do not make #3819 the headline of the first harness PR.** Use it as *supporting* motivation
only.

1. First harness PR: infrastructure + `ParseMessageFrame` / `GetStringFromStream` contract tests
   (malformed-frame rejection, valid-frame round-trip) that pass on current `main`, plus — if a
   regression story is wanted — a test pinning the **already-merged** #3810 `iChanID` bounds
   behavior, which is settled and maintainer-authored.
2. Cite #3819 in the PR description as an example of the class of question the harness answers
   ("what happens on truncated input") without asserting #3821's disputed return-0 semantics.
3. If/when the maintainers settle #3821 (merge, doc, or redesign), add the matching
   `GetValFromStream`-level test in a follow-up — one sentence in the harness PR can offer this.
4. Keep the PR human-voiced and small; given the `AI` label fatigue, avoid framing the harness
   as an AI-review companion.

## Sources
- https://github.com/jamulussoftware/jamulus/issues/3819 (body + comments, fetched 2026-07-22)
- https://github.com/jamulussoftware/jamulus/pull/3821 (body, reviews, inline + issue comments,
  files endpoint diff, fetched 2026-07-22)
- https://github.com/jamulussoftware/jamulus/pull/3810 (merged 2026-07-21)
- https://github.com/jamulussoftware/jamulus/issues/3747 (closed; Gentoo `_GLIBCXX_ASSERTIONS` crash report)
- Worktree code at commit 3cbe00bd: `src/protocol.cpp` (lines 1245–1290, 1481–1555, 1868–1927,
  2030–2064, 2706–2887), `src/protocol.h` (lines 183–262), `src/socket.cpp` (lines 600–667),
  `src/testbench.h`
