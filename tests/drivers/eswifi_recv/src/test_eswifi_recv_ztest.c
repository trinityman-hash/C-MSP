/* test_eswifi_recv.c (Ztest)
 *
 * Zephyr/Ztest port of the same proof case already verified on the host
 * (see tests/drivers/eswifi_recv/src/... and the top-level README for
 * the host-only run). Same assertions, same fault-injection primitive,
 * now built and run as a real Zephyr test image on native_sim via
 * `west twister`.
 *
 * This file is compiled against either eswifi_repro_buggy.c or
 * eswifi_repro_fixed.c -- selected by the ESWIFI_RECV_VARIANT CMake
 * variable set in CMakeLists.txt, driven by testcase.yaml's
 * per-test `extra_args`. See testcase.yaml for the two entries:
 *   - drivers.eswifi_recv.fixed  (expected: PASS)
 *   - drivers.eswifi_recv.buggy  (expected: the image crashes under
 *     CONFIG_ASAN before Ztest can report a result -- Twister sees
 *     that as a failed test, which is the correct, intended outcome)
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "eswifi_repro.h"
#include "fault_inject.h"

ZTEST_SUITE(eswifi_recv_fault_injection, NULL, NULL, NULL, NULL, NULL);

/* Sanity check: with no fault armed, a normal small payload is received
 * correctly, identically on both the buggy and fixed builds. If this
 * fails, the bug reproduction isn't faithful -- a missing-bounds-check
 * bug should only bite on the oversized case, not normal traffic. */
ZTEST(eswifi_recv_fault_injection, test_normal_recv_no_fault)
{
	fi_reset_all();

	struct eswifi_socket sock;
	memset(&sock, 0, sizeof(sock));
	struct eswifi_hw_mock hw = { .reported_len = 12 };
	uint8_t hw_data[12];

	for (int i = 0; i < 12; i++) {
		hw_data[i] = (uint8_t)(0xA0 + i);
	}

	int rc = eswifi_socket_recv(&sock, &hw, hw_data);

	zassert_equal(rc, ESWIFI_OK, "expected ESWIFI_OK for an in-bounds payload, got %d", rc);
	zassert_equal(sock.rx_len, 12, "rx_len should match the reported length");
	zassert_mem_equal(sock.rx_buf, hw_data, 12, "payload should be copied correctly");
	zassert_equal(fi_hit_count(FI_ESWIFI_RECV_LEN), 1,
		      "fault point should have been reached exactly once");
}

/* The proof case: force the hardware-length call to report a length
 * larger than the destination buffer -- a condition real hardware
 * won't easily produce on demand, and exactly what CVE-2026-1679's
 * pattern needs to be exercised deterministically in a test. */
ZTEST(eswifi_recv_fault_injection, test_oversized_length_injected)
{
	fi_reset_all();

	const int oversized = ESWIFI_RX_BUF_SIZE * 2; /* 64, buffer is 32 */

	fi_arm(FI_ESWIFI_RECV_LEN, oversized);

	struct eswifi_socket sock;
	memset(&sock, 0, sizeof(sock));
	/* hw.reported_len is deliberately left small/safe: the fault point
	 * overrides it, proving the override -- not the mock hardware --
	 * produced the oversized value. */
	struct eswifi_hw_mock hw = { .reported_len = 12 };

	uint8_t hw_data[128];

	zassert_true(oversized <= (int)sizeof(hw_data), "test setup: source buffer too small");
	memset(hw_data, 0xEE, (size_t)oversized);

	int rc = eswifi_socket_recv(&sock, &hw, hw_data);

	zassert_true(fi_hit_count(FI_ESWIFI_RECV_LEN) > 0,
		     "fault point was never reached -- this test proves nothing");
	zassert_equal(rc, ESWIFI_EMSGSIZE,
		      "oversized length should be rejected with ESWIFI_EMSGSIZE, got %d", rc);
	zassert_equal(sock.rx_len, 0, "rx_len should be untouched when the copy is rejected");
}
