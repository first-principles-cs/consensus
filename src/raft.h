/**
 * raft.h - Core Raft node interface
 *
 * Defines the main Raft node structure and operations.
 */

#ifndef RAFT_H
#define RAFT_H

#include "types.h"
#include "log.h"

/* Forward declaration for storage */
typedef struct raft_storage raft_storage_t;

/**
 * Raft node structure
 */
struct raft_node {
    /* Configuration */
    int32_t node_id;
    int32_t num_nodes;
    raft_apply_fn apply_fn;
    raft_send_fn send_fn;
    void* user_data;

    /* Current role */
    raft_role_t role;

    /* Persistent state */
    raft_persistent_t persistent;

    /* Volatile state */
    raft_volatile_t volatile_state;

    /* Leader state (only valid when role == RAFT_LEADER) */
    raft_leader_state_t leader_state;

    /* Log */
    raft_log_t* log;

    /* Running state */
    bool running;

    /* Current leader (-1 if unknown) */
    int32_t current_leader;

    /* Election state */
    int32_t votes_received;     /* Number of votes received in current election */
    bool* votes_granted;        /* Array tracking which nodes granted votes */

    /* Timer state */
    uint64_t election_timeout_ms;   /* Current election timeout */
    uint64_t election_timer_ms;     /* Time since last election reset */
    uint64_t heartbeat_timer_ms;    /* Time since last heartbeat (leader only) */

    /* Persistence (Phase 4+) */
    raft_storage_t* storage;    /* Persistent storage (NULL if not enabled) */
    char* data_dir;             /* Data directory path */
};

/**
 * Create a new Raft node
 */
raft_node_t* raft_create(const raft_config_t* config);

/**
 * Destroy a Raft node
 */
void raft_destroy(raft_node_t* node);

/**
 * Start the Raft node
 */
raft_status_t raft_start(raft_node_t* node);

/**
 * Stop the Raft node
 */
raft_status_t raft_stop(raft_node_t* node);

/**
 * Propose a command to the cluster
 * Only succeeds if this node is the leader
 * Returns the log index of the proposed entry
 */
raft_status_t raft_propose(raft_node_t* node, const char* command,
                           size_t command_len, uint64_t* out_index);

/**
 * Check if this node is the leader
 */
bool raft_is_leader(raft_node_t* node);

/**
 * Get the current leader's node ID (-1 if unknown)
 */
int32_t raft_get_leader(raft_node_t* node);

/**
 * Get the current term
 */
uint64_t raft_get_term(raft_node_t* node);

/**
 * Get the current role
 */
raft_role_t raft_get_role(raft_node_t* node);

/**
 * Get the commit index
 */
uint64_t raft_get_commit_index(raft_node_t* node);

/**
 * Get the last applied index
 */
uint64_t raft_get_last_applied(raft_node_t* node);

/**
 * Get the log
 */
raft_log_t* raft_get_log(raft_node_t* node);

/**
 * Become leader (for single-node cluster or testing)
 */
raft_status_t raft_become_leader(raft_node_t* node);

/**
 * Apply committed entries to state machine
 */
void raft_apply_committed(raft_node_t* node);

#endif /* RAFT_H */
