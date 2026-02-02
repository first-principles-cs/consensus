/**
 * transfer.h - Leadership transfer (Phase 6)
 *
 * Provides graceful leadership transfer to a specific node.
 */

#ifndef RAFT_TRANSFER_H
#define RAFT_TRANSFER_H

#include "types.h"

/* Transfer state */
typedef enum {
    RAFT_TRANSFER_NONE = 0,
    RAFT_TRANSFER_PENDING = 1,
    RAFT_TRANSFER_SENDING = 2,
} raft_transfer_state_t;

/**
 * Transfer leadership to a specific node
 *
 * @param node Raft node (must be leader)
 * @param target_id Node ID to transfer leadership to (-1 for any)
 * @return RAFT_OK if transfer started, RAFT_NOT_LEADER if not leader
 */
raft_status_t raft_transfer_leadership(raft_node_t* node, int32_t target_id);

/**
 * Abort an ongoing leadership transfer
 */
void raft_transfer_abort(raft_node_t* node);

/**
 * Check if a leadership transfer is in progress
 */
bool raft_transfer_in_progress(raft_node_t* node);

/**
 * Get the target node for leadership transfer (-1 if none)
 */
int32_t raft_transfer_target(raft_node_t* node);

/**
 * Process transfer progress (called after replication)
 * Sends TimeoutNow when target is caught up
 */
void raft_transfer_check_progress(raft_node_t* node);

#endif /* RAFT_TRANSFER_H */
