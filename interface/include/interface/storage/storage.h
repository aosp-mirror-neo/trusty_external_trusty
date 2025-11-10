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

/*
 * The contents of this file are copied from
 * trusty/lib/interface/storage/include/interface/storage/storage.h.
 * It is required to stay in sync for struct formats and enum values.
 */

#pragma once

#include <trusty/sysdeps.h>

/*
 * @STORAGE_DISK_PROXY_PORT: Port used by non-secure proxy server
 */
#define STORAGE_DISK_PROXY_PORT "com.android.trusty.storage.proxy"

enum storage_cmd {
    STORAGE_REQ_SHIFT = 1,
    STORAGE_RESP_BIT = 1,

    STORAGE_RESP_MSG_ERR = STORAGE_RESP_BIT,

    STORAGE_FILE_DELETE = 1 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_OPEN = 2 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_CLOSE = 3 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_READ = 4 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_WRITE = 5 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_GET_SIZE = 6 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_SET_SIZE = 7 << STORAGE_REQ_SHIFT,

    STORAGE_RPMB_SEND = 8 << STORAGE_REQ_SHIFT,

    /* transaction support */
    STORAGE_END_TRANSACTION = 9 << STORAGE_REQ_SHIFT,

    STORAGE_FILE_MOVE = 10 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_LIST = 11 << STORAGE_REQ_SHIFT,
    STORAGE_FILE_GET_MAX_SIZE = 12 << STORAGE_REQ_SHIFT,
    STORAGE_CHECKPOINTING_STATE = 13 << STORAGE_REQ_SHIFT,
};

/**
 * enum storage_err - error codes for storage protocol
 * @STORAGE_NO_ERROR:           all OK
 * @STORAGE_ERR_GENERIC:        unknown error. Can occur when there's an
 *                              internal server error, e.g. the server runs out
 *                              of memory or is in a bad state.
 * @STORAGE_ERR_NOT_VALID:      input not valid. May occur if the arguments
 *                              passed into the command are not valid, for
 *                              example if the file handle passed in is not a
 *                              valid one.
 * @STORAGE_ERR_UNIMPLEMENTED:  the command passed in is not recognized
 * @STORAGE_ERR_ACCESS:         the file is not accessible in the requested mode
 * @STORAGE_ERR_NOT_FOUND:      the file was not found
 * @STORAGE_ERR_EXIST           the file exists when it shouldn't as in with
 *                              OPEN_CREATE | OPEN_EXCLUSIVE.
 * @STORAGE_ERR_TRANSACT        returned by various operations to indicate that
 *                              current transaction is in error state. Such
 *                              state could be only cleared by sending
 *                              STORAGE_END_TRANSACTION message.
 * @STORAGE_ERR_SYNC_FAILURE    indicates that the current operation failed to
 *                              sync to disk. Only returned if
 *                              STORAGE_MSG_FLAG_PRE_COMMIT or
 *                              STORAGE_MSG_FLAG_POST_COMMIT was set for the
 *                              request.
 * @STORAGE_ERR_NOT_ALLOWED     operation is not allowed in the current state
 * @STORAGE_ERR_CORRUPTED       requested operation touches a corrupted block
 *                              and is not allowed
 * @STORAGE_ERR_FS_REPAIRED     filesystem has been repaired in a way that may
 *                              affect the requested operation. The requested
 *                              operation is not allowed without acknowledging
 *                              the repaired state. Pass
 *                              STORAGE_MSG_FLAGS_FS_REPAIRED_ACK if a repaired
 *                              filesystem state is acceptable.
 */
enum storage_err {
    STORAGE_NO_ERROR = 0,
    STORAGE_ERR_GENERIC = 1,
    STORAGE_ERR_NOT_VALID = 2,
    STORAGE_ERR_UNIMPLEMENTED = 3,
    STORAGE_ERR_ACCESS = 4,
    STORAGE_ERR_NOT_FOUND = 5,
    STORAGE_ERR_EXIST = 6,
    STORAGE_ERR_TRANSACT = 7,
    STORAGE_ERR_SYNC_FAILURE = 8,
    STORAGE_ERR_NOT_ALLOWED = 9,
    STORAGE_ERR_CORRUPTED = 10,
    STORAGE_ERR_FS_REPAIRED = 11,
};

/**
 * enum storage_msg_flag - protocol-level flags in struct storage_msg
 * @STORAGE_MSG_FLAG_BATCH:                 if set, command belongs to a batch
 *                                          transaction. No response will be
 *                                          sent by the server until it receives
 *                                          a command with this flag unset, at
 *                                          which point a cumulative result for
 *                                          all messages sent with
 *                                          STORAGE_MSG_FLAG_BATCH will be sent.
 *                                          This is only supported by the
 *                                          non-secure disk proxy server.
 * @STORAGE_MSG_FLAG_PRE_COMMIT:            if set, indicates that server need
 *                                          to commit pending changes before
 *                                          processing this message.
 * @STORAGE_MSG_FLAG_POST_COMMIT:           if set, indicates that server need
 *                                          to commit pending changes after
 *                                          processing this message.
 * @STORAGE_MSG_FLAG_TRANSACT_COMPLETE:     if set, indicates that server need
 *                                          to commit current transaction after
 *                                          processing this message. It is an
 *                                          alias for
 *                                          STORAGE_MSG_FLAG_POST_COMMIT.
 * @STORAGE_MSG_FLAG_PRE_COMMIT_CHECKPOINT: if set, indicates that server needs
 *                                          to ensure that there is not a
 *                                          pending checkpoint for the
 *                                          underlying userdata filesystem
 *                                          before processing this message.
 * @STORAGE_MSG_FLAG_TRANSACT_CHECKPOINT:   if set, update the current
 *                                          checkpoint to the new state of
 *                                          files modified by this
 *                                          transaction. Only valid along with
 *                                          @STORAGE_MSG_FLAG_TRANSACT_COMPLETE,
 *                                          and only allowed when provisiong is
 *                                          allowed by the system state service.
 * @STORAGE_MSG_FLAG_FS_REPAIRED_ACK:       acknowledge that an error in the
 *                                          file system may have been repaired,
 *                                          and therefore rollback may have
 *                                          occurred. Do not use this flag
 *                                          unless accessing files where partial
 *                                          rollback of file system state is
 *                                          acceptable.
 * @STORAGE_MSG_FLAG_STAGE_IF_CHECKPOINT:   if the underlying filesystem is
 *                                          checkpointing, stage the pending
 *                                          changes to be committed after
 *                                          checkpoint finishes and before the
 *                                          first post-checkpoint commit (on any
 *                                          filesystem). If the checkpoint is
 *                                          rolled back, the changes will not be
 *                                          committed at all. Only valid along
 *                                          with
 *                                          @STORAGE_MSG_FLAG_TRANSACT_COMPLETE.
 *                                          Not valid together with
 *                                          @STORAGE_MSG_FLAG_PRE_COMMIT_CHECKPOINT.
 */
enum storage_msg_flag {
    STORAGE_MSG_FLAG_BATCH = 0x1,
    STORAGE_MSG_FLAG_PRE_COMMIT = 0x2,
    STORAGE_MSG_FLAG_POST_COMMIT = 0x4,
    STORAGE_MSG_FLAG_TRANSACT_COMPLETE = STORAGE_MSG_FLAG_POST_COMMIT,
    STORAGE_MSG_FLAG_PRE_COMMIT_CHECKPOINT = 0x8,
    STORAGE_MSG_FLAG_TRANSACT_CHECKPOINT = 0x10,
    STORAGE_MSG_FLAG_FS_REPAIRED_ACK = 0x20,
    STORAGE_MSG_FLAG_STAGE_IF_CHECKPOINT = 0x40,
};

/*
 * The following declarations are the message-specific contents of
 * the 'payload' element inside struct storage_msg.
 */

/**
 * struct storage_rpmb_send_req - request format for STORAGE_RPMB_SEND
 * @reliable_write_size:        size in bytes of reliable write region
 * @write_size:                 size in bytes of write region
 * @read_size:                  number of bytes to read for a read request
 * @__reserved:                 unused, must be set to 0
 * @payload:                    start of reliable write region, followed by
 *                              write region.
 *
 * Only used in proxy<->server interface.
 */
struct storage_rpmb_send_req {
    uint32_t reliable_write_size;
    uint32_t write_size;
    uint32_t read_size;
    uint32_t __reserved;
    uint8_t payload[0];
};

/**
 * struct storage_rpmb_send_resp: response type for STORAGE_RPMB_SEND
 * @data: the data frames frames retrieved from the MMC.
 *
 * This struct definition is only safe to use in C since empty structs have
 * different sizes in C(0) and C++(1) which makes them unsafe to be used
 * across languages
 */
#ifndef __cplusplus
struct storage_rpmb_send_resp {
    uint8_t data[0];
};
#endif

/**
 * enum storage_checkpoint_state: the device's User Data Checkpoint status
 * @STORAGE_CHECKPOINT_STATE_DONE:     There is no active UDC. Writes will not
 *                                     be rolled back and if there's a forward
 *                                     checkpoint for
 *                                     &storage_checkpointing_state_resp.boot_slot,
 *                                     it should be committed.
 * @STORAGE_CHECKPOINT_STATE_POSSIBLE: There may be an active UDC or maybe the
 *                                     device boot is not yet far enough along
 *                                     to know. Writes may need to be rolled
 *                                     back.
 *
 * Other values are reserved for future use.
 */
enum storage_checkpoint_state : uint32_t {
    STORAGE_CHECKPOINT_STATE_DONE = 1,
    STORAGE_CHECKPOINT_STATE_POSSIBLE = 2,
};

/**
 * struct storage_checkpointing_state_resp: response type for
 * STORAGE_CHECKPOINTING_STATE
 * @state:     Value of type &enum storage_checkpoint_state describing the
 *             current state
 * @boot_slot: ID for the current boot slot. Same as
 *             IBootControl::getCurrentSlot. A value < 0 indicates that
 *             storageproxy does not support on-disk staged changes, so they'll
 *             always be discarded.
 *
 * If storage observes staged changes on-disk which are tagged with a different
 * boot slot than @boot_slot, it means that we have rolled back, so the changes
 * should be discarded. If the changes' boot slot matches @boot_slot, we have
 * not rolled back, so the on-disk staged changes should be used, then committed
 * when the checkpoint completes. Storage gets a blocking call (TODO: implement
 * this) to mark a checkpoint complete (before IBootControl::setActiveBootSlot
 * is called), so a non-matching boot slot can't be the result of a double
 * update.
 */
struct storage_checkpointing_state_resp {
    uint32_t state;
    int32_t boot_slot;
};

/**
 * struct storage_msg - generic req/resp format for all storage commands
 * @cmd:        one of enum storage_cmd
 * @op_id:      client chosen operation identifier for an instance
 *              of a command or atomic grouping of commands (transaction).
 * @flags:      one or many of enum storage_msg_flag or'ed together.
 * @size:       total size of the message including this header
 * @result:     one of enum storage_err
 * @__reserved: unused, must be set to 0.
 * @payload:    beginning of command specific message format
 */
struct storage_msg {
    uint32_t cmd;
    uint32_t op_id;
    uint32_t flags;
    uint32_t size;
    int32_t result;
    uint32_t __reserved;
    uint8_t payload[0];
};
