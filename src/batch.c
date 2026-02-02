/**
 * batch.c - Batch operations implementation
 *
 * Implements batch propose and apply for improved throughput.
 */

#include "batch.h"
#include "log.h"
#include "storage.h"
#include <stdlib.h>

raft_status_t raft_propose_batch(raft_node_t* node,
                                  const char** commands,
                                  const size_t* command_lens,
                                  size_t count,
                                  uint64_t* out_first_index) {
    if (!node) return RAFT_INVALID_ARG;
    if (!node->running) return RAFT_STOPPED;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;
    if (count == 0) return RAFT_INVALID_ARG;
    if (!commands || !command_lens) return RAFT_INVALID_ARG;

    uint64_t first_index = 0;
    uint64_t term = node->persistent.current_term;

    /* Append all entries to log */
    for (size_t i = 0; i < count; i++) {
        uint64_t index;
        raft_status_t status = raft_log_append(node->log, term,
                                                commands[i], command_lens[i],
                                                &index);
        if (status != RAFT_OK) {
            /* Rollback on failure - truncate any entries we added */
            if (first_index > 0) {
                raft_log_truncate_after(node->log, first_index - 1);
            }
            return status;
        }

        if (i == 0) {
            first_index = index;
        }

        /* Persist if storage is enabled */
        if (node->storage) {
            raft_entry_t entry = {
                .term = term,
                .index = index,
                .type = RAFT_ENTRY_COMMAND,
                .command = (char*)commands[i],
                .command_len = command_lens[i],
            };
            raft_status_t persist_status = raft_storage_append_entry(node->storage, &entry);
            if (persist_status != RAFT_OK) {
                /* Rollback on persistence failure */
                raft_log_truncate_after(node->log, first_index - 1);
                return persist_status;
            }
        }
    }

    /* Update match_index for self (leader) */
    if (node->leader_state.match_index) {
        node->leader_state.match_index[node->node_id] = raft_log_last_index(node->log);
    }

    if (out_first_index) {
        *out_first_index = first_index;
    }

    return RAFT_OK;
}

size_t raft_apply_batch(raft_node_t* node, size_t max_entries) {
    if (!node) return 0;

    size_t applied = 0;
    uint64_t commit_index = node->volatile_state.commit_index;
    uint64_t last_applied = node->volatile_state.last_applied;

    /* Determine how many to apply */
    size_t available = 0;
    if (commit_index > last_applied) {
        available = (size_t)(commit_index - last_applied);
    }

    if (max_entries > 0 && available > max_entries) {
        available = max_entries;
    }

    /* Apply entries */
    for (size_t i = 0; i < available; i++) {
        uint64_t index = last_applied + 1 + i;
        const raft_entry_t* entry = raft_log_get(node->log, index);

        if (!entry) break;

        /* Call apply callback if set */
        if (node->apply_fn) {
            node->apply_fn(node, entry, node->user_data);
        }

        applied++;
    }

    /* Update last_applied */
    node->volatile_state.last_applied = last_applied + applied;

    return applied;
}

size_t raft_pending_apply_count(raft_node_t* node) {
    if (!node) return 0;

    uint64_t commit_index = node->volatile_state.commit_index;
    uint64_t last_applied = node->volatile_state.last_applied;

    if (commit_index > last_applied) {
        return (size_t)(commit_index - last_applied);
    }

    return 0;
}
