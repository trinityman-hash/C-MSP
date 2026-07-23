# Fault-Injection Testing for Zephyr RTOS
### A problem/solution research brief

**Status:** Pre-build research brief — nothing has been coded yet.
**Prepared for:** solo build, initial scope of a few weeks.

---

## 1. Executive summary

Zephyr RTOS has a nine-year-old, still-open, officially-tracked feature request for a fault-injection testing framework, and nothing resembling it exists as an accessible developer tool today. Cloud engineering solved this exact class of problem years ago with chaos engineering (Chaos Monkey, Gremlin, Litmus) — deliberately breaking a system on purpose to prove its recovery logic actually works — but every one of those tools operates on networks and containers, not firmware. The proposal here is narrow: build a small, out-of-tree Zephyr module that lets a developer declare "this call could fail here," trigger that failure on demand inside Zephyr's existing test runner, and assert that the recovery path actually recovers — starting with one fault type, one platform (Zephyr), and no physical hardware required.

---

## 2. The problem

### 2.1 It's not a guess — it's a nine-year-old open issue

Zephyr's own issue tracker contains a feature request titled *"Fault injection framework for Zephyr"* (issue #3559), filed May 2017 by a Zephyr contributor. As of today it is still **Open**, sitting in the official "Test Framework & Test Runner (Twister)" project backlog, with a maintainer assigned and **no branches or pull requests** attached to it. The original report states the problem plainly: many subsystems have error-handling and recovery paths that are hard to exercise with normal unit tests, especially when they depend on the behavior of something external, and without a consistent framework, developers reach for inconsistent, ad-hoc workarounds instead — which the reporter explicitly flagged as a maintenance problem.

Zephyr is not a niche project. It is a Linux Foundation–hosted RTOS with roughly 1,000 contributors, 50,000+ commits, support for 250+ boards across ARM, x86, RISC-V, and other architectures, and (as of this writing) around 16,000 GitHub stars and 9,600 forks. This is a mainstream, foundational piece of embedded/IoT infrastructure with a real, named, unresolved gap in it.

### 2.2 The gap has a real cost, not just a hypothetical one

This isn't an abstract concern. Recent Zephyr security history shows the exact class of bug that fault injection is built to catch:

- **CVE-2026-1679** — a buffer overflow in Zephyr's eswifi socket offload driver: the driver copies a caller-supplied payload into a fixed-size buffer without first checking that the payload actually fits, allowing an oversized socket send to overflow into adjacent kernel memory.
- **CVE-2025-1673** — a Zephyr DNS packet handler that doesn't verify a packet actually contains its expected payload before reading it, causing an out-of-bounds read that can crash or hang a device processing malformed network traffic.
- Recent Zephyr release notes list a steady stream of "missing bounds check" driver fixes (e.g., a WiFi TWT event buffer bounds check, a TLS connection-ID buffer validation fix) — the same bug family, recurring.

All three are failures in a code path that should have handled a bad input gracefully and instead didn't — precisely the class of defect a fault-injection test (deliberately feeding a driver a bad/oversized/malformed input during a test run) is designed to surface before release, not after a CVE.

### 2.3 Why existing tools don't cover this

- **Cloud chaos-engineering tools** (Chaos Monkey, Gremlin, Litmus, AWS Fault Injection Service, Azure Chaos Studio) inject faults at the network and container level — killing pods, adding latency between microservices, partitioning network calls. None of them have a concept of a memory-mapped peripheral, an interrupt, or a hardware watchdog, because they were built for distributed cloud services, not single embedded devices.
- **Academic fault-injection research for RTOS/embedded systems exists, but isn't a usable developer tool.** The published work in this space is aimed at things like radiation-induced bit-flip reliability assessment for safety-critical hardware (aerospace/automotive) or side-channel/cryptographic attack research — specialized research instruments, not something a firmware developer installs and points at their own error-handling code.
- **Device observability platforms (e.g., Memfault)** are the closest adjacent category, and they are excellent at what they do — but they are reactive: they tell you what happened when a fault occurred naturally in the field (crash reports, coredumps, watchdog timeout logs). They do not let a developer deliberately manufacture a fault during development or CI to prove a recovery path works before shipping.

The gap is specifically: **a proactive, developer-facing, in-CI fault-injection framework for embedded/RTOS firmware.** Nothing found in this research covers that combination.

---

## 3. The proposed solution

> **A note on the code in this section:** every snippet below is a design sketch to scope the work, not compiled or verified code. There is no Zephyr SDK, no toolchain, and no network access available in the environment this document was written in, so nothing here has been built or run. Treat it as a blueprint to implement and test against a real Zephyr checkout, not as a drop-in.

### 3.1 Design principle

Keep the scope deliberately small. This is not a pitch for "chaos engineering for all embedded systems everywhere" — it's a fault-injection module for one RTOS (Zephyr), integrated into infrastructure that already exists, so the barrier to a working v0 is low.

### 3.2 Why Zephyr's existing test infrastructure makes this tractable

Zephyr already ships a test framework (**Ztest**) and a test runner (**Twister**) that:

- Discover tests via `testcase.yaml` files and run them automatically.
- Can run on `native_sim` — Zephyr's host-native simulator, which builds the *entire* Zephyr OS (kernel, drivers, subsystems) into an ordinary host binary that runs like real firmware, with no microcontroller or physical board required.
- Can also target QEMU-emulated boards (`qemu_cortex_m3`, `qemu_x86`, etc.) for closer-to-hardware behavior, still with no physical device.
- Are driven with a single command, e.g. `west twister -p native_sim -T tests/my_module/`.

This matters for scope: a solo developer with no hardware lab can build and validate a fault-injection module entirely on a laptop, using infrastructure Zephyr already maintains, rather than building a test harness from scratch.

### 3.3 Prior art — don't invent this from scratch

Before designing an API, it's worth being clear this problem has already been solved well once, just not in this domain. The Linux kernel has shipped an in-tree fault-injection framework for years:

- Functions are opted into error injection with an `ALLOW_ERROR_INJECTION()` annotation; a `should_fail()` check decides, at runtime, whether to force a failure.
- Existing injectable classes include forced allocation failures (`failslab`, `fail_page_alloc`), forced disk I/O errors (`make-it-fail`), and forced errors on arbitrary annotated functions (`fail_function`) — configured live via debugfs, with knobs for failure *probability*, *interval*, and an initial failure *budget* ("space") that gets decremented until it runs out.
- A well-known independent extension of the same idea (`ionos-enterprise/fault-injection`) makes the "must cost nothing when disabled" property concrete: a fault point compiles down to a single NOP when disabled, and only gets patched into a live jump when the fault is armed — the point itself is stateless and doesn't know *what* failure to inject until it's configured. This is the detail that makes fault injection safe to scatter liberally through a codebase without worrying about performance.
- Kernel documentation is explicit about *why* this matters: error paths are the least-tested code in any codebase, and a `goto err_free` cleanup path that has never once executed in production is a landmine, not a safety net.

None of this transfers directly to Zephyr — Linux's approach leans on debugfs (no filesystem like that exists on a microcontroller) and, in its most advanced form, on jump-label self-modifying code (architecture-specific, and a genuinely heavier lift than a solo v0 needs). But the *shape* of the design — annotate a call site, gate it behind a cheap check, configure the specific failure externally, keep disabled cost near zero — is exactly right and should be reused, not reinvented.

### 3.4 The fault point primitive

A fault point is a macro dropped at a call site the developer has identified as a plausible failure boundary — a driver call, a buffer copy, an allocation:

```c
/* include/fault_inject.h — illustrative, unverified */

#ifdef CONFIG_FAULT_INJECTION

int fi_should_fail(uint32_t fault_id);   /* 0 = pass through, nonzero = injected error code */

#define FI_POINT(id, real_call)                                   \
    ( { int _fi_rc = fi_should_fail(id);                          \
        (_fi_rc != 0) ? _fi_rc : (real_call); } )

#else

#define FI_POINT(id, real_call) (real_call)

#endif
```

Used at a call site:

```c
/* before */
ret = spi_transceive(dev, &config, &tx, &rx);

/* after */
ret = FI_POINT(FI_SPI_TRANSCEIVE, spi_transceive(dev, &config, &tx, &rx));
```

When `CONFIG_FAULT_INJECTION` is unset, `FI_POINT` expands to exactly the original call — not a disabled check, not a NOP, *nothing*. The macro doesn't exist in that build. This is a deliberately simpler (and, for firmware, safer) choice than Linux's runtime-toggleable jump-label approach: a release build should not merely have fault injection turned off, it should be physically incapable of containing a fault-injection hook at all. For safety- and security-relevant firmware, "compiled out" beats "disabled" — there is no code path for a misconfiguration, a stray debug build, or a future bug in the fault-injection module itself to accidentally trigger in the field.

### 3.5 Fault registry and Kconfig gating

A minimal, deterministic registry backs `fi_should_fail()` — deliberately simpler than Linux's probability/interval/budget model for v0, because deterministic arm/fire is far easier to write a reliable, non-flaky unit test against:

```c
/* fault_inject.c — illustrative, unverified */

struct fi_entry {
    uint32_t id;
    bool armed;
    int inject_rc;
    uint32_t hit_count;
};

static struct fi_entry fi_table[CONFIG_FAULT_INJECTION_MAX_POINTS];

int fi_should_fail(uint32_t fault_id)
{
    struct fi_entry *e = fi_lookup(fault_id);
    if (!e || !e->armed) {
        return 0;
    }
    e->hit_count++;
    return e->inject_rc;
}

void fi_arm(uint32_t fault_id, int inject_rc);
void fi_disarm(uint32_t fault_id);
void fi_reset_all(void);
uint32_t fi_hit_count(uint32_t fault_id);
```

`fi_hit_count()` exists specifically to guard against a quiet false-positive mode: a test that "passes" only because the fault point it armed was never actually reached. Every test in §3.7 should assert both the expected recovery behavior *and* `fi_hit_count(id) > 0` — proof the fault actually fired, not just that nothing crashed.

Kconfig:

```kconfig
# Kconfig — illustrative, unverified

config FAULT_INJECTION
    bool "Fault injection test hooks"
    default n
    help
      Enables FI_POINT() hooks throughout the tree. Must never be
      enabled in a release build. Intended for test/CI builds only.

config FAULT_INJECTION_MAX_POINTS
    int "Maximum number of distinct fault injection points"
    default 32
    depends on FAULT_INJECTION
```

A worthwhile extra safety rail: a build-time assertion (or a Twister/CI check) that fails the build outright if `CONFIG_FAULT_INJECTION=y` is combined with whatever the project's release/production build profile is, so this can't ship by accident even if someone forgets to check.

### 3.6 Test-time control API and an example test

Tests arm faults directly through C calls — no shell round-trip needed for automated CI, since Ztest tests run in-process:

```c
/* tests/drivers/spi_recovery/src/test_spi_recovery.c — illustrative, unverified */

#include <zephyr/ztest.h>
#include "fault_inject.h"

ZTEST(spi_recovery, test_transceive_failure_triggers_retry)
{
    fi_reset_all();
    fi_arm(FI_SPI_TRANSCEIVE, -EIO);

    int ret = my_sensor_read(&sample);

    zassert_true(fi_hit_count(FI_SPI_TRANSCEIVE) > 0,
                  "fault point was never reached — test proves nothing");
    zassert_equal(ret, 0,
                  "driver should have retried and recovered, got %d", ret);
    zassert_equal(sample.retry_count, 1,
                  "expected exactly one retry after injected failure");
}
```

### 3.7 Module layout and Twister wiring

```
fault-inject-zephyr/            <- standalone git repo, out-of-tree module
├── zephyr/
│   └── module.yml              <- declares this as a west module
├── CMakeLists.txt
├── Kconfig
├── include/
│   └── fault_inject.h
├── src/
│   └── fault_inject.c
└── tests/
    └── drivers/spi_recovery/
        ├── testcase.yaml
        └── src/test_spi_recovery.c
```

```yaml
# tests/drivers/spi_recovery/testcase.yaml — illustrative, unverified

tests:
  drivers.spi_recovery.fault_injection:
    tags: [drivers, fault_injection]
    platform_allow: [native_sim]
    harness: ztest
    extra_configs:
      - CONFIG_FAULT_INJECTION=y
```

Run with `west twister -p native_sim -T tests/drivers/spi_recovery/` — no board, no JTAG probe, no physical hardware. That command is the whole feedback loop.

### 3.8 What v0 actually is, restated concretely

1. `fault_inject.h` / `fault_inject.c` implementing exactly the deterministic arm/disarm/hit-count API above — **one** fault kind (force a return code), nothing else yet.
2. One Ztest test, structured like §3.6, proving a real (simplified, reconstructed) Zephyr driver bug from the missing-bounds-check family in §2.2 is caught: red against the buggy version, green against the fixed version.
3. Packaged as the module layout in §3.7, buildable and runnable via a single `west twister` command on `native_sim`.

Everything past this — probabilistic failure (Linux's probability/interval/budget model), a Shell-based interface for live arming on real hardware (Zephyr's own Shell subsystem, `CONFIG_SHELL`, is the natural place for this later — it already supports exactly this kind of runtime, per-module command registration), additional fault kinds (delay injection, buffer corruption), and additional platforms — is deliberately deferred. See §5.

### 3.9 Distribution: don't build in a vacuum, don't wait for a blessing either

Zephyr's module system (`zephyr/module.yml`, discovered automatically via the `west` workspace and `ZEPHYR_MODULES`) lets out-of-tree code integrate with a Zephyr build without being merged into the core tree. The plan:

- Ship this as a **standalone out-of-tree Zephyr module** first — installable, usable, and provable on its own, with no dependency on upstream approval.
- Once it demonstrably works (catches the planted bug from §3.8), post it against the existing, still-open issue #3559 as a concrete proposal — a real, already-interested audience, rather than a cold-start launch.
- Treat any upstream adoption as a bonus outcome, not the definition of success. The module is useful standalone regardless of whether Zephyr's core team ever merges it.

### 3.10 A natural dogfood loop

This project's own C watchdog daemon already has a documented fallback-signal mechanism. Once v0 exists, it becomes the first real test subject: does the daemon's fallback path actually engage and recover correctly when the fault it's designed for is deliberately triggered? That answers a real open question from this project's own status doc, using a tool built for exactly that purpose — not a hypothetical user, an actual one.

---

## 4. Stress test — reasons this could still fail

- **Scope creep is the single biggest risk.** Fault-injection frameworks invite ambition: more fault types, more RTOSes, more architectures, hardware-level fault injection (voltage glitching, clock manipulation) in addition to software-level. Resist all of it until one fault type, on one platform, catches one real bug. Every addition beyond that is a *second* milestone, not part of the first.
- **The idea only has value if it catches something real.** A framework that runs cleanly but never surfaces a genuine bug is a toy. The §3.3 proof case (reproducing a real, simplified driver bug) is not optional polish — it's the actual test of whether this is worth continuing.
- **Upstream acceptance is not guaranteed and shouldn't be depended on.** Large open-source projects move slowly and can be political about accepting new subsystems, even ones with a matching open issue. Building it as a standalone module first means the project doesn't depend on Zephyr's core team saying yes.
- **Zephyr-only is a starting wedge, not the whole embedded world.** FreeRTOS, bare-metal AUTOSAR automotive stacks, and other RTOSes are separate ecosystems with their own test infrastructure (or lack of it). A Zephyr-specific tool does not automatically generalize, and porting is a distinct, later effort — not a v0 concern, but also not something to oversell as "solved for embedded" broadly.
- **QEMU/native_sim can't simulate everything.** Software-triggered faults (a function returning an error, a corrupted buffer) are well within native_sim/QEMU's reach. Faults tied to real electrical/physical behavior (actual sensor noise, real thermal drift, genuine voltage instability) are not — that's a legitimate limit of this approach, not a gap to quietly paper over.
- **No validated business model.** This research brief establishes that the *problem* is real and *undersupplied* — it does not establish that anyone would pay for a polished version. The credible early win is credibility and a working tool, not revenue; monetization (if any) is a later question, not a v0 assumption.

---

## 5. Future problems to expect (roadmap-level, not immediate)

- **API stability as Zephyr evolves.** Zephyr's driver and subsystem APIs change between releases; a fault-injection layer that hooks into internals will need ongoing maintenance to track upstream changes, same as any out-of-tree module.
- **False positives/negatives as fault types grow.** A single "force this call to fail" primitive is simple to reason about. Adding more nuanced fault types (partial failures, timing-based faults, intermittent faults) increases the risk of tests that are flaky or misleading, and will need careful, deliberate design rather than ad-hoc addition.
- **Real-hardware fault injection is a different, harder project.** If this ever needs to move beyond native_sim/QEMU to real boards (via JTAG-based register/memory manipulation, for instance), that is a substantially larger undertaking with its own tooling, safety, and reproducibility problems — treat it as a distinct future decision, not an assumed next step.
- **Governance if it's ever proposed upstream.** If Zephyr's maintainers do want this in core, expect a design-review process, coding-standard requirements, and long-term maintenance commitments that a solo contributor needs to weigh deliberately rather than assume.
- **Maintaining honesty about what it proves.** Same caution as everywhere else in this project: this catches the specific fault classes it's told to inject. It is not a general correctness or safety certification, and should never be marketed as one.

---

## 6. Concrete next step

Don't start with the Zephyr module. Start smaller than that:

1. Pick one real, disclosed Zephyr driver bug from the missing-bounds-check family (§2.2).
2. Write the smallest possible C reproduction of that bug pattern.
3. Write, by hand, the one fault-injection primitive needed to trigger it in a test.
4. Confirm the test fails against the buggy version and passes against the fixed version.

If that loop works, it has earned a place in a real Ztest/Twister integration. If it doesn't reliably catch the planted bug, that's the signal to rethink the approach before investing further.

---

## Sources

- Zephyr issue #3559, "Fault injection framework for Zephyr" — https://github.com/zephyrproject-rtos/zephyr/issues/3559
- Zephyr project overview / contributor and board-support figures — https://docs.zephyrproject.org
- CVE-2026-1679 (Zephyr eswifi socket driver buffer overflow) — https://www.sentinelone.com/vulnerability-database/cve-2026-1679/
- CVE-2025-1673 (Zephyr DNS packet handling DoS) — https://www.sentinelone.com/vulnerability-database/cve-2025-1673/
- Zephyr Security Advisories / Vulnerabilities page — https://docs.zephyrproject.org/latest/security/vulnerabilities.html
- Zephyr GitHub Releases (recent driver bounds-check fixes) — https://github.com/zephyrproject-rtos/zephyr/releases
- Zephyr Test Framework (Ztest) documentation — https://docs.zephyrproject.org/latest/develop/test/ztest.html
- Zephyr Test Runner (Twister) documentation — https://docs.zephyrproject.org/latest/develop/twister/index.html
- Zephyr Modules (external/out-of-tree projects) documentation — https://docs.zephyrproject.org/latest/develop/modules.html
- "SoK: Where's the 'up'?!" — Cortex-M security research survey (RTOS driver bug classes) — https://arxiv.org/pdf/2401.15289
