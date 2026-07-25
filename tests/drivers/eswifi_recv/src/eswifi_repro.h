/* SPDX-License-Identifier: Apache-2.0 */

/* eswifi_repro.h
 *
 * A small, simplified reconstruction of the bug pattern behind
 * CVE-2026-1679 (Zephyr eswifi socket offload driver buffer overflow):
 * a driver copies a payload into a fixed-size buffer without first
 * checking that the payload actually fits.
 *
 * Here the payload length is not attacker-supplied over a socket -- it's
 * a length reported by a hardware/firmware call (eswifi_hw_read_length),
 * which the driver trusts. In real hardware you cannot easily make the
 * device report a bogus oversized length on demand; that's exactly the
 * kind of hard-to-reach external condition a fault point exists to
 * manufacture in a test.
 *
 * Two implementations of the same function signature are provided in
 * separate translation units (eswifi_repro_buggy.c / eswifi_repro_fixed.c)
 * so the identical test file can be linked against either one.
 */

#ifndef ESWIFI_REPRO_H
#define ESWIFI_REPRO_H

#include <stddef.h>
#include <stdint.h>

#define ESWIFI_RX_BUF_SIZE 32

#define ESWIFI_OK        0
#define ESWIFI_EMSGSIZE  (-90) /* mirrors POSIX/Zephyr -EMSGSIZE */

/* Fault point id for the hardware length-read call. */
#define FI_ESWIFI_RECV_LEN 1u

/* Stand-in for the eswifi device handle / hardware state. reported_len
 * is what the "real" hardware call would return when not fault-injected. */
struct eswifi_hw_mock {
    int reported_len;
};

struct eswifi_socket {
    uint8_t rx_buf[ESWIFI_RX_BUF_SIZE];
    size_t rx_len;
};

/* Returns hw->reported_len. Represents the real, normally-trustworthy
 * hardware call that FI_POINT wraps in the implementations below. */
int eswifi_hw_read_length(struct eswifi_hw_mock *hw);

/* Reads the reported length (via the FI_POINT-wrapped hw call) and
 * copies that many bytes from hw_data into sock->rx_buf.
 *
 * Buggy implementation: copies unconditionally.
 * Fixed implementation: rejects with ESWIFI_EMSGSIZE if the reported
 * length exceeds the buffer, and does not touch rx_buf/rx_len in that
 * case.
 */
int eswifi_socket_recv(struct eswifi_socket *sock, struct eswifi_hw_mock *hw,
                        const uint8_t *hw_data);

#endif /* ESWIFI_REPRO_H */
