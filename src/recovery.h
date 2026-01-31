/**
 * recovery.h - Crash recovery for Raft nodes
 *
 * Restores node state from persistent storage after a crash.
 */

#ifndef RAFT_RECOVERY_H
#define RAFT_RECOVERY_H

#include "types.h"
#include "raft.h"
#include "storage.h"

/**
 * Recovery result information
 */
typedef struct raft_recovery_result {
    uint64_t recovered_term;      /* Recovered current_term */
    int32_t recovered_voted_for;  /* Recovered voted_for */
    uint64_t log_entries_count;   /* Number of log entries recovered */
    uint64_t last_log_index;      /* Index of last recovered entry */
    uint64_t last_log_term;       /* Term of last recovered entry */
    bool had_snapshot;            /* Whether a snapshot was found */
} raft_recovery_result_t;

/**
 * Recover node state from persistent storage
 *
 * This function:
 * 1. Checks for snapshot and loads metadata if present
 * 2. Loads current_term and voted_for from state file
 * 3. Replays log entries from storage into memory
 * 4. Updates node state accordingly
 *
 * @param node The Raft node to recover
 * @param storage The storage handle
 * @param result Optional result information (can be NULL)
 * @return RAFT_OK on success, error code otherwise
 */
raft_status_t raft_recover(raft_node_t* node,
                            raft_storage_t* storage,
                            raft_recovery_result_t* result);

#endif /* RAFT_RECOVERY_H */
