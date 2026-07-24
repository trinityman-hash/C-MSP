/* test_eswifi_recv.c
 *
 * This file is compiled and linked twice against two different
 * implementations of eswifi_socket_recv() (see the Makefile):
 *   - test_recv_buggy  : linked against eswifi_repro_buggy.c
 *   - test_recv_fixed  : linked against eswifi_repro_fixed.c
 *
 * Same test code both times. Expected outcome:
 *   - test_recv_buggy crashes under AddressSanitizer (the overflow is
 *     real) -- that IS the failing result, not a test bug.
 *   - test_recv_fixed passes cleanly.
 *
 * No test framework dependency on purpose -- this is Step 1 of the
 * project (host-only proof), before any Zephyr/Ztest integration.
 */

#include "eswifi_repro.h"
#include "fault_inject.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                              \
        if (!(cond)) {                                                \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            g_failures++;                                             \
        } else {                                                      \
            printf("  ok: %s\n", msg);                                \
        }                                                              \
    } while (0)

/* Sanity check: with no fault armed, a normal small payload is received
 * correctly. This must pass identically on both the buggy and fixed
 * builds -- if it doesn't, the bug reproduction isn't faithful (a
 * bounds-check bug should only bite on the oversized case). */
static void test_normal_recv_no_fault(void)
{
    printf("test_normal_recv_no_fault:\n");
    fi_reset_all();

    struct eswifi_socket sock;
    memset(&sock, 0, sizeof(sock));
    struct eswifi_hw_mock hw = { .reported_len = 12 };
    uint8_t hw_data[12];
    for (int i = 0; i < 12; i++) {
        hw_data[i] = (uint8_t)(0xA0 + i);
    }

    int rc = eswifi_socket_recv(&sock, &hw, hw_data);

    CHECK(rc == ESWIFI_OK, "returns ESWIFI_OK for an in-bounds payload");
    CHECK(sock.rx_len == 12, "rx_len matches the reported length");
    CHECK(memcmp(sock.rx_buf, hw_data, 12) == 0, "payload copied correctly");
    CHECK(fi_hit_count(FI_ESWIFI_RECV_LEN) == 1,
          "fault point was reached exactly once");
}

/* The actual proof case: force the hardware-length call to report a
 * length larger than the destination buffer -- a condition that's hard
 * to hit with real hardware but is exactly what the CVE-2026-1679
 * pattern needs to be exercised. */
static void test_oversized_length_injected(void)
{
    printf("test_oversized_length_injected:\n");
    fi_reset_all();

    const int oversized = ESWIFI_RX_BUF_SIZE * 2; /* 64, buffer is 32 */
    fi_arm(FI_ESWIFI_RECV_LEN, oversized);

    struct eswifi_socket sock;
    memset(&sock, 0, sizeof(sock));
    /* hw.reported_len is deliberately left small/safe: the fault point
     * overrides it, proving the override -- not the mock hardware --
     * is what's producing the oversized value. */
    struct eswifi_hw_mock hw = { .reported_len = 12 };

    uint8_t *hw_data = malloc((size_t)oversized);
    CHECK(hw_data != NULL, "test setup: source buffer allocated");
    memset(hw_data, 0xEE, (size_t)oversized);

    int rc = eswifi_socket_recv(&sock, &hw, hw_data);

    CHECK(fi_hit_count(FI_ESWIFI_RECV_LEN) > 0,
          "fault point was actually reached -- proof this test proves something");
    CHECK(rc == ESWIFI_EMSGSIZE,
          "oversized length is rejected with ESWIFI_EMSGSIZE, not copied");
    CHECK(sock.rx_len == 0,
          "rx_len left untouched when the copy was rejected");

    free(hw_data);
}

int main(void)
{
#ifndef CONFIG_FAULT_INJECTION
#error "This test requires CONFIG_FAULT_INJECTION"
#endif
    test_normal_recv_no_fault();
    test_oversized_length_injected();

    if (g_failures > 0) {
        fprintf(stderr, "\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
