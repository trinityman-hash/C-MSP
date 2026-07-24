# Step 2 verification log

Everything below was actually run against a real Zephyr checkout
(`main`, version 4.4.99 at the time), `west`, and `native_sim` -- not
hand-written or assumed. Commands are reproducible as written, given a
Zephyr workspace with this module reachable via `ZEPHYR_EXTRA_MODULES`.

Toolchain used: `ZEPHYR_TOOLCHAIN_VARIANT=host` (native_sim builds with
the host's own gcc -- no Zephyr SDK / cross-compiler needed for this
board). Requires `cmake`, `ninja`, `device-tree-compiler`, and 32-bit
multilib support (`gcc-multilib`) since native_sim defaults to a 32-bit
build on an x86_64 host.

## 1. The real regression test: Twister, fixed variant

```
west twister -p native_sim -T tests/drivers/eswifi_recv \
  -x=ZEPHYR_EXTRA_MODULES=/path/to/this/repo
```

Result: **PASSED**, both test cases.

```
*** Booting Zephyr OS build 06bcf1e225de ***
Running TESTSUITE eswifi_recv_fault_injection
===================================================================
START - test_normal_recv_no_fault
 PASS - test_normal_recv_no_fault in 0.000 seconds
===================================================================
START - test_oversized_length_injected
 PASS - test_oversized_length_injected in 0.000 seconds
===================================================================
TESTSUITE eswifi_recv_fault_injection succeeded

SUITE PASS - 100.00% [eswifi_recv_fault_injection]: pass = 2, fail = 0, skip = 0, total = 2
...
PROJECT EXECUTION SUCCESSFUL
```

## 2. The release guard actually blocks misuse

Building an unrelated sample (`samples/hello_world`) with
`-DCONFIG_FAULT_INJECTION=y` and no Ztest in the image:

```
west build -p always -b native_sim zephyr/samples/hello_world -- \
  -DZEPHYR_EXTRA_MODULES=/path/to/this/repo -DCONFIG_FAULT_INJECTION=y
```

Result: **hard CMake failure**, not a silent pass-through:

```
CMake Error at .../CMakeLists.txt:8 (message):
  CONFIG_FAULT_INJECTION=y outside a Ztest build.  This module's design
  requires fault injection to be physically absent from non-test images...
-- Configuring incomplete, errors occurred!
```

This is the concrete enforcement of the promise in `Kconfig` and
`docs/brief.md` §3.4/§3.5 -- a release image cannot ship this module's
hooks by accident.

## 3. The buggy variant: proof the bug reproduction is real, under Zephyr

Deliberately **not** a permanent Twister suite entry -- see the comment
at the top of `tests/drivers/eswifi_recv/testcase.yaml` for why. Built
and run once, by hand, exactly as you would `git stash` a fix to confirm
a test actually catches the regression it claims to:

```
west build -p always -b native_sim tests/drivers/eswifi_recv -- \
  -DZEPHYR_EXTRA_MODULES=/path/to/this/repo \
  -DCONFIG_FAULT_INJECTION=y -DCONFIG_FAULT_INJECTION_MAX_POINTS=8 \
  -DCONFIG_ASAN=y -DCONFIG_UBSAN=y -DESWIFI_RECV_VARIANT=buggy

./build/zephyr/zephyr.exe
```

Result: the first test passes, and the second **crashes with a real
AddressSanitizer stack-buffer-overflow report**, pointing at the exact
line with the missing bounds check:

```
==1991==ERROR: AddressSanitizer: stack-buffer-overflow on address 0xef7f6054
WRITE of size 64 at 0xef7f6054 thread T5
    #0 ... in memcpy
    #1 ... in memcpy /usr/include/bits/string_fortified.h:29
    #2 ... in eswifi_socket_recv .../eswifi_repro_buggy.c:23
    #3 ... in eswifi_recv_fault_injection_test_oversized_length_injected
           .../test_eswifi_recv_ztest.c:76
    ...
SUMMARY: AddressSanitizer: stack-buffer-overflow ... in memcpy
==1991==ABORTING
```

Note the stack trace runs through real Zephyr kernel machinery
(`z_impl_k_thread_create`, `run_test`, `posix_arch_thread_entry`,
`nct_thread_starter`) -- this is the actual RTOS scheduler and Ztest
runner catching the fault-injected condition, not a simulated result.

## What this establishes

Both proof requirements from `docs/brief.md` §6 are now met twice: once
on a bare host build (see the top-level README), and once through the
real Zephyr module/Kconfig/CMake/Ztest/Twister integration this was
ultimately meant to prove out. The fault-injection primitive, the bug
reproduction, and the release-safety guard all behave exactly as
designed under the actual target toolchain, not a stand-in for it.

## What wasn't (yet) verified here

- Real hardware / a physical board -- this is `native_sim` only.
- QEMU-emulated targets.
- Any board other than `native_sim`.
- CI wiring (a GitHub Actions workflow calling `west twister`) -- not
  built, since it can't be verified from this environment without
  actually running it in GitHub's CI, and an unverified CI config is
  worse than no CI config.
