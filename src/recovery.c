/**
 * recovery.c - Crash recovery implementation
 */

#include "recovery.h"
#include "snapshot.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/* Context for log recovery callback */
typedef struct {
    raft_node_t* node;
    uint64_t count;
    uint64_t last_index;
    uint64_t last_term;
} recovery_ctx_t;

/* Callback to add recovered entries to in-memory log */
static raft_status_t recover_entry_cb(void* ctx,
                                       uint64_t term,
                                       uint64_t index,
                                       const char* command,
                                       size_t command_len) {
    recovery_ctx_t* rctx = (recovery_ctx_t*)ctx;

    /* Append to in-memory log */
    uint64_t out_index;
    raft_status_t status = raft_log_append(rctx->node->log, term,
                                            command, command_len, &out_index);
    if (status != RAFT_OK) return status;

    /* Verify index matches */
    if (out_index != index) {
        return RAFT_CORRUPTION;
    }

    rctx->count++;
    rctx->last_index = index;
    rctx->last_term = term;
    return RAFT_OK;
}

raft_status_t raft_recover(raft_node_t* node,
                            raft_storage_t* storage,
                            raft_recovery_result_t* result) {
    if (!node || !storage) return RAFT_INVALID_ARG;

    raft_recovery_result_t local_result = {0};
    local_result.recovered_voted_for = -1;

    const char* data_dir = raft_storage_get_dir(storage);
    if (!data_dir) return RAFT_INVALID_ARG;

    /* Step 1: Check for snapshot */
    raft_snapshot_meta_t snap_meta;
    if (raft_snapshot_exists(data_dir)) {
        raft_status_t status = raft_snapshot_load_meta(data_dir, &snap_meta);
        if (status == RAFT_OK) {
            local_result.had_snapshot = true;
            /* Set log base from snapshot */
            node->log->base_index = snap_meta.last_index;
            node->log->base_term = snap_meta.last_term;
        }
    }

    /* Step 2: Load persistent state (current_term, voted_for) */
    uint64_t term;
    int32_t voted_for;
    raft_status_t status = raft_storage_load_state(storage, &term, &voted_for);
    if (status == RAFT_OK) {
        node->persistent.current_term = term;
        node->persistent.voted_for = voted_for;
        local_result.recovered_term = term;
        local_result.recovered_voted_for = voted_for;
    } else if (status != RAFT_NOT_FOUND) {
        return status;
    }

    /* Step 3: Recover log entries */
    recovery_ctx_t ctx = {
        .node = node,
        .count = 0,
        .last_index = 0,
        .last_term = 0,
    };

    status = raft_storage_iterate_log(storage, recover_entry_cb, &ctx);
    if (status != RAFT_OK) return status;

    local_result.log_entries_count = ctx.count;
    local_result.last_log_index = ctx.last_index;
    local_result.last_log_term = ctx.last_term;

    if (result) {
        *result = local_result;
    }

    return RAFT_OK;
}
