# Consensus (Raft)

A Raft consensus protocol implementation in C for learning distributed systems concepts.

## Overview

Raft is a consensus algorithm designed to be easy to understand. It provides the same fault-tolerance and performance as Paxos but is decomposed into relatively independent subproblems.

## Current Status

**Phase 1**: ✅ Complete - Basic structures and single-node operation
**Phase 2**: ✅ Complete - Leader election
**Phase 3**: ✅ Complete - Log replication
**Phase 4**: ✅ Complete - Persistence and recovery
**Phase 5**: ✅ Complete - Membership changes and optimization
**Phase 6**: ✅ Complete - Advanced Raft features

## Building

```bash
make test_phase1   # Build and run Phase 1 tests
make test_phase2   # Build and run Phase 2 tests
make test_phase3   # Build and run Phase 3 tests
make test_phase4   # Build and run Phase 4 tests
make test_phase5   # Build and run Phase 5 tests
make test_phase6   # Build and run Phase 6 tests
make clean         # Clean build artifacts
```

## Project Structure

```
consensus/
├── src/
│   ├── types.h          # Status codes and type definitions
│   ├── param.h          # Tunable parameters
│   ├── log.h/c          # Raft log management
│   ├── raft.h/c         # Core Raft node
│   ├── rpc.h            # RPC message structures
│   ├── election.h/c     # Leader election logic
│   ├── timer.h/c        # Timer management
│   ├── replication.h/c  # Log replication logic
│   ├── commit.h/c       # Commit index management
│   ├── crc32.h/c        # CRC32 checksum
│   ├── storage.h/c      # Persistent storage
│   ├── snapshot.h/c     # Snapshot support
│   ├── recovery.h/c     # Recovery from storage
│   ├── membership.h/c   # Cluster membership changes
│   ├── batch.h/c        # Batch operations
│   ├── read.h/c         # ReadIndex for linearizable reads
│   └── transfer.h/c     # Leadership transfer
├── tests/
│   └── unit/
│       ├── test_phase1.c  # Phase 1 tests (10 tests)
│       ├── test_phase2.c  # Phase 2 tests (10 tests)
│       ├── test_phase3.c  # Phase 3 tests (10 tests)
│       ├── test_phase4.c  # Phase 4 tests (10 tests)
│       ├── test_phase5.c  # Phase 5 tests (10 tests)
│       └── test_phase6.c  # Phase 6 tests (10 tests)
└── docs/              # Documentation
```

## Implemented Components

### Phase 1: Basic Structures (10 tests)

1. **Log Management (log.c)** - 200 lines
   - Log entry storage
   - Append/get operations
   - Truncation (before/after)
   - Term tracking

2. **Raft Node (raft.c)** - 170 lines
   - Node lifecycle (create/destroy/start/stop)
   - Single-node leader election
   - Command proposal
   - State machine apply callback

### Phase 2: Leader Election (10 tests)

1. **RPC Messages (rpc.h)** - 60 lines
   - RequestVote request/response
   - AppendEntries request/response
   - Message type enum

2. **Election Logic (election.c)** - 270 lines
   - Start election (become candidate)
   - Handle RequestVote RPC
   - Handle RequestVote response
   - Handle AppendEntries (heartbeat)
   - Step down on higher term
   - Send heartbeats

3. **Timer Management (timer.c)** - 70 lines
   - Random election timeout
   - Election timer tick
   - Heartbeat timer tick
   - Timer reset

### Phase 3: Log Replication (10 tests)

1. **Replication Logic (replication.c)** - 230 lines
   - Replicate log entries to peers
   - Handle AppendEntries with log entries
   - Log consistency check (prev_log_index/prev_log_term)
   - Handle AppendEntries response
   - Decrement next_index on mismatch

2. **Commit Management (commit.c)** - 90 lines
   - Advance commit index based on majority
   - Only commit entries from current term
   - Calculate majority match index

### Phase 4: Persistence and Recovery (10 tests)

1. **CRC32 Checksum (crc32.c)** - 50 lines
   - Data integrity verification
   - Incremental CRC calculation

2. **Persistent Storage (storage.c)** - 280 lines
   - Save/load current_term and voted_for
   - Append/truncate log entries
   - Sync writes to disk

3. **Snapshot Support (snapshot.c)** - 75 lines
   - Snapshot metadata management
   - Snapshot existence check

4. **Recovery (recovery.c)** - 100 lines
   - Recover state from storage
   - Recover log entries
   - Handle corruption detection

### Phase 5: Membership Changes and Optimization (10 tests)

1. **Snapshot (snapshot.c)** - 230 lines (expanded)
   - Full snapshot create/load
   - Log compaction
   - Snapshot installation for lagging nodes

2. **Membership Changes (membership.c)** - 230 lines
   - Single-step membership changes
   - Add/remove nodes dynamically
   - Configuration change as log entries

3. **Batch Operations (batch.c)** - 110 lines
   - Batch propose multiple commands
   - Batch apply committed entries
   - Reduced per-entry overhead

### Phase 6: Advanced Raft Features (10 tests)

1. **PreVote (election.c)** - 120 lines
   - Pre-election phase to prevent disruption
   - Partitioned nodes don't increment term
   - Log up-to-date check before real election

2. **ReadIndex (read.c)** - 120 lines
   - Linearizable read-only queries
   - Heartbeat-based leadership confirmation
   - No log entry for reads

3. **Auto Compaction (snapshot.c)** - 100 lines
   - Automatic log compaction trigger
   - User-provided snapshot callback
   - Configurable compaction threshold

4. **Leadership Transfer (transfer.c)** - 100 lines
   - Graceful leadership handoff
   - TimeoutNow message for immediate election
   - Target node selection

## Test Results

```
Phase 1: 10/10 tests passed
Phase 2: 10/10 tests passed
Phase 3: 10/10 tests passed
Phase 4: 10/10 tests passed
Phase 5: 10/10 tests passed
Phase 6: 10/10 tests passed
Integration (Partition): 6/6 tests passed
Integration (Chaos): 5/5 tests passed
Total: 71/71 tests passed
```

## Key Invariants

- **Election Safety**: At most one leader per term
- **Leader Append-Only**: Leader never overwrites or deletes log entries
- **Log Matching**: If two logs contain an entry with same index and term, logs are identical up to that index
- **Leader Completeness**: If an entry is committed, it will be present in all future leaders' logs

## Quick Start

```bash
git clone https://github.com/first-principles-cs/consensus.git
cd consensus
make test_phase6
./test_phase6
```

## Phases

- **Phase 1**: Basic structures and single-node operation ✅
- **Phase 2**: Leader election ✅
- **Phase 3**: Log replication ✅
- **Phase 4**: Persistence and recovery ✅
- **Phase 5**: Membership changes and optimization ✅
- **Phase 6**: Advanced Raft features ✅
- **Phase 7**: Documentation ✅
- **Phase 8**: Integration tests and benchmarks ✅
