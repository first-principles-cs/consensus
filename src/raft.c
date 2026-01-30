/**
 * raft.c - Core Raft node implementation
 */

#include "raft.h"
#include <stdlib.h>
#include <string.h>

raft_node_t* raft_create(const raft_config_t* config) {
    if (!config || config->node_id < 0 || config->num_nodes < 1) {
        return NULL;
    }

    raft_node_t* node = calloc(1, sizeof(raft_node_t));
    if (!node) return NULL;

    node->node_id = config->node_id;
    node->num_nodes = config->num_nodes;
    node->apply_fn = config->apply_fn;
    node->send_fn = config->send_fn;
    node->user_data = config->user_data;

    node->role = RAFT_FOLLOWER;
    node->persistent.current_term = 0;
    node->persistent.voted_for = -1;

    node->volatile_state.commit_index = 0;
    node->volatile_state.last_applied = 0;

    node->leader_state.next_index = NULL;
    node->leader_state.match_index = NULL;

    node->log = raft_log_create();
    if (!node->log) {
        free(node);
        return NULL;
    }

    node->running = false;
    node->current_leader = -1;

    /* Election state */
    node->votes_received = 0;
    node->votes_granted = calloc(config->num_nodes, sizeof(bool));
    if (!node->votes_granted) {
        raft_log_destroy(node->log);
        free(node);
        return NULL;
    }

    /* Timer state */
    node->election_timeout_ms = 0;
    node->election_timer_ms = 0;
    node->heartbeat_timer_ms = 0;

    return node;
}

void raft_destroy(raft_node_t* node) {
    if (!node) return;

    raft_log_destroy(node->log);
    free(node->leader_state.next_index);
    free(node->leader_state.match_index);
    free(node->votes_granted);
    free(node);
}

raft_status_t raft_start(raft_node_t* node) {
    if (!node) return RAFT_INVALID_ARG;
    if (node->running) return RAFT_OK;

    node->running = true;

    /* Single-node cluster becomes leader immediately */
    if (node->num_nodes == 1) {
        return raft_become_leader(node);
    }

    return RAFT_OK;
}

raft_status_t raft_stop(raft_node_t* node) {
    if (!node) return RAFT_INVALID_ARG;
    node->running = false;
    return RAFT_OK;
}

raft_status_t raft_propose(raft_node_t* node, const char* command,
                           size_t command_len, uint64_t* out_index) {
    if (!node) return RAFT_INVALID_ARG;
    if (!node->running) return RAFT_STOPPED;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;

    uint64_t index;
    raft_status_t status = raft_log_append(node->log, node->persistent.current_term,
                                           command, command_len, &index);
    if (status != RAFT_OK) return status;

    if (out_index) *out_index = index;

    /* For single-node cluster, entry is committed immediately */
    if (node->num_nodes == 1) {
        node->volatile_state.commit_index = index;
    }

    return RAFT_OK;
}

bool raft_is_leader(raft_node_t* node) {
    return node && node->role == RAFT_LEADER;
}

int32_t raft_get_leader(raft_node_t* node) {
    if (!node) return -1;
    if (node->role == RAFT_LEADER) return node->node_id;
    return node->current_leader;
}

uint64_t raft_get_term(raft_node_t* node) {
    return node ? node->persistent.current_term : 0;
}

raft_role_t raft_get_role(raft_node_t* node) {
    return node ? node->role : RAFT_FOLLOWER;
}

uint64_t raft_get_commit_index(raft_node_t* node) {
    return node ? node->volatile_state.commit_index : 0;
}

uint64_t raft_get_last_applied(raft_node_t* node) {
    return node ? node->volatile_state.last_applied : 0;
}

raft_log_t* raft_get_log(raft_node_t* node) {
    return node ? node->log : NULL;
}

raft_status_t raft_become_leader(raft_node_t* node) {
    if (!node) return RAFT_INVALID_ARG;

    node->role = RAFT_LEADER;
    node->current_leader = node->node_id;

    /* Initialize leader state */
    free(node->leader_state.next_index);
    free(node->leader_state.match_index);

    node->leader_state.next_index = calloc(node->num_nodes, sizeof(uint64_t));
    node->leader_state.match_index = calloc(node->num_nodes, sizeof(uint64_t));

    if (!node->leader_state.next_index || !node->leader_state.match_index) {
        free(node->leader_state.next_index);
        free(node->leader_state.match_index);
        node->leader_state.next_index = NULL;
        node->leader_state.match_index = NULL;
        return RAFT_NO_MEMORY;
    }

    uint64_t last_index = raft_log_last_index(node->log);
    for (int32_t i = 0; i < node->num_nodes; i++) {
        node->leader_state.next_index[i] = last_index + 1;
        node->leader_state.match_index[i] = 0;
    }

    /* For single-node cluster, commit index advances immediately */
    if (node->num_nodes == 1) {
        node->volatile_state.commit_index = last_index;
    }

    return RAFT_OK;
}

void raft_apply_committed(raft_node_t* node) {
    if (!node || !node->apply_fn) return;

    while (node->volatile_state.last_applied < node->volatile_state.commit_index) {
        node->volatile_state.last_applied++;
        const raft_entry_t* entry = raft_log_get(node->log,
                                                  node->volatile_state.last_applied);
        if (entry) {
            node->apply_fn(node, entry, node->user_data);
        }
    }
}
