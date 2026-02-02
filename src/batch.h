/**
 * batch.h - Batch operations for improved throughput
 *
 * Provides batch propose and apply operations to reduce per-entry overhead.
 */

#ifndef RAFT_BATCH_H
#define RAFT_BATCH_H

#include "types.h"
#include "raft.h"

/**
 * Propose multiple commands in a batch
 * All commands are appended atomically to the log.
 * Only succeeds if this node is the leader.
 *
 * @param node Raft node (must be leader)
 * @param commands Array of command data pointers
 * @param command_lens Array of command lengths
 * @param count Number of commands
 * @param out_first_index Output: index of first entry in batch
 * @return RAFT_OK on success, RAFT_NOT_LEADER if not leader
 */
raft_status_t raft_propose_batch(raft_node_t* node,
                                  const char** commands,
                                  const size_t* command_lens,
                                  size_t count,
                                  uint64_t* out_first_index);

/**
 * Apply multiple committed entries to the state machine
 * Applies up to max_entries entries starting from last_applied + 1.
 *
 * @param node Raft node
 * @param max_entries Maximum number of entries to apply (0 = all available)
 * @return Number of entries applied
 */
size_t raft_apply_batch(raft_node_t* node, size_t max_entries);

/**
 * Get the number of entries ready to be applied
 * (commit_index - last_applied)
 *
 * @param node Raft node
 * @return Number of entries ready to apply
 */
size_t raft_pending_apply_count(raft_node_t* node);

#endif /* RAFT_BATCH_H */
