/**
 * types.h - Status codes and type definitions for Raft consensus
 *
 * Defines common types used throughout the Raft implementation.
 */

#ifndef RAFT_TYPES_H
#define RAFT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Status codes for Raft operations
 */
typedef enum {
    RAFT_OK = 0,
    RAFT_NOT_LEADER = 1,
    RAFT_NOT_FOUND = 2,
    RAFT_IO_ERROR = 3,
    RAFT_INVALID_ARG = 4,
    RAFT_NO_MEMORY = 5,
    RAFT_CORRUPTION = 6,
    RAFT_STOPPED = 7,
} raft_status_t;

/**
 * Node role in the Raft cluster
 */
typedef enum {
    RAFT_FOLLOWER = 0,
    RAFT_CANDIDATE = 1,
    RAFT_LEADER = 2,
    RAFT_PRE_CANDIDATE = 3,     /* PreVote phase (Phase 6) */
} raft_role_t;

/**
 * Log entry type
 */
typedef enum {
    RAFT_ENTRY_COMMAND = 0,     /* Normal command */
    RAFT_ENTRY_CONFIG = 1,      /* Configuration change */
    RAFT_ENTRY_NOOP = 2,        /* No-op (new leader commit) */
} raft_entry_type_t;

/**
 * Log entry
 */
typedef struct raft_entry {
    uint64_t term;            /* Term when entry was received */
    uint64_t index;           /* Log index (1-based) */
    raft_entry_type_t type;   /* Entry type */
    char* command;            /* Command data */
    size_t command_len;       /* Length of command data */
} raft_entry_t;

/**
 * Persistent state (must be saved to stable storage before responding to RPCs)
 */
typedef struct raft_persistent {
    uint64_t current_term;  /* Latest term server has seen */
    int32_t voted_for;      /* CandidateId that received vote (-1 if none) */
} raft_persistent_t;

/**
 * Volatile state on all servers
 */
typedef struct raft_volatile {
    uint64_t commit_index;  /* Index of highest log entry known to be committed */
    uint64_t last_applied;  /* Index of highest log entry applied to state machine */
} raft_volatile_t;

/**
 * Volatile state on leaders (reinitialized after election)
 */
typedef struct raft_leader_state {
    uint64_t* next_index;   /* For each server, index of next log entry to send */
    uint64_t* match_index;  /* For each server, index of highest log entry known replicated */
} raft_leader_state_t;

/* Forward declarations */
typedef struct raft_log raft_log_t;
typedef struct raft_node raft_node_t;
typedef struct raft_config raft_config_t;

/**
 * Callback for applying committed entries to state machine
 */
typedef void (*raft_apply_fn)(raft_node_t* node, const raft_entry_t* entry, void* user_data);

/**
 * Callback for sending RPC messages
 */
typedef void (*raft_send_fn)(raft_node_t* node, int32_t peer_id, const void* msg,
                             size_t msg_len, void* user_data);

/**
 * Raft configuration
 */
struct raft_config {
    int32_t node_id;          /* This node's ID */
    int32_t num_nodes;        /* Total number of nodes in cluster */
    raft_apply_fn apply_fn;   /* State machine apply callback */
    raft_send_fn send_fn;     /* RPC send callback */
    void* user_data;          /* User data passed to callbacks */
    const char* data_dir;     /* Data directory for persistence (NULL = no persistence) */
};

#endif /* RAFT_TYPES_H */
