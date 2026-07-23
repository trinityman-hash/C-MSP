# C-MSP — Fault-Injection Testing for Zephyr RTOS

**Status: Step 1 of the roadmap complete and verified.** Not yet a Zephyr
module — this step is deliberately host-only, per the project brief's own
"concrete next step" (see `docs/brief.md`, §6): prove the idea catches a
real bug before building any Zephyr/Twister integration around it.

## What's here

A deterministic fault-injection primitive (`fault_inject.h/.c`) and a
reconstruction of the bug pattern behind **CVE-2026-1679** (a Zephyr
eswifi driver buffer overflow: a hardware-reported length is copied into
a fixed buffer with no bounds check).

The same test file (`tests/test_eswifi_recv.c`) is compiled against two
implementations of the identical function:

- `src/eswifi_repro_buggy.c` — no bounds check (the "before")
- `src/eswifi_repro_fixed.c` — validates the length first (the "after")

## What's proven, and how

```
make test
```

runs three builds under AddressSanitizer + UBSan:

1. **`test_recv_fixed`** — all checks pass. The fault-injection point
   forces the hardware-length call to report an oversized value (a
   condition real hardware won't easily produce on demand), and the
   fixed driver rejects it safely.
2. **`test_disabled`** — compiled *without* `FAULT_INJECTION_ENABLED`,
   and not linked against `fault_inject.c` at all. Passes cleanly, and
   `nm` on the resulting binary contains none of the `fi_*` symbols —
   proving `FI_POINT()` compiles to exactly the original call, not a
   disabled check. (See brief §3.4 for why that distinction matters for
   release builds.)
3. **`test_recv_buggy`** — **expected to crash.** AddressSanitizer
   reports a stack-buffer-overflow inside `memcpy`, at the exact line
   with the missing bounds check, only when the fault-injected oversized
   length is armed. That crash is the result being tested for: it's the
   proof this reproduces a real defect, not a hypothetical one.

All three outcomes were run and confirmed before this was pushed.

## What's next

Per the brief's roadmap (§3.8–3.9): package this primitive as a real
out-of-tree Zephyr module (`zephyr/module.yml`, Kconfig, Ztest test under
Twister), targeting `native_sim`. That step needs a Zephyr SDK and `west`
toolchain this environment doesn't have — the code here is structured so
that's a port, not a rewrite: `fault_inject.h/.c` has no host-specific
dependencies beyond a C11 compiler, and the fault-point pattern
(`FI_POINT(id, real_call)`) is exactly the macro proposed for the Zephyr
module.

Not done yet, on purpose (see brief §3.8, "everything past this is
deliberately deferred"): probabilistic failure, a Shell-based live-arming
interface, additional fault kinds, additional platforms.

## Layout

```
include/
  fault_inject.h       fault-injection primitive (arm/disarm/hit-count)
  eswifi_repro.h        shared driver-bug-repro interface
src/
  fault_inject.c        registry implementation
  eswifi_repro_buggy.c   before: missing bounds check
  eswifi_repro_fixed.c   after: bounds checked
tests/
  test_eswifi_recv.c              the proof case (linked twice, see Makefile)
  test_disabled_compiles_out.c    proves FI_POINT vanishes when disabled
docs/
  brief.md               original research brief this project is built from
Makefile
```
