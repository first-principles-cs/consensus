# Raft Consensus API Reference

This document provides a comprehensive reference for all public APIs in the Raft consensus implementation.

## Table of Contents

1. [Types and Definitions](#types-and-definitions)
2. [Core Raft Node](#core-raft-node)
3. [Log Management](#log-management)
4. [Election](#election)
5. [Timer](#timer)
6. [Replication](#replication)
7. [Commit](#commit)
8. [Storage](#storage)
9. [Snapshot](#snapshot)
10. [Recovery](#recovery)
11. [Membership](#membership)
12. [Batch Operations](#batch-operations)
13. [ReadIndex](#readindex)
14. [Leadership Transfer](#leadership-transfer)

---

## Types and Definitions

### Status Codes (`types.h`)

```c
typedef enum {
    RAFT_OK = 0,           // Operation succeeded
    RAFT_NOT_LEADER = 1,   // Node is not the leader
    RAFT_NOT_FOUND = 2,    // Entry not found
    RAFT_IO_ERROR = 3,     // I/O operation failed
    RAFT_INVALID_ARG = 4,  // Invalid argument
    RAFT_NO_MEMORY = 5,    // Memory allocation failed
    RAFT_CORRUPTION = 6,   // Data corruption detected
    RAFT_STOPPED = 7,      // Node is stopped
} raft_status_t;
```

### Node Roles

```c
typedef enum {
    RAFT_FOLLOWER = 0,      // Following a leader
    RAFT_CANDIDATE = 1,     // Running for election
    RAFT_LEADER = 2,        // Cluster leader
    RAFT_PRE_CANDIDATE = 3, // PreVote phase
} raft_role_t;
```

### Log Entry Types

```c
typedef enum {
    RAFT_ENTRY_COMMAND = 0, // Normal command
    RAFT_ENTRY_CONFIG = 1,  // Configuration change
    RAFT_ENTRY_NOOP = 2,    // No-op (leader commit)
} raft_entry_type_t;
```

### Log Entry Structure

```c
typedef struct raft_entry {
    uint64_t term;            // Term when entry was received
    uint64_t index;           // Log index (1-based)
    raft_entry_type_t type;   // Entry type
    char* command;            // Command data
    size_t command_len;       // Length of command data
} raft_entry_t;
```

### Configuration Structure

```c
struct raft_config {
    int32_t node_id;          // This node's ID
    int32_t num_nodes;        // Total number of nodes in cluster
    raft_apply_fn apply_fn;   // State machine apply callback
    raft_send_fn send_fn;     // RPC send callback
    void* user_data;          // User data passed to callbacks
    const char* data_dir;     // Data directory for persistence
};
```

### Callbacks

```c
// Called when an entry is committed and should be applied
typedef void (*raft_apply_fn)(raft_node_t* node,
                               const raft_entry_t* entry,
                               void* user_data);

// Called to send an RPC message to a peer
typedef void (*raft_send_fn)(raft_node_t* node,
                              int32_t peer_id,
                              const void* msg,
                              size_t msg_len,
                              void* user_data);
```

---

## Core Raft Node

### raft_create

```c
raft_node_t* raft_create(const raft_config_t* config);
```

Creates a new Raft node with the given configuration.

**Parameters:**
- `config`: Configuration structure (must not be NULL)

**Returns:**
- Pointer to new node on success
- NULL on failure (invalid config or memory allocation failure)

**Example:**
```c
raft_config_t config = {
    .node_id = 0,
    .num_nodes = 3,
    .apply_fn = my_apply_callback,
    .send_fn = my_send_callback,
    .user_data = &my_context,
    .data_dir = "/var/lib/raft",
};
raft_node_t* node = raft_create(&config);
```

### raft_destroy

```c
void raft_destroy(raft_node_t* node);
```

Destroys a Raft node and frees all resources.

**Parameters:**
- `node`: Node to destroy (may be NULL)

### raft_start

```c
raft_status_t raft_start(raft_node_t* node);
```

Starts the Raft node. For single-node clusters, immediately becomes leader.

**Parameters:**
- `node`: Node to start

**Returns:**
- `RAFT_OK` on success
- `RAFT_INVALID_ARG` if node is NULL

### raft_stop

```c
raft_status_t raft_stop(raft_node_t* node);
```

Stops the Raft node.

**Parameters:**
- `node`: Node to stop

**Returns:**
- `RAFT_OK` on success
- `RAFT_INVALID_ARG` if node is NULL

### raft_propose

```c
raft_status_t raft_propose(raft_node_t* node,
                           const char* command,
                           size_t command_len,
                           uint64_t* out_index);
```

Proposes a command to the cluster. Only succeeds if this node is the leader.

**Parameters:**
- `node`: Raft node
- `command`: Command data
- `command_len`: Length of command
- `out_index`: Output parameter for log index (may be NULL)

**Returns:**
- `RAFT_OK` on success
- `RAFT_NOT_LEADER` if not the leader
- `RAFT_STOPPED` if node is stopped

### raft_is_leader

```c
bool raft_is_leader(raft_node_t* node);
```

Checks if this node is the leader.

**Parameters:**
- `node`: Raft node

**Returns:**
- `true` if leader, `false` otherwise

### raft_get_leader

```c
int32_t raft_get_leader(raft_node_t* node);
```

Gets the current leader's node ID.

**Parameters:**
- `node`: Raft node

**Returns:**
- Leader's node ID, or -1 if unknown

### raft_get_term

```c
uint64_t raft_get_term(raft_node_t* node);
```

Gets the current term.

### raft_get_role

```c
raft_role_t raft_get_role(raft_node_t* node);
```

Gets the current role (FOLLOWER, CANDIDATE, LEADER, PRE_CANDIDATE).

### raft_get_commit_index

```c
uint64_t raft_get_commit_index(raft_node_t* node);
```

Gets the commit index (highest log entry known to be committed).

### raft_get_last_applied

```c
uint64_t raft_get_last_applied(raft_node_t* node);
```

Gets the last applied index (highest log entry applied to state machine).

---

## Log Management

### raft_log_create

```c
raft_log_t* raft_log_create(void);
```

Creates a new empty log.

**Returns:**
- Pointer to new log, or NULL on failure

### raft_log_destroy

```c
void raft_log_destroy(raft_log_t* log);
```

Destroys a log and frees all entries.

### raft_log_append

```c
raft_status_t raft_log_append(raft_log_t* log,
                               uint64_t term,
                               const char* command,
                               size_t command_len,
                               uint64_t* out_index);
```

Appends an entry to the log.

**Parameters:**
- `log`: Log to append to
- `term`: Term of the entry
- `command`: Command data
- `command_len`: Length of command
- `out_index`: Output parameter for assigned index

**Returns:**
- `RAFT_OK` on success
- `RAFT_NO_MEMORY` on allocation failure

### raft_log_get

```c
const raft_entry_t* raft_log_get(raft_log_t* log, uint64_t index);
```

Gets an entry by index (1-based).

**Parameters:**
- `log`: Log to query
- `index`: Entry index

**Returns:**
- Pointer to entry, or NULL if not found

### raft_log_truncate_after

```c
raft_status_t raft_log_truncate_after(raft_log_t* log, uint64_t after_index);
```

Removes all entries with index > after_index.

### raft_log_truncate_before

```c
raft_status_t raft_log_truncate_before(raft_log_t* log, uint64_t before_index);
```

Removes all entries with index < before_index (for compaction).

### raft_log_last_index

```c
uint64_t raft_log_last_index(raft_log_t* log);
```

Gets the index of the last entry (0 if empty).

### raft_log_last_term

```c
uint64_t raft_log_last_term(raft_log_t* log);
```

Gets the term of the last entry (0 if empty).

### raft_log_count

```c
size_t raft_log_count(raft_log_t* log);
```

Gets the number of entries in the log.

---

## Election

### raft_start_election

```c
raft_status_t raft_start_election(raft_node_t* node);
```

Starts a new election. Transitions to candidate, increments term, votes for self.

**Returns:**
- `RAFT_OK` on success
- `RAFT_STOPPED` if node is stopped

### raft_start_pre_vote

```c
raft_status_t raft_start_pre_vote(raft_node_t* node);
```

Starts a pre-vote phase. Checks if we would win before incrementing term.

### raft_handle_request_vote

```c
raft_status_t raft_handle_request_vote(raft_node_t* node,
                                        const raft_request_vote_t* request,
                                        raft_request_vote_response_t* response);
```

Handles an incoming RequestVote RPC.

### raft_handle_append_entries

```c
raft_status_t raft_handle_append_entries(raft_node_t* node,
                                          const raft_append_entries_t* request,
                                          raft_append_entries_response_t* response);
```

Handles an incoming AppendEntries RPC (heartbeat or log replication).

### raft_step_down

```c
void raft_step_down(raft_node_t* node, uint64_t new_term);
```

Steps down to follower state with the given term.

### raft_send_heartbeats

```c
raft_status_t raft_send_heartbeats(raft_node_t* node);
```

Sends heartbeats to all peers (leader only).

### raft_receive_message

```c
raft_status_t raft_receive_message(raft_node_t* node,
                                    int32_t from_node,
                                    const void* msg,
                                    size_t msg_len);
```

Receives and dispatches an RPC message.

---

## Timer

### raft_reset_election_timer

```c
void raft_reset_election_timer(raft_node_t* node);
```

Resets the election timer with a new random timeout.

### raft_tick

```c
void raft_tick(raft_node_t* node, uint64_t elapsed_ms);
```

Advances time by the given milliseconds. Triggers elections or heartbeats as needed.

### raft_timer_seed

```c
void raft_timer_seed(uint32_t seed);
```

Seeds the random number generator for deterministic testing.

---

## Storage

### raft_storage_open

```c
raft_storage_t* raft_storage_open(const char* data_dir, bool sync_writes);
```

Opens or creates persistent storage in the given directory.

**Parameters:**
- `data_dir`: Directory path
- `sync_writes`: Whether to sync writes to disk

**Returns:**
- Storage handle, or NULL on failure

### raft_storage_close

```c
void raft_storage_close(raft_storage_t* storage);
```

Closes storage and frees resources.

### raft_storage_save_state

```c
raft_status_t raft_storage_save_state(raft_storage_t* storage,
                                       uint64_t current_term,
                                       int32_t voted_for);
```

Saves persistent state (term and vote).

### raft_storage_load_state

```c
raft_status_t raft_storage_load_state(raft_storage_t* storage,
                                       uint64_t* current_term,
                                       int32_t* voted_for);
```

Loads persistent state.

### raft_storage_append_entry

```c
raft_status_t raft_storage_append_entry(raft_storage_t* storage,
                                         const raft_entry_t* entry);
```

Appends a log entry to storage.

---

## Snapshot

### raft_snapshot_create

```c
raft_status_t raft_snapshot_create(const char* data_dir,
                                    uint64_t last_index,
                                    uint64_t last_term,
                                    const void* state_data,
                                    size_t state_len);
```

Creates a snapshot with the given state data.

### raft_snapshot_load

```c
raft_status_t raft_snapshot_load(const char* data_dir,
                                  raft_snapshot_meta_t* meta,
                                  void** state_data,
                                  size_t* state_len);
```

Loads a complete snapshot including state data.

### raft_snapshot_install

```c
raft_status_t raft_snapshot_install(raft_node_t* node,
                                     const raft_snapshot_meta_t* meta,
                                     const void* state_data,
                                     size_t state_len);
```

Installs a snapshot received from leader.

### raft_maybe_compact

```c
raft_status_t raft_maybe_compact(raft_node_t* node);
```

Checks if log compaction should be triggered and performs it.

### raft_set_snapshot_callback

```c
void raft_set_snapshot_callback(raft_node_t* node,
                                 raft_snapshot_cb callback,
                                 void* user_data);
```

Sets the callback for creating snapshot state data.

---

## Membership

### raft_add_node

```c
raft_status_t raft_add_node(raft_node_t* node, int32_t new_node_id);
```

Adds a new node to the cluster (leader only).

### raft_remove_node

```c
raft_status_t raft_remove_node(raft_node_t* node, int32_t node_id);
```

Removes a node from the cluster (leader only).

### raft_get_cluster_size

```c
int32_t raft_get_cluster_size(raft_node_t* node);
```

Gets the current cluster size.

### raft_is_voting_member

```c
bool raft_is_voting_member(raft_node_t* node, int32_t node_id);
```

Checks if a node is a voting member.

---

## Batch Operations

### raft_propose_batch

```c
raft_status_t raft_propose_batch(raft_node_t* node,
                                  const char** commands,
                                  const size_t* lens,
                                  size_t count,
                                  uint64_t* first_index);
```

Proposes multiple commands in a single batch.

### raft_apply_batch

```c
size_t raft_apply_batch(raft_node_t* node, size_t max_count);
```

Applies up to max_count committed entries. Returns number applied.

---

## ReadIndex

### raft_read_index

```c
raft_status_t raft_read_index(raft_node_t* node,
                               raft_read_cb callback,
                               void* ctx);
```

Requests a linearizable read. Callback is invoked when safe to serve.

**Parameters:**
- `node`: Raft node (must be leader)
- `callback`: Function to call when read is safe
- `ctx`: User context passed to callback

**Returns:**
- `RAFT_OK` if request was queued
- `RAFT_NOT_LEADER` if not leader

### raft_read_cancel_all

```c
void raft_read_cancel_all(raft_node_t* node);
```

Cancels all pending read requests (e.g., on leadership loss).

---

## Leadership Transfer

### raft_transfer_leadership

```c
raft_status_t raft_transfer_leadership(raft_node_t* node, int32_t target_id);
```

Transfers leadership to a specific node.

**Parameters:**
- `node`: Raft node (must be leader)
- `target_id`: Target node ID (-1 for any)

**Returns:**
- `RAFT_OK` if transfer started
- `RAFT_NOT_LEADER` if not leader

### raft_transfer_abort

```c
void raft_transfer_abort(raft_node_t* node);
```

Aborts an ongoing leadership transfer.

### raft_transfer_in_progress

```c
bool raft_transfer_in_progress(raft_node_t* node);
```

Checks if a leadership transfer is in progress.

---

## Parameters (`param.h`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `RAFT_ELECTION_TIMEOUT_MIN_MS` | 150 | Minimum election timeout |
| `RAFT_ELECTION_TIMEOUT_MAX_MS` | 300 | Maximum election timeout |
| `RAFT_HEARTBEAT_INTERVAL_MS` | 50 | Heartbeat interval |
| `RAFT_MAX_ENTRIES_PER_APPEND` | 100 | Max entries per AppendEntries |
| `RAFT_LOG_COMPACTION_THRESHOLD` | 10000 | Entries before compaction |
| `RAFT_AUTO_COMPACTION_THRESHOLD` | 1000 | Auto-compaction trigger |
| `RAFT_PREVOTE_ENABLED` | 1 | Enable PreVote |
