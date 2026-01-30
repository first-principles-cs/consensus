/**
 * replication.c - Log replication implementation
 */

#include "replication.h"
#include "raft.h"
#include "log.h"
#include "commit.h"
#include "election.h"
#include "param.h"
#include <stdlib.h>
#include <string.h>

raft_status_t raft_replicate_to_peer(raft_node_t* node, int32_t peer_id) {
    if (!node) return RAFT_INVALID_ARG;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;
    if (peer_id < 0 || peer_id >= node->num_nodes) return RAFT_INVALID_ARG;
    if (peer_id == node->node_id) return RAFT_OK;
    if (!node->send_fn) return RAFT_OK;

    uint64_t next_idx = node->leader_state.next_index[peer_id];
    uint64_t last_idx = raft_log_last_index(node->log);

    /* Prepare AppendEntries header */
    uint64_t prev_log_index = (next_idx > 1) ? next_idx - 1 : 0;
    uint64_t prev_log_term = raft_log_term_at(node->log, prev_log_index);

    /* Count entries to send */
    uint32_t entries_count = 0;
    if (last_idx >= next_idx) {
        entries_count = (uint32_t)(last_idx - next_idx + 1);
        if (entries_count > RAFT_MAX_ENTRIES_PER_APPEND) {
            entries_count = RAFT_MAX_ENTRIES_PER_APPEND;
        }
    }

    /* Calculate message size */
    size_t msg_size = sizeof(raft_append_entries_t);
    for (uint32_t i = 0; i < entries_count; i++) {
        const raft_entry_t* entry = raft_log_get(node->log, next_idx + i);
        if (entry) {
            msg_size += sizeof(uint64_t) + sizeof(uint32_t) + entry->command_len;
        }
    }

    /* Allocate and fill message */
    void* msg = malloc(msg_size);
    if (!msg) return RAFT_NO_MEMORY;

    raft_append_entries_t* header = (raft_append_entries_t*)msg;
    header->type = RAFT_MSG_APPEND_ENTRIES;
    header->term = node->persistent.current_term;
    header->leader_id = node->node_id;
    header->prev_log_index = prev_log_index;
    header->prev_log_term = prev_log_term;
    header->leader_commit = node->volatile_state.commit_index;
    header->entries_count = entries_count;

    /* Serialize entries after header */
    char* ptr = (char*)msg + sizeof(raft_append_entries_t);
    for (uint32_t i = 0; i < entries_count; i++) {
        const raft_entry_t* entry = raft_log_get(node->log, next_idx + i);
        if (entry) {
            /* Write term */
            memcpy(ptr, &entry->term, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            /* Write command length */
            uint32_t len = (uint32_t)entry->command_len;
            memcpy(ptr, &len, sizeof(uint32_t));
            ptr += sizeof(uint32_t);
            /* Write command data */
            memcpy(ptr, entry->command, entry->command_len);
            ptr += entry->command_len;
        }
    }

    node->send_fn(node, peer_id, msg, msg_size, node->user_data);
    free(msg);

    return RAFT_OK;
}

raft_status_t raft_replicate_log(raft_node_t* node) {
    if (!node) return RAFT_INVALID_ARG;
    if (node->role != RAFT_LEADER) return RAFT_NOT_LEADER;

    for (int32_t i = 0; i < node->num_nodes; i++) {
        if (i != node->node_id) {
            raft_replicate_to_peer(node, i);
        }
    }

    return RAFT_OK;
}

raft_status_t raft_handle_append_entries_response(
    raft_node_t* node,
    int32_t from_node,
    const raft_append_entries_response_t* response) {

    if (!node || !response) return RAFT_INVALID_ARG;
    if (from_node < 0 || from_node >= node->num_nodes) return RAFT_INVALID_ARG;
    if (node->role != RAFT_LEADER) return RAFT_OK;

    /* Step down if response has higher term */
    if (response->term > node->persistent.current_term) {
        raft_step_down(node, response->term);
        return RAFT_OK;
    }

    /* Ignore stale responses */
    if (response->term < node->persistent.current_term) {
        return RAFT_OK;
    }

    if (response->success) {
        /* Update match_index and next_index */
        if (response->match_index > node->leader_state.match_index[from_node]) {
            node->leader_state.match_index[from_node] = response->match_index;
            node->leader_state.next_index[from_node] = response->match_index + 1;
        }
        /* Try to advance commit index */
        raft_advance_commit_index(node);
    } else {
        /* Decrement next_index and retry */
        if (node->leader_state.next_index[from_node] > 1) {
            node->leader_state.next_index[from_node]--;
        }
    }

    return RAFT_OK;
}

raft_status_t raft_handle_append_entries_with_log(
    raft_node_t* node,
    const void* msg,
    size_t msg_len,
    raft_append_entries_response_t* response) {

    if (!node || !msg || !response) return RAFT_INVALID_ARG;
    if (msg_len < sizeof(raft_append_entries_t)) return RAFT_INVALID_ARG;

    const raft_append_entries_t* request = (const raft_append_entries_t*)msg;

    response->type = RAFT_MSG_APPEND_ENTRIES_RESPONSE;
    response->term = node->persistent.current_term;
    response->success = false;
    response->match_index = 0;

    /* Step down if request has higher term */
    if (request->term > node->persistent.current_term) {
        raft_step_down(node, request->term);
        response->term = node->persistent.current_term;
    }

    /* Reject if request term < current term */
    if (request->term < node->persistent.current_term) {
        return RAFT_OK;
    }

    /* Valid AppendEntries - reset election timer */
    node->election_timer_ms = 0;
    node->current_leader = request->leader_id;

    /* Step down from candidate if we receive AppendEntries */
    if (node->role == RAFT_CANDIDATE) {
        node->role = RAFT_FOLLOWER;
        node->votes_received = 0;
    }

    /* Log consistency check */
    if (request->prev_log_index > 0) {
        uint64_t term_at_prev = raft_log_term_at(node->log, request->prev_log_index);
        if (term_at_prev == 0 || term_at_prev != request->prev_log_term) {
            /* Log doesn't contain entry at prev_log_index with matching term */
            response->match_index = raft_log_last_index(node->log);
            return RAFT_OK;
        }
    }

    /* Process entries if any */
    if (request->entries_count > 0) {
        const char* ptr = (const char*)msg + sizeof(raft_append_entries_t);
        const char* end = (const char*)msg + msg_len;

        for (uint32_t i = 0; i < request->entries_count && ptr < end; i++) {
            uint64_t entry_index = request->prev_log_index + 1 + i;

            /* Read entry term */
            if (ptr + sizeof(uint64_t) > end) break;
            uint64_t entry_term;
            memcpy(&entry_term, ptr, sizeof(uint64_t));
            ptr += sizeof(uint64_t);

            /* Read command length */
            if (ptr + sizeof(uint32_t) > end) break;
            uint32_t cmd_len;
            memcpy(&cmd_len, ptr, sizeof(uint32_t));
            ptr += sizeof(uint32_t);

            /* Read command data */
            if (ptr + cmd_len > end) break;

            /* Check for conflict */
            uint64_t existing_term = raft_log_term_at(node->log, entry_index);
            if (existing_term != 0 && existing_term != entry_term) {
                /* Conflict - delete this and all following entries */
                raft_log_truncate_after(node->log, entry_index - 1);
            }

            /* Append if not already present */
            if (entry_index > raft_log_last_index(node->log)) {
                raft_log_append(node->log, entry_term, ptr, cmd_len, NULL);
            }

            ptr += cmd_len;
        }
    }

    /* Update commit index */
    if (request->leader_commit > node->volatile_state.commit_index) {
        uint64_t last_new_index = request->prev_log_index + request->entries_count;
        uint64_t last_log = raft_log_last_index(node->log);
        uint64_t new_commit = request->leader_commit;
        if (last_new_index < new_commit) new_commit = last_new_index;
        if (last_log < new_commit) new_commit = last_log;
        node->volatile_state.commit_index = new_commit;
        raft_apply_committed(node);
    }

    response->success = true;
    response->match_index = raft_log_last_index(node->log);

    return RAFT_OK;
}
