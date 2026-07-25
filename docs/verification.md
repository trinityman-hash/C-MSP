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

## 4. CI workflow (`.github/workflows/ci.yml`): two bugs found and fixed by local reproduction

`ci.yml` was added directly on GitHub, not by Claude, and its
`zephyr-twister` job had not been run anywhere before this. Rather than
trust it, its exact recipe was reproduced by hand in a scratch
workspace, same as everything else in this log.

**Bug 1 -- missing project-filter.** The job's
`west config manifest.group-filter -- -hal,-babblesim,-bootloader,-tools`
excludes `hal_nordic`, but `nrf_hw_models` is an *ungrouped* project, so
the group-filter doesn't touch it -- and `nrf_hw_models` hard-depends on
`hal_nordic`. Running the job's exact `west init`/`west update` recipe
as written, then `west build -b native_sim samples/hello_world`,
reproduced:

```
CMake Error at .../zephyr_module.cmake:73 (message):
  Unmet or cyclic dependencies in modules:

  .../modules/bsim_hw_models/nrf_hw_models depends on:
  ['hal_nordic']
```

Fix: add `west config manifest.project-filter -- -nrf_hw_models`
alongside the group-filter. Re-running `west update` then the same
`west build` afterward: **succeeded**, `zephyr.exe` built and ran
("Hello World! native_sim/native").

**Bug 2 -- insufficient pip requirements.** The job's
`pip install -r zephyr/scripts/requirements-base.txt` is not enough to
run `west twister` at all -- `twisterlib` imports `natsort` at import
time, which lives in `requirements-run-test.txt`, not
`requirements-base.txt`. Reproduced:

```
File ".../twisterlib/hardwaremap.py", line 20, in <module>
    from natsort import natsorted
ModuleNotFoundError: No module named 'natsort'
```

Fix: also install `requirements-build-test.txt` (covers `colorama`,
`ply`, also imported by twister) and `requirements-run-test.txt`
(covers `natsort`, `tabulate`). `requirements-compliance.txt` and
`requirements-extras.txt` were checked and are not needed -- nothing in
them is imported by twister or this module's build.

**Full job re-verified end-to-end after both fixes**, using the real
module (not just `hello_world`):

```
west twister -p native_sim \
  -T /path/to/C-MSP/tests/drivers/eswifi_recv \
  -x=ZEPHYR_EXTRA_MODULES=/path/to/C-MSP \
  --inline-logs -v
```

```
INFO    - 1/1 native_sim/native         drivers.eswifi_recv.fixed          PASSED (native 0.069s <host/gnu>)
INFO    - 1 of 1 executed test configurations passed (100.00%), 0 built (not run), 0 failed, 0 errored
INFO    - 2 of 2 executed test cases passed (100.00%) on 1 out of total 1640 platforms (0.06%).
```

The job's release-safety guard-check step (same CMake-error assertion
as verification §2 above, automated) was also re-run against the fixed
workspace and correctly rejected the build with the expected message.

The `host-test` job (`make test`) was re-checked too, unchanged and
still green: fixed passes, disabled passes, buggy crashes under ASan as
expected.

**Not yet done:** pushing the fixed `ci.yml` back to the repo. The
connected GitHub App token doesn't carry the `workflows` permission, so
writes to any path under `.github/workflows/` are rejected by GitHub's
API regardless of branch (`403: refusing to allow a GitHub App to
create or update workflow ... without workflows permission`). The
verified diff is provided separately for the user to apply. Actually
triggering the workflow on GitHub's own runners (the one thing no local
reproduction can substitute for) is still outstanding either way.

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
- The actual GitHub Actions run of `ci.yml` on GitHub's own runners --
  see §4. The workflow's recipe has now been reproduced and fixed
  locally, but a local reproduction of the same commands is still not
  the same signal as a real run on GitHub's infrastructure.
