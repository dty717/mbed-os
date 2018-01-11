/* Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include "mbed_toolchain.h"
#include "spm_client.h"
#include "spm_internal.h"
#include "spm_panic.h"
#include "handles_manager.h"

extern spm_t g_spm;

static inline secure_func_t *sec_func_in_partition_get_by_sfid(partition_t *partition, uint32_t sfid)
{
    for (uint32_t i = 0; i < partition->sec_funcs_count; ++i) {
        secure_func_t *sec_func = &(partition->sec_funcs[i]);
        if (sec_func->sfid == sfid) {
            return sec_func;
        }
    }

    return NULL;
}

static inline secure_func_t *sec_func_get(uint32_t sfid)
{
    for (uint32_t i = 0; i < g_spm.partition_count; ++i) {
        secure_func_t *sec_func = sec_func_in_partition_get_by_sfid(&(g_spm.partitions[i]), sfid);
        if (NULL != sec_func) {
            return sec_func;
        }
    }

    return NULL;
}

static inline void gather_iovecs(const iovec_t *src, uint32_t src_len, void *dst)
{
    uint32_t active_dst_offset = 0;

    for (uint32_t i = 0; i < src_len; ++i) {
        memcpy((uint8_t *)dst + active_dst_offset, (uint8_t *)src[i].iovec_base, src[i].iov_len);
        active_dst_offset += src[i].iov_len;
    }
}

// This function should do proper validation
static inline bool is_buffer_accessible(void* ptr, size_t size)
{
    PSA_UNUSED(ptr);
    PSA_UNUSED(size);
    return true;
}

static inline error_t spm_channel_handle_create(void *handle_mem, int32_t friend_pid, psa_handle_t *handle)
{
    return psa_hndl_mgr_handle_create(&(g_spm.channels_handle_mgr), handle_mem, friend_pid, handle);
}

static inline void spm_channel_handle_destroy(psa_handle_t handle)
{
    psa_hndl_mgr_handle_destroy(&(g_spm.channels_handle_mgr), handle);
}

static inline void spm_channel_handle_get_mem(psa_handle_t handle, ipc_channel_t **handle_mem)
{
    psa_hndl_mgr_handle_get_mem(&(g_spm.channels_handle_mgr), handle, (void **)handle_mem);
}

static inline void spm_validate_connection_allowed(secure_func_t *target, partition_t *source)
{
    if ((NULL == source) && (false == target->allow_nspe)) {
        spm_panic("%s - SFID 0x%x is not allowed to be called from NSPE\n", __func__, target->sfid);
    }

    if (NULL != source) {
        if (NULL == source->extern_sfids) {
            spm_panic("%s - Partition %d did not declare extern functions\n", __func__, source->partition_id);
        }

        for (uint32_t i = 0; i < source->extern_sfids_count; i++) {
            if (source->extern_sfids[i] == target->sfid) {
                return;
            }
        }

        spm_panic("%s - SFID 0x%x is not in partition %d extern functions list\n", __func__, target->sfid, source->partition_id);
    }
}

psa_handle_t psa_connect(uint32_t sfid, uint32_t minor_version)
{
    secure_func_t *dst_sec_func = sec_func_get(sfid);
    if (NULL == dst_sec_func) {
        spm_panic("%s - SFID 0x%x is invalid!\n", __func__, sfid);
    }

    if (((dst_sec_func->min_version_policy == PSA_MINOR_VERSION_POLICY_RELAXED) && (minor_version > dst_sec_func->min_version)) ||
        ((dst_sec_func->min_version_policy == PSA_MINOR_VERSION_POLICY_STRICT) && (minor_version != dst_sec_func->min_version))) {
            spm_panic("%s - minor version %d does not comply with sfid %d minor policy %d",
                    __func__, minor_version, dst_sec_func->sfid, dst_sec_func->min_version_policy);
    }

    partition_t *current_part = active_partition_get();
    spm_validate_connection_allowed(dst_sec_func, current_part);

    partition_t *dst_partition = dst_sec_func->partition;

    osStatus_t os_status = osMutexAcquire(dst_partition->mutex, osWaitForever);
    SPM_ASSERT(osOK == os_status);

    PARTITION_STATE_ASSERT(dst_partition, PARTITION_STATE_IDLE);

    ipc_channel_t *channel = (ipc_channel_t *)osMemoryPoolAlloc(g_spm.channel_mem_pool, 0);
    if (NULL == channel) {
        os_status = osMutexRelease(dst_partition->mutex);
        SPM_ASSERT(osOK == os_status);
        return PSA_CONNECTION_REFUSED_BUSY;
    }

    memset(channel, 0, sizeof(ipc_channel_t));
    channel->src_partition = current_part;
    channel->dst_sec_func = dst_sec_func;
    channel->rhandle = NULL;

    active_msg_t *active_msg = &(dst_partition->active_msg);
    SPM_ASSERT(PSA_IPC_MSG_TYPE_INVALID == active_msg->type);
    memset(active_msg, 0, sizeof(active_msg_t));
    active_msg->channel = channel;

    active_msg->type = PSA_IPC_MSG_TYPE_CONNECT;

    dst_partition->partition_state = PARTITION_STATE_ACTIVE;

    int32_t flags = (int32_t)osThreadFlagsSet(dst_partition->thread_id, dst_sec_func->mask);
    SPM_ASSERT(flags >= 0);
    PSA_UNUSED(flags);

    os_status = osSemaphoreAcquire(dst_partition->semaphore, osWaitForever);
    SPM_ASSERT(osOK == os_status);

    PARTITION_STATE_ASSERT(dst_partition, PARTITION_STATE_COMPLETED);

    psa_handle_t handle = active_msg->rc;
    if (PSA_SUCCESS == active_msg->rc) {
        psa_error_t status = spm_channel_handle_create(channel, PSA_HANDLE_MGR_INVALID_FRIEND_OWNER, &handle);
        // handle_create() is expected to pass since channel allocation has already passed,
        // and the channels handler manager has the same storage size as the channels pool storage size
        SPM_ASSERT(PSA_SUCCESS == status);
        PSA_UNUSED(status);
    }

    dst_partition->partition_state = PARTITION_STATE_IDLE;

    os_status = osMutexRelease(dst_partition->mutex);
    SPM_ASSERT(osOK == os_status);

    PSA_UNUSED(os_status);

    return handle;
}

psa_error_t psa_call(
    psa_handle_t handle,
    const iovec_t *tx_iovec,
    size_t tx_len,
    void *rx_buf,
    size_t rx_len
    )
{
    if (tx_len > PSA_MAX_IOVEC_LEN) {
        spm_panic("%s - tx_len (%d) is bigger than allowed (%d)\n", __func__, tx_len, PSA_MAX_IOVEC_LEN);
    }

    if ((tx_iovec == NULL) ^ (tx_len == 0)) {
        spm_panic("%s - tx_iovec or tx_len are invalid\n", __func__);
    }

    if ((rx_buf == NULL) ^ (rx_len == 0)) {
        spm_panic("%s - rx_buf or rx_len are invalid\n", __func__);
    }

    uint32_t total_iovec_size = 0;
    for (uint32_t i = 0; i < tx_len; ++i) {
        if (tx_iovec[i].iovec_base == NULL) {
            continue;
        }

        if (!is_buffer_accessible((void *)tx_iovec[i].iovec_base, tx_iovec[i].iov_len)) {
            spm_panic("%s - tx_iovec[%d] is inaccessible\n", __func__, i);
        }

        total_iovec_size += tx_iovec[i].iov_len;
    }

    if (total_iovec_size > MBED_CONF_SPM_CLIENT_DATA_TX_BUF_SIZE_LIMIT) {
        spm_panic("%s - payload (%d) is larger than avialable memory(%d)\n", __func__, total_iovec_size,
            MBED_CONF_SPM_CLIENT_DATA_TX_BUF_SIZE_LIMIT);
    }

    if ((rx_buf != NULL) && (!is_buffer_accessible(rx_buf, rx_len))) {
        spm_panic("%s - rx_buf is inaccessible\n", __func__);
    }

    ipc_channel_t *channel = NULL;
    spm_channel_handle_get_mem(handle, &channel);
    secure_func_t *dst_sec_func = channel->dst_sec_func;

    partition_t *dst_partition = dst_sec_func->partition;

    osStatus_t os_status = osMutexAcquire(dst_partition->mutex, osWaitForever);
    SPM_ASSERT(osOK == os_status);

    PARTITION_STATE_ASSERT(dst_partition, PARTITION_STATE_IDLE);

    active_msg_t *active_msg = &(dst_partition->active_msg);
    SPM_ASSERT(PSA_IPC_MSG_TYPE_INVALID == active_msg->type);
    memset(active_msg, 0, sizeof(active_msg_t));
    active_msg->channel = channel;
    active_msg->rx_buf = rx_buf;
    active_msg->rx_size = rx_len;
    active_msg->tx_size = total_iovec_size;
    gather_iovecs(tx_iovec, tx_len, active_msg->tx_buf);

    active_msg->type = PSA_IPC_MSG_TYPE_CALL;

    dst_partition->partition_state = PARTITION_STATE_ACTIVE;

    int32_t flags = (int32_t)osThreadFlagsSet(dst_partition->thread_id, dst_sec_func->mask);
    SPM_ASSERT(flags >= 0);
    PSA_UNUSED(flags);

    os_status = osSemaphoreAcquire(dst_partition->semaphore, osWaitForever);
    SPM_ASSERT(osOK == os_status);

    PARTITION_STATE_ASSERT(dst_partition, PARTITION_STATE_COMPLETED);

    dst_partition->partition_state = PARTITION_STATE_IDLE;

    psa_error_t rc = active_msg->rc;
    memset(active_msg, 0, sizeof(active_msg_t));

    os_status = osMutexRelease(dst_partition->mutex);
    SPM_ASSERT(osOK == os_status);

    PSA_UNUSED(os_status);

    return rc;
}

psa_error_t psa_close(psa_handle_t handle)
{
    if (handle == PSA_NULL_HANDLE) {
        return PSA_SUCCESS;
    }

    ipc_channel_t *channel = NULL;

    spm_channel_handle_get_mem(handle, &channel);

    secure_func_t *dst_sec_func = channel->dst_sec_func;

    partition_t *dst_partition = dst_sec_func->partition;

    osStatus_t os_status = osMutexAcquire(dst_partition->mutex, osWaitForever);
    SPM_ASSERT(osOK == os_status);

    PARTITION_STATE_ASSERT(dst_partition, PARTITION_STATE_IDLE);

    active_msg_t *active_msg = &(dst_partition->active_msg);
    SPM_ASSERT(PSA_IPC_MSG_TYPE_INVALID == active_msg->type);
    memset(active_msg, 0, sizeof(active_msg_t));
    active_msg->channel = channel;

    active_msg->type = PSA_IPC_MSG_TYPE_DISCONNECT;

    dst_partition->partition_state = PARTITION_STATE_ACTIVE;

    int32_t flags = (int32_t)osThreadFlagsSet(dst_partition->thread_id, dst_sec_func->mask);
    SPM_ASSERT(flags >= 0);
    PSA_UNUSED(flags);

    os_status = osSemaphoreAcquire(dst_partition->semaphore, osWaitForever);
    SPM_ASSERT(osOK == os_status);

    PARTITION_STATE_ASSERT(dst_partition, PARTITION_STATE_COMPLETED);

    dst_partition->partition_state = PARTITION_STATE_IDLE;

    spm_channel_handle_destroy(handle);

    os_status = osMemoryPoolFree(g_spm.channel_mem_pool, channel);
    SPM_ASSERT(osOK == os_status);
    active_msg->channel = NULL;

    os_status = osMutexRelease(dst_partition->mutex);
    SPM_ASSERT(osOK == os_status);

    PSA_UNUSED(os_status);

    return PSA_SUCCESS;
}