# Consensus (Raft)

A Raft consensus protocol implementation in C for learning distributed systems concepts.

## Overview

Raft is a consensus algorithm designed to be easy to understand. It provides the same fault-tolerance and performance as Paxos but is decomposed into relatively independent subproblems.

## Current Status

**Phase 1**: ✅ Complete - Basic structures and single-node operation
**Phase 2**: ✅ Complete - Leader election
**Phase 3**: 📋 Planned - Log replication
**Phase 4**: 📋 Planned - Persistence and recovery
**Phase 5**: 📋 Planned - Membership changes and optimization

## Building

```bash
make test_phase1   # Build and run Phase 1 tests
make test_phase2   # Build and run Phase 2 tests
make clean         # Clean build artifacts
```

## Project Structure

```
consensus/
├── src/
│   ├── types.h        # Status codes and type definitions
│   ├── param.h        # Tunable parameters
│   ├── log.h/c        # Raft log management
│   ├── raft.h/c       # Core Raft node
│   ├── rpc.h          # RPC message structures
│   ├── election.h/c   # Leader election logic
│   └── timer.h/c      # Timer management
├── tests/
│   └── unit/
│       ├── test_phase1.c  # Phase 1 tests (10 tests)
│       └── test_phase2.c  # Phase 2 tests (10 tests)
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

## Test Results

```
Phase 1: 10/10 tests passed
Phase 2: 10/10 tests passed
Total: 20/20 tests passed
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
make test_phase2
./test_phase2
```

## Phases

- **Phase 1**: Basic structures and single-node operation ✅
- **Phase 2**: Leader election ✅
- **Phase 3**: Log replication
- **Phase 4**: Persistence and recovery
- **Phase 5**: Membership changes and optimization
