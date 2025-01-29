/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <trusty/arm_ffa.h>
#include <trusty/sm_err.h>
#include <trusty/smc.h>
#include <trusty/smcall.h>
#include <trusty/trusty_dev.h>
#include <trusty/trusty_mem.h>
#include <trusty/util.h>

struct trusty_dev;

#define LOCAL_LOG 0

#define TRUSTY_FFA_RXTX_PAGE_COUNT 1

/*
 * Select RXTX map smc variant based on register size. Note that the FF-A spec
 * does not support passing a 64 bit paddr from a 32 bit client, so the
 * allocated buffer has to be below 4G if this is called from 32 bit code.
 */
#define SMC_FCZ_FFA_RXTX_MAP \
    ((sizeof(unsigned long) <= 4) ? SMC_FC_FFA_RXTX_MAP : SMC_FC64_FFA_RXTX_MAP)

__attribute__((weak)) unsigned long trusty_get_cpu_num(void) {
    trusty_fatal(
            "%s: weak default called, "
            "might return the wrong CPU number\n",
            __func__);
}

static int trusty_ffa_send_direct_msg(struct trusty_dev* dev,
                                      unsigned long msg,
                                      unsigned long a0,
                                      unsigned long a1,
                                      unsigned long a2,
                                      unsigned long a3) {
    struct smc_ret8 smc_ret;
    uint32_t src_dst;

    src_dst = FFA_PACK_SRC_DST(dev->ffa_local_id, dev->ffa_remote_id);

    /* 32bit FFA call for direct message request/response */
    smc_ret = smc8(SMC_FC_FFA_MSG_SEND_DIRECT_REQ, src_dst, 0, msg, a0, a1, a2,
                   a3);

    switch ((uint32_t)smc_ret.r0) {
    case SMC_FC_FFA_INTERRUPT:
        /*
         * Trusty supports managed exit so it should return from an interrupt
         * cleanly. We should never see an FFA_INTERRUPT here.
         */
        trusty_error("unexpected FFA_INTERRUPT(0x%x, 0x%x)\n",
                     (uint32_t)smc_ret.r1, (uint32_t)smc_ret.r2);
        return SM_ERR_INTERNAL_FAILURE;

    case SMC_FC_FFA_ERROR:
        trusty_error("unexpected FFA_ERROR(0x%x, 0x%x)\n", (uint32_t)smc_ret.r1,
                     (uint32_t)smc_ret.r2);
        return SM_ERR_INTERNAL_FAILURE;

    case SMC_FC_FFA_MSG_SEND_DIRECT_RESP:
        return smc_ret.r3;

    default:
        trusty_error("unknown FF-A result: 0x%x\n", (uint32_t)smc_ret.r0);
        return SM_ERR_INTERNAL_FAILURE;
    }
}

static int trusty_ffa_run(struct trusty_dev* dev) {
    struct smc_ret8 smc_ret;
    uint32_t dst_cpu;

    dst_cpu = FFA_PACK_DST_CPU(dev->ffa_remote_id,
                               (uint16_t)trusty_get_cpu_num());

    smc_ret = smc8(SMC_FC_FFA_RUN, dst_cpu, 0, 0, 0, 0, 0, 0);

    switch ((uint32_t)smc_ret.r0) {
    case SMC_FC_FFA_INTERRUPT:
        /*
         * Trusty supports managed exit so it should return from an interrupt
         * cleanly. We should never see an FFA_INTERRUPT here.
         */
        trusty_error("unexpected FFA_INTERRUPT(0x%x, 0x%x)\n",
                     (uint32_t)smc_ret.r1, (uint32_t)smc_ret.r2);
        return SM_ERR_INTERNAL_FAILURE;

    case SMC_FC_FFA_ERROR:
        trusty_error("unexpected FFA_ERROR(0x%x, 0x%x)\n", (uint32_t)smc_ret.r1,
                     (uint32_t)smc_ret.r2);
        return SM_ERR_INTERNAL_FAILURE;

    case SMC_FC_FFA_SUCCESS:
    case SMC_FC_FFA_MSG_WAIT:
        return trusty_ffa_send_direct_msg(dev, TRUSTY_FFA_MSG_IS_IDLE, 0, 0, 0,
                                          0);

    default:
        trusty_error("unknown FF-A result: 0x%x\n", (uint32_t)smc_ret.r0);
        return SM_ERR_INTERNAL_FAILURE;
    }
}

static uint32_t trusty_ffa_call(struct trusty_dev* dev,
                                unsigned long fid,
                                unsigned long a0,
                                unsigned long a1,
                                unsigned long a2) {
    int ret;

    if (SMC_IS_FASTCALL(fid)) {
        return trusty_ffa_send_direct_msg(dev, TRUSTY_FFA_MSG_RUN_FASTCALL, fid,
                                          a0, a1, a2);
    }

    if (fid == SMC_SC_NOP) {
        if (a0) {
            ret = trusty_ffa_send_direct_msg(dev, TRUSTY_FFA_MSG_RUN_NOPCALL,
                                             a0, a1, a2, 0);
            if (ret) {
                return ret;
            }
        }

        ret = trusty_ffa_run(dev);
        switch (ret) {
        case 1:
            /* Trusty is idle, we are done */
            return SM_ERR_NOP_DONE;
        case 0:
            /* Trusty has more work to do, keep running it */
            return SM_ERR_NOP_INTERRUPTED;
        default:
            return ret;
        }
    }

    if (fid != SMC_SC_RESTART_LAST) {
        ret = trusty_ffa_send_direct_msg(dev, TRUSTY_FFA_MSG_QUEUE_STDCALL, fid,
                                         a0, a1, a2);
        if (ret) {
            return ret;
        }

        /*
         * Trusty sends us the IPI after queuing a new stdcall.
         * Unlike Linux which has a handler and enqueues a NOP in that case,
         * we do not handle that interrupt so we need to call FFA_RUN manually.
         */
    }

    ret = trusty_ffa_run(dev);
    switch (ret) {
    case 1:
        /* Trusty is actually idle now, get the stdcall return value */
        return trusty_ffa_send_direct_msg(dev, TRUSTY_FFA_MSG_GET_STDCALL_RET,
                                          0, 0, 0, 0);
    case 0:
        /* Keep running Trusty without executing WFI until it becomes idle */
        return SM_ERR_INTERRUPTED;
    default:
        return ret;
    }
}

static int32_t trusty_call32(struct trusty_dev* dev,
                             uint32_t smcnr,
                             uint32_t a0,
                             uint32_t a1,
                             uint32_t a2) {
    trusty_assert(dev);

    if (dev->ffa_supports_direct_recv) {
        return trusty_ffa_call(dev, smcnr, a0, a1, a2);
    } else {
        return smc(smcnr, a0, a1, a2);
    }
}

int32_t trusty_fast_call32(struct trusty_dev* dev,
                           uint32_t smcnr,
                           uint32_t a0,
                           uint32_t a1,
                           uint32_t a2) {
    trusty_assert(dev);
    trusty_assert(SMC_IS_FASTCALL(smcnr));

    return trusty_call32(dev, smcnr, a0, a1, a2);
}

static unsigned long trusty_std_call_inner(struct trusty_dev* dev,
                                           unsigned long smcnr,
                                           unsigned long a0,
                                           unsigned long a1,
                                           unsigned long a2) {
    unsigned long ret;
    int retry = 5;

    trusty_debug("%s(0x%lx 0x%lx 0x%lx 0x%lx)\n", __func__, smcnr, a0, a1, a2);

    while (true) {
        ret = trusty_call32(dev, smcnr, a0, a1, a2);
        while ((int32_t)ret == SM_ERR_FIQ_INTERRUPTED)
            ret = trusty_call32(dev, SMC_SC_RESTART_FIQ, 0, 0, 0);
        if ((int)ret != SM_ERR_BUSY || !retry)
            break;

        trusty_debug("%s(0x%lx 0x%lx 0x%lx 0x%lx) returned busy, retry\n",
                     __func__, smcnr, a0, a1, a2);

        retry--;
    }

    return ret;
}

static unsigned long trusty_std_call_helper(struct trusty_dev* dev,
                                            unsigned long smcnr,
                                            unsigned long a0,
                                            unsigned long a1,
                                            unsigned long a2) {
    unsigned long ret;
    unsigned long irq_state;

    while (true) {
        trusty_local_irq_disable(&irq_state);
        ret = trusty_std_call_inner(dev, smcnr, a0, a1, a2);
        trusty_local_irq_restore(&irq_state);

        if ((int)ret != SM_ERR_BUSY)
            break;

        trusty_idle(dev, false);
    }

    return ret;
}

static int32_t trusty_std_call32(struct trusty_dev* dev,
                                 uint32_t smcnr,
                                 uint32_t a0,
                                 uint32_t a1,
                                 uint32_t a2) {
    int ret;

    trusty_assert(dev);
    trusty_assert(!SMC_IS_FASTCALL(smcnr));

    if (smcnr != SMC_SC_NOP) {
        trusty_lock(dev);
    }

    trusty_debug("%s(0x%x 0x%x 0x%x 0x%x) started\n", __func__, smcnr, a0, a1,
                 a2);

    ret = trusty_std_call_helper(dev, smcnr, a0, a1, a2);
    while (ret == SM_ERR_INTERRUPTED || ret == SM_ERR_CPU_IDLE) {
        trusty_debug("%s(0x%x 0x%x 0x%x 0x%x) interrupted\n", __func__, smcnr,
                     a0, a1, a2);
        if (ret == SM_ERR_CPU_IDLE) {
            trusty_idle(dev, false);
        }
        ret = trusty_std_call_helper(dev, SMC_SC_RESTART_LAST, 0, 0, 0);
    }

    trusty_debug("%s(0x%x 0x%x 0x%x 0x%x) returned 0x%x\n", __func__, smcnr, a0,
                 a1, a2, ret);

    if (smcnr != SMC_SC_NOP) {
        trusty_unlock(dev);
    }

    return ret;
}

static int trusty_call32_mem_buf_id(struct trusty_dev* dev,
                                    uint32_t smcnr,
                                    trusty_shared_mem_id_t buf_id,
                                    uint32_t size) {
    trusty_assert(dev);

    if (SMC_IS_FASTCALL(smcnr)) {
        return trusty_fast_call32(dev, smcnr, (uint32_t)buf_id,
                                  (uint32_t)(buf_id >> 32), size);
    } else {
        return trusty_std_call32(dev, smcnr, (uint32_t)buf_id,
                                 (uint32_t)(buf_id >> 32), size);
    }
}

int trusty_dev_init_ipc(struct trusty_dev* dev,
                        trusty_shared_mem_id_t buf_id,
                        uint32_t buf_size) {
    return trusty_call32_mem_buf_id(dev, SMC_SC_TRUSTY_IPC_CREATE_QL_DEV,
                                    buf_id, buf_size);
}

int trusty_dev_exec_ipc(struct trusty_dev* dev,
                        trusty_shared_mem_id_t buf_id,
                        uint32_t buf_size) {
    return trusty_call32_mem_buf_id(dev, SMC_SC_TRUSTY_IPC_HANDLE_QL_DEV_CMD,
                                    buf_id, buf_size);
}

int trusty_dev_exec_fc_ipc(struct trusty_dev* dev,
                           trusty_shared_mem_id_t buf_id,
                           uint32_t buf_size) {
    return trusty_call32_mem_buf_id(dev, SMC_FC_HANDLE_QL_TIPC_DEV_CMD, buf_id,
                                    buf_size);
}

int trusty_dev_shutdown_ipc(struct trusty_dev* dev,
                            trusty_shared_mem_id_t buf_id,
                            uint32_t buf_size) {
    return trusty_call32_mem_buf_id(dev, SMC_SC_TRUSTY_IPC_SHUTDOWN_QL_DEV,
                                    buf_id, buf_size);
}

static int trusty_init_api_version(struct trusty_dev* dev) {
    uint32_t api_version;

    api_version = trusty_fast_call32(dev, SMC_FC_API_VERSION,
                                     TRUSTY_API_VERSION_CURRENT, 0, 0);
    if (api_version == SM_ERR_UNDEFINED_SMC)
        api_version = 0;

    if (api_version > TRUSTY_API_VERSION_CURRENT) {
        trusty_error("unsupported trusty api version %u > %u\n", api_version,
                     TRUSTY_API_VERSION_CURRENT);
        return -1;
    }

    trusty_info("selected trusty api version: %u (requested %u)\n", api_version,
                TRUSTY_API_VERSION_CURRENT);

    dev->api_version = api_version;

    return 0;
}

/*
 * Trusty UUID: RFC-4122 compliant UUID version 4
 * 40ee25f0-a2bc-304c-8c4ca173c57d8af1
 * Trusty UUID used for partition info get call
 */
static const uint32_t trusty_uuid0_4[4] = {0xf025ee40, 0x4c30bca2, 0x73a14c8c,
                                           0xf18a7dc5};

static bool trusty_dev_ffa_init(struct trusty_dev* dev) {
    int ret;
    struct smc_ret8 smc_ret;
    struct ns_mem_page_info tx_pinfo;
    struct ns_mem_page_info rx_pinfo;
    trusty_assert(dev);

    dev->ffa_tx = NULL;
    dev->ffa_supports_direct_recv = false;

    /* Get supported FF-A version and check if it is compatible */
    smc_ret = smc8(SMC_FC_FFA_VERSION, FFA_CURRENT_VERSION, 0, 0, 0, 0, 0, 0);
    if (FFA_VERSION_TO_MAJOR(smc_ret.r0) != FFA_CURRENT_VERSION_MAJOR) {
        /* TODO: support more than one (minor) version. */
        trusty_error("%s: unsupported FF-A version 0x%lx, expected 0x%x\n",
                     __func__, smc_ret.r0, FFA_CURRENT_VERSION);
        goto err_version;
    }

    /* Check that SMC_FC_FFA_MEM_SHARE is implemented */
    smc_ret = smc8(SMC_FC_FFA_FEATURES, SMC_FC_FFA_MEM_SHARE, 0, 0, 0, 0, 0, 0);
    if (smc_ret.r0 != SMC_FC_FFA_SUCCESS) {
        trusty_error(
                "%s: SMC_FC_FFA_FEATURES(SMC_FC_FFA_MEM_SHARE) failed 0x%lx 0x%lx 0x%lx\n",
                __func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
        goto err_features;
    }

    /*
     * Set FF-A endpoint IDs.
     */
    smc_ret = smc8(SMC_FC_FFA_ID_GET, 0, 0, 0, 0, 0, 0, 0);
    if (smc_ret.r0 != SMC_FC_FFA_SUCCESS) {
        trusty_error("%s: SMC_FC_FFA_ID_GET failed 0x%lx 0x%lx 0x%lx\n",
                     __func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
        goto err_id_get;
    }
    dev->ffa_local_id = smc_ret.r2;

    dev->ffa_tx = trusty_alloc_pages(TRUSTY_FFA_RXTX_PAGE_COUNT);
    if (!dev->ffa_tx) {
        goto err_alloc_ffa_tx;
    }
    dev->ffa_rx = trusty_alloc_pages(TRUSTY_FFA_RXTX_PAGE_COUNT);
    if (!dev->ffa_rx) {
        goto err_alloc_ffa_rx;
    }
    ret = trusty_encode_page_info(&tx_pinfo, dev->ffa_tx);
    if (ret) {
        trusty_error("%s: trusty_encode_page_info failed (%d)\n", __func__,
                     ret);
        goto err_encode_page_info;
    }
    ret = trusty_encode_page_info(&rx_pinfo, dev->ffa_rx);
    if (ret) {
        trusty_error("%s: trusty_encode_page_info failed (%d)\n", __func__,
                     ret);
        goto err_encode_page_info;
    }

    /*
     * TODO: check or pass memory attributes. The FF-A spec says the buffer has
     * to be cached, but we currently have callers that don't match this.
     */

    smc_ret = smc8(SMC_FCZ_FFA_RXTX_MAP, tx_pinfo.paddr, rx_pinfo.paddr,
                   TRUSTY_FFA_RXTX_PAGE_COUNT, 0, 0, 0, 0);
    if (smc_ret.r0 != SMC_FC_FFA_SUCCESS) {
        trusty_error("%s: FFA_RXTX_MAP failed 0x%lx 0x%lx 0x%lx\n", __func__,
                     smc_ret.r0, smc_ret.r1, smc_ret.r2);
        goto err_rxtx_map;
    }

    /*
     * Set remote FF-A endpoint IDs.
     *
     * This queries Trusty SP based on its UUID
     */
    smc_ret = smc8(SMC_FC_FFA_PARTITION_INFO_GET, trusty_uuid0_4[0],
                   trusty_uuid0_4[1], trusty_uuid0_4[2], trusty_uuid0_4[3], 0,
                   0, 0);
    if ((uint32_t)smc_ret.r0 == SMC_FC_FFA_ERROR &&
        (int32_t)smc_ret.r2 == FFA_ERROR_NOT_SUPPORTED) {
        /*
         * This interface is not supported,
         * use hard-coded values for backward compatibility
         */
        dev->ffa_remote_id = 0x8000;
        dev->ffa_supports_direct_recv = false;
    } else if ((uint32_t)smc_ret.r0 != SMC_FC_FFA_SUCCESS || smc_ret.r2 != 1) {
        trusty_error(
                "%s: SMC_FC_FFA_PARTITION_INFO_GET failed 0x%lx 0x%lx 0x%lx\n",
                __func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
        goto err_part_info_get;
    } else {
        struct ffa_partition_info* part_info = dev->ffa_rx;

        dev->ffa_remote_id = part_info->id;
        dev->ffa_supports_direct_recv =
                !!(part_info->id & FFA_PARTITION_DIRECT_REQ_RECV);

        /* release ownership of the receive buffer */
        smc_ret = smc8(SMC_FC_FFA_RX_RELEASE, 0, 0, 0, 0, 0, 0, 0);
        if ((uint32_t)smc_ret.r0 != SMC_FC_FFA_SUCCESS) {
            trusty_error("%s: SMC_FC_FFA_RX_RELEASE failed 0x%lx 0x%lx 0x%lx\n",
                         __func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
            goto err_rx_release;
        }
    }

    if (dev->ffa_supports_direct_recv) {
        /* Check that direct message support is implemented */
        smc_ret = smc8(SMC_FC_FFA_FEATURES, SMC_FC_FFA_MSG_SEND_DIRECT_REQ, 0,
                       0, 0, 0, 0, 0);
        if ((uint32_t)smc_ret.r0 == SMC_FC_FFA_ERROR &&
            (int32_t)smc_ret.r2 == FFA_ERROR_NOT_SUPPORTED) {
            /*
             * The partition info said Trusty supports direct messages,
             * but EL3 does not (e.g. it's running the SPD) so disable them.
             */
            dev->ffa_supports_direct_recv = false;
        } else if ((uint32_t)smc_ret.r0 != SMC_FC_FFA_SUCCESS) {
            trusty_error(
                    "%s: SMC_FC_FFA_FEATURES(SMC_FC_FFA_MSG_SEND_DIRECT_REQ) "
                    "failed 0x%lx 0x%lx 0x%lx\n",
                    __func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
            goto err_features_direct_req;
        }
    }

    return true;

err_features_direct_req:
err_rx_release:
    dev->ffa_supports_direct_recv = false;
    dev->ffa_remote_id = 0;
err_part_info_get:
    smc(SMC_FC_FFA_RXTX_UNMAP, FFA_PACK_RXTX_UNMAP_ID(dev->ffa_local_id), 0, 0);
err_rxtx_map:
err_encode_page_info:
    trusty_free_pages(dev->ffa_rx, TRUSTY_FFA_RXTX_PAGE_COUNT);
    dev->ffa_rx = NULL;
err_alloc_ffa_rx:
    trusty_free_pages(dev->ffa_tx, TRUSTY_FFA_RXTX_PAGE_COUNT);
    dev->ffa_tx = NULL;
err_alloc_ffa_tx:
    dev->ffa_local_id = 0;
err_id_get:
err_features:
err_version:
    return false;
}

int trusty_dev_init(struct trusty_dev* dev, void* priv_data) {
    bool found_ffa;
    int ret;
    trusty_assert(dev);

    dev->priv_data = priv_data;
    found_ffa = trusty_dev_ffa_init(dev);

    ret = trusty_init_api_version(dev);
    if (ret) {
        return ret;
    }
    if (dev->api_version >= TRUSTY_API_VERSION_MEM_OBJ && !found_ffa) {
        trusty_fatal("%s: FF-A required but not found\n", __func__);
    }

    return 0;
}

int trusty_dev_shutdown(struct trusty_dev* dev) {
    trusty_assert(dev);

    if (dev->ffa_tx) {
        smc(SMC_FC_FFA_RXTX_UNMAP, FFA_PACK_RXTX_UNMAP_ID(dev->ffa_local_id), 0,
            0);

        trusty_free_pages(dev->ffa_rx, TRUSTY_FFA_RXTX_PAGE_COUNT);
        dev->ffa_rx = NULL;
        trusty_free_pages(dev->ffa_tx, TRUSTY_FFA_RXTX_PAGE_COUNT);
        dev->ffa_tx = NULL;
        dev->ffa_local_id = 0;
        dev->ffa_remote_id = 0;
        dev->ffa_supports_direct_recv = false;
    }
    dev->priv_data = NULL;
    return 0;
}

int trusty_dev_nop(struct trusty_dev* dev) {
    int ret = trusty_std_call32(dev, SMC_SC_NOP, 0, 0, 0);
    return ret == SM_ERR_NOP_DONE ? 0 : ret == SM_ERR_NOP_INTERRUPTED ? 1 : -1;
}

int trusty_dev_share_memory(struct trusty_dev* dev,
                            trusty_shared_mem_id_t* idp,
                            struct ns_mem_page_info* pinfo,
                            size_t page_count) {
    struct smc_ret8 smc_ret;
    struct ffa_mtd* mtd = dev->ffa_tx;
    size_t comp_mrd_offset = offsetof(struct ffa_mtd, emad[1]);
    struct ffa_comp_mrd* comp_mrd = dev->ffa_tx + comp_mrd_offset;
    struct ffa_cons_mrd* cons_mrd = comp_mrd->address_range_array;
    size_t tx_size = ((void*)cons_mrd - dev->ffa_tx) + sizeof(*cons_mrd);

    if (!dev->ffa_tx) {
        /*
         * If the trusty api version is before TRUSTY_API_VERSION_MEM_OBJ, fall
         * back to old api of passing the 64 bit paddr/attr value directly.
         */
        *idp = pinfo->attr;
        return 0;
    }

    trusty_memset(mtd, 0, tx_size);
    mtd->sender_id = dev->ffa_local_id;
    mtd->memory_region_attributes = pinfo->ffa_mem_attr;
    mtd->emad_count = 1;
    mtd->emad[0].mapd.endpoint_id = dev->ffa_remote_id;
    mtd->emad[0].mapd.memory_access_permissions = pinfo->ffa_mem_perm;
    mtd->emad[0].comp_mrd_offset = comp_mrd_offset;
    comp_mrd->total_page_count = page_count;
    comp_mrd->address_range_count = 1;
    cons_mrd->address = pinfo->paddr;
    cons_mrd->page_count = page_count;

    /*
     * Tell the SPM/Hypervisor to share the memory.
     */
    smc_ret = smc8(SMC_FC_FFA_MEM_SHARE, tx_size, tx_size, 0, 0, 0, 0, 0);
    if ((unsigned int)smc_ret.r0 != SMC_FC_FFA_SUCCESS) {
        trusty_error("%s: SMC_FC_FFA_MEM_SHARE failed 0x%lx 0x%lx 0x%lx\n",
                     __func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
        return -1;
    }

    *idp = smc_ret.r2;

    return 0;
}

int trusty_dev_reclaim_memory(struct trusty_dev* dev,
                              trusty_shared_mem_id_t id) {
    struct smc_ret8 smc_ret;

    if (!dev->ffa_tx) {
        /*
         * If the trusty api version is before TRUSTY_API_VERSION_MEM_OBJ, fall
         * back to old api.
         */
        return 0;
    }

    /*
     * Tell the SPM/Hypervisor to reclaim the memory. If the memory is still in
     * use this will fail.
     */
    smc_ret =
            smc8(SMC_FC_FFA_MEM_RECLAIM, (uint32_t)id, id >> 32, 0, 0, 0, 0, 0);
    if ((unsigned int)smc_ret.r0 != SMC_FC_FFA_SUCCESS) {
        trusty_error("%s: SMC_FC_FFA_MEM_RECLAIM failed 0x%lx 0x%lx 0x%lx\n",
                     __func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
        return -1;
    }

    return 0;
}
