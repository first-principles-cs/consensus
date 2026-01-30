/**
 * commit.h - Commit index management for Raft consensus
 *
 * Handles commit index advancement based on replication status.
 */

#ifndef RAFT_COMMIT_H
#define RAFT_COMMIT_H

#include "types.h"

/**
 * Advance commit index based on match_index array
 * Only commits entries from current term (Raft safety requirement)
 */
raft_status_t raft_advance_commit_index(raft_node_t* node);

/**
 * Check if entry at given index is committed on majority
 */
bool raft_is_committed(raft_node_t* node, uint64_t index);

/**
 * Get the highest index replicated on majority of nodes
 */
uint64_t raft_get_majority_match_index(raft_node_t* node);

#endif /* RAFT_COMMIT_H */
