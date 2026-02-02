/**
 * transfer.c - Leadership transfer implementation (Phase 6)
 */

#include "transfer.h"
#include "raft.h"
#include "rpc.h"
#include "log.h"
#include <stdlib.h>

/* Global transfer state (per-node in production, simplified here) */
static raft_transfer_state_t g_transfer_state = RAFT_TRANSFER_NONE;
static int32_t g_transfer_target = -1;

raft_status_t raft_transfer_leadership(raft_node_t* node, int32_t target_id) {
    if (!node) return RAFT_INVALID_ARG;
    if (!node->running) return RAFT_STOPPED;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;

    /* Can't transfer to self */
    if (target_id == node->node_id) return RAFT_INVALID_ARG;

    /* Validate target if specified */
    if (target_id >= 0 && target_id >= node->num_nodes) {
        return RAFT_INVALID_ARG;
    }

    /* If target not specified, pick the most up-to-date follower */
    if (target_id < 0) {
        uint64_t best_match = 0;
        for (int32_t i = 0; i < node->num_nodes; i++) {
            if (i != node->node_id) {
                uint64_t match = node->leader_state.match_index[i];
                if (match > best_match) {
                    best_match = match;
                    target_id = i;
                }
            }
        }
        /* If no followers, can't transfer */
        if (target_id < 0) {
            return RAFT_INVALID_ARG;
        }
    }

    g_transfer_state = RAFT_TRANSFER_PENDING;
    g_transfer_target = target_id;

    /* Check if target is already caught up */
    raft_transfer_check_progress(node);

    return RAFT_OK;
}

void raft_transfer_abort(raft_node_t* node) {
    (void)node;
    g_transfer_state = RAFT_TRANSFER_NONE;
    g_transfer_target = -1;
}

bool raft_transfer_in_progress(raft_node_t* node) {
    (void)node;
    return g_transfer_state != RAFT_TRANSFER_NONE;
}

int32_t raft_transfer_target(raft_node_t* node) {
    (void)node;
    return g_transfer_target;
}

void raft_transfer_check_progress(raft_node_t* node) {
    if (!node || g_transfer_state == RAFT_TRANSFER_NONE) return;
    if (node->role != RAFT_LEADER) {
        raft_transfer_abort(node);
        return;
    }

    int32_t target = g_transfer_target;
    if (target < 0 || target >= node->num_nodes) {
        raft_transfer_abort(node);
        return;
    }

    /* Check if target is caught up */
    uint64_t last_index = raft_log_last_index(node->log);
    uint64_t target_match = node->leader_state.match_index[target];

    if (target_match >= last_index) {
        /* Target is caught up - send TimeoutNow */
        if (node->send_fn) {
            raft_timeout_now_t msg = {
                .type = RAFT_MSG_TIMEOUT_NOW,
                .term = node->persistent.current_term,
                .leader_id = node->node_id,
            };
            node->send_fn(node, target, &msg, sizeof(msg), node->user_data);
        }

        g_transfer_state = RAFT_TRANSFER_SENDING;
    }
}

/* Reset function for testing */
void raft_transfer_reset(void) {
    g_transfer_state = RAFT_TRANSFER_NONE;
    g_transfer_target = -1;
}
