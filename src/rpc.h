/**
 * rpc.h - RPC message structures for Raft consensus
 *
 * Defines message types and structures for RequestVote and AppendEntries RPCs.
 */

#ifndef RAFT_RPC_H
#define RAFT_RPC_H

#include "types.h"

/**
 * RPC message types
 */
typedef enum {
    RAFT_MSG_REQUEST_VOTE = 1,
    RAFT_MSG_REQUEST_VOTE_RESPONSE = 2,
    RAFT_MSG_APPEND_ENTRIES = 3,
    RAFT_MSG_APPEND_ENTRIES_RESPONSE = 4,
    RAFT_MSG_INSTALL_SNAPSHOT = 5,
    RAFT_MSG_INSTALL_SNAPSHOT_RESPONSE = 6,
    RAFT_MSG_PRE_VOTE = 7,
    RAFT_MSG_PRE_VOTE_RESPONSE = 8,
    RAFT_MSG_TIMEOUT_NOW = 9,
} raft_msg_type_t;

/**
 * RequestVote RPC request
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Candidate's term */
    int32_t candidate_id;       /* Candidate requesting vote */
    uint64_t last_log_index;    /* Index of candidate's last log entry */
    uint64_t last_log_term;     /* Term of candidate's last log entry */
} raft_request_vote_t;

/**
 * RequestVote RPC response
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Current term, for candidate to update itself */
    bool vote_granted;          /* True means candidate received vote */
} raft_request_vote_response_t;

/**
 * AppendEntries RPC request (heartbeat when entries_count == 0)
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Leader's term */
    int32_t leader_id;          /* So follower can redirect clients */
    uint64_t prev_log_index;    /* Index of log entry immediately preceding new ones */
    uint64_t prev_log_term;     /* Term of prev_log_index entry */
    uint64_t leader_commit;     /* Leader's commit index */
    uint32_t entries_count;     /* Number of entries (0 for heartbeat) */
} raft_append_entries_t;

/**
 * AppendEntries RPC response
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Current term, for leader to update itself */
    bool success;               /* True if follower contained entry matching prev_log */
    uint64_t match_index;       /* Highest index known to be replicated */
} raft_append_entries_response_t;

/**
 * InstallSnapshot RPC request
 * Used to send snapshots to followers that are too far behind
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Leader's term */
    int32_t leader_id;          /* So follower can redirect clients */
    uint64_t last_index;        /* Index of last entry included in snapshot */
    uint64_t last_term;         /* Term of last entry included in snapshot */
    uint64_t offset;            /* Byte offset for chunked transfer */
    uint32_t data_len;          /* Length of data in this chunk */
    bool done;                  /* True if this is the last chunk */
    /* Snapshot data follows this header */
} raft_install_snapshot_t;

/**
 * InstallSnapshot RPC response
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Current term, for leader to update itself */
    bool success;               /* True if snapshot was accepted */
} raft_install_snapshot_response_t;

/**
 * PreVote RPC request (Phase 6)
 * Used to check if a candidate would win an election before incrementing term
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Candidate's term (not incremented yet) */
    int32_t candidate_id;       /* Candidate requesting pre-vote */
    uint64_t last_log_index;    /* Index of candidate's last log entry */
    uint64_t last_log_term;     /* Term of candidate's last log entry */
} raft_pre_vote_t;

/**
 * PreVote RPC response
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Current term */
    bool vote_granted;          /* True means candidate would receive vote */
} raft_pre_vote_response_t;

/**
 * TimeoutNow RPC (Phase 6)
 * Sent by leader to target node during leadership transfer
 */
typedef struct {
    raft_msg_type_t type;
    uint64_t term;              /* Leader's term */
    int32_t leader_id;          /* Leader sending the message */
} raft_timeout_now_t;

#endif /* RAFT_RPC_H */
