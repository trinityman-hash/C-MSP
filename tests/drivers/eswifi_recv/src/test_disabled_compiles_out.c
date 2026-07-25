/* SPDX-License-Identifier: Apache-2.0 */

/* test_disabled_compiles_out.c
 *
 * Compiled WITHOUT -DCONFIG_FAULT_INJECTION, and linked against
 * eswifi_repro_fixed.c only -- fault_inject.c is not part of this
 * build at all. This proves FI_POINT(id, real_call) truly expands to
 * exactly (real_call): the binary has no fault-injection symbols in it
 * and normal behavior is unaffected. See §3.4 of the brief for why
 * "compiled out" (not just "disabled") matters for release builds.
 */

#include "eswifi_repro.h"
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_FAULT_INJECTION
#error "This test must be built with CONFIG_FAULT_INJECTION undefined"
#endif

int main(void)
{
    struct eswifi_socket sock;
    memset(&sock, 0, sizeof(sock));
    struct eswifi_hw_mock hw = { .reported_len = 12 };
    uint8_t hw_data[12];
    for (int i = 0; i < 12; i++) {
        hw_data[i] = (uint8_t)(0xA0 + i);
    }

    int rc = eswifi_socket_recv(&sock, &hw, hw_data);

    if (rc != ESWIFI_OK || sock.rx_len != 12 ||
        memcmp(sock.rx_buf, hw_data, 12) != 0) {
        fprintf(stderr, "FAIL: normal recv broke with fault injection compiled out\n");
        return 1;
    }
    printf("  ok: normal recv works identically with fault injection compiled out\n");
    printf("all checks passed\n");
    return 0;
}
