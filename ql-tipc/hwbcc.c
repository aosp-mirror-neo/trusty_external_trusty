/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <trusty/hwbcc.h>
#include <trusty/trusty_ipc.h>
#include <trusty/util.h>

#include <uapi/uapi/err.h>

static struct trusty_ipc_chan hwbcc_chan;
static bool initialized;

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

int hwbcc_tipc_init(struct trusty_ipc_dev* dev) {
    trusty_assert(dev);
    trusty_ipc_chan_init(&hwbcc_chan, dev);

    trusty_debug("In hwbcc_tipc_init: connecting to hwbcc service.\n");
    int rc = trusty_ipc_connect(&hwbcc_chan, HWBCC_PORT, true /*wait*/);
    if (rc < 0) {
        trusty_error("In hwbcc_tipc_init:: failed (%d) to connect to '%s'.\n",
                     rc, HWBCC_PORT);
        return rc;
    }
    initialized = true;
    return TRUSTY_ERR_NONE;
}

void hwbcc_tipc_shutdown(void) {
    if (!initialized) {
        return;
    }
    trusty_ipc_close(&hwbcc_chan);
    initialized = false;
}

static int send_header_only_request(struct hwbcc_req_hdr* hdr,
                                    size_t hdr_size) {
    int num_iovec = 1;
    struct trusty_ipc_iovec req_iov = {.base = hdr, .len = hdr_size};
    return trusty_ipc_send(&hwbcc_chan, &req_iov, num_iovec, true);
}

static int read_response_with_data(struct hwbcc_req_hdr* hdr,
                                   uint8_t* buf,
                                   size_t buf_size,
                                   size_t* out_size) {
    struct hwbcc_resp_hdr resp_hdr = {};

    trusty_assert(buf);
    trusty_assert(out_size);

    int num_iovec = 2;
    struct trusty_ipc_iovec resp_iovecs[2] = {
            {.base = &resp_hdr, .len = sizeof(resp_hdr)},
            {.base = buf, .len = buf_size},
    };

    int rc = trusty_ipc_recv(&hwbcc_chan, resp_iovecs, num_iovec, true);
    if (rc < 0) {
        trusty_error("Failure on receiving response: %d\n", rc);
        return rc;
    }

    if ((size_t)rc < sizeof(resp_hdr)) {
        trusty_error("Invalid response size (%d).\n", rc);
        return TRUSTY_ERR_GENERIC;
    }

    if (resp_hdr.cmd != (hdr->cmd | HWBCC_CMD_RESP_BIT)) {
        trusty_error("Unknown response cmd: %x\n", resp_hdr.cmd);
        return TRUSTY_ERR_GENERIC;
    }

    if (resp_hdr.status != NO_ERROR) {
        trusty_error("Status (%d) is not SUCCESS.\n", resp_hdr.status);
        return TRUSTY_ERR_GENERIC;
    }

    if (resp_hdr.payload_size != (size_t)rc - sizeof(resp_hdr)) {
        trusty_error("Invalid payload size: %d.", resp_hdr.payload_size);
        return TRUSTY_ERR_GENERIC;
    }

    *out_size = resp_hdr.payload_size;
    return rc;
}

static int read_header_only_response(struct hwbcc_req_hdr* hdr) {
    struct hwbcc_resp_hdr resp_hdr = {};

    struct trusty_ipc_iovec resp_iovec = {.base = &resp_hdr,
                                          .len = sizeof(resp_hdr)};

    int rc = trusty_ipc_recv(&hwbcc_chan, &resp_iovec, 1, true);
    if (rc < 0) {
        trusty_error("Failure on receiving response: %d\n", rc);
        return rc;
    }

    if ((size_t)rc < sizeof(resp_hdr)) {
        trusty_error("Invalid response size (%d).\n", rc);
        return TRUSTY_ERR_GENERIC;
    }

    if (resp_hdr.cmd != (hdr->cmd | HWBCC_CMD_RESP_BIT)) {
        trusty_error("Unknown response cmd: %x\n", resp_hdr.cmd);
        return TRUSTY_ERR_GENERIC;
    }

    if (resp_hdr.status != NO_ERROR) {
        trusty_error("Status (%d) is not SUCCESS.\n", resp_hdr.status);
        return TRUSTY_ERR_GENERIC;
    }

    return rc;
}

int hwbcc_get_dice_artifacts(uint64_t context,
                             uint8_t* dice_artifacts,
                             size_t dice_artifacts_buf_size,
                             size_t* dice_artifacts_size) {
    trusty_assert(dice_artifacts);
    trusty_assert(dice_artifacts_size);

    struct hwbcc_req_hdr hdr;
    hdr.cmd = HWBCC_CMD_GET_DICE_ARTIFACTS;
    hdr.context = context;

    int rc = send_header_only_request(&hdr, sizeof(hdr));

    if (rc < 0) {
        trusty_error(
                "In hwbcc_get_dice_artifacts: failed (%d) to send request to HWBCC.",
                rc);
        return rc;
    }

    rc = read_response_with_data(&hdr, dice_artifacts, dice_artifacts_buf_size,
                                 dice_artifacts_size);

    if (rc < 0) {
        trusty_error(
                "In hwbcc_get_dice_artifacts: failed (%d) to read the response.",
                rc);
        return rc;
    }

    return TRUSTY_ERR_NONE;
}

int hwbcc_ns_deprivilege(void) {
    struct hwbcc_req_hdr hdr = {.cmd = HWBCC_CMD_NS_DEPRIVILEGE};
    int rc = send_header_only_request(&hdr, sizeof(hdr));

    if (rc < 0) {
        trusty_error(
                "In hwbcc_deprivilege: failed (%d) to send request to HWBCC.",
                rc);
        return rc;
    }

    rc = read_header_only_response(&hdr);

    if (rc < 0) {
        trusty_error("In hwbcc_deprivilege: failed (%d) to read the response.",
                     rc);
        return rc;
    }

    return TRUSTY_ERR_NONE;
}
