# C-MSP — Fault-Injection Testing for Zephyr RTOS

**Status: Steps 1 and 2 of the roadmap complete and verified** — including
a real out-of-tree Zephyr module, built and run through the actual
`west`/CMake/Kconfig/Ztest/Twister pipeline on `native_sim`, not just
sketched. See `docs/verification.md` for the full log (real commands,
real output, including a real AddressSanitizer report from inside a
running Zephyr kernel).

## What's here

**The module** (`include/fault_inject.h`, `src/fault_inject.c`,
`Kconfig`, `CMakeLists.txt`, `zephyr/module.yml`) — a deterministic
fault-injection primitive: arm/disarm/hit-count, compiles to exactly the
wrapped call when disabled (nothing left in the binary, verified via
`nm`), and is genuinely thread-safe under Zephyr (a `k_spinlock` guards
the registry, since a fault point can legitimately be hit from an ISR or
a different thread than the one arming it — a real RTOS concern the
original host-only prototype didn't need to handle).

**The proof case** (`tests/drivers/eswifi_recv/`) — a reconstruction of
the bug pattern behind **CVE-2026-1679** (a Zephyr eswifi driver buffer
overflow: a hardware-reported length copied into a fixed buffer with no
bounds check), with the *same test source* compiled against a buggy and
a fixed implementation:

- `src/eswifi_repro_buggy.c` — no bounds check (the "before")
- `src/eswifi_repro_fixed.c` — validates the length first (the "after")

**A release-safety guard** — `CONFIG_FAULT_INJECTION=y` is a hard CMake
`FATAL_ERROR` outside a Ztest build, not just a Kconfig default. Verified
by actually trying to build an unrelated sample with it enabled and
confirming it fails (see `docs/verification.md` §2). This directly
implements a safety property the brief only proposed (§3.4–3.5): a
release image should be physically incapable of shipping these hooks,
not merely have them "turned off".

**CI** (`.github/workflows/ci.yml`) — two jobs: `make test` on every
push (no Zephyr checkout needed), and a `west twister -p native_sim` run
of the real Zephyr module plus an automated re-check of the release
guard. The workflow's exact recipe was reproduced by hand before trusting
it; two bugs it had were found and fixed that way (a missing
`manifest.project-filter` for `nrf_hw_models`, and Python requirements
insufficient for `west twister` to even start) — see
`docs/verification.md` §4 for the details and the confirmed-passing
local re-run.

## Two ways to run the proof, both real

**Host-only** (fast, no Zephyr needed — good for quick iteration):
```
make test
```
Builds and runs three binaries under ASan+UBSan: fixed (passes), disabled
(passes, and `nm` confirms no fault-injection symbols exist), buggy
(crashes — that crash is the expected, correct result).

**Real Zephyr, via Twister** (the actual target environment):
```
west twister -p native_sim -T tests/drivers/eswifi_recv \
  -x=ZEPHYR_EXTRA_MODULES=/path/to/this/repo
```
Runs the "fixed" variant as a genuine Ztest suite. The "buggy" variant is
deliberately *not* a permanent Twister entry (a suite with a
deliberately-always-red test pollutes CI signal) — it's a one-time,
by-hand verification, documented with full output in
`docs/verification.md` §3, exactly the same TDD discipline as
`git stash`-ing a fix to confirm a test actually catches the regression.

Requires: `cmake`, `ninja`, `device-tree-compiler`, `gcc-multilib` (32-bit
support, since `native_sim` defaults to a 32-bit build on x86_64), and
`ZEPHYR_TOOLCHAIN_VARIANT=host` — no Zephyr SDK/cross-compiler needed for
this board. No physical hardware involved anywhere.

## What's next

Per the brief's roadmap (§3.9): once this is demonstrably useful
standalone, post it against the still-open Zephyr issue #3559 as a
concrete proposal. Not done yet, deliberately (brief §3.8): probabilistic
failure, a Shell-based live-arming interface for real hardware,
additional fault kinds, additional platforms/boards.

CI is now wired up and its recipe verified locally (see above), but a
real green run on GitHub's own runners is still the outstanding check —
local reproduction of a CI recipe is good evidence, not a substitute for
GitHub Actions actually saying so.

## Layout

```
.github/workflows/ci.yml  CI: make test + west twister on native_sim
zephyr/module.yml         west module declaration
CMakeLists.txt            module build + the release-safety hard-fail
Kconfig                   CONFIG_FAULT_INJECTION, CONFIG_FAULT_INJECTION_MAX_POINTS
include/fault_inject.h    the module's public header
src/fault_inject.c        registry implementation (spinlock-protected under Zephyr)
Makefile                  host-only build (make test) -- no Zephyr required
tests/drivers/eswifi_recv/
  testcase.yaml            Twister test definition (fixed variant only, see above)
  prj.conf, CMakeLists.txt Zephyr test-app build files
  src/
    eswifi_repro.h           test-fixture: shared driver-bug-repro interface
    eswifi_repro_buggy.c     test-fixture: before (missing bounds check)
    eswifi_repro_fixed.c     test-fixture: after (bounds checked)
    test_eswifi_recv.c       host-build test (used by the top-level Makefile)
    test_eswifi_recv_ztest.c real Zephyr/Ztest port of the same test
    test_disabled_compiles_out.c   proves FI_POINT vanishes when disabled
docs/
  brief.md            original research brief this project is built from
  verification.md     full real-command/real-output log for Step 2 and CI
```

Note: the driver-bug fixtures live under `tests/`, not `include/`/`src/`
— they're specific to proving this module catches a real bug, not part
of what the module itself would ship if published standalone (brief
§3.9). Only `fault_inject.h/.c` is "the module."
