# C-MSP — Fault-Injection Testing for Zephyr RTOS

[![CI](https://github.com/trinityman-hash/C-MSP/actions/workflows/ci.yml/badge.svg)](https://github.com/trinityman-hash/C-MSP/actions/workflows/ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

A small, deterministic fault-injection primitive for Zephyr, built to let
a developer force a specific call to fail (or return an
externally-controlled bad value) during a test, so an error-recovery path
can be proven to actually work instead of just assumed to. Modeled on the
idea behind Linux's kernel fault-injection framework, deliberately scoped
down for Zephyr's needs — see `docs/brief.md` for the full research brief
this project is built from.

## Why

Error-handling code is some of the least-tested code in most codebases,
because the conditions that trigger it (a driver returning malformed
data, a hardware read failing) are hard to force on demand. `fault_inject`
gives a test a way to say "this call happens, but return this specific
bad value instead of the real result" — deterministically, with no
retries or flakiness, and with the injected code path exercised exactly
once per fault point per test.

As a concrete demonstration, this repo reconstructs the bug pattern
behind **CVE-2026-1679** (a Zephyr eswifi driver stack buffer overflow —
a hardware-reported length gets copied into a fixed-size buffer with no
bounds check) and shows a fault-injection test catching it: the same test
source, run against a buggy implementation and a fixed one, passes
against the fix and — genuinely, under a real ASan-instrumented Zephyr
build — crashes against the bug.

## Features

- **Deterministic arm/disarm/hit-count**, not probability/interval/budget
  based — easier to write a reliable, non-flaky test against, which is
  what matters most for a v0.
- **Compiles to nothing when disabled.** `FI_POINT(id, real_call)`
  expands to exactly `(real_call)` when `CONFIG_FAULT_INJECTION` isn't
  set — no check, no branch, no symbol. Confirmed with `nm` in
  `docs/verification.md`, not just asserted.
- **Thread-/ISR-safe under Zephyr.** The registry is guarded by a
  `k_spinlock`, since a fault point can legitimately be hit from an ISR
  or a different thread than the one arming it.
- **A release build cannot ship this by accident.** Enabling
  `CONFIG_FAULT_INJECTION` outside a Ztest build is a hard CMake
  `FATAL_ERROR`, not a soft default — see `CMakeLists.txt` and
  `docs/verification.md` §2.

## Quick start

**Host-only, no Zephyr needed** (fastest way to see it work):
```sh
make test
```
Builds and runs three binaries under ASan+UBSan: fixed (passes), disabled
(passes — and `nm` confirms no fault-injection symbols were even linked
in), buggy (crashes — that crash *is* the correct, expected result: it
proves the reproduced bug is real).

**Real Zephyr, via Twister** (the actual target environment):
```sh
west twister -p native_sim -T tests/drivers/eswifi_recv \
  -x=ZEPHYR_EXTRA_MODULES=/path/to/this/repo
```
Runs the fixed variant as a genuine Ztest suite on `native_sim`. Requires
`cmake`, `ninja`, `device-tree-compiler`, `gcc-multilib` (native_sim
defaults to a 32-bit build on x86_64), and
`ZEPHYR_TOOLCHAIN_VARIANT=host` — no Zephyr SDK/cross-compiler needed for
this board, and no physical hardware involved anywhere.

## Usage

```c
#include "fault_inject.h"

int eswifi_socket_recv(struct eswifi_socket *sock, struct eswifi_hw_mock *hw,
                        const uint8_t *hw_data)
{
    /* Normally: eswifi_hw_read_length(hw). A test can arm
     * FI_ESWIFI_RECV_LEN to substitute an attacker/hardware-controlled
     * value here instead, without touching eswifi_hw_read_length at all. */
    int len = FI_POINT(FI_ESWIFI_RECV_LEN, eswifi_hw_read_length(hw));

    if (len < 0 || (size_t)len > sizeof(sock->rx_buf)) {
        return ESWIFI_EMSGSIZE;
    }

    memcpy(sock->rx_buf, hw_data, (size_t)len);
    sock->rx_len = (size_t)len;
    return ESWIFI_OK;
}
```

From the test side:
```c
fi_arm(FI_ESWIFI_RECV_LEN, 9999);      /* force an oversized length */
zassert_equal(eswifi_socket_recv(&sock, &hw, data), ESWIFI_EMSGSIZE);
zassert_true(fi_hit_count(FI_ESWIFI_RECV_LEN) > 0); /* prove the point fired */
fi_disarm(FI_ESWIFI_RECV_LEN);
```

Full API in `include/fault_inject.h`: `fi_arm`, `fi_disarm`,
`fi_reset_all`, `fi_hit_count`, and the `FI_POINT` macro itself.

## How this is verified

Every claim above — the passing/crashing builds, the `nm` check, the
release guard actually failing a build, the real AddressSanitizer report
from inside a running Zephyr kernel, and the CI recipe itself — was
actually run, not asserted. Full commands and real output:
**`docs/verification.md`**.

## Status

Steps 1 (host-only proof) and 2 (real Zephyr module: Kconfig, CMake,
Ztest, Twister, thread-safety, the release guard) from the roadmap are
complete and verified. CI (badge above) builds and runs both the
host-only and the real-Zephyr proof on every push to `main`.

Deliberately not done yet, per the brief's own roadmap (§3.8–3.9) — not
missing work, just out of scope for v0:
- Probabilistic/interval/budget-based failure (Linux-style)
- A Shell-based live fault-arming interface for real hardware
- Additional fault kinds beyond "force a return value"
- Any board/platform beyond `native_sim` (no QEMU, no physical hardware)
- Posting this against Zephyr issue #3559 upstream — the brief says to
  do this once the module is "demonstrably useful standalone," which is
  arguably true now, but that's a call for the maintainer to make, not
  something to do unprompted.

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
  testcase.yaml            Twister test definition (fixed variant only, see below)
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
  verification.md     full real-command/real-output verification log
LICENSE                Apache-2.0, matching Zephyr's own license
```

The driver-bug fixtures live under `tests/`, not `include/`/`src/` — they
exist to prove this module catches a real bug, and aren't part of what
the module itself would ship if published standalone (brief §3.9). Only
`fault_inject.h`/`fault_inject.c` is "the module."

The buggy variant (`eswifi_repro_buggy.c`) is deliberately **not** wired
into the permanent Twister suite — a suite with an always-red test
pollutes CI signal. It's built and run by hand instead, the same
discipline as `git stash`-ing a fix to confirm a test actually catches
the regression it claims to; see `docs/verification.md` §3 for that run,
captured in full.

## License

Apache-2.0 — see `LICENSE`. Chosen to match Zephyr's own license, since
this project's eventual goal is contributing back upstream.
