/**
 * read.c - ReadIndex implementation (Phase 6)
 */

#include "read.h"
#include "raft.h"
#include <stdlib.h>
#include <string.h>

/* Global pending reads list (per-node in production, simplified here) */
static raft_read_request_t* g_pending_reads = NULL;

raft_status_t raft_read_index(raft_node_t* node, raft_read_cb callback, void* ctx) {
    if (!node || !callback) return RAFT_INVALID_ARG;
    if (!node->running) return RAFT_STOPPED;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;

    /* For single-node cluster, can serve immediately */
    if (node->num_nodes == 1) {
        callback(node, ctx, RAFT_OK);
        return RAFT_OK;
    }

    /* Create read request */
    raft_read_request_t* req = calloc(1, sizeof(raft_read_request_t));
    if (!req) return RAFT_NO_MEMORY;

    req->read_index = node->volatile_state.commit_index;
    req->callback = callback;
    req->ctx = ctx;
    req->acks_needed = node->num_nodes / 2;  /* Need majority (excluding self) */
    req->acks_received = 0;
    req->acked = calloc(node->num_nodes, sizeof(bool));
    if (!req->acked) {
        free(req);
        return RAFT_NO_MEMORY;
    }
    req->next = NULL;

    /* Add to pending list */
    if (!g_pending_reads) {
        g_pending_reads = req;
    } else {
        raft_read_request_t* tail = g_pending_reads;
        while (tail->next) tail = tail->next;
        tail->next = req;
    }

    return RAFT_OK;
}

void raft_read_process_ack(raft_node_t* node, int32_t from_node) {
    if (!node || from_node < 0 || from_node >= node->num_nodes) return;
    if (node->role != RAFT_LEADER) return;

    raft_read_request_t* prev = NULL;
    raft_read_request_t* req = g_pending_reads;

    while (req) {
        raft_read_request_t* next = req->next;

        /* Count this ack if not already counted */
        if (!req->acked[from_node]) {
            req->acked[from_node] = true;
            req->acks_received++;
        }

        /* Check if we have enough acks */
        if (req->acks_received >= req->acks_needed) {
            /* Remove from list */
            if (prev) {
                prev->next = next;
            } else {
                g_pending_reads = next;
            }

            /* Invoke callback */
            req->callback(node, req->ctx, RAFT_OK);

            /* Free request */
            free(req->acked);
            free(req);
        } else {
            prev = req;
        }

        req = next;
    }
}

void raft_read_cancel_all(raft_node_t* node) {
    raft_read_request_t* req = g_pending_reads;

    while (req) {
        raft_read_request_t* next = req->next;

        /* Invoke callback with error */
        if (req->callback) {
            req->callback(node, req->ctx, RAFT_NOT_LEADER);
        }

        free(req->acked);
        free(req);
        req = next;
    }

    g_pending_reads = NULL;
}

size_t raft_read_pending_count(raft_node_t* node) {
    (void)node;
    size_t count = 0;
    raft_read_request_t* req = g_pending_reads;
    while (req) {
        count++;
        req = req->next;
    }
    return count;
}

/* Reset function for testing */
void raft_read_reset(void) {
    raft_read_request_t* req = g_pending_reads;
    while (req) {
        raft_read_request_t* next = req->next;
        free(req->acked);
        free(req);
        req = next;
    }
    g_pending_reads = NULL;
}
