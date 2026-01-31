/**
 * storage.h - Persistent storage interface for Raft state
 *
 * Provides durable storage for:
 * - current_term and voted_for (must survive crashes)
 * - Log entries
 */

#ifndef RAFT_STORAGE_H
#define RAFT_STORAGE_H

#include "types.h"

#define RAFT_STATE_MAGIC    0x52414654  /* "RAFT" */
#define RAFT_LOG_MAGIC      0x524C4F47  /* "RLOG" */
#define RAFT_STORAGE_VERSION 1

typedef struct raft_storage raft_storage_t;

/**
 * Open persistent storage
 * @param data_dir Directory for storage files
 * @param sync_writes If true, fsync after each write
 * @return Storage handle or NULL on failure
 */
raft_storage_t* raft_storage_open(const char* data_dir, bool sync_writes);

/**
 * Close storage and release resources
 */
void raft_storage_close(raft_storage_t* storage);

/**
 * Save current_term and voted_for to stable storage
 * Must be called before responding to RPCs
 */
raft_status_t raft_storage_save_state(raft_storage_t* storage,
                                       uint64_t current_term,
                                       int32_t voted_for);

/**
 * Load current_term and voted_for from storage
 * Returns RAFT_NOT_FOUND if no state file exists
 */
raft_status_t raft_storage_load_state(raft_storage_t* storage,
                                       uint64_t* current_term,
                                       int32_t* voted_for);

/**
 * Append a log entry to persistent storage
 */
raft_status_t raft_storage_append_entry(raft_storage_t* storage,
                                         const raft_entry_t* entry);

/**
 * Truncate log after given index (for conflict resolution)
 * Removes all entries with index > after_index
 */
raft_status_t raft_storage_truncate_log(raft_storage_t* storage,
                                         uint64_t after_index);

/**
 * Sync all pending writes to disk
 */
raft_status_t raft_storage_sync(raft_storage_t* storage);

/**
 * Get the data directory path
 */
const char* raft_storage_get_dir(raft_storage_t* storage);

/**
 * Callback type for iterating over log entries during recovery
 */
typedef raft_status_t (*raft_log_iter_fn)(void* ctx,
                                           uint64_t term,
                                           uint64_t index,
                                           const char* command,
                                           size_t command_len);

/**
 * Iterate over all log entries in storage
 * Calls fn for each entry in order
 */
raft_status_t raft_storage_iterate_log(raft_storage_t* storage,
                                        raft_log_iter_fn fn,
                                        void* ctx);

/**
 * Get log metadata (base_index, base_term, entry_count)
 */
raft_status_t raft_storage_get_log_info(raft_storage_t* storage,
                                         uint64_t* base_index,
                                         uint64_t* base_term,
                                         uint64_t* entry_count);

#endif /* RAFT_STORAGE_H */
