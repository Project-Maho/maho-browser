# Chromium Fork Cross-Build Bring-up — Parallel Execution Plan

**Created:** 2026-05-24  
**Branch:** main  
**Status:** Draft  
**Primary Scope:** `maho-chromium/build/`, `maho-chromium/third_party/maho/`, `maho/scripts/`, selected Chromium fork build validation entrypoints  
**Primary Goal:** Bring the Chromium fork from macOS-only practical buildability to a first verified non-mac build path, with Linux first and Windows staged behind shared infrastructure fixes.

---

## Objective

Establish a defensible, implementation-ready plan that gets the Chromium fork to its first real non-mac build path without mixing product parity work into infrastructure bring-up.

The plan is intentionally staged:

1. **Linux first-build bring-up** is the first delivery target.
2. **Windows first-build bring-up** follows only after the shared blockers are removed and the Linux path is validated.
3. Packaging polish, feature parity, and release automation are explicitly deferred until after first-build success.

This plan is written so multiple subagents can work in parallel after the minimum serial gates freeze the contracts and unblock the execution graph.

---

## Evidence-Based Starting Point

### Confirmed current state

1. The repository already documents Windows and Linux as intended targets:
   - `maho-chromium/docs/cross-platform-build.md`
   - Explicit target triples listed for Windows and Linux
   - `args_linux.gn` and `args_win.gn` are documented as starting templates

2. The Rust prebuilt helper already models cross-platform targets:
   - `maho-chromium/build/scripts/build_maho_core.py`
   - Handles Darwin, Linux, and Windows target triples
   - Produces `.a` for Unix-like targets and `.lib` for Windows targets

3. The top-level Chromium build wrapper is not yet non-mac safe:
   - `maho-chromium/build/scripts/build_maho.py`
   - Always runs `apply_chromium_src_overrides.py`
   - Always expects the shared lock helper from `maho/scripts/build_lock.py`

4. The Chromium override applier currently includes a macOS bundle plist patch with no visible platform gate:
   - `maho-chromium/build/scripts/apply_chromium_src_overrides.py`
   - Replacement entry exists for `chrome/app/app-Info.plist`
   - This is a likely failure point on non-mac trees unless explicitly skipped

5. The prebuilt GN glue is still effectively mac-centric:
   - `maho-chromium/third_party/maho/BUILD.gn`
   - `inputs` uses `libmaho_ffi.a` in both `arm64` and `x64` directories
   - Only `if (is_mac)` system-framework/system-lib linkage exists
   - No visible `is_linux` or `is_win` linkage blocks exist

6. Actual non-mac prebuilt artifacts are absent:
   - `maho-chromium/third_party/maho/prebuilt/arm64/` exists and is populated for macOS
   - `maho-chromium/third_party/maho/prebuilt/x64/` exists but there is no confirmed Linux or Windows library present

7. The shared build lock is not Windows-safe:
   - `maho/scripts/build_lock.py`
   - Imports and uses `fcntl.flock`
   - This is Unix-safe but not Windows-safe

8. Some C++ product code is already portability-aware:
   - Multiple files use `BUILDFLAG(IS_MAC)` with non-mac fallbacks
   - `maho-chromium/browser/BUILD.gn` contains explicit `is_win` and `is_linux` stubs for Little Arc
   - This is a sign of intended future support, not proof of a working build

9. Packaging and verification remain mac-centric:
   - `maho-chromium/branding/README_PLATFORM.md` says Windows `.ico` generation is missing and Linux desktop entry is only suggested
   - `maho-chromium/docs/chromium-desktop-e2e.md` is macOS-only
   - No Chromium-fork Windows/Linux CI evidence has been found

### Architectural implication

The current state is **scaffolded, not partially functional**.

The repository has enough structure to justify a focused bring-up effort, but it does not yet have an end-to-end non-mac build path. The first job is not product parity. The first job is to make the build graph and build scripts tell the truth on Linux, then reuse that work to open a Windows path.

---

## Target End State

### User-visible outcome for engineering

The engineering team can do all of the following without ad hoc one-off local hacks:

1. Build the Rust prebuilt for Linux x64 and link it into the Chromium fork.
2. Run the canonical Chromium fork build flow on a Linux host without mac-only override failures.
3. Produce a first validated Linux Chromium build artifact.
4. Reuse the same platform-aware build infrastructure to begin a Windows first-build path.

### Delivery milestone definition

| Milestone | Meaning |
|---|---|
| M1 | Linux build scripts and GN glue are non-mac safe |
| M2 | Linux prebuilt + Chromium link complete |
| M3 | Linux smoke validation documented and repeatable |
| M4 | Windows infra blockers removed |
| M5 | Windows prebuilt + Chromium link attempted with bounded blocker list |

### Non-negotiable rule

This project is not complete when docs look cross-platform. It is complete only when the build entrypoints, GN graph, and generated artifacts prove at least one non-mac build path actually works.

---

## In-Scope Files

### Shared build infrastructure

- `maho/scripts/build_lock.py`
- `maho-chromium/build/scripts/build_maho.py`
- `maho-chromium/build/scripts/build_maho_core.py`
- `maho-chromium/build/scripts/apply_chromium_src_overrides.py`

### GN and prebuilt integration

- `maho-chromium/third_party/maho/BUILD.gn`
- `maho-chromium/third_party/maho/prebuilt/`
- `maho-chromium/build/config/args_linux.gn`
- `maho-chromium/build/config/args_win.gn`

### Platform/product readiness checkpoints

- `maho-chromium/browser/BUILD.gn`
- `maho-chromium/branding/README_PLATFORM.md`
- `maho-chromium/docs/cross-platform-build.md`
- `maho-chromium/docs/chromium-desktop-e2e.md`

### Likely validation and documentation touchpoints

- Linux bring-up notes under `maho-chromium/docs/`
- Chromium build invocation notes under repo docs if needed
- CI workflow files only after first-build success is proven locally

---

## Explicitly Out of Scope

- No GN “cleanup” driven by speculative fixes outside the first observed blocker
- No broad product feature parity across Linux and Windows in the first pass
- No Little Arc implementation on Linux/Windows during build bring-up unless it becomes a hard compile blocker
- No Windows `.ico` generation pipeline in the first Linux milestone
- No Linux desktop integration polish in the first Linux milestone
- No release packaging or notarization-equivalent workflows in this phase
- No commit instructions in this plan

---

## Execution Model

This project has **two mandatory serial gates** before the largest safe parallel batch:

1. Freeze the Linux-first bring-up contract.
2. Make shared build infrastructure truthful and platform-aware.

After that, implementation can split into parallel tracks with limited overlap.

### Parallelism ceiling

- Recommended concurrent implementation tracks after Gate 1: **4**
- Recommended concurrent validation / hardening tracks after first Linux success: **3**

---

## Global Constraints

- Every workstream must preserve the existing macOS path unless a change is explicitly platform-gated.
- Linux is the first-class bring-up target; Windows work must not destabilize Linux while Linux is still unverified.
- No workstream may treat a missing feature stub in `browser/BUILD.gn` as justification for unrelated feature work.
- The first non-mac milestone is **build truth**, not UX completeness.
- If a GN or link failure appears, fix the narrowest proven blocker first and do not widen scope without evidence.
- Do not edit upstream Chromium source directly; stay within the overlay and allowed override flow.

---

## Serial Gates

## Gate 0 — Freeze Linux-First Bring-up Contract

**Purpose:** Prevent implementation streams from solving different problems.

### Required decisions locked by this gate

- The first success criterion is a **Linux host build**, not true cross-compilation from macOS.
- The first deliverable is a **linked Chromium build**, not release packaging.
- `apply_chromium_src_overrides.py` must become platform-aware before any non-mac build claim is trusted.
- `third_party/maho/BUILD.gn` is the canonical integration seam for Rust prebuilt linkage.
- `browser/BUILD.gn` Little Arc stubs are treated as **deferred unless proven blocking**.
- Windows bring-up does not start until Linux has a stable blocker list or a successful build.

### Acceptance criteria

- One written milestone order is agreed: Linux infra → Linux build → Linux smoke → Windows infra → Windows build attempt
- No stream starts from a “full parity first” interpretation
- Owners know which files are infrastructure-critical versus optional follow-up

### Executable QA scenario

- Tool: code review + targeted file read
- Files to inspect:
  - `maho-chromium/build/scripts/build_maho.py`
  - `maho-chromium/build/scripts/apply_chromium_src_overrides.py`
  - `maho-chromium/third_party/maho/BUILD.gn`
  - `maho/scripts/build_lock.py`
- Expected result:
  - the Linux-first milestone and Windows-later ordering are explicit
  - the critical infrastructure seams are named and frozen

---

## Gate 1 — Make Shared Build Infrastructure Platform-Aware

**Purpose:** Remove the known infra lies before asking GN/ninja to prove anything.

### Target files

- `maho/scripts/build_lock.py`
- `maho-chromium/build/scripts/build_maho.py`
- `maho-chromium/build/scripts/apply_chromium_src_overrides.py`
- `maho-chromium/docs/cross-platform-build.md`

### Required changes

1. Define the supported behavior of the shared lock helper by platform.
2. Ensure the non-mac build path does not die because mac-only override targets are absent.
3. Make the top-level build wrapper honest about platform expectations.
4. Update docs so the canonical path reflects the real staged support level.

### Acceptance criteria

- Linux host can run the build scripts without a known mac-only override failure
- Windows limitations are explicit rather than implicit
- The script layer communicates correct expectations before GN/link work begins

### Executable QA scenario

- Script-level validation only
- Expected result:
  - Linux invocation reaches GN/autoninja stage rather than failing in wrapper/override code
  - Windows wrapper limitation is either removed or documented as still pending behind an explicit TODO

---

## Parallel Workstreams

After Gate 1, the project can split into these parallel tracks.

## Workstream A — Linux Script Bring-up and Override Safety

### Primary files

- `maho-chromium/build/scripts/apply_chromium_src_overrides.py`
- `maho-chromium/build/scripts/build_maho.py`

### Goal

Make the canonical Chromium fork build flow safe to invoke on Linux without dying on mac-only assumptions.

### Tasks

- Add platform-aware handling or existence-based skipping for mac-only override targets such as `chrome/app/app-Info.plist`
- Preserve idempotence and clear failure messages for genuine missing overlay targets
- Keep `build_maho.py` behavior unchanged for macOS while making non-mac expectations explicit
- Document the override behavior contract in code comments where needed

### Constraints

- Do not silently ignore unrelated missing files that would indicate overlay drift
- Keep the mac override path intact
- Do not introduce Windows-specific lock logic here; that belongs to Workstream D unless strictly necessary for Linux

### Exit criteria

- On Linux, the override phase exits cleanly when only mac-specific targets are absent
- The wrapper reaches the real Chromium build phase on Linux

---

## Workstream B — Linux Prebuilt Artifact and GN Linkage

### Primary files

- `maho-chromium/build/scripts/build_maho_core.py`
- `maho-chromium/third_party/maho/BUILD.gn`
- `maho-chromium/third_party/maho/prebuilt/`
- `maho-chromium/build/config/args_linux.gn`

### Goal

Create the first truthful Linux x64 prebuilt path and wire it into GN so Chromium can actually link it.

### Tasks

- Generate the Linux x64 Rust prebuilt and place it in the expected x64 prebuilt directory
- Make `third_party/maho/BUILD.gn` express Linux linkage explicitly rather than relying on mac-only linkage
- Ensure the x64 artifact path matches what GN expects on Linux
- Validate that `args_linux.gn` is sufficient for a first build, adding only minimal overlay-owned args if proven necessary

### Constraints

- Do not mix Windows `.lib` support into the first Linux linkage patch unless the abstraction naturally supports both with low risk
- Do not solve speculative future architectures during x64 Linux bring-up
- Avoid unrelated BUILD.gn cleanup outside the `third_party/maho` seam

### Exit criteria

- `third_party/maho/BUILD.gn` truthfully models Linux x64 linkage
- The Linux build reaches and passes the Rust prebuilt link seam
- Any remaining Linux blocker is no longer “missing or misdeclared prebuilt”

---

## Workstream C — Linux Validation, Smoke, and Bring-up Notes

### Primary files

- `maho-chromium/docs/cross-platform-build.md`
- likely a new Linux bring-up note under `maho-chromium/docs/`
- optional validation notes in `.sisyphus/plans/` or repo docs if the team wants an execution log

### Goal

Turn the first Linux success into a repeatable engineering procedure instead of a one-off local memory.

### Tasks

- Define the exact Linux host prerequisites actually required by the working path
- Record the canonical command sequence for Linux prebuilt build, GN args setup, and Chromium build invocation
- Add a smoke validation checklist focused on “binary exists / launches / obvious crash or missing-lib failures are absent”
- Record any still-deferred Linux product gaps separately from build success

### Constraints

- This stream starts after Workstream A and B each produce a real result or bounded blocker list
- Do not write aspirational docs that claim unverified support
- Keep smoke validation narrow and executable

### Exit criteria

- A fresh engineer can follow one written Linux bring-up path without tribal knowledge
- Deferred gaps are separated from first-build success criteria

---

## Workstream D — Windows Infrastructure Prereqs Behind Linux

### Primary files

- `maho/scripts/build_lock.py`
- `maho-chromium/third_party/maho/BUILD.gn`
- `maho-chromium/build/config/args_win.gn`
- `maho-chromium/branding/README_PLATFORM.md`

### Hard dependency

- This stream may start design work after Gate 1, but code landing should wait until Linux has either succeeded or reduced the blocker set enough to avoid cross-stream churn.

### Goal

Remove the shared infra blockers that make Windows impossible before platform-specific product gaps even matter.

### Tasks

- Replace or abstract the `fcntl`-based build lock so Windows entrypoints can run
- Extend the `third_party/maho` prebuilt integration so Windows `.lib` artifacts are represented truthfully
- Bound the minimum Windows system-link requirements at the GN seam
- Record packaging assets and branding gaps that remain after first build viability

### Constraints

- Do not let Windows requirements rewrite the Linux-first path unless the shared abstraction improves both safely
- No `.ico` pipeline or packaging automation work in the first Windows infra pass
- Keep Windows product stubs such as Little Arc out of scope unless they prove compile-blocking

### Exit criteria

- Windows can invoke the build entrypoints without failing at import/lock time
- GN prebuilt declarations can represent a Windows artifact truthfully
- The first Windows build attempt fails, if at all, on a smaller bounded list than today

---

## Gate 2 — Linux Integration Build

**Objective:** Prove the Linux-first path with a real integrated build.

### Tasks

- Run the Linux prebuilt generation path
- Configure a Linux Chromium output directory from the Linux args template
- Run the canonical build wrapper or the minimal equivalent validated by Gate 1
- Capture the first real blocking error after the known script/GN fixes, if any remain

### Exit criteria

- Either the Linux build completes, or the next blocker is narrow, concrete, and outside the already-fixed script/link seams
- The blocker list is short enough to decide whether Linux M2 is done or one more patch is required

---

## Gate 3 — Linux Smoke Validation and Freeze

**Objective:** Convert “build completed” into “repeatable engineering milestone.”

### Tasks

- Verify the produced artifact exists at the expected Linux output path
- Perform a minimal launch or loadability check appropriate to the host environment
- Record unresolved runtime or product gaps separately from build success
- Freeze the Linux procedure and blocker list for reuse by Windows follow-up

### Exit criteria

- Linux first-build milestone is documented and reproducible
- The team can distinguish build-system success from later product-completeness work

---

## Gate 4 — Windows First Attempt

**Objective:** Attempt the first truthful Windows build path after shared blockers are reduced.

### Tasks

- Generate or place the Windows x64 prebuilt artifact
- Invoke the Windows-capable wrapper path
- Run the first GN/gen/build attempt
- Categorize failures into infra, linkage, compile-time product gaps, or packaging-only gaps

### Exit criteria

- Windows has a bounded, evidence-based blocker list
- The project can decide whether Windows is a short follow-up or a separate milestone project

---

## Deliverables by Phase

### Phase A — Linux-first infrastructure

- Non-mac safe override behavior
- Truthful Linux prebuilt and GN linkage
- Linux-first invocation notes

### Phase B — Linux proof

- Successful Linux integrated build or one final bounded blocker
- Smoke validation notes

### Phase C — Windows prerequisites

- Cross-platform lock strategy implemented
- Windows prebuilt declaration and path truth
- First Windows attempt with categorized blockers

---

## Verification Plan

### Script-level verification

- Linux wrapper invocation no longer fails on mac-only plist override
- Windows wrapper path no longer fails on `fcntl` import once Workstream D lands

### Build-graph verification

- `third_party/maho/BUILD.gn` points at the correct artifact type and directory for the active target
- GN input declarations match the actual generated prebuilt artifacts

### Linux executable verification

- Linux build command exits successfully, or fails on a newly surfaced narrow blocker
- Artifact presence is confirmed at the output location

### Windows readiness verification

- Windows build command can start
- Any failure is post-wrapper and post-prebuilt-declaration, not immediate infra breakage

---

## Risks and Failure Modes

### Risk 1 — Hidden Linux-specific link dependencies appear after the known prebuilt fix

**Mitigation:** Keep Linux linkage changes isolated to `third_party/maho/BUILD.gn` first, then adjust only based on actual linker output.

### Risk 2 — `apply_chromium_src_overrides.py` skipping becomes too permissive

**Mitigation:** Skip only clearly platform-scoped files or missing targets proven to be absent on non-mac builds; preserve hard failure for unexpected drift.

### Risk 3 — Windows abstractions destabilize Linux before Linux is proven

**Mitigation:** Make Linux the first accepted target; land Windows code behind explicit platform branches and after Linux freeze where possible.

### Risk 4 — Product stubs get mistaken for build blockers

**Mitigation:** Treat `browser/BUILD.gn` platform stubs as out of scope until an actual compile error proves otherwise.

---

## Recommended Delegation Map

### Serial coordinator owner

- Gate 0 and Gate 1 contract lock
- Gate 2 integration decision-making
- Final Linux/Windows milestone classification

### Parallel implementation owners

- **Stream A:** Python build wrapper and override safety
- **Stream B:** GN/prebuilt Linux linkage
- **Stream C:** docs, smoke validation, and reproducibility
- **Stream D:** Windows lock and prebuilt declaration groundwork

---

## Definition of Done

This plan is executed successfully only when all of the following are true:

1. Linux build infrastructure is platform-aware and truthful.
2. Linux x64 prebuilt artifact generation and GN linkage are proven.
3. A Linux Chromium fork build completes or is blocked by one narrow post-link blocker.
4. The Linux path is documented as a repeatable procedure.
5. Windows no longer fails at the immediate wrapper/lock/prebuilt-declaration layer.
6. Deferred product and packaging gaps are explicitly listed rather than silently bundled into “cross-platform support.”

Until then, the project remains in bring-up, not in supported cross-platform delivery.
