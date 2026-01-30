/**
 * replication.h - Log replication for Raft consensus
 *
 * Handles log entry replication from leader to followers.
 */

#ifndef RAFT_REPLICATION_H
#define RAFT_REPLICATION_H

#include "types.h"
#include "rpc.h"

/**
 * Send log entries to a specific peer
 * Uses next_index to determine which entries to send
 */
raft_status_t raft_replicate_to_peer(raft_node_t* node, int32_t peer_id);

/**
 * Send log entries to all peers
 */
raft_status_t raft_replicate_log(raft_node_t* node);

/**
 * Handle AppendEntries response from a peer
 * Updates next_index and match_index accordingly
 */
raft_status_t raft_handle_append_entries_response(
    raft_node_t* node,
    int32_t from_node,
    const raft_append_entries_response_t* response);

/**
 * Handle AppendEntries with log entries (not just heartbeat)
 * Performs log consistency check and appends entries
 */
raft_status_t raft_handle_append_entries_with_log(
    raft_node_t* node,
    const void* msg,
    size_t msg_len,
    raft_append_entries_response_t* response);

#endif /* RAFT_REPLICATION_H */
