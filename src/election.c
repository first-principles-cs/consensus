/**
 * election.c - Election logic implementation
 */

#include "election.h"
#include "raft.h"
#include "timer.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

void raft_step_down(raft_node_t* node, uint64_t new_term) {
    if (!node) return;

    node->role = RAFT_FOLLOWER;
    node->persistent.current_term = new_term;
    node->persistent.voted_for = -1;
    node->current_leader = -1;
    node->votes_received = 0;

    /* Clear votes_granted array */
    if (node->votes_granted) {
        memset(node->votes_granted, 0, node->num_nodes * sizeof(bool));
    }

    raft_reset_election_timer(node);
}

static bool is_log_up_to_date(raft_node_t* node, uint64_t last_log_term,
                               uint64_t last_log_index) {
    uint64_t my_last_term = raft_log_last_term(node->log);
    uint64_t my_last_index = raft_log_last_index(node->log);

    /* Candidate's log is up-to-date if:
     * 1. Its last term is greater than ours, OR
     * 2. Terms are equal and its index is >= ours */
    if (last_log_term > my_last_term) return true;
    if (last_log_term == my_last_term && last_log_index >= my_last_index) return true;
    return false;
}

raft_status_t raft_start_election(raft_node_t* node) {
    if (!node) return RAFT_INVALID_ARG;
    if (!node->running) return RAFT_STOPPED;

    /* Transition to candidate */
    node->role = RAFT_CANDIDATE;
    node->persistent.current_term++;
    node->persistent.voted_for = node->node_id;
    node->current_leader = -1;

    /* Reset election state */
    node->votes_received = 1;  /* Vote for self */
    memset(node->votes_granted, 0, node->num_nodes * sizeof(bool));
    node->votes_granted[node->node_id] = true;

    raft_reset_election_timer(node);

    /* Check if we already have majority (single-node cluster) */
    if (node->votes_received > node->num_nodes / 2) {
        return raft_become_leader(node);
    }

    /* Send RequestVote to all peers */
    if (node->send_fn) {
        raft_request_vote_t request = {
            .type = RAFT_MSG_REQUEST_VOTE,
            .term = node->persistent.current_term,
            .candidate_id = node->node_id,
            .last_log_index = raft_log_last_index(node->log),
            .last_log_term = raft_log_last_term(node->log),
        };

        for (int32_t i = 0; i < node->num_nodes; i++) {
            if (i != node->node_id) {
                node->send_fn(node, i, &request, sizeof(request), node->user_data);
            }
        }
    }

    return RAFT_OK;
}

raft_status_t raft_handle_request_vote(raft_node_t* node,
                                        const raft_request_vote_t* request,
                                        raft_request_vote_response_t* response) {
    if (!node || !request || !response) return RAFT_INVALID_ARG;

    response->type = RAFT_MSG_REQUEST_VOTE_RESPONSE;
    response->term = node->persistent.current_term;
    response->vote_granted = false;

    /* If request term > current term, step down */
    if (request->term > node->persistent.current_term) {
        raft_step_down(node, request->term);
        response->term = node->persistent.current_term;
    }

    /* Reject if request term < current term */
    if (request->term < node->persistent.current_term) {
        return RAFT_OK;
    }

    /* Check if we can vote for this candidate */
    bool can_vote = (node->persistent.voted_for == -1 ||
                     node->persistent.voted_for == request->candidate_id);

    /* Check if candidate's log is up-to-date */
    bool log_ok = is_log_up_to_date(node, request->last_log_term,
                                     request->last_log_index);

    if (can_vote && log_ok) {
        node->persistent.voted_for = request->candidate_id;
        response->vote_granted = true;
        raft_reset_election_timer(node);
    }

    return RAFT_OK;
}

raft_status_t raft_handle_request_vote_response(raft_node_t* node,
                                                 int32_t from_node,
                                                 const raft_request_vote_response_t* response) {
    if (!node || !response) return RAFT_INVALID_ARG;
    if (from_node < 0 || from_node >= node->num_nodes) return RAFT_INVALID_ARG;

    /* Ignore if not a candidate */
    if (node->role != RAFT_CANDIDATE) return RAFT_OK;

    /* If response term > current term, step down */
    if (response->term > node->persistent.current_term) {
        raft_step_down(node, response->term);
        return RAFT_OK;
    }

    /* Ignore stale responses */
    if (response->term < node->persistent.current_term) {
        return RAFT_OK;
    }

    /* Record vote if granted and not already counted */
    if (response->vote_granted && !node->votes_granted[from_node]) {
        node->votes_granted[from_node] = true;
        node->votes_received++;

        /* Check for majority */
        if (node->votes_received > node->num_nodes / 2) {
            return raft_become_leader(node);
        }
    }

    return RAFT_OK;
}

raft_status_t raft_handle_append_entries(raft_node_t* node,
                                          const raft_append_entries_t* request,
                                          raft_append_entries_response_t* response) {
    if (!node || !request || !response) return RAFT_INVALID_ARG;

    response->type = RAFT_MSG_APPEND_ENTRIES_RESPONSE;
    response->term = node->persistent.current_term;
    response->success = false;
    response->match_index = 0;

    /* If request term > current term, step down */
    if (request->term > node->persistent.current_term) {
        raft_step_down(node, request->term);
        response->term = node->persistent.current_term;
    }

    /* Reject if request term < current term */
    if (request->term < node->persistent.current_term) {
        return RAFT_OK;
    }

    /* Valid AppendEntries from leader - reset election timer */
    raft_reset_election_timer(node);
    node->current_leader = request->leader_id;

    /* If we're a candidate, step down to follower */
    if (node->role == RAFT_CANDIDATE) {
        node->role = RAFT_FOLLOWER;
        node->votes_received = 0;
    }

    /* For heartbeats (no entries), just acknowledge */
    if (request->entries_count == 0) {
        response->success = true;
        response->match_index = raft_log_last_index(node->log);

        /* Update commit index */
        if (request->leader_commit > node->volatile_state.commit_index) {
            uint64_t last_index = raft_log_last_index(node->log);
            node->volatile_state.commit_index =
                (request->leader_commit < last_index) ?
                request->leader_commit : last_index;
        }
    }

    return RAFT_OK;
}

raft_status_t raft_send_heartbeats(raft_node_t* node) {
    if (!node) return RAFT_INVALID_ARG;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;
    if (!node->send_fn) return RAFT_OK;

    raft_append_entries_t heartbeat = {
        .type = RAFT_MSG_APPEND_ENTRIES,
        .term = node->persistent.current_term,
        .leader_id = node->node_id,
        .prev_log_index = raft_log_last_index(node->log),
        .prev_log_term = raft_log_last_term(node->log),
        .leader_commit = node->volatile_state.commit_index,
        .entries_count = 0,
    };

    for (int32_t i = 0; i < node->num_nodes; i++) {
        if (i != node->node_id) {
            node->send_fn(node, i, &heartbeat, sizeof(heartbeat), node->user_data);
        }
    }

    return RAFT_OK;
}

raft_status_t raft_receive_message(raft_node_t* node, int32_t from_node,
                                    const void* msg, size_t msg_len) {
    if (!node || !msg) return RAFT_INVALID_ARG;
    if (msg_len < sizeof(raft_msg_type_t)) return RAFT_INVALID_ARG;

    raft_msg_type_t type = *(const raft_msg_type_t*)msg;

    switch (type) {
        case RAFT_MSG_REQUEST_VOTE: {
            if (msg_len < sizeof(raft_request_vote_t)) return RAFT_INVALID_ARG;
            raft_request_vote_response_t response;
            raft_status_t status = raft_handle_request_vote(node,
                (const raft_request_vote_t*)msg, &response);
            if (status == RAFT_OK && node->send_fn) {
                node->send_fn(node, from_node, &response, sizeof(response),
                              node->user_data);
            }
            return status;
        }

        case RAFT_MSG_REQUEST_VOTE_RESPONSE: {
            if (msg_len < sizeof(raft_request_vote_response_t)) return RAFT_INVALID_ARG;
            return raft_handle_request_vote_response(node, from_node,
                (const raft_request_vote_response_t*)msg);
        }

        case RAFT_MSG_APPEND_ENTRIES: {
            if (msg_len < sizeof(raft_append_entries_t)) return RAFT_INVALID_ARG;
            raft_append_entries_response_t response;
            raft_status_t status = raft_handle_append_entries(node,
                (const raft_append_entries_t*)msg, &response);
            if (status == RAFT_OK && node->send_fn) {
                node->send_fn(node, from_node, &response, sizeof(response),
                              node->user_data);
            }
            return status;
        }

        case RAFT_MSG_APPEND_ENTRIES_RESPONSE: {
            /* Will be handled in Phase 3 (replication) */
            return RAFT_OK;
        }

        default:
            return RAFT_INVALID_ARG;
    }
}
