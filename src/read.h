/**
 * read.h - ReadIndex for linearizable reads (Phase 6)
 *
 * Provides linearizable read-only queries without going through the log.
 */

#ifndef RAFT_READ_H
#define RAFT_READ_H

#include "types.h"

/**
 * Callback for read completion
 * Called when the read request can be safely served
 */
typedef void (*raft_read_cb)(raft_node_t* node, void* ctx, raft_status_t status);

/**
 * Pending read request
 */
typedef struct raft_read_request {
    uint64_t read_index;        /* Commit index at time of request */
    raft_read_cb callback;      /* Callback to invoke */
    void* ctx;                  /* User context */
    int32_t acks_needed;        /* Number of heartbeat acks needed */
    int32_t acks_received;      /* Number of acks received */
    bool* acked;                /* Which nodes have acked */
    struct raft_read_request* next;
} raft_read_request_t;

/**
 * Request a linearizable read
 * The callback will be invoked when it's safe to serve the read
 *
 * @param node Raft node (must be leader)
 * @param callback Function to call when read is safe
 * @param ctx User context passed to callback
 * @return RAFT_OK if request was queued, RAFT_NOT_LEADER if not leader
 */
raft_status_t raft_read_index(raft_node_t* node, raft_read_cb callback, void* ctx);

/**
 * Process heartbeat acknowledgment for pending reads
 * Called when a heartbeat response is received
 */
void raft_read_process_ack(raft_node_t* node, int32_t from_node);

/**
 * Cancel all pending read requests (e.g., on leadership loss)
 */
void raft_read_cancel_all(raft_node_t* node);

/**
 * Get number of pending read requests
 */
size_t raft_read_pending_count(raft_node_t* node);

#endif /* RAFT_READ_H */
