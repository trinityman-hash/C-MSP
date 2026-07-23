/* eswifi_repro_fixed.c
 *
 * Same function signature as eswifi_repro_buggy.c, but validates the
 * reported length against the destination buffer before copying. This
 * is the "after" version -- the fault-injection test should pass
 * cleanly against this file.
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

    if (len < 0 || (size_t)len > sizeof(sock->rx_buf)) {
        return ESWIFI_EMSGSIZE;
    }

    memcpy(sock->rx_buf, hw_data, (size_t)len);
    sock->rx_len = (size_t)len;

    return ESWIFI_OK;
}
