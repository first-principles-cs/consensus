# Raft Consensus Tutorial

This tutorial explains the Raft consensus algorithm and how it's implemented in this codebase.

## What is Consensus?

Consensus is the problem of getting multiple computers to agree on a single value. In distributed systems, this is fundamental for:

- **Replicated state machines**: All replicas execute the same commands in the same order
- **Leader election**: Choosing a single coordinator
- **Distributed locks**: Ensuring mutual exclusion

## Why Raft?

Raft was designed to be understandable. Unlike Paxos, Raft:

- Decomposes consensus into independent subproblems
- Reduces the number of states to consider
- Uses a strong leader to simplify replication

## The Three Subproblems

Raft divides consensus into three relatively independent subproblems:

1. **Leader Election**: How to choose a leader
2. **Log Replication**: How to replicate commands
3. **Safety**: How to ensure correctness

---

## Part 1: Leader Election

### Roles

Every node is in one of three states:

```
┌──────────┐     ┌───────────┐     ┌──────────┐
│ FOLLOWER │────>│ CANDIDATE │────>│  LEADER  │
└──────────┘     └───────────┘     └──────────┘
      ▲                                  │
      └──────────────────────────────────┘
```

- **Follower**: Passive, responds to RPCs
- **Candidate**: Actively seeking votes
- **Leader**: Handles all client requests

### Terms

Time is divided into **terms** of arbitrary length:

```
Term 1        Term 2        Term 3        Term 4
┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐
│ Election│   │ Election│   │ Election│   │ Election│
│ Leader 1│   │ (split) │   │ Leader 3│   │ Leader 3│
│ Normal  │   │         │   │ Normal  │   │ Normal  │
└─────────┘   └─────────┘   └─────────┘   └─────────┘
```

- Each term begins with an election
- At most one leader per term
- Terms act as a logical clock

### Election Process

1. **Timeout**: Follower doesn't hear from leader
2. **Become Candidate**: Increment term, vote for self
3. **Request Votes**: Send RequestVote to all peers
4. **Win Election**: Receive majority of votes
5. **Become Leader**: Start sending heartbeats

```c
// Starting an election (simplified)
raft_status_t raft_start_election(raft_node_t* node) {
    node->role = RAFT_CANDIDATE;
    node->persistent.current_term++;
    node->persistent.voted_for = node->node_id;
    node->votes_received = 1;  // Vote for self

    // Send RequestVote to all peers
    for (int i = 0; i < node->num_nodes; i++) {
        if (i != node->node_id) {
            send_request_vote(node, i);
        }
    }

    return RAFT_OK;
}
```

### Voting Rules

A node grants a vote if:

1. Candidate's term >= voter's term
2. Voter hasn't voted in this term (or voted for this candidate)
3. Candidate's log is at least as up-to-date

```c
// Handling a vote request (simplified)
bool should_grant_vote(raft_node_t* node, raft_request_vote_t* req) {
    // Rule 1: Term check
    if (req->term < node->persistent.current_term) {
        return false;
    }

    // Rule 2: Already voted?
    if (node->persistent.voted_for != -1 &&
        node->persistent.voted_for != req->candidate_id) {
        return false;
    }

    // Rule 3: Log up-to-date?
    return is_log_up_to_date(node, req->last_log_term, req->last_log_index);
}
```

### Exercise 1: Election Timeout

**Question**: Why is the election timeout randomized?

<details>
<summary>Answer</summary>

Randomized timeouts prevent split votes. If all nodes had the same timeout, they would all become candidates simultaneously, split the vote, and repeat forever.

By randomizing (e.g., 150-300ms), one node usually times out first and wins the election before others start.
</details>

---

## Part 2: Log Replication

### The Log

The log is an ordered sequence of commands:

```
Index:  1      2      3      4      5
      ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐
Term: │ 1  │ │ 1  │ │ 2  │ │ 2  │ │ 3  │
      ├────┤ ├────┤ ├────┤ ├────┤ ├────┤
Cmd:  │ x=1│ │ y=2│ │ x=3│ │ z=4│ │ y=5│
      └────┘ └────┘ └────┘ └────┘ └────┘
```

Each entry has:
- **Index**: Position in log (1-based)
- **Term**: When entry was created
- **Command**: The actual data

### Replication Process

1. **Client sends command to leader**
2. **Leader appends to local log**
3. **Leader sends AppendEntries to followers**
4. **Followers append and acknowledge**
5. **Leader commits when majority replicate**
6. **Leader applies to state machine**
7. **Leader responds to client**

```c
// Proposing a command (simplified)
raft_status_t raft_propose(raft_node_t* node, const char* cmd, size_t len) {
    if (node->role != RAFT_LEADER) {
        return RAFT_NOT_LEADER;
    }

    // Append to local log
    uint64_t index;
    raft_log_append(node->log, node->persistent.current_term, cmd, len, &index);

    // Replicate to followers
    for (int i = 0; i < node->num_nodes; i++) {
        if (i != node->node_id) {
            send_append_entries(node, i);
        }
    }

    return RAFT_OK;
}
```

### Log Consistency

Raft maintains the **Log Matching Property**:

> If two logs contain an entry with the same index and term, then the logs are identical in all entries up through that index.

This is enforced by the consistency check in AppendEntries:

```c
// Consistency check (simplified)
bool check_consistency(raft_log_t* log, uint64_t prev_index, uint64_t prev_term) {
    if (prev_index == 0) {
        return true;  // Empty log always matches
    }

    const raft_entry_t* entry = raft_log_get(log, prev_index);
    if (!entry) {
        return false;  // Missing entry
    }

    return entry->term == prev_term;
}
```

### Commit Index

An entry is **committed** when it's replicated on a majority of servers.

```c
// Advancing commit index (simplified)
void advance_commit_index(raft_node_t* node) {
    // Find the highest index replicated on majority
    for (uint64_t n = node->volatile_state.commit_index + 1;
         n <= raft_log_last_index(node->log); n++) {

        int count = 1;  // Leader has it
        for (int i = 0; i < node->num_nodes; i++) {
            if (i != node->node_id &&
                node->leader_state.match_index[i] >= n) {
                count++;
            }
        }

        if (count > node->num_nodes / 2) {
            // Only commit entries from current term
            const raft_entry_t* entry = raft_log_get(node->log, n);
            if (entry->term == node->persistent.current_term) {
                node->volatile_state.commit_index = n;
            }
        }
    }
}
```

### Exercise 2: Why Current Term Only?

**Question**: Why does the leader only commit entries from its current term?

<details>
<summary>Answer</summary>

Consider this scenario:

```
Time 1: Leader S1 (term 2) replicates entry at index 2 to S2
Time 2: S1 crashes, S5 becomes leader (term 3)
Time 3: S5 crashes, S1 becomes leader (term 4)
Time 4: S1 replicates index 2 to S3 (now on majority)
```

If S1 commits index 2 (term 2) and then crashes, S5 could become leader and overwrite it because S5's log might be "more up-to-date" by term.

By only committing current-term entries, we ensure the Leader Completeness property: committed entries appear in all future leaders' logs.
</details>

---

## Part 3: Safety

### Election Restriction

A candidate's log must be at least as up-to-date as any voter's log.

"Up-to-date" means:
1. Higher last term, OR
2. Same last term and >= last index

```c
bool is_log_up_to_date(raft_node_t* node, uint64_t last_term, uint64_t last_index) {
    uint64_t my_last_term = raft_log_last_term(node->log);
    uint64_t my_last_index = raft_log_last_index(node->log);

    if (last_term > my_last_term) return true;
    if (last_term == my_last_term && last_index >= my_last_index) return true;
    return false;
}
```

### Persistence

Before responding to any RPC, these must be persisted:

- `current_term`
- `voted_for`
- `log[]`

This ensures safety across crashes.

### Exercise 3: What Could Go Wrong?

**Question**: What happens if `voted_for` is not persisted?

<details>
<summary>Answer</summary>

A node could vote twice in the same term:

1. Node A votes for candidate B in term 5
2. Node A crashes and restarts
3. Node A (forgetting its vote) votes for candidate C in term 5
4. Both B and C could get a majority!

This violates Election Safety (at most one leader per term).
</details>

---

## Part 4: Advanced Topics

### PreVote

PreVote prevents partitioned nodes from disrupting the cluster.

**Problem**: A partitioned node keeps timing out and incrementing its term. When it rejoins, its high term causes the leader to step down.

**Solution**: Before starting a real election, ask peers if they would vote. Only proceed if majority would vote.

```c
raft_status_t raft_start_pre_vote(raft_node_t* node) {
    node->role = RAFT_PRE_CANDIDATE;
    // Don't increment term yet!

    // Ask peers if they would vote
    for (int i = 0; i < node->num_nodes; i++) {
        if (i != node->node_id) {
            send_pre_vote(node, i, node->persistent.current_term + 1);
        }
    }

    return RAFT_OK;
}
```

### ReadIndex

For linearizable reads without going through the log:

1. Leader records current commit index
2. Leader sends heartbeats to confirm leadership
3. Once majority responds, read is safe

```c
raft_status_t raft_read_index(raft_node_t* node, raft_read_cb callback, void* ctx) {
    if (node->role != RAFT_LEADER) {
        return RAFT_NOT_LEADER;
    }

    // Record commit index and wait for heartbeat acks
    queue_read_request(node, node->volatile_state.commit_index, callback, ctx);

    return RAFT_OK;
}
```

### Log Compaction

Logs can't grow forever. Snapshots capture state at a point in time:

1. Application creates snapshot of state machine
2. Raft saves snapshot with last included index/term
3. Log entries before snapshot can be discarded

```c
raft_status_t raft_maybe_compact(raft_node_t* node) {
    if (raft_log_count(node->log) < THRESHOLD) {
        return RAFT_OK;  // Not enough entries
    }

    // Get state from application
    void* state;
    size_t len;
    snapshot_callback(node, &state, &len);

    // Save snapshot
    raft_snapshot_create(node->data_dir,
                         node->volatile_state.last_applied,
                         get_term_at(node, node->volatile_state.last_applied),
                         state, len);

    // Truncate log
    raft_log_truncate_before(node->log, node->volatile_state.last_applied);

    return RAFT_OK;
}
```

---

## Practice Exercises

### Exercise 4: Trace an Election

Given a 5-node cluster where node 0 is leader in term 3:

1. Node 0 crashes
2. Node 2 times out first
3. Trace the election process

<details>
<summary>Solution</summary>

1. Node 2 becomes candidate, increments term to 4
2. Node 2 votes for itself
3. Node 2 sends RequestVote to nodes 1, 3, 4
4. Nodes 1, 3, 4 check:
   - Term 4 > their term 3 ✓
   - Haven't voted in term 4 ✓
   - Node 2's log up-to-date? (depends on log state)
5. If nodes 1 and 3 vote yes, node 2 has 3 votes (majority of 5)
6. Node 2 becomes leader, sends heartbeats
</details>

### Exercise 5: Log Divergence

Consider this scenario:

```
Leader (term 2):  [1:1] [2:1] [3:2] [4:2]
Follower:         [1:1] [2:1] [3:1]
```

What happens when the leader sends AppendEntries?

<details>
<summary>Solution</summary>

1. Leader sends AE with prev_log_index=3, prev_log_term=2
2. Follower checks: entry at index 3 has term 1, not 2
3. Follower rejects (success=false)
4. Leader decrements next_index to 3
5. Leader sends AE with prev_log_index=2, prev_log_term=1
6. Follower checks: entry at index 2 has term 1 ✓
7. Follower accepts, truncates index 3, appends new entries
8. Follower now has: [1:1] [2:1] [3:2] [4:2]
</details>

### Exercise 6: Implement a KV Store

Using this Raft implementation, design a simple key-value store:

1. What commands would you support?
2. How would you handle reads?
3. How would you serialize commands?

<details>
<summary>Hints</summary>

Commands:
- `SET key value`
- `DELETE key`
- `GET key` (use ReadIndex for linearizable reads)

Serialization:
```c
typedef struct {
    uint8_t op;      // 0=SET, 1=DELETE
    uint16_t key_len;
    uint16_t val_len;
    char data[];     // key followed by value
} kv_command_t;
```

Apply callback:
```c
void apply_kv(raft_node_t* node, const raft_entry_t* entry, void* ud) {
    kv_store_t* store = (kv_store_t*)ud;
    kv_command_t* cmd = (kv_command_t*)entry->command;

    char* key = cmd->data;
    char* val = cmd->data + cmd->key_len;

    if (cmd->op == 0) {
        kv_set(store, key, cmd->key_len, val, cmd->val_len);
    } else {
        kv_delete(store, key, cmd->key_len);
    }
}
```
</details>

---

## Summary

Raft achieves consensus through:

1. **Leader Election**: One leader per term, elected by majority
2. **Log Replication**: Leader replicates entries, commits on majority
3. **Safety**: Persistence, election restriction, commit rules

Key invariants:
- **Election Safety**: At most one leader per term
- **Leader Append-Only**: Leader never overwrites entries
- **Log Matching**: Same index+term means identical prefix
- **Leader Completeness**: Committed entries in future leaders
- **State Machine Safety**: Same index means same command

## Further Reading

- [Raft Paper](https://raft.github.io/raft.pdf)
- [Raft Visualization](https://raft.github.io/)
- [TLA+ Specification](https://github.com/ongardie/raft.tla)