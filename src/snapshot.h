/**
 * snapshot.h - Snapshot support for Raft log compaction
 *
 * Minimal implementation for Phase 4. Full implementation in Phase 5.
 */

#ifndef RAFT_SNAPSHOT_H
#define RAFT_SNAPSHOT_H

#include "types.h"

#define RAFT_SNAPSHOT_MAGIC   0x52534E50  /* "RSNP" */
#define RAFT_SNAPSHOT_VERSION 1
#define RAFT_SNAPSHOT_FILE    "raft_snapshot.dat"

/**
 * Snapshot metadata
 */
typedef struct raft_snapshot_meta {
    uint64_t last_index;  /* Index of last entry included in snapshot */
    uint64_t last_term;   /* Term of last entry included in snapshot */
} raft_snapshot_meta_t;

/**
 * Check if a snapshot exists in the data directory
 */
bool raft_snapshot_exists(const char* data_dir);

/**
 * Load snapshot metadata (not the full snapshot data)
 * Returns RAFT_NOT_FOUND if no snapshot exists
 */
raft_status_t raft_snapshot_load_meta(const char* data_dir,
                                       raft_snapshot_meta_t* meta);

#endif /* RAFT_SNAPSHOT_H */
