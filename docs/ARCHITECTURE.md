# Raft Consensus Architecture

This document describes the architecture and design decisions of the Raft consensus implementation.

## Module Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                         │
│                    (State Machine, Callbacks)                    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Raft Core (raft.c)                       │
│              Node lifecycle, propose, apply, getters             │
└─────────────────────────────────────────────────────────────────┘
        │           │           │           │           │
        ▼           ▼           ▼           ▼           ▼
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│ Election │ │  Timer   │ │Replicat- │ │  Commit  │ │   Log    │
│          │ │          │ │   ion    │ │          │ │          │
└──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘
        │           │           │           │           │
        └───────────┴───────────┴───────────┴───────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Persistence Layer                           │
│              Storage, Snapshot, Recovery, CRC32                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        File System                               │
└─────────────────────────────────────────────────────────────────┘
```

## Module Relationships

### Core Modules

| Module | Depends On | Description |
|--------|------------|-------------|
| `raft.c` | log, timer, election | Main node implementation |
| `log.c` | types | In-memory log storage |
| `election.c` | raft, timer, log, rpc | Leader election logic |
| `timer.c` | param | Timer management |
| `replication.c` | raft, log, rpc | Log replication |
| `commit.c` | raft, log | Commit index management |

### Persistence Modules

| Module | Depends On | Description |
|--------|------------|-------------|
| `storage.c` | crc32, types | Persistent storage |
| `snapshot.c` | crc32, raft, log | Snapshot management |
| `recovery.c` | storage, raft | State recovery |
| `crc32.c` | - | Checksum calculation |

### Extension Modules

| Module | Depends On | Description |
|--------|------------|-------------|
| `membership.c` | raft, log | Cluster membership |
| `batch.c` | raft, log | Batch operations |
| `read.c` | raft | ReadIndex |
| `transfer.c` | raft, rpc, log | Leadership transfer |

## Data Flow

### Command Proposal Flow

```
Client                Leader              Followers
  │                     │                    │
  │  propose(cmd)       │                    │
  │────────────────────>│                    │
  │                     │                    │
  │                     │ append to log      │
  │                     │────────┐           │
  │                     │        │           │
  │                     │<───────┘           │
  │                     │                    │
  │                     │ AppendEntries      │
  │                     │───────────────────>│
  │                     │                    │ append to log
  │                     │                    │────────┐
  │                     │                    │        │
  │                     │                    │<───────┘
  │                     │ AE Response        │
  │                     │<───────────────────│
  │                     │                    │
  │                     │ update match_index │
  │                     │ advance commit     │
  │                     │────────┐           │
  │                     │        │           │
  │                     │<───────┘           │
  │                     │                    │
  │                     │ apply to SM        │
  │                     │────────┐           │
  │                     │        │           │
  │                     │<───────┘           │
  │                     │                    │
  │  success            │                    │
  │<────────────────────│                    │
```

### Election Flow

```
Follower            Candidate           Other Nodes
  │                     │                    │
  │ election timeout    │                    │
  │────────┐            │                    │
  │        │            │                    │
  │<───────┘            │                    │
  │                     │                    │
  │ become candidate    │                    │
  │────────────────────>│                    │
  │                     │                    │
  │                     │ increment term     │
  │                     │ vote for self      │
  │                     │────────┐           │
  │                     │        │           │
  │                     │<───────┘           │
  │                     │                    │
  │                     │ RequestVote        │
  │                     │───────────────────>│
  │                     │                    │
  │                     │ VoteResponse       │
  │                     │<───────────────────│
  │                     │                    │
  │                     │ majority votes?    │
  │                     │────────┐           │
  │                     │        │           │
  │                     │<───────┘           │
  │                     │                    │
  │                     │ become leader      │
  │                     │────────┐           │
  │                     │        │           │
  │                     │<───────┘           │
  │                     │                    │
  │                     │ send heartbeats    │
  │                     │───────────────────>│
```

## State Machine

### Node State Transitions

```
                    ┌─────────────────────────────────┐
                    │                                 │
                    ▼                                 │
              ┌──────────┐                           │
    ┌────────>│ FOLLOWER │<──────────────────┐      │
    │         └──────────┘                   │      │
    │              │                         │      │
    │              │ election timeout        │      │
    │              │ (PreVote enabled)       │      │
    │              ▼                         │      │
    │    ┌────────────────┐                  │      │
    │    │ PRE_CANDIDATE  │──────────────────┤      │
    │    └────────────────┘  higher term     │      │
    │              │                         │      │
    │              │ majority pre-votes      │      │
    │              ▼                         │      │
    │       ┌───────────┐                    │      │
    │       │ CANDIDATE │────────────────────┤      │
    │       └───────────┘  higher term       │      │
    │              │                         │      │
    │              │ majority votes          │      │
    │              ▼                         │      │
    │        ┌──────────┐                    │      │
    └────────│  LEADER  │────────────────────┘      │
  step down  └──────────┘  higher term              │
                   │                                │
                   │ leadership transfer            │
                   └────────────────────────────────┘
```

### Persistent State

The following state must be persisted before responding to RPCs:

| Field | Description | Updated When |
|-------|-------------|--------------|
| `current_term` | Latest term seen | On term change |
| `voted_for` | Candidate voted for in current term | On voting |
| `log[]` | Log entries | On append |

### Volatile State

| Field | Description | Initialized |
|-------|-------------|-------------|
| `commit_index` | Highest committed index | 0 |
| `last_applied` | Highest applied index | 0 |
| `next_index[]` | Next index to send (leader) | last_log_index + 1 |
| `match_index[]` | Highest replicated index (leader) | 0 |

## Design Decisions

### 1. Single-Step Membership Changes

We implement single-step membership changes (add/remove one node at a time) rather than joint consensus. This is simpler and sufficient for most use cases.

**Rationale:**
- Simpler implementation
- Easier to reason about
- Sufficient for gradual cluster changes

### 2. PreVote Extension

PreVote prevents partitioned nodes from disrupting the cluster by incrementing terms.

**How it works:**
1. Node starts pre-vote phase (doesn't increment term)
2. Asks peers if they would vote for it
3. Only starts real election if majority would vote
4. Partitioned nodes never get pre-votes, so never increment term

### 3. ReadIndex for Linearizable Reads

Instead of going through the log, reads confirm leadership via heartbeats.

**How it works:**
1. Leader records current commit index
2. Sends heartbeats to confirm leadership
3. Once majority responds, read is safe
4. No log entry needed for reads

### 4. Batch Operations

Batching reduces per-entry overhead for high-throughput scenarios.

**Benefits:**
- Fewer disk syncs
- Fewer network round trips
- Better throughput

### 5. Weak Symbols for Modularity

We use `__attribute__((weak))` for optional module dependencies.

**Benefits:**
- Modules can be compiled independently
- Tests can link only needed modules
- Gradual feature addition

## File Format

### State File (`raft_state.dat`)

```
┌────────────────────────────────────────┐
│ Magic (4 bytes): 0x52535441 ("RSTA")   │
├────────────────────────────────────────┤
│ Version (4 bytes): 1                   │
├────────────────────────────────────────┤
│ CRC32 (4 bytes)                        │
├────────────────────────────────────────┤
│ Padding (4 bytes)                      │
├────────────────────────────────────────┤
│ Current Term (8 bytes)                 │
├────────────────────────────────────────┤
│ Voted For (4 bytes)                    │
└────────────────────────────────────────┘
```

### Log File (`raft_log.dat`)

```
┌────────────────────────────────────────┐
│ Magic (4 bytes): 0x524C4F47 ("RLOG")   │
├────────────────────────────────────────┤
│ Version (4 bytes): 1                   │
├────────────────────────────────────────┤
│ Base Index (8 bytes)                   │
├────────────────────────────────────────┤
│ Base Term (8 bytes)                    │
├────────────────────────────────────────┤
│ Entry Count (8 bytes)                  │
├────────────────────────────────────────┤
│ Entry 1:                               │
│   CRC32 (4 bytes)                      │
│   Term (8 bytes)                       │
│   Index (8 bytes)                      │
│   Type (4 bytes)                       │
│   Command Length (4 bytes)             │
│   Command Data (variable)              │
├────────────────────────────────────────┤
│ Entry 2...                             │
└────────────────────────────────────────┘
```

### Snapshot File (`raft_snapshot.dat`)

```
┌────────────────────────────────────────┐
│ Magic (4 bytes): 0x52534E50 ("RSNP")   │
├────────────────────────────────────────┤
│ Version (4 bytes): 1                   │
├────────────────────────────────────────┤
│ CRC32 (4 bytes)                        │
├────────────────────────────────────────┤
│ Padding (4 bytes)                      │
├────────────────────────────────────────┤
│ Last Index (8 bytes)                   │
├────────────────────────────────────────┤
│ Last Term (8 bytes)                    │
├────────────────────────────────────────┤
│ State Length (8 bytes)                 │
├────────────────────────────────────────┤
│ State Data (variable)                  │
└────────────────────────────────────────┘
```

## Safety Properties

### Election Safety

At most one leader can be elected in a given term.

**Enforced by:**
- Each node votes at most once per term
- Candidate needs majority to win
- Vote is persisted before responding

### Leader Append-Only

A leader never overwrites or deletes entries in its log.

**Enforced by:**
- Leader only appends new entries
- Truncation only happens on followers with conflicting entries

### Log Matching

If two logs contain an entry with the same index and term, then the logs are identical in all entries up through that index.

**Enforced by:**
- Log consistency check in AppendEntries
- Entries are only appended if prev_log matches

### Leader Completeness

If a log entry is committed in a given term, that entry will be present in the logs of all leaders for all higher-numbered terms.

**Enforced by:**
- Voting restriction (candidate's log must be up-to-date)
- Commit requires majority replication

### State Machine Safety

If a server has applied a log entry at a given index to its state machine, no other server will ever apply a different log entry for that index.

**Enforced by:**
- Log Matching property
- Entries applied in order
