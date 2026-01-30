/**
 * param.h - Default parameters for Raft consensus
 *
 * Defines tunable parameters with sensible defaults.
 */

#ifndef RAFT_PARAM_H
#define RAFT_PARAM_H

/* Election timeout range in milliseconds */
#define RAFT_ELECTION_TIMEOUT_MIN_MS  150
#define RAFT_ELECTION_TIMEOUT_MAX_MS  300

/* Heartbeat interval in milliseconds (should be << election timeout) */
#define RAFT_HEARTBEAT_INTERVAL_MS    50

/* Maximum entries per AppendEntries RPC */
#define RAFT_MAX_ENTRIES_PER_APPEND   100

/* Maximum log entries before compaction */
#define RAFT_LOG_COMPACTION_THRESHOLD 10000

/* Initial log capacity */
#define RAFT_LOG_INITIAL_CAPACITY     64

/* Maximum command size in bytes */
#define RAFT_MAX_COMMAND_SIZE         (1024 * 1024)

#endif /* RAFT_PARAM_H */
