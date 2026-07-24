/* fault_inject.h
 *
 * Deterministic fault-injection primitive.
 *
 * A "fault point" is a macro dropped at a call site that could plausibly
 * fail (or, as in this repro, at a call site that returns an
 * externally-controlled value a test wants to force to an otherwise
 * hard-to-reach case). When armed, the injected value is substituted
 * *instead of* making the real call -- the real call is not evaluated,
 * same as forcing a real driver call to fail without actually invoking
 * the driver. When fault injection is not compiled in, FI_POINT expands
 * to exactly the original call: no check, no branch, nothing. The macro
 * does not exist in that build.
 *
 * This is deliberately simpler than Linux's probability/interval/budget
 * model: deterministic arm/fire is easier to write a reliable, non-flaky
 * test against, and that's what v0 needs.
 */

#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#include <stdint.h>

#ifdef CONFIG_FAULT_INJECTION_MAX_POINTS
#define FI_MAX_POINTS CONFIG_FAULT_INJECTION_MAX_POINTS
#else
#define FI_MAX_POINTS 32 /* host-only build default; Zephyr sets this via Kconfig */
#endif

#ifdef CONFIG_FAULT_INJECTION

/* Increments the point's hit count every time it is evaluated (armed or
 * not), so a test can prove the point was actually reached. Returns 0 if
 * the point should pass through to the real call, or the armed nonzero
 * injected value otherwise. */
int fi_should_fail(uint32_t fault_id);

void fi_arm(uint32_t fault_id, int inject_value);
void fi_disarm(uint32_t fault_id);
void fi_reset_all(void);
uint32_t fi_hit_count(uint32_t fault_id);

/* Thread safety: under Zephyr, the registry is protected by a spinlock,
 * so fi_should_fail() is safe to call from any thread or ISR context
 * while another thread arms/disarms/reads it -- necessary because a
 * fault point may legitimately be hit from a driver ISR while a test
 * thread is (dis)arming it. In the host-only build (no Zephyr present)
 * there is no locking, since host tests here are single-threaded. */

/* GCC/Clang statement-expression: evaluates fi_should_fail() exactly
 * once, and evaluates real_call only when the point is not armed, so an
 * armed fault genuinely replaces the call rather than merely overriding
 * its result after the fact. */
#define FI_POINT(id, real_call)                    \
    ({ int _fi_rc = fi_should_fail(id);             \
       (_fi_rc != 0) ? _fi_rc : (real_call); })

#else

#define FI_POINT(id, real_call) (real_call)

#endif /* CONFIG_FAULT_INJECTION */

#endif /* FAULT_INJECT_H */
