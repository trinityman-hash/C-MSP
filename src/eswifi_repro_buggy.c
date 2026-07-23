/* eswifi_repro_buggy.c
 *
 * Reproduces the CVE-2026-1679 pattern: the reported length is copied
 * into rx_buf with no check that it fits. This is the "before" version
 * -- it should be caught red-handed by the fault-injection test.
 */

#include "eswifi_repro.h"
#include "fault_inject.h"
#include <string.h>

int eswifi_hw_read_length(struct eswifi_hw_mock *hw)
{
    return hw->reported_len;
}

int eswifi_socket_recv(struct eswifi_socket *sock, struct eswifi_hw_mock *hw,
                        const uint8_t *hw_data)
{
    int len = FI_POINT(FI_ESWIFI_RECV_LEN, eswifi_hw_read_length(hw));

    /* BUG: no check that len fits in sock->rx_buf before copying. */
    memcpy(sock->rx_buf, hw_data, (size_t)len);
    sock->rx_len = (size_t)len;

    return ESWIFI_OK;
}
