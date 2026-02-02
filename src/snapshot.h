/**
 * snapshot.h - Snapshot support for Raft log compaction
 *
 * Provides snapshot creation, loading, and installation for log compaction.
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

/**
 * Create a snapshot with the given state data
 * This saves the snapshot to disk and can be used for log compaction
 *
 * @param data_dir Directory to save snapshot
 * @param last_index Index of last log entry included in snapshot
 * @param last_term Term of last log entry included in snapshot
 * @param state_data Application state data to snapshot
 * @param state_len Length of state data
 * @return RAFT_OK on success
 */
raft_status_t raft_snapshot_create(const char* data_dir,
                                    uint64_t last_index,
                                    uint64_t last_term,
                                    const void* state_data,
                                    size_t state_len);

/**
 * Load a complete snapshot including state data
 * Caller must free *state_data when done
 *
 * @param data_dir Directory containing snapshot
 * @param meta Output: snapshot metadata
 * @param state_data Output: allocated buffer with state data
 * @param state_len Output: length of state data
 * @return RAFT_OK on success, RAFT_NOT_FOUND if no snapshot
 */
raft_status_t raft_snapshot_load(const char* data_dir,
                                  raft_snapshot_meta_t* meta,
                                  void** state_data,
                                  size_t* state_len);

/**
 * Install a snapshot received from leader
 * This replaces the current state with the snapshot
 *
 * @param node Raft node to install snapshot on
 * @param meta Snapshot metadata
 * @param state_data Snapshot state data
 * @param state_len Length of state data
 * @return RAFT_OK on success
 */
raft_status_t raft_snapshot_install(raft_node_t* node,
                                     const raft_snapshot_meta_t* meta,
                                     const void* state_data,
                                     size_t state_len);

/**
 * Callback for creating snapshot state data
 * Called when auto-compaction triggers a snapshot
 *
 * @param node Raft node
 * @param state_data Output: allocated buffer with state data (caller frees)
 * @param state_len Output: length of state data
 * @param user_data User context
 * @return RAFT_OK on success
 */
typedef raft_status_t (*raft_snapshot_cb)(raft_node_t* node,
                                           void** state_data,
                                           size_t* state_len,
                                           void* user_data);

/**
 * Set the snapshot callback for auto-compaction
 */
void raft_set_snapshot_callback(raft_node_t* node, raft_snapshot_cb callback,
                                 void* user_data);

/**
 * Check if log compaction should be triggered and perform it
 * Called after applying entries
 *
 * @param node Raft node
 * @return RAFT_OK if compaction was performed or not needed
 */
raft_status_t raft_maybe_compact(raft_node_t* node);

/**
 * Get the number of entries since last snapshot
 */
uint64_t raft_entries_since_snapshot(raft_node_t* node);

#endif /* RAFT_SNAPSHOT_H */
