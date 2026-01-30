/**
 * commit.c - Commit index management implementation
 */

#include "commit.h"
#include "raft.h"
#include "log.h"
#include <stdlib.h>

static int compare_uint64(const void* a, const void* b) {
    uint64_t va = *(const uint64_t*)a;
    uint64_t vb = *(const uint64_t*)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

uint64_t raft_get_majority_match_index(raft_node_t* node) {
    if (!node || node->role != RAFT_LEADER) return 0;
    if (!node->leader_state.match_index) return 0;

    /* Create sorted copy of match_index array */
    uint64_t* sorted = malloc(node->num_nodes * sizeof(uint64_t));
    if (!sorted) return 0;

    for (int32_t i = 0; i < node->num_nodes; i++) {
        if (i == node->node_id) {
            /* Leader's match_index is its last log index */
            sorted[i] = raft_log_last_index(node->log);
        } else {
            sorted[i] = node->leader_state.match_index[i];
        }
    }

    qsort(sorted, node->num_nodes, sizeof(uint64_t), compare_uint64);

    /* Majority index is at position (n-1)/2 when sorted descending */
    /* Or equivalently, position n/2 when sorted ascending */
    uint64_t majority_index = sorted[node->num_nodes / 2];

    free(sorted);
    return majority_index;
}

bool raft_is_committed(raft_node_t* node, uint64_t index) {
    if (!node || index == 0) return false;
    return index <= node->volatile_state.commit_index;
}

raft_status_t raft_advance_commit_index(raft_node_t* node) {
    if (!node) return RAFT_INVALID_ARG;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;

    uint64_t last_index = raft_log_last_index(node->log);
    uint64_t new_commit = node->volatile_state.commit_index;

    /* Find highest index replicated on majority */
    for (uint64_t n = node->volatile_state.commit_index + 1; n <= last_index; n++) {
        int count = 1;  /* Leader counts itself */

        for (int32_t i = 0; i < node->num_nodes; i++) {
            if (i != node->node_id &&
                node->leader_state.match_index[i] >= n) {
                count++;
            }
        }

        /* Only commit if majority AND entry is from current term */
        if (count > node->num_nodes / 2) {
            uint64_t entry_term = raft_log_term_at(node->log, n);
            if (entry_term == node->persistent.current_term) {
                new_commit = n;
            }
        }
    }

    if (new_commit > node->volatile_state.commit_index) {
        node->volatile_state.commit_index = new_commit;
        raft_apply_committed(node);
    }

    return RAFT_OK;
}
